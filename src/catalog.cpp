
#include <sstream>
#include "catalog.h"

void
set_serial_name(Catalog *catalog, Registration_Base *reg, int arg_idx) {
	auto scope = catalog->get_scope(reg->scope_id);
	auto name = single_arg(reg->decl, arg_idx);
	reg->name = name->string_value;
	scope->set_serial_name(name->string_value, name->source_loc, reg->id);
}

Scope_Entity *
Decl_Scope::add_local(const std::string &identifier, Source_Location source_loc, Entity_Id id, bool is_locally_declared) {
	
	if(!identifier.empty()) {
		if(is_reserved(identifier)) {
			source_loc.print_error_header();
			fatal_error("The identifier '", identifier, "' is reserved.");
		}
		
		auto find = visible_entities.find(identifier);
		if(find != visible_entities.end()) {
			source_loc.print_error_header();
			error_print("The identifier '", identifier, "' was already declared in this scope. See declaration at: ");
			find->second.source_loc.print_error();
			fatal_error();
		}
	}
	Scope_Entity entity;
	entity.identifier = identifier;
	entity.id = id;
	entity.external = !is_locally_declared;
	entity.source_loc = source_loc;
	Scope_Entity *result = nullptr;
	if(!identifier.empty()) {
		visible_entities[identifier] = entity;
		result = &visible_entities[identifier];
	}
	identifiers[id] = identifier;
	if(is_locally_declared)
		all_ids.insert(id);
	
	return result;
}

Scope_Entity *
Decl_Scope::register_decl(Decl_AST *decl, Entity_Id id) {
	
	std::string identifier;
	if(decl->identifier.string_value.count)
		identifier = decl->identifier.string_value;
	
	auto result = add_local(identifier, decl->source_loc, id);
	by_decl[decl] = id;
	
	return result;
}

void
Decl_Scope::set_serial_name(const std::string &serial_name, Source_Location source_loc, Entity_Id id) {
	
	check_allowed_serial_name(serial_name, source_loc);
	
	auto find = serialized_entities.find(serial_name);
	if(find != serialized_entities.end()) {
		source_loc.print_error_header();
		error_print("The name \"", serial_name, "\" has already been used for another entity of type '", name(find->second.id.reg_type), "' in this scope. See the declaration here:");
		find->second.source_loc.print_error();
		mobius_error_exit();
	}
	
	serialized_entities[serial_name] = Serial_Entity {id, source_loc};
}

Entity_Id
Decl_Scope::deserialize(const std::string &serial_name, Reg_Type expected_type) const {
	
	if(serial_name.empty())
		fatal_error(Mobius_Error::internal, "Tried to deserialize an empty string");
	
	auto find = serialized_entities.find(serial_name);
	if(find == serialized_entities.end()) return invalid_entity_id;
	auto result = find->second.id;
	if(expected_type != Reg_Type::unrecognized && result.reg_type != expected_type) return invalid_entity_id;
	return result;
}

void
Decl_Scope::import(const Decl_Scope &other, Source_Location *import_loc, bool allow_recursive_import_params) {
	for(const auto &ent : other.visible_entities) {
		const auto &identifier = ent.first;
		const auto &entity = ent.second;
		// NOTE: In a model, parameters are in the scope of parameter groups, and would not otherwise be imported into inlined modules. This fix is a bit unelegant though.
		// NOTE: no recursive importing unless explicitly specified.
		// TODO: We should instead have two types of import (or 'external'), one that comes from a load() and one that comes from a parent scope.
		
		if(entity.external && !(allow_recursive_import_params && entity.id.reg_type == Reg_Type::parameter))
			continue;
		
		auto find = visible_entities.find(identifier);
		if(find != visible_entities.end()) {
			if(import_loc)
				import_loc->print_error_header();
			else
				begin_error(Mobius_Error::parsing);
			error_print("There is a name conflict with the identifier '", identifier, "'. It was declared separately in the following two locations:\n");
			entity.source_loc.print_error();
			find->second.source_loc.print_error();
			fatal_error();
		}
		Scope_Entity new_entity = entity;
		new_entity.external = true;
		visible_entities[identifier] = new_entity;
		identifiers[entity.id] = identifier;
	}
}

