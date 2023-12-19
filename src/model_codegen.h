
#ifndef MOBIUS_MODEL_CODEGEN_H
#define MOBIUS_MODEL_CODEGEN_H

#include "common_types.h"
#include "function_tree.h"
#include "state_variable.h"


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
		add_to_parameter_aggregate,
		add_to_connection_aggregate,
		external_computation,
	}                   type = Type::invalid;
	
	Var_Id              var_id     = invalid_var;
	Var_Id              source_id  = invalid_var;
	Var_Id              target_id  = invalid_var;
	Entity_Id           par_id     = invalid_entity_id;
	
	bool                subtract   = false; // For certain connection aggregations, we want to subtract from the aggregate instead of adding to it.
	
	Var_Loc_Restriction restriction; // May eventually need 2?   Is this one used correctly?
	
	Entity_Id           solver     = invalid_entity_id;
	
	Index_Set_Tuple index_sets;
	
	// TODO: Rename to depends_on, loose_depends_on and is_blocking
	std::set<int> depends_on_instruction; // Instructions that must be executed before this one.
	std::set<int> loose_depends_on_instruction; // Instructions that could go after this one only if they are in the same for loop.
	std::set<int> instruction_is_blocking; // Instructions that can not go in the same for loop as this one.
	
	// NOTE: We need both since the first can be dependencies on non-state-vars that are generated during instruction generation, while the other are dependencies on specific state vars that comes from code lookups, and there we need to take into account more data.
	std::set<int> inherits_index_sets_from_instruction;
	std::set<Identifier_Data> inherits_index_sets_from_state_var;
	
	int clear_instr = -1;  // This is the clear instruction of this instruction if this instruction is an aggregation variable.
	
	Math_Expr_FT *code = nullptr;
	
	bool visited = false;
	
	std::string debug_string(Model_Application *app) const;
	
	Model_Instruction() {};
	
	inline bool is_valid() { return type != Type::invalid;	}
	
	// Having trouble getting this to work. Seems like the destructor is called too early when the Instructions vector resizes, and setting up move constructors etc. to work around that is irksome.
	// If we instead store instructions in a vector of unique_ptr, (like for state_var), this problem should go away.
	/*
	~Model_Instruction() {
		if(code) delete code;
	}
	*/
};

struct
Batch_Array {
	std::vector<int>               instr_ids;
	Index_Set_Tuple                index_sets;
};

struct
Batch {
	Entity_Id solver;
	std::vector<int> instrs;
	std::vector<Batch_Array> arrays;
	std::vector<Batch_Array> arrays_ode;
	
	const Batch_Array &operator[](int idx) const {
		if(idx < arrays.size()) return arrays[idx];
		return arrays_ode[idx - arrays.size()];
	}
	int array_count() const { return arrays.size() + arrays_ode.size(); }
	
	Batch() : solver(invalid_entity_id) {}
};


void
instruction_codegen(Model_Application *app, std::vector<Model_Instruction> &instructions, bool initial);

Math_Expr_FT *
generate_run_code(Model_Application *app, Batch *batch, std::vector<Model_Instruction> &instructions, bool initial);


#endif // MOBIUS_MODEL_CODEGEN_H