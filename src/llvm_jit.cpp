

#include "../third_party/kaleidoscope/KaleidoscopeJIT.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Transforms/Utils/BuildLibCalls.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Passes/PassBuilder.h"

#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Host.h"
//#include "llvm/Support/TargetSelect.h"
//#include "llvm/Support/raw_ostream.h"
//#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Analysis/TargetLibraryInfo.h"


#include "llvm_jit.h"
#include "special_computations.h"


extern "C" DLLEXPORT double
_test_fun_(double a) {
	// Fibonacci
	if(a <= 1.0) return 1.0;
	return _test_fun_(a-1) + _test_fun_(a-2);
}



static bool llvm_initialized = false;
static std::unique_ptr<llvm::orc::KaleidoscopeJIT> global_jit;

void
initialize_llvm() {
	if(llvm_initialized) return;
	
	llvm::InitializeNativeTarget();
	llvm::InitializeNativeTargetAsmPrinter();
	llvm::InitializeNativeTargetAsmParser();
	
	auto result = llvm::orc::KaleidoscopeJIT::Create();
	if(result)
		global_jit = std::move(*result);
	else 
		fatal_error(Mobius_Error::internal, "Failed to initialize LLVM.");
	
	llvm_initialized = true;
}

struct
LLVM_Module_Data {
	std::unique_ptr<llvm::LLVMContext>         context;
	std::unique_ptr<llvm::Module>              module;
	std::unique_ptr<llvm::IRBuilder<>>         builder;
	llvm::orc::ResourceTrackerSP               resource_tracker;
	
	std::unique_ptr<llvm::TargetLibraryInfoImpl> libinfoimpl;
	std::unique_ptr<llvm::TargetLibraryInfo>     libinfo;
	
	llvm::GlobalVariable                      *global_connection_data;
	llvm::GlobalVariable                      *global_index_count_data;
	
	llvm::Type                                *dt_struct_type;
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
	
	// TODO: maybe set the fast math flags a bit more granularly.
	// we should monitor better how they affect model correctness and/or interfers with is_finite
	data->builder->setFastMathFlags(llvm::FastMathFlags::getFast());
	
	
	// Add libc math functions that dont have intrinsics
	
	auto triple = llvm::sys::getDefaultTargetTriple();
	data->libinfoimpl = std::make_unique<llvm::TargetLibraryInfoImpl>(llvm::Triple(triple));
	data->libinfo     = std::make_unique<llvm::TargetLibraryInfo>(*data->libinfoimpl);
	auto double_ty = llvm::Type::getDoubleTy(*data->context);
	llvm::FunctionType *fun_type = llvm::FunctionType::get(double_ty, {double_ty}, false);
	
	//TODO: If one calls tan(atan(a)) it does know that it should optimize it out in that it just returns a, but for some reason it still calls atan(a). Why? Does it not know that atan does not have side effects, and in that case, can we tell it by passing an attributelist here?
	
	llvm::getOrInsertLibFunc(data->module.get(), *data->libinfo, llvm::LibFunc_tan, fun_type);
	llvm::getOrInsertLibFunc(data->module.get(), *data->libinfo, llvm::LibFunc_atan, fun_type);
	llvm::getOrInsertLibFunc(data->module.get(), *data->libinfo, llvm::LibFunc_acos, fun_type);
	llvm::getOrInsertLibFunc(data->module.get(), *data->libinfo, llvm::LibFunc_asin, fun_type);
	llvm::getOrInsertLibFunc(data->module.get(), *data->libinfo, llvm::LibFunc_cbrt, fun_type);
	llvm::getOrInsertLibFunc(data->module.get(), *data->libinfo, llvm::LibFunc_floor, fun_type);
	llvm::getOrInsertLibFunc(data->module.get(), *data->libinfo, llvm::LibFunc_ceil, fun_type);
	
	return data;
}

void
jit_compile_module(LLVM_Module_Data *data, std::string *output_string) {
	
	// TODO: rabbit hole on optimizations/passes and see how they affect complex models!
	llvm::LoopAnalysisManager     lam;
	llvm::FunctionAnalysisManager fam;
	llvm::CGSCCAnalysisManager    cgam;
	llvm::ModuleAnalysisManager   mam;
	
	llvm::PassBuilder pb;
	
	pb.registerModuleAnalyses(mam);
	pb.registerCGSCCAnalyses(cgam);
	pb.registerFunctionAnalyses(fam);
	pb.registerLoopAnalyses(lam);
	pb.crossRegisterProxies(lam, fam, cgam, mam);
	
	llvm::ModulePassManager mpm = pb.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O2);
	
	mpm.run(*data->module, mam);
	
	if(output_string) {
		std::string module_ir_text;
		llvm::raw_string_ostream os(module_ir_text);
		os << *data->module;
		*output_string = os.str();
	}
	
	data->resource_tracker = global_jit->getMainJITDylib().createResourceTracker();
	auto tsm = llvm::orc::ThreadSafeModule(std::move(data->module), std::move(data->context));
	auto maybe_error = global_jit->addModule(std::move(tsm), data->resource_tracker);
	if(maybe_error)
		fatal_error(Mobius_Error::internal, "Failed to jit compile module.");
	
	//TODO: Put a flag on the data to signify that it is now compiled (can't add more stuff to it), and properly error handle in other procs.
}

