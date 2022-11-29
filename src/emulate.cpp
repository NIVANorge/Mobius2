
#include "function_tree.h"
#include "emulate.h"
#include "model_application.h"

#include <cmath>


Typed_Value
apply_cast(Typed_Value val, Value_Type to_type) {
	Typed_Value result;
	result.type = to_type;
	if(val.type == to_type) fatal_error(Mobius_Error::internal, "apply_cast() to same type."); // this should have been eliminated at a different stage.
	if(to_type == Value_Type::boolean) {
		if(val.type == Value_Type::real)
			result.val_boolean = (bool)val.val_real;
		else if(val.type == Value_Type::integer)
			result.val_boolean = (bool)val.val_integer;
	} else if(to_type == Value_Type::real) {
		if(val.type == Value_Type::integer)
			result.val_real = (double)val.val_integer;
		else if(val.type == Value_Type::boolean)
			result.val_real = (double)val.val_boolean;
	} else if(to_type == Value_Type::integer) {
		if(val.type == Value_Type::real)
			result.val_integer = (s64)val.val_real;
		else if(val.type == Value_Type::boolean);
			result.val_integer = (s64)val.val_boolean;
	}
	return result;
}

Typed_Value
apply_unary(Typed_Value val, Token_Type oper) {
	Typed_Value result;
	result.type = val.type;
	if((char)oper == '-') {
		if(val.type == Value_Type::boolean) fatal_error(Mobius_Error::internal, "apply_unary() minus on boolean.");
		else if(val.type == Value_Type::real)    result.val_real    = -val.val_real;
		else if(val.type == Value_Type::integer) result.val_integer = -val.val_integer;
	} else if((char)oper == '!') {
		if(val.type != Value_Type::boolean) fatal_error(Mobius_Error::internal, "apply_unary() negation on non-boolean.");
		result.val_boolean = !val.val_boolean;
	} else
		fatal_error(Mobius_Error::internal, "apply_unary() unhandled operator ", name(oper), " .");
	return result;
}

Typed_Value
apply_binary(Typed_Value lhs, Typed_Value rhs, Token_Type oper) {
	Typed_Value result;
	char op = (char)oper;
	if(op != '^' && lhs.type != rhs.type) fatal_error(Mobius_Error::internal, "Mismatching types in apply_binary(). lhs: ", name(lhs.type), ", rhs: ", name(rhs.type), "."); //this should have been eliminated at a different stage.
	
	//NOTE: this implementation doesn't allow for short-circuiting. Dunno if we want that.	
	
	//TODO: should probably do type checking here too, but it SHOULD be correct from the function tree resolution already.
	if(op == '|')      result.val_boolean = lhs.val_boolean || rhs.val_boolean; 
	else if(op == '&') result.val_boolean = lhs.val_boolean && rhs.val_boolean;
	else if(op == '<') {
		if(lhs.type == Value_Type::integer)   result.val_boolean = lhs.val_integer < rhs.val_integer;
		else if(lhs.type == Value_Type::real) result.val_boolean = lhs.val_real < rhs.val_real;
		result.type = Value_Type::boolean;
	} else if(oper == Token_Type::leq) {
		if(lhs.type == Value_Type::integer)   result.val_boolean = lhs.val_integer <= rhs.val_integer;
		else if(lhs.type == Value_Type::real) result.val_boolean = lhs.val_real <= rhs.val_real;
		result.type = Value_Type::boolean;
	} else if(op == '>') {
		if(lhs.type == Value_Type::integer)   result.val_boolean = lhs.val_integer > rhs.val_integer;
		else if(lhs.type == Value_Type::real) result.val_boolean = lhs.val_real > rhs.val_real;
		result.type = Value_Type::boolean;
	} else if(oper == Token_Type::geq) {
		if(lhs.type == Value_Type::integer)   result.val_boolean = lhs.val_integer >= rhs.val_integer;
		else if(lhs.type == Value_Type::real) result.val_boolean = lhs.val_real >= rhs.val_real;
		result.type = Value_Type::boolean;
	} else if(op == '=') {
		if(lhs.type == Value_Type::integer)   result.val_boolean = lhs.val_integer == rhs.val_integer;
		else if(lhs.type == Value_Type::real) result.val_boolean = lhs.val_real == rhs.val_real;
		result.type = Value_Type::boolean;
	} else if(oper == Token_Type::neq) {
		if(lhs.type == Value_Type::integer)   result.val_boolean = lhs.val_integer != rhs.val_integer;
		else if(lhs.type == Value_Type::real) result.val_boolean = lhs.val_real != rhs.val_real;
		result.type = Value_Type::boolean;
	} else if(op == '+') {
		if(lhs.type == Value_Type::integer)   result.val_integer = lhs.val_integer + rhs.val_integer;
		else if(lhs.type == Value_Type::real) result.val_real = lhs.val_real + rhs.val_real;
		result.type = lhs.type;
	} else if(op == '-') {
		if(lhs.type == Value_Type::integer)   result.val_integer = lhs.val_integer - rhs.val_integer;
		else if(lhs.type == Value_Type::real) result.val_real = lhs.val_real - rhs.val_real;
		result.type = lhs.type;
	} else if(op == '*') {
		if(lhs.type == Value_Type::integer)   result.val_integer = lhs.val_integer * rhs.val_integer;
		else if(lhs.type == Value_Type::real) result.val_real = lhs.val_real * rhs.val_real;
		result.type = lhs.type;
	} else if(op == '/') {
		if(lhs.type == Value_Type::integer)   result.val_integer = lhs.val_integer / rhs.val_integer;
		else if(lhs.type == Value_Type::real) result.val_real = lhs.val_real / rhs.val_real;
		result.type = lhs.type;
	} else if(op == '^') {
		if(rhs.type == Value_Type::integer)   result.val_real = std::pow(lhs.val_real, rhs.val_integer);
		else if(rhs.type == Value_Type::real) result.val_real = std::pow(lhs.val_real, rhs.val_real);
		result.type = Value_Type::real;
	} else if(op == '%') {
		result.val_integer = lhs.val_integer % rhs.val_integer;
		result.type = Value_Type::integer;
	} else
		fatal_error(Mobius_Error::internal, "apply_binary() unhandled operator ", name(oper), " .");
	
	return result;
}

