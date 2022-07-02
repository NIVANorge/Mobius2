

#ifndef MOBIUS_FUNCTION_TREE_H
#define MOBIUS_FUNCTION_TREE_H

#include "module_declaration.h"

//NOTE: this is really just a copy of the AST, but we want to put additional data on it.
//NOTE: we *could* put the extra data in the AST, but it is not that clean (?)
// however we may want to copy the tree any way when we resolve properties that are attached to multiple other entities.

//TODO: Memory leak! Need to free sub-nodes when destructing!

typedef s32 state_var_id;

enum class
Value_Type {
	unresolved = 0, real, integer, boolean,    // NOTE: enum would resolve to bool.
};

inline Value_Type
get_value_type(Decl_Type decl_type) {
	if(decl_type == Decl_Type::par_real) return Value_Type::real;
	//TODO: fill in for other parameters!
	
	return Value_Type::unresolved;
}

inline Value_Type
get_value_type(Token_Type type) {
	if(type == Token_Type::real) return Value_Type::real;
	else if(type == Token_Type::integer) return Value_Type::integer;
	else if(type == Token_Type::boolean) return Value_Type::boolean;
	
	return Value_Type::unresolved;
}

enum class
Variable_Type {
	parameter, input_series, state_var,  // Maybe also computed_parameter eventually. Also local.
};


struct
Math_Expr_FT {
	Math_Expr_Type               expr_type;
	Value_Type                   value_type;
	Entity_Id                    unit;
	Source_Location              location;
	std::vector<Math_Expr_FT *>  exprs;
	
	Math_Expr_FT() : value_type(Value_Type::unresolved) {};
};

struct
Math_Block_FT : Math_Expr_FT {
	//TODO: scope info
	
	Math_Block_FT() : Math_Expr_FT() { expr_type = Math_Expr_Type::block; };
};

struct
Identifier_FT : Math_Expr_FT {

	Variable_Type                variable_type;
	union {
		Entity_Id                parameter;
		state_var_id             state_var;
		state_var_id             series;
	};
	
	Identifier_FT() : Math_Expr_FT() { expr_type = Math_Expr_Type::identifier_chain; };
};

struct
Literal_FT : Math_Expr_FT {
	Parameter_Value value;
	
	Literal_FT() : Math_Expr_FT() { expr_type = Math_Expr_Type::literal; };
};

struct
Function_Call_FT : Math_Expr_FT {
	Function_Type fun_type;
	String_View   fun_name;
	
	Function_Call_FT() : Math_Expr_FT() { expr_type = Math_Expr_Type::function_call; };
};

struct
Operator_FT : Math_Expr_FT {
	Token_Type oper;
	
	Operator_FT() : Math_Expr_FT() {};
};



struct Mobius_Model;
struct Math_Expr_AST;
struct State_Variable;

Math_Expr_FT *
resolve_function_tree(Mobius_Model *model, s32 module_id, Math_Expr_AST *ast);

void
register_dependencies(Math_Expr_FT *expr, State_Variable *var);

Math_Expr_FT *
make_cast(Math_Expr_FT *expr, Value_Type cast_to);

Math_Expr_FT *
prune_tree(Math_Expr_FT *expr);

Math_Expr_FT *
quantity_codegen(Mobius_Model *model, Entity_Id id);

#endif // MOBIUS_FUNCTION_TREE_H