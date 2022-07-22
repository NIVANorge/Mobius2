

#include "../dependencies/kaleidoscope/KaleidoscopeJIT.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"


#include "llvm_jit.h"

//TODO: replace this with own error handler
static llvm::ExitOnError ExitOnErr;

bool   llvm_initialized = false;
static std::unique_ptr<llvm::orc::KaleidoscopeJIT> global_jit;

void
initialize_llvm() {
	if(llvm_initialized) return;
	
	llvm::InitializeNativeTarget();
	llvm::InitializeNativeTargetAsmPrinter();
	llvm::InitializeNativeTargetAsmParser();
	
	// TODO: we don't want ExitOnErr, instead log error message first.
	global_jit = ExitOnErr(llvm::orc::KaleidoscopeJIT::Create());
	
	llvm_initialized = true;
}

struct
LLVM_Module_Data {
	std::unique_ptr<llvm::LLVMContext>         context;
	std::unique_ptr<llvm::Module>              module;
	std::unique_ptr<llvm::IRBuilder<>>         builder;
	std::unique_ptr<llvm::legacy::FunctionPassManager> optimizer;           //TODO: find out why this is legacy, and can we replace it?
};

LLVM_Module_Data *
create_llvm_module() {
	
	if(!llvm_initialized)
		fatal_error(Mobius_Error::internal, "Tried to create LLVM module before the JIT was initialized.");
	
	LLVM_Module_Data *data = new LLVM_Module_Data();
	
	data->context = std::make_unique<llvm::LLVMContext>();
	data->module  = std::make_unique<llvm::Module>("Mobius2 batch JIT", *data->context);
	data->module->setDataLayout(global_jit->getDataLayout());

	// Create a new builder for the module.
	data->builder = std::make_unique<llvm::IRBuilder<>>(*data->context);

	// Create a new pass manager attached to it.
	data->optimizer = std::make_unique<llvm::legacy::FunctionPassManager>(data->module.get());

	
	// TODO: rabbit hole on optimizations and see how the affect complex models!
	
	// Do simple "peephole" optimizations and bit-twiddling optzns.
	data->optimizer->add(llvm::createInstructionCombiningPass());
	// Reassociate expressions.
	data->optimizer->add(llvm::createReassociatePass());
	// Eliminate Common SubExpressions.
	data->optimizer->add(llvm::createGVNPass());
	// Simplify the control flow graph (deleting unreachable blocks, etc).
	data->optimizer->add(llvm::createCFGSimplificationPass());

	data->optimizer->doInitialization();
	
	return data;
}

void
jit_compile_module(LLVM_Module_Data *data) {
	auto rt = global_jit->getMainJITDylib().createResourceTracker();                           //TODO: keep this around so that we can   ExitOnErr(rt->remove());  to free memory used by jit.
	auto tsm = llvm::orc::ThreadSafeModule(std::move(data->module), std::move(data->context));
	//TODO: don't exitonerr, instead log.
	ExitOnErr(global_jit->addModule(std::move(tsm), rt));
	
	//TODO: Put a flag on the data to signify that it is now compiled (can't add more stuff to it), and properly error handle in other procs.
}

void
free_llvm_module(LLVM_Module_Data *data) {
	// TODO!!!
	delete data;
}

batch_function *
get_jitted_batch_function(const std::string &fun_name) {
	warning_print("Lookup of function from jitted module.\n");
	
	// TODO: don't do ExitOnErr, instead log error first.
	auto symbol = ExitOnErr(global_jit->lookup(fun_name));
	// Get the symbol's address and cast it to the right type so we can call it as a native function.
	batch_function *fun_ptr = (batch_function *)(intptr_t)symbol.getAddress();

	return fun_ptr;
}

/*
	Batch function is of the form
	
	void evaluate_batch(Parameter_Value *parameters, double *series, double *state_vars, double *solver_workspace);
	
	since there are no union types in llvm, we treat Parameter_Value as a double and use bitcast when we want it as other types.
	
	     (to be expanded upon with datetime info etc).
*/

struct Scope_Local_Vars;
llvm::Value *build_expression_ir(Math_Expr_FT *expr, Scope_Local_Vars *scope, std::vector<llvm::Value *> &args, LLVM_Module_Data *data);


