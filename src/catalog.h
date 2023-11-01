
#ifndef MOBIUS_CATALOG_H
#define MOBIUS_CATALOG_H

#include "ast.h"

struct
Scope_Entity {
	std::string handle;
	Entity_Id id  = invalid_entity_id;
	bool external = false;
	bool was_referenced = false;
	bool is_load_arg = false;
	Source_Location source_loc;
};

struct
Serial_Entity {
	Entity_Id id = invalid_entity_id;
	Source_Location source_loc;
};

struct Catalog;
struct Mobius_Model;

struct
Decl_Scope {
	struct Entity_Id_Hash {
		int operator()(const Entity_Id &id) const { return 97*(int)id.reg_type + id.id; }
	};
	
	Entity_Id parent_id = invalid_entity_id; // Id of module or library this is the scope of. Invalid if it is the global or model scope.
	
	Scope_Entity *add_local(const std::string &handle, Source_Location source_loc, Entity_Id id);
	Scope_Entity *register_decl(Decl_AST *ast, Entity_Id id);
	void set_serial_name(const std::string &serial_name, Source_Location source_loc, Entity_Id id);
	void import(const Decl_Scope &other, Source_Location *import_loc = nullptr, bool allow_recursive_import_params = false);
	void check_for_unreferenced_things(Mobius_Model *model);
	
	Entity_Id resolve_argument(Reg_Type expected_type, Argument_AST *arg);
	
	Scope_Entity *operator[](const std::string &handle) {
		auto find = visible_entities.find(handle);
		if(find != visible_entities.end()) {
			find->second.was_referenced = true;
			return &find->second;
		}
		return nullptr;
	}
	
	const std::string& operator[](Entity_Id id) {
		auto find = handles.find(id);
		if(find == handles.end())
			fatal_error(Mobius_Error::internal, "Attempt to look up handle name of an entity id in a scope where it was not declared.");
		return find->second;
	}
	
	Entity_Id deserialize(const std::string &serial_name, Reg_Type expected_type) const;
	

	std::unordered_map<std::string, Scope_Entity>                  visible_entities;
	std::unordered_map<std::string, Serial_Entity>                 serialized_entities;
	std::unordered_map<Entity_Id, std::string, Entity_Id_Hash>     handles;
	std::unordered_map<Decl_AST *, Entity_Id>                      by_decl;  // Used to look up the id we put for a decl when we go back and process it.
	
	std::set<Entity_Id>                                            all_ids;
};

struct
Registration_Base {
	Decl_AST        *decl;
	// TODO: The below two are now not needed.
	//Decl_Type       decl_type;
	//Source_Location source_loc;         // if it has_been_declared, this should be the declaration location. Will not always be canonical for all entity types (some can be redeclared)
	Entity_Id       scope_id = invalid_entity_id;  // The id of the module or library where this entity was declared. (It could have been imported to other scopes also, that is not reflected here)
	std::string     name;
	bool            has_been_processed = false;
};

template<Reg_Type reg_type> struct
Registration : Registration_Base {
	Registration() = delete;  // NOTE: prevent instantiation of the completely generic version.
	
	void process_declaration(Catalog *catalog, Decl_Scope *scope);
};

struct
Registry_Base {
	virtual Entity_Id register_decl(Decl_Scope *scope, Decl_AST *decl) = 0;
	virtual Registration_Base *operator[](Entity_Id id) = 0;
};

template<Reg_Type reg_type> struct
Registry : Registry_Base {

	// NOTE: This one should no longer process the serial_name, that should be done in the later processing instead.
	// Although that would make it a bit annoying to remove stuff based on "extend" criteria Hmm..
	// But if we do that we have to standardize name position or something like that :(( ugh.
	Entity_Id
	register_decl(Decl_Scope *scope, Decl_AST *decl);
	
	Entity_Id
	create_internal(Decl_Scope *scope, const std::string &handle, const std::string &name, Decl_Type decl_type);
	
	s64 count() { return (s64)registrations.size(); }
	Entity_Id begin() { return { reg_type, 0 }; }
	Entity_Id end() { return { reg_type, (s16)registrations.size() }; }
	
	Registration<Reg_Type> *operator[](Entity_Id id);
	
private :
	std::vector<Registration<reg_type>>  registrations;
};

struct
Catalog {
	
	void
	register_decls_recursive(Decl_Scope *scope, Decl_AST *decl, std::vector<Decl_Type> &allowed_types);
	
	Registration_Base *
	find_entity(Entity_Id id) { return (*registry(id.reg_type))[id]; }
	
	// TODO: This one probably has to be virtual too?
	Decl_Scope *
	get_scope(Entity_Id id);
	
	template<Reg_Type reg_type> struct
	By_Scope {
		By_Scope(Decl_Scope *scope) : scope(scope) {
			end_it.scope = scope;
			end_it.it = scope->all_ids.end();
		}
		
		struct
		Scope_It {
			std::unordered_set<Entity_Id>::iterator it;
			Decl_Scope *scope;
			Scope_It &operator++() {
				do it++; while(it != scope->all_ids.end() && (it->reg_type != reg_type));
				return *this;
			}
			Entity_Id operator*() { return *it; }
			bool operator!=(const Scope_It &other) { return it != other.it; }
		};
		
		Scope_It end_it;
		Decl_Scope *scope;
		
		Scope_It begin() {
			Scope_It it;
			it.scope = scope;
			it.it = scope->all_ids.begin();
			while(it != end_it && (it.it->reg_type != reg_type))
				++it.it;
			return it;
		}
		Scope_It end() { return end_it; }
		
		s64 size() {
			s64 sz = 0;
			for(auto id : scope->all_ids)
				if(id.reg_type == reg_type) ++sz;
			return sz;
		}
	};
	
	template<Reg_Type reg_type> By_Scope<reg_type>
	by_scope(Entity_Id scope_id) { return By_Scope<reg_type>(get_scope(scope_id)); }
	
	const std::string &get_symbol(Entity_Id id) {
		auto decl = find_entity(id);
		return (*get_scope(decl->scope_id))[id];
	}
	
	virtual ~Catalog() {}
	
protected :
	//Decl_Scope base_scope;
	virtual Registry_Base *registry(Reg_Type reg_type) = 0;
	
private :
	void register_single_decl(Decl_Scope *scope, Decl_AST *decl, std::vector<Decl_Type> &allowed_types);
};

template<Reg_Type reg_type> Entity_Id
//Registry<reg_type>::find_or_create(Decl_Scope *scope, Token *handle, Token *serial_name, Decl_AST *decl) {
Registry<reg_type>::register_decl(Decl_Scope *scope, Decl_AST *decl) {
	
	Entity_Id result_id = invalid_entity_id;
	
	if(!scope)
		fatal_error(Mobius_Error::internal, "find_or_create always requires a scope.");
	
	if(get_reg_type(decl->type) != reg_type) {
		decl->source_loc.print_error_header(Mobius_Error::internal);
		fatal_error("Registering declaration with a mismatching type.");
	}
	
	result_id.reg_type  = reg_type;
	result_id.id        = (s16)registrations.size();     // TODO: Detect overflow?
	registrations.push_back(Entity_Registration<reg_type>());
	auto &registration = registrations[result_id.id];
	
	registration.source_loc  = decl->source_loc;
	registration.decl_type   = decl->type;
	registration.scope_id    = scope->parent_id;
	
	scope->register_decl(decl, result_id);
	
	return result_id;
}

template<Reg_Type reg_type> Entity_Id
Registry<reg_type>::create_internal(const std::string &handle, Decl_Scope *scope, const std::string &name, Decl_Type decl_type) {
	Source_Location internal = {};
	internal.type      = Source_Location::Type::internal;
	
	Entity_Id result_id;
	result_id.reg_type = reg_type;
	result_id.id = (s32)registrations.size();
	registrations.push_back(Entity_Registration<reg_type>());
	auto &registration = registrations[result_id.id];
	
	registration.name = name;
	registration.source_loc = internal;
	registration.has_been_declared = true;
	registration.decl_type = decl_type;
	
	if(scope) {
		scope->add_local(handle, internal, result_id);
		scope->set_serial_name(name, internal, result_id);
	}
	return result_id;
}

#endif // MOBIUS_CATALOG_H

