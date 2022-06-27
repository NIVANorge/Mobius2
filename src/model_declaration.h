
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

struct Par_Group_Registration   : Entity_Registration {
	entity_handle                  compartment;  //TODO: could also be substance
	std::vector<entity_handle>     parameters;   //TODO: may not be necessary to store these here since the parameters already know what group they are in??
};

union Parameter_Value {
	double    val_double;
	s64       val_int;
	u64       val_bool;
	u64       val_enum;
	Date_Time val_datetime;
	
	Parameter_Value() : val_datetime() {};
};

struct Par_Registration         : Entity_Registration {
	entity_handle  par_group;
	entity_handle  unit;
	
	Parameter_Value default_val;
	Parameter_Value min_val;
	Parameter_Value max_val;
	
	String_View     description;
};

struct Unit_Registration        : Entity_Registration {
	// TODO: put data here.
};

struct Substance_Registration   : Entity_Registration {
	entity_handle  unit;
};

struct Module_Declaration;

template <typename Registration_Type, Decl_Type decl_type>
struct Registry {
	string_map<entity_handle>      handle_name_to_handle;
	string_map<entity_handle>      name_to_handle;
	std::vector<Registration_Type> registrations;
	Module_Declaration            *parent;
	
	Registry(Module_Declaration *parent) : parent(parent) {}
	
	entity_handle
	find_or_create(Token *handle_name, Token *name = nullptr, bool this_is_the_declaration = false);
	
	Registration_Type *operator[](entity_handle handle) {
		if(!is_valid(handle) || handle >= registrations.size())
			fatal_error(Mobius_Error::internal, "Tried to look up an entity using an invalid handle.");
		return &registrations[handle];
	}
};

struct Module_Declaration {
	String_View name;
	int major, minor, revision;
	
	String_View doc_string;
	
	string_map<std::pair<Decl_Type, entity_handle>> handles_in_scope;
	
	Registry<Compartment_Registration, Decl_Type::compartment> compartments;
	Registry<Par_Group_Registration,   Decl_Type::par_group>   par_groups;
	Registry<Par_Registration,         Decl_Type::par_real>    parameters;     // NOTE: par_real is a stand-in for all parameter declarations.
	Registry<Unit_Registration,        Decl_Type::unit>        units;
	Registry<Substance_Registration,   Decl_Type::substance>   substances;
	
	Module_Declaration() : 
		compartments(this),
		par_groups  (this),
		parameters  (this),
		units       (this),
		substances  (this)
	{}
};




Module_Declaration *
process_module_declaration(Decl_AST *decl);


#endif // MOBIUS_MODEL_BUILDER_H