Typed_Value
apply_intrinsic(Typed_Value a, Typed_Value b, String_View function) {
	
	//TODO: use MAKE_INTRINSIC2 macro here!
	
	Typed_Value result;
	bool ismin = function == "min";
	if(ismin || function == "max") {
		if(a.type != b.type) fatal_error(Mobius_Error::internal, "Mismatching types in apply_intrinsic()."); //this should have been eliminated at a different stage.
		
		result.type = a.type;
		if(a.type == Value_Type::real) {
			if(ismin) result.val_real = a.val_real < b.val_real ? a.val_real : b.val_real;
			else      result.val_real = a.val_real > b.val_real ? a.val_real : b.val_real;
		} else if(a.type == Value_Type::integer) {
			if(ismin) result.val_integer = a.val_integer < b.val_integer ? a.val_integer : b.val_integer;
			else      result.val_integer = a.val_integer > b.val_integer ? a.val_integer : b.val_integer;
		} else {
			fatal_error(Mobius_Error::internal, "Somehow we got wrong type of arguments to \"", function, "\" in apply_intrinsic(a, b).");
		}
	} else
		fatal_error(Mobius_Error::internal, "Unhandled intrinsic \"", function, "\" in apply_intrinsic(a, b).");
	return result;
}

Typed_Value
apply_intrinsic(Typed_Value a, String_View function) {
	Typed_Value result;
	if(false) {}
	#define MAKE_INTRINSIC1(name, emul, llvm, ret_type, type1) \
		else if(function == #name) { \
			result.type = Value_Type::ret_type; \
			if(a.type != Value_Type::type1) \
				fatal_error(Mobius_Error::internal, "Somehow we got wrong type of arguments to \"", function, "\" in apply_intrinsic()."); \
			result.val_##ret_type = std::emul(a.val_##type1); \
		}
	#define MAKE_INTRINSIC2(name, emul, ret_type, type1, type2)
	#include "intrinsics.incl"
	#undef MAKE_INTRINSIC1
	#undef MAKE_INTRINSIC2
	else
		fatal_error(Mobius_Error::internal, "Unhandled intrinsic \"", function, "\" in apply_intrinsic(a).");
	
	return result;
}

