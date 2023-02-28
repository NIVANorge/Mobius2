
#include "function_tree.h"
#include "emulate.h"


Math_Expr_FT *
prune_helper(Math_Expr_FT *expr, Function_Scope *scope);

Math_Expr_FT *
optimize_pow_int(Math_Expr_FT *lhs, s64 p) {
	// Note: case p == 0 is handled in another call. Also for p == 1, but we also use this for powers 1.5 etc. see below
	Math_Expr_FT *result = nullptr;
	if(p == 1)
		result = lhs;
	else if(p == -1)
		result = make_binop('/', make_literal(1.0), lhs);
	else if(p >= -2 && p <=3) {
		auto scope = new Math_Block_FT();
		result = scope;
		result->value_type = Value_Type::real;
		
		auto ref = add_local_var(scope, lhs);
	
		if(p == 2)
			result->exprs.push_back(make_binop('*', ref, ref));
		else if(p == 3)
			result->exprs.push_back(make_binop('*', make_binop('*', ref, ref), ref));
		else if(p == 4) {
			auto ref2 = add_local_var(scope, make_binop('*', ref, ref));
			result->exprs.push_back(make_binop('*', ref2, ref2));
		}
		else if(p == -2)
			result->exprs.push_back(make_binop('/', make_literal(1.0), make_binop('*', ref, ref)));
	} else
		result = make_binop('^', lhs, make_literal(p));  // Fallback on powi
	
	return result;
}

Math_Expr_FT *
maybe_optimize_pow(Operator_FT *binary, Math_Expr_FT *lhs, Literal_FT *rhs) {
	Math_Expr_FT *result = binary;
	if(rhs->value_type == Value_Type::real) {
		double val = rhs->value.val_real;
		double valmh = val-0.5;
		if(val == 0.5) {
			result = make_intrinsic_function_call(Value_Type::real, "sqrt", lhs);
		} else if (valmh == (double)(s64)valmh) {
			auto rt = make_intrinsic_function_call(Value_Type::real, "sqrt", lhs);
			auto rest = optimize_pow_int(copy(lhs), (s64)valmh);   // TODO: oops, lhs should instead be a local var. Will probably be caught by optimizer though.
			result = make_binop('*', rt, rest);
		} else if(val == (double)(s64)val) {
			result = optimize_pow_int(lhs, (s64)val);
		}
	} else if (rhs->value_type == Value_Type::integer)
		result = optimize_pow_int(lhs, rhs->value.val_integer);
	
	if(result != binary) {
		result->source_loc = binary->source_loc;
		binary->exprs.clear(); // to not invoke recursive destructor on lhs.
		delete rhs;
		delete binary;
	}
	
	return result;
}

Math_Expr_FT *
replace_iteration_index(Math_Expr_FT *expr, s32 block_id) {
	if(expr->expr_type == Math_Expr_Type::identifier) {
		auto ident = static_cast<Identifier_FT *>(expr);
		if(ident->variable_type == Variable_Type::local && ident->local_var.scope_id == block_id) {
			//NOTE: the for loop block has only one local variable, the iteration index, so we only have to know that it points to that block.
			delete expr;
			return make_literal((s64)0);
		}
	}
	for(auto &arg : expr->exprs) arg = replace_iteration_index(arg, block_id);
	
	return expr;
}

