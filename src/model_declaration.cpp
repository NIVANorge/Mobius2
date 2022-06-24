
#include "model_declaration.h"

inline void
match_declaration(Decl_AST *decl, const std::initializer_list<Token_Type> &pattern, bool allow_chain = false, bool allow_handle = true, bool allow_multiple_bodies = false, bool allow_body_modifiers = false) {
	if(!allow_chain && decl->decl_chain.size() > 1) {
		decl->decl_chain[0].print_error_header();
		fatal_error("This should not be a chained declaration.");
	}
	if(!allow_handle && decl->handle_name.string_value.count > 0) {
		decl->handle_name.print_error_header();
		fatal_error("This declaration should not have a handle");
	}
	if(decl->args.size() != pattern.size()) {
		Token decl_type = decl->decl_chain.back();
		decl_type.print_error_header();
		fatal_error("Wrong number of arguments to ", decl_type.string_value, " declaration.");
	}
	auto match = pattern.begin();
	for(auto arg : decl->args) {
		if(arg->sub_chain.size() != 1) {
			arg->sub_chain[0].print_error_header();    //TODO: not always safe..
			fatal_error("Expected a single symbol argument only.");
		}
		if(arg->sub_chain[0].type != *match) {
			arg->sub_chain[0].print_error_header();
			fatal_error("Expected an argument of type ", name(*match), " .");
		}
		++match;
	}
	if(!allow_multiple_bodies && decl->bodies.size() > 1) {
		decl->decl_chain.back().print_error_header();
		fatal_error("This declaration should not have multiple bodies.");
	}
	
	if(!allow_body_modifiers) {
		for(auto body : decl->bodies) {
			if(body->modifiers.size() > 0) {
				decl->decl_chain.back().print_error_header();
				fatal_error("The bodies of this declaration should not have modifiers.");
			}
		}
	}
}

inline Token *
single_arg(Decl_AST *decl, int which) {
	return &decl->args[which]->sub_chain[0];
}

Module_Declaration *
process_module_declaration(Decl_AST *decl, Linear_Allocator *allocator) {
	
	auto *module = allocator->make_new<Module_Declaration>();
	
	match_declaration(decl, {Token_Type::quoted_string, Token_Type::integer, Token_Type::integer, Token_Type::integer}, false, false, true, false);
	
	module->name  = allocator->copy_string_view(single_arg(decl, 0)->string_value);
	module->major = single_arg(decl, 1)->val_int;
	module->minor = single_arg(decl, 2)->val_int;
	module->revision = single_arg(decl, 3)->val_int;

	Decl_Body_AST *body = reinterpret_cast<Decl_Body_AST *>(decl->bodies[0]);
	
	if(body->doc_string.type == Token_Type.quoted_string)
		module->doc_string = allocator->copy_string_view(body->doc_string.string_value);
	
	for(Decl_AST *child : decl->child_decls) {
		Token *type_token = &child->decl_chain.back();
		String_View type = type_token->string_value;
		
		//TODO: finish!
		if(type == "compartment") {
			
		} else if (type == "par_group") {
		} else if (type == "substance") {
		} else if (type == "property") {
		} else if (type == "has") {
		} else if (type == "flux") {
		} else {
			type_token->print_error_header();
			if (type == "par_real" || type == "module" || type == "model") {
				fatal_error("Declarations of type ", type, " are not allowed inside declarations of type module .");
			}
			fatal_error("Unrecognized declaration type ", type, " .");
		}
	}
	
	return nullptr;
}