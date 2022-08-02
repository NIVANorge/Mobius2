
#ifndef MOBIUS_AST_H
#define MOBIUS_AST_H

#include "lexer.h"
#include "common_types.h"
#include <vector>


struct
Expr_AST {
	virtual ~Expr_AST() {};
};

enum class
Body_Type {
	none,
	decl,
	function,
};

struct
Body_AST : Expr_AST {
	std::vector<Token> modifiers;
	Source_Location    opens_at;
	Body_Type type;
	
	Body_AST(Body_Type type) : type(type) {};
	virtual ~Body_AST() {};
};

struct Decl_AST;

struct
Argument_AST : Expr_AST {
	std::vector<Token> sub_chain;    //TODO: better name?
	char               chain_sep;
	Decl_AST          *decl;
	
	~Argument_AST();
};

struct
Decl_AST : Expr_AST {
	Token                        handle_name;
	Source_Location              location;
	Decl_Type                    type;
	
	std::vector<Token>           decl_chain;
	std::vector<Argument_AST *>  args;
	std::vector<Body_AST *>      bodies;
	
	~Decl_AST() { for(auto arg : args) delete arg; for(auto body : bodies) delete body; }
};

struct
Decl_Body_AST : Body_AST {
	Token                   doc_string;
	std::vector<Decl_AST *> child_decls;
	
	Decl_Body_AST() : Body_AST(Body_Type::decl), doc_string() {};
	~Decl_Body_AST() { for(auto decl : child_decls) delete decl; }
};

struct Math_Block_AST;

struct
Function_Body_AST : Body_AST {
	Math_Block_AST *block;
	
	Function_Body_AST() : Body_AST(Body_Type::function) {};
	~Function_Body_AST();
};


enum class
Math_Expr_Type {
	#define ENUM_VALUE(name) name,
	#include "math_expr_types.incl"
	#undef ENUM_VALUE
};

inline const char *
name(Math_Expr_Type type) {
	#define ENUM_VALUE(name) if(type == Math_Expr_Type::name) return #name;
	#include "math_expr_types.incl"
	#undef ENUM_VALUE
	return "unrecognized";
}


struct
Math_Expr_AST : Expr_AST {
	Math_Expr_Type               type;
	std::vector<Math_Expr_AST *> exprs;
	Source_Location              location;
	
	Math_Expr_AST(Math_Expr_Type type) : type(type) {};
	~Math_Expr_AST() { for(auto expr : exprs) delete expr; }
};

struct
Math_Block_AST : Math_Expr_AST {
	Source_Location              opens_at; //TODO: remove, use location instead
	
	Math_Block_AST() : Math_Expr_AST(Math_Expr_Type::block) {};
};

struct
Identifier_Chain_AST : Math_Expr_AST {
	std::vector<Token>           chain;
	
	Identifier_Chain_AST() : Math_Expr_AST(Math_Expr_Type::identifier_chain) {};
};

struct
Literal_AST : Math_Expr_AST {
	Token                        value;
	
	Literal_AST() : Math_Expr_AST(Math_Expr_Type::literal) {};
};

struct
Function_Call_AST : Math_Expr_AST {
	Token                        name;
	
	Function_Call_AST() : Math_Expr_AST(Math_Expr_Type::function_call) {};
};

struct
Unary_Operator_AST : Math_Expr_AST {
	Token_Type                   oper;
	
	Unary_Operator_AST() : Math_Expr_AST(Math_Expr_Type::unary_operator) {};
};

struct
Binary_Operator_AST : Math_Expr_AST {
	Token_Type                   oper;
	
	Binary_Operator_AST() : Math_Expr_AST(Math_Expr_Type::binary_operator) {};
};

struct
If_Expr_AST : Math_Expr_AST {
	If_Expr_AST() : Math_Expr_AST(Math_Expr_Type::if_chain) {};
};

struct
Local_Var_AST : Math_Expr_AST {
	Token                        name;
	
	Local_Var_AST() : Math_Expr_AST(Math_Expr_Type::local_var) {};
};


Decl_AST *
parse_decl(Token_Stream *stream);

Math_Expr_AST *
parse_potential_if_expr(Token_Stream *stream);

Math_Expr_AST *
parse_math_expr(Token_Stream *stream);

Math_Expr_AST *
parse_primary_expr(Token_Stream *stream);

Math_Block_AST *
parse_math_block(Token_Stream *stream, Source_Location opens_at);


inline Token *
single_arg(Decl_AST *decl, int which) {
	return &decl->args[which]->sub_chain[0];
}

//TODO: Don't have all the functions below inlined.
//TODO: Make a general-purpose tagged union?
struct Arg_Pattern {
	enum class Type { value, decl, unit_literal };
	Type pattern_type;
	bool is_vararg;
	
	union {
		Token_Type token_type;
		Decl_Type  decl_type;
	};
	
	Arg_Pattern(Token_Type token_type, bool is_vararg = false) : token_type(token_type), pattern_type(Type::value), is_vararg(is_vararg) {}
	Arg_Pattern(Decl_Type decl_type, bool is_vararg = false)   : decl_type(decl_type), pattern_type(Type::decl), is_vararg(is_vararg) {}
	Arg_Pattern() : pattern_type(Type::unit_literal) {}
	
	bool matches(Argument_AST *arg) const;
	
	void print_to_error() const {
		switch(pattern_type) {
			case Type::unit_literal : { error_print("(unit literal)"); } break;
			case Type::decl :         { error_print(name(decl_type));  } break;
			case Type::value :        { error_print(name(token_type)); } break;
		}
	}
};

int
match_declaration(Decl_AST *decl, const std::initializer_list<std::initializer_list<Arg_Pattern>> &patterns, int allow_chain = 0, bool allow_handle = true, int allow_body_count = 1, bool allow_body_modifiers = false);



#endif // MOBIUS_AST_H