void
jit_add_batch(Math_Expr_FT *batch_code, const std::string &fun_name, LLVM_Module_Data *data) {
	
	auto double_ptr_ty = llvm::Type::getDoublePtrTy(*data->context);
	
	std::vector<llvm::Type *> arg_types = {
		double_ptr_ty, double_ptr_ty, double_ptr_ty, double_ptr_ty,
		};
		
	llvm::FunctionType *fun_type = llvm::FunctionType::get(llvm::Type::getVoidTy(*data->context), arg_types, false);
	llvm::Function *fun = llvm::Function::Create(fun_type, llvm::Function::ExternalLinkage, fun_name, data->module.get());
	
	// Hmm, is it important to set the argument names, or could we skip it?
	const char *argnames[4] = {"parameters", "series", "state_vars", "solver_workspace"};
	std::vector<llvm::Value *> args;
	int idx = 0;
	for(auto &arg : fun->args()) {
		arg.setName(argnames[idx++]);
		args.push_back(&arg);
	}
	
	llvm::BasicBlock *basic_block = llvm::BasicBlock::Create(*data->context, "entry", fun);
	data->builder->SetInsertPoint(basic_block);
	
	warning_print("Begin llvm function creation\n");
	
	build_expression_ir(batch_code, nullptr, args, data);
	data->builder->CreateRetVoid();
	
	warning_print("Created llvm function\n");
	
	std::string errmsg = "";
	llvm::raw_string_ostream errstream(errmsg);
	errstream.reserveExtraSpace(512);
	bool errors = llvm::verifyFunction(*fun, &errstream);
	if(errors) {
		//TODO: clean up the llvm context stuff. This is needed if the error exit does not close the process, but if we instead are in the IDE or python and we just catch an exception.
		fatal_error(Mobius_Error::internal, "LLVM function verification failed for function \"", fun_name, "\" : ", errstream.str(), " .");
	}
	
	warning_print("Verification done.\n");
	
	data->optimizer->run(*fun);
	
	warning_print("Optimization done.\n");
}


struct
Scope_Local_Vars {
	s32 scope_id;
	Scope_Local_Vars *scope_up;
	std::vector<llvm::Value *> local_vars;
};

llvm::Value *
get_local_var(Scope_Local_Vars *scope, s32 index, s32 scope_id) {
	if(!scope)
		fatal_error(Mobius_Error::internal, "Mis-counting of scopes in ir building.");
	while(scope->scope_id != scope_id) {
		scope = scope->scope_up;
		if(!scope)
			fatal_error(Mobius_Error::internal, "Mis-counting of scopes in ir building.");
	}
	if(index >= scope->local_vars.size())
		fatal_error(Mobius_Error::internal, "Mis-counting of local variables in ir building.");
	return scope->local_vars[index];
}

llvm::Value *build_unary_ir(llvm::Value *arg, Value_Type type, Token_Type oper, LLVM_Module_Data *data) {
	llvm::Value *result = nullptr;
	if((char)oper == '-') {
		if(type == Value_Type::boolean) fatal_error(Mobius_Error::internal, "build_unary_ir() minus on boolean.");
		else if(type == Value_Type::real) result = data->builder->CreateFNeg(arg, "fnegtemp");
		else if(type == Value_Type::integer) result = data->builder->CreateNeg(arg, "negtemp");
	} else if((char)oper == '!') {
		if(type != Value_Type::boolean) fatal_error(Mobius_Error::internal, "build_unary_ir() negation on non-boolean.");
		result = data->builder->CreateNot(arg, "nottemp");
	} else
		fatal_error(Mobius_Error::internal, "apply_unary() unhandled operator ", name(oper), " .");
	return result;
}