void
free_llvm_module(LLVM_Module_Data *data) {
	if(!data) return;
	
	if(data->resource_tracker) {
		auto maybe_error = data->resource_tracker->remove();
		if(maybe_error)
			fatal_error(Mobius_Error::internal, "Failed to free LLVM jit resources.");
	}
	
	delete data;
}

batch_function *
get_jitted_batch_function(const std::string &fun_name) {
	//warning_print("Lookup of function from jitted module.\n");
	
	auto result = global_jit->lookup(fun_name);
	if(result) {
		// Get the symbol's address and cast it to the right type so we can call it as a native function.
		batch_function *fun_ptr = (batch_function *)(intptr_t)result->getAddress();
		return fun_ptr;
	} else
		fatal_error(Mobius_Error::internal, "Failed to find function ", fun_name, " in LLVM module.");

	return nullptr;
}

/*
	Batch function is of the form
	
	void evaluate_batch(Parameter_Value *parameters, double *series, double *state_vars, double *solver_workspace, Expanded_Date_Time *date_time);
	
	since there are no union types in llvm, we treat Parameter_Value as a double and use bitcast when we want it as other types.
*/

//struct Scope_Local_Vars;
llvm::Value *build_expression_ir(Math_Expr_FT *expr, Scope_Local_Vars<llvm::Value *> *scope, std::vector<llvm::Value *> &args, LLVM_Module_Data *data);

llvm::GlobalVariable *
jit_create_constant_array(LLVM_Module_Data *data, s32 *vals, s64 count, const char *name) {
	auto int_32_ty      = llvm::Type::getInt32Ty(*data->context);
	auto conn_array_ty = llvm::ArrayType::get(int_32_ty, count);
	std::vector<llvm::Constant *> values(count);
	for(s64 idx = 0; idx < values.size(); ++idx)
		values[idx] = llvm::ConstantInt::get(*data->context, llvm::APInt(32, vals[idx], true));
	auto const_array_init = llvm::ConstantArray::get(conn_array_ty, values);
	//NOTE: we are not responsible for the ownership of this one even though we allocate it with new.
	return new llvm::GlobalVariable(
		*data->module, conn_array_ty, true,
		llvm::GlobalValue::ExternalLinkage,
		//llvm::GlobalValue::InternalLinkage,
		const_array_init, name);
}

void
jit_add_global_data(LLVM_Module_Data *data, LLVM_Constant_Data *constants) {
	data->global_connection_data  = jit_create_constant_array(data, constants->connection_data, constants->connection_data_count, "global_connection_data");
	data->global_index_count_data = jit_create_constant_array(data, constants->index_count_data, constants->index_count_data_count, "global_connection_data");
}

