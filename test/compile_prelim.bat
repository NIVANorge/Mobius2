@echo off
g++ prelim_test.cpp ../src/lexer.cpp ../src/ast.cpp ../src/module_declaration.cpp ../src/model_declaration.cpp -O2 -std=c++11 -fno-exceptions -o prelim_test.exe 
REM -fmax-errors=5