
#include <sys/stat.h>
#include <algorithm>
#include <sstream>
#include "model_declaration.h"

Scope_Entity *
Decl_Scope::add_local(const std::string &handle, Source_Location source_loc, Entity_Id id) {
	
	if(is_reserved(handle))
		fatal_error(Mobius_Error::internal, "Somehow we got a reserved keyword '", handle, "' on a stage after it should have been ruled out.");
	
	if(!handle.empty()) {
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

void
Decl_Scope::check_for_missing_decls(Mobius_Model *model) {
	for(auto &ent : visible_entities) {
		auto &entity = ent.second;
		if(entity.external) continue;
		auto reg = model->find_entity(entity.id);
		if(!reg->has_been_declared) {
			entity.source_loc.print_error_header();
			fatal_error("The handle name '", entity.handle, "' was referenced, but never declared or loaded in this scope.");
		}
	}
}

void
Decl_Scope::check_for_unreferenced_things(Mobius_Model *model) {
	std::unordered_map<Entity_Id, bool, Entity_Id_Hash> lib_was_used;
	for(auto &pair : visible_entities) {
		auto &reg = pair.second;
		
		if(reg.is_load_arg && !reg.was_referenced) {
			log_print("Warning: In ");
			reg.source_loc.print_log_header();
			log_print("The module argument '", reg.handle, "' was never referenced.\n");
		}
		
		if(reg.external) {
			auto entity = model->find_entity(reg.id);
			if(entity->scope_id.reg_type == Reg_Type::library) {
				if(reg.was_referenced)
					lib_was_used[entity->scope_id] = true;
				else {
					auto find = lib_was_used.find(entity->scope_id);
					if(find == lib_was_used.end())
						lib_was_used[entity->scope_id] = false;
				}
			}
		}
	}
	for(auto &pair : lib_was_used) {
		if(!pair.second) {
			// TODO: How would we find the load source location of the library in this module? That is probably not stored anywhere right now.
			// TODO:  we could put that in the Scope_Entity source_loc (?)
			std::string scope_type;
			if(parent_id.reg_type == Reg_Type::module)
				scope_type = "module";
			else if(parent_id.reg_type == Reg_Type::library)
				scope_type = "library";
			else
				scope_type = "model";
			log_print("Warning: The ", scope_type, " \"", model->find_entity(parent_id)->name, "\" loads the library \"", model->libraries[pair.first]->name, "\", but does not use any of it.\n");
		}
	}
	// TODO: Could also check for unreferenced parameters, and maybe some other types of entities (solver, connection..) (but not all entities in general).
}

// TODO: Should change scope to the first argument.
template<Reg_Type reg_type> Entity_Id
Registry<reg_type>::find_or_create(Decl_Scope *scope, Token *handle, Token *serial_name, Decl_AST *decl) {
	
	Entity_Id result_id = invalid_entity_id;
	
	if(!scope)
		fatal_error(Mobius_Error::internal, "find_or_create always requires a scope.");
	
	bool found_in_scope = false;
	if(is_valid(handle)) {
		if(handle->type != Token_Type::identifier)
			fatal_error(Mobius_Error::internal, "Passed a non-identifier as a handle to find_or_create().");
		
		std::string hh = handle->string_value;
		auto entity = (*scope)[hh];
		if(entity) {
			found_in_scope = true;
			result_id = entity->id;
			if(result_id.reg_type != reg_type) {
				handle->print_error_header();
				error_print("Expected '", handle->string_value, "' to be a ", name(reg_type), " in this context. It was previously used here: ");
				entity->source_loc.print_error();
				fatal_error("as a ", name(result_id.reg_type), ".");
			}
			if(decl && registrations[result_id.id].has_been_declared) {
				decl->source_loc.print_error_header();
				error_print("Re-declaration of symbol '", handle->string_value, "'. It was already declared here: ");
				entity->source_loc.print_error();
				fatal_error();
			}
		}
	}
	
	// It was not previously referenced. Create a new entry for it.
	if(!is_valid(result_id)) {
		result_id.reg_type  = reg_type;
		result_id.id        = (s16)registrations.size();     // TODO: Detect overflow?
		registrations.push_back(Entity_Registration<reg_type>());
		auto &registration = registrations[result_id.id];
		registration.has_been_declared = false;
		if(is_valid(handle))
			registration.source_loc = handle->source_loc;
	}
	
	auto &registration = registrations[result_id.id];
	
	if(decl) {
		if(get_reg_type(decl->type) != reg_type) {
			decl->source_loc.print_error_header(Mobius_Error::internal);
			fatal_error("Registering declaration with a mismatching type.");
		}
		
		registration.source_loc        = decl->source_loc;
		registration.has_been_declared = true;
		registration.decl_type         = decl->type;
		registration.scope_id          = scope->parent_id;
	}
	
	if(is_valid(serial_name)) {
		registration.name = serial_name->string_value;
			
		check_allowed_serial_name(serial_name->string_value, serial_name->source_loc);
		
		scope->set_serial_name(registration.name, serial_name->source_loc, result_id);
	}
	
	if(is_valid(handle)) {
		std::string hh = handle->string_value;
		if(!found_in_scope)
			scope->add_local(hh, registration.source_loc, result_id);
		else if(decl) // Update source location to be the declaration location.
			(*scope)[hh]->source_loc = registration.source_loc;
	} else
		scope->add_local("", registration.source_loc, result_id);     // This is necessary so that it gets put into scope->all_ids.
	
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
	
	//name_to_id[name] = result_id;
	
	if(scope) {
		scope->add_local(handle, internal, result_id);
		scope->set_serial_name(name, internal, result_id);
	}
	return result_id;
}

template<Reg_Type reg_type> Entity_Id
Registry<reg_type>::standard_declaration(Decl_Scope *scope, Decl_AST *decl) {
	Token *name = single_arg(decl, 0);
	return find_or_create(scope, &decl->handle_name, name, decl);
}

Registry_Base *
Mobius_Model::registry(Reg_Type reg_type) {
	switch(reg_type) {
		case Reg_Type::par_group :                return &par_groups;
		case Reg_Type::parameter :                return &parameters;
		case Reg_Type::unit :                     return &units;
		case Reg_Type::module_template :          return &module_templates;
		case Reg_Type::library :                  return &libraries;
		case Reg_Type::component :                return &components;
		case Reg_Type::var :                      return &vars;
		case Reg_Type::flux :                     return &fluxes;
		case Reg_Type::function :                 return &functions;
		case Reg_Type::index_set :                return &index_sets;
		case Reg_Type::solver :                   return &solvers;
		//case Reg_Type::solve :                    return &solves;
		case Reg_Type::constant :                 return &constants;
		case Reg_Type::connection :               return &connections;
		case Reg_Type::module :                   return &modules;
		case Reg_Type::loc :                      return &locs;
	}
	
	fatal_error(Mobius_Error::internal, "Unhandled entity type ", name(reg_type), " in registry().");
	
	return nullptr;
}

Decl_Scope *
Mobius_Model::get_scope(Entity_Id id) {
	if(!is_valid(id))
		return &model_decl_scope;
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
Mobius_Model::serialize(Entity_Id id) {
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
Mobius_Model::deserialize(const std::string &serial_name, Reg_Type expected_type) {

	auto vec = split(serial_name, '\\');  // Hmm, this is maybe a bit inefficient, but probably not a problem.
	
	Decl_Scope *scope = &model_decl_scope;
	for(int idx = 0; idx < vec.size()-1; ++idx) {
		auto scope_id = scope->deserialize(vec[idx], Reg_Type::unrecognized);
		if(!is_valid(scope_id)) return invalid_entity_id;
		scope = get_scope(scope_id);
	}
	return scope->deserialize(vec.back(), expected_type);
}

template<Reg_Type expected_type> Entity_Id
resolve_argument(Mobius_Model *model, Decl_Scope *scope, Decl_Base_AST *decl, int which, int max_sub_chain_size=1) {
	// We could do more error checking here, but it should really only be called after calling match_declaration...
	
	Argument_AST *arg = decl->args[which];
	if(arg->decl) {
		if(get_reg_type(arg->decl->type) != expected_type)
			fatal_error(Mobius_Error::internal, "Mismatched type in type resolution."); // This should not have happened since we should have checked this aready.
		
		return process_declaration<expected_type>(model, scope, arg->decl);
	} else {
		if(max_sub_chain_size > 0 && arg->chain.size() > max_sub_chain_size) {
			arg->chain[0].print_error_header();
			fatal_error("Misformatted argument.");
		}
		if(!arg->bracketed_chain.empty()) {
			arg->bracketed_chain[0].print_error_header();
			fatal_error("Did not expect a bracketed location.");
		}
		return model->registry(expected_type)->find_or_create(scope, &arg->chain[0]);
	}
	return invalid_entity_id;
}


Decl_AST *
read_model_ast_from_file(File_Data_Handler *handler, String_View file_name, String_View rel_path = {}, std::string *normalized_path_out = nullptr) {
	String_View model_data = handler->load_file(file_name, {}, rel_path, normalized_path_out);
	
	Token_Stream stream(file_name, model_data);
	Decl_AST *decl = parse_decl(&stream);
	if(decl->type != Decl_Type::model || stream.peek_token().type != Token_Type::eof) {
		decl->source_loc.print_error_header();
		fatal_error("Model files should only have a single model declaration in the top scope.");
	}
	
	match_declaration(decl, {{Token_Type::quoted_string}}, false);
	
	return decl;
}

void
register_intrinsics(Mobius_Model *model) {
	auto global = &model->global_scope;
	
	#define MAKE_INTRINSIC1(name, emul, llvm, ret_type, type1) \
		{ \
			auto fun = model->functions.create_internal(#name, global, #name, Decl_Type::function); \
			model->functions[fun]->fun_type = Function_Type::intrinsic; \
			model->functions[fun]->args = {"a"}; \
		}
	#define MAKE_INTRINSIC2(name, emul, ret_type, type1, type2) \
		{ \
			auto fun = model->functions.create_internal(#name, global, #name, Decl_Type::function); \
			model->functions[fun]->fun_type = Function_Type::intrinsic; \
			model->functions[fun]->args = {"a", "b"}; \
		}
	#include "intrinsics.incl"
	#undef MAKE_INTRINSIC1
	#undef MAKE_INTRINSIC2
	
	auto test_fun = model->functions.create_internal("_test_fun_", global, "_test_fun_", Decl_Type::function);
	model->functions[test_fun]->fun_type = Function_Type::linked;
	model->functions[test_fun]->args = {"a"};
	
	auto dimless_id = model->units.create_internal("na", nullptr, "dimensionless", Decl_Type::unit); // NOTE: this is not exported to the scope. Just have it as a unit for pi.
	auto pi_id = model->constants.create_internal("pi", global, "π", Decl_Type::constant);
	model->constants[pi_id]->value = 3.14159265358979323846;
	model->constants[pi_id]->unit = dimless_id;
	
	auto mod_scope = &model->model_decl_scope;
	
	auto system_id = model->par_groups.create_internal("system", mod_scope, "System", Decl_Type::par_group);
	auto group_scope = model->get_scope(system_id);
	auto start_id  = model->parameters.create_internal("start_date", group_scope, "Start date", Decl_Type::par_datetime);
	auto end_id    = model->parameters.create_internal("end_date", group_scope, "End date", Decl_Type::par_datetime);
	mod_scope->import(*group_scope);
	
	auto system = model->par_groups[system_id];
	auto start  = model->parameters[start_id];
	auto end    = model->parameters[end_id];
	
	Date_Time default_start(1970, 1, 1);
	Date_Time default_end(1970, 1, 16);
	Date_Time min_date(1000, 1, 1);
	Date_Time max_date(3000, 1, 1);
	
	start->par_group = system_id;
	start->default_val.val_datetime = default_start;
	start->description = "The start date is inclusive";
	start->min_val.val_datetime = min_date;
	start->max_val.val_datetime = max_date;
	
	end->par_group = system_id;
	end->default_val.val_datetime = default_end;
	end->description   = "The end date is inclusive";
	end->min_val.val_datetime = min_date;
	end->max_val.val_datetime = max_date;
	
	//model->solvers.create("discrete", mod_scope, "Discrete", Decl_Type::solver);
}

Entity_Id
load_top_decl_from_file(Mobius_Model *model, Source_Location from, String_View path, String_View relative_to, const std::string &decl_name, Decl_Type type) {
	
	// TODO: Should really check if the model-relative path exists before changing the relative path.
	std::string models_path = model->mobius_base_path + "models/"; // Note: since relative_to is a string view, this one must exist in the outer scope.
	if(!model->mobius_base_path.empty()) {
		//warning_print("*** Base path is ", model->mobius_base_path, "\n");
		//warning_print("Bottom directory of \"", path, "\" is stdlib: ", bottom_directory_is(path, "stdlib"), "\n");
		
		if(type == Decl_Type::module && bottom_directory_is(path, "modules"))
			relative_to = models_path;
		else if(type == Decl_Type::library && bottom_directory_is(path, "stdlib"))
			relative_to = model->mobius_base_path;
	}
	
	std::string normalized_path;
	String_View file_data = model->file_handler.load_file(path, from, relative_to, &normalized_path);
	
	//warning_print("Try to load ", decl_name, " from ", normalized_path, "\n");
	
	bool already_parsed_file = false;
	Entity_Id result = invalid_entity_id;
	auto find_file = model->parsed_decls.find(normalized_path);
	//warning_print("Look for file ", normalized_path, "\n");
	if(find_file != model->parsed_decls.end()) {
		already_parsed_file = true;
		//warning_print("Already parsed file\n");
		auto find_decl = find_file->second.find(decl_name);
		if(find_decl != find_file->second.end())
			result = find_decl->second;
	}

	if(!already_parsed_file) {
		Token_Stream stream(path, file_data);
			
		while(true) {
			if(stream.peek_token().type == Token_Type::eof) break;
			Decl_AST *decl = parse_decl(&stream);
			
			// TODO: Could we find a way to not have to do these matches both here and inside process_module_declaration ?
			if(decl->type == Decl_Type::module) {
				match_declaration(decl,
					{
						{Token_Type::quoted_string, Decl_Type::version},
						{Token_Type::quoted_string, Decl_Type::version, {true}}
					}, false);
			} else if (decl->type == Decl_Type::library) {
				match_declaration(decl, {{Token_Type::quoted_string}}, false);   //TODO: Should just have versions here too maybe..
			} else {
				decl->source_loc.print_error_header();
				fatal_error("Module files should only have modules or libraries in the top scope. Encountered a ", name(decl->type), ".");
			}
			
			std::string found_name = single_arg(decl, 0)->string_value;
			Entity_Id id = invalid_entity_id;
			
			if(decl->type == Decl_Type::library) {
				id = model->libraries.find_or_create(&model->model_decl_scope, nullptr, nullptr, decl);
				auto lib = model->libraries[id];
				lib->has_been_processed = false;
				lib->decl = decl;
				lib->scope.parent_id = id;
				lib->normalized_path = normalized_path;
				lib->name = found_name;
			} else if (decl->type == Decl_Type::module) {
				id = model->module_templates.find_or_create(&model->model_decl_scope, nullptr, nullptr, decl);
				auto mod = model->module_templates[id];
				//mod->has_been_processed = false;
				mod->decl = decl;
				//mod->scope.parent_id = id;
				mod->normalized_path = normalized_path;
			}
			model->parsed_decls[normalized_path][found_name] = id;
			if(decl_name == found_name) {
				result = id;
				if(decl->type != type) {
					decl->source_loc.print_error_header();
					fatal_error(Mobius_Error::parsing, "The declaration '", decl_name, "' is of type ", name(decl->type), ", but was loaded as a ", name(type), ".");
				}
			}
		}
	}
	
	if(!is_valid(result))
		fatal_error(Mobius_Error::parsing, "Could not find the ", name(type), " '", decl_name, "' in the file ", path, " .");
	
	return result;
}

void
process_bracket(Mobius_Model *model, Decl_Scope *scope, std::vector<Token> &bracket, Single_Restriction &res, bool allow_bracket) {
	if(bracket.size() == 2 && allow_bracket) {
		
		res.connection_id = model->connections.find_or_create(scope, &bracket[0]);
		auto type = bracket[1].string_value;
		if(type == "top")
			res.restriction = Var_Loc_Restriction::top;
		else if(type == "bottom")
			res.restriction = Var_Loc_Restriction::bottom;
		else if(type == "specific")
			res.restriction = Var_Loc_Restriction::specific;
		else if(type == "below")
			res.restriction = Var_Loc_Restriction::below;
		else {
			bracket[1].print_error_header();
			fatal_error("The keyword '", type, "' is not allowed as a location restriction in this context.");
		}
	} else if(!bracket.empty()) {
		bracket[0].print_error_header();
		fatal_error("This bracketed restriction is either invalidly placed or invalidly formatted.");
	}
}

// TODO: A lot of this function could be merged with similar functionality in function_tree.cpp
void
process_location_argument(Mobius_Model *model, Decl_Scope *scope, Argument_AST *arg, Var_Location *location,
	bool allow_unspecified, bool allow_restriction, Entity_Id *par_id) {
	
	Specific_Var_Location *specific_loc = nullptr;
	if(allow_restriction)
		specific_loc = static_cast<Specific_Var_Location *>(location);
	
	if(arg->decl) {   // Hmm, this disallows inlined loc() declarations, but those are nonsensical anyway.
		arg->decl->source_loc.print_error_header();
		fatal_error("Expected a single identifier or a .-separated chain of identifiers.");
	}
	auto &symbol     = arg->chain;
	auto &bracketed  = arg->bracketed_chain;
	auto &bracketed2 = arg->secondary_bracketed;
	
	int count = symbol.size();
	bool is_out = false;
	
	if(count == 1) {
		Token *token = &symbol[0];
		if(token->string_value == "out") {
			if(!allow_unspecified) {
				token->print_error_header();
				fatal_error("An 'out' is not allowed in this context.");
			}
			location->type = Var_Location::Type::out;
			is_out = true;
		} else {
			auto reg = (*scope)[token->string_value];
			if(reg) {
				if(reg->id.reg_type == Reg_Type::connection) {
					location->type = Var_Location::Type::connection;
					specific_loc->connection_id = reg->id;
					specific_loc->restriction   = Var_Loc_Restriction::below;  // This means that the target of the flux is the 'next' index along the connection.
				} else if (reg->id.reg_type == Reg_Type::loc) {
					auto loc = model->locs[reg->id];
					
					if(allow_restriction)
						*specific_loc = loc->loc;
					else {
						*location = loc->loc;
						if(loc->loc.restriction != Var_Loc_Restriction::none) {
							loc->source_loc.print_error_header();
							error_print("This declared location has a bracketed restriction, but that is not allowed when it is used in the following context :\n");
							token->source_loc.print_error();
							mobius_error_exit();
						}
					}
					if(is_valid(loc->par_id)) {
						if(par_id)
							*par_id = loc->par_id;
						else {
							loc->source_loc.print_error_header();
							error_print("This location is declared as a parameter, but that is not allowed when it is used in the following context :\n");
							token->source_loc.print_error();
							mobius_error_exit();
						}
					}
				} else if (par_id && reg->id.reg_type == Reg_Type::parameter)
					*par_id = reg->id;
				else {
					token->print_error_header();
					fatal_error("The entity '", token->string_value, "' has not been declared in this scope, or is of a type that is not recognized in a location argument.");
				}
			}
		}
	} else if (count >= 2 && count <= max_var_loc_components) {
		location->type     = Var_Location::Type::located;
		for(int idx = 0; idx < count; ++idx)
			location->components[idx] = model->components.find_or_create(scope, &symbol[idx]);
		location->n_components        = count;
	} else {
		symbol[0].print_error_header();
		fatal_error("Too many components in a variable location (max ", max_var_loc_components, " allowed).");
	}
	
	if(allow_restriction) {
		bool allow_bracket = !is_out &&	(count >= 2 || (par_id && is_valid(*par_id))); // We can only have a bracket on something that is either a full var location or a parameter.
		process_bracket(model, scope, bracketed, *specific_loc, allow_bracket);
		process_bracket(model, scope, bracketed2, specific_loc->restriction2, allow_bracket);
		
		if(!bracketed2.empty() && specific_loc->restriction != Var_Loc_Restriction::below) {
			bracketed2[0].print_error_header();
			fatal_error("For now, if there is a second argument in the location bracket, the first one must be 'below'.");
		}
		
		if(specific_loc->restriction == Var_Loc_Restriction::below)
			specific_loc->type = Var_Location::Type::connection;
	} else if (!bracketed.empty()) {
		bracketed[0].print_error_header();
		fatal_error("A bracketed restriction on the location argument is not allowed in this context.");
	}
}

template<Reg_Type reg_type> Entity_Id
process_declaration(Mobius_Model *model, Decl_Scope *scope, Decl_AST *decl); //NOTE: this will be template specialized below.

template<> Entity_Id
process_declaration<Reg_Type::unit>(Mobius_Model *model, Decl_Scope *scope, Decl_AST *decl) {
	auto id   = model->units.find_or_create(scope, &decl->handle_name, nullptr, decl);
	auto unit = model->units[id];
	
	// NOTE: we could de-duplicate based on the standard form, but it is maybe not worth it.
	unit->data.set_data(decl);
	
	return id;
}

template<> Entity_Id
process_declaration<Reg_Type::component>(Mobius_Model *model, Decl_Scope *scope, Decl_AST *decl) {
	// NOTE: the Decl_Type is either compartment, property or quantity.
	
	//TODO: If we want to allow units on this declaration directly, we have to check for mismatches between decls in different modules.
	// For now it is safer to just have it on the "has", but we could go over this later and see if we could make it work.
	int which = match_declaration(decl,
		{
			{Token_Type::quoted_string},
			//{Token_Type::quoted_string, Decl_Type::unit},
		});
		
	auto id          = model->components.standard_declaration(scope, decl);
	auto component   = model->components[id];
	
	if(decl->body) {
		if(decl->type != Decl_Type::property) {
			decl->body->opens_at.print_error_header();
			fatal_error("Only properties can have default code, not quantities or compartments.");
		}
		// TODO : have to guard against clashes between different modules here! But that should be done in model_composition
		auto fun = static_cast<Function_Body_AST *>(decl->body);
		component->default_code = fun->block;
	}
	/*
	if(which == 1)
		component->unit = resolve_argument<Reg_Type::unit>(module, decl, 1);
	else
		component->unit = invalid_entity_id;
	*/
	return id;
}

template<> Entity_Id
process_declaration<Reg_Type::parameter>(Mobius_Model *model, Decl_Scope *scope, Decl_AST *decl) {
	Token_Type token_type = get_token_type(decl->type);
	
	int which;
	if(decl->type == Decl_Type::par_real || decl->type == Decl_Type::par_int) {
		which = match_declaration(decl,
		{
			{Token_Type::quoted_string, Decl_Type::unit, token_type},                                                      // 0
			{Token_Type::quoted_string, Decl_Type::unit, token_type, Token_Type::quoted_string},                           // 1
			{Token_Type::quoted_string, Decl_Type::unit, token_type, token_type, token_type},                              // 2
			{Token_Type::quoted_string, Decl_Type::unit, token_type, token_type, token_type, Token_Type::quoted_string},   // 3
		});
	} else if (decl->type == Decl_Type::par_bool || decl->type == Decl_Type::par_enum) {            // note: min, max values for boolean parameters are redundant.
		which = match_declaration(decl,
		{
			{Token_Type::quoted_string, token_type},                                                      // 0
			{Token_Type::quoted_string, token_type, Token_Type::quoted_string},                           // 1
		}, true, -1);
	} else
		fatal_error(Mobius_Error::internal, "Got an unrecognized type in parameter declaration processing.");
	
	auto id        = model->parameters.standard_declaration(scope, decl);
	auto parameter = model->parameters[id];
	
	int mt0 = 2;
	if(token_type == Token_Type::boolean) mt0--;
	if(decl->type != Decl_Type::par_enum)
		parameter->default_val             = get_parameter_value(single_arg(decl, mt0), token_type);
	
	if(decl->type == Decl_Type::par_real) {
		parameter->unit                    = resolve_argument<Reg_Type::unit>(model, scope, decl, 1);
		if(which == 2 || which == 3) {
			parameter->min_val             = get_parameter_value(single_arg(decl, 3), Token_Type::real);
			parameter->max_val             = get_parameter_value(single_arg(decl, 4), Token_Type::real);
		} else {
			parameter->min_val.val_real    = -std::numeric_limits<double>::infinity();
			parameter->max_val.val_real    =  std::numeric_limits<double>::infinity();
		}
	} else if (decl->type == Decl_Type::par_int) {
		parameter->unit                    = resolve_argument<Reg_Type::unit>(model, scope, decl, 1);
		if(which == 2 || which == 3) {
			parameter->min_val             = get_parameter_value(single_arg(decl, 3), Token_Type::integer);
			parameter->max_val             = get_parameter_value(single_arg(decl, 4), Token_Type::integer);
		} else {
			parameter->min_val.val_integer = std::numeric_limits<s64>::lowest();
			parameter->max_val.val_integer = std::numeric_limits<s64>::max();
		}
	} else if (decl->type == Decl_Type::par_bool) {
		parameter->min_val.val_boolean = false;
		parameter->max_val.val_boolean = true;
	}
	
	int mt1 = 3;
	if(decl->type == Decl_Type::par_bool || decl->type == Decl_Type::par_enum) mt1--;
	if(which == 1)
		parameter->description         = single_arg(decl, mt1)->string_value;
	else if(which == 3)
		parameter->description         = single_arg(decl, 5)->string_value;
	
	if(decl->type == Decl_Type::par_enum) {
		auto body = static_cast<Function_Body_AST *>(decl->body);   // Re-purposing function body for a simple list... TODO: We should maybe have a separate body type for that
		for(auto expr : body->block->exprs) {
			if(expr->type != Math_Expr_Type::identifier) {
				expr->source_loc.print_error_header();
				fatal_error("Expected a list of identifiers only.");
			}
			auto ident = static_cast<Identifier_Chain_AST *>(expr);
			if(ident->chain.size() != 1) {
				expr->source_loc.print_error_header();
				fatal_error("Expected a list of identifiers only.");
			}
			parameter->enum_values.push_back(ident->chain[0].string_value);
		}
		std::string default_val_name = single_arg(decl, 1)->string_value;
		s64 default_val = enum_int_value(parameter, default_val_name);
		if(default_val < 0) {
			single_arg(decl, 1)->print_error_header();
			fatal_error("The given default value '", default_val_name, "' does not appear on the list of possible values for this enum parameter.");
		}
		parameter->default_val.val_integer = default_val;
		parameter->min_val.val_integer = 0;
		parameter->max_val.val_integer = (s64)parameter->enum_values.size() - 1;
	}
	
	return id;
}

template<> Entity_Id
process_declaration<Reg_Type::par_group>(Mobius_Model *model, Decl_Scope *scope, Decl_AST *decl) {
	int which = match_declaration(decl,
	{
		{Token_Type::quoted_string},
		{Token_Type::quoted_string, {Decl_Type::compartment, true}},   // Could eventually make the last a vararg?
	}, false, -1);
	
	auto id        = model->par_groups.standard_declaration(scope, decl);
	auto par_group = model->par_groups[id];
	
	par_group->scope.parent_id = id;
	
	if(which >= 1) {
		for(int idx = 1; idx < decl->args.size(); ++idx)
			par_group->components.push_back(resolve_argument<Reg_Type::component>(model, scope, decl, idx));
	}
	
	auto body = static_cast<Decl_Body_AST *>(decl->body);

	for(Decl_AST *child : body->child_decls) {
		if(child->type == Decl_Type::par_real || child->type == Decl_Type::par_int || child->type == Decl_Type::par_bool || child->type == Decl_Type::par_enum) {
			auto par_id = process_declaration<Reg_Type::parameter>(model, &par_group->scope, child);
			//par_group->parameters.push_back(par_id);
			model->parameters[par_id]->par_group = id;
		} else {
			child->source_loc.print_error_header();
			fatal_error("Did not expect a declaration of type '", name(child->type), "' inside a par_group declaration.");
		}
	}
	
	// All the parameters are visible in the module or model scope.
	scope->import(par_group->scope, &decl->source_loc);
	
	//TODO: process doc string(s) in the par group?
	
	return id;
}

template<> Entity_Id
process_declaration<Reg_Type::function>(Mobius_Model *model, Decl_Scope *scope, Decl_AST *decl) {
	match_declaration(decl,
		{
			{},
			{{Decl_Type::unit, true}},
		}, true, -1);
	
	auto id =  model->functions.find_or_create(scope, &decl->handle_name, nullptr, decl);
	auto function = model->functions[id];
	
	for(auto arg : decl->args) {
		if(!arg->decl && arg->chain.size() != 1) {
			decl->source_loc.print_error_header();
			fatal_error("The arguments to a function declaration should be just single identifiers with or without units.");
		}
		if(arg->decl) {
			// It is a bit annoying to not use  resolve_argument here, but we don't want it to be associated to the handle in the scope.
			auto unit_id = model->units.find_or_create(scope, nullptr, nullptr, arg->decl);
			auto unit = model->units[unit_id];
			unit->data.set_data(arg->decl);
			if(!arg->decl->handle_name.string_value.count) {
				arg->decl->source_loc.print_error_header();
				fatal_error("All arguments to a function must be named.");
			}
			function->args.push_back(arg->decl->handle_name.string_value);
			function->expected_units.push_back(unit_id);
		} else {
			function->args.push_back(arg->chain[0].string_value);
			function->expected_units.push_back(invalid_entity_id);
		}
	}
	
	for(int idx = 1; idx < function->args.size(); ++idx) {
		for(int idx2 = 0; idx2 < idx; ++idx2) {
			if(function->args[idx] == function->args[idx2]) {
				decl->args[idx]->chain[0].print_error_header();
				fatal_error("Duplicate argument name '", function->args[idx], "' in function declaration.");
			}
		}
	}
	
	auto body = static_cast<Function_Body_AST *>(decl->body);
	function->code = body->block;
	function->fun_type = Function_Type::decl;
	
	return id;
}

template<> Entity_Id
process_declaration<Reg_Type::constant>(Mobius_Model *model, Decl_Scope *scope, Decl_AST *decl) {
	match_declaration(decl, {{Token_Type::quoted_string, Decl_Type::unit, Token_Type::real}});
	
	auto id        = model->constants.standard_declaration(scope, decl);
	auto constant  = model->constants[id];
	
	constant->unit = resolve_argument<Reg_Type::unit>(model, scope, decl, 1);
	constant->value = single_arg(decl, 2)->double_value();
	
	return id;
}

template<> Entity_Id
process_declaration<Reg_Type::var>(Mobius_Model *model, Decl_Scope *scope, Decl_AST *decl) {

	int which = match_declaration(decl,
		{
			{Token_Type::identifier, Decl_Type::unit},
			{Token_Type::identifier, Decl_Type::unit, Decl_Type::unit},
			{Token_Type::identifier, Decl_Type::unit, Token_Type::quoted_string},
			{Token_Type::identifier, Decl_Type::unit, Decl_Type::unit, Token_Type::quoted_string},
		},
		false, true, true);
	
	auto id  = model->vars.find_or_create(scope, &decl->handle_name, nullptr, decl);
	auto var = model->vars[id];
	
	// NOTE: We don't register it with the name in find_or_create because it doesn't matter if this name clashes with other entities
	int name_idx = which;
	Token *var_name = nullptr;
	if(which == 2 || which == 3)
		var_name = single_arg(decl, name_idx);
	
	if(var_name) {
		check_allowed_serial_name(var_name->string_value, var_name->source_loc);
		var->var_name = var_name->string_value;
	}

	process_location_argument(model, scope, decl->args[0], &var->var_location);	
	
	var->unit = resolve_argument<Reg_Type::unit>(model, scope, decl, 1);
	
	if(which == 1 || which == 3) {
		if(!var->var_location.is_dissolved()) {
			var->source_loc.print_error_header();
			fatal_error("Concentration units should only be provided for dissolved quantities.");
		}
		var->conc_unit = resolve_argument<Reg_Type::unit>(model, scope, decl, 2);
	} else
		var->conc_unit = invalid_entity_id;
	
	if(decl->body)
		var->code = static_cast<Function_Body_AST *>(decl->body)->block;
	
	for(auto note : decl->notes) {
		auto str = note->decl.string_value;
		if(str == "initial" || str == "initial_conc") {
			
			match_declaration_base(note, {{}}, -1);
			
			if(var->initial_code) {
				note->decl.print_error_header();
				fatal_error("Declaration var more than one 'initial' or 'initial_conc' block.");
			}
			var->initial_code = static_cast<Function_Body_AST *>(note->body)->block;
			var->initial_is_conc = (str == "initial_conc");
		} else if(str == "override" || str == "override_conc") {
			
			match_declaration_base(note, {{}}, -1);
			
			if(var->override_code) {
				note->decl.print_error_header();
				fatal_error("Declaration has more than one 'override' or 'override_conc' block.");
			}
			var->override_code = static_cast<Function_Body_AST *>(note->body)->block;
			var->override_is_conc = (str == "override_conc");
		} else {
			note->decl.print_error_header();
			fatal_error("Expected either no notes, 'initial' or 'override_conc'.");
		}
	}
	
	return id;
}

template<> Entity_Id
process_declaration<Reg_Type::flux>(Mobius_Model *model, Decl_Scope *scope, Decl_AST *decl) {
	
	match_declaration(decl,
		{
			{Token_Type::identifier, Token_Type::identifier, Decl_Type::unit, Token_Type::quoted_string},
		}, true, -1, true);
	
	Token *name = single_arg(decl, 3);
	
	auto id   = model->fluxes.find_or_create(scope, &decl->handle_name, name, decl);
	auto flux = model->fluxes[id];
	
	flux->unit = resolve_argument<Reg_Type::unit>(model, scope, decl, 2);
	
	process_location_argument(model, scope, decl->args[0], &flux->source, true, true);
	process_location_argument(model, scope, decl->args[1], &flux->target, true, true);
	
	if(flux->source == flux->target && is_located(flux->source)) {
		decl->source_loc.print_error_header();
		fatal_error("The source and the target of a flux can't be the same.");
	}
	
	if(flux->source.restriction == Var_Loc_Restriction::specific) {
		decl->source_loc.print_error_header();
		fatal_error("For now we only allow the target of a flux to have the 'specific' restriction");
	}
	bool has_specific_target = (flux->target.restriction == Var_Loc_Restriction::specific) || (flux->target.restriction2.restriction == Var_Loc_Restriction::specific);
	bool has_specific_code = false;
	
	if(decl->body)
		flux->code = static_cast<Function_Body_AST *>(decl->body)->block;
	
	for(auto note : decl->notes) {
		auto str = note->decl.string_value;
		if(str == "no_carry") {
			match_declaration_base(note, {{}}, 1);
		
			if(!is_located(flux->source)) {
				note->decl.print_error_header();
				fatal_error("A 'no_carry' block only makes sense if the source of the flux is specific (not 'out').");
			}
			if(note->body)
				flux->no_carry_ast = static_cast<Function_Body_AST *>(note->body)->block;
			else
				flux->no_carry_by_default = true;
			
		} else if(str == "specific") {
			match_declaration_base(note, {{}}, -1);
			
			has_specific_code = true;
			flux->specific_target_ast = static_cast<Function_Body_AST *>(note->body)->block;
		} else {
			note->decl.print_error_header();
			fatal_error("Unrecognized note '", str, "' for a flux declaration.");
		}
	}
	
	if(has_specific_target != has_specific_code) {
		decl->source_loc.print_error_header();
		fatal_error("A flux must either have both a 'specific' in its target and a '@specific' code block, or have none of these.");
	}

	return id;
}

template<> Entity_Id
process_declaration<Reg_Type::discrete_order>(Mobius_Model *model, Decl_Scope *scope, Decl_AST *decl) {
	match_declaration(decl, {{ }}, false, -1);
	
	auto id    = model->discrete_orders.find_or_create(scope, nullptr, nullptr, decl);
	auto discr = model->discrete_orders[id];
	
	auto body = static_cast<Function_Body_AST *>(decl->body);
	bool success = true;
	for(auto expr : body->block->exprs) {
		if(expr->type != Math_Expr_Type::identifier) {
			success = false;
			break;
		}
		auto ident = static_cast<Identifier_Chain_AST *>(expr);
		if(ident->chain.size() != 1 || !ident->bracketed_chain.empty()) {
			success = false;
			break;
		}
		auto flux_id = model->fluxes.find_or_create(scope, &ident->chain[0], nullptr, nullptr);
		discr->fluxes.push_back(flux_id);
		auto flux = model->fluxes[flux_id];
		if(is_valid(flux->discrete_order)) {
			ident->chain[0].source_loc.print_error_header();
			error_print("The flux '", ident->chain[0].string_value, "' was already put in another discrete order declared here:\n");
			model->discrete_orders[flux->discrete_order]->source_loc.print_error();
			mobius_error_exit();
		}
		flux->discrete_order = id;
	}
	if(!success) {
		body->opens_at.print_error_header();
		fatal_error("A 'discrete_order' body should just contain a list of flux identifiers.");
	}
	
	return id;
}

template<> Entity_Id
process_declaration<Reg_Type::external_computation>(Mobius_Model *model, Decl_Scope *scope, Decl_AST *decl) {
	// NOTE: Since we disambiguate entities on their name right now, we can't let the name and function_name be the same in case you want to reuse the same function many times.
	//    could be fixed if/when we make the new module loading / scope system.
	int which = match_declaration(decl,
		{
			{ Token_Type::quoted_string, Token_Type::quoted_string },
			{ Token_Type::quoted_string, Token_Type::quoted_string, Decl_Type::compartment },
		}, false, -1);
	
	auto id = model->external_computations.standard_declaration(scope, decl);
	auto comp = model->external_computations[id];
	
	comp->function_name = single_arg(decl, 1)->string_value;
	if(which == 1)
		comp->component = resolve_argument<Reg_Type::component>(model, scope, decl, 2);
	
	auto body = static_cast<Function_Body_AST *>(decl->body);
	
	comp->code = body->block;
	
	return id;
}

void
add_connection_component_option(Mobius_Model *model, Decl_Scope *scope, Entity_Id connection_id, Regex_Identifier_AST *ident) {
	
	auto connection = model->connections[connection_id];
	
	if(ident->ident.string_value == "out") {
		if(is_valid(&ident->index_set)) {
			ident->index_set.print_error_header();
			fatal_error("There should not be an index set on 'out'");
		}
		
		return;
	} else if (ident->wildcard)
		return;
	
	auto component_id = model->components.find_or_create(scope, &ident->ident);
	Entity_Id index_set_id = invalid_entity_id;
	if(is_valid(&ident->index_set)) {
		index_set_id = model->index_sets.find_or_create(scope, &ident->index_set);
		auto index_set = model->index_sets[index_set_id];
		//	TODO: Also check for duplicates
		if(connection->type == Connection_Type::directed_graph) {
			index_set->is_edge_of_connection = connection_id;
			index_set->is_edge_of_node       = component_id;
		}
		if(connection->type != Connection_Type::directed_graph && connection->type != Connection_Type::grid1d && connection->type != Connection_Type::all_to_all) {
			ident->index_set.print_error_header();
			fatal_error("Index sets should only be assigned to the nodes in connections of type 'directed_graph', 'grid1d' or 'all_to_all'");
		}
	}
	bool found = false;
	for(auto &comp : connection->components) {
		if(comp.first == component_id) {
			found = true;
			break;
		}
	}
	if(!found) {
		if(!is_valid(index_set_id) && 
			(connection->type == Connection_Type::directed_graph || connection->type == Connection_Type::grid1d || connection->type == Connection_Type::all_to_all)) {
			
			ident->source_loc.print_error_header();
			fatal_error("Nodes in connections of type 'directed_graph', 'grid1d' or 'all_to_all' must have index sets assigned to them.");
		}
		connection->components.push_back({component_id, index_set_id});
	} else if(is_valid(index_set_id)) {
		ident->index_set.print_error_header();
		fatal_error("An index set should only be declared on the first occurrence of a component in the regex.");
	}
}

void
recursive_gather_connection_component_options(Mobius_Model *model, Decl_Scope *scope, Entity_Id connection_id, Math_Expr_AST *expr) {
	if(expr->type == Math_Expr_Type::regex_identifier)
		add_connection_component_option(model, scope, connection_id, static_cast<Regex_Identifier_AST *>(expr));
	else {
		for(auto child : expr->exprs) {
			recursive_gather_connection_component_options(model, scope, connection_id, child);
		}
	}
}

template<> Entity_Id
process_declaration<Reg_Type::connection>(Mobius_Model *model, Decl_Scope *scope, Decl_AST *decl) {
	int which = match_declaration(decl,
	{
		{Token_Type::quoted_string},
		{Token_Type::quoted_string, Token_Type::identifier},
	}, true, -1);
	
	auto id         = model->connections.standard_declaration(scope, decl);
	auto connection = model->connections[id];
	
	if(which == 1) {
		String_View structure_type = single_arg(decl, 1)->string_value;
		if(structure_type == "directed_tree")	    connection->type = Connection_Type::directed_tree;
		else if(structure_type == "directed_graph") connection->type = Connection_Type::directed_graph;
		else if(structure_type == "all_to_all")     connection->type = Connection_Type::all_to_all;
		else if(structure_type == "grid1d")         connection->type = Connection_Type::grid1d;
		else {
			single_arg(decl, 1)->print_error_header();
			fatal_error("Unsupported connection structure type '", structure_type, "'.");
		}
	}
	
	auto expr = static_cast<Regex_Body_AST *>(decl->body)->expr;
	connection->regex = expr;
	
	if((connection->type == Connection_Type::all_to_all || connection->type == Connection_Type::grid1d)
		&& expr->type != Math_Expr_Type::regex_identifier) {
		expr->source_loc.print_error_header();
		fatal_error("For connections of type all_to_all and grid1d, only one component should be declared ( of the form {  c[i]  } , where i is the index set).");
	}
	
	recursive_gather_connection_component_options(model, scope, id, expr);
	
	if(connection->components.empty()) {
		expr->source_loc.print_error_header();
		fatal_error("At least one component must be involved in a connection.");
	}

	return id;
}

template<> Entity_Id
process_declaration<Reg_Type::loc>(Mobius_Model *model, Decl_Scope *scope, Decl_AST *decl) {
	match_declaration(decl, {{Token_Type::identifier}});
	
	auto id  = model->locs.find_or_create(scope, &decl->handle_name, nullptr, decl);
	auto loc = model->locs[id];
	
	process_location_argument(model, scope, decl->args[0], &loc->loc, true, true, &loc->par_id);
	
	return id;
}

void
process_load_library_declaration(Mobius_Model *model, Decl_AST *decl, Entity_Id to_scope, String_View load_decl_path);

void
load_library(Mobius_Model *model, Entity_Id to_scope, String_View rel_path, String_View load_decl_path, std::string &decl_name, Source_Location load_loc) {
	
	Entity_Id lib_id = load_top_decl_from_file(model, load_loc, rel_path, load_decl_path, decl_name, Decl_Type::library);

	auto lib = model->libraries[lib_id];
	
	if(!lib->has_been_processed && !lib->is_being_processed) {
  
		lib->is_being_processed = true; // To not go into an infinite loop if we have a recursive load.
		
		match_declaration(lib->decl, {{Token_Type::quoted_string}}, false, -1); // REFACTOR. matching is already done in the load_top_decl_from_file
		
		auto body = static_cast<Decl_Body_AST *>(lib->decl->body);
		
		if(body->doc_string.string_value.count)
			lib->doc_string = body->doc_string.string_value;
		
		lib->scope.import(model->global_scope);
		
		// It is important to process the contents of the library before we do subsequent loads, otherwise some contents may not be loaded if we short-circuit due to a recursive load.
		//   Note that the code of the functions is processed at a much later snotee, so we don't need the symbols in the function bodies to be available yet.
		for(Decl_AST *child : body->child_decls) {
			if(child->type == Decl_Type::load) continue; // already processed above.
			else if(child->type == Decl_Type::constant) {
				process_declaration<Reg_Type::constant>(model, &lib->scope, child);
			} else if(child->type == Decl_Type::function) {
				process_declaration<Reg_Type::function>(model, &lib->scope, child);
			} else {
				child->source_loc.print_error_header();
				fatal_error("Did not expect a declaration of type ", name(child->type), " inside a library declaration.");
			}
		}
		
		// Check for recursive library loads into other libraries.
		for(Decl_AST *child : body->child_decls) {
			if(child->type == Decl_Type::load)
				process_load_library_declaration(model, child, lib_id, lib->normalized_path);
		}
		
		lib = model->libraries[lib_id]; // NOTE: Have to re-fetch it because the pointer may have been invalidated by registering more libraries.
		lib->has_been_processed = true;
		lib->scope.check_for_missing_decls(model);
	}
	
	model->get_scope(to_scope)->import(lib->scope, &load_loc);
}

void
process_load_library_declaration(Mobius_Model *model, Decl_AST *decl, Entity_Id to_scope, String_View load_decl_path) {
	int which = match_declaration(decl,
		{
			{Token_Type::quoted_string, { Decl_Type::library, true } },
			{{Decl_Type::library, true}},
		}, false);
	String_View file_name;
	String_View relative_to;
	if(which == 0) {
		file_name = single_arg(decl, 0)->string_value;
		relative_to = load_decl_path;
	} else {
		file_name = load_decl_path;   // Load another library from the same file.
		relative_to = "";
	}
	
	int offset = which == 0 ? 1 : 0;  // If the library names start at argument 0 or 1 of the load decl.
	
	for(int idx = offset; idx < decl->args.size(); ++idx) {
		auto lib_load_decl = decl->args[idx]->decl;
		match_declaration(lib_load_decl, {{Token_Type::quoted_string}}, false, false);
		std::string library_name = single_arg(lib_load_decl, 0)->string_value;

		load_library(model, to_scope, file_name, relative_to, library_name, lib_load_decl->source_loc);
	}
}

Entity_Id
process_module_load(Mobius_Model *model, Token *load_name, Entity_Id template_id, Source_Location &load_loc, std::vector<Entity_Id> &load_args, bool import_scope = false) {
	
	auto mod_temp = model->module_templates[template_id];
	
	// TODO: It is a bit superfluous to process the name and version of the template every time it is specialized.
	
	auto decl = mod_temp->decl;
	match_declaration(decl,
		{
			{Token_Type::quoted_string, Decl_Type::version},
			{Token_Type::quoted_string, Decl_Type::version, {true}}
		}, false, -1);
	
	auto version_decl = decl->args[1]->decl;
	
	match_declaration(version_decl, {{Token_Type::integer, Token_Type::integer, Token_Type::integer}}, false);
	
	mod_temp->name                 = single_arg(decl, 0)->string_value;
	mod_temp->version.major        = single_arg(version_decl, 0)->val_int;
	mod_temp->version.minor        = single_arg(version_decl, 1)->val_int;
	mod_temp->version.revision     = single_arg(version_decl, 2)->val_int;
	
	auto body = static_cast<Decl_Body_AST *>(decl->body);
	
	if(body->doc_string.string_value.count)
		mod_temp->doc_string = body->doc_string.string_value;
	
	// Create a module specialization of the module template:
	
	auto spec_name_token = single_arg(decl, 0);
	if(load_name)
		spec_name_token = load_name;
	
	std::string spec_name = spec_name_token->string_value;
	
	auto model_scope = &model->model_decl_scope;
	
	// TODO: Potentially a different name:
	auto module_id = model_scope->deserialize(spec_name, Reg_Type::module);
	
	if(is_valid(module_id)) return module_id; // It has been specialized with this name already, so just return the one that was already created.
	
	// TODO: The other name must be used here too:
	module_id = model->modules.find_or_create(model_scope, nullptr, spec_name_token, nullptr);
	auto module = model->modules[module_id];
	
	if(load_name)
		module->full_name = mod_temp->name + " (" + module->name + ")";
	else
		module->full_name = module->name;
	
	// Ouch, this is a bit hacky, but it is to avoid the problem that Decl_Type::module is tied to Reg_Type::module_template .
	// Maybe we should instead have another flag on it?
	module->has_been_declared = true;
	
	module->scope.parent_id = module_id;
	module->template_id = template_id;
	module->scope.import(model->global_scope);
	
	if(import_scope)
		module->scope.import(*model_scope, &load_loc, true);  // The 'true' is to signify that we also import parameters (that would otherwise be a double import since they come from a par_group scope)
	
	if(import_scope && decl->args.size() > 2) {
		decl->source_loc.print_error_header();
		fatal_error("Inlined module declarations should not have load arguments.\n");
	}
	
	int required_args = decl->args.size() - 2;
	if(load_args.size() != required_args) {
		load_loc.print_error_header();
		error_print("The module \"", mod_temp->name, "\" requires ", required_args, " load arguments. Only ", load_args.size(), " were passed. See declaration at\n");
		decl->source_loc.print_error();
		mobius_error_exit();
	}
	
	for(int idx = 0; idx < required_args; ++idx) {
		
		Entity_Id load_id = load_args[idx];
		
		auto arg = decl->args[idx + 2];
		if(!arg->decl || !is_valid(&arg->decl->handle_name)) {
			arg->chain[0].print_error_header();
			fatal_error("Load arguments to a module must be of the form  handle : decl_type.");
		}
		match_declaration(arg->decl, {{}}, true, false); // TODO: Not sure if we should allow passing a name to enforce name match.
		std::string handle = arg->decl->handle_name.string_value;
		auto *entity = model->find_entity(load_id);
		if(arg->decl->type != entity->decl_type) {
			load_loc.print_error_header();
			error_print("Load argument ", idx, " to the module ", mod_temp->name, " should have type '", name(arg->decl->type), "'. A '", name(entity->decl_type), "' was passed instead. See declaration at\n");
			decl->source_loc.print_error();
			mobius_error_exit();
		}
		auto reg = module->scope.add_local(handle, arg->decl->source_loc, load_id);
		reg->is_load_arg = true;
	}
	

	for(Decl_AST *child : body->child_decls) {
		switch(child->type) {
			
			case Decl_Type::property : {
				process_declaration<Reg_Type::component>(model, &module->scope, child);
			} break;
			
			case Decl_Type::connection : {
				process_declaration<Reg_Type::connection>(model, &module->scope, child);
			} break;
		}
	}
	
	for(Decl_AST *child : body->child_decls) {
		if(child->type == Decl_Type::loc)
			process_declaration<Reg_Type::loc>(model, &module->scope, child);
	}
	
	for(Decl_AST *child : body->child_decls) {
		switch(child->type) {
			case Decl_Type::load : {
				process_load_library_declaration(model, child, module_id, mod_temp->normalized_path);
			} break;
			
			case Decl_Type::unit : {
				process_declaration<Reg_Type::unit>(model, &module->scope, child);
			} break;
			
			case Decl_Type::par_group : {
				process_declaration<Reg_Type::par_group>(model, &module->scope, child);
			} break;
			
			case Decl_Type::function : {
				process_declaration<Reg_Type::function>(model, &module->scope, child);
			} break;
			
			case Decl_Type::constant : {
				process_declaration<Reg_Type::constant>(model, &module->scope, child);
			} break;
			
			case Decl_Type::var : {
				process_declaration<Reg_Type::var>(model, &module->scope, child);
			} break;
			
			case Decl_Type::flux : {
				process_declaration<Reg_Type::flux>(model, &module->scope, child);
			} break;
			
			case Decl_Type::discrete_order : {
				process_declaration<Reg_Type::discrete_order>(model, &module->scope, child);
			} break;
			
			case Decl_Type::external_computation : {
				process_declaration<Reg_Type::external_computation>(model, &module->scope, child);
			} break;
			
			case Decl_Type::property :
			case Decl_Type::connection :
			case Decl_Type::loc : {  // Already processed in an earlier loop
			} break;
			
			default : {
				child->source_loc.print_error_header();
				fatal_error("Did not expect a declaration of type ", name(child->type), " inside a module declaration.");
			};
		}
	}

	module->scope.check_for_missing_decls(model);
	
	return module_id;
}

template<> Entity_Id
process_declaration<Reg_Type::index_set>(Mobius_Model *model, Decl_Scope *scope, Decl_AST *decl) {
	
	match_declaration(decl, {{Token_Type::quoted_string}}, true, 0, true);
	
	auto id  = model->index_sets.standard_declaration(scope, decl);
	auto index_set = model->index_sets[id];
	
	for(auto note : decl->notes) {
		auto str = note->decl.string_value;
		if(str == "sub") {
			match_declaration_base(note, {{Decl_Type::index_set}}, 0);
			auto subto_id = resolve_argument<Reg_Type::index_set>(model, scope, note, 0);
			auto subto = model->index_sets[subto_id];
			if(!subto->has_been_declared) {
				note->decl.print_error_header();
				fatal_error("For technical reasons, an index set must be declared after an index sets it is sub-indexed to.");
			}
			index_set->sub_indexed_to = subto_id;
			if(std::find(subto->union_of.begin(), subto->union_of.end(), id) != subto->union_of.end()) {
				decl->source_loc.print_error_header();
				fatal_error("An index set can not be sub-indexed to something that is a union of it.");
			}
		} else if(str == "union") {
			match_declaration_base(note, {{Decl_Type::index_set, {Decl_Type::index_set, true}}}, 0);
			
			for(int argidx = 0; argidx < note->args.size(); ++argidx) {
				auto union_set = resolve_argument<Reg_Type::index_set>(model, scope, note, argidx);
				if(union_set == id || std::find(index_set->union_of.begin(), index_set->union_of.end(), union_set) != index_set->union_of.end()) {
					decl->args[argidx]->source_loc().print_error_header();
					fatal_error("Any index set can only appear once in a union.");
				}
				index_set->union_of.push_back(union_set);
			}
		} else {
			note->decl.print_error_header();
			fatal_error("Unrecognized note '", str, "' for 'index_set' declaration.");
		}
	}
	
	return id;
}

template<> Entity_Id
process_declaration<Reg_Type::solver>(Mobius_Model *model, Decl_Scope *scope, Decl_AST *decl) {
	int which = match_declaration(decl, {
		{Token_Type::quoted_string, Token_Type::quoted_string, Token_Type::real},
		{Token_Type::quoted_string, Token_Type::quoted_string, Token_Type::real, Token_Type::real},
		{Token_Type::quoted_string, Token_Type::quoted_string, Decl_Type::par_real},
		{Token_Type::quoted_string, Token_Type::quoted_string, Decl_Type::par_real, Decl_Type::par_real},
	});
	auto id = model->solvers.standard_declaration(scope, decl);
	auto solver = model->solvers[id];
	
	// TODO: this should be more dynamic so that it easy for users to link in other solver functions (e.g. from a .dll).
	String_View solver_name = single_arg(decl, 1)->string_value;
	if(solver_name == "Euler")
		solver->solver_fun = &euler_solver;
	else if(solver_name == "INCADascru")
		solver->solver_fun = &inca_dascru;
	else {
		single_arg(decl, 1)->print_error_header();
		fatal_error("The name \"", solver_name, "\" is not recognized as the name of an ODE solver.");
	}
	//TODO: allow parametrization of the solver h and hmin like in Mobius1.
	
	solver->hmin = 0.01;
	if(which == 0 || which == 1) {
		solver->h = single_arg(decl, 2)->double_value();
		if(which == 1)
			solver->hmin = single_arg(decl, 3)->double_value();
	} else {
		solver->h_par = resolve_argument<Reg_Type::parameter>(model, scope, decl, 2);
		if(which == 3)
			solver->hmin_par = resolve_argument<Reg_Type::parameter>(model, scope, decl, 3);
	}
	
	return id;
}

void
process_solve_declaration(Mobius_Model *model, Decl_Scope *scope, Decl_AST *decl) {

	match_declaration(decl, {{Decl_Type::solver, {Token_Type::identifier, true}}}, false);
	
	auto solver_id = resolve_argument<Reg_Type::solver>(model, scope, decl, 0);
	auto solver    = model->solvers[solver_id];
	
	for(int idx = 1; idx < decl->args.size(); ++idx) {
		Var_Location loc;
		process_location_argument(model, scope, decl->args[idx], &loc);
		
		if(loc.is_dissolved()) {
			decl->args[idx]->chain[0].source_loc.print_error_header(Mobius_Error::model_building);
			fatal_error("For now we don't allow specifying solvers for dissolved substances. Instead they are given the solver of the variable they are dissolved in.");
		}
		
		solver->locs.push_back(loc);
	}
}

void
process_distribute_declaration(Mobius_Model *model, Decl_Scope *scope, Decl_AST *decl) {
	match_declaration(decl,
	{
		{Decl_Type::compartment, {Decl_Type::index_set, true}},
		{Decl_Type::quantity,    {Decl_Type::index_set, true}},
	}, false);
	
	auto comp_id   = resolve_argument<Reg_Type::component>(model, scope, decl, 0);
	auto component = model->components[comp_id];
	
	if(component->decl_type == Decl_Type::property) {
		decl->source_loc.print_error_header(Mobius_Error::model_building);
		fatal_error("Only compartments and quantities can be distributed, not properties");
	}
	
	for(int idx = 1; idx < decl->args.size(); ++idx) {
		auto id = resolve_argument<Reg_Type::index_set>(model, scope, decl, idx);
		component->index_sets.push_back(id);
	}
	check_valid_distribution(model, component->index_sets, decl->source_loc);
}

void
process_aggregation_weight_declaration(Mobius_Model *model, Decl_Scope *scope, Decl_AST *decl) {
	match_declaration(decl, {{Decl_Type::compartment, Decl_Type::compartment}}, false, -1);
	
	auto from_comp = resolve_argument<Reg_Type::component>(model, scope, decl, 0);
	auto to_comp   = resolve_argument<Reg_Type::component>(model, scope, decl, 1);
	
	if(model->components[from_comp]->decl_type != Decl_Type::compartment || model->components[to_comp]->decl_type != Decl_Type::compartment) {
		decl->source_loc.print_error_header(Mobius_Error::model_building);
		fatal_error("Aggregations can only be declared between compartments");
	}
	
	//TODO: some guard against overlapping / contradictory declarations.
	auto function = static_cast<Function_Body_AST *>(decl->body);
	model->components[from_comp]->aggregations.push_back({to_comp, function->block, scope->parent_id});
}

void
process_unit_conversion_declaration(Mobius_Model *model, Decl_Scope *scope, Decl_AST *decl) {
	match_declaration(decl, {{Token_Type::identifier, Token_Type::identifier}}, false, -1);
	
	Flux_Unit_Conversion_Data data = {};
	
	process_location_argument(model, scope, decl->args[0], &data.source);
	process_location_argument(model, scope, decl->args[1], &data.target);
	data.code = static_cast<Function_Body_AST *>(decl->body)->block;
	data.scope_id = scope->parent_id;
	
	//TODO: some guard against overlapping / contradictory declarations.
	if(data.source == data.target) {
		decl->source_loc.print_error_header();
		fatal_error("The source and target of a 'unit_conversion' should not be the same.");
	}
	
	model->components[data.source.first()]->unit_convs.push_back(data);
}

struct
Model_Extension {
	std::string normalized_path;
	Decl_AST *decl;
	int load_order;
	int depth;
};

bool
load_model_extensions(File_Data_Handler *handler, Decl_AST *from_decl,
	std::unordered_set<std::string> &loaded_paths, std::vector<Model_Extension> &loaded_decls, String_View rel_path, int *load_order, int depth) {
	
	auto body = static_cast<Decl_Body_AST *>(from_decl->body);
	
	for(Decl_AST *child : body->child_decls) {
		if(child->type != Decl_Type::extend) continue;
			
		match_declaration(child, {{Token_Type::quoted_string}}, false);
		String_View extend_file_name = single_arg(child, 0)->string_value;
		
		// TODO: It is a bit unnecessary to read the AST from the file before we check that the normalized path is not already in the dictionary.
		//  but right now normalizing the path and loading the ast happens inside the same call in read_model_ast_from_file
		std::string normalized_path;
		auto extend_model = read_model_ast_from_file(handler, extend_file_name, rel_path, &normalized_path);
		
		if(loaded_paths.find(normalized_path) != loaded_paths.end()) {
			begin_error(Mobius_Error::parsing);
			error_print("There is circularity in the model extensions:\n", extend_file_name, "\n");
			delete extend_model; //TODO: see above
			return false;
		}
		
		// NOTE: this is for tracking circularity. We have to reset it for each branch we go down in the extension tree (but we allow multiple branches to extend the same model)
		auto loaded_paths_sub = loaded_paths;
		loaded_paths_sub.insert(normalized_path);
		
		// Make sure to only load it once.
		bool found = false;
		for(auto &extend : loaded_decls) {
			if(extend.normalized_path == normalized_path) {
				found = true;
				break;
			}
		}
		if(found)
			delete extend_model; // TODO: see above
		else {
			loaded_decls.push_back( { normalized_path, extend_model, *load_order, depth} );
			(*load_order)++;
			// Load extensions of extensions.
			bool success = load_model_extensions(handler, extend_model, loaded_paths_sub, loaded_decls, normalized_path, load_order, depth+1);
			if(!success) {
				error_print(extend_file_name, "\n");
				return false;
			}
		}
	}
	
	return true;
}

Mobius_Config
load_config(String_View file_name) {
	Mobius_Config config;
	
	File_Data_Handler files;
	
	auto file_data = files.load_file(file_name);
	Token_Stream stream(file_name, file_data);
	while(true) {
		Token token = stream.peek_token();
		if(token.type == Token_Type::eof) break;
		Decl_AST *decl = parse_decl(&stream);
		if(decl->type != Decl_Type::config) {
			decl->source_loc.print_error_header();
			fatal_error("Unexpected declaration type '", name(decl->type), "' in a config file.");
		}
		match_declaration(decl, {{Token_Type::quoted_string, Token_Type::quoted_string}}, false);
		auto item = single_arg(decl, 0)->string_value;
		if(item == "Mobius2 base path") {
			config.mobius_base_path = single_arg(decl, 1)->string_value;
			struct stat info;
			if(stat(config.mobius_base_path.data(), &info) != 0 || !(info.st_mode & S_IFDIR)) {
				single_arg(decl, 0)->print_error_header();
				fatal_error("The path \"", config.mobius_base_path, "\" is not a valid directory path.");
			}
		} else {
			decl->source_loc.print_error_header();
			fatal_error("Unknown config option \"", item, "\".");
		}
		delete decl;
	}
	
	return std::move(config);
}


void
process_module_arguments(Mobius_Model *model, Decl_Scope *scope, Decl_AST *decl, std::vector<Entity_Id> &load_args, int first_idx) {
	for(int idx = first_idx; idx < decl->args.size(); ++idx) {
		auto arg = decl->args[idx];
		
		if(arg->decl) {
			// TODO: Make all relevant arguments inlinable:
			if(arg->decl->type != Decl_Type::loc) {
				arg->decl->source_loc.print_error_header();
				fatal_error("For now, we don't support direct declarations in the arguments passed to a module load unless they are of type 'loc'.");
			} else {
				auto id = resolve_argument<Reg_Type::loc>(model, scope, decl, idx);
				load_args.push_back(id);
			}
		} else {
			auto &token = arg->chain[0];
			if(arg->chain.size() != 1 || !arg->bracketed_chain.empty() || token.type != Token_Type::identifier) {
				token.print_error_header();
				fatal_error("Unsupported module load argument.");
			}
			std::string handle = token.string_value;
			auto reg = (*scope)[handle];
			bool found = false;
			if(reg) {
				auto entity = model->find_entity(reg->id);
				if(entity->has_been_declared) found = true;
			}
			if(!found) {
				token.print_error_header();
				fatal_error("The handle '", token.string_value, "' was not declared in this scope.");
			}
			load_args.push_back(reg->id); // TODO: Check on what types of objects are allowed to be passed?
		}
	}
}

void
apply_config(Mobius_Model *model, Mobius_Config *config) {
	// TODO: Maybe check validity of the path here instead of in load_config (Need to save the source_loc in that case).
	model->mobius_base_path = config->mobius_base_path;
}

Mobius_Model *
load_model(String_View file_name, Mobius_Config *config) {
	
	Mobius_Model *model = new Mobius_Model();
	
	if(config)
		apply_config(model, config);
	else {
		Mobius_Config new_config = load_config();
		apply_config(model, &new_config);
	}
	
	auto decl = read_model_ast_from_file(&model->file_handler, file_name);
	model->main_decl  = decl;
	model->model_name = single_arg(decl, 0)->string_value;
	model->path       = file_name;
	
	auto body = static_cast<Decl_Body_AST *>(decl->body);
	
	if(body->doc_string.string_value.count)
		model->doc_string = body->doc_string.string_value;
	
	register_intrinsics(model);

	std::unordered_set<std::string> loaded_files = { file_name };
	std::vector<Model_Extension> extend_models = { { file_name, decl, 0, 0} };
	int order = 1;
	bool success = load_model_extensions(&model->file_handler, decl, loaded_files, extend_models, file_name, &order, 1);
	if(!success)
		mobius_error_exit();
	
	std::sort(extend_models.begin(), extend_models.end(), [](const Model_Extension &extend1, const Model_Extension &extend2) -> bool {
		if(extend1.depth == extend2.depth) return extend1.load_order < extend2.load_order;
		return extend1.depth > extend2.depth;
	});

	auto scope = &model->model_decl_scope;
	
	// This is a bit tricky, but we have to register index sets first so that they get in the right order, otherwise resolution of connection regexes could trip the order, and this is very important for sub-indexed index sets. TODO: we could improve the sorting of index set dependencies in codegen instead.
	for(auto &extend : extend_models) {
		auto ast = extend.decl;
		auto body = static_cast<Decl_Body_AST *>(ast->body);
		
		for(Decl_AST *child : body->child_decls) {
			if(child->type == Decl_Type::index_set) // Process index sets before distribute() because we need info about what we distribute over.
				process_declaration<Reg_Type::index_set>(model, scope, child);
		}
	}
	
	// We need to process these first since some other declarations rely on these existing, such as par_group.
	for(auto &extend : extend_models) {
		auto ast = extend.decl;
		auto body = static_cast<Decl_Body_AST *>(ast->body);
		
		for(Decl_AST *child : body->child_decls) {
			switch (child->type) {
				case Decl_Type::compartment :
				case Decl_Type::quantity :
				case Decl_Type::property : {
					process_declaration<Reg_Type::component>(model, scope, child);
				} break;
				
				case Decl_Type::connection : {
					process_declaration<Reg_Type::connection>(model, scope, child);
				} break;
				
				case Decl_Type::par_group : {
					process_declaration<Reg_Type::par_group>(model, scope, child);
				} break;
				
				default : {
				} break;
			}
		}
	}
	
	for(auto &extend : extend_models) {
		auto ast = extend.decl;
		auto body = static_cast<Decl_Body_AST *>(ast->body);
		for(Decl_AST *child : body->child_decls) {
			
			if(child->type == Decl_Type::loc)
				process_declaration<Reg_Type::loc>(model, scope, child);
			else if(child->type == Decl_Type::solver)
				process_declaration<Reg_Type::solver>(model, scope, child);
			else if(child->type == Decl_Type::constant)
				process_declaration<Reg_Type::constant>(model, scope, child);
		}
	}
	
	// NOTE: process loads before the rest of the model scope declarations. (may no longer be necessary).
	for(auto &extend : extend_models) {
		auto &model_path = extend.normalized_path;
		auto ast = extend.decl;
		auto body = static_cast<Decl_Body_AST *>(ast->body);
		for(Decl_AST *child : body->child_decls) {
			//TODO: do libraries also.
			
			switch (child->type) {
				case Decl_Type::load : {
					// Load from another file using a "load" declaration
					match_declaration(child, {{Token_Type::quoted_string, {Decl_Type::module, true}}}, false);
					String_View file_name = single_arg(child, 0)->string_value;
					for(int idx = 1; idx < child->args.size(); ++idx) {
						Decl_AST *module_spec = child->args[idx]->decl;
						if(!module_spec) {
							single_arg(child, idx)->source_loc.print_error_header();
							fatal_error("An argument to a load must be a module() or library() declaration.");
						}
						//TODO: allow specifying the version also?
						//TODO: Allow specifying a separate name for the specialization.
						int which = match_declaration(module_spec, 
							{
								{Token_Type::quoted_string, Token_Type::quoted_string},
								{Token_Type::quoted_string, Token_Type::quoted_string, {true}},
								{Token_Type::quoted_string},
								{Token_Type::quoted_string, {true}}
							}, false, false);
						
						//warning_print("Which was ", which, "\n");
						
						auto load_loc = single_arg(child, 0)->source_loc;
						auto module_name = single_arg(module_spec, 0)->string_value;
						Token *load_name = nullptr;
						if(which <= 1)
							load_name = single_arg(module_spec, 1);
						
						auto template_id = load_top_decl_from_file(model, load_loc, file_name, model_path, module_name, Decl_Type::module);
						
						std::vector<Entity_Id> load_args;
						process_module_arguments(model, scope, module_spec, load_args, which <= 1 ? 2 : 1);

						auto module_id = process_module_load(model, load_name, template_id, load_loc, load_args);
					}
					//TODO: should also allow loading libraries inside the model scope!
				} break;
				
				case Decl_Type::module : {
					// Inline module sub-scope inside the model declaration.
					auto template_id = model->module_templates.find_or_create(scope, nullptr, nullptr, child);
					auto mod_temp = model->module_templates[template_id];
					mod_temp->decl = child;
					//module->scope.parent_id = module_id;
					mod_temp->normalized_path = model_path;
					auto load_loc = single_arg(child, 0)->source_loc;
					
					std::vector<Entity_Id> load_args; // Inline modules don't have load arguments, so this should be left empty.
					process_module_load(model, nullptr, template_id, load_loc, load_args, true);
				} break;
			}
		}
	}
	
	for(auto &extend : extend_models) {
		auto ast = extend.decl;
		auto body = static_cast<Decl_Body_AST *>(ast->body);
		
		for(Decl_AST *child : body->child_decls) {

			switch (child->type) {
				
				case Decl_Type::distribute : {
					process_distribute_declaration(model, scope, child);
				} break;
				
				case Decl_Type::solve : {
					process_solve_declaration(model, scope, child);
				} break;
				
				case Decl_Type::aggregation_weight : {
					process_aggregation_weight_declaration(model, scope, child);
				} break;
				
				case Decl_Type::unit_conversion : {
					process_unit_conversion_declaration(model, scope, child);
				} break;
				
				case Decl_Type::par_group :
				case Decl_Type::constant :
				case Decl_Type::index_set :
				case Decl_Type::solver :
				case Decl_Type::compartment :
				case Decl_Type::quantity :
				case Decl_Type::property :
				case Decl_Type::connection :
				case Decl_Type::extend :
				case Decl_Type::module :
				case Decl_Type::loc :
				case Decl_Type::load : {
					// Don't do anything. We handled it above already
				} break;
				
				default : {
					child->source_loc.print_error_header();
					fatal_error("Did not expect a declaration of type ", name(child->type), " inside a model declaration.");
				};
			}
		}
	}
	
	scope->check_for_missing_decls(model);
	
	return model;
}

Var_Location
remove_dissolved(const Var_Location &loc) {
	Var_Location result = loc;
	if(!loc.is_dissolved())
		fatal_error(Mobius_Error::internal, "Tried to find a variable location above one that is not dissolved in anything.");
	result.n_components--;
	return result;
}

Var_Location
add_dissolved(const Var_Location &loc, Entity_Id quantity) {
	Var_Location result = loc;
	if(loc.n_components == max_var_loc_components)
		fatal_error(Mobius_Error::internal, "Tried to find a variable location with a dissolved chain that is too long.");
	result.n_components++;
	result.components[result.n_components-1] = quantity;
	return result;
}

void
check_valid_distribution(Mobius_Model *model, std::vector<Entity_Id> &index_sets, Source_Location &err_loc) {
	int idx = 0;
	for(auto id : index_sets) {
		auto set = model->index_sets[id];
		if(is_valid(set->sub_indexed_to)) {
			if(std::find(index_sets.begin(), index_sets.begin()+idx, set->sub_indexed_to) == index_sets.begin()+idx) {
				err_loc.print_error_header();
				error_print("The index set \"", set->name, "\" is sub-indexed to another index set \"", model->index_sets[set->sub_indexed_to]->name, "\", but the parent index set does not precede it in this distribution. See the declaration of the index sets here:");
				set->source_loc.print_error();
				mobius_error_exit();
			}
		}
		if(!set->union_of.empty()) {
			for(auto ui_id : set->union_of) {
				if(std::find(index_sets.begin(), index_sets.end(), ui_id) != index_sets.end()) {
					err_loc.print_error_header();
					error_print("The index set \"", set->name, "\" is a union consisting among others of the index set \"", model->index_sets[ui_id]->name, "\", but both appear in the same distribution. See the declaration of the index sets here:");
					set->source_loc.print_error();
					mobius_error_exit();
				}
			}
		}
		++idx;
	}
}

// NOTE: would like to just have an ostream& operator<< on the Var_Location, but it needs to reference the scope to get the names..
void
error_print_location(Mobius_Model *model, const Specific_Var_Location &loc) {
	for(int idx = 0; idx < loc.n_components; ++idx)
		error_print(model->get_symbol(loc.components[idx]), idx == loc.n_components-1 ? "" : ".");
	// TODO: This should also print the bracket (restriction)
}

void
log_print_location(Mobius_Model *model, const Specific_Var_Location &loc) {
	for(int idx = 0; idx < loc.n_components; ++idx)
		log_print(model->get_symbol(loc.components[idx]), idx == loc.n_components-1 ? "" : ".");
	// TODO: This should also print the bracket (restriction)
}

void
Mobius_Model::free_asts() {
	// NOTE: All other ASTs that are stored are sub-trees of one of these, so we don't need to delete them separately (though we could maybe null them).
	delete main_decl;
	main_decl = nullptr;
	std::unordered_map<std::string, std::unordered_map<std::string, Entity_Id>> parsed_decls;
	for(auto id : module_templates) {
		delete module_templates[id]->decl;
		module_templates[id]->decl = nullptr;
	}
	for(auto id : libraries) {
		delete libraries[id]->decl;
		libraries[id]->decl = nullptr;
	}
}

