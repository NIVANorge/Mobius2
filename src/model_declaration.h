
#ifndef MOBIUS_MODEL_BUILDER_H
#define MOBIUS_MODEL_BUILDER_H

#include "ast.h"

#include <unordered_map>

typedef s32 entity_handle;

constexpr s32 invalid_entity_handle = -1;

inline bool is_valid(entity_handle handle) { return handle >= 0; }

template <typename Value_Type>
using string_map = std::unordered_map<String_View, Value_Type, String_View_Hash>;

struct Entity_Registration {
	Decl_Type     type;
	bool          has_been_declared;
	Token         handle_name;
	String_View   name;
};

struct Compartment_Registration : Entity_Registration {
	// no need for additional info right now..
	
	
};


struct Module_Declaration;

template <typename Registration_Type, Decl_Type decl_type>
struct Registry {
	string_map<entity_handle>      handle_name_to_handle;
	string_map<entity_handle>      name_to_handle;
	std::vector<Registration_Type> registrations;
	Module_Declaration            *parent;
	
	entity_handle
	find_or_create(Token *handle_name, Token *name, bool this_is_the_declaration = false);
	
	Registration_Type *operator[](entity_handle handle) {
		if(!is_valid(handle) || handle >= registrations.size()) {
			fatal_error(Mobius_Error::internal, "Tried to look up an entity using an invalid handle.");
		}
		return &registrations[handle];
	}
};

struct Module_Declaration {
	String_View name;
	int major, minor, revision;
	
	String_View doc_string;
	
	string_map<Entity_Registration *> handles_in_scope;
	
	Registry<Compartment_Registration, Decl_Type::compartment> compartments;
	
	Module_Declaration() {
		compartments.parent = this;
	}
};

inline entity_handle
register_compartment(String_View *handle_name, bool is_declaration, Token *name) {
	
	//TODO.
	entity_handle handle = 0;
	return handle;
}




Module_Declaration *
process_module_declaration(Decl_AST *decl, Linear_Allocator *allocator);


#endif // MOBIUS_MODEL_BUILDER_H