
#include "catalog.h"

Scope_Entity *
Decl_Scope::add_local(const std::string &handle, Source_Location source_loc, Entity_Id id) {
	
	if(!handle.empty()) {
		if(is_reserved(handle))
			fatal_error(Mobius_Error::internal, "Somehow we got a reserved keyword '", handle, "' at a stage after it should have been ruled out.");
		
		auto find = visible_entities.find(handle);
		if(find != visible_entities.end()) {
			source_loc.print_error_header();
			error_print("The handle '", handle, "' was already declared in this scope. See declaration at: ");
			find->second.source_loc.print_error();
			fatal_error();
		}
	}
	Scope_Entity entity;
	entity.handle = handle;
	entity.id = id;
	entity.external = false;
	entity.source_loc = source_loc;
	Scope_Entity *result = nullptr;
	if(!handle.empty()) {
		visible_entities[handle] = entity;
		result = &visible_entities[handle];
	}
	handles[id] = handle;
	all_ids.insert(id);
	
	return result;
}

Scope_Entity *
Decl_Scope::register_decl(Decl_AST *ast, Entity_Id id) {
	
	std::string handle;
	if(ast->handle_name.string_value.count)
		handle = ast->handle_name.string_value;
	
	auto result = add_local(handle, ast->source_loc, id);
	by_decl[ast] = id;
	
	return result;
}

void
Decl_Scope::set_serial_name(const std::string &serial_name, Source_Location source_loc, Entity_Id id) {
	
	auto find = serialized_entities.find(serial_name);
	if(find != serialized_entities.end()) {
		source_loc.print_error_header();
		error_print("The name \"", serial_name, "\" has already been used for another entity in this scope of type '", name(find->second.id.reg_type), "'. See the declaration here:");
		find->second.source_loc.print_error();
		mobius_error_exit();
	}
	
	serialized_entities[serial_name] = Serial_Entity {id, source_loc};
}

Entity_Id
Decl_Scope::deserialize(const std::string &serial_name, Reg_Type expected_type) const {
	auto find = serialized_entities.find(serial_name);
	if(find == serialized_entities.end()) return invalid_entity_id;
	auto result = find->second.id;
	if(expected_type != Reg_Type::unrecognized && result.reg_type != expected_type) return invalid_entity_id;
	return result;
}

void
Decl_Scope::import(const Decl_Scope &other, Source_Location *import_loc, bool allow_recursive_import_params) {
	for(const auto &ent : other.visible_entities) {
		const auto &handle = ent.first;
		const auto &entity = ent.second;
		// NOTE: In a model, the parameters are in the scope of parameter groups, and would not otherwise be imported into inlined modules. This fix is a bit unelegant though.
		if(!entity.external || (allow_recursive_import_params && entity.id.reg_type == Reg_Type::parameter)) { // NOTE: no recursive importing
			auto find = visible_entities.find(handle);
			if(find != visible_entities.end()) {
				if(import_loc)
					import_loc->print_error_header();
				else
					begin_error(Mobius_Error::parsing);
				error_print("There is a name conflict with the handle '", handle, "'. It was declared separately in the following two locations:\n");
				entity.source_loc.print_error();
				find->second.source_loc.print_error();
				fatal_error();
			}
			Scope_Entity new_entity = entity;
			new_entity.external = true;
			visible_entities[handle] = new_entity;
			handles[entity.id] = handle;
		}
	}
}

Entity_Id
Decl_Scope::resolve_argument(Reg_Type expected_type, Argument_AST *arg) {
	if(arg->decl) {
		if(get_reg_type(arg->decl->type) != expected_type)
			fatal_error(Mobius_Error::internal, "Mismatched type in type resolution."); // This should not have happened since we should have checked this in pattern matching aready.
		
		auto find = by_decl.find(arg->decl);
		if(find == by_decl.end())
			fatal_error(Mobius_Error::internal, "An inline declaration was not properly registered.");
		
		return find->second;
	} else {
		// TODO: Ensure that format correctness is checked in pattern matching. (e.g. chain size, no bracket, is identifier).
		if(arg->chain.size() != 1 || !arg->bracketed_chain.empty())
			fatal_error(Mobius_Error::internal, "Misformatted handle argument.");
		auto &token = arg->chain[0];
		if(token.type != Token_Type::identifier)
			fatal_error(Mobius_Error::internal, "Somehow got a non-identifier argument when expecting a declaration or handle type.");
		
		auto reg = (*this)[token.string_value];
		if(!reg) {
			token.print_error_header();
			fatal_error("The handle '", token.string_value, "' does not refer to an entity that was declared in this scope.");
		}
		return reg->id;
	}
	return invalid_entity_id;
}

void
Catalog::register_single_decl(Decl_Scope *scope, Decl_AST *decl, const std::vector<Decl_Type> &allowed_types) {
	
	if(std::find(allowed_types.begin(), allowed_types.end(), decl->type) == allowed_types.end()) {
		decl->source_loc.print_error_header();
		fatal_error("Did not expect a declaration of type '", name(decl->type), "' in this context.");
	}
	
	Reg_Type reg_type = get_reg_type(decl->type);
	auto reg = registry(reg_type);
	reg->register_decl(scope, decl);
}

void
Catalog::register_decls_recursive(Decl_Scope *scope, Decl_AST *decl, const std::vector<Decl_Type> &allowed_types) {
	
	register_single_decl(scope, decl, allowed_types);
	
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

Decl_Scope *
Catalog::get_scope(Entity_Id id) {
	if(!is_valid(id))
		return &top_scope;
	else if(id.reg_type == Reg_Type::library)
		return &libraries[id]->scope;
	else if(id.reg_type == Reg_Type::module)
		return &modules[id]->scope;
	else if(id.reg_type == Reg_Type::par_group)
		return &par_groups[id]->scope;
	fatal_error(Mobius_Error::internal, "Tried to look up the scope belonging to an id that is not a library or module.");
	return nullptr;
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
	for(int idx = 0; idx < vec.size()-1; ++idx) {
		auto scope_id = scope->deserialize(vec[idx], Reg_Type::unrecognized);
		if(!is_valid(scope_id)) return invalid_entity_id;
		scope = get_scope(scope_id);
	}
	return scope->deserialize(vec.back(), expected_type);
}


