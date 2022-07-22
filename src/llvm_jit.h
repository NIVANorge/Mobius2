

#include "function_tree.h"

typedef void batch_function(double *parameters, double *series, double *state_vars, double *solver_workspace);

void initialize_llvm();

struct LLVM_Module_Data;

LLVM_Module_Data *
create_llvm_module();

void
jit_compile_module(LLVM_Module_Data *data);

void
free_llvm_module(LLVM_Module_Data *data);

void
jit_add_batch(Math_Expr_FT *expr, const std::string &function_name, LLVM_Module_Data *data);

batch_function *
get_jitted_batch_function(const std::string &function_name);