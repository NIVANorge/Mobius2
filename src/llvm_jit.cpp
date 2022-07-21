

/*
	Batch function is of the form
	
	void evaluate_batch(Parameter_Value *parameters, double *series, double *state_vars, double *solver_workspace);
	
	since there are no union types in llvm, we treat Parameter_Value as a double and use bitcast when we want it as other types.
	
	     (to be expanded upon with datetime info etc).
*/

struct Scope_Local_Vars;
llvm::Value *build_expression_ir(Math_Expr_FT *expr, Scope_Local_Vars *scope = nullptr);


llvm::Function *create_batch_function(Math_Expr_FT *batch_code, const std::string &fun_name) {
	/*
		//TODO: parameters context, module, builder, optimizer(TheFPM)
	*/
	
	std::vector<llvm::Type *> arg_types = {
		llvm::Type::getDoublePtrTy(*context), 
		llvm::Type::getDoublePtrTy(*context), 
		llvm::Type::getDoublePtrTy(*context), 
		llvm::Type::getDoublePtrTy(*context)
		};
		
	llvm::FunctionType *fun_type = FunctionType::get(llvm::Type::getVoidTy(*context), arg_types, false);
	llvm::Function *fun = llvm::Function::Create(fun_type, llvm::Function::ExternalLinkage, fun_name, module.get());
	
	// Hmm, is it important to set the argument names, or could we skip it?
	const char *argnames[4] = {"parameters", "series", "state_vars", "solver_workspace"};
	int idx = 0;
	for(auto &arg : fun->args())
		arg.setName(argnames[idx++]);
	
	llvm::BasicBlock *basic_block = llvm::BasicBlock::Create(*context, "entry", fun);
	builder->SetInsertPoint(basic_block);
	
	llvm::Value *return_value = build_expression_ir(batch_code);
	if(!return_value) {
		//TODO: clean up the llvm context stuff. This is needed if the error exit does not close the process, but if we instead are in the IDE or python and we just catch an exception.
		fatal_error(Mobius_Error::internal, "LLVM ir was not correctly generated.");
	}
	builder->CreateRet(batch_code);
	
	std::stringstream errstream;
	bool correct = llvm::verifyFunction(*fun, &errstream);
	if(!correct) {
		//TODO: clean up the llvm context stuff. This is needed if the error exit does not close the process, but if we instead are in the IDE or python and we just catch an exception.
		fatal_error(Mobius_Error::internal, "LLVM function verification failed for function ", fun_name, " : ", errstream.str(), " .");
	}
	
	optimizer->run(*fun);

	//TODO: just return the function pointer directly.
	return fun;
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

llvm::Value *build_unary_ir(llvm::Value *arg, Value_Type type, Token_Type oper) {
	llvm::Value result = nullptr;
	if((char)oper == '-') {
		if(type == Value_Type::boolean) fatal_error(Mobius_Error::internal, "build_unary_ir() minus on boolean.");
		else if(type == Value_Type::real) result = builder->CreateFNeg(arg, "fnegtemp");
		else if(type == Value_Type::integer) result = builder->CreteNeg(arg, "negtemp");
	} else if((char)oper == '!') {
		if(type != Value_Type::boolean) fatal_error(Mobius_Error::internal, "build_unary_ir() negation on non-boolean.");
		result = builder->CreateNot(arg, "nottemp");
	} else
		fatal_error(Mobius_Error::internal, "apply_unary() unhandled operator ", name(oper), " .");
	return result;
}

llvm::Value *build_binary_ir(llvm::Value *lhs, Value_Type type1, llvm::Value *rhs, Value_Type type2, Token_Type oper) {
	llvm::Value result;
	char op = (char)oper;
	if(op != '^' && type1 != type2) fatal_error(Mobius_Error::internal, "Mismatching types in build_binary_ir(). lhs: ", name(type1), ", rhs: ", name(type2), "."); //this should have been eliminated at a different stage.
	
	//TODO: should probably do type checking here too, but it SHOULD be correct from the function tree resolution already.
	
	//note FCmpU** means that either argument could be nan. FCmpO** would mean that they could not be, but we can't guarantee that in general (?)
	if(op == '|')      result = builder->CreateLogicalOr(lhs, rhs, "ortemp");
	else if(op == '&') result = builder->CreateLogicalAnd(lhs, rhs, "andtemp");
	else if(op == '<') {
		if(lhs.type == Value_Type::integer)   result = builder->CreateICmpULT(lhs, rhs, "lttemp");
		else if(lhs.type == Value_Type::real) result = builder->CreateFCmpULT(lhs, rhs, "flttemp");
	} else if(oper == Token_Type::leq) {
		if(lhs.type == Value_Type::integer)   result = builder->CreateICmpULE(lhs, rhs, "letemp");
		else if(lhs.type == Value_Type::real) result = builder->CreateFCmpULE(lhs, rhs, "fletemp");
	} else if(op == '>') {
		if(lhs.type == Value_Type::integer)   result = builder->CreateICmpUGT(lhs, rhs, "gttemp");
		else if(lhs.type == Value_Type::real) result = builder->CreateFCmpUGT(lhs, rhs, "fgttemp");
	} else if(oper == Token_Type::geq) {
		if(lhs.type == Value_Type::integer)   result = builder->CreateICmpUGE(lhs, rhs, "uetemp");
		else if(lhs.type == Value_Type::real) result = builder->CreateFCmpUGE(lhs, rhs, "fuetemp");
	} else if(oper == Token_Type::eq) {
		if(lhs.type == Value_Type::integer)   result = builder->CreateICmpEQ(lhs, rhs, "eqtemp");
		else if(lhs.type == Value_Type::real) result = builder->CreateFCmpUEQ(lhs, rhs, "feqtemp");
	} else if(oper == Token_Type::neq) {
		if(lhs.type == Value_Type::integer)   result = builder->CreateICmpNE(lhs, rhs, "netemp");
		else if(lhs.type == Value_Type::real) result = builder->CreateFCmpUNE(lhs, rhs, "fnetemp");
	} else if(op == '+') {
		if(lhs.type == Value_Type::integer)   result = builder->CreateAdd(lhs, rhs, "addtemp");
		else if(lhs.type == Value_Type::real) result = builder->CreateFAdd(lhs, rhs, "faddtemp");
	} else if(op == '-') {
		if(lhs.type == Value_Type::integer)   result = builder->CreateSub(lhs, rhs, "subtemp");
		else if(lhs.type == Value_Type::real) result = builder->CreateFSub(lhs, rhs, "fsubtemp");
	} else if(op == '*') {
		if(lhs.type == Value_Type::integer)   result = builder->CreateMul(lhs, rhs, "multemp");
		else if(lhs.type == Value_Type::real) result = builder->CreateFMul(lhs, rhs, "fmultemp");
	} else if(op == '/') {
		if(lhs.type == Value_Type::integer)   result = builder->CreateSDiv(lhs, rhs, "divtemp");    //TODO: what is difference between sdiv and exactsdiv ?
		else if(lhs.type == Value_Type::real) result = builder->CreateFDiv(lhs, rhs, "fdivtemp");
	} else if(op == '^') {
		if(rhs.type == Value_Type::integer) {
			std::vector<Type *> arg_types = { llvm::Type::getDoubleTy(context), llvm::Type::getInt64Ty(context) };
			llvm::Function *fun = llvm::Intrinsic::getDeclaration(module, Intrinsic::powi, arg_types);
			builder->CreateCall(fun, args);
		}
		else if(rhs.type == Value_Type::real) {
			std::vector<Type *> arg_types = { llvm::Type::getDoubleTy(context), llvm::Type::getDoubleTy(context) };
			llvm::Function *fun = llvm::Intrinsic::getDeclaration(module, Intrinsic::pow, arg_types);
			builder->CreateCall(fun, args);
		}
		result.type = Value_Type::real;
	} else
		fatal_error(Mobius_Error::internal, "build_binary_ir() unhandled operator ", name(oper), " .");
	
	return result;
}

llvm::Value *build_expression_ir(Math_Expr_FT *expr, Scope_Local_Vars *locals) {
	
	if(!expr)
		fatal_error(Mobius_Error::internal, "Got a nullptr expression in build_expression_ir().");
	
	switch(expr->expr_type) {
		case Math_Expr_Type::block : {
			auto block = reinterpret_cast<Math_Block_FT *>(expr);
			llvm::Value *result;
			Scope_Local_Vars new_locals;
			new_locals.scope_id = block->unique_block_id;
			new_locals.scope_up = locals;
			new_locals.local_vars.resize(block->n_locals);
			if(!block->is_for_loop) {
				int index = 0;
				for(auto sub_expr : expr->exprs) {
					result = build_expression_ir(sub_expr, &new_locals);
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
			return build_expression_ir(expr->exprs[0], locals);
		} break;
		
		// TODO!!
		/*
		case Math_Expr_Type::identifier_chain : {
			auto ident = reinterpret_cast<Identifier_FT *>(expr);
			//TODO: instead we have to find a way to convert the id to a local index. This is just for early testing.
			Typed_Value result;
			result.type = expr->value_type;
			s64 offset = 0;
			if(ident->variable_type != Variable_Type::local) {
				DEBUG(warning_print("lookup var offset.\n"))
				offset = emulate_expression(expr->exprs[0], state, locals).val_integer;
			}
			//warning_print("offset for lookup was ", offset, "\n");
			
			if(ident->variable_type == Variable_Type::parameter) {
				result = Typed_Value {state->parameters[offset], expr->value_type};
			} else if(ident->variable_type == Variable_Type::state_var) {
				result.val_real = state->state_vars[offset];
			} else if(ident->variable_type == Variable_Type::series) {
				result.val_real = state->series[offset];
			} else if(ident->variable_type == Variable_Type::local) {
				result = get_local_var(locals, ident->local_var.index, ident->local_var.scope_id);
			}
			return result;
		} break;
		*/
		
		case Math_Expr_Type::literal : {
			llvm::Value *result = nullptr;
			auto literal = reinterpret_cast<Literal_FT *>(expr);
			switch (literal->value_type) {
				case Value_Type::real : {
					result = llvm::ConstantFP::get(*context, llvm::APFloat(literal->value.val_real));
				} break;
				
				case Value_Type::integer : {
					result = llvm::ConstantInt::get(*context, llvm::APInt(64, literal->value.val_integer)); //TODO: check that this handles negative numbers correctly!
				} break;
				
				case Value_Type::boolean : {
					result = llvm::ConstantInt::get(*context, llvm::APInt(1, literal->value.val_boolean));
				} break;
				
				default : {
					fatal_error(Mobius_Error::internal, "Unhandled value type for literal in build_expression_ir().");
				}
			}
			return result;
		} break;
		
		case Math_Expr_Type::unary_operator : {
			auto unary = reinterpret_cast<Operator_FT *>(expr);
			llvm::Value *a = build_expression_ir(expr->exprs[0], locals);
			return build_unary_ir(a, expr->exprs[0].value_type, unary->oper);
		} break;
		
		case Math_Expr_Type::binary_operator : {
			auto binary = reinterpret_cast<Operator_FT *>(expr);
			llvm::Value *a = build_expression_ir(expr->exprs[0], locals);
			llvm::Value *b = build_expression_ir(expr->exprs[0], locals);
			return build_binary_ir(a, expr->exprs[0].value_type, b, expr->exprs[1].value_type, binary->oper);
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
			/*
			Typed_Value index = emulate_expression(expr->exprs[0], state, locals);
			Typed_Value value = emulate_expression(expr->exprs[1], state, locals);
			state->state_vars[index.val_integer] = value.val_real;
			//warning_print("offset for val assignment was ", index.val_integer, "\n");
			return {Parameter_Value(), Value_Type::none};
			*/
		} break;
		
		case Math_Expr_Type::derivative_assignment : {
			/*
			Typed_Value index = emulate_expression(expr->exprs[0], state, locals);
			Typed_Value value = emulate_expression(expr->exprs[1], state, locals);
			state->solver_workspace[index.val_integer] = value.val_real;
			//warning_print("offset for deriv assignment was ", index.val_integer, "\n");
			return {Parameter_Value(), Value_Type::none};
			*/
		} break;
		
		case Math_Expr_Type::cast : {
			/*
			Typed_Value a = emulate_expression(expr->exprs[0], state, locals);
			return apply_cast(a, expr->value_type);
			*/
		} break;
	}
	
	fatal_error(Mobius_Error::internal, "Didn't generate llvm ir for ", name(expr->expr_type), " expression.");
	
	return nullptr;
}