void
jit_add_batch(Math_Expr_FT *batch_code, const std::string &fun_name, LLVM_Module_Data *data) {
	
	auto double_ty     = llvm::Type::getDoubleTy(*data->context);
	auto double_ptr_ty = llvm::Type::getDoublePtrTy(*data->context);
	auto int_64_ptr_ty = llvm::Type::getInt64PtrTy(*data->context);
	auto int_64_ty     = llvm::Type::getInt64Ty(*data->context);
	auto int_32_ty     = llvm::Type::getInt32Ty(*data->context);
	
	#define TIME_VALUE(name, nbits) int_##nbits##_ty,
	std::vector<llvm::Type *> dt_member_types = {
		#include "time_values.incl"
	};
	#undef TIME_VALUE
	
	llvm::StructType *dt_ty     = llvm::StructType::get(*data->context, dt_member_types);
	llvm::Type       *dt_ptr_ty = llvm::PointerType::getUnqual(dt_ty);
	
	data->dt_struct_type = dt_ty;
	
	std::vector<llvm::Type *> arg_types = {
		double_ptr_ty, double_ptr_ty, double_ptr_ty, double_ptr_ty, dt_ptr_ty, double_ty
		};
		
	llvm::FunctionType *fun_type = llvm::FunctionType::get(llvm::Type::getVoidTy(*data->context), arg_types, false);
	llvm::Function *fun = llvm::Function::Create(fun_type, llvm::Function::ExternalLinkage, fun_name, data->module.get());
	
	// Hmm, is it important to set the argument names, or could we skip it?
	const char *argnames[6] = {"parameters", "series", "state_vars", "solver_workspace", "date_time", "fractional_step"};
	std::vector<llvm::Value *> args;
	int idx = 0;
	for(auto &arg : fun->args()) {
		arg.setName(argnames[idx++]);
		args.push_back(&arg);
	}
	
	llvm::BasicBlock *basic_block = llvm::BasicBlock::Create(*data->context, "entry", fun);
	data->builder->SetInsertPoint(basic_block);
	
	//warning_print("Begin llvm function creation\n");
	
	build_expression_ir(batch_code, nullptr, args, data);
	data->builder->CreateRetVoid();
	
	//warning_print("Created llvm function\n");
	
	std::string errmsg = "";
	llvm::raw_string_ostream errstream(errmsg);
	errstream.reserveExtraSpace(512);
	bool errors = llvm::verifyFunction(*fun, &errstream);
	if(errors) {
		//TODO: clean up the llvm context stuff. This is needed if the error exit does not close the process, but if we instead are in the GUI or python and we just catch an exception.
		fatal_error(Mobius_Error::internal, "LLVM function verification failed for function \"", fun_name, "\" : ", errstream.str(), " .");
	}
	
	//warning_print("Verification done.\n");
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
		fatal_error(Mobius_Error::internal, "build_unary_ir() unhandled operator ", name(oper), " .");
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
		if(type1 == Value_Type::integer)   result = data->builder->CreateICmpSLT(lhs, rhs, "lttemp");
		else if(type1 == Value_Type::real) result = data->builder->CreateFCmpULT(lhs, rhs, "flttemp");
	} else if(oper == Token_Type::leq) {
		if(type1 == Value_Type::integer)   result = data->builder->CreateICmpSLE(lhs, rhs, "letemp");
		else if(type1 == Value_Type::real) result = data->builder->CreateFCmpULE(lhs, rhs, "fletemp");
	} else if(op == '>') {
		if(type1 == Value_Type::integer)   result = data->builder->CreateICmpSGT(lhs, rhs, "gttemp");
		else if(type1 == Value_Type::real) result = data->builder->CreateFCmpUGT(lhs, rhs, "fgttemp");
	} else if(oper == Token_Type::geq) {
		if(type1 == Value_Type::integer)   result = data->builder->CreateICmpSGE(lhs, rhs, "uetemp");
		else if(type1 == Value_Type::real) result = data->builder->CreateFCmpUGE(lhs, rhs, "fuetemp");
	} else if(op == '=') {
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
		if(type1 == Value_Type::integer)   result = data->builder->CreateSDiv(lhs, rhs, "divtemp");    //TODO: what is the difference between sdiv and exactsdiv ?
		else if(type1 == Value_Type::real) result = data->builder->CreateFDiv(lhs, rhs, "fdivtemp");
	} else if(op == '%') {
		                                   result = data->builder->CreateSRem(lhs, rhs, "remtemp");
	} else if(op == '^') {
		if(type2 == Value_Type::integer) {
			// TODO: Do we really want to do the trunc? There is no version of the intrinsic that takes int64. But likelihood of causing errors is very low..
			rhs = data->builder->CreateTrunc(rhs, llvm::Type::getInt32Ty(*data->context));
			std::vector<llvm::Type *> arg_types = { llvm::Type::getDoubleTy(*data->context), llvm::Type::getInt32Ty(*data->context) };
			llvm::Function *fun = llvm::Intrinsic::getDeclaration(data->module.get(), llvm::Intrinsic::powi, arg_types);
			result = data->builder->CreateCall(fun, { lhs, rhs });
		} else if(type2 == Value_Type::real) {
			std::vector<llvm::Type *> arg_types = { llvm::Type::getDoubleTy(*data->context) };
			llvm::Function *fun = llvm::Intrinsic::getDeclaration(data->module.get(), llvm::Intrinsic::pow, arg_types);
			result = data->builder->CreateCall(fun, { lhs, rhs });
		}
	} else
		fatal_error(Mobius_Error::internal, "build_binary_ir() unhandled operator ", name(oper), " .");
	
	return result;
}

llvm::Type *
get_llvm_type(Value_Type type, LLVM_Module_Data *data) {
	if(type == Value_Type::real)    return llvm::Type::getDoubleTy(*data->context);
	if(type == Value_Type::integer) return llvm::Type::getInt64Ty(*data->context);
	if(type == Value_Type::boolean) return llvm::Type::getInt1Ty(*data->context);
	fatal_error(Mobius_Error::internal, "Tried to look up llvm type of unrecognized type.");
	return llvm::Type::getInt64Ty(*data->context);
}

