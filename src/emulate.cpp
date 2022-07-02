
#include "function_tree.h"
#include "emulate.h"

#include <cmath>


Parameter_Value
apply_cast(Parameter_Value val, Value_Type from_type, Value_Type to_type) {
	Parameter_Value result;
	if(from_type == to_type) fatal_error(Mobius_Error::internal, "apply_cast() to same type."); // this should have been eliminated at a different stage.
	//NOTE: we don't cast to bool yet. May want to implement it later.
	if(to_type == Value_Type::boolean) fatal_error(Mobius_Error::internal, "apply_cast() to bool not implemented.");
	if(to_type == Value_Type::real) {
		if(from_type == Value_Type::integer)
			result.val_double = (double)val.val_int;
		else if(from_type == Value_Type::boolean)
			result.val_double = (double)val.val_bool;
	} else if(to_type == Value_Type::integer) {
		if(from_type == Value_Type::real)
			result.val_int = (s64)val.val_double;
		else if(from_type == Value_Type::boolean);
			result.val_int = (s64)val.val_bool;
	}
	return result;
}

Parameter_Value
apply_unary(Parameter_Value val, Value_Type from_type, Token_Type oper) {
	Parameter_Value result;
	if((char)oper == '-') {
		if(from_type == Value_Type::boolean) fatal_error(Mobius_Error::internal, "apply_unary() minus on boolean.");
		if(from_type == Value_Type::real) result.val_double = val.val_double;
		else if(from_type == Value_Type::integer) result.val_int = val.val_int;
	} else if((char)oper == '!') {
		if(from_type != Value_Type::boolean) fatal_error(Mobius_Error::internal, "apply_unary() negation on non-boolean.");
		result.val_bool = !val.val_bool;
	} else
		fatal_error(Mobius_Error::internal, "apply_unary() unhandled operator ", name(oper), " .");
	return result;
}

Parameter_Value
apply_binary(Parameter_Value lhs, Parameter_Value rhs, Value_Type value_type, Token_Type oper) {
	//TODO: should probably do type checking here too, but it SHOULD be correct from the function tree resolution already.
	Parameter_Value result;
	char op = (char)oper;
	//NOTE: this implementation doesn't allow for short-circuiting. Dunno if we want that.	
	if(op == '|')      result.val_bool = lhs.val_bool || rhs.val_bool; 
	else if(op == '&') result.val_bool = lhs.val_bool && rhs.val_bool;
	else if(op == '<') {
		if(value_type == Value_Type::integer)   result.val_bool = lhs.val_int < rhs.val_int;
		else if(value_type == Value_Type::real) result.val_bool = lhs.val_double < rhs.val_double;
	} else if(oper == Token_Type::leq) {
		if(value_type == Value_Type::integer)   result.val_bool = lhs.val_int <= rhs.val_int;
		else if(value_type == Value_Type::real) result.val_bool = lhs.val_double <= rhs.val_double;
	} else if(op == '>') {
		if(value_type == Value_Type::integer)   result.val_bool = lhs.val_int > rhs.val_int;
		else if(value_type == Value_Type::real) result.val_bool = lhs.val_double > rhs.val_double;
	} else if(oper == Token_Type::geq) {
		if(value_type == Value_Type::integer)   result.val_bool = lhs.val_int >= rhs.val_int;
		else if(value_type == Value_Type::real) result.val_bool = lhs.val_double >= rhs.val_double;
	} else if(oper == Token_Type::eq) {
		if(value_type == Value_Type::integer)   result.val_bool = lhs.val_int == rhs.val_int;
		else if(value_type == Value_Type::real) result.val_bool = lhs.val_double == rhs.val_double;
	} else if(oper == Token_Type::neq) {
		if(value_type == Value_Type::integer)   result.val_bool = lhs.val_int != rhs.val_int;
		else if(value_type == Value_Type::real) result.val_bool = lhs.val_double != rhs.val_double;
	} else if(op == '+') {
		if(value_type == Value_Type::integer)   result.val_int = lhs.val_int + rhs.val_int;
		else if(value_type == Value_Type::real) result.val_double = lhs.val_double + rhs.val_double;
	} else if(op == '-') {
		if(value_type == Value_Type::integer)   result.val_int = lhs.val_int - rhs.val_int;
		else if(value_type == Value_Type::real) result.val_double = lhs.val_double - rhs.val_double;
	} else if(op == '*') {
		if(value_type == Value_Type::integer)   result.val_int = lhs.val_int * rhs.val_int;
		else if(value_type == Value_Type::real) result.val_double = lhs.val_double * rhs.val_double;
	} else if(op == '/') {
		if(value_type == Value_Type::integer)   result.val_int = lhs.val_int / rhs.val_int;
		else if(value_type == Value_Type::real) result.val_double = lhs.val_double / rhs.val_double;
	} else if(op == '^') {
		if(value_type == Value_Type::integer)   result.val_double = std::pow(lhs.val_double, rhs.val_int);
		else if(value_type == Value_Type::real) result.val_double = std::pow(lhs.val_double, rhs.val_int);
	} else
		fatal_error(Mobius_Error::internal, "apply_binary() unhandled operator ", name(oper), " .");
	
	return result;
}