Entity_Id
Decl_Scope::resolve_argument(Reg_Type expected_type, Argument_AST *arg) {
	if(arg->decl) {
		if(expected_type != Reg_Type::unrecognized && get_reg_type(arg->decl->type) != expected_type)
			fatal_error(Mobius_Error::internal, "Mismatched type in type resolution."); // This should not have happened since we should have checked this in pattern matching aready.
		
		auto find = by_decl.find(arg->decl);
		if(find == by_decl.end()) {
			arg->source_loc().print_error_header(Mobius_Error::internal);
			fatal_error("An inline declaration was not properly registered.");
		}
		
		return find->second;
	} else {
		// Note: Format correctness should be checked in pattern matching, giving a proper eror. This is just a failsafe.
		if(arg->chain.size() != 1 || !arg->bracketed_chain.empty())
			fatal_error(Mobius_Error::internal, "Misformatted identifier argument.");
		auto &token = arg->chain[0];
		if(token.type != Token_Type::identifier)
			fatal_error(Mobius_Error::internal, "Somehow got a non-identifier argument when expecting a declaration or identifier type.");
		
		auto reg = (*this)[token.string_value];
		if(!reg) {
			token.print_error_header();
			fatal_error("The identifier '", token.string_value, "' does not refer to an entity that was declared in or imported into this scope.");
		}
		if(expected_type != Reg_Type::unrecognized && reg->id.reg_type != expected_type) {
			token.print_error_header();
			fatal_error("Expected '", token.string_value, "' to be of type '", name(expected_type), "', but it is of type '", name(reg->id.reg_type), "'.");
		}
		return reg->id;
	}
	return invalid_entity_id;
}

Entity_Id
Decl_Scope::expect(Reg_Type expected_type, const Token *identifier) {
	if(identifier->type != Token_Type::identifier) {
		identifier->print_error_header();
		fatal_error("Expected an identifier.");
	}
	auto reg = (*this)[identifier->string_value];
	if(!reg) {
		identifier->print_error_header();
		fatal_error("The identifier '", identifier->string_value, "' has not been declared.");
	}
	if((expected_type != Reg_Type::unrecognized) && reg->id.reg_type != expected_type) {
		identifier->print_error_header();
		fatal_error("Expected '", identifier->string_value, "' to be a '", name(expected_type), "', but it was declared as a '", name(reg->id.reg_type), "'.");
	}
	return reg->id;
}

void
Catalog::register_single_decl(Decl_Scope *scope, Decl_AST *decl, const std::set<Decl_Type> &allowed_types) {
	
	if(allowed_types.find(decl->type) == allowed_types.end()) {
		decl->source_loc.print_error_header();
		fatal_error("Did not expect a declaration of type '", name(decl->type), "' in this context.");
	}
	
	Reg_Type reg_type = get_reg_type(decl->type);
	auto reg = registry(reg_type);
	reg->register_decl(scope, decl);
}

void
Catalog::register_decls_recursive(Decl_Scope *scope, Decl_AST *decl, const std::set<Decl_Type> &allowed_types) {
	
	if(decl->type == Decl_Type::library) {
		fatal_error(Mobius_Error::internal, "Should not get a library to register_decls_recursive.");
	}
	
	register_single_decl(scope, decl, allowed_types);
	
	// NOTE: function arguments have the same syntax as decls, but they aren't actually decls.
	// Maybe we should change the notation for those?
	if(decl->type == Decl_Type::function) return;
	
	for(auto arg : decl->args) {
		if(arg->decl)
			register_decls_recursive(scope, arg->decl, allowed_types);
	}
	for(auto note : decl->notes) {
		for(auto arg : note->args) {
			if(arg->decl)
				register_decls_recursive(scope, arg->decl, allowed_types);
		}
	}
}

std::string
Catalog::serialize(Entity_Id id) {
	if(!is_valid(id))
		fatal_error(Mobius_Error::api_usage, "An invalid entity id was passed to serialize().");
	auto entity = find_entity(id);
	if(is_valid(entity->scope_id)) {
		std::stringstream ss;
		auto scope = find_entity(entity->scope_id);
		if(is_valid(scope->scope_id)) {
			auto superscope = find_entity(scope->scope_id);
			ss << superscope->name << '\\';
		}
		ss << scope->name << '\\' << entity->name;
		return ss.str();
	}
	return entity->name;
}

Entity_Id
Catalog::deserialize(const std::string &serial_name, Reg_Type expected_type) {

	auto vec = split(serial_name, '\\');  // Hmm, this is maybe a bit inefficient, but probably not a problem.
	
	Decl_Scope *scope = &top_scope;
	for(int idx = 0; idx < (int)vec.size()-1; ++idx) {
		auto scope_id = scope->deserialize(vec[idx], Reg_Type::unrecognized);
		if(!is_valid(scope_id)) return invalid_entity_id;
		scope = get_scope(scope_id);
	}
	return scope->deserialize(vec.back(), expected_type);
}

Decl_AST *
read_catalog_ast_from_file(Decl_Type expected_type, File_Data_Handler *handler, String_View file_name, String_View rel_path, std::string *normalized_path_out) {
	String_View model_data = handler->load_file(file_name, {}, rel_path, normalized_path_out);
	
	Token_Stream stream(file_name, model_data);
	
	// Hmm, not that nice to hard code this at this level maybe.
	if(expected_type == Decl_Type::data_set)
		stream.allow_date_time_tokens = true;
	
	Decl_AST *decl = parse_decl(&stream);
	if(decl->type != expected_type || stream.peek_token().type != Token_Type::eof) {
		decl->source_loc.print_error_header();
		fatal_error("Main '", name(expected_type), "' files should only have a single declaration in the top scope.");
	}
	
	return decl;
}


