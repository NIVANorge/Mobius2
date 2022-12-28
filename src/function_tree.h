

#ifndef MOBIUS_FUNCTION_TREE_H
#define MOBIUS_FUNCTION_TREE_H

#include "common_types.h"
#include "model_declaration.h"

//NOTE: this is really just a copy of the AST, but we want to put additional data on it.
//NOTE: we *could* put the extra data in the AST, but it is not that clean. We will also want to copy the tree any way when we resolve properties that are attached to multiple other entities.



struct
Math_Expr_FT {
	Math_Expr_Type               expr_type;
	Value_Type                   value_type;
	Source_Location              source_loc;
	std::vector<Math_Expr_FT *>  exprs;
	
	Math_Expr_FT(Math_Expr_Type expr_type) : value_type(Value_Type::unresolved), expr_type(expr_type), source_loc() {};
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
	// although TODO, just scope it inside the Identifier_FT
enum Identifier_Flags : u32 {
	ident_flags_none        = 0x0,
	ident_flags_last_result = 0x1,
	ident_flags_in_flux     = 0x2,
	ident_flags_aggregate   = 0x4,
	ident_flags_conc        = 0x8,
	//TODO: trust_unit, auto_convert, etc.
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
	std::string   fun_name;
	
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
	std::string   name;
	bool          is_used;
	
	Local_Var_FT() : Math_Expr_FT(Math_Expr_Type::local_var), is_used(false) { }
};

struct
State_Var_Dependency {
	enum Type : u32 {
		none =         0x0,
		earlier_step = 0x1,
	}                 type;
	Var_Id            var_id;
};

inline bool operator<(const State_Var_Dependency &a, const State_Var_Dependency &b) {
	if(a.var_id == b.var_id) return (u32)a.type < (u32)b.type;
	return a.var_id.id < b.var_id.id;
}

struct
Dependency_Set {
	std::set<Entity_Id>             on_parameter;
	std::set<Var_Id>                on_series;
	std::set<State_Var_Dependency>  on_state_var;
};

struct Model_Application;
struct Math_Expr_AST;
struct Function_Scope;

struct
Function_Resolve_Data {
	Model_Application *app;
	Decl_Scope        *scope;
	Var_Location       in_loc;
	std::vector<Entity_Id> *baked_parameters;
};

Math_Expr_FT *
resolve_function_tree(Math_Expr_AST *ast, Function_Resolve_Data *data, Function_Scope *scope = nullptr);

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
make_intrinsic_function_call(Value_Type value_type, const std::string &name, Math_Expr_FT *arg);

Math_Expr_FT *
make_intrinsic_function_call(Value_Type value_type, const std::string &name, Math_Expr_FT *arg1, Math_Expr_FT *arg2);

Math_Expr_FT *
make_binop(Token_Type oper, Math_Expr_FT *lhs, Math_Expr_FT *rhs);

Math_Expr_FT *
make_simple_if(Math_Expr_FT *first, Math_Expr_FT *condition, Math_Expr_FT *otherwise);

inline Math_Expr_FT *
make_binop(char oper, Math_Expr_FT *lhs, Math_Expr_FT *rhs) {
	return make_binop((Token_Type)oper, lhs, rhs);
}

Math_Block_FT *
make_for_loop();

Math_Expr_FT *
make_safe_divide(Math_Expr_FT *lhs, Math_Expr_FT *rhs);


Math_Expr_FT *
prune_tree(Math_Expr_FT *expr, Function_Scope *scope = nullptr);

Math_Expr_FT *
copy(Math_Expr_FT *source);

void
print_tree(Math_Expr_FT *expr, int ntabs=0);


#endif // MOBIUS_FUNCTION_TREE_H