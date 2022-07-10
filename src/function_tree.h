

#ifndef MOBIUS_FUNCTION_TREE_H
#define MOBIUS_FUNCTION_TREE_H

#include "module_declaration.h"

//NOTE: this is really just a copy of the AST, but we want to put additional data on it.
//NOTE: we *could* put the extra data in the AST, but it is not that clean. We will also want to copy the tree any way when we resolve properties that are attached to multiple other entities.

struct Var_Id {
	s32 id;
	
	Var_Id &operator *() { return *this; }  //trick so that it can be an iterator to itself..
	Var_Id &operator++() { id++; return *this; }
};

inline bool
is_valid(Var_Id id) {
	return id.id >= 0;
}

constexpr Var_Id invalid_var = {-1};

inline bool
operator==(const Var_Id &a, const Var_Id &b) { return a.id == b.id; }
inline bool
operator!=(const Var_Id &a, const Var_Id &b) { return a.id != b.id; }
inline bool
operator<(const Var_Id &a, const Var_Id &b) { return a.id < b.id; }

enum class
Value_Type {
	unresolved = 0, none, real, integer, boolean,    // NOTE: enum would resolve to bool.
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
	parameter, series, state_var, local // Maybe also computed_parameter eventually.
};

struct Math_Block_FT;

struct
Math_Expr_FT {
	Math_Expr_Type               expr_type;
	Value_Type                   value_type;
	Source_Location              location;
	std::vector<Math_Expr_FT *>  exprs;
	Math_Block_FT               *scope;
	
	Math_Expr_FT(Math_Block_FT *scope, Math_Expr_Type expr_type) : value_type(Value_Type::unresolved), scope(scope), expr_type(expr_type), location() {};
	~Math_Expr_FT() { for(auto expr : exprs) delete expr; };
	
	virtual void add_expr(Math_Expr_FT *expr) {
		exprs.push_back(expr);
	}
};

struct
Math_Block_FT : Math_Expr_FT {
	
	String_View function_name;
	int n_locals;
	
	Math_Block_FT(Math_Block_FT *scope) : Math_Expr_FT(scope, Math_Expr_Type::block), n_locals(0), function_name("") { };
};

//NOTE: we don't want to use enum class here, because it doesn't autocast to int, making bitwise operations annoying :(
enum Identifier_Flags : u32 {
	ident_flags_none        = 0x0,
	ident_flags_last_result = 0x1,
	//TODO: conc, trust_unit, auto_convert, etc.
};

struct
Identifier_FT : Math_Expr_FT {

	Variable_Type                variable_type;
	Identifier_Flags             flags;
	union {
		Entity_Id                parameter;
		Var_Id                   state_var;
		Var_Id                   series;
		struct {
			s32           index;
			s32           scopes_up;
		}                        local_var;
	};
	
	Identifier_FT(Math_Block_FT *scope) : Math_Expr_FT(scope, Math_Expr_Type::identifier_chain), flags(ident_flags_none) { };
};

struct
Literal_FT : Math_Expr_FT {
	Parameter_Value value;
	
	Literal_FT(Math_Block_FT *scope) : Math_Expr_FT(scope, Math_Expr_Type::literal) { };
};

struct
Function_Call_FT : Math_Expr_FT {
	Function_Type fun_type;
	String_View   fun_name;
	
	Function_Call_FT(Math_Block_FT *scope) : Math_Expr_FT(scope, Math_Expr_Type::function_call) { };
};

struct
Operator_FT : Math_Expr_FT {
	Token_Type oper;
	
	Operator_FT(Math_Block_FT *scope, Math_Expr_Type expr_type) : Math_Expr_FT(scope, expr_type) { };
};

struct
Local_Var_FT : Math_Expr_FT {
	String_View   name;
	bool          is_used;
	
	Local_Var_FT(Math_Block_FT *scope) : Math_Expr_FT(scope, Math_Expr_Type::local_var), is_used(false) { }
};



struct Mobius_Model;
struct Math_Expr_AST;
struct Dependency_Set;

Math_Expr_FT *
resolve_function_tree(Mobius_Model *model, s32 module_id, Math_Expr_AST *ast, Math_Block_FT *scope);

void
register_dependencies(Math_Expr_FT *expr, Dependency_Set *depends);

Math_Expr_FT *
make_cast(Math_Expr_FT *expr, Value_Type cast_to);

Math_Expr_FT *
make_literal(Math_Block_FT *scope, s64 val_int);

Math_Expr_FT *
make_state_var_identifier(Math_Block_FT *scope, Var_Id state_var);

Math_Expr_FT *
make_intrinsic_function_call(Math_Block_FT *scope, Value_Type value_type, String_View name, Math_Expr_FT *arg1, Math_Expr_FT *arg2);

Math_Expr_FT *
make_binop(Math_Block_FT *scope, char oper, Math_Expr_FT *lhs, Math_Expr_FT *rhs, Value_Type value_type);


Math_Expr_FT *
prune_tree(Math_Expr_FT *expr);

//Math_Expr_FT *
//quantity_codegen(Mobius_Model *model, Entity_Id id);

Math_Expr_FT *
restrict_flux(Math_Expr_FT *expr, Var_Id source);

Math_Expr_FT *
copy(Math_Expr_FT *source);


#endif // MOBIUS_FUNCTION_TREE_H