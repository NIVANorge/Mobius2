
#ifndef MOBIUS_MODEL_BUILDER_H
#define MOBIUS_MODEL_BUILDER_H

#include "ast.h"

struct Module_Declaration {
	String_View name;
	int major, minor, revision;
	
	String_View doc_string;
};

Module_Declaration *
build_module_declaration(Decl_AST *decl, Linear_Allocator *allocator);


#endif // MOBIUS_MODEL_BUILDER_H