llvm::Value *build_cast_ir(llvm::Value *val, Value_Type from_type, Value_Type to_type, LLVM_Module_Data *data) {
	
	auto llvm_to_type = get_llvm_type(to_type, data);
	
	if(from_type == Value_Type::real) {
		if(to_type == Value_Type::boolean) {
			auto tmp = data->builder->CreateFPToSI(val, llvm_to_type, "castftoi"); // Do this before comparison to get rid of negative 0. TODO: check what the behaviour of overflow is here...
			return data->builder->CreateICmpNE(tmp, llvm::ConstantInt::get(*data->context, llvm::APInt(1, 0)), "netmp");  // Force the value to be 0 or 1
		} else
			fatal_error(Mobius_Error::internal, "Cast from real to int not implemented");
	} else if (from_type == Value_Type::integer) {
		if(to_type == Value_Type::real)
			return data->builder->CreateSIToFP(val, llvm_to_type, "castitof");
		else if (to_type == Value_Type::boolean)
			return data->builder->CreateICmpNE(val, llvm::ConstantInt::get(*data->context, llvm::APInt(64, 0)), "netmp");  // Force value to be 0 or 1
	} else if (from_type == Value_Type::boolean) {
		if(to_type == Value_Type::integer)
			return data->builder->CreateZExt(val, llvm_to_type, "castbtoi");
		else if(to_type == Value_Type::real)
			return data->builder->CreateUIToFP(val, llvm_to_type, "castbtof");
	}
	fatal_error(Mobius_Error::internal, "Unimplemented cast in ir building.");
}

llvm::Value *
build_intrinsic_ir(llvm::Value *a, Value_Type type, const std::string &function, LLVM_Module_Data *data) {
	llvm::Value *result = nullptr;
	if(false) {}
	#define MAKE_INTRINSIC1(name, emul, intrin_name, ret_type, type1) \
		else if(function == #name && strcmp(#intrin_name, "fshr")!=0) { \
			if(type != Value_Type::type1) \
				fatal_error(Mobius_Error::internal, "Somehow we got wrong type of arguments to \"", function, "\" in build_intrinsic_ir()."); \
			std::vector<llvm::Type *> arg_types = { get_llvm_type(Value_Type::type1, data) }; \
			llvm::Function *fun = llvm::Intrinsic::getDeclaration(data->module.get(), llvm::Intrinsic::intrin_name, arg_types); \
			result = data->builder->CreateCall(fun, { a }); \
		}
	// could name the function call
	#define MAKE_INTRINSIC2(name, intrin_name, ret_type, type1, type2)
	#include "intrinsics.incl"
	#undef MAKE_INTRINSIC1
	#undef MAKE_INTRINSIC2
	else if(function == "is_finite") {
		// NOTE: This checks if the mantissa bits are not all 1. (If they are it is either an inf or a nan).
		//   TODO: this could break on Big Endian architectures (unless both the int64 and float64 are swapped the same way.?)
		//      we are not likely to encounter it, but should probably have an alternative for it just in case.
		//   - we could maybe also just use libc isfinite instead...
		
		auto mask = llvm::ConstantInt::get(*data->context, llvm::APInt(64, 0x7ff0000000000000));
		result = data->builder->CreateBitCast(a, llvm::Type::getInt64Ty(*data->context));
		result = data->builder->CreateAnd(result, mask);
		result = data->builder->CreateICmpNE(result, mask);
	} else {
		auto fun = data->module->getFunction(function);
		if(!fun)
			fatal_error(Mobius_Error::internal, "Unhandled or unsupported function \"", function, "\" in build_intrinsic_ir().");
		result = data->builder->CreateCall(fun, a);
	}
	
	return result;
}

