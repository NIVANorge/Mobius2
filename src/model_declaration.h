
#ifndef MOBIUS_MODEL_BUILDER_H
#define MOBIUS_MODEL_BUILDER_H

#include "ast.h"


typedef entity_handle s32;

template <typename Value_Type>
using string_map = std::unordered_map<String_View, Value_Type, String_View_Hash>;

struct Entity_Registration {
	bool has_been_declared;
	String_View   handle_name;
};

struct Compartment_Registration : Entity_Registration {
	String_View name;
}


//TODO: this has to be rethought since we have to avoid global clashes of names. The names have to be registered at the model instead!

template <typename Registration_Type>
struct Registry {
	string_map<entity_handle>      handle_name_to_handle;
	std::vector<Registration_Type> registrations;
	
	entity_handle register_entity(Token *handle_name, bool expect_not_exists, Linear_Allocator *allocator) {
		String_View handle_n = {};
		if(handle_name) {
			handle_n = handle_name->string_value;
			
			auto find = handle_name_to_handle.find(handle_n);
			if(find != handle_name_to_handle.end())
				return find->second;
		}
		
		if(expect_not_exists) {
			fatal_error();
		}
			
		handle_n = allocator->copy_string_view(handle_n);
		
		entity_handle h = (entity_handle)registrations.size();
		Registration_Type registration = {};
		registration.handle_name = handle_n;
		registrations.push_back(registration);
		
		if(handle_name)
			handle_name_to_handle[handle_n] = h;
		
		return h;
	}
};

struct Module_Declaration {
	String_View name;
	int major, minor, revision;
	
	String_View doc_string;
	
	Registry<Compartment_Registration> compartments;
};

entity_handle
register_compartment(String_View *handle_name, bool is_declaration, Token *name) {
	entity_handle =
}




Module_Declaration *
process_module_declaration(Decl_AST *decl, Linear_Allocator *allocator);


#endif // MOBIUS_MODEL_BUILDER_H