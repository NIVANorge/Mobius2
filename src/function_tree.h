

#ifndef MOBIUS_FUNCTION_TREE_H
#define MOBIUS_FUNCTION_TREE_H

#include "module_declaration.h"

//NOTE: this is really just a copy of the AST, but we want to put additional data on it.
//NOTE: we *could* put it in the AST, but it is not that clean.

enum class
Value_Type {
	unresolved = 0, real, integer, boolean, datetime,    // NOTE: enum would resolve to bool.
};

enum class
Variable_Type {
	parameter, input_series, state_var,  // Maybe also computed_parameter eventually.
};


struct
Math_Expr_FT {
	Math_Expr_AST               *ast;
	Value_Type                   value_type;
	entity_id                    unit;
	
	Math_Expr_AST(Math_Expr_AST *ast) : ast(ast), value_type(Value_Type::unresolved) {};
};

struct
Math_Block_FT : Math_Expr_FT {
	std::vector<Math_Expr_FT *> exprs;
	
	//TODO: scope info
	
	Math_Block_FT(Math_Block_AST *ast) : Math_Expr_FT(ast) {};
};

struct
Identifier_Chain_FT : Math_Expr_FT {

	Variable_Type                variable_type;
	union {
		entity_id                parameter;
		state_var_id             state_var;
		state_var_id             series;
	};
	
	Identifier_Chain_FT(Math_Block_AST *ast) : Math_Expr_FT(ast) {};
};

struct
Literal_FT : Math_Expr_FT {
	Token                        value;
	
	Literal_FT(Math_Block_AST *ast) : Math_Expr_FT(ast) {};
};

struct
Function_Call_FT : Math_Expr_FT {
	std::vector<Math_Expr_FT *>  args;
	
	
	Function_Call_FT(Math_Block_AST *ast) : Math_Expr_FT(ast) {};
};

struct
Unary_Operator_FT : Math_Expr_FT {
	Math_Expr_FT                 *arg;
	
	Unary_Operator_FT() : Math_Expr_AST(Math_Expr_Type::unary_operator) {};
};

struct
Binary_Operator_AST : Math_Expr_AST {
	Math_Expr_FT                *lhs;
	Math_Expr_FT                *rhs;
	
	Binary_Operator_AST() : Math_Expr_AST(Math_Expr_Type::binary_operator) {};
};

struct
If_Expr_AST : Math_Expr_AST {
	std::vector<std::pair<Math_Expr_FT *, Math_Expr_FT *>>    ifs;
	Math_Expr_FT                                             *otherwise;
	
	If_Expr_AST() : Math_Expr_AST(Math_Expr_Type::if_chain) {};
};


#endif // MOBIUS_FUNCTION_TREE_H