Typed_Value
check_binop_reduction(Source_Location loc, Token_Type oper, Parameter_Value val, Value_Type type, bool is_lhs) {
	Typed_Value result;
	result.type = Value_Type::unresolved;
	char op = (char)oper;
	if(op == '+' || (op == '-' && !is_lhs)) { // We could also reduce 0 - a to -a, but it is probably not that useful
		if(type == Value_Type::real) {
			if(val.val_real == 0.0)
				result.type = Value_Type::none;     // Signal to keep the other argument as the result
		} else if (type == Value_Type::integer) {
			if(val.val_integer = 0)
				result.type = Value_Type::none;
		}
	} else if (op == '*') {
		// Note: technically mul by 0 should give nan if other operand is inf or nan, but we don't care about it since then we have an invalid model run anyway.
		if(type == Value_Type::real) {
			if(val.val_real == 0.0) {
				result.type = Value_Type::real;
				result.val_real = 0.0;
			} else if (val.val_real == 1.0)
				result.type = Value_Type::none; 
		} else if (type == Value_Type::integer) {
			if(val.val_integer = 0) {
				result.type = Value_Type::integer;
				result.val_integer = 0;
			} else if (val.val_integer == 1)
				result.type = Value_Type::none;
		}
	} else if (op == '/') {
		if(type == Value_Type::real) {
			if(val.val_real == 0.0) {    
				result.type = Value_Type::real;
				if(is_lhs)
					result.val_real = 0.0;
				else {
					loc.print_error_header();
					fatal_error("This expression always evaluates to a division by zero.");
				}
			} else if (val.val_real == 1.0 && !is_lhs)
				result.type = Value_Type::none; 
		} else if (type == Value_Type::integer) {
			if(val.val_integer = 0) {
				if(is_lhs) {
					result.type = Value_Type::integer;
					result.val_integer = 0;
				} else {
					loc.print_error_header();
					fatal_error("This expression always evaluates to a division by zero.");
				}
			} else if (val.val_integer == 1 && !is_lhs)
				result.type = Value_Type::none;
		}
	} else if (op == '^') {
		if(type == Value_Type::real) {
			if((val.val_real == 1.0 && is_lhs)  ||  (val.val_real == 0.0 && !is_lhs)) {    // 1^a = 1,  a^0 = 1
				result.type = Value_Type::real;
				result.val_real = 1.0;
			} else if (val.val_real == 1.0 && !is_lhs)     // a^1 = a
				result.type = Value_Type::none;
		} else if(type == Value_Type::integer) {
			if(val.val_integer == 0 && !is_lhs) {     // NOTE: lhs can't be int for this operator
				result.type = Value_Type::integer;
				result.val_real = 1;
			} else if (val.val_integer == 1 && !is_lhs)
				result.type = Value_Type::none;
		}
	} else if (op == '&') {
		result.type = Value_Type::boolean;
		result.val_boolean = val.val_boolean;
	} else if (op == '|') {
		if(result.val_boolean) {
			result.type = Value_Type::boolean;
			result.val_boolean = true;
		} else
			result.type = Value_Type::none;
	}
	return result;
}

struct
Scope_Local_Vars {
	s32 scope_id;
	Scope_Local_Vars *scope_up;
	std::vector<Typed_Value> local_vars;
};

Typed_Value
get_local_var(Scope_Local_Vars *scope, s32 index, s32 scope_id) {
	if(!scope)
		fatal_error(Mobius_Error::internal, "Mis-counting of scopes in emulation.");
	while(scope->scope_id != scope_id) {
		scope = scope->scope_up;
		if(!scope)
			fatal_error(Mobius_Error::internal, "Mis-counting of scopes in emulation.");
	}
	if(index >= scope->local_vars.size())
		fatal_error(Mobius_Error::internal, "Mis-counting of local variables in emulation.");
	return scope->local_vars[index];
}


#define DO_DEBUG 0
#if DO_DEBUG
	#define DEBUG(a) a;
#else
	#define DEBUG(a)
#endif

