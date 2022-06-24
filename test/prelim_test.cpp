
#include "../src/linear_memory.h"
#include "../src/peek_queue.h"
#include "../src/file_utils.h"
#include "../src/datetime.h"
#include "../src/lexer.h"
#include "../src/ast.h"

//#include <windows.h>

int main() {
	//SetConsoleOutputCP(65001);
	
	Linear_Allocator memory;
	memory.initialize(1024*1024);

	Token_Stream stream("small_lexer_test.txt");
	
	Decl_AST *module_decl = parse_decl(&stream, &memory);
}