@echo off
g++ prelim_test.cpp ../src/lexer.cpp ../src/ast.cpp ../src/module_declaration.cpp ../src/model_declaration.cpp ../src/function_tree.cpp  ../src/emulate.cpp ../src/units.cpp -O2 -std=c++11 -o prelim_test.exe 
REM -fmax-errors=5