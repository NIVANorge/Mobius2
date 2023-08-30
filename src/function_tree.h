

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
	
	bool visited = false; // For debug purposes in order to verify that it is indeed a tree.
	
	Math_Expr_FT(Math_Expr_Type expr_type) : value_type(Value_Type::unresolved), expr_type(expr_type), source_loc() {};
	Math_Expr_FT() {}; //NOTE: we need this one in copy() (where the data is overwritten) but it should otherwise not be used!
	~Math_Expr_FT() { for(auto expr : exprs) delete expr; };
};

struct
Math_Block_FT : Math_Expr_FT {
	
	s32 unique_block_id;
	int n_locals;
	bool is_for_loop;
	std::string iter_tag;
	
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
		result      = 0x16,
	}                            flags;
	Var_Loc_Restriction          restriction;
	Entity_Id                    other_connection;
	union {
		Entity_Id                par_id;
		Var_Id                   var_id;
	};
	
	void set_flag(Flags flag)   { flags = (Flags)(flags | flag); }
	void remove_flag(Flags flag) { flags = (Flags)(flags & ~flag); }
	bool has_flag(Flags flag)   { return flags & flag; }
	
	Identifier_Data() : flags(Flags::none), other_connection(invalid_entity_id) { };
};

inline bool operator<(const Identifier_Data &a, const Identifier_Data &b) {
	// NOTE: The current use case for this is such that they have the same variable type
	// NOTE: We should not have to care about other_connection here, since it is just a
	// placeholder used until the identifier is fully resolved.
	if(a.variable_type != b.variable_type)
		return a.variable_type < b.variable_type;
	if(a.variable_type == Variable_Type::parameter) {
		if(a.par_id == b.par_id) return a.flags < b.flags;
		return a.par_id.id < b.par_id.id;
	}
	if(a.var_id == b.var_id) {
		if(a.flags == b.flags)
			return a.restriction < b.restriction;
		return a.flags < b.flags;
	}
	return a.var_id.id < b.var_id.id;
}

struct
Local_Var_Id {
	s32 scope_id;
	s32 id;
};

inline bool operator==(const Local_Var_Id &a, const Local_Var_Id &b) {
	return a.scope_id == b.scope_id && a.id == b.id;
}

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
	// This is the declaration of a local var
	std::string   name;
	s32           id;
	bool          is_used          = false;
	bool          is_reassignable  = false;
	
	Local_Var_FT() : Math_Expr_FT(Math_Expr_Type::local_var) { }
};

struct
External_Computation_FT : Math_Expr_FT {
	std::string                  function_name;
	
	std::vector<Identifier_Data> arguments;
	Entity_Id                    connection_component = invalid_entity_id;
	Entity_Id                    connection           = invalid_entity_id;
	
	External_Computation_FT() : Math_Expr_FT(Math_Expr_Type::external_computation) { }
};

struct
Assignment_FT : Math_Expr_FT {
	Var_Id        var_id;   // Used if the type is state_var_assignment or derivative_assignment
	Local_Var_Id  local_var; // Used if the type is local_var_assignment
	
	Assignment_FT() : Math_Expr_FT(Math_Expr_Type::local_var_assignment) {}  // NOTE this one is only for use in copy(), should not really be used otherwise.
	Assignment_FT(Math_Expr_Type type, Var_Id var_id) : Math_Expr_FT(type), var_id(var_id) {}
	Assignment_FT(Local_Var_Id local_var) : Math_Expr_FT(Math_Expr_Type::local_var_assignment), local_var(local_var) {}
};

struct
Iterate_FT : Math_Expr_FT {
	s32 scope_id = -1;
	Iterate_FT() : Math_Expr_FT(Math_Expr_Type::iterate) {}
};


typedef std::unique_ptr<Math_Expr_FT> owns_code;

struct Model_Application;
struct Math_Expr_AST;

struct
Function_Resolve_Data {
	Model_Application           *app;
	Decl_Scope                  *scope;
	Specific_Var_Location        in_loc;
	std::vector<Entity_Id>      *baked_parameters = nullptr;
	Standardized_Unit            expected_unit;
	Entity_Id                    connection = invalid_entity_id;
	
	bool                         restrictive_lookups = false;
	bool                         allow_in_flux       = true;
	bool                         allow_no_override   = false;
	bool                         allow_result        = false;
	
	// For unit_conversion and aggregation_weight :
	//Entity_Id                    source_compartment = invalid_entity_id;
	//Entity_Id                    target_compartment = invalid_entity_id;
	
	// The simplified option is if we are resolving a simple expression of provided symbols, not for main code, but e.g. for parameter exprs in an optimizer run.
	bool                         simplified = false;
	std::vector<std::string>     simplified_syms;
};

struct
Function_Resolve_Result {
	Math_Expr_FT *fun;
	Standardized_Unit unit;
};

// TODO: We could use a simpler version of the Function_Scope for the pruning.
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
register_dependencies(Math_Expr_FT *expr, std::set<Identifier_Data> *depends);

Math_Expr_FT *
make_cast(Math_Expr_FT *expr, Value_Type cast_to);

Math_Expr_FT *
make_literal(s64 val_int);

Math_Expr_FT *
make_literal(double val_double);

Math_Expr_FT *
make_literal(bool val_bool);

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

Math_Expr_FT *
make_no_op();

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
prune_tree(Math_Expr_FT *expr);

Math_Expr_FT *
copy(Math_Expr_FT *source);


Rational<s64>
is_constant_rational(Math_Expr_FT *expr, Function_Scope *scope, bool *found);

void
print_tree(Model_Application *app, Math_Expr_FT *expr, std::ostream &os);

template<typename T, typename R=int> struct
Scope_Local_Vars {
	s32                     scope_id;
	Scope_Local_Vars<T, R> *scope_up;
	R                       scope_value;
	std::map<s32, T>        values;
};

template<typename T, typename R> Scope_Local_Vars<T, R> *
find_scope(Scope_Local_Vars<T, R> *scope, s32 scope_id) {
	while(true) {
		if(!scope)
			fatal_error(Mobius_Error::internal, "Misordering of scopes when looking up scope id: ", scope_id, ".");
		if(scope->scope_id == scope_id)
			return scope;
		scope = scope->scope_up;
	}
	return nullptr; // Unreachable.
}

template<typename T, typename R> T
find_local_var(Scope_Local_Vars<T, R> *scope, Local_Var_Id id) {
	
	auto scope2 = find_scope(scope, id.scope_id);
	
	auto find = scope2->values.find(id.id);
	if(find == scope2->values.end())
		fatal_error(Mobius_Error::internal, "A local variable is missing from a scope.");
	
	return find->second;
}

template<typename T, typename R> void
replace_local_var(Scope_Local_Vars<T, R> *scope, Local_Var_Id id, T entry) {
	
	auto scope2 = find_scope(scope, id.scope_id);
	
	auto find = scope2->values.find(id.id);
	if(find == scope2->values.end())
		fatal_error(Mobius_Error::internal, "A local variable is missing from a scope.");
	
	scope2->values[id.id] = entry;
}


#endif // MOBIUS_FUNCTION_TREE_H