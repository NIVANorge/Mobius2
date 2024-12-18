
#ifndef MOBIUS_LLVM_JIT_H
#define MOBIUS_LLVM_JIT_H

#include "function_tree.h"
#include "run_model.h"

constexpr int data_alignment = 32;

struct
LLVM_Constant_Data {
	s32 *connection_data;
	s64 connection_data_count;
	s32 *index_count_data;
	s64 index_count_data_count;
};

void initialize_llvm();

struct LLVM_Module_Data;

LLVM_Module_Data *
create_llvm_module();

void
jit_compile_module(LLVM_Module_Data *data, std::string *output_string);

void
free_llvm_module(LLVM_Module_Data *data);

void
jit_add_global_data(LLVM_Module_Data *data, LLVM_Constant_Data *constants, int llvm_module_instance);

void
jit_add_batch(Math_Expr_FT *expr, const std::string &function_name, LLVM_Module_Data *data);

batch_function *
get_jitted_batch_function(const std::string &function_name);

#endif // MOBIUS_LLVM_JIT_H