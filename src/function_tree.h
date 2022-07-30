

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

inline String_View
name(Value_Type type) {
	if(type == Value_Type::unresolved) return "unresolved";
	if(type == Value_Type::none)       return "none";
	if(type == Value_Type::real)       return "real";
	if(type == Value_Type::integer)    return "integer";
	if(type == Value_Type::boolean)    return "boolean";
	return "unresolved";
}

inline Value_Type
get_value_type(Decl_Type decl_type) {
	if(decl_type == Decl_Type::par_real) return Value_Type::real;
	if(decl_type == Decl_Type::par_int)  return Value_Type::integer;
	if(decl_type == Decl_Type::par_bool) return Value_Type::boolean;
	
	return Value_Type::unresolved;
}

inline Value_Type
get_value_type(Token_Type type) {
	if(type == Token_Type::real) return Value_Type::real;
	else if(type == Token_Type::integer) return Value_Type::integer;
	else if(type == Token_Type::boolean) return Value_Type::boolean;
	
	return Value_Type::unresolved;
}

inline Token_Type
get_token_type(Decl_Type type) {
	if(type == Decl_Type::par_real) return Token_Type::real;
	if(type == Decl_Type::par_int) return Token_Type::integer;
	if(type == Decl_Type::par_bool) return Token_Type::boolean;
	if(type == Decl_Type::par_enum) return Token_Type::identifier;
	
	return Token_Type::unknown;
}

enum class
Variable_Type {
	parameter, series, state_var, local, 
	// "special" state variables
	#define TIME_VALUE(name, bits, pos) time_##name,
	#include "time_values.incl"
	#undef TIME_VALUE
	// Maybe also computed_parameter eventually.
};

struct
Math_Expr_FT {
	Math_Expr_Type               expr_type;
	Value_Type                   value_type;
	Source_Location              location;
	std::vector<Math_Expr_FT *>  exprs;
	
	Math_Expr_FT(Math_Expr_Type expr_type) : value_type(Value_Type::unresolved), expr_type(expr_type), location() {};
	Math_Expr_FT() {}; //NOTE: we need this one in copy() (where the data is overwritten) but it should otherwise not be used!
	~Math_Expr_FT() { for(auto expr : exprs) delete expr; };
};

struct
Math_Block_FT : Math_Expr_FT {
	
	s32 unique_block_id;
	int n_locals;
	bool is_for_loop;
	
	void set_id();
	Math_Block_FT() : Math_Expr_FT(Math_Expr_Type::block), n_locals(0), is_for_loop(false) { set_id(); };
};

//NOTE: we don't want to use enum class here, because it doesn't autocast to int, making bitwise operations annoying :(
enum Identifier_Flags : u32 {
	ident_flags_none        = 0x0,
	ident_flags_last_result = 0x1,
	ident_flags_in_flux     = 0x2
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
			s32           scope_id;
		}                        local_var;
	};
	
	Identifier_FT() : Math_Expr_FT(Math_Expr_Type::identifier_chain), flags(ident_flags_none) { };
};

struct
Literal_FT : Math_Expr_FT {
	Parameter_Value value;
	
	Literal_FT() : Math_Expr_FT(Math_Expr_Type::literal) { };
};

struct
Function_Call_FT : Math_Expr_FT {
	Function_Type fun_type;
	String_View   fun_name;
	
	Function_Call_FT() : Math_Expr_FT(Math_Expr_Type::function_call) { };
};

struct
Operator_FT : Math_Expr_FT {
	Token_Type oper;
	
	Operator_FT(Math_Expr_Type expr_type) : Math_Expr_FT(expr_type) { };
	Operator_FT() : Math_Expr_FT(Math_Expr_Type::unary_operator) { }; // NOTE: we need this one in copy(), where the type is then overwritten, but should otherwise not be used.
};

struct
Local_Var_FT : Math_Expr_FT {
	String_View   name;
	bool          is_used;
	
	Local_Var_FT() : Math_Expr_FT(Math_Expr_Type::local_var), is_used(false) { }
};



struct Mobius_Model;
struct Math_Expr_AST;
struct Dependency_Set;

struct Scope_Data;

Math_Expr_FT *
resolve_function_tree(Mobius_Model *model, s32 module_id, Math_Expr_AST *ast, Scope_Data *scope = nullptr);

void
register_dependencies(Math_Expr_FT *expr, Dependency_Set *depends);

Math_Expr_FT *
make_cast(Math_Expr_FT *expr, Value_Type cast_to);

Math_Expr_FT *
make_literal(s64 val_int);

Math_Expr_FT *
make_literal(double val_double);

Math_Expr_FT *
make_state_var_identifier(Var_Id state_var);

Math_Expr_FT *
add_local_var(Math_Block_FT *scope, Math_Expr_FT *val);

Math_Expr_FT *
make_local_var_reference(s32 index, s32 scope_id, Value_Type value_type);

Math_Expr_FT *
make_intrinsic_function_call(Value_Type value_type, String_View name, Math_Expr_FT *arg);

Math_Expr_FT *
make_intrinsic_function_call(Value_Type value_type, String_View name, Math_Expr_FT *arg1, Math_Expr_FT *arg2);

Math_Expr_FT *
make_binop(char oper, Math_Expr_FT *lhs, Math_Expr_FT *rhs);

Math_Block_FT *
make_for_loop();

Math_Expr_FT *
prune_tree(Math_Expr_FT *expr, Scope_Data *scope = nullptr);

Math_Expr_FT *
copy(Math_Expr_FT *source);

//void
//print_tree(Math_Expr_FT *expr, int ntabs=0);


#endif // MOBIUS_FUNCTION_TREE_H