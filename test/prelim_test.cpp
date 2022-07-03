
#include "../src/linear_memory.h"
#include "../src/peek_queue.h"
#include "../src/file_utils.h"
#include "../src/datetime.h"
#include "../src/lexer.h"
#include "../src/ast.h"
#include "../src/model_declaration.h"
#include "../src/emulate.h"

//#include <windows.h>

int main() {
	//SetConsoleOutputCP(65001);
	
	//Linear_Allocator memory;
	//memory.initialize(32*1024*1024);

	auto file_name = "small_lexer_test.txt";
	String_View file_data = read_entire_file(file_name);
	Token_Stream stream(file_name, file_data);
	
	Decl_AST *module_decl = parse_decl(&stream);
	
	Module_Declaration *module = process_module_declaration(0, module_decl);
	
	Mobius_Model model;
	model.add_module(module);
	
	model.compose();
	
	std::cout << "Composition done.\n";
	
	delete module_decl; //NOTE: only safe after compose();
	
	Model_Run_State run_state;
	run_state.parameters = (Parameter_Value *)malloc(sizeof(Parameter_Value)*module->parameters.registrations.size());
	run_state.parameters[0].val_double = 5.1;
	run_state.parameters[1].val_double = 3.2;
	run_state.state_vars = (double *)malloc(sizeof(double)*model.state_vars.vars.size());
	run_state.series     = (double *)malloc(sizeof(double)*100); //TODO.
	run_state.series[0] = 2.0;
	State_Variable *var = &model.state_vars.vars[0];
	std::cout << module->hases[var->entity_id]->handle_name << " has function tree " << (size_t)var->function_tree << std::endl;
	Parameter_Value val = emulate_expression(var->function_tree, &run_state);
	std::cout << "got " << val.val_double << "\n";
	
	free(file_data.data); //NOTE: only safe after completely finished with the model, since all string names point into the file data.
	
	std::cout << "Finished correctly.\n";
}