Math_Expr_FT *
potentially_prune_local(Math_Expr_FT *expr, Function_Scope *scope) {
	auto ident = static_cast<Identifier_FT *>(expr);
	if(ident->variable_type != Variable_Type::local) return expr;
	
	if(!scope)
		fatal_error(Mobius_Error::internal, "Something went wrong with the scope of an identifier when pruning a function tree.");
	Function_Scope *sc = scope;
	while(sc->block->unique_block_id != ident->local_var.scope_id) {
		sc = sc->parent;
		if(!sc)
			fatal_error(Mobius_Error::internal, "Something went wrong with the scope of an identifier when pruning a function tree.");
	}
	Math_Block_FT *block = sc->block;
	if(block->is_for_loop) return expr; // If the scope was a for loop, the identifier was pointing to the iteration index, and that can't be optimized away here.
		
	int index = 0;
	for(auto loc : block->exprs) {
		if(loc->expr_type == Math_Expr_Type::local_var) {
			if (index == ident->local_var.id) {
				loc->exprs[0] = prune_helper(loc->exprs[0], sc); // TODO: This is not very clean, and causes double work some times, but is needed if we want to use this from constant checking in the unit checking. Should probably instead make a separate constant checking thing rather than prune_helper!
				if(loc->exprs[0]->expr_type == Math_Expr_Type::literal) {
					auto literal        = new Literal_FT();
					auto loc2           = static_cast<Local_Var_FT *>(loc);
					auto loc_literal    = static_cast<Literal_FT *>(loc->exprs[0]);
					literal->value      = loc_literal->value;
					literal->value_type = loc_literal->value_type;
					literal->source_loc = ident->source_loc;
					loc2->is_used       = false;
					delete expr;
					return literal;
				} else if (loc->exprs[0]->expr_type == Math_Expr_Type::identifier) {
					auto expr2 = potentially_prune_local(loc->exprs[0], sc);
					if(expr2->expr_type == Math_Expr_Type::literal)
						return expr2;
					else
						return expr;
				}
			}
			++index;
		}
	}
	return expr;
}

Math_Expr_FT *
binop_reduction_first_pass(Math_Expr_FT *expr) {
	Literal_FT *lhs = nullptr;
	Literal_FT *rhs = nullptr;
	auto binary = static_cast<Operator_FT *>(expr);
	if(expr->exprs[0]->expr_type == Math_Expr_Type::literal)
		lhs = static_cast<Literal_FT *>(expr->exprs[0]);
	if(expr->exprs[1]->expr_type == Math_Expr_Type::literal)
		rhs = static_cast<Literal_FT *>(expr->exprs[1]);
	
	Typed_Value result;
	result.type = Value_Type::unresolved;
	if(lhs && rhs) {
		// If both arguments are literals, just apply the operator directly.
		result = apply_binary({lhs->value, lhs->value_type}, {rhs->value, rhs->value_type}, binary->oper);
	} else {
		// If one argument is a literal, we can still in some cases determine the result in advance.
		if(lhs)
			result = check_binop_reduction(binary->source_loc, binary->oper, lhs->value, lhs->value_type, true);
		else if(rhs)
			result = check_binop_reduction(binary->source_loc, binary->oper, rhs->value, rhs->value_type, false);
	}
	
	if(result.type != Value_Type::unresolved) {
		if(result.type == Value_Type::none) { // note: this is used by the check_binop_reduction procedure to signal to keep the other operand rather than replace it with a literal
			Math_Expr_FT *res;
			if(lhs) {
				delete expr->exprs[0];
				res =  expr->exprs[1];
			} else {
				delete expr->exprs[1];
				res =  expr->exprs[0];
			}
			expr->exprs.clear(); // to not invoke recursive destructor
			delete expr;
			return res;
		} else {
			auto literal = new Literal_FT();
			literal->value = result;
			literal->value_type = expr->value_type;
			literal->source_loc = expr->source_loc;
			delete expr;
			return literal;
		}
	} else if((char)binary->oper == '^') {
		if(rhs)
			return maybe_optimize_pow(binary, expr->exprs[0], rhs);
		else if(lhs) {
			if(lhs->value.val_real == 2.0) {
				auto result = make_intrinsic_function_call(Value_Type::real, "pow2", make_cast(expr->exprs[1], Value_Type::real));
				expr->exprs.clear();
				delete lhs;
				delete expr;
				return result;
			} else if(lhs->value.val_real > 1e-16) {   // TODO: find a good epsilon here. We should check what the floating point error resulting from the replacement is for various epsilons.
				// if lhs is a literal > 0, replace    lhs^rhs with exp(ln(lhs)*rhs). Since the ln can be computed now, this function call is faster and easier for llvm to optimize.
				//    llvm does NOT do this reduction on its own, and it can have a huge impact on model speed.
				lhs->value.val_real = std::log(lhs->value.val_real);
				auto result = make_intrinsic_function_call(Value_Type::real, "exp", make_binop('*', lhs, make_cast(expr->exprs[1], Value_Type::real)));
				expr->exprs.clear();
				delete expr;
				return result;
			}
		}
	}
	return expr;
}

