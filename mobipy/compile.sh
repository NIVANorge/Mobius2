#!/bin/bash
g++ -fmax-errors=5 -fpic -m64 -std=c++17 -c -O2 ../src/c_abi.cpp ../src/llvm_jit.cpp ../src/resolve_identifier.cpp ../src/model_compilation.cpp  ../src/model_codegen.cpp ../src/tree_pruning.cpp ../src/spreadsheet_inputs_openxlsx.cpp ../src/process_series_data.cpp ../src/data_set.cpp ../src/model_application.cpp ../src/model_composition.cpp ../src/run_model.cpp ../src/lexer.cpp ../src/ast.cpp ../src/model_declaration.cpp ../src/function_tree.cpp  ../src/emulate.cpp ../src/units.cpp ../src/ode_solvers.cpp ../src/file_utils.cpp ../src/connection_regex.cpp ../src/index_data.cpp ../src/catalog.cpp ../src/model_specific/nivafjord_special.cpp -DMOBIUS_ERROR_STREAMS

g++ -o c_abi.so -m64 -shared c_abi.o