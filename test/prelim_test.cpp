
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
	
	File_Data_Handler files;
	
	String_View file_name = "small_lexer_test.txt";
	String_View file_data = files.load_file(file_name);
	Token_Stream stream(file_name, file_data);
	
	Decl_AST *module_decl = parse_decl(&stream);
	
	Module_Declaration *module = process_module_declaration(0, module_decl);
	
	Mobius_Model model;
	model.add_module(module);
	
	model.compose();
	
	std::cout << "Composition done.\n";
	
	delete module_decl; //NOTE: only safe after compose();
	
	emulate_model_run(&model);
		
	std::cout << "Finished correctly.\n";
}