llvm::Value *build_binary_ir(llvm::Value *lhs, Value_Type type1, llvm::Value *rhs, Value_Type type2, Token_Type oper, LLVM_Module_Data *data) {
	llvm::Value *result;
	char op = (char)oper;
	if(op != '^' && type1 != type2) fatal_error(Mobius_Error::internal, "Mismatching types in build_binary_ir(). lhs: ", name(type1), ", rhs: ", name(type2), "."); //this should have been eliminated at a different stage.
	
	//TODO: should probably do type checking here too, but it SHOULD be correct from the function tree resolution already.
	
	//note FCmpU** means that either argument could be nan. FCmpO** would mean that they could not be, but we can't guarantee that in general (?)
	if(op == '|')      result = data->builder->CreateLogicalOr(lhs, rhs, "ortemp");
	else if(op == '&') result = data->builder->CreateLogicalAnd(lhs, rhs, "andtemp");
	else if(op == '<') {
		if(type1 == Value_Type::integer)   result = data->builder->CreateICmpULT(lhs, rhs, "lttemp");
		else if(type1 == Value_Type::real) result = data->builder->CreateFCmpULT(lhs, rhs, "flttemp");
	} else if(oper == Token_Type::leq) {
		if(type1 == Value_Type::integer)   result = data->builder->CreateICmpULE(lhs, rhs, "letemp");
		else if(type1 == Value_Type::real) result = data->builder->CreateFCmpULE(lhs, rhs, "fletemp");
	} else if(op == '>') {
		if(type1 == Value_Type::integer)   result = data->builder->CreateICmpUGT(lhs, rhs, "gttemp");
		else if(type1 == Value_Type::real) result = data->builder->CreateFCmpUGT(lhs, rhs, "fgttemp");
	} else if(oper == Token_Type::geq) {
		if(type1 == Value_Type::integer)   result = data->builder->CreateICmpUGE(lhs, rhs, "uetemp");
		else if(type1 == Value_Type::real) result = data->builder->CreateFCmpUGE(lhs, rhs, "fuetemp");
	} else if(oper == Token_Type::eq) {
		if(type1 == Value_Type::integer)   result = data->builder->CreateICmpEQ(lhs, rhs, "eqtemp");
		else if(type1 == Value_Type::real) result = data->builder->CreateFCmpUEQ(lhs, rhs, "feqtemp");
	} else if(oper == Token_Type::neq) {
		if(type1 == Value_Type::integer)   result = data->builder->CreateICmpNE(lhs, rhs, "netemp");
		else if(type1 == Value_Type::real) result = data->builder->CreateFCmpUNE(lhs, rhs, "fnetemp");
	} else if(op == '+') {
		if(type1 == Value_Type::integer)   result = data->builder->CreateAdd(lhs, rhs, "addtemp");
		else if(type1 == Value_Type::real) result = data->builder->CreateFAdd(lhs, rhs, "faddtemp");
	} else if(op == '-') {
		if(type1 == Value_Type::integer)   result = data->builder->CreateSub(lhs, rhs, "subtemp");
		else if(type1 == Value_Type::real) result = data->builder->CreateFSub(lhs, rhs, "fsubtemp");
	} else if(op == '*') {
		if(type1 == Value_Type::integer)   result = data->builder->CreateMul(lhs, rhs, "multemp");
		else if(type1 == Value_Type::real) result = data->builder->CreateFMul(lhs, rhs, "fmultemp");
	} else if(op == '/') {
		if(type1 == Value_Type::integer)   result = data->builder->CreateSDiv(lhs, rhs, "divtemp");    //TODO: what is difference between sdiv and exactsdiv ?
		else if(type1 == Value_Type::real) result = data->builder->CreateFDiv(lhs, rhs, "fdivtemp");
	} else if(op == '^') {
		std::vector<llvm::Value *> args = { lhs, rhs };
		if(type2 == Value_Type::integer) {
			// TODO: Do we really want to do the trunc? There is no version of the intrinsic that takes int64. But likelihood of causing errors is very low..
			rhs = data->builder->CreateTrunc(rhs, llvm::Type::getInt32Ty(*data->context));
			std::vector<llvm::Type *> arg_types = { llvm::Type::getDoubleTy(*data->context), llvm::Type::getInt32Ty(*data->context) };
			llvm::Function *fun = llvm::Intrinsic::getDeclaration(data->module.get(), llvm::Intrinsic::powi, arg_types);
			result = data->builder->CreateCall(fun, args);
		}
		else if(type2 == Value_Type::real) {
			std::vector<llvm::Type *> arg_types = { llvm::Type::getDoubleTy(*data->context) };
			llvm::Function *fun = llvm::Intrinsic::getDeclaration(data->module.get(), llvm::Intrinsic::pow, arg_types);
			result = data->builder->CreateCall(fun, args);
		}
	} else
		fatal_error(Mobius_Error::internal, "build_binary_ir() unhandled operator ", name(oper), " .");
	
	return result;
}

