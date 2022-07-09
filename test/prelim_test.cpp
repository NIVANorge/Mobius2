
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
	
	Mobius_Model *model = load_model("test_model.txt");
	//Mobius_Model model;
	//model.load_module("hbv_snow.txt");
	//model.load_module("test_soil.txt");
	
	model->compose();
	
	std::cout << "Composition done.\n";
	
	//delete module_decl; //NOTE: only safe after compose();
	
	emulate_model_run(model);
		
	std::cout << "Finished correctly.\n";
}