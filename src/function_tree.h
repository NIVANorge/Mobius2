

#ifndef MOBIUS_FUNCTION_TREE_H
#define MOBIUS_FUNCTION_TREE_H

#include <map>

#include "common_types.h"
#include "model_declaration.h"

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


struct
Identifier_Data {
	Variable_Type                variable_type;
	enum Flags : u32 {
		none        = 0x0,
		last_result = 0x1,
		in_flux     = 0x2,
		aggregate   = 0x4,
		conc        = 0x8,
		below_above = 0x10,
		top_bottom  = 0x20,
	}                            flags;
	union {
		Entity_Id                par_id;
		Var_Id                   var_id;
	};
	Entity_Id connection;   // If it is 'below', 'above', 'top', or 'bottom', what connection is it along?
	bool      is_above;     // If it is below or above, is it actually above (or top vs bottom)?
	
	Identifier_Data() : flags(Flags::none), connection(invalid_entity_id), is_above(false) { };
};

inline bool operator<(const Identifier_Data &a, const Identifier_Data &b) {
	// NOTE: The current use case for this is such that they have the same variable type
	if(a.variable_type == Variable_Type::parameter) {
		if(a.par_id == b.par_id) return (u32)a.flags < (u32)b.flags;
		return a.par_id.id < b.par_id.id;
	}
	if(a.var_id == b.var_id) return a.flags < b.flags;
	return a.var_id.id < b.var_id.id;
}

struct
Local_Var_Id {
	s32 scope_id;
	s32 id;
};

struct
Identifier_FT : Math_Expr_FT, Identifier_Data {
	Local_Var_Id local_var; // NOTE: Only used if the Identifier_Data::variable_type is local_var.
	
	Identifier_FT() : Math_Expr_FT(Math_Expr_Type::identifier), Identifier_Data() { };
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
	Operator_FT() : Math_Expr_FT(Math_Expr_Type::unary_operator) { }; //NOTE: we need this one in copy() (where the data is overwritten) but it should otherwise not be used!
};

struct
Local_Var_FT : Math_Expr_FT {
	std::string   name;
	s32           id;
	bool          is_used;
	
	Local_Var_FT() : Math_Expr_FT(Math_Expr_Type::local_var), is_used(false) { }
};

struct
Special_Computation_FT : Math_Expr_FT {
	std::string                  function_name;
	
	Var_Id                       target;
	std::vector<Identifier_Data> arguments;
	
	Special_Computation_FT() : Math_Expr_FT(Math_Expr_Type::special_computation) { }
};

struct
Dependency_Set {
	std::set<Identifier_Data>  on_parameter;
	std::set<Identifier_Data>  on_series;
	std::set<Identifier_Data>  on_state_var;
};


typedef std::unique_ptr<Math_Expr_FT> owns_code;

struct Model_Application;
struct Math_Expr_AST;

struct
Function_Resolve_Data {
	Model_Application *app;
	Decl_Scope        *scope;
	Var_Location       in_loc;
	std::vector<Entity_Id> *baked_parameters = nullptr;
	Standardized_Unit  expected_unit;
	Entity_Id          connection = invalid_entity_id;
	
	// The simplified option is if we are resolving a simple expression of provided symbols, not for main code, but e.g. for parameter exprs in an optimizer run.
	bool                      simplified = false;
	std::vector<std::string>  simplified_syms;
};

struct
Function_Resolve_Result {
	Math_Expr_FT *fun;
	Standardized_Unit unit;
};

// TODO: When we remove pruning from the resolve_function_tree, we can have a simpler version of the Function_Scope for the pruning.
struct
Function_Scope {
	Function_Scope *parent;
	Math_Block_FT *block;
	std::map<int, Standardized_Unit> local_var_units;
	std::string function_name;
	
	Function_Scope() : parent(nullptr), block(nullptr), function_name("") {}
};

Function_Resolve_Result
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

Math_Expr_FT *
make_unary(char oper, Math_Expr_FT *arg);

Math_Block_FT *
make_for_loop();

Math_Expr_FT *
make_safe_divide(Math_Expr_FT *lhs, Math_Expr_FT *rhs);

Math_Expr_FT *
prune_tree(Math_Expr_FT *expr, Function_Scope *scope = nullptr, bool prune_unused_locals = true);

Math_Expr_FT *
copy(Math_Expr_FT *source);

//owns_code
//copy(owns_code &source);

void
print_tree(Model_Application *app, Math_Expr_FT *expr, std::ostream &os);

template<typename T> struct
Scope_Local_Vars {
	s32 scope_id;
	Scope_Local_Vars<T> *scope_up;
	std::map<s32, T> values;
};

template<typename T> T
find_local_var(Scope_Local_Vars<T> *scope, Local_Var_Id id) {
	if(!scope)
		fatal_error(Mobius_Error::internal, "Misordering of scopes when looking up a local variable.");
	while(scope->scope_id != id.scope_id) {
		scope = scope->scope_up;
		if(!scope)
			fatal_error(Mobius_Error::internal, "Misordering of scopes when looking up a local variable.");
	}
	auto find = scope->values.find(id.id);
	if(find == scope->values.end())
		fatal_error(Mobius_Error::internal, "A local variable is missing from a scope.");
	
	return find->second;
}


#endif // MOBIUS_FUNCTION_TREE_H