bool
is_divisive(Token_Type type) {
	return (char)type == '-' || (char)type == '/';
}

Math_Expr_FT *
binop_reassociate(Operator_FT *sup_op, Operator_FT *sub_op, Literal_FT *lit1, bool sup_literal_is_lhs) {
	
	// NOTE: Both sides of the sub expression are not literals, because that would have been reduced already.
	Literal_FT *lit2 = nullptr;
	Math_Expr_FT *nc   = nullptr;
	bool sub_literal_is_lhs = false;
	if(sub_op->exprs[0]->expr_type == Math_Expr_Type::literal) {
		lit2 = static_cast<Literal_FT *>(sub_op->exprs[0]);
		nc   = sub_op->exprs[1];
		sub_literal_is_lhs = true;
	} else if(sub_op->exprs[1]->expr_type == Math_Expr_Type::literal) {
		lit2 = static_cast<Literal_FT *>(sub_op->exprs[1]);
		nc   = sub_op->exprs[0];
	}
	if(!lit2) return sup_op;
	
	if(lit1->value_type != lit2->value_type)  // Could happen if the operator is a ^ for instance.
		return sup_op;//fatal_error(Mobius_Error::internal, "Tried re-associating values of different types.");
	
	s64 nc_parity = 1;
	if(sup_literal_is_lhs && is_divisive(sup_op->oper))
		nc_parity *= -1;
	if(sub_literal_is_lhs && is_divisive(sub_op->oper))
		nc_parity *= -1;
	
	s64 lit1_parity = 1;
	if(!sup_literal_is_lhs && is_divisive(sup_op->oper))
		lit1_parity = -1;
	
	s64 lit2_parity = 1;
	if(sup_literal_is_lhs && is_divisive(sup_op->oper))
		lit2_parity *= -1;
	if(!sub_literal_is_lhs && is_divisive(sub_op->oper))
		lit2_parity *= -1;
	
	Math_Expr_FT *result = sup_op;
	bool changed = false;
	if(((char)sup_op->oper == '+' || (char)sup_op->oper == '-') && ((char)sub_op->oper == '+' || (char)sub_op->oper == '-')) {
		changed = true;
		// lit1 +/- (nc +/- lit2)  etc.
		Math_Expr_FT *new_lit = nullptr;
		if(lit1->value_type == Value_Type::real)
			new_lit = make_literal((double)lit1_parity*lit1->value.val_real + (double)lit2_parity*lit2->value.val_real);
		else if(lit1->value_type == Value_Type::integer)
			new_lit = make_literal(lit1_parity*lit1->value.val_integer + lit2_parity*lit2->value.val_integer);
		else
			fatal_error(Mobius_Error::internal, "Somehow got a + or - for a boolean value");
		result = make_binop(nc_parity > 0 ? '+' : '-', new_lit, nc);
	} else if (lit1->value_type == Value_Type::real) {
		if(((char)sup_op->oper == '*' || (char)sup_op->oper == '/') && ((char)sub_op->oper == '*' || (char)sub_op->oper == '/')) {
			changed = true;
			double val1 = lit1->value.val_real;
			double val2 = lit2->value.val_real;
			// TODO: There may be some issues with floating point precision here...
			if(lit1_parity < 0 && lit2_parity < 0)
				result = make_binop('/', nc, make_literal(val1*val2));
			else {
				double val = val1*val2;
				if(lit1_parity < 0)
					val = val2/val1;
				else if(lit2_parity < 0)
					val = val1/val2;
				auto new_lit = make_literal(val);
				result = make_binop(nc_parity > 0 ? '*' : '/', new_lit, nc);
			}
		}
	} // TODO: could also do for integers if just multiplication, but that is not that common.
	if(changed) {
		sub_op->exprs.clear();
		sup_op->exprs.clear();
		delete sub_op;
		delete sup_op;
		delete lit1;
		delete lit2;
	}
	
	return result;
	
}