Parameter_Value
apply_intrinsic(Parameter_Value a, Parameter_Value b, Value_Type type, String_View function) {
	Parameter_Value result;
	bool ismin = function == "min";
	if(ismin || function == "max") {
		if(type == Value_Type::real) {
			if(ismin) result.val_double = a.val_double < b.val_double ? a.val_double : b.val_double;
			else      result.val_double = a.val_double > b.val_double ? a.val_double : b.val_double;
		} else if(type == Value_Type::integer) {
			if(ismin) result.val_int = a.val_int < b.val_int ? a.val_int : b.val_int;
			else      result.val_int = a.val_int > b.val_int ? a.val_int : b.val_int;
		} else {
			fatal_error(Mobius_Error::internal, "Somehow we got wrong type of arguments to \"", function, "\" in apply_intrinsic().");
		}
	} else
		fatal_error(Mobius_Error::internal, "Unhandled intrinsic \"", function, "\" in apply_intrinsic().");
	return result;
}

Parameter_Value
apply_intrinsic(Parameter_Value a, Value_Type type, String_View function) {
	Parameter_Value result;
	if(function == "exp") {
		if(type != Value_Type::real)
			fatal_error(Mobius_Error::internal, "Somehow we got wrong type of arguments to \"", function, "\" in apply_intrinsic().");
		result.val_double = std::exp(a.val_double);
	} else
		fatal_error(Mobius_Error::internal, "Unhandled intrinsic \"", function, "\" in apply_intrinsic().");
	
	return result;
}



Parameter_Value
emulate_expression(Math_Expr_FT *expr, Model_Run_State *state) {
	switch(expr->expr_type) {
		case Math_Expr_Type::block : {
			//TODO: incomplete, but ok before we get assignments
			return emulate_expression(expr->exprs.back(), state);
		} break;
		
		case Math_Expr_Type::identifier_chain : {
			auto ident = reinterpret_cast<Identifier_FT *>(expr);
			//TODO: instead we have to find a way to convert the id to a local index. This is just for early testing.
			Parameter_Value result;
			if(ident->variable_type == Variable_Type::parameter)
				return state->parameters[ident->parameter.id];
			else if(ident->variable_type == Variable_Type::state_var)
				result.val_double = state->state_vars[ident->state_var];
			else if(ident->variable_type == Variable_Type::input_series)
				result.val_double = state->series[ident->series];
			return result;
		} break;
		
		case Math_Expr_Type::literal : {
			auto literal = reinterpret_cast<Literal_FT *>(expr);
			return literal->value;
		} break;
		
		case Math_Expr_Type::unary_operator : {
			auto unary = reinterpret_cast<Operator_FT *>(expr);
			Parameter_Value a = emulate_expression(expr->exprs[0], state);
			return apply_unary(a, expr->exprs[0]->value_type, unary->oper);
		} break;
		
		case Math_Expr_Type::binary_operator : {
			auto binary = reinterpret_cast<Operator_FT *>(expr);
			Parameter_Value a = emulate_expression(expr->exprs[0], state);
			Parameter_Value b = emulate_expression(expr->exprs[1], state);
			return apply_binary(a, b, expr->exprs[1]->value_type, binary->oper);
		} break;
		
		case Math_Expr_Type::function_call : {
			auto fun = reinterpret_cast<Function_Call_FT *>(expr);
			if(fun->fun_type != Function_Type::intrinsic)
				fatal_error(Mobius_Error::internal, "Unhandled function type in emulate_expression().");
			
			if(fun->exprs.size() == 1) {
				Parameter_Value a = emulate_expression(fun->exprs[0], state);
				return apply_intrinsic(a, fun->exprs[0]->value_type, fun->fun_name);
			} else if(fun->exprs.size() == 2) {
				Parameter_Value a = emulate_expression(fun->exprs[0], state);
				Parameter_Value b = emulate_expression(fun->exprs[1], state);
				return apply_intrinsic(a, b, fun->exprs[0]->value_type, fun->fun_name);
			} else
				fatal_error(Mobius_Error::internal, "Unhandled number of function arguments in emulate_expression().");
		} break;

		case Math_Expr_Type::if_chain : {
			for(int idx = 0; idx < expr->exprs.size()-1; idx+=2) {
				Parameter_Value cond = emulate_expression(expr->exprs[idx+1], state);
				if(cond.val_bool) return emulate_expression(expr->exprs[idx], state);
				return emulate_expression(expr->exprs.back(), state);
			}
		} break;
		
		case Math_Expr_Type::cast : {
			Parameter_Value a = emulate_expression(expr->exprs[0], state);
			return apply_cast(a, expr->exprs[0]->value_type, expr->value_type);
		} break;
	}
	
	fatal_error(Mobius_Error::internal, "Didn't emulate ", name(expr->expr_type), " expression.");
	
	return Parameter_Value();
}



