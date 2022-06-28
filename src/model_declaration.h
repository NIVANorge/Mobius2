
#ifndef MOBIUS_MODEL_BUILDER_H
#define MOBIUS_MODEL_BUILDER_H

#include "ast.h"

#include <unordered_map>

struct Module_Declaration;

struct entity_id {
	//TODO: should this just contain the type too?
	s32 module_id;
	s32 id;
	
	entity_id &operator *() { return *this; }  //trick so that it can be an iterator to itself..
	entity_id &operator++() { id++; return *this; }
};

inline bool
operator==(const entity_id &a, const entity_id &b) {
	return a.module_id == b.module_id && a.id == b.id;
}

inline bool
operator!=(const entity_id &a, const entity_id &b) {
	return a.module_id != b.module_id || a.id != b.id;
}

constexpr entity_id invalid_entity_id = {-1, -1};

inline bool is_valid(entity_id id) { return id.module_id >= 0 && id.id >= 0; }

template <typename Value_Type>
using string_map = std::unordered_map<String_View, Value_Type, String_View_Hash>;

struct
Entity_Registration_Base {
	Decl_Type       type;
	bool            has_been_declared;
	Source_Location location;            // if it has_been_declared, this should be the declaration location, otherwise it should be the location where it was last referenced.
	String_View     handle_name;
	String_View     name;
};

template<Decl_Type decl_type> struct
Entity_Registration : Entity_Registration_Base {
	//TODO: delete constructor or something like that
};

template<> struct
Entity_Registration<Decl_Type::compartment> : Entity_Registration_Base {
	// no need for additional info right now..
};

template<> struct
Entity_Registration<Decl_Type::par_group> : Entity_Registration_Base {
	entity_id              compartment;  //TODO: could also be substance
	std::vector<entity_id> parameters;   //TODO: may not be necessary to store these here since the parameters already know what group they are in??
};

union
Parameter_Value {
	double    val_double;
	s64       val_int;
	u64       val_bool;
	u64       val_enum;
	Date_Time val_datetime;
	
	Parameter_Value() : val_datetime() {};
};

template<> struct
Entity_Registration<Decl_Type::par_real> : Entity_Registration_Base {
	entity_id       par_group;
	entity_id       unit;
	
	Parameter_Value default_val;
	Parameter_Value min_val;
	Parameter_Value max_val;
	
	String_View     description;
};

template<> struct
Entity_Registration<Decl_Type::unit> : Entity_Registration_Base {
	// TODO: put data here.
};

template<> struct
Entity_Registration<Decl_Type::property> : Entity_Registration_Base {
	//NOTE: this is in practice used both for property and substance
	entity_id    unit;
};

enum class
Location_Type {
	nowhere, out, located,
};

struct
Value_Location {
	//TODO: this becomes more complicated with dissolved substances etc.
	Location_Type type;
	
	entity_id compartment;
	entity_id property_or_substance;
};

inline bool
operator==(const Value_Location &a, const Value_Location &b) {
	return a.type == b.type && a.compartment == b.compartment && a.property_or_substance == b.property_or_substance;
}

template<> struct
Entity_Registration<Decl_Type::has> : Entity_Registration_Base {
	Value_Location value_location;
	entity_id      override_unit;
	//entity_id    conc_unit;
	//String_View 
	Math_Block_AST *code;
	Math_Block_AST *initial_code;
};

struct
Value_Location_Hash {
	int operator()(const Value_Location &loc) const {
		if(loc.type != Location_Type::located)
			fatal_error(Mobius_Error::internal, "Tried to look up the state variable of a non-located value.");
		// hopefully this one is ok...
		return
			 loc.compartment.module_id
		+ 23*loc.compartment.id
		+ 97*loc.property_or_substance.module_id
		+ 2237*loc.property_or_substance.id;
	}
};

template<> struct
Entity_Registration<Decl_Type::flux> : Entity_Registration_Base {
	Value_Location   source;
	Value_Location   target;
	
	Math_Block_AST  *code;
};

struct Registry_Base {
	string_map<entity_id>          handle_name_to_handle;
	string_map<entity_id>          name_to_handle;
	Module_Declaration            *parent;
	
	virtual entity_id find_or_create(Token *handle_name, Token *name = nullptr, Decl_AST *declaration = nullptr);
	virtual Entity_Registration_Base *operator[](entity_id handle);
	
	Registry_Base(Module_Declaration *parent) : parent(parent) {}
};

template <Decl_Type decl_type> struct
Registry : Registry_Base {
	
	std::vector<Entity_Registration<decl_type>> registrations;
	
	Registry(Module_Declaration *parent) : Registry_Base(parent) {}
	
	entity_id
	find_or_create(Token *handle_name, Token *name = nullptr, Decl_AST *declaration = nullptr);
	
	entity_id
	standard_declaration(Decl_AST *decl);
	
	Entity_Registration<decl_type> *operator[](entity_id id);
	
	entity_id begin();
	entity_id end();
};

struct
Module_Declaration {
	String_View name;
	int major, minor, revision;
	
	s32 module_id;
	
	String_View doc_string;
	
	string_map<std::pair<Decl_Type, entity_id>> handles_in_scope;
	
	Registry<Decl_Type::compartment> compartments;
	Registry<Decl_Type::par_group>   par_groups;
	Registry<Decl_Type::par_real>    parameters;     // NOTE: par_real is a stand-in for all parameter declarations.
	Registry<Decl_Type::unit>        units;
	Registry<Decl_Type::property>    properties_and_substances;  // NOTE: is used for both Decl_Type::property and Decl_Type::substance.
	Registry<Decl_Type::has>         hases;
	Registry<Decl_Type::flux>        fluxes;
	
	Module_Declaration() : 
		compartments(this),
		par_groups  (this),
		parameters  (this),
		units       (this),
		properties_and_substances(this),
		hases       (this),
		fluxes      (this)
	{}
	
	Registry_Base *	registry(Decl_Type);
};

template<Decl_Type decl_type>
Entity_Registration<decl_type> *Registry<decl_type>::operator[](entity_id id) {
	if(!is_valid(id) || id.id >= registrations.size() || id.module_id != parent->module_id)
		fatal_error(Mobius_Error::internal, "Tried to look up an entity using an invalid handle.");
	return &registrations[id.id];
}

template<Decl_Type decl_type>
entity_id Registry<decl_type>::begin() { return {parent->module_id, 0}; }

template<Decl_Type decl_type>
entity_id Registry<decl_type>::end()   { return {parent->module_id, (s32)registrations.size()}; }


Module_Declaration *
process_module_declaration(int module_id, Decl_AST *decl);

typedef s32 state_var_id;

struct
State_Variable {
	Decl_Type type;     //either flux, substance or property
	entity_id entity;
};

struct
Mobius_Model {
	Module_Declaration *module;   //TODO: allow multiple modules!
	
	std::vector<State_Variable> state_variables;
	std::unordered_map<Value_Location, state_var_id, Value_Location_Hash> location_to_id;
	
	void add_module(Module_Declaration *module);
};

#endif // MOBIUS_MODEL_BUILDER_H