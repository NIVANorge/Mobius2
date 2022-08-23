

#include "function_tree.h"
#include "run_model.h"

struct
LLVM_Constant_Data {
	s64 *neighbor_data;
	s64 neighbor_data_count;
};

void initialize_llvm();

struct LLVM_Module_Data;

LLVM_Module_Data *
create_llvm_module();

void
jit_compile_module(LLVM_Module_Data *data);

void
free_llvm_module(LLVM_Module_Data *data);

void
jit_add_global_data(LLVM_Module_Data *data, LLVM_Constant_Data *constants);

void
jit_add_batch(Math_Expr_FT *expr, const std::string &function_name, LLVM_Module_Data *data);

batch_function *
get_jitted_batch_function(const std::string &function_name);