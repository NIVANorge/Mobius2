
#ifndef MOBIUS_EMULATE_H
#define MOBIUS_EMULATE_H

struct Typed_Value : Parameter_Value {
	Value_Type type;
	
	Typed_Value() : Parameter_Value(), type(Value_Type::unresolved) {}
	Typed_Value(Parameter_Value a, Value_Type type) : Parameter_Value(a), type(type) {}
};

Typed_Value
apply_cast(Typed_Value val, Value_Type to_type);

Typed_Value
apply_unary(Typed_Value val, Token_Type oper);

Typed_Value
apply_binary(Typed_Value lhs, Typed_Value rhs, Token_Type oper);

Typed_Value
apply_intrinsic(Typed_Value a, String_View function);

Typed_Value
apply_intrinsic(Typed_Value a, Typed_Value b, String_View function);

struct Scope_Local_Vars;
struct Model_Application;

struct
Model_Run_State {
	Model_Application *model_app;
	Parameter_Value *parameters;
	//double *last_state_vars;
	double *state_vars;
	double *series;
	
	double *solver_workspace;
	
	Model_Run_State() : solver_workspace(nullptr) {}
};

Typed_Value
emulate_expression(Math_Expr_FT *expr, Model_Run_State *state, Scope_Local_Vars *locals);

void
emulate_model_run(Model_Application *model_app, s64 time_steps);

#endif // MOBIUS_EMULATE_H