
#ifndef MOBIUS_MODEL_CODEGEN_H
#define MOBIUS_MODEL_CODEGEN_H

#include "common_types.h"
#include "function_tree.h"

struct
Model_Instruction {
	enum class
	Type {
		invalid,
		compute_state_var,
		subtract_flux_from_source,
		add_flux_to_target,
		clear_state_var,
		add_to_aggregate,
	}                   type;
	
	Var_Id              var_id;
	Var_Id              source_or_target_id;
	Entity_Id           connection;
	
	Entity_Id           solver;
	
	std::set<Entity_Id> index_sets;
	
	std::set<int> depends_on_instruction; // Instructions that must be executed before this one.
	std::set<int> instruction_is_blocking; // Instructions that can not go in the same for loop as this one
	std::set<int> inherits_index_sets_from_instruction;
	
	Math_Expr_FT *code;
	
	bool visited;
	bool temp_visited;
	
	Model_Instruction() : visited(false), temp_visited(false), var_id(invalid_var), solver(invalid_entity_id), connection(invalid_entity_id), type(Type::invalid), code(nullptr) {};
};

struct
Batch_Array {
	std::vector<int>         instr_ids;
	std::set<Entity_Id>      index_sets;
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