Typed_Value
emulate_expression(Math_Expr_FT *expr, Model_Run_State *state, Scope_Local_Vars *locals) {
	if(!expr)
		fatal_error(Mobius_Error::internal, "Got a nullptr expression in emulate_expression().");
	
	DEBUG(warning_print("emulate expression of type ", name(expr->expr_type), "\n"))
	
	switch(expr->expr_type) {
		case Math_Expr_Type::block : {
			auto block= reinterpret_cast<Math_Block_FT *>(expr);
			Typed_Value result;
			Scope_Local_Vars new_locals;
			new_locals.scope_id = block->unique_block_id;
			new_locals.scope_up = locals;
			new_locals.local_vars.resize(block->n_locals);
			if(!block->is_for_loop) {
				int index = 0;
				for(auto sub_expr : expr->exprs) {
					result = emulate_expression(sub_expr, state, &new_locals);
					if(sub_expr->expr_type == Math_Expr_Type::local_var) {
						new_locals.local_vars[index] = result;
						++index;
					}
				}
			} else {
				//DEBUG(warning_print("for loop with ", block->n_locals, " locals\n"))
				s64 n = emulate_expression(expr->exprs[0], state, locals).val_integer;
				for(s64 i = 0; i < n; ++i) {
					new_locals.local_vars[0].val_integer = i;
					new_locals.local_vars[0].type = Value_Type::integer;
					result = emulate_expression(expr->exprs[1], state, &new_locals);
				}
			}
			return result;
		} break;
		
		case Math_Expr_Type::local_var : {
			return emulate_expression(expr->exprs[0], state, locals);
		} break;
		
		case Math_Expr_Type::identifier_chain : {
			auto ident = reinterpret_cast<Identifier_FT *>(expr);
			Typed_Value result;
			result.type = expr->value_type;
			s64 offset = 0;
			if(ident->variable_type == Variable_Type::parameter || ident->variable_type == Variable_Type::state_var || ident->variable_type == Variable_Type::series
				|| ident->variable_type == Variable_Type::connection_info) {
				DEBUG(warning_print("lookup var offset.\n"))
				offset = emulate_expression(expr->exprs[0], state, locals).val_integer;
			}
			//warning_print("offset for lookup was ", offset, "\n");
			
			Parameter_Value val;
			switch(ident->variable_type) {
				case Variable_Type::parameter: {
					result = Typed_Value {state->parameters[offset], expr->value_type};
				} break;
				
				case Variable_Type::state_var : {
					result.val_real = state->state_vars[offset];
				} break;
				
				case Variable_Type::series : {
					result.val_real = state->series[offset];
				} break;
				
				case Variable_Type::connection_info : {
					result.val_integer = state->connection_info[offset];
				} break;
				
				case Variable_Type::local : {
					result = get_local_var(locals, ident->local_var.index, ident->local_var.scope_id);
				} break;
				
				#define TIME_VALUE(name, bits)\
				case Variable_Type::time_##name : { \
					val.val_integer = state->date_time.name; \
					result = Typed_Value {val, expr->value_type}; \
				} break;
				#include "time_values.incl"
				#undef TIME_VALUE			
				
				default : {
					fatal_error(Mobius_Error::internal, "Unhandled variable type in emulate_expression().");
				}
			}
			return result;
		} break;
		
		case Math_Expr_Type::literal : {
			auto literal = reinterpret_cast<Literal_FT *>(expr);
			return {literal->value, literal->value_type};
		} break;
		
		case Math_Expr_Type::unary_operator : {
			auto unary = reinterpret_cast<Operator_FT *>(expr);
			Typed_Value a = emulate_expression(expr->exprs[0], state, locals);
			return apply_unary(a, unary->oper);
		} break;
		
		case Math_Expr_Type::binary_operator : {
			auto binary = reinterpret_cast<Operator_FT *>(expr);
			Typed_Value a = emulate_expression(expr->exprs[0], state, locals);
			Typed_Value b = emulate_expression(expr->exprs[1], state, locals);
			return apply_binary(a, b, binary->oper);
		} break;
		
		case Math_Expr_Type::function_call : {
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
		} break;

		case Math_Expr_Type::if_chain : {
			for(int idx = 0; idx < expr->exprs.size()-1; idx+=2) {
				Typed_Value cond = emulate_expression(expr->exprs[idx+1], state, locals);
				if(cond.val_boolean) return emulate_expression(expr->exprs[idx], state, locals);
				return emulate_expression(expr->exprs.back(), state, locals);
			}
		} break;
		
		case Math_Expr_Type::state_var_assignment : {
			Typed_Value index = emulate_expression(expr->exprs[0], state, locals);
			Typed_Value value = emulate_expression(expr->exprs[1], state, locals);
			state->state_vars[index.val_integer] = value.val_real;
			//warning_print("offset for val assignment was ", index.val_integer, "\n");
			return {Parameter_Value(), Value_Type::none};
		} break;
		
		case Math_Expr_Type::derivative_assignment : {
			Typed_Value index = emulate_expression(expr->exprs[0], state, locals);
			Typed_Value value = emulate_expression(expr->exprs[1], state, locals);
			state->solver_workspace[index.val_integer] = value.val_real;
			//warning_print("offset for deriv assignment was ", index.val_integer, "\n");
			return {Parameter_Value(), Value_Type::none};
		} break;
		
		case Math_Expr_Type::cast : {
			Typed_Value a = emulate_expression(expr->exprs[0], state, locals);
			return apply_cast(a, expr->value_type);
		} break;
	}
	
	fatal_error(Mobius_Error::internal, "Didn't emulate ", name(expr->expr_type), " expression.");
	
	return {Parameter_Value(), Value_Type::unresolved};
}

