
#ifndef MOBIUS_EMULATE_H
#define MOBIUS_EMULATE_H

#include "common_types.h"

#include "function_tree.h"

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
apply_intrinsic(Typed_Value a, Typed_Value b, String_View function, Model_Run_State *state=nullptr);

//struct Scope_Local_Vars;
struct Model_Application;
struct Model_Run_State;
//struct Math_Expr_FT;

Typed_Value
emulate_expression(Math_Expr_FT *expr, Model_Run_State *state, Scope_Local_Vars<Typed_Value> *locals);

#endif // MOBIUS_EMULATE_H