llvm::Value *build_cast_ir(llvm::Value *val, Value_Type from_type, Value_Type to_type, LLVM_Module_Data *data) {
	if(from_type == Value_Type::real) {
		fatal_error(Mobius_Error::internal, "Cast from real to other type not implemented");
	} else if (from_type == Value_Type::integer) {
		if(to_type == Value_Type::real) {
			return data->builder->CreateSIToFP(val, llvm::Type::getDoubleTy(*data->context), "castitof");
		}
		fatal_error(Mobius_Error::internal, "Cast from integer to boolean type not implemented");
	} else if (from_type == Value_Type::boolean) {
		if(to_type == Value_Type::integer)
			return data->builder->CreateSExt(val, llvm::Type::getInt64Ty(*data->context), "castbtoi");
		else if(to_type == Value_Type::real)
			return data->builder->CreateUIToFP(val, llvm::Type::getDoubleTy(*data->context), "castbtof");
	}
	fatal_error(Mobius_Error::internal, "Unimplemented cast in ir building.");
}



llvm::Value *build_expression_ir(Math_Expr_FT *expr, Scope_Local_Vars *locals, std::vector<llvm::Value *> &args, LLVM_Module_Data *data) {
	
	if(!expr)
		fatal_error(Mobius_Error::internal, "Got a nullptr expression in build_expression_ir().");
	
	warning_print("ir gen, ", name(expr->expr_type), "\n");
	
	switch(expr->expr_type) {
		case Math_Expr_Type::block : {
			auto block = reinterpret_cast<Math_Block_FT *>(expr);
			llvm::Value *result = nullptr;
			Scope_Local_Vars new_locals;
			new_locals.scope_id = block->unique_block_id;
			new_locals.scope_up = locals;
			new_locals.local_vars.resize(block->n_locals);
			if(!block->is_for_loop) {
				int index = 0;
				for(auto sub_expr : expr->exprs) {
					result = build_expression_ir(sub_expr, &new_locals, args, data);
					if(sub_expr->expr_type == Math_Expr_Type::local_var) {
						new_locals.local_vars[index] = result;
						++index;
					}
				}
			}
			/*   TODO!!!!
			else {
				s64 n = emulate_expression(expr->exprs[0], state, locals).val_integer;
				for(s64 i = 0; i < n; ++i) {
					new_locals.local_vars[0].val_integer = i;
					new_locals.local_vars[0].type = Value_Type::integer;
					result = emulate_expression(expr->exprs[1], state, &new_locals);
				}
			}
			*/
			return result;
		} break;
		
		case Math_Expr_Type::local_var : {
			return build_expression_ir(expr->exprs[0], locals, args, data);
		} break;
		
		case Math_Expr_Type::identifier_chain : {
			auto ident = reinterpret_cast<Identifier_FT *>(expr);
			//TODO: instead we have to find a way to convert the id to a local index. This is just for early testing.
			llvm::Value *result = nullptr;
			
			llvm::Value *offset = nullptr;
			if(ident->variable_type != Variable_Type::local) {
				offset = build_expression_ir(expr->exprs[0], locals, args, data);
			}
			//warning_print("offset for lookup was ", offset, "\n");
			auto double_ty = llvm::Type::getDoubleTy(*data->context);
			
			if(ident->variable_type == Variable_Type::parameter) {
				//llvm::ArrayRef<llvm::Value*>(offset, 1)
				result = data->builder->CreateGEP(double_ty, args[0], offset, "par_lookup");
				if(ident->value_type == Value_Type::integer || ident->value_type == Value_Type::boolean) {
					result = data->builder->CreateBitCast(result, llvm::Type::getInt64Ty(*data->context));
					if(ident->value_type == Value_Type::boolean)
						result = data->builder->CreateTrunc(result, llvm::Type::getInt1Ty(*data->context));
				}
				result = data->builder->CreateLoad(double_ty, result);
			} else if(ident->variable_type == Variable_Type::state_var) {
				result = data->builder->CreateGEP(double_ty, args[2], offset, "var_lookup");
				result = data->builder->CreateLoad(double_ty, result);
			} else if(ident->variable_type == Variable_Type::series) {
				result = data->builder->CreateGEP(double_ty, args[1], offset, "series_lookup");
				result = data->builder->CreateLoad(double_ty, result);
			} else if(ident->variable_type == Variable_Type::local) {
				result = get_local_var(locals, ident->local_var.index, ident->local_var.scope_id);
			}
			return result;
		} break;
		
		case Math_Expr_Type::literal : {
			llvm::Value *result = nullptr;
			auto literal = reinterpret_cast<Literal_FT *>(expr);
			switch (literal->value_type) {
				case Value_Type::real : {
					result = llvm::ConstantFP::get(*data->context, llvm::APFloat(literal->value.val_real));
				} break;
				
				case Value_Type::integer : {
					result = llvm::ConstantInt::get(*data->context, llvm::APInt(64, literal->value.val_integer)); //TODO: check that this handles negative numbers correctly!
				} break;
				
				case Value_Type::boolean : {
					result = llvm::ConstantInt::get(*data->context, llvm::APInt(1, literal->value.val_boolean));
				} break;
				
				default : {
					fatal_error(Mobius_Error::internal, "Unhandled value type for literal in build_expression_ir().");
				}
			}
			return result;
		} break;
		
		case Math_Expr_Type::unary_operator : {
			auto unary = reinterpret_cast<Operator_FT *>(expr);
			llvm::Value *a = build_expression_ir(expr->exprs[0], locals, args, data);
			return build_unary_ir(a, expr->exprs[0]->value_type, unary->oper, data);
		} break;
		
		case Math_Expr_Type::binary_operator : {
			auto binary = reinterpret_cast<Operator_FT *>(expr);
			llvm::Value *a = build_expression_ir(expr->exprs[0], locals, args, data);
			llvm::Value *b = build_expression_ir(expr->exprs[1], locals, args, data);
			return build_binary_ir(a, expr->exprs[0]->value_type, b, expr->exprs[1]->value_type, binary->oper, data);
		} break;
		
		case Math_Expr_Type::function_call : {
			/*
			auto fun = reinterpret_cast<Function_Call_FT *>(expr);
			if(fun->fun_type != Function_Type::intrinsic)
				fatal_error(Mobius_Error::internal, "Unhandled function type in emulate_expression().");
			
			if(fun->exprs.size() == 1) {
				Typed_Value a = emulate_expression(fun->exprs[0], state, locals);
				return apply_intrinsic(a, fun->fun_name);
			} else if(fun->exprs.size() == 2) {
				Typed_Value a = emulate_expression(fun->exprs[0], state, locals);
				Typed_Value b = emulate_expression(fun->exprs[1], state, locals);
				return apply_intrinsic(a, b, fun->fun_name);
			} else
				fatal_error(Mobius_Error::internal, "Unhandled number of function arguments in emulate_expression().");
			*/
		} break;

		case Math_Expr_Type::if_chain : {
			/*
			for(int idx = 0; idx < expr->exprs.size()-1; idx+=2) {
				Typed_Value cond = emulate_expression(expr->exprs[idx+1], state, locals);
				if(cond.val_boolean) return emulate_expression(expr->exprs[idx], state, locals);
				return emulate_expression(expr->exprs.back(), state, locals);
			}
			*/
		} break;
		
		case Math_Expr_Type::state_var_assignment : {
			auto double_ty = llvm::Type::getDoubleTy(*data->context);
			llvm::Value *offset = build_expression_ir(expr->exprs[0], locals, args, data);
			llvm::Value *value  = build_expression_ir(expr->exprs[1], locals, args, data);
			auto ptr = data->builder->CreateGEP(double_ty, args[2], offset, "var_store");
			data->builder->CreateStore(value, ptr);
			return nullptr;
		} break;
		
		case Math_Expr_Type::derivative_assignment : {
			auto double_ty = llvm::Type::getDoubleTy(*data->context);
			llvm::Value *offset = build_expression_ir(expr->exprs[0], locals, args, data);
			llvm::Value *value  = build_expression_ir(expr->exprs[1], locals, args, data);
			auto ptr = data->builder->CreateGEP(double_ty, args[3], offset, "deriv_store");
			data->builder->CreateStore(value, ptr);
			return nullptr;
		} break;
		
		case Math_Expr_Type::cast : {
			llvm::Value *a = build_expression_ir(expr->exprs[0], locals, args, data);
			return build_cast_ir(a, expr->exprs[0]->value_type, expr->value_type, data);
		} break;
	}
	
	fatal_error(Mobius_Error::internal, "Didn't generate llvm ir for ", name(expr->expr_type), " expression.");
	
	return nullptr;
}