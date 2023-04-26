
#ifndef MOBIUS_AST_H
#define MOBIUS_AST_H

#include "lexer.h"
#include "common_types.h"
#include "units.h"
#include <vector>

// TODO: What do we need the Expr_AST base class for? Remove it?

struct
Expr_AST {
	virtual ~Expr_AST() {};
};

enum class
Body_Type {
	none,
	decl,
	function,
	regex,
};

struct
Body_AST : Expr_AST {
	std::vector<Token> notes;
	Body_Type type;
	Source_Location opens_at;
	
	Body_AST(Body_Type type) : type(type) {};
	virtual ~Body_AST() {};
};

struct Decl_AST;

struct
Argument_AST : Expr_AST {
	std::vector<Token> chain;
	std::vector<Token> bracketed_chain;
	Decl_AST          *decl = nullptr;
	
	~Argument_AST();
};

struct
Decl_AST : Expr_AST {
	Token                        handle_name;
	Source_Location              source_loc;
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
	Source_Location              source_loc;
	
	Math_Expr_AST(Math_Expr_Type type) : type(type) {};
	~Math_Expr_AST() { for(auto expr : exprs) delete expr; }
};

// TODO: A lot of these structs are superfluous... Could have one called   Single_Token_AST, Chain_AST and Operator_AST, but just with different Math_Expr_Types
//    Can also unify struct for Function_Body_AST and Regex_Body_AST.
//    Also a bit confusing that they are called Math_Expr_whatever when we reuse them for regexes. Just rename to Expr_whatever ? However Expr_AST is already taken...

struct
Math_Block_AST : Math_Expr_AST {
	Math_Block_AST() : Math_Expr_AST(Math_Expr_Type::block) {};
};

struct
Identifier_Chain_AST : Math_Expr_AST {
	std::vector<Token>           chain;
	std::vector<Token>           bracketed_chain;
	
	Identifier_Chain_AST() : Math_Expr_AST(Math_Expr_Type::identifier) {};
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

struct
Unit_Convert_AST : Math_Expr_AST {
	Decl_AST *unit;             // TODO: free in destructor.
	bool auto_convert, force;
	
	Unit_Convert_AST() : Math_Expr_AST(Math_Expr_Type::unit_convert) {};
};

struct
Regex_Body_AST : Body_AST {
	Math_Expr_AST               *expr;
	
	Regex_Body_AST() : Body_AST(Body_Type::regex) {};
	~Regex_Body_AST();
};

struct
Regex_Or_Chain_AST : Math_Expr_AST {
	Regex_Or_Chain_AST() : Math_Expr_AST(Math_Expr_Type::regex_or_chain) {};
};

struct
Regex_Identifier_AST : Math_Expr_AST {
	Token                        ident;
	
	Regex_Identifier_AST() : Math_Expr_AST(Math_Expr_Type::regex_identifier) {};
};




Decl_AST *
parse_decl_header(Token_Stream *stream, Body_Type *body_type_out = nullptr);

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

Math_Expr_AST *
parse_regex_list(Token_Stream *stream, Source_Location opens_at, bool outer);


inline Token *
single_arg(Decl_AST *decl, int which) {
	if(decl->args[which]->chain.size() != 1) {
		decl->args[which]->chain[1].source_loc.print_error_header(Mobius_Error::internal);
		fatal_error(Mobius_Error::internal, "Expected a single value or identifier, not a chain.");
	}
	if(!decl->args[which]->bracketed_chain.empty()) {
		decl->args[which]->bracketed_chain[0].source_loc.print_error_header();
		fatal_error("This argument should not have a bracket.");
	}
	return &decl->args[which]->chain[0];
}

//TODO: Make a general-purpose tagged union?
struct Arg_Pattern {
	enum class Type { any, value, decl };
	Type pattern_type;
	bool is_vararg;
	
	union {
		Token_Type token_type;
		Decl_Type  decl_type;
	};
	
	Arg_Pattern(bool is_vararg = false) : pattern_type(Type::any), is_vararg(is_vararg) {}
	Arg_Pattern(Token_Type token_type, bool is_vararg = false) : token_type(token_type), pattern_type(Type::value), is_vararg(is_vararg) {}
	Arg_Pattern(Decl_Type decl_type, bool is_vararg = false)   : decl_type(decl_type), pattern_type(Type::decl), is_vararg(is_vararg) {}
	
	bool matches(Argument_AST *arg) const;
	
	void print_to_error() const {
		switch(pattern_type) {
			case Type::decl :         { error_print(name(decl_type));  } break;
			case Type::value :        { error_print(name(token_type)); } break;
		}
		if(is_vararg) error_print("...");
	}
};

int
match_declaration(Decl_AST *decl, const std::initializer_list<std::initializer_list<Arg_Pattern>> &patterns, int allow_chain = 0, bool allow_handle = true, int allow_body_count = 1, bool allow_body_notes = false);

int
operator_precedence(Token_Type t);

#endif // MOBIUS_AST_H