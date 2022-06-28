
#ifndef MOBIUS_MODEL_BUILDER_H
#define MOBIUS_MODEL_BUILDER_H

#include "ast.h"

#include <unordered_map>

typedef s32 entity_id;

constexpr s32 invalid_entity_id = -1;

inline bool is_valid(entity_id id) { return id >= 0; }

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
Located_Value {
	//TODO: this becomes more complicated with dissolved substances etc.
	Location_Type type;
	
	entity_id compartment;
	entity_id property_or_substance;
};

template<> struct
Entity_Registration<Decl_Type::has> : Entity_Registration_Base {
	Located_Value   located_value;
	entity_id       override_unit;
	//entity_id    conc_unit;
	//String_View 
	Math_Block_AST *code;
	Math_Block_AST *initial_code;
};

struct
Located_Value_Hash {
	int operator()(const Located_Value &loc) const {
		if(loc.type != Location_Type::located)
			fatal_error(Mobius_Error::internal, "Tried to look up the state variable of a non-located value.");
		return loc.compartment + 97*loc.property_or_substance;   // Should be ok. We probably don't have more than 96 compartment types, and if that happens we are still ok with a clash.
	}
};

template<> struct
Entity_Registration<Decl_Type::flux> : Entity_Registration_Base {
	Located_Value    source;
	Located_Value    target;
	
	Math_Block_AST  *code;
};


struct Module_Declaration;

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
	
	Entity_Registration<decl_type> *operator[](entity_id handle) {
		if(!is_valid(handle) || handle >= registrations.size())
			fatal_error(Mobius_Error::internal, "Tried to look up an entity using an invalid handle.");
		return &registrations[handle];
	}
};

struct
Module_Declaration {
	String_View name;
	int major, minor, revision;
	
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

Module_Declaration *
process_module_declaration(Decl_AST *decl);

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
	std::unordered_map<Located_Value, state_var_id, Located_Value_Hash> location_to_id;
	
	void add_module(Module_Declaration *module);
};


#endif // MOBIUS_MODEL_BUILDER_H