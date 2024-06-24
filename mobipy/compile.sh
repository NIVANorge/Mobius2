#!/bin/bash
clang -Wno-return-type -Wno-switch -std=c++17 -shared -undefined dynamic_lookup -DMOBIUS_ERROR_STREAMS -fcxx-exceptions -I/usr/lib/llvm-18/include -DLLVM18 -D_GNU_SOURCE -D__STDC_CONSTANT_MACROS -D__STDC__FORMAT_MACROS -D__STDC__LIMIT_MACROS -L/usr/lib/llvm-18/lib -lLLVM-18 -I/usr/local/include/OpenXLSX -o c_abi.so ../src/c_abi.cpp ../src/llvm_jit.cpp ../src/resolve_identifier.cpp ../src/model_compilation.cpp  ../src/model_codegen.cpp ../src/tree_pruning.cpp ../src/spreadsheet_inputs_openxlsx.cpp ../src/process_series_data.cpp ../src/data_set.cpp ../src/model_application.cpp ../src/model_composition.cpp ../src/run_model.cpp ../src/lexer.cpp ../src/ast.cpp ../src/model_declaration.cpp ../src/function_tree.cpp  ../src/emulate.cpp ../src/units.cpp ../src/ode_solvers.cpp ../src/file_utils.cpp ../src/connection_regex.cpp ../src/index_data.cpp ../src/catalog.cpp ../src/model_specific/nivafjord_special.cpp