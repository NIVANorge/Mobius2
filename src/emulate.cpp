
#include "function_tree.h"
#include "emulate.h"

#include <cmath>


Typed_Value
apply_cast(Typed_Value val, Value_Type to_type) {
	Typed_Value result;
	result.type = to_type;
	if(val.type == to_type) fatal_error(Mobius_Error::internal, "apply_cast() to same type."); // this should have been eliminated at a different stage.
	if(to_type == Value_Type::boolean) fatal_error(Mobius_Error::internal, "apply_cast() to bool not implemented."); // we don't cast to bool yet. May want to implement it later, but it is not really necessary.
	if(to_type == Value_Type::real) {
		if(val.type == Value_Type::integer)
			result.val_double = (double)val.val_int;
		else if(val.type == Value_Type::boolean)
			result.val_double = (double)val.val_bool;
	} else if(to_type == Value_Type::integer) {
		if(val.type == Value_Type::real)
			result.val_int = (s64)val.val_double;
		else if(val.type == Value_Type::boolean);
			result.val_int = (s64)val.val_bool;
	}
	return result;
}

Typed_Value
apply_unary(Typed_Value val, Token_Type oper) {
	Typed_Value result;
	result.type = val.type;
	if((char)oper == '-') {
		if(val.type == Value_Type::boolean) fatal_error(Mobius_Error::internal, "apply_unary() minus on boolean.");
		else if(val.type == Value_Type::real) result.val_double = val.val_double;
		else if(val.type == Value_Type::integer) result.val_int = val.val_int;
	} else if((char)oper == '!') {
		if(val.type != Value_Type::boolean) fatal_error(Mobius_Error::internal, "apply_unary() negation on non-boolean.");
		result.val_bool = !val.val_bool;
	} else
		fatal_error(Mobius_Error::internal, "apply_unary() unhandled operator ", name(oper), " .");
	return result;
}

Typed_Value
apply_binary(Typed_Value lhs, Typed_Value rhs, Token_Type oper) {
	Typed_Value result;
	char op = (char)oper;
	if(op != '^' && lhs.type != rhs.type) fatal_error(Mobius_Error::internal, "Mismatching types in apply_binary()."); //this should have been eliminated at a different stage.
	result.type = lhs.type;
	//NOTE: this implementation doesn't allow for short-circuiting. Dunno if we want that.	
	
	//TODO: should probably do type checking here too, but it SHOULD be correct from the function tree resolution already.
	if(op == '|')      result.val_bool = lhs.val_bool || rhs.val_bool; 
	else if(op == '&') result.val_bool = lhs.val_bool && rhs.val_bool;
	else if(op == '<') {
		if(lhs.type == Value_Type::integer)   result.val_bool = lhs.val_int < rhs.val_int;
		else if(lhs.type == Value_Type::real) result.val_bool = lhs.val_double < rhs.val_double;
	} else if(oper == Token_Type::leq) {
		if(lhs.type == Value_Type::integer)   result.val_bool = lhs.val_int <= rhs.val_int;
		else if(lhs.type == Value_Type::real) result.val_bool = lhs.val_double <= rhs.val_double;
	} else if(op == '>') {
		if(lhs.type == Value_Type::integer)   result.val_bool = lhs.val_int > rhs.val_int;
		else if(lhs.type == Value_Type::real) result.val_bool = lhs.val_double > rhs.val_double;
	} else if(oper == Token_Type::geq) {
		if(lhs.type == Value_Type::integer)   result.val_bool = lhs.val_int >= rhs.val_int;
		else if(lhs.type == Value_Type::real) result.val_bool = lhs.val_double >= rhs.val_double;
	} else if(oper == Token_Type::eq) {
		if(lhs.type == Value_Type::integer)   result.val_bool = lhs.val_int == rhs.val_int;
		else if(lhs.type == Value_Type::real) result.val_bool = lhs.val_double == rhs.val_double;
	} else if(oper == Token_Type::neq) {
		if(lhs.type == Value_Type::integer)   result.val_bool = lhs.val_int != rhs.val_int;
		else if(lhs.type == Value_Type::real) result.val_bool = lhs.val_double != rhs.val_double;
	} else if(op == '+') {
		if(lhs.type == Value_Type::integer)   result.val_int = lhs.val_int + rhs.val_int;
		else if(lhs.type == Value_Type::real) result.val_double = lhs.val_double + rhs.val_double;
	} else if(op == '-') {
		if(lhs.type == Value_Type::integer)   result.val_int = lhs.val_int - rhs.val_int;
		else if(lhs.type == Value_Type::real) result.val_double = lhs.val_double - rhs.val_double;
	} else if(op == '*') {
		if(lhs.type == Value_Type::integer)   result.val_int = lhs.val_int * rhs.val_int;
		else if(lhs.type == Value_Type::real) result.val_double = lhs.val_double * rhs.val_double;
	} else if(op == '/') {
		if(lhs.type == Value_Type::integer)   result.val_int = lhs.val_int / rhs.val_int;
		else if(lhs.type == Value_Type::real) result.val_double = lhs.val_double / rhs.val_double;
	} else if(op == '^') {
		if(rhs.type == Value_Type::integer)   result.val_double = std::pow(lhs.val_double, rhs.val_int);
		else if(rhs.type == Value_Type::real) result.val_double = std::pow(lhs.val_double, rhs.val_double);
		result.type = Value_Type::real;
	} else
		fatal_error(Mobius_Error::internal, "apply_binary() unhandled operator ", name(oper), " .");
	
	return result;
}

Typed_Value
apply_intrinsic(Typed_Value a, Typed_Value b, String_View function) {
	Typed_Value result;
	bool ismin = function == "min";
	if(ismin || function == "max") {
		if(a.type != b.type) fatal_error(Mobius_Error::internal, "Mismatching types in apply_intrinsic()."); //this should have been eliminated at a different stage.
		
		result.type = a.type;
		if(a.type == Value_Type::real) {
			if(ismin) result.val_double = a.val_double < b.val_double ? a.val_double : b.val_double;
			else      result.val_double = a.val_double > b.val_double ? a.val_double : b.val_double;
		} else if(a.type == Value_Type::integer) {
			if(ismin) result.val_int = a.val_int < b.val_int ? a.val_int : b.val_int;
			else      result.val_int = a.val_int > b.val_int ? a.val_int : b.val_int;
		} else {
			fatal_error(Mobius_Error::internal, "Somehow we got wrong type of arguments to \"", function, "\" in apply_intrinsic().");
		}
	} else
		fatal_error(Mobius_Error::internal, "Unhandled intrinsic \"", function, "\" in apply_intrinsic().");
	return result;
}

Typed_Value
apply_intrinsic(Typed_Value a, String_View function) {
	Typed_Value result;
	result.type = Value_Type::real;
	if(function == "exp") {
		if(a.type != Value_Type::real)
			fatal_error(Mobius_Error::internal, "Somehow we got wrong type of arguments to \"", function, "\" in apply_intrinsic().");
		result.val_double = std::exp(a.val_double);
	} else
		fatal_error(Mobius_Error::internal, "Unhandled intrinsic \"", function, "\" in apply_intrinsic().");
	
	return result;
}



Typed_Value
emulate_expression(Math_Expr_FT *expr, Model_Run_State *state) {
	switch(expr->expr_type) {
		case Math_Expr_Type::block : {
			//TODO: incomplete, but ok before we get assignments
			return emulate_expression(expr->exprs.back(), state);
		} break;
		
		case Math_Expr_Type::identifier_chain : {
			auto ident = reinterpret_cast<Identifier_FT *>(expr);
			//TODO: instead we have to find a way to convert the id to a local index. This is just for early testing.
			Typed_Value result;
			result.type = expr->value_type;
			if(ident->variable_type == Variable_Type::parameter)
				return {state->parameters[ident->parameter.id], expr->value_type};
			else if(ident->variable_type == Variable_Type::state_var)
				result.val_double = state->state_vars[ident->state_var.id];
			else if(ident->variable_type == Variable_Type::series)
				result.val_double = state->series[ident->series.id];
			return result;
		} break;
		
		case Math_Expr_Type::literal : {
			auto literal = reinterpret_cast<Literal_FT *>(expr);
			return {literal->value, literal->value_type};
		} break;
		
		case Math_Expr_Type::unary_operator : {
			auto unary = reinterpret_cast<Operator_FT *>(expr);
			Typed_Value a = emulate_expression(expr->exprs[0], state);
			return apply_unary(a, unary->oper);
		} break;
		
		case Math_Expr_Type::binary_operator : {
			auto binary = reinterpret_cast<Operator_FT *>(expr);
			Typed_Value a = emulate_expression(expr->exprs[0], state);
			Typed_Value b = emulate_expression(expr->exprs[1], state);
			return apply_binary(a, b, binary->oper);
		} break;
		
		case Math_Expr_Type::function_call : {
			auto fun = reinterpret_cast<Function_Call_FT *>(expr);
			if(fun->fun_type != Function_Type::intrinsic)
				fatal_error(Mobius_Error::internal, "Unhandled function type in emulate_expression().");
			
			if(fun->exprs.size() == 1) {
				Typed_Value a = emulate_expression(fun->exprs[0], state);
				return apply_intrinsic(a, fun->fun_name);
			} else if(fun->exprs.size() == 2) {
				Typed_Value a = emulate_expression(fun->exprs[0], state);
				Typed_Value b = emulate_expression(fun->exprs[1], state);
				return apply_intrinsic(a, b, fun->fun_name);
			} else
				fatal_error(Mobius_Error::internal, "Unhandled number of function arguments in emulate_expression().");
		} break;

		case Math_Expr_Type::if_chain : {
			for(int idx = 0; idx < expr->exprs.size()-1; idx+=2) {
				Typed_Value cond = emulate_expression(expr->exprs[idx+1], state);
				if(cond.val_bool) return emulate_expression(expr->exprs[idx], state);
				return emulate_expression(expr->exprs.back(), state);
			}
		} break;
		
		case Math_Expr_Type::cast : {
			Typed_Value a = emulate_expression(expr->exprs[0], state);
			return apply_cast(a, expr->value_type);
		} break;
	}
	
	fatal_error(Mobius_Error::internal, "Didn't emulate ", name(expr->expr_type), " expression.");
	
	return {Parameter_Value(), Value_Type::unresolved};
}



void emulate_model_run(Mobius_Model *model) {
	//TODO: check that model is composed
	
	//TODO: finish this later!
	/*
	Model_Run_State run_state;
	run_state.parameters = (Parameter_Value *)malloc(sizeof(Parameter_Value)*module->parameters.registrations.size());
	run_state.parameters[0].val_double = 5.1;
	run_state.parameters[1].val_double = 3.2;
	run_state.state_vars = (double *)malloc(sizeof(double)*model.state_vars.vars.size());
	run_state.series     = (double *)malloc(sizeof(double)*100); //TODO.
	run_state.series[0] = 2.0;
	*/
}