llvm::Value *
build_intrinsic_ir(llvm::Value *a, Value_Type type1, llvm::Value *b, Value_Type type2, const std::string &function, LLVM_Module_Data *data) {
	//TODO: use MAKE_INTRINSIC2 macro here?
	llvm::Value *result;
	bool ismin = (function == "min");
	if(ismin || function == "max") {
		if(type1 != type2) fatal_error(Mobius_Error::internal, "Mismatching types in build_intrinsic_ir()."); //this should have been eliminated at a different stage.
		
		std::vector<llvm::Type *> arg_types = { get_llvm_type(type1, data) };
		llvm::Function *fun = nullptr;
		if(type1 == Value_Type::integer) {
			fun = llvm::Intrinsic::getDeclaration(data->module.get(), ismin ? llvm::Intrinsic::smin : llvm::Intrinsic::smax, arg_types);
		} else if(type1 == Value_Type::real) {
			fun = llvm::Intrinsic::getDeclaration(data->module.get(), ismin ? llvm::Intrinsic::minnum : llvm::Intrinsic::maxnum, arg_types);
		} else
			fatal_error(Mobius_Error::internal, "Somehow we got wrong type of arguments to \"", function, "\" in build_intrinsic_ir().");
		result = data->builder->CreateCall(fun, { a, b });
	} else if (function == "copysign") {
		std::vector<llvm::Type *> arg_types = { llvm::Type::getDoubleTy(*data->context) };
		llvm::Function *fun = llvm::Intrinsic::getDeclaration(data->module.get(), llvm::Intrinsic::copysign, arg_types);
		result = data->builder->CreateCall(fun, {a, b});
	} else
		fatal_error(Mobius_Error::internal, "Unhandled intrinsic \"", function, "\" in build_intrinsic_ir().");
	return result;
}


llvm::Value *
build_if_chain_ir(Math_Expr_FT **exprs, size_t exprs_size, Scope_Local_Vars<llvm::Value *> *locals, std::vector<llvm::Value *> &args, LLVM_Module_Data *data) {
	
	//TODO: we could maybe optimize by having only one merge block and phi node!
	
	if(exprs_size % 2 != 1)
		fatal_error(Mobius_Error::internal, "Got a malformed if statement in ir generation. This should have been detected at an earlier stage!");
	
	if(exprs_size == 1)
		return build_expression_ir(exprs[0], locals, args, data);
	
	llvm::Value *cond = build_expression_ir(exprs[1], locals, args, data);
	llvm::Function *fun = data->builder->GetInsertBlock()->getParent();
	
	llvm::BasicBlock *then_block  = llvm::BasicBlock::Create(*data->context, "then", fun);
	llvm::BasicBlock *else_block  = llvm::BasicBlock::Create(*data->context, "else");
	llvm::BasicBlock *merge_block = llvm::BasicBlock::Create(*data->context, "endif");
	
	data->builder->CreateCondBr(cond, then_block, else_block);
	
	data->builder->SetInsertPoint(then_block);
	llvm::Value *then_val = build_expression_ir(exprs[0], locals, args, data);
	
	data->builder->CreateBr(merge_block);
	then_block = data->builder->GetInsertBlock();
	
	fun->getBasicBlockList().push_back(else_block);
	data->builder->SetInsertPoint(else_block);
	
	llvm::Value *else_val = build_if_chain_ir(exprs+2, exprs_size-2, locals, args, data);
	
	data->builder->CreateBr(merge_block);
	else_block = data->builder->GetInsertBlock();
	
	fun->getBasicBlockList().push_back(merge_block);
	data->builder->SetInsertPoint(merge_block);
	
	if(exprs[0]->value_type == Value_Type::none)
		return llvm::ConstantInt::get(*data->context, llvm::APInt(64, 0, true));  // NOTE: This is a dummy, it should not be read by anyone.
	
	llvm::PHINode *phi = data->builder->CreatePHI(get_llvm_type(exprs[0]->value_type, data), 2, "iftemp");
	
	phi->addIncoming(then_val, then_block);
	phi->addIncoming(else_val, else_block);
	
	return phi;
}

llvm::Value *
build_for_loop_ir(Math_Expr_FT *n, Math_Expr_FT *body, Scope_Local_Vars<llvm::Value *> *loop_local, std::vector<llvm::Value *> &args, LLVM_Module_Data *data) {
	llvm::Value *start_val = llvm::ConstantInt::get(*data->context, llvm::APInt(64, 0, true));  // Iterator starts at 0
	
	llvm::Function *fun = data->builder->GetInsertBlock()->getParent();
	llvm::BasicBlock *pre_header_block = data->builder->GetInsertBlock();
	llvm::BasicBlock *loop_block       = llvm::BasicBlock::Create(*data->context, "loop", fun);
	
	data->builder->CreateBr(loop_block);
	data->builder->SetInsertPoint(loop_block);
	
	// Create the iterator
	llvm::PHINode *iter = data->builder->CreatePHI(llvm::Type::getInt64Ty(*data->context), 2, "index");
	iter->addIncoming(start_val, pre_header_block);
	loop_local->values[0] = iter;
	
	// Insert the loop body
	build_expression_ir(body, loop_local, args, data);
	
	llvm::Value *step_val = llvm::ConstantInt::get(*data->context, llvm::APInt(64, 1, true));  // Step is always 1
	llvm::Value *next_iter = data->builder->CreateAdd(iter, step_val, "next_iter");
	llvm::Value *iter_end  = build_expression_ir(n, loop_local, args, data);
	llvm::Value *loop_cond = data->builder->CreateICmpNE(next_iter, iter_end, "loopcond"); // iter+1 != n
	
	llvm::BasicBlock *loop_end_block = data->builder->GetInsertBlock();
	llvm::BasicBlock *after_block = llvm::BasicBlock::Create(*data->context, "afterloop", fun);
	
	data->builder->CreateCondBr(loop_cond, loop_block, after_block);
	data->builder->SetInsertPoint(after_block);
	
	iter->addIncoming(next_iter, loop_end_block);
	
	return llvm::ConstantInt::get(*data->context, llvm::APInt(64, 0, true));  // NOTE: This is a dummy, it should not be used by anyone.
}

