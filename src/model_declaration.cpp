
#include "module_declaration.h"

bool matches(std::vector<Argument_AST> *args, const std::initializer_list<Token_Type> &pattern) {
	if(args->size() != pattern->size()) return false;
	int idx = 0;
	for(auto arg : *args) {
		if(arg->sub_chain.size() != 1)
			return false;
		if(arg->sub_chain[0].type != pattern[idx])
			return false;
		++idx;
	}
	return true;
}


Module_Declaration *
build_module_declaration(Decl_AST *decl, Linear_Allocator *allocator) {
	//TODO;
	return nullptr;
}