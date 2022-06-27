
#ifndef MOBIUS_AST_H
#define MOBIUS_AST_H

#include "lexer.h"
#include <vector>


struct Expr_AST {
	
};

enum class Decl_Type {
	unrecognized,
	#define ENUM_VALUE(name, body_type) name,
	#include "decl_types.incl"
	#undef ENUM_VALUE
};

inline const char *
name(Decl_Type type) {
	#define ENUM_VALUE(name, body_type) if(type == Decl_Type::name) return #name;
	#include "decl_types.incl"
	#undef ENUM_VALUE
	return "unrecognized";
}

enum class Body_Type {
	none,
	decl,
	function,
};

struct Body_AST : Expr_AST {
	std::vector<Token> modifiers;
	Body_Type type;
	
	Body_AST(Body_Type type) : type(type) {};
};

struct Decl_AST;

struct Argument_AST : Expr_AST {
	std::vector<Token> sub_chain;    //TODO: better name?
	char               chain_sep;
	Decl_AST          *decl;
};

struct Decl_AST : Expr_AST {
	Token                        handle_name;
	Token                        type_name; // Hmm, is a bit superfluous, but it is mostly to store the source location.
	Decl_Type                    type;
	
	std::vector<Token>           decl_chain;
	std::vector<Argument_AST *>  args;
	std::vector<Body_AST *>      bodies;
};

struct Decl_Body_AST : Body_AST {
	Token                   doc_string;
	std::vector<Decl_AST *> child_decls;
	
	Decl_Body_AST() : Body_AST(Body_Type::decl) {};
};

struct Math_Block_AST;

struct Function_Body_AST : Body_AST {
	Math_Block_AST *block;
	
	Function_Body_AST() : Body_AST(Body_Type::function) {};
};


enum class Math_Expr_Type {
	block,
	identifier_chain,
	literal,
	function_call,
	unary_operator,
	binary_operator,
	if_chain,
	assignment,
};


struct Math_Expr_AST : Expr_AST {
	Math_Expr_Type               type;
	
	Math_Expr_AST(Math_Expr_Type type) : type(type) {};
};

struct Math_Block_AST : Math_Expr_AST {
	std::vector<Math_Expr_AST *> exprs;
	
	Math_Block_AST() : Math_Expr_AST(Math_Expr_Type::block) {};
};

struct Identifier_Chain_AST : Math_Expr_AST {
	std::vector<Token>           chain;
	
	Identifier_Chain_AST() : Math_Expr_AST(Math_Expr_Type::identifier_chain) {};
};

struct Literal_AST : Math_Expr_AST {
	Token                        value;
	
	Literal_AST() : Math_Expr_AST(Math_Expr_Type::literal) {};
};

struct Function_Call_AST : Math_Expr_AST {
	Token                        name;
	std::vector<Math_Expr_AST *> args;
	
	Function_Call_AST() : Math_Expr_AST(Math_Expr_Type::function_call) {};
};

struct Unary_Operator_AST : Math_Expr_AST {
	String_View                   oper;
	Math_Expr_AST                *arg;
	
	Unary_Operator_AST() : Math_Expr_AST(Math_Expr_Type::unary_operator) {};
};

struct Binary_Operator_AST : Math_Expr_AST {
	String_View                   oper;
	Math_Expr_AST                *lhs;
	Math_Expr_AST                *rhs;
	
	Binary_Operator_AST() : Math_Expr_AST(Math_Expr_Type::binary_operator) {};
};

struct If_Expr_AST : Math_Expr_AST {
	std::vector<std::pair<Math_Expr_AST *, Math_Expr_AST *>>  ifs;
	Math_Expr_AST                                            *otherwise;
	
	If_Expr_AST() : Math_Expr_AST(Math_Expr_Type::if_chain) {};
};


Decl_AST *
parse_decl(Token_Stream *stream, Linear_Allocator *allocator);

Math_Expr_AST *
parse_potential_if_expr(Token_Stream *stream, Linear_Allocator *allocator);

Math_Expr_AST *
parse_math_expr(Token_Stream *stream, Linear_Allocator *allocator);

Math_Expr_AST *
parse_primary_expr(Token_Stream *stream, Linear_Allocator *allocator);

Math_Block_AST *
parse_math_block(Token_Stream *stream, Linear_Allocator *allocator);


#endif // MOBIUS_AST_H