llvm::Function *
get_linked_function(LLVM_Module_Data *data, const std::string &fun_name, llvm::Type *ret_ty, std::vector<llvm::Type *> &arguments_ty) {
	
	auto fun = data->module->getFunction(fun_name);
	// Could check that the types are correct.
	
	if(fun) return fun;
	
	auto *funty = llvm::FunctionType::get(ret_ty, arguments_ty, false);
	fun = llvm::Function::Create(funty, llvm::Function::ExternalLinkage, fun_name, data->module.get());
	return fun;
}

llvm::Value *
build_special_computation_ir(Math_Expr_FT *expr, Scope_Local_Vars<llvm::Value *> *locals, std::vector<llvm::Value *> &args, LLVM_Module_Data *data) {
	//TODO!
	
	//warning_print("****** Build special computation ir\n");
	
	auto double_ty = llvm::Type::getDoubleTy(*data->context);
	auto int_64_ty = llvm::Type::getInt64Ty(*data->context);
	auto double_ptr_ty = llvm::Type::getDoublePtrTy(*data->context);
	auto void_ty = llvm::Type::getVoidTy(*data->context);
	
	auto special = static_cast<Special_Computation_FT *>(expr);
	
	// NOTE: the first call argument is currently the "return" value of the special computation, hence +1
	int n_call_args = special->arguments.size() + 1;
	
	std::vector<llvm::Value *> valptrs;
	std::vector<llvm::Value *> strides;
	std::vector<llvm::Value *> counts;
	for(int idx = 0; idx < n_call_args; ++idx) {
		auto offset = build_expression_ir(special->exprs[3*idx], locals, args, data);
		auto stride = build_expression_ir(special->exprs[3*idx + 1], locals, args, data);
		auto count  = build_expression_ir(special->exprs[3*idx + 2], locals, args, data);
		llvm::Value *valptr;
		if(idx == 0 || special->arguments[idx-1].variable_type == Variable_Type::state_var)
			valptr = data->builder->CreateGEP(double_ty, args[2], offset, "state_var_ptr");
		else
			valptr = data->builder->CreateGEP(double_ty, args[0], offset, "par_ptr");
		valptrs.push_back(valptr);
		strides.push_back(stride);
		counts.push_back(count);
	}
	
	//warning_print("***** The function name is ", special->function_name, "\n");
	
	// Must match Special_Indexed_Value in special_computations.h
	std::vector<llvm::Type *> member_types = {
		double_ptr_ty,
		int_64_ty,
		int_64_ty,
	};
	auto struct_ty = llvm::StructType::get(*data->context, member_types);
	auto struct_ptr_ty = llvm::PointerType::getUnqual(struct_ty);
	
	std::vector<llvm::Type *> arguments_ty(n_call_args, struct_ptr_ty);
	
	auto special_fun = get_linked_function(data, special->function_name, void_ty, arguments_ty);
	
	// Construct the struct arguments
	std::vector<llvm::Value *> arguments;
	for(int idx = 0; idx < n_call_args; ++idx) {
		auto alloc = data->builder->CreateAlloca(struct_ty);
		auto val = data->builder->CreateStructGEP(struct_ty, alloc, 0);
		data->builder->CreateStore(valptrs[idx], val);
		auto stride = data->builder->CreateStructGEP(struct_ty, alloc, 1);
		data->builder->CreateStore(strides[idx], stride);
		auto count = data->builder->CreateStructGEP(struct_ty, alloc, 2);
		data->builder->CreateStore(counts[idx], count);
		arguments.push_back(alloc);
	}
	
	data->builder->CreateCall(special_fun, arguments);
	
	return llvm::ConstantInt::get(*data->context, llvm::APInt(64, 0, true));  // NOTE: This is a dummy, it should not be used by anyone.
}

