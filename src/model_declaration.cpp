
#include "model_declaration.h"


void
see_if_handle_name_exists_in_scope(Module_Declaration *module, Token *handle_name) {
	
	// check for reserved names
	static const char * reserved[] = {
		#define ENUM_VALUE(name, body_type) #name,
		#include "decl_types.incl"
		#undef ENUM_VALUE
		"if", "otherwise",
		//todo: probably more?
	}
	
	if(std::find(reserved.begin(), reserved.end(), handle_name->string_value) != reserved.end()) {
		handle_name->print_error_header();
		fatal_error("The name \"", handle_name->string_value, "\" is reserved.");
	}
	
	auto find = module->handles_in_scope.find(handle_name->string_value);
	if(find != module->handles_in_scope.end()) {
		hand_name->print_error_header();
		error_print("The name \"", handle_name->string_value, "\" was already used here:\n");
		Entity_Registration *reg = find->second;
		reg->handle_name.print_error_location();
		error_print("with type ", name(reg->type), ".");
	}
}

template <typename Registration_Type, Decl_Type decl_type> entity_handle
Registry<Registration_Type, Decl_Type>::find_or_create(Token *handle_name, Token *name, bool this_is_the_declaration = false) {
		
	if(handle_name) {
		auto find = handle_name_to_handle.find(handle_name->string_value);
		if(find != handle_name_to_handle.end()) {
			if(this_is_the_declaration && registrations[find->second].has_been_declared) {
				hand_name->print_error_header();
				fatal_error("Re-declaration of handle \"", handle_name->string_value, "\". It was previously declared here:\n");
				registrations[find->second].print_error_location();
			}
			
			return find->second;
		}
	} else if (name) {
		auto find = name_to_handle.find(name->string_value);
		if(find != name_to_handle.end())
			return find->second;
		else
			return invalid_entity_handle;
	}
	
	// See if the handle is reserved or exists for another entity type.
	if(handle_name)
		see_if_handle_name_exists_in_scope(parent, handle_name);
	
	
	// We did not find it, so we create it.
	entity_handle h = (entity_handle)registrations.size();
	Entity_Registration registration = {};
	registration.handle_name = *handle_name;
	registration.type        = decl_type;
	if(name) registration.name = name->string_value;
	if(this_is_the_declaration) registration.has_been_declared = true;
	registrations.push_back(registration);
	
	if(handle_name) {
		handle_name_to_handle[handle_name->string_value] = h;
		parent->handle_names_in_scope[handle_name->string_value] = &registration;
	}
	if(name)
		name_to_handle[name->string_value] = h;
	
	return h;
}



inline void
match_declaration(Decl_AST *decl, const std::initializer_list<Token_Type> &pattern, int allow_chain = 0, bool allow_handle = true, bool allow_multiple_bodies = false, bool allow_body_modifiers = false) {
	// allow_chain = 0 means no chain. allow_chain=-1 means any length. allow_chain = n means only of length n exactly.
	
	if(!allow_chain && !decl->decl_chain.empty() || allow_chain > 0 && decl->decl_chain.size() != allow_chain) {
		decl->decl_chain[0].print_error_header();
		fatal_error("This should not be a chained declaration.");
	}
	if(!allow_handle && decl->handle_name.string_value.count > 0) {
		decl->handle_name.print_error_header();
		fatal_error("This declaration should not have a handle");
	}
	if(decl->args.size() != pattern.size()) {
		decl->type_name.print_error_header();
		fatal_error("Wrong number of arguments to ", decl->type_name.string_value, " declaration.");
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
	// NOTE: We already checked in the AST processing stage if the declaration is allowed to have bodies at all. This just checks if it can have more than one.
	if(!allow_multiple_bodies && decl->bodies.size() > 1) {
		decl->type_name.print_error_header();
		fatal_error("This declaration should not have multiple bodies.");
	}
	
	if(!allow_body_modifiers) {
		for(auto body : decl->bodies) {
			if(body->modifiers.size() > 0) {
				decl->type_name.print_error_header();
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
	
	//TODO: have to decide whether we should copy over string views at this point.
	
	auto *module = allocator->make_new<Module_Declaration>();
	
	match_declaration(decl, {Token_Type::quoted_string, Token_Type::integer, Token_Type::integer, Token_Type::integer}, 0, false, true, false);
	
	module->name  = single_arg(decl, 0)->string_value;
	module->major = single_arg(decl, 1)->val_int;
	module->minor = single_arg(decl, 2)->val_int;
	module->revision = single_arg(decl, 3)->val_int;

	Decl_Body_AST *body = reinterpret_cast<Decl_Body_AST *>(decl->bodies[0]);
	
	if(body->doc_string.type == Token_Type::quoted_string)
		module->doc_string = body->doc_string.string_value;
	
	for(Decl_AST *child : body->child_decls) {
		
		//TODO: finish!
		switch(child->type) {
			case Decl_Type::compartment : {
				match_declaration(child, {Token_Type::quoted_string});
				register_compartment(module, &child->handle_name, single_arg(child, 0));
			} break;
			
			case Decl_Type::par_group : {
				match_declaration(child, {Token_Type::quoted_string}, 1);
				Token *compartment_handle = &child->decl_chain[0];
				// TODO:
				// auto handle = register_par_group(module, compartment_handle, &child->handle_name, single_arg(child, 0));
				//    ... process parameter declarations in the body.
			} break;
			
			case Decl_Type::substance : {
				
			} break;
			
			case Decl_Type::property : {
			
			} break;
			
			case Decl_Type::has : {
				
			} break;
			
			case Decl_Type::flux : {
				
			} break;
			
			case Decl_Type::unit : {
				
			} 
			
			default : {
				child->type_name.print_error_header();
				fatal_error("Did not expect a declaration of type ", child->type_name.string_value, " inside a module declaration.");
			};
		}
	}
	
	return module;
}



