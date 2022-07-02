
#ifndef MOBIUS_EMULATE_H
#define MOBIUS_EMULATE_H

Parameter_Value
apply_cast(Parameter_Value val, Value_Type from_type, Value_Type to_type);

Parameter_Value
apply_unary(Parameter_Value val, Value_Type from_type, Token_Type oper);

Parameter_Value
apply_binary(Parameter_Value lhs, Parameter_Value rhs, Value_Type type, Token_Type oper);

Parameter_Value
apply_intrinsic(Parameter_Value a, Value_Type type, String_View function);

Parameter_Value
apply_intrinsic(Parameter_Value a, Parameter_Value b, Value_Type type, String_View function);


struct
Model_Run_State {
	Parameter_Value *parameters;
	double *state_vars;
	double *series;
};


Parameter_Value
emulate_expression(Math_Expr_FT *expr, Model_Run_State *state);

#endif // MOBIUS_EMULATE_H