llvm::Value *
build_expression_ir(Math_Expr_FT *expr, Scope_Local_Vars<llvm::Value *> *locals, std::vector<llvm::Value *> &args, LLVM_Module_Data *data) {
	
	if(!expr)
		fatal_error(Mobius_Error::internal, "Got a nullptr expression in build_expression_ir().");
	
	//warning_print("ir gen, ", name(expr->expr_type), "\n");
	
	switch(expr->expr_type) {
		case Math_Expr_Type::block : {
			auto block = static_cast<Math_Block_FT *>(expr);
			llvm::Value *result = nullptr;
			Scope_Local_Vars<llvm::Value *> new_locals;
			new_locals.scope_id = block->unique_block_id;
			new_locals.scope_up = locals;
			//new_locals.local_vars.resize(block->n_locals);
			if(!block->is_for_loop) {
				//int index = 0;
				for(auto sub_expr : expr->exprs) {
					if(sub_expr->expr_type == Math_Expr_Type::local_var) {
						auto local = static_cast<Local_Var_FT *>(sub_expr);
						if(local->is_used) {
							result = build_expression_ir(sub_expr, &new_locals, args, data);
							new_locals.values[local->id] = result;
						} /*else {
							new_locals.values[local->id] = nullptr; // TODO: Should not be necessary
						}*/
						//++index;
					} else
						result = build_expression_ir(sub_expr, &new_locals, args, data);
				}
			} else
				result = build_for_loop_ir(expr->exprs[0], expr->exprs[1], &new_locals, args, data);
			return result;
		} break;
		
		case Math_Expr_Type::local_var : {
			return build_expression_ir(expr->exprs[0], locals, args, data);
		} break;
		
		case Math_Expr_Type::identifier : {
			auto ident = static_cast<Identifier_FT *>(expr);
			llvm::Value *result = nullptr;
			
			llvm::Value *offset = nullptr;
			if(ident->variable_type == Variable_Type::parameter || ident->variable_type == Variable_Type::state_var || ident->variable_type == Variable_Type::series
				|| ident->variable_type == Variable_Type::connection_info || ident->variable_type == Variable_Type::index_count) {
				if(expr->exprs.size() != 1)
					fatal_error(Mobius_Error::internal, "An identifier was not properly indexed before LLVM codegen.");
				offset = build_expression_ir(expr->exprs[0], locals, args, data);
			}
			//warning_print("offset for lookup was ", offset, "\n");
			auto double_ty = llvm::Type::getDoubleTy(*data->context);
			auto int_64_ty = llvm::Type::getInt64Ty(*data->context);
			auto int_32_ty = llvm::Type::getInt32Ty(*data->context);
			
			int struct_pos = -1;
			if(ident->variable_type == Variable_Type::parameter) {
				result = data->builder->CreateGEP(double_ty, args[0], offset, "par_lookup");
				result = data->builder->CreateLoad(double_ty, result, "par");
				if(ident->value_type == Value_Type::integer || ident->value_type == Value_Type::boolean) {
					result = data->builder->CreateBitCast(result, llvm::Type::getInt64Ty(*data->context));
					if(ident->value_type == Value_Type::boolean)
						result = data->builder->CreateTrunc(result, llvm::Type::getInt1Ty(*data->context));
				}
			} else if(ident->variable_type == Variable_Type::state_var) {
				result = data->builder->CreateGEP(double_ty, args[2], offset, "var_lookup");
				result = data->builder->CreateLoad(double_ty, result, "var");
			} else if(ident->variable_type == Variable_Type::series) {
				result = data->builder->CreateGEP(double_ty, args[1], offset, "series_lookup");
				result = data->builder->CreateLoad(double_ty, result, "series");
			} else if(ident->variable_type == Variable_Type::local) {
				result = find_local_var(locals, ident->local_var);
				if(!result)
					fatal_error(Mobius_Error::internal, "A local var was not initialized in ir building.");
			} else if(ident->variable_type == Variable_Type::connection_info) {
				result = data->builder->CreateGEP(int_32_ty, data->global_connection_data, offset, "connection_info_lookup");
				result = data->builder->CreateLoad(int_32_ty, result, "connection_info");
				result = data->builder->CreateSExt(result, int_64_ty, "connection_info_cast");
			} else if(ident->variable_type == Variable_Type::index_count) {
				result = data->builder->CreateGEP(int_32_ty, data->global_index_count_data, offset, "index_count_lookup");
				result = data->builder->CreateLoad(int_32_ty, result, "index_count");
				result = data->builder->CreateSExt(result, int_64_ty, "index_count_cast");
			}
			#define TIME_VALUE(name, bits) \
			else if(++struct_pos, ident->variable_type == Variable_Type::time_##name) { \
				result = data->builder->CreateStructGEP(data->dt_struct_type, args[4], struct_pos, #name); \
				result = data->builder->CreateLoad(int_##bits##_ty, result); \
				if(bits != 64) \
					result = data->builder->CreateSExt(result, int_64_ty, "cast"); \
			}
			#include "time_values.incl"
			#undef TIME_VALUE
			else if(ident->variable_type == Variable_Type::time_fractional_step) {
				result = args[5];
			} else if(ident->variable_type == Variable_Type::no_override) {
				ident->source_loc.print_error_header(Mobius_Error::model_building);
				fatal_error("This 'no_override' is not in a branch that could be resolved at compile time."); // TODO: should probably check for that before this.
			} else if (ident->variable_type == Variable_Type::connection) {
				ident->source_loc.print_error_header(Mobius_Error::model_building);
				fatal_error("This handle refers to a connection, and should only appear in an expression as an argument to a special instruction like 'target'."); //TODO: Should we name the rest when they are implemented?
			} else {
				fatal_error(Mobius_Error::internal, "Unhandled variable type in build_expression_ir().");
			}
			return result;
		} break;
		
		case Math_Expr_Type::literal : {
			llvm::Value *result = nullptr;
			auto literal = static_cast<Literal_FT *>(expr);
			switch (literal->value_type) {
				case Value_Type::real : {
					result = llvm::ConstantFP::get(*data->context, llvm::APFloat(literal->value.val_real));
				} break;
				
				case Value_Type::integer : {
					result = llvm::ConstantInt::get(*data->context, llvm::APInt(64, literal->value.val_integer, true));
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
			auto unary = static_cast<Operator_FT *>(expr);
			llvm::Value *a = build_expression_ir(expr->exprs[0], locals, args, data);
			return build_unary_ir(a, expr->exprs[0]->value_type, unary->oper, data);
		} break;
		
		case Math_Expr_Type::binary_operator : {
			auto binary = static_cast<Operator_FT *>(expr);
			llvm::Value *a = build_expression_ir(expr->exprs[0], locals, args, data);
			llvm::Value *b = build_expression_ir(expr->exprs[1], locals, args, data);
			return build_binary_ir(a, expr->exprs[0]->value_type, b, expr->exprs[1]->value_type, binary->oper, data);
		} break;
		
		case Math_Expr_Type::function_call : {
			auto fun = static_cast<Function_Call_FT *>(expr);
			if(fun->fun_type == Function_Type::intrinsic) {
				if(fun->exprs.size() == 1) {
					llvm::Value *a = build_expression_ir(fun->exprs[0], locals, args, data);
					return build_intrinsic_ir(a, fun->exprs[0]->value_type, fun->fun_name, data);
				} else if(fun->exprs.size() == 2) {
					llvm::Value *a = build_expression_ir(fun->exprs[0], locals, args, data);
					llvm::Value *b = build_expression_ir(fun->exprs[1], locals, args, data);
					return build_intrinsic_ir(a, fun->exprs[0]->value_type, b, fun->exprs[1]->value_type, fun->fun_name, data);
				} else
					fatal_error(Mobius_Error::internal, "Unhandled number of function arguments in ir building.");
			} else if (fun->fun_type == Function_Type::linked) {
				// TODO: Hmm, should maybe actually just scan the program once first and create all the externally linked functions needed so that the same function is not created multiple times.
				
				auto double_ty = llvm::Type::getDoubleTy(*data->context);
				std::vector<llvm::Type *> arguments_ty(fun->exprs.size(), double_ty);
				auto *funty = llvm::FunctionType::get(double_ty, arguments_ty, false);
				auto *linked_fun = get_linked_function(data, fun->fun_name, double_ty, arguments_ty);
				if(!linked_fun) {
					fun->source_loc.print_error_header();
					fatal_error("Unable to link with the function \"", fun->fun_name, "\".");
				}
				
				std::vector<llvm::Value *> fun_args;
				for(auto arg : fun->exprs)
					fun_args.push_back(build_expression_ir(arg, locals, args, data));
				
				return data->builder->CreateCall(linked_fun, fun_args, "calltmp");
				
			} else
				fatal_error(Mobius_Error::internal, "Unhandled function type in ir building.");
		} break;

		case Math_Expr_Type::if_chain : {
			//warning_print("*** New if chain, size is ", expr->exprs.size(), "\n");
			return build_if_chain_ir(expr->exprs.data(), expr->exprs.size(), locals, args, data);
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
		
		case Math_Expr_Type::special_computation : {
			return build_special_computation_ir(expr, locals, args, data);
		} break;
	}
	
	fatal_error(Mobius_Error::internal, "Didn't generate llvm ir for ", name(expr->expr_type), " expression.");
	
	return nullptr;
}