Math_Expr_FT *
binop_reduction_second_pass(Math_Expr_FT *expr) {
	// TODO: we could also try to re-associate recursively, but more complicated
	
	auto binary = static_cast<Operator_FT *>(expr);
	
	auto left = expr->exprs[0]->expr_type;
	auto right = expr->exprs[1]->expr_type;
	
	if(left == Math_Expr_Type::binary_operator && right == Math_Expr_Type::literal)
		return binop_reassociate(binary, static_cast<Operator_FT *>(expr->exprs[0]), static_cast<Literal_FT *>(expr->exprs[1]), false);
	else if(right == Math_Expr_Type::binary_operator && left == Math_Expr_Type::literal)
		return binop_reassociate(binary, static_cast<Operator_FT *>(expr->exprs[1]), static_cast<Literal_FT *>(expr->exprs[0]), true);
	return expr;
}

Math_Expr_FT *
prune_helper(Math_Expr_FT *expr, Function_Scope *scope) {
	
	// Try to simplify the math expression if some components can be evaluated to constants at compile time.
	//    Some of this could be left to llvm, but it is beneficial make the job easier for llvm, and we do see improvements from some of these optimizations.
	
	Function_Scope *old_scope = scope;
	Function_Scope new_scope;
	if(expr->expr_type == Math_Expr_Type::block) {
		new_scope.parent = scope;
		new_scope.block = static_cast<Math_Block_FT *>(expr);
		scope = &new_scope;
	}
	
	for(auto &arg : expr->exprs)
		arg = prune_helper(arg, scope);
	
	switch(expr->expr_type) {
		case Math_Expr_Type::block : {
			auto block = static_cast<Math_Block_FT *>(expr);
			if(block->is_for_loop
				&& block->exprs[0]->expr_type == Math_Expr_Type::literal
				&& static_cast<Literal_FT *>(block->exprs[0])->value.val_integer == 1) {   // Iteration count of for loop is 1, so the loop only executes once.
				
				delete block->exprs[0];
				auto result = block->exprs[1];
				result = replace_iteration_index(result, block->unique_block_id); // Replace the iteration index with constant 0 in the body.
				result = prune_helper(result, scope);  // We could maybe prune more now that more stuff could be constant.
				
				block->exprs.clear();
				delete block;
				return result;
			}
			if(!block->is_for_loop && block->exprs.size() == 1) {   // A block with a single statement can be replaced with that statement.
				auto result = block->exprs[0];
				block->exprs.clear();
				delete block;
				return result;
			}
			
			// TODO Could in some instances merge neighboring if blocks if they have the same condition, but it could be tricky.
		} break;
		
		case Math_Expr_Type::function_call : {
			auto fun = static_cast<Function_Call_FT *>(expr);
			if(fun->fun_type != Function_Type::intrinsic)           //TODO: implement for others.
				return expr;
			
			bool all_literal = true;
			for(auto arg : expr->exprs) {
				if(arg->expr_type != Math_Expr_Type::literal) { all_literal = false; break; }
			}
			if(all_literal) {
				auto literal = new Literal_FT();
				literal->source_loc = expr->source_loc;
				literal->value_type = expr->value_type;
				
				if(expr->exprs.size() == 1) {
					auto arg1 = static_cast<Literal_FT *>(expr->exprs[0]);
					literal->value = apply_intrinsic({arg1->value, arg1->value_type}, fun->fun_name);
					delete expr;
					return literal;
				} else if(expr->exprs.size() == 2) {
					auto arg1 = static_cast<Literal_FT *>(expr->exprs[0]);
					auto arg2 = static_cast<Literal_FT *>(expr->exprs[1]);
					literal->value = apply_intrinsic({arg1->value, arg1->value_type}, {arg2->value, arg2->value_type}, fun->fun_name);
					delete expr;
					return literal;
				} else
					fatal_error(Mobius_Error::internal, "Unhandled number of arguments to intrinsic.");
			}
		} break;
		
		case Math_Expr_Type::unary_operator : {
			// If the argument is a literal, just apply the operator directly on the unary and replace the unary with the literal.
			if(expr->exprs[0]->expr_type == Math_Expr_Type::literal) {
				auto unary = static_cast<Operator_FT *>(expr);
				auto arg = static_cast<Literal_FT *>(expr->exprs[0]);
				Parameter_Value val = apply_unary({arg->value, arg->value_type}, unary->oper);
				auto literal = new Literal_FT();
				literal->value = val;
				literal->value_type = expr->value_type;
				literal->source_loc = expr->source_loc;
				delete expr;
				return literal;
			}
			// TODO: could also reduce double negations etc.
		} break;
		
		case Math_Expr_Type::binary_operator : {
			// TODO: could reduce e.g.   a + -b  -> a - b etc, or 0-a = -a
			auto result = binop_reduction_first_pass(expr);
			if(result->expr_type == Math_Expr_Type::binary_operator)
				result = binop_reduction_second_pass(result);
			if(result->expr_type == Math_Expr_Type::binary_operator)
				result = binop_reduction_first_pass(result);  // In case the second pass reduced it to e.g. (0 + a)
			
			return result;
		} break;
	
		case Math_Expr_Type::if_chain : {
			
			if(expr->exprs.size() % 2 != 1)
				fatal_error(Mobius_Error::internal, "Got malformed if chain in prune_helper.");
			
			std::vector<Math_Expr_FT *> remains;
			bool found_true = false;
			for(int idx = 0; idx < (int)expr->exprs.size()-1; idx+=2) {
				bool is_false = false;
				bool is_true  = false;
				if(!found_true && expr->exprs[idx+1]->expr_type == Math_Expr_Type::literal) {
					auto literal = static_cast<Literal_FT *>(expr->exprs[idx+1]);
					if(literal->value.val_boolean) is_true  = true;
					else                        is_false = true;
				}
				
				// If this clause is constantly false, or a previous one was true, delete the value.
				// If this clause was true, this value becomes the 'otherwise'. Only delete clause, not value.
				if(is_false || found_true)
					delete expr->exprs[idx];
				else
					remains.push_back(expr->exprs[idx]);
				
				if(is_true || is_false || found_true)
					delete expr->exprs[idx+1];
				else
					remains.push_back(expr->exprs[idx+1]);
				
				if(is_true) found_true = true;
			}
			if(found_true)
				delete expr->exprs.back();
			else
				remains.push_back(expr->exprs.back());
			
			if(remains.size() == 1) {
				expr->exprs.clear(); //To not invoke destructor on children.
				delete expr;
				return remains[0];
			}
			expr->exprs = remains;
			return expr;
			
		} break;
		
		case Math_Expr_Type::cast : {
			if(expr->exprs[0]->expr_type == Math_Expr_Type::literal) {
				auto old_literal = static_cast<Literal_FT*>(expr->exprs[0]);
				auto literal = new Literal_FT();
				literal->value = apply_cast({old_literal->value, old_literal->value_type}, expr->value_type);
				literal->value_type = expr->value_type;
				literal->source_loc = expr->source_loc;
				delete expr;
				return literal;
			}
		} break;
		
		case Math_Expr_Type::identifier : {
			expr = potentially_prune_local(expr, scope);
		} break;
	}
	return expr;
}

void
remove_unused_locals(Math_Expr_FT *expr) {
	if(expr->expr_type == Math_Expr_Type::block) {
		expr->exprs.erase(std::remove_if(expr->exprs.begin(), expr->exprs.end(), [](Math_Expr_FT *arg) {
			if(arg->expr_type == Math_Expr_Type::local_var) {
				auto local = static_cast<Local_Var_FT *>(arg);
				if(!local->is_used) {
					delete local;
					return true;
				}
			}
			return false;
		}), expr->exprs.end());
	}
	for(auto arg : expr->exprs)
		remove_unused_locals(arg);
}

Math_Expr_FT *
prune_tree(Math_Expr_FT *expr, Function_Scope *scope, bool prune_unused_locals) {
	auto result = prune_helper(expr, scope);
	if(prune_unused_locals)
		remove_unused_locals(result);
	return result;
}


