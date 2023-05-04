
#include <sys/stat.h>
#include "model_declaration.h"

void
Decl_Scope::add_local(const std::string &handle, Source_Location source_loc, Entity_Id id) {
	static std::string reserved[] = {
		#define ENUM_VALUE(name, _a, _b) #name,
		#include "decl_types.incl"
		#undef ENUM_VALUE
		
		#define ENUM_VALUE(name) #name,
		#include "special_directives.incl"
		#undef ENUM_VALUE
		
		#include "other_reserved.incl"
	};
	
	if(!handle.empty()) {
		if(std::find(std::begin(reserved), std::end(reserved), handle) != std::end(reserved)) {
			source_loc.print_error_header();
			fatal_error("The name '", handle, "' is reserved.");
		}
		
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
	if(!handle.empty())
		visible_entities[handle] = entity;
	handles[id] = handle;
	all_ids.insert(id);
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
	if(find == serialized_entities.end())
		return invalid_entity_id;
	auto result = find->second.id;
	if(expected_type != Reg_Type::unrecognized && result.reg_type != expected_type) return invalid_entity_id;
	return result;
}

void
Decl_Scope::import(const Decl_Scope &other, Source_Location *import_loc) {
	for(const auto &ent : other.visible_entities) {
		const auto &handle = ent.first;
		const auto &entity = ent.second;
		if(!entity.external) { // NOTE: no chain importing
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
check_allowed_serial_name(String_View serial_name, Source_Location &loc) {
	for(int idx = 0; idx < serial_name.count; ++idx) {
		char c = serial_name[idx];
		if(c == ':' || c == '.') {
			loc.print_error_header();
			fatal_error("The symbol '", c, "' is not allowed inside a name.");
		}
	}
}

// TODO: Should change scope to the first argument.
template<Reg_Type reg_type> Entity_Id
Registry<reg_type>::find_or_create(Token *handle, Decl_Scope *scope, Token *serial_name, Decl_AST *decl) {
	
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
		result_id.id        = (s32)registrations.size();
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
	return find_or_create(&decl->handle_name, scope, name, decl);
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
		case Reg_Type::solve :                    return &solves;
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
		ss << find_entity(entity->scope_id)->name << ':' << entity->name;
		return ss.str();
	}
	return entity->name;
}

Entity_Id
Mobius_Model::deserialize(const std::string &serial_name, Reg_Type expected_type) {

	auto vec = split(serial_name, ':');  // Hmm, this is maybe a bit inefficient, but probably not a problem.
	if(vec.size() > 2) return invalid_entity_id;
	else if(vec.size() == 2) {
		auto scope_id = model_decl_scope.deserialize(vec[0], Reg_Type::module);
		if(!is_valid(scope_id)) return invalid_entity_id;
		return get_scope(scope_id)->deserialize(vec[1], expected_type);
	}
	return model_decl_scope.deserialize(serial_name, expected_type);
}

template<Reg_Type expected_type> Entity_Id
resolve_argument(Mobius_Model *model, Decl_Scope *scope, Decl_AST *decl, int which, int max_sub_chain_size=1) {
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
		return model->registry(expected_type)->find_or_create(&arg->chain[0], scope);
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
	
	//system->parameters.push_back(start_id);
	//system->parameters.push_back(end_id);
	system->component = invalid_entity_id;
	
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
				id = model->libraries.find_or_create(nullptr, &model->model_decl_scope, nullptr, decl);
				auto lib = model->libraries[id];
				lib->has_been_processed = false;
				lib->decl = decl;
				lib->scope.parent_id = id;
				lib->normalized_path = normalized_path;
			} else if (decl->type == Decl_Type::module) {
				id = model->module_templates.find_or_create(nullptr, &model->model_decl_scope, nullptr, decl);
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
process_location_argument(Mobius_Model *model, Decl_Scope *scope, Decl_AST *decl, int which, Var_Location *location, bool allow_unspecified = false, bool allow_restriction = false) {
	
	Specific_Var_Location *specific_loc = nullptr;
	if(allow_restriction) {
		specific_loc = static_cast<Specific_Var_Location *>(location);
	}
	
	if(decl->args[which]->decl) {
		decl->args[which]->decl->source_loc.print_error_header();
		fatal_error("Expected a single identifier or a .-separated chain of identifiers.");
	}
	std::vector<Token> &symbol = decl->args[which]->chain;
	std::vector<Token> &bracketed = decl->args[which]->bracketed_chain;
	
	int count = symbol.size();
	
	bool success = false;
	if(count == 1) {
		Token *token = &symbol[0];
		if(allow_unspecified) {
			if(token->string_value == "nowhere") {
				location->type = Var_Location::Type::nowhere;
				success = true;
			}
		}
		if(!success) {
			std::string handle = token->string_value;
			auto reg = (*scope)[handle];
			if(reg) {
				if(reg->id.reg_type == Reg_Type::connection) {
					location->type = Var_Location::Type::connection;
					specific_loc->connection_id = reg->id;
					specific_loc->restriction   = Var_Loc_Restriction::below;  // This means that the target of the flux is the 'next' index along the connection.
					success = true;
				} else if (reg->id.reg_type == Reg_Type::loc) {
					auto loc = model->locs[reg->id];
					success = true;
					if(specific_loc)
						*specific_loc = loc->loc;
					else {
						*location = loc->loc;
						if(loc->loc.restriction != Var_Loc_Restriction::none)
							success = false;
					}
				}
			}
		}
		if(!bracketed.empty())
			success = false;
	} else if (count >= 2 && count <= max_var_loc_components) {
		location->type     = Var_Location::Type::located;
		for(int idx = 0; idx < count; ++idx)
			location->components[idx] = model->components.find_or_create(&symbol[idx], scope);
		location->n_components        = count;
		success = true;

		if(bracketed.size() == 2) {
			if(specific_loc) {
				// TODO: We should have some kind of check that only a target is top and a source is bottom (maybe, unless we implement it to work)
				specific_loc->connection_id = model->connections.find_or_create(&bracketed[0], scope);
				auto type = bracketed[1].string_value;
				if(type == "top")
					specific_loc->restriction = Var_Loc_Restriction::top;
				else if(type == "bottom")
					specific_loc->restriction = Var_Loc_Restriction::bottom;
				else
					success = false;
			} else
				success = false;
		} else if (!bracketed.empty())
			success = false;
		
	} else {
		symbol[0].print_error_header();
		fatal_error("Too many components in a variable location (max ", max_var_loc_components, " allowed).");
	}
	
	if(!success) {
		//TODO: Give a reason for why it failed (requires 5 different potential error messages :( ).
		symbol[0].print_error_header();
		fatal_error("Misformatted variable location in this context.");
	}
}

template<Reg_Type reg_type> Entity_Id
process_declaration(Mobius_Model *model, Decl_Scope *scope, Decl_AST *decl); //NOTE: this will be template specialized below.

template<> Entity_Id
process_declaration<Reg_Type::unit>(Mobius_Model *model, Decl_Scope *scope, Decl_AST *decl) {
	auto id   = model->units.find_or_create(&decl->handle_name, scope, nullptr, decl);
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
	
	if(!decl->bodies.empty()) {
		if(decl->type != Decl_Type::property) {
			decl->source_loc.print_error_header();
			fatal_error("Only properties can have default code, not quantities or compartments.");
		}
		// TODO : have to guard against clashes between different modules here! But that should be done in model_composition
		auto fun = static_cast<Function_Body_AST *>(decl->bodies[0]);
		component->default_code = fun->block;
		//component->code_scope = scope->parent_id;
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
		});
	} else
		fatal_error(Mobius_Error::internal, "Got an unrecognized type in parameter declaration processing.");
	
	auto id        = model->parameters.standard_declaration(scope, decl);
	auto parameter = model->parameters[id];
	
	if(decl->handle_name.string_value.count)
		parameter->symbol = decl->handle_name.string_value;   // NOTE: This should be ok since parameters can only be declared uniquely in one place
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
		auto body = static_cast<Function_Body_AST *>(decl->bodies[0]);   // Re-purposing function body for a simple list... TODO: We should maybe have a separate body type for that
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
		{Token_Type::quoted_string, Decl_Type::compartment},   // Could eventually make the last a vararg?
		{Token_Type::quoted_string, Decl_Type::quantity},
	}, false);
	
	auto id        = model->par_groups.standard_declaration(scope, decl);
	auto par_group = model->par_groups[id];
	
	if(which >= 1) {
		par_group->component = resolve_argument<Reg_Type::component>(model, scope, decl, 1);
		if(model->components[par_group->component]->decl_type == Decl_Type::property) {
			single_arg(decl, 1)->source_loc.print_error_header();
			fatal_error("A 'par_group' can not be attached to a 'property'.");
		}
	}
	
	auto body = static_cast<Decl_Body_AST *>(decl->bodies[0]);

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
	
	// All the parameters are visible in the module scope.
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
		});
	
	auto id =  model->functions.find_or_create(&decl->handle_name, scope, nullptr, decl);
	auto function = model->functions[id];
	
	for(auto arg : decl->args) {
		if(!arg->decl && arg->chain.size() != 1) {
			decl->source_loc.print_error_header();
			fatal_error("The arguments to a function declaration should be just single identifiers with or without units.");
		}
		if(arg->decl) {
			// It is a bit annoying to not use  resolve_argument here, but we don't want it to be associated to the handle in the scope.
			auto unit_id = model->units.find_or_create(nullptr, scope, nullptr, arg->decl);
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
	
	auto body = static_cast<Function_Body_AST *>(decl->bodies[0]);
	function->code = body->block;
	//function->code_scope = scope->parent_id;
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
	
	
	
	auto id  = model->vars.find_or_create(&decl->handle_name, scope, nullptr, decl);
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

	process_location_argument(model, scope, decl, 0, &var->var_location);	
	
	var->unit = resolve_argument<Reg_Type::unit>(model, scope, decl, 1);
	
	if(which == 1 || which == 3) {
		if(!var->var_location.is_dissolved()) {
			var->source_loc.print_error_header();
			fatal_error("Concentration units should only be provided for dissolved quantities.");
		}
		var->conc_unit = resolve_argument<Reg_Type::unit>(model, scope, decl, 2);
	} else
		var->conc_unit = invalid_entity_id;
	
	for(Body_AST *body : decl->bodies) {
		auto function = static_cast<Function_Body_AST *>(body);
		
		if(is_valid(&function->note)) {
			auto str = function->note.string_value;
			if(str == "initial" || str == "initial_conc") {
				if(var->initial_code) {
					function->opens_at.print_error_header();
					fatal_error("Declaration var more than one 'initial' or 'initial_conc' block.");
				}
				var->initial_code = function->block;
				var->initial_is_conc = (str == "initial_conc");
			} else if(str == "override" || str == "override_conc") {
				if(var->override_code) {
					function->opens_at.print_error_header();
					fatal_error("Declaration has more than one 'override' or 'override_conc' block.");
				}
				var->override_code = function->block;
				var->override_is_conc = (str == "override_conc");
			} else {
				function->note.print_error_header();
				fatal_error("Expected either no function body notes, 'initial' or 'override_conc'.");
			}
		} else
			var->code = function->block;
	}
	
	//var->code_scope = scope->parent_id;
	
	return id;
}

template<> Entity_Id
process_declaration<Reg_Type::flux>(Mobius_Model *model, Decl_Scope *scope, Decl_AST *decl) {
	
	match_declaration(decl,
		{
			{Token_Type::identifier, Token_Type::identifier, Decl_Type::unit, Token_Type::quoted_string},
		}, true, true, true);
	
	Token *name = single_arg(decl, 3);
	
	auto id   = model->fluxes.find_or_create(&decl->handle_name, scope, name, decl);
	auto flux = model->fluxes[id];
	
	flux->unit = resolve_argument<Reg_Type::unit>(model, scope, decl, 2);
	
	process_location_argument(model, scope, decl, 0, &flux->source, true, true);
	process_location_argument(model, scope, decl, 1, &flux->target, true, true);
	
	if(flux->source == flux->target && is_located(flux->source)) {
		decl->source_loc.print_error_header();
		fatal_error("The source and the target of a flux can't be the same.");
	}
	
	for(auto body : decl->bodies) {
		auto fun = static_cast<Function_Body_AST *>(body);
		if(!is_valid(&body->note))
			flux->code = fun->block;
		else {
			if(body->note.string_value != "no_carry") {
				body->note.print_error_header();
				fatal_error("Unrecognized note '", body->note.string_value, "' for a flux declaration.");
			}
			if(!is_located(flux->source)) {
				body->opens_at.print_error_header();
				fatal_error("A 'no_carry' block only makes sense if the source of the flux is specific (not 'nowhere').");
			}
			flux->no_carry_ast = fun->block;
		}
	}
	
	if(!flux->code) {
		decl->source_loc.print_error_header();
		fatal_error("This flux does not have a main code body.");
	}
			
	//flux->code_scope = scope->parent_id;
	
	return id;
}

template<> Entity_Id
process_declaration<Reg_Type::discrete_order>(Mobius_Model *model, Decl_Scope *scope, Decl_AST *decl) {
	match_declaration(decl, {{ }}, false);
	
	auto id    = model->discrete_orders.find_or_create(nullptr, scope, nullptr, decl);
	auto discr = model->discrete_orders[id];
	
	auto body = static_cast<Function_Body_AST *>(decl->bodies[0]);
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
		auto flux_id = model->fluxes.find_or_create(&ident->chain[0], scope, nullptr, nullptr);
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
process_declaration<Reg_Type::special_computation>(Mobius_Model *model, Decl_Scope *scope, Decl_AST *decl) {
	// NOTE: Since we disambiguate entities on their name right now, we can't let the name and function_name be the same in case you want to reuse the same function many times.
	//    could be fixed if/when we make the new module loading / scope system.
	match_declaration(decl, {{ Token_Type::quoted_string, Token_Type::quoted_string, Token_Type::identifier }}, false);
	
	auto id = model->special_computations.standard_declaration(scope, decl);
	auto comp = model->special_computations[id];
	
	comp->function_name = single_arg(decl, 1)->string_value;
	process_location_argument(model, scope, decl, 2, &comp->target);
	
	auto body = static_cast<Function_Body_AST *>(decl->bodies[0]);
	bool success = true;
	for(auto expr : body->block->exprs) {
		if(expr->type != Math_Expr_Type::identifier) {
			body->opens_at.print_error_header();
			fatal_error("A 'special_computation' body should just contain a list of identifiers.");
		}
	}
	comp->code = body->block;
	//comp->code_scope = scope->parent_id;
	
	return id;
}

template<> Entity_Id
process_declaration<Reg_Type::connection>(Mobius_Model *model, Decl_Scope *scope, Decl_AST *decl) {
	int which = match_declaration(decl,
	{
		{Token_Type::quoted_string},
		{Token_Type::quoted_string, Token_Type::identifier},
	});
	
	auto id         = model->connections.standard_declaration(scope, decl);
	auto connection = model->connections[id];
	
	// TODO: actually "compile" a regex and support more types
	// TODO: we have to check somewhere in post that the handles are indeed a compartments (not quantity or property). This will probably be handled when we start to process regexes (?) (Although later we should allow connections between quantities also).
	
	if(which == 1) {
		String_View structure_type = single_arg(decl, 1)->string_value;
		if(structure_type == "directed_tree")	connection->type = Connection_Type::directed_tree;
		else if(structure_type == "all_to_all") connection->type = Connection_Type::all_to_all;
		else if(structure_type == "grid1d")     connection->type = Connection_Type::grid1d;
		else {
			single_arg(decl, 1)->print_error_header();
			fatal_error("Unsupported connection structure type '", structure_type, "'.");
		}
	}
	
	connection->components.clear(); // NOTE: Needed since this could be a re-declaration.
	
	bool success = false;
	auto expr = static_cast<Regex_Body_AST *>(decl->bodies[0])->expr;
	if (expr->type == Math_Expr_Type::unary_operator) {
		char oper_type = (char)static_cast<Unary_Operator_AST *>(expr)->oper;
		if(oper_type != '*') {
			expr->source_loc.print_error_header();
			fatal_error("We currently only support the '*' operator in connection regexes.");
		}
		
		expr = expr->exprs[0];
		
		if(expr->type == Math_Expr_Type::regex_identifier) {
			auto ident = static_cast<Regex_Identifier_AST *>(expr);
			auto compartment_id = model->components.find_or_create(&ident->ident, scope);
			connection->components.push_back(compartment_id);
			success = true;
		} else if(expr->type == Math_Expr_Type::regex_or_chain) {
			bool success2 = true;
			for(auto expr2 : expr->exprs) {
				if(expr2->type != Math_Expr_Type::regex_identifier) {
					success2 = false;
					break;
				}
				auto ident = static_cast<Regex_Identifier_AST *>(expr2);
				auto compartment_id = model->components.find_or_create(&ident->ident, scope);
				connection->components.push_back(compartment_id);
			}
			success = success2;
		}
	}
	
	if(!success) {
		expr->source_loc.print_error_header();
		fatal_error("Temporary: This is not a supported regex format for connections yet.");
	}
	
	if(connection->components.empty()) {
		expr->source_loc.print_error_header();
		fatal_error("At least one component must be involved in a connection.");
	}
	
	if((connection->type == Connection_Type::all_to_all || connection->type == Connection_Type::grid1d)
		&& connection->components.size() > 1) {
		expr->source_loc.print_error_header();
		fatal_error("Connections of type all_to_all and grid1d are only supported for one component type at a time.");
	}
	
	return id;
}

template<> Entity_Id
process_declaration<Reg_Type::loc>(Mobius_Model *model, Decl_Scope *scope, Decl_AST *decl) {
	match_declaration(decl, {{Token_Type::identifier}});
	
	auto id  = model->locs.find_or_create(&decl->handle_name, scope, nullptr, decl);
	auto loc = model->locs[id];
	
	process_location_argument(model, scope, decl, 0, &loc->loc, true, true);
	
	//loc->scope_id = scope->parent_id;
	
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
		
		match_declaration(lib->decl, {{Token_Type::quoted_string}}, false); // REFACTOR. matching is already done in the load_top_decl_from_file
		
		auto body = static_cast<Decl_Body_AST *>(lib->decl->bodies[0]);
		
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
	
	// TODO: It is a bit superfluous to process the arguments and version of the template every time it is specialized.
	
	auto decl = mod_temp->decl;
	match_declaration(decl,
		{
			{Token_Type::quoted_string, Decl_Type::version},
			{Token_Type::quoted_string, Decl_Type::version, {true}}
		}, false);
	
	auto version_decl = decl->args[1]->decl;
	
	match_declaration(version_decl, {{Token_Type::integer, Token_Type::integer, Token_Type::integer}}, false);
	
	mod_temp->name                 = single_arg(decl, 0)->string_value;
	mod_temp->version.major        = single_arg(version_decl, 0)->val_int;
	mod_temp->version.minor        = single_arg(version_decl, 1)->val_int;
	mod_temp->version.revision     = single_arg(version_decl, 2)->val_int;
	
	auto body = static_cast<Decl_Body_AST *>(decl->bodies[0]);
	
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
	module_id = model->modules.find_or_create(nullptr, model_scope, spec_name_token, nullptr);
	auto module = model->modules[module_id];
	// Ouch, this is a bit hacky, but it is to avoid the problem that Decl_Type::module is tied to Reg_Type::module_template .
	// Maybe we should instead have another flag on it?
	module->has_been_declared = true;
	
	module->scope.parent_id = module_id;
	module->template_id = template_id;
	module->scope.import(model->global_scope);
	
	if(import_scope)
		module->scope.import(*model_scope, &load_loc);
	
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
		module->scope.add_local(handle, arg->decl->source_loc, load_id);
	}
	
	// TODO: Order of processing could probably be simplified when the new module load system is finished.
	for(Decl_AST *child : body->child_decls) {
		switch(child->type) {
			
			//case Decl_Type::compartment :
			//case Decl_Type::quantity :
			case Decl_Type::property : {
				process_declaration<Reg_Type::component>(model, &module->scope, child);
			} break;
			
			case Decl_Type::connection : {
				process_declaration<Reg_Type::connection>(model, &module->scope, child);
			} break;
		}
	}
	
	for(Decl_AST *child : body->child_decls) {
		switch(child->type) {
			case Decl_Type::load : {
				process_load_library_declaration(model, child, module_id, mod_temp->normalized_path);
			} break;
			
			case Decl_Type::unit : {
				process_declaration<Reg_Type::unit>(model, &module->scope, child);
			};
			
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
			
			case Decl_Type::special_computation : {
				process_declaration<Reg_Type::special_computation>(model, &module->scope, child);
			} break;
			
			case Decl_Type::property :
			case Decl_Type::connection : {  // already processed above, or will be processed below
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
	
	int which = match_declaration(decl, {
		{Token_Type::quoted_string},
	});
	auto id        = model->index_sets.standard_declaration(scope, decl);
	
	// NOTE: the reason we do it this way is to force the higher index set to have a smaller Entity_Id. This is very useful in processing code later.
	if(decl->bodies.size() > 0) {
		auto body = static_cast<Decl_Body_AST *>(decl->bodies[0]);
		for(auto child : body->child_decls) {
			if(child->type != Decl_Type::index_set) {
				child->source_loc.print_error_header();
				fatal_error("Only 'index_set' declarations are allowed in the body of another index_set declaration.");
			}
			auto sub_id = process_declaration<Reg_Type::index_set>(model, scope, child);
			model->index_sets[sub_id]->sub_indexed_to = id;
		}
	}
	return id;
}

template<> Entity_Id
process_declaration<Reg_Type::solver>(Mobius_Model *model, Decl_Scope *scope, Decl_AST *decl) {
	int which = match_declaration(decl, {
		{Token_Type::quoted_string, Token_Type::quoted_string, Token_Type::real},
		{Token_Type::quoted_string, Token_Type::quoted_string, Token_Type::real, Token_Type::real},
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
	
	solver->h = single_arg(decl, 2)->double_value();
	if(which == 1)
		solver->hmin = single_arg(decl, 3)->double_value();
	else
		solver->hmin = 0.01 * solver->h;
	
	return id;
}

template<> Entity_Id
process_declaration<Reg_Type::solve>(Mobius_Model *model, Decl_Scope *scope, Decl_AST *decl) {

	// TODO: We may not need a special "solve" registry, we could just store these in the solver registry.

	match_declaration(decl, {{Decl_Type::solver, {Token_Type::identifier, true}}}, false);
	
	auto id = model->solves.find_or_create(nullptr, scope, nullptr, decl);
	auto solve = model->solves[id];
	
	solve->solver = resolve_argument<Reg_Type::solver>(model, scope, decl, 0);
	for(int idx = 1; idx < decl->args.size(); ++idx) {
		Var_Location loc;
		process_location_argument(model, scope, decl, idx, &loc);
		
		if(loc.is_dissolved()) {
			decl->args[idx]->chain[0].source_loc.print_error_header(Mobius_Error::model_building);
			fatal_error("For now we don't allow specifying solvers for dissolved substances. Instead they are given the solver of the variable they are dissolved in.");
		}
		
		solve->locs.push_back(loc);
	}

	return id;
}

void
process_distribute_declaration(Mobius_Model *model, Decl_Scope *scope, Decl_AST *decl) {
	match_declaration(decl,
	{
		{Decl_Type::compartment, {Decl_Type::index_set, true}},
		{Decl_Type::quantity, {Decl_Type::index_set, true}},
	}, false);
	
	auto comp_id   = resolve_argument<Reg_Type::component>(model, scope, decl, 0);
	auto component = model->components[comp_id];
	
	if(component->decl_type == Decl_Type::property) {
		decl->source_loc.print_error_header(Mobius_Error::model_building);
		fatal_error("Only compartments and quantities can be distributed, not properties");
	}
	
	for(int idx = 1; idx < decl->args.size(); ++idx) {
		auto id = resolve_argument<Reg_Type::index_set>(model, scope, decl, idx);
		auto index_set = model->index_sets[id];
		if(is_valid(index_set->sub_indexed_to)) {
			if(idx == 0 || component->index_sets[idx-1] != index_set->sub_indexed_to) {
				index_set->source_loc.print_error_header();
				fatal_error("This index set is sub-indexed to another index set, but it doesn't immediately follow that other index set in the 'distribute' declaration.");
			}
		}
		component->index_sets.push_back(id);
	}
}

void
process_aggregation_weight_declaration(Mobius_Model *model, Decl_Scope *scope, Decl_AST *decl) {
	match_declaration(decl, {{Decl_Type::compartment, Decl_Type::compartment}}, false);
	
	auto from_comp = resolve_argument<Reg_Type::component>(model, scope, decl, 0);
	auto to_comp   = resolve_argument<Reg_Type::component>(model, scope, decl, 1);
	
	if(model->components[from_comp]->decl_type != Decl_Type::compartment || model->components[to_comp]->decl_type != Decl_Type::compartment) {
		decl->source_loc.print_error_header(Mobius_Error::model_building);
		fatal_error("Aggregations can only be declared between compartments");
	}
	
	//TODO: some guard against overlapping / contradictory declarations.
	auto function = static_cast<Function_Body_AST *>(decl->bodies[0]);
	model->components[from_comp]->aggregations.push_back({to_comp, function->block, scope->parent_id});
}

void
process_unit_conversion_declaration(Mobius_Model *model, Decl_Scope *scope, Decl_AST *decl) {
	match_declaration(decl, {{Token_Type::identifier, Token_Type::identifier}}, false);
	
	Flux_Unit_Conversion_Data data = {};
	
	process_location_argument(model, scope, decl, 0, &data.source);
	process_location_argument(model, scope, decl, 1, &data.target);
	data.code = static_cast<Function_Body_AST *>(decl->bodies[0])->block;
	data.scope_id = scope->parent_id;
	
	//TODO: some guard against overlapping / contradictory declarations.
	if(data.source == data.target) {
		decl->source_loc.print_error_header();
		fatal_error("The source and target of a 'unit_conversion' should not be the same.");
	}
	
	model->components[data.source.first()]->unit_convs.push_back(data);
}

bool
load_model_extensions(File_Data_Handler *handler, Decl_AST *from_decl, std::unordered_set<std::string> &loaded_paths, std::vector<std::pair<std::string, Decl_AST *>> &loaded_decls, String_View rel_path) {
	
	auto body = static_cast<Decl_Body_AST *>(from_decl->bodies[0]);
	
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
		for(auto &pair : loaded_decls) {
			if(pair.first == normalized_path) {
				found = true;
				break;
			}
		}
		if(found)
			delete extend_model; // TODO: see above
		else {
			loaded_decls.push_back( { normalized_path, extend_model } );
		
			// Load extensions of extensions.
			bool success = load_model_extensions(handler, extend_model, loaded_paths_sub, loaded_decls, normalized_path);
			if(!success) {
				error_print(extend_file_name, "\n");
				return false;
			}
		}
	}
	
	return true;
}

void
load_config(Mobius_Model *model, String_View config) {
	auto file_data = model->file_handler.load_file(config);
	//warning_print("Loaded config\n");
	Token_Stream stream(config, file_data);
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
			model->mobius_base_path = single_arg(decl, 1)->string_value;
			//warning_print("Loaded base path ", model->mobius_base_path, "\n");
			struct stat info;
			if(stat(model->mobius_base_path.data(), &info) != 0 || !(info.st_mode & S_IFDIR)) {
				single_arg(decl, 0)->print_error_header();
				fatal_error("The path \"", model->mobius_base_path, "\" is not a valid directory path.");
			}
		} else {
			decl->source_loc.print_error_header();
			fatal_error("Unknown config option \"", item, "\".");
		}
		delete decl;
	}
	model->file_handler.unload(config);
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
			if(!reg) {
				token.print_error_header();
				fatal_error("The handle '", token.string_value, "' was not declared in this scope.");
			}
			load_args.push_back(reg->id); // TODO: Check on what types of objects are allowed to be passed?
		}
	}
}


Mobius_Model *
load_model(String_View file_name, String_View config) {
	
	Mobius_Model *model = new Mobius_Model();
	
	load_config(model, config);
	
	auto decl = read_model_ast_from_file(&model->file_handler, file_name);
	model->main_decl  = decl;
	model->model_name = single_arg(decl, 0)->string_value;
	model->path       = file_name;
	
	auto body = static_cast<Decl_Body_AST *>(decl->bodies[0]);
	
	if(body->doc_string.string_value.count)
		model->doc_string = body->doc_string.string_value;
	
	register_intrinsics(model);

	std::unordered_set<std::string> loaded_files = { file_name };
	std::vector<std::pair<std::string, Decl_AST *>> extend_models = { { file_name, decl} };
	bool success = load_model_extensions(&model->file_handler, decl, loaded_files, extend_models, file_name);
	if(!success)
		mobius_error_exit();
	
	std::reverse(extend_models.begin(), extend_models.end());

	auto scope = &model->model_decl_scope;
	
	// We need to process these first since some other declarations rely on these existing, such as par_group.
	for(auto &extend : extend_models) {
		auto ast = extend.second;
		auto body = static_cast<Decl_Body_AST *>(ast->bodies[0]);
		
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
				
				case Decl_Type::loc : {
					process_declaration<Reg_Type::loc>(model, scope, child);
				} break;
				
				default : {
				} break;
			}
		}
	}
	
	
	for(auto &extend : extend_models) {
		auto ast = extend.second;
		auto body = static_cast<Decl_Body_AST *>(ast->bodies[0]);
		for(Decl_AST *child : body->child_decls) {
			if(child->type == Decl_Type::par_group)
				process_declaration<Reg_Type::par_group>(model, scope, child);
			else if(child->type == Decl_Type::index_set) // Process index sets before distribute() because we need info about what we distribute over.
				process_declaration<Reg_Type::index_set>(model, scope, child);
		}
	}
	
	// NOTE: process loads before the rest of the model scope declarations. (may no longer be necessary).
	for(auto &extend : extend_models) {
		auto model_path = extend.first;
		auto ast = extend.second;
		auto body = static_cast<Decl_Body_AST *>(ast->bodies[0]);
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
					auto template_id = model->module_templates.find_or_create(nullptr, scope, nullptr, child);
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
		auto ast = extend.second;
		auto body = static_cast<Decl_Body_AST *>(ast->bodies[0]);
		
		for(Decl_AST *child : body->child_decls) {

			switch (child->type) {
				
				case Decl_Type::distribute : {
					process_distribute_declaration(model, scope, child);
				} break;
				
				case Decl_Type::solver : {
					process_declaration<Reg_Type::solver>(model, scope, child);
				} break;
				
				case Decl_Type::solve : {
					process_declaration<Reg_Type::solve>(model, scope, child);
				} break;
				
				case Decl_Type::aggregation_weight : {
					process_aggregation_weight_declaration(model, scope, child);
				} break;
				
				case Decl_Type::unit_conversion : {
					process_unit_conversion_declaration(model, scope, child);
				} break;
				
				case Decl_Type::par_group :
				case Decl_Type::index_set :
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

// NOTE: would like to just have an ostream& operator<< on the Var_Location, but it needs to reference the scope to get the names..
void
error_print_location(Decl_Scope *scope, const Var_Location &loc) {
	for(int idx = 0; idx < loc.n_components; ++idx)
		error_print((*scope)[loc.components[idx]], idx == loc.n_components-1 ? "" : ".");
}

void
debug_print_location(Decl_Scope *scope, const Var_Location &loc) {
	for(int idx = 0; idx < loc.n_components; ++idx)
		warning_print((*scope)[loc.components[idx]], idx == loc.n_components-1 ? "" : ".");
}