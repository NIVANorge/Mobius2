
#ifndef MOBIUS_MODEL_CODEGEN_H
#define MOBIUS_MODEL_CODEGEN_H

#include "common_types.h"
#include "function_tree.h"
#include "state_variable.h"

struct
Index_Set_Dependency {
	Entity_Id id;
	int order;
	
	Index_Set_Dependency(Entity_Id id) : id(id), order(1) {}
	Index_Set_Dependency(Entity_Id id, int order) : id(id), order(order) {}
};

inline bool operator<(const Index_Set_Dependency &a, const Index_Set_Dependency &b) {
	if(a.id == b.id) return a.order < b.order;
	return a.id < b.id;
	//if(a.order == b.order) return a.id < b.id;
	//return a.order < b.order;
}

inline bool operator==(const Index_Set_Dependency &a, const Index_Set_Dependency &b) {
	return (a.order == b.order) && (a.id == b.id);
}

struct
Model_Instruction {
	enum class
	Type {
		invalid,
		compute_state_var,
		clear_state_var,
		subtract_discrete_flux_from_source,
		add_discrete_flux_to_target,
		add_to_aggregate,
		add_to_connection_aggregate,
		special_computation,
	}                   type;
	
	Var_Id              var_id;
	Var_Id              source_id;
	Var_Id              target_id;
	
	Var_Loc_Restriction restriction; // May eventually need 2?
	
	Entity_Id           solver;
	
	std::set<Index_Set_Dependency> index_sets;
	
	std::set<int> depends_on_instruction; // Instructions that must be executed before this one.
	std::set<int> instruction_is_blocking; // Instructions that can not go in the same for loop as this one.
	std::set<int> loose_depends_on_instruction; // Instructions that could go after this one if they are in the same for loop.
	
	// NOTE: We need both since the first can be dependencies on non-state-vars that are generated during instruction generation, while the other are dependencies on specific state vars that comes from code lookups, and there we need to take into account more data.
	std::set<int> inherits_index_sets_from_instruction;
	std::set<Identifier_Data> inherits_index_sets_from_state_var;
	
	Math_Expr_FT *code;
	Math_Expr_FT *specific_target;
	
	bool visited;
	bool temp_visited;
	
	std::string debug_string(Model_Application *app);
	
	Model_Instruction() : visited(false), temp_visited(false), var_id(invalid_var), source_id(invalid_var), target_id(invalid_var), restriction(), solver(invalid_entity_id), type(Type::invalid), code(nullptr), specific_target(nullptr) {};
	
	// Having trouble getting this to work. Seems like the destructor is called too early when the Instructions vector resizes, and setting up move constructors etc. to work around that is irksome.
	/*
	~Model_Instruction() {
		if(code) delete code;
	}
	
	Model_Instruction(const Model_Instruction &other) = delete;
	Model_Instruction(Model_Instruction &&other) = default;
	*/
};

struct
Batch_Array {
	std::vector<int>               instr_ids;
	std::set<Index_Set_Dependency> index_sets;
};

struct Batch {
	Entity_Id solver;
	std::vector<int> instrs;
	std::vector<Batch_Array> arrays;
	std::vector<Batch_Array> arrays_ode;
	
	Batch() : solver(invalid_entity_id) {}
};


void
instruction_codegen(Model_Application *app, std::vector<Model_Instruction> &instructions, bool initial);

Math_Expr_FT *
generate_run_code(Model_Application *app, Batch *batch, std::vector<Model_Instruction> &instructions, bool initial);


#endif // MOBIUS_MODEL_CODEGEN_H