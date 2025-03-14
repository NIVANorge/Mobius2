
#ifndef MOBIUS_CATALOG_H
#define MOBIUS_CATALOG_H

#include <set>
#include <map>

#include "common_types.h"
#include "ast.h"

struct
Scope_Entity {
	std::string identifier;
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

struct
Decl_Scope {
	struct Entity_Id_Hash {
		int operator()(const Entity_Id &id) const { return 97*(int)id.reg_type + id.id; }
	};
	
	Entity_Id parent_id = invalid_entity_id; // Id of module or library this is the scope of. Invalid if it is the global or model scope.
	
	Scope_Entity *add_local(const std::string &identifier, Source_Location source_loc, Entity_Id id, bool is_locally_declared = true);
	Scope_Entity *register_decl(Decl_AST *ast, Entity_Id id);
	void set_serial_name(const std::string &serial_name, Source_Location source_loc, Entity_Id id);
	void import(const Decl_Scope &other, Source_Location *import_loc = nullptr, bool allow_recursive_import_params = false);
	void check_for_unreferenced_things(Catalog *catalog);
	
	Entity_Id resolve_argument(Reg_Type expected_type, Argument_AST *arg);
	Entity_Id expect(Reg_Type expected_type, const Token *identifier);
	
	Scope_Entity *operator[](const std::string &identifier) {
		auto find = visible_entities.find(identifier);
		if(find != visible_entities.end()) {
			find->second.was_referenced = true;
			return &find->second;
		}
		return nullptr;
	}
	
	const std::string& operator[](Entity_Id id) {
		auto find = identifiers.find(id);
		if(find == identifiers.end())
			fatal_error(Mobius_Error::internal, "Attempt to look up identifier name of an entity id in a scope where it was not declared.");
		return find->second;
	}
	
	Entity_Id deserialize(const std::string &serial_name, Reg_Type expected_type) const;
	
	std::unordered_map<std::string, Scope_Entity>                  visible_entities;
	std::unordered_map<std::string, Serial_Entity>                 serialized_entities;
	std::unordered_map<Entity_Id, std::string, Entity_Id_Hash>     identifiers;
	std::unordered_map<Decl_AST *, Entity_Id>                      by_decl;  // Used to look up the id we put for a decl when we go back and process it.
	std::set<Entity_Id>                                            all_ids;
	
	struct
	By_Type {
		
		By_Type(Decl_Scope *scope, Reg_Type reg_type) : scope(scope), reg_type(reg_type) {
			end_it.scope = scope;
			end_it.reg_type = reg_type;
			end_it.it = scope->all_ids.end();
		}
		
		struct
		Scope_It {
			std::set<Entity_Id>::iterator it;
			Decl_Scope *scope;
			Reg_Type reg_type;
			Scope_It &operator++() {
				do it++; while(it != scope->all_ids.end() && (it->reg_type != reg_type));
				return *this;
			}
			Entity_Id operator*() { return *it; }
			bool operator!=(const Scope_It &other) { return it != other.it; }
		};
		
		Scope_It end_it;
		Decl_Scope *scope;
		Reg_Type reg_type;
		
		Scope_It begin() {
			Scope_It it;
			it.scope = scope;
			it.reg_type = reg_type;
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
	
	By_Type by_type(Reg_Type reg_type) { return By_Type(this, reg_type); }
};

struct
Registration_Base {
	Decl_AST        *decl = nullptr;
	
	Entity_Id       id;
	Decl_Type       decl_type;
	Source_Location source_loc;
	Entity_Id       scope_id = invalid_entity_id;  // The id of the module or library where this entity was declared. (It could have been imported to other scopes also, that is not reflected here)
	std::string     name;
	bool            has_been_processed = false;
	
	virtual void process_declaration(Catalog *catalog) = 0;
};

struct
Registry_Base {
	virtual Entity_Id register_decl(Decl_Scope *scope, Decl_AST *decl) = 0;
	virtual Registration_Base *operator[](Entity_Id id) = 0;
};

template<typename Registration_Type, Reg_Type reg_type> struct
Registry : Registry_Base {

	Entity_Id
	register_decl(Decl_Scope *scope, Decl_AST *decl);
	
	Entity_Id
	create_internal(Decl_Scope *scope, const std::string &identifier, const std::string &name, Decl_Type decl_type);
	
	s64 count() { return (s64)registrations.size(); }
	Entity_Id begin() { return { reg_type, 0 }; }
	Entity_Id end() { return { reg_type, (s16)registrations.size() }; }
	
	Registration_Type *operator[](Entity_Id id);
	
private :
	std::vector<Registration_Type>  registrations;
};

template<typename Registration_Type, Reg_Type reg_type> Registration_Type *
Registry<Registration_Type, reg_type>::operator[](Entity_Id id) {
	if(!is_valid(id))
		fatal_error(Mobius_Error::internal, "Tried to look up an entity using an invalid id.");
	else if(id.reg_type != reg_type)
		fatal_error(Mobius_Error::internal, "Tried to look up an entity using an id of a wrong type. Expected ", name(reg_type), " got ", name(id.reg_type), ".");
	else if(id.id >= registrations.size())
		fatal_error(Mobius_Error::internal, "Tried to look up an entity using a id that was out of bounds.");
	return &registrations[id.id];
}

// It is easier to have index sets directly in the catalog, not a separate type per Data_Set and Mobius_Model. This is because it needs to be accessed by Index_Data for both types.

struct
Index_Set_Registration : Registration_Base {
	
	// NOTE: This can not be an Index_Set_Tuple because the order matters, and the order of declaration of index sets may be different in the Mobius_Model and the Data_Set
	std::vector<Entity_Id>  union_of;
	
	Entity_Id sub_indexed_to = invalid_entity_id;
	
	Entity_Id is_edge_of_connection = invalid_entity_id;
	Entity_Id is_edge_of_node = invalid_entity_id;
	
	void process_declaration(Catalog *catalog);
	void process_main_decl(Catalog *catalog);
};

struct
Catalog {
	
	std::string  model_name;  // A bit weird that it is called that here..
	std::string  doc_string;
	std::string  path;
	
	Decl_AST *main_decl = nullptr;
	
	Registry<Index_Set_Registration, Reg_Type::index_set>   index_sets;
	
	Decl_Scope top_scope;
	
	File_Data_Handler file_handler;
	
	virtual ~Catalog() {}
	
	void
	register_decls_recursive(Decl_Scope *scope, Decl_AST *decl, const std::set<Decl_Type> &allowed_types);
	
	Registration_Base *
	find_entity(Entity_Id id) { return (*registry(id.reg_type))[id]; }
	
	std::string
	serialize(Entity_Id id);
	
	Entity_Id
	deserialize(const std::string &serial_name, Reg_Type expected_type);
	
	const std::string &
	get_symbol(Entity_Id id) {
		/*
		auto decl = find_entity(id);
		auto scope = get_scope(decl->scope_id);
		auto find = scope->identifiers.find(id);
		if(find != scope->identifiers.end())
			return find->second;
		return "";*/
		auto decl = find_entity(id);
		return (*get_scope(decl->scope_id))[id];
	}
	
	virtual Registry_Base *registry(Reg_Type reg_type) = 0;
	virtual Decl_Scope    *get_scope(Entity_Id id)     = 0;
	
protected :
	void register_single_decl(Decl_Scope *scope, Decl_AST *decl, const std::set<Decl_Type> &allowed_types);
};

// Hmm, maybe not the best place to have this, but both Mobius_Model and Data_Set need to know about it.
struct
Model_Options {
	std::map<std::string, std::string> options;
};

inline Entity_Id
map_id(Catalog *from, Catalog *to, Entity_Id id) {
	return to->deserialize(from->serialize(id), id.reg_type);
}

Decl_AST *
read_catalog_ast_from_file(Decl_Type expected_type, File_Data_Handler *handler, String_View file_name, String_View rel_path = {}, std::string *normalized_path_out = nullptr);

void
set_serial_name(Catalog *catalog, Registration_Base *reg, int arg_idx = 0);

template<typename Registration_Type, Reg_Type reg_type> Entity_Id
Registry<Registration_Type, reg_type>::register_decl(Decl_Scope *scope, Decl_AST *decl) {
	
	Entity_Id result_id = invalid_entity_id;
	
	if(!scope)
		fatal_error(Mobius_Error::internal, "find_or_create always requires a scope.");
	
	// TODO: It is very annoying to have the module vs module_template thing work differently in Data_Set and Mobius_Model.
	//   Is there a way to resolve this? Probably easiest to have get_reg_type(Decl_Type::module) == module, and just do something specific for inline module registrations in model_declaration.cpp ?
	if(get_reg_type(decl->type) != reg_type && (decl->type != Decl_Type::module) && (decl->type != Decl_Type::preamble)) {
		decl->source_loc.print_error_header(Mobius_Error::internal);
		fatal_error("Registering declaration with a mismatching type.");
	}
	
	result_id.reg_type  = reg_type;
	result_id.id        = (s16)registrations.size();     // TODO: Detect overflow?
	registrations.emplace_back();
	auto &registration = registrations[result_id.id];
	
	registration.decl        = decl;
	registration.scope_id    = scope->parent_id;
	registration.decl_type   = decl->type;
	registration.source_loc  = decl->source_loc;
	registration.id          = result_id;
	
	scope->register_decl(decl, result_id);
	
	return result_id;
}

template<typename Registration_Type, Reg_Type reg_type> Entity_Id
Registry<Registration_Type, reg_type>::create_internal(Decl_Scope *scope, const std::string &identifier, const std::string &name, Decl_Type decl_type) {
	Source_Location internal = {};
	internal.type      = Source_Location::Type::internal;
	
	Entity_Id result_id;
	result_id.reg_type = reg_type;
	result_id.id = (s32)registrations.size();
	registrations.emplace_back();
	auto &registration = registrations[result_id.id];
	
	registration.name       = name;
	registration.source_loc = internal;
	registration.decl_type  = decl_type;
	registration.id         = result_id;
	registration.has_been_processed = true; // Just so that it doesn't create any errors. We trust internally created ones are correctly processed.
	
	if(scope) {
		registration.scope_id   = scope->parent_id;
		scope->add_local(identifier, internal, result_id);
		if(!name.empty())
			scope->set_serial_name(name, internal, result_id);
	}
	return result_id;
}

#endif // MOBIUS_CATALOG_H

