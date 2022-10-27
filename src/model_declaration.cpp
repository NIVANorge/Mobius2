
#include "model_declaration.h"

void
Decl_Scope::add_local(const std::string &handle, Source_Location source_loc, Entity_Id id) {
	static std::string reserved[] = {
		#define ENUM_VALUE(name, _a, _b) #name,
		#include "decl_types.incl"
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
				error_print("There is a name conflict with the handle '", handle, "'. It was declared separately in the following two locations and loaded into this scope: ");
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

template<Reg_Type reg_type> Entity_Id
Registry<reg_type>::find_or_create(Token *handle, Decl_Scope *scope, Token *decl_name, Decl_AST *decl) {
	
	Entity_Id result_id = invalid_entity_id;
	
	bool found_in_scope = false;
	if(is_valid(handle) && scope) {
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
				decl->location.print_error_header();
				error_print("Re-declaration of entity '", handle->string_value, "'. It was already declared here: ");
				entity->source_loc.print_error();
				fatal_error();
			}
		}
	}
	
	bool linked_to_universal = false;
	if(is_valid(decl_name) && decl) {
		if(decl->type == Decl_Type::compartment || decl->type == Decl_Type::quantity || decl->type == Decl_Type::property || decl->type == Decl_Type::neighbor
			|| decl->type == Decl_Type::par_real || decl->type == Decl_Type::par_bool || decl->type == Decl_Type::par_int || decl->type == Decl_Type::par_enum) {
			//TODO: it may or may not be a good idea to have this functionality for properties. Let's find out how it works out in practice.
			if(is_valid(result_id)) {
				fatal_error(Mobius_Error::internal, "We assigned an id to a '", name(decl->type), "' entity \"", decl_name->string_value, "\" too early without linking it to its universal version using its id.");
			}
			
			std::string name = decl_name->string_value;
			auto find = name_to_id.find(name);
			if(find != name_to_id.end()) {
				result_id = find->second;
				linked_to_universal = true;
			}
		}
	}
	
	//TODO: If decl_name is valid, some types are identified by the name, and can be looked up, but we need a better system for it.
		//TODO: obviously also check for name clash.
	
	// It was not previously referenced. Create it instead.
	if(!is_valid(result_id)) {
		result_id.reg_type  = reg_type;
		result_id.id        = (s32)registrations.size();
		registrations.push_back({});
		auto &registration = registrations[result_id.id];
		registration.has_been_declared = false;
		if(is_valid(handle))
			registration.source_loc = handle->location;
	}
	
	auto &registration = registrations[result_id.id];
	
	if(decl && !linked_to_universal) {
		if(get_reg_type(decl->type) != reg_type) {
			decl->location.print_error_header(Mobius_Error::internal);
			fatal_error("Registering declaration with a mismatching type.");
		}
		
		registration.source_loc = decl->location;
		registration.has_been_declared = true;
		registration.decl_type = decl->type;
		
		if(decl_name) {
			registration.name = decl_name->string_value;
			//TODO: NOTE: for now names are globally scoped. This is necessary for some systems to work, but could cause problems in larger models. Make a better system later?
			auto find = name_to_id.find(registration.name);
			if(find != name_to_id.end()) {
				decl->location.print_error_header();
				error_print("The name \"", registration.name, "\" was already used for another '", name(reg_type), "' declared here: ");
				registrations[find->second.id].source_loc.print_error();
				mobius_error_exit();
			}
			name_to_id[registration.name] = result_id;
		}
	}
	
	if(is_valid(handle) && scope) {
		std::string hh = handle->string_value;
		if(!found_in_scope)
			scope->add_local(hh, registration.source_loc, result_id);
		else if(decl) // Update source location to be the declaration location.
			(*scope)[hh]->source_loc = registration.source_loc;
	} else if(scope)
		scope->add_local("", registration.source_loc, result_id);
	
	return result_id;
}

template<Reg_Type reg_type> Entity_Id
Registry<reg_type>::create_internal(const std::string &handle, Decl_Scope *scope, const std::string &name, Decl_Type decl_type) {
	Source_Location internal = {};
	internal.type      = Source_Location::Type::internal;
	
	Entity_Id result_id;
	result_id.reg_type = reg_type;
	result_id.id = (s32)registrations.size();
	registrations.push_back({});
	auto &registration = registrations[result_id.id];
	
	registration.name = name;
	registration.source_loc = internal;
	registration.has_been_declared = true;
	registration.decl_type = decl_type;
	
	name_to_id[name] = result_id;
	
	if(scope)
		scope->add_local(handle, internal, result_id);
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
		case Reg_Type::module :                   return &modules;
		case Reg_Type::library :                  return &libraries;
		case Reg_Type::component :                return &components;
		case Reg_Type::has :                      return &hases;
		case Reg_Type::flux :                     return &fluxes;
		case Reg_Type::function :                 return &functions;
		case Reg_Type::index_set :                return &index_sets;
		case Reg_Type::solver :                   return &solvers;
		case Reg_Type::solve :                    return &solves;
		case Reg_Type::constant :                 return &constants;
		case Reg_Type::neighbor :                 return &neighbors;
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
	fatal_error(Mobius_Error::internal, "Tried to look up the scope belonging to an id that is not a library or module.");
	return nullptr;
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
		if(max_sub_chain_size > 0 && arg->sub_chain.size() > max_sub_chain_size) {
			arg->sub_chain[0].print_error_header();
			fatal_error("Did not expect a chained declaration.");
		}
		return model->registry(expected_type)->find_or_create(&arg->sub_chain[0], scope);
	}
	return invalid_entity_id;
}


Decl_AST *
read_model_ast_from_file(File_Data_Handler *handler, String_View file_name, String_View rel_path = {}, String_View *normalized_path_out = nullptr) {
	String_View model_data = handler->load_file(file_name, {}, rel_path, normalized_path_out);
	
	Token_Stream stream(file_name, model_data);
	Decl_AST *decl = parse_decl(&stream);
	if(decl->type != Decl_Type::model || stream.peek_token().type != Token_Type::eof) {
		decl->location.print_error_header();
		fatal_error("Model files should only have a single model declaration in the top scope.");
	}
	
	match_declaration(decl, {{Token_Type::quoted_string}}, 0, false);
	
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
	
	auto dimless_id = model->units.create_internal("na", nullptr, "dimensionless", Decl_Type::unit); // NOTE: this is not exported to the scope. Just have it as a unit for pi.
	auto pi_id = model->constants.create_internal("pi", global, "π", Decl_Type::constant);
	model->constants[pi_id]->value = 3.14159265358979323846;
	model->constants[pi_id]->unit = dimless_id;
	
	auto mod_scope = &model->model_decl_scope;
	
	//TODO: We should actually make it so that these can't be referenced in code (i.e. not have a handle)
	auto system_id = model->par_groups.create_internal("system", mod_scope, "System", Decl_Type::par_group);
	auto start_id  = model->parameters.create_internal("start_date", mod_scope, "Start date", Decl_Type::par_datetime);
	auto end_id    = model->parameters.create_internal("end_date", mod_scope, "End date", Decl_Type::par_datetime);
	
	auto system = model->par_groups[system_id];
	auto start  = model->parameters[start_id];
	auto end    = model->parameters[end_id];
	
	system->parameters.push_back(start_id);
	system->parameters.push_back(end_id);
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
}

Entity_Id
load_top_decl_from_file(Mobius_Model *model, Source_Location from, Decl_Scope *scope, String_View rel_path, const std::string &decl_name, Decl_Type type) {
	
	String_View normalized_path;
	auto path_ptr = &normalized_path;
	
	String_View file_data = model->file_handler.load_file(rel_path, from, model->path, path_ptr);
	
	//warning_print("Try to load ", decl_name, " from ", *path_ptr, "\n");
	
	bool already_parsed_file = false;
	Entity_Id result = invalid_entity_id;
	auto find_file = model->parsed_decls.find(*path_ptr);
	//warning_print("Look for file ", *path_ptr, "\n");
	if(find_file != model->parsed_decls.end()) {
		already_parsed_file = true;
		//warning_print("Already parsed file\n");
		auto find_decl = find_file->second.find(decl_name);
		if(find_decl != find_file->second.end())
			result = find_decl->second;
	}

	if(!already_parsed_file) {
		Token_Stream stream(rel_path, file_data);
			
		while(true) {
			if(stream.peek_token().type == Token_Type::eof) break;
			Decl_AST *decl = parse_decl(&stream);
			if(decl->type != Decl_Type::module && decl->type != Decl_Type::library) {
				decl->location.print_error_header();
				fatal_error("Module files should only have modules or libraries in the top scope. Encountered a ", name(decl->type), ".");
			}
			if(!(decl->args.size() >= 1 && decl->args[0]->sub_chain.size() >= 1 && decl->args[0]->sub_chain[0].type == Token_Type::quoted_string)) {
				decl->location.print_error_header();
				fatal_error("Encountered a top-level declaration without a name.");
			}
			std::string found_name = decl->args[0]->sub_chain[0].string_value;
			Entity_Id id = invalid_entity_id;
			
			if(decl->type == Decl_Type::library) {
				id = model->libraries.standard_declaration(scope, decl);
				auto lib = model->libraries[id];
				lib->has_been_processed = false;
				lib->decl = decl;
				lib->scope.parent_id = id;
			} else if (decl->type == Decl_Type::module) {
				id = model->modules.standard_declaration(scope, decl);
				auto mod = model->modules[id];
				mod->has_been_processed = false;
				mod->decl = decl;
				mod->scope.parent_id = id;
			}
			model->parsed_decls[*path_ptr][found_name] = id;
			if(decl_name == found_name) {
				result = id;
				if(decl->type != type) {
					decl->location.print_error_header();
					fatal_error(Mobius_Error::parsing, "The declaration '", decl_name, "' is of type ", name(decl->type), ", but was loaded as a ", name(type), ".");
				}
			}
		}
	}
	
	if(!is_valid(result))
		fatal_error(Mobius_Error::parsing, "Could not find the ", name(type), " '", decl_name, "' in the file ", rel_path, " .");
	
	return result;
}

void
process_location_argument(Mobius_Model *model, Decl_Scope *scope, Decl_AST *decl, int which, Var_Location *location, bool allow_unspecified) {
	if(decl->args[which]->decl) {
		decl->args[which]->decl->location.print_error_header();
		fatal_error("Expected a single identifier or a .-separated chain of identifiers.");
	}
	std::vector<Token> &symbol = decl->args[which]->sub_chain;
	int count = symbol.size();
	if(count == 1 && allow_unspecified) {
		Token *token = &symbol[0];
		if(token->string_value == "nowhere")
			location->type = Var_Location::Type::nowhere;
		else if(token->string_value == "out") {
			if(which == 0) {
				token->print_error_header();
				fatal_error("The source of a flux can never be 'out'.");
			}
			location->type = Var_Location::Type::out;
		} else {
			token->print_error_header();
			fatal_error("Invalid variable location.");
		}
	} else if (count >= 2 && count <= max_var_loc_components) {
		if(decl->args[which]->chain_sep != '.') {
			symbol[0].print_error_header();
			fatal_error("Expected a single identifier or a .-separated chain of identifiers.");
		}
		location->type     = Var_Location::Type::located;
		for(int idx = 0; idx < count; ++idx)
			location->components[idx] = model->components.find_or_create(&symbol[idx], scope);
		location->n_components        = count;
	} else {
		//TODO: Give a reason for why it failed
		symbol[0].print_error_header();
		fatal_error("Invalid variable location.");
	}
}

template<Reg_Type reg_type> Entity_Id
process_declaration(Mobius_Model *model, Decl_Scope *scope, Decl_AST *decl); //NOTE: this will be template specialized below.

template<> Entity_Id
process_declaration<Reg_Type::unit>(Mobius_Model *model, Decl_Scope *scope, Decl_AST *decl) {
	auto id   = model->units.find_or_create(&decl->handle_name, scope, nullptr, decl);
	auto unit = model->units[id];
	
	// TODO: we could de-duplicate based on the standard form.
	set_unit_data(unit->data, decl);
	
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
		}, 0, true, -1);
		
	auto id          = model->components.standard_declaration(scope, decl);
	auto component   = model->components[id];
	
	if(!decl->bodies.empty()) {
		if(decl->type != Decl_Type::property) {
			decl->location.print_error_header();
			fatal_error("Only properties can have default code, not quantities.");
		}
		if(decl->bodies.size() > 1) {
			decl->location.print_error_header();
			fatal_error("Expected at most one body for property declaration.");
		}
		// TODO : have to guard against clashes between different modules here!
		auto fun = reinterpret_cast<Function_Body_AST *>(decl->bodies[0]);
		component->default_code = fun->block;
		component->code_scope = scope->parent_id;
	}
	/*
	if(which == 1)
		component->unit = resolve_argument<Reg_Type::unit>(module, decl, 1);
	else
		component->unit = invalid_entity_id;
	*/
	return id;
}

/*
template<> Entity_Id
process_declaration<Reg_Type::compartment>(Mobius_Model *model, Decl_Scope *scope, Decl_AST *decl) {
	Entity_Id id = model->compartments.standard_declaration(scope, decl);

	return id;
}

template<> inline Entity_Id
process_declaration<Reg_Type::property_or_quantity>(Mobius_Model *model, Decl_Scope *scope, Decl_AST *decl) {
	//TODO: If we want to allow units on this declaration directly, we have to check for mismatches between decls in different modules.
	// For now it is safer to just have it on the "has", but we could go over this later and see if we could make it work.
	int which = match_declaration(decl,
		{
			{Token_Type::quoted_string},
			//{Token_Type::quoted_string, Decl_Type::unit},
		}, 0, true, -1);
		
	auto id       = model->properties_and_quantities.standard_declaration(scope, decl);
	auto property = model->properties_and_quantities[id];
	
	if(!decl->bodies.empty()) {
		if(decl->type != Decl_Type::property) {
			decl->location.print_error_header();
			fatal_error("Only properties can have default code, not quantities.");
		}
		if(decl->bodies.size() > 1) {
			decl->location.print_error_header();
			fatal_error("Expected at most one body for property declaration.");
		}
		// TODO : have to guard against clashes between different modules here!
		auto fun = reinterpret_cast<Function_Body_AST *>(decl->bodies[0]);
		property->default_code = fun->block;
		property->code_scope = scope->parent_id;
	}
	return id;
}
*/

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
	
	if(decl->handle_name.string_value)
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
		auto body = reinterpret_cast<Function_Body_AST *>(decl->bodies[0]);   // Re-purposing function body for a simple list... TODO: We should maybe have a separate body type for that
		for(auto expr : body->block->exprs) {
			if(expr->type != Math_Expr_Type::identifier_chain) {
				expr->location.print_error_header();
				fatal_error("Expected a list of identifiers only.");
			}
			auto ident = reinterpret_cast<Identifier_Chain_AST *>(expr);
			if(ident->chain.size() != 1) {
				expr->location.print_error_header();
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
	match_declaration(decl, {{Token_Type::quoted_string}}, 1, false);
	
	//TODO: Do we always need to require that a par group is tied to a component?
	
	auto id        = model->par_groups.standard_declaration(scope, decl);
	auto par_group = model->par_groups[id];
	
	par_group->component = model->components.find_or_create(&decl->decl_chain[0], scope);
	// TODO: It should be checked that the component is not a property..
	
	auto body = reinterpret_cast<Decl_Body_AST *>(decl->bodies[0]);

	for(Decl_AST *child : body->child_decls) {
		if(child->type == Decl_Type::par_real || child->type == Decl_Type::par_int || child->type == Decl_Type::par_bool || child->type == Decl_Type::par_enum) {
			auto par_id = process_declaration<Reg_Type::parameter>(model, scope, child);
			par_group->parameters.push_back(par_id);
			model->parameters[par_id]->par_group = id;
		} else {
			child->location.print_error_header();
			fatal_error("Did not expect a declaration of type '", name(child->type), "' inside a par_group declaration.");
		}
	}
	//TODO: process doc string(s) in the par group?
	
	return id;
}

template<> Entity_Id
process_declaration<Reg_Type::function>(Mobius_Model *model, Decl_Scope *scope, Decl_AST *decl) {
	match_declaration(decl, {{{Token_Type::identifier, true}}});
	
	auto id =  model->functions.find_or_create(&decl->handle_name, scope, nullptr, decl);
	auto function = model->functions[id];
	
	for(auto arg : decl->args) {
		if(arg->decl || arg->sub_chain.size() != 1) {
			decl->location.print_error_header();
			fatal_error("The arguments to a function declaration should be just single identifiers.");
		}
		function->args.push_back(arg->sub_chain[0].string_value);
	}
	
	for(int idx = 1; idx < function->args.size(); ++idx) {
		for(int idx2 = 0; idx2 < idx; ++idx2) {
			if(function->args[idx] == function->args[idx2]) {
				decl->args[idx]->sub_chain[0].print_error_header();
				fatal_error("Duplicate argument name '", function->args[idx], "' in function declaration.");
			}
		}
	}
	
	auto body = reinterpret_cast<Function_Body_AST *>(decl->bodies[0]);
	function->code = body->block;
	function->code_scope = scope->parent_id;
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
process_declaration<Reg_Type::has>(Mobius_Model *model, Decl_Scope *scope, Decl_AST *decl) {
	int which = match_declaration(decl,
		{
			{Decl_Type::property},
			{Decl_Type::property, Decl_Type::unit},
			{Decl_Type::property, Token_Type::quoted_string},
			{Decl_Type::property, Decl_Type::unit, Token_Type::quoted_string},
		},
		-1, true, -1, true);
	
	Token *name = nullptr;
	if(which == 2) name = single_arg(decl, 1);
	else if(which == 3) name = single_arg(decl, 2);
	
	auto id  = model->hases.find_or_create(&decl->handle_name, scope, name, decl);
	auto has = model->hases[id];
	
	int chain_size = decl->decl_chain.size();
	if(chain_size == 0 || chain_size > max_var_loc_components - 1) {
		decl->decl_chain.back().print_error_header();
		fatal_error("A 'has' declaration must either be of the form compartment.has(property_or_quantity) or compartment.<chain>.has(quantity) where <chain> is a .-separated chain of quantity handles that is no more than ", max_var_loc_components-2, " long.");
	}
	
	// TODO: can eventually be tied to just a quantity not only a compartment or compartment.quantities
	has->var_location.type = Var_Location::Type::located;
	for(int idx = 0; idx < chain_size; ++idx)
		has->var_location.components[idx] = model->components.find_or_create(&decl->decl_chain[idx], scope);
	has->var_location.n_components = chain_size + 1;
	has->var_location.components[chain_size] = resolve_argument<Reg_Type::component>(model, scope, decl, 0);
	
	if(which == 1 || which == 3)
		has->unit = resolve_argument<Reg_Type::unit>(model, scope, decl, 1);
	else
		has->unit = invalid_entity_id;
	
	for(Body_AST *body : decl->bodies) {
		auto function = reinterpret_cast<Function_Body_AST *>(body);
		if(function->modifiers.size() > 1) {
			function->opens_at.print_error_header();
			fatal_error("Bodies belonging to declarations of type 'has' can only have one modifier.");
		} else if(function->modifiers.size() == 1) {
			auto str = function->modifiers[0].string_value;
			if(str == "initial" || str == "initial_conc") {
				if(has->initial_code) {
					function->opens_at.print_error_header();
					fatal_error("Declaration has more than one '.initial' or '.initial_conc' block.");
				}
				has->initial_code = function->block;
				has->initial_is_conc = (str == "initial_conc");
			} else if(str == "override" || str == "override_conc") {
				if(has->override_code) {
					function->opens_at.print_error_header();
					fatal_error("Declaration has more than one '.override' or '.override_conc' block.");
				}
				has->override_code = function->block;
				has->override_is_conc = (str == "override_conc");
			} else {
				function->opens_at.print_error_header();
				fatal_error("Expected either no function body tags, '.initial' or '.override_conc'.");   //TODO: should maybe come up with a better name than "tag" (?)
			}
		} else {
			if(has->code) {
				function->opens_at.print_error_header();
				fatal_error("Declaration has more than one main block.");
			}
			has->code = function->block;
		}
	}
	has->code_scope = scope->parent_id;
	
	return id;
}

template<> Entity_Id
process_declaration<Reg_Type::flux>(Mobius_Model *model, Decl_Scope *scope, Decl_AST *decl) {
	
	int which = match_declaration(decl,
		{
			// it seems to be safer and very time saving just to require a name.
			//{Token_Type::identifier, Token_Type::identifier},
			{Token_Type::identifier, Token_Type::identifier, Token_Type::quoted_string},
		});
	
	Token *name = nullptr;
	//if(which == 1)
	name = single_arg(decl, 2);
	
	auto id   = model->fluxes.find_or_create(&decl->handle_name, scope, name, decl);
	auto flux = model->fluxes[id];
	
	process_location_argument(model, scope, decl, 0, &flux->source, true);
	process_location_argument(model, scope, decl, 1, &flux->target, true);
	flux->target_was_out = (flux->target.type == Var_Location::Type::out);
	
	if(flux->source == flux->target && is_located(flux->source)) {
		decl->location.print_error_header();
		fatal_error("The source and the target of a flux can't be the same.");
	}
	
	auto body = reinterpret_cast<Function_Body_AST *>(decl->bodies[0]); //NOTE: In parsing and match_declaration it has already been checked that we have exactly one.
	flux->code = body->block;
	flux->code_scope = scope->parent_id;
	
	return id;
}

Entity_Id
process_par_ref_declaration(Mobius_Model *model, Decl_Scope *scope, Decl_AST *decl) {
	match_declaration(decl, {{Token_Type::quoted_string, Decl_Type::unit}});
	
	auto id       = model->parameters.standard_declaration(scope, decl);
	
	// TODO: check for unit clashes with previous def.
	auto unit = resolve_argument<Reg_Type::unit>(model, scope, decl, 1);
	
	if(!model->model_decl_scope.has(id)) {
		decl->location.print_error_header();
		fatal_error("Reference to a parameter \"", single_arg(decl, 0)->string_value, "\" that was not declared in the main model scope.");
	}
	
	return id;
}

bool
process_load_library_declaration(Mobius_Model *model, Decl_AST *decl, Decl_Scope *to_scope, std::vector<Entity_Id> &loaded_now);

Entity_Id
load_library(Mobius_Model *model, Source_Location from, Decl_Scope *to_scope, std::vector<Entity_Id> &loaded_now, String_View rel_path, std::string &decl_name, Source_Location load_loc) {
	
	Entity_Id lib_id = load_top_decl_from_file(model, from, to_scope, rel_path, decl_name, Decl_Type::library);

	if(std::find(loaded_now.begin(), loaded_now.end(), lib_id) != loaded_now.end()) {
		begin_error(Mobius_Error::parsing);
		error_print("There is a circular reference between libraries. This is currently not allowed.\n");
		error_print(rel_path, " ", decl_name, "\n");
		return invalid_entity_id;
	}
	loaded_now.push_back(lib_id);

	auto lib = model->libraries[lib_id];
	
	if(!lib->has_been_processed) {
		match_declaration(lib->decl, {{Token_Type::quoted_string}}, 0, false);
		
		auto body = reinterpret_cast<Decl_Body_AST *>(lib->decl->bodies[0]);
		
		if(body->doc_string.string_value)
			lib->doc_string = body->doc_string.string_value;
		
		lib->scope.import(model->global_scope);
		
		// Check for recursive library loads into other libraries.
		for(Decl_AST *child : body->child_decls) {
			if(child->type == Decl_Type::load) {
				bool success = process_load_library_declaration(model, child, &lib->scope, loaded_now);
				if(!success) return invalid_entity_id;
			}
		}
		
		for(Decl_AST *child : body->child_decls) {
			if(child->type == Decl_Type::load) continue; // already processed above.
			else if(child->type == Decl_Type::constant) {
				process_declaration<Reg_Type::constant>(model, &lib->scope, child);
			} else if(child->type == Decl_Type::function) {
				process_declaration<Reg_Type::function>(model, &lib->scope, child);
			} else {
				child->location.print_error_header();
				fatal_error("Did not expect a declaration of type ", name(child->type), " inside a library declaration.");
			}
		}
		lib->has_been_processed = true;
		lib->scope.check_for_missing_decls(model);
	}
	
	to_scope->import(lib->scope, &load_loc);
	
	return lib_id;
}

bool
process_load_library_declaration(Mobius_Model *model, Decl_AST *decl, Decl_Scope *to_scope, std::vector<Entity_Id> &loaded_now) {
	match_declaration(decl, {{Token_Type::quoted_string, { Decl_Type::library, true } }});
	
	String_View file_name = single_arg(decl, 0)->string_value;
	for(int idx = 1; idx < decl->args.size(); ++idx) {
		auto lib_load_decl = decl->args[idx]->decl;
		match_declaration(lib_load_decl, {{Token_Type::quoted_string}}, 0, true, 0);
		std::string library_name = single_arg(lib_load_decl, 0)->string_value;

		Entity_Id child_lib_id = load_library(model, single_arg(decl, 0)->location, to_scope, loaded_now, file_name, library_name, lib_load_decl->location);
		if(!is_valid(child_lib_id)) {
			error_print(file_name, " ", library_name, "\n");
			return false;
		}
	}
	return true;
}

void
process_module_declaration(Mobius_Model *model, Entity_Id id) {
		
	auto module = model->modules[id];
	if(module->has_been_processed) return;
	
	auto decl = module->decl;
	match_declaration(decl, {{Token_Type::quoted_string, Token_Type::integer, Token_Type::integer, Token_Type::integer}});
	
	module->version.major        = single_arg(decl, 1)->val_int;
	module->version.minor        = single_arg(decl, 2)->val_int;
	module->version.revision     = single_arg(decl, 3)->val_int;
	
	module->scope.import(model->global_scope);
	
	auto body = reinterpret_cast<Decl_Body_AST *>(decl->bodies[0]);
	
	if(body->doc_string.string_value)
		module->doc_string = body->doc_string.string_value;
	
	// NOTE we have to process these first since they have to be linked to their universal version. This could break if they were referenced before declared and the declaration was processed later.
	// TODO: this may still break for in-argument declarations that happen after a reference, but that is probably rare..
	for(Decl_AST *child : body->child_decls) {
		switch(child->type) {
			
			case Decl_Type::compartment :
			case Decl_Type::quantity :
			case Decl_Type::property : {
				process_declaration<Reg_Type::component>(model, &module->scope, child);
			} break;
			
			case Decl_Type::par_real :
			case Decl_Type::par_int  :
			case Decl_Type::par_bool :
			case Decl_Type::par_enum : {
				process_par_ref_declaration(model, &module->scope, child);
			} break;
		}
	}
	
	for(Decl_AST *child : body->child_decls) {
		switch(child->type) {
			case Decl_Type::load : {
				std::vector<Entity_Id> loaded_now;
				process_load_library_declaration(model, child, &module->scope, loaded_now);
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
			
			case Decl_Type::has : {
				process_declaration<Reg_Type::has>(model, &module->scope, child);
			} break;
			
			case Decl_Type::flux : {
				process_declaration<Reg_Type::flux>(model, &module->scope, child);
			} break;
			
			case Decl_Type::par_real :
			case Decl_Type::par_int  :
			case Decl_Type::par_bool :
			case Decl_Type::par_enum :
			case Decl_Type::compartment :
			case Decl_Type::property :
			case Decl_Type::quantity : {  // already processed above
			} break;
			
			default : {
				child->location.print_error_header();
				fatal_error("Did not expect a declaration of type ", name(child->type), " inside a module declaration.");
			};
		}
	}
	module->scope.check_for_missing_decls(model);
	
	module->has_been_processed = true;
}

Entity_Id
expect_exists(Decl_Scope *scope, Token *handle_name, Reg_Type reg_type) {
	std::string handle = handle_name->string_value;
	auto reg = (*scope)[handle];
	if(!reg) {
		handle_name->print_error_header();
		fatal_error("There is no entity with the identifier '", handle, "' in the referenced scope.");
	}
	if(reg->id.reg_type != reg_type) {
		handle_name->print_error_header();
		fatal_error("The entity '", handle_name->string_value, "' does not have the type ", name(reg_type), ".");
	}
	return reg->id;
}

void
process_to_declaration(Mobius_Model *model, Decl_Scope *scope, Decl_AST *decl) {
	// Process a "to" declaration
	match_declaration(decl, {{Token_Type::identifier}}, 2, false);
	
	auto module_id = expect_exists(scope, &decl->decl_chain[0], Reg_Type::module);
	auto module = model->modules[module_id];
	
	auto flux_id = expect_exists(&module->scope, &decl->decl_chain[1], Reg_Type::flux);
	auto flux = model->fluxes[flux_id];
	
	if(flux->target.type != Var_Location::Type::out) {
		decl->decl_chain[1].print_error_header();
		fatal_error("The flux '", decl->decl_chain[1].string_value, "' does not have the target 'out', and so we can't re-assign its target.");
	}
	
	auto &chain = decl->args[0]->sub_chain;
	
	if(chain.size() >= 2) {
		process_location_argument(model, scope, decl, 0, &flux->target, false);
	} else if (chain.size() == 1) {
		auto neigh_id = expect_exists(scope, &chain[0], Reg_Type::neighbor);
		flux->neighbor_target = neigh_id;
	} else {
		chain[0].print_error_header();
		fatal_error("This is not a well-formed flux target. Expected something on the form 'compartment.quantity' or 'neighbor'.");
	}
	// NOTE: in model scope, all compartment and quantity/property declarations are processed first, so it is ok to just look them up here.
}

void
process_no_carry_declaration(Mobius_Model *model, Decl_Scope *scope, Decl_AST *decl) {
	match_declaration(decl, {{Token_Type::identifier}}, 2, false);
	
	auto module_id = expect_exists(scope, &decl->decl_chain[0], Reg_Type::module);
	auto module = model->modules[module_id];
	
	auto flux_id = expect_exists(&module->scope, &decl->decl_chain[1], Reg_Type::flux);
	auto flux = model->fluxes[flux_id];
	
	Var_Location loc;
	process_location_argument(model, scope, decl, 0, &loc, false);
	
	bool found = false;
	auto above = loc;
	while(above.is_dissolved()) {
		above = remove_dissolved(above);
		if(above == flux->source) {
			found = true;
			break;
		}
	}
	if(!found) {
		decl->location.print_error_header();
		fatal_error("This flux could not have carried this quantity since the latter is not dissolved in the source of the flux.");
	}
	
	flux->no_carry.push_back(loc);
}

template<> Entity_Id
process_declaration<Reg_Type::index_set>(Mobius_Model *model, Decl_Scope *scope, Decl_AST *decl) {
	//TODO: index set type (maybe)
	match_declaration(decl, {{Token_Type::quoted_string}});
	return model->index_sets.standard_declaration(scope, decl);
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
	//TODO: not sure if we need this one or if we could just locate the corresponding 'has' declaration and put the solver info there.
	match_declaration(decl, {{Token_Type::identifier}}, 1, false);
	
	auto id = model->solves.find_or_create(nullptr, nullptr, nullptr, decl);
	auto solve = model->solves[id];
	
	solve->solver = model->solvers.find_or_create(&decl->decl_chain[0], scope);
	process_location_argument(model, scope, decl, 0, &solve->loc, false);
	
	return id;
}

template<> Entity_Id
process_declaration<Reg_Type::neighbor>(Mobius_Model *model, Decl_Scope *scope, Decl_AST *decl) {
	match_declaration(decl, {{Token_Type::quoted_string, Token_Type::identifier}}, 1);
	
	auto id       = model->neighbors.standard_declaration(scope, decl);
	auto neighbor = model->neighbors[id];
	
	auto index_set             = model->index_sets.find_or_create(&decl->decl_chain[0], scope);
	String_View structure_type = single_arg(decl, 1)->string_value;
	
	neighbor->index_set = index_set;
	model->index_sets[index_set]->neighbor_structure = id;
	if(structure_type == "directed_tree")
		neighbor->type = Neighbor_Structure_Type::directed_tree;
	else {
		single_arg(decl, 1)->print_error_header();
		fatal_error("Unsupported neighbor structure type '", structure_type, "'.");
	}
	
	return id;
}

void
process_distribute_declaration(Mobius_Model *model, Decl_Scope *scope, Decl_AST *decl) {
	match_declaration(decl, {{{Decl_Type::index_set, true}}}, 1, false);
	
	//auto comp_id = model->components.find_or_create(&decl->decl_chain[0], scope);
	auto comp_id = expect_exists(scope, &decl->decl_chain[0], Reg_Type::component);
	auto component = model->components[comp_id];
	
	if(component->decl_type == Decl_Type::property) {
		decl->decl_chain[0].location.print_error_header(Mobius_Error::model_building);
		fatal_error("Only compartments and quantities can be distributed, not properties");
	}
	
	//TODO: some guard against overlapping / contradictory declarations.
	// TODO: guard against distribute on properties.
	for(int idx = 0; idx < decl->args.size(); ++idx) {
		auto index_set = resolve_argument<Reg_Type::index_set>(model, scope, decl, idx);
		component->index_sets.push_back(index_set);
	}
}

void
process_aggregation_weight_declaration(Mobius_Model *model, Decl_Scope *scope, Decl_AST *decl) {
	match_declaration(decl, {{Decl_Type::compartment, Decl_Type::compartment}}, 0, false);
	
	auto from_comp = resolve_argument<Reg_Type::component>(model, scope, decl, 0);
	auto to_comp   = resolve_argument<Reg_Type::component>(model, scope, decl, 1);
	
	if(model->components[from_comp]->decl_type != Decl_Type::compartment || model->components[to_comp]->decl_type != Decl_Type::compartment) {
		decl->location.print_error_header(Mobius_Error::model_building);
		fatal_error("Aggregations can only be declared between compartments");
	}
	
	//TODO: some guard against overlapping / contradictory declarations.
	//TODO: guard against nonsensical declarations (e.g. going between the same compartment).
	auto function = reinterpret_cast<Function_Body_AST *>(decl->bodies[0]);
	model->components[from_comp]->aggregations.push_back({to_comp, function->block, scope->parent_id});
}

void
process_unit_conversion_declaration(Mobius_Model *model, Decl_Scope *scope, Decl_AST *decl) {
	match_declaration(decl, {{Token_Type::identifier, Token_Type::identifier}});
	
	Flux_Unit_Conversion_Data data = {};
	
	//TODO: some guard against overlapping / contradictory declarations.
	//TODO: guard against nonsensical declarations (e.g. going between the same compartment).
	process_location_argument(model, scope, decl, 0, &data.source, false);
	process_location_argument(model, scope, decl, 1, &data.target, false);
	data.code = reinterpret_cast<Function_Body_AST *>(decl->bodies[0])->block;
	data.code_scope = scope->parent_id;
	
	// TODO: Ideally we should check here that the location is valid. But it could be messy wrt order of declarations.
	
	model->components[data.source.first()]->unit_convs.push_back(data);
}

bool
load_model_extensions(File_Data_Handler *handler, Decl_AST *from_decl, std::unordered_set<String_View, String_View_Hash> &loaded_paths, std::vector<Decl_AST *> &loaded_decls, String_View rel_path) {
	auto body = reinterpret_cast<Decl_Body_AST *>(from_decl->bodies[0]);
	
	for(Decl_AST *child : body->child_decls) {
		if(child->type == Decl_Type::extend) {
			match_declaration(child, {{Token_Type::quoted_string}});
			String_View extend_file_name = single_arg(child, 0)->string_value;
			
			// TODO: It is a bit unnecessary to read the AST from the file before we check that the normalized path is not already in the dictionary.
			//  but right now that happens in the same function call.
			String_View normalized_path;
			auto extend_model = read_model_ast_from_file(handler, extend_file_name, rel_path, &normalized_path);
			
			if(loaded_paths.find(normalized_path) != loaded_paths.end()) {
				begin_error(Mobius_Error::parsing);
				error_print("There is circularity in the model extensions:\n", extend_file_name, "\n");
				delete extend_model;
				return false;
			}
			
			loaded_paths.insert(normalized_path);
			loaded_decls.push_back(extend_model);
			
			// Load extensions of extensions.
			bool success = load_model_extensions(handler, extend_model, loaded_paths, loaded_decls, normalized_path);
			if(!success) {
				error_print(extend_file_name, "\n");
				return false;
			}
		}
	}
	
	return true;
}

Mobius_Model *
load_model(String_View file_name) {
	Mobius_Model *model = new Mobius_Model();
	
	auto decl = read_model_ast_from_file(&model->file_handler, file_name);
	model->main_decl  = decl;
	model->model_name = single_arg(decl, 0)->string_value;
	model->path       = file_name;
	
	auto body = reinterpret_cast<Decl_Body_AST *>(decl->bodies[0]);
	
	if(body->doc_string.string_value)
		model->doc_string = body->doc_string.string_value;
	
	register_intrinsics(model);

	std::unordered_set<String_View, String_View_Hash> loaded_files = { file_name };
	std::vector<Decl_AST *> extend_models = { decl };
	load_model_extensions(&model->file_handler, decl, loaded_files, extend_models, file_name);
	
	std::reverse(extend_models.begin(), extend_models.end()); // Reverse inclusion order so that the modules from the base model are listed first in e.g. MobiView2.
	
	//TODO: now we just throw everything into a single model decl scope, but what happens if we have re-declarations of handles because of multiple extensions?

	auto scope = &model->model_decl_scope;
	
	// We need to process these first since some other declarations rely on these existing, such as par_group.
	for(auto &extend : extend_models) {
		auto body = reinterpret_cast<Decl_Body_AST *>(extend->bodies[0]);
		
		for(Decl_AST *child : body->child_decls) {
			switch (child->type) {
				case Decl_Type::compartment :
				case Decl_Type::quantity : {
					process_declaration<Reg_Type::component>(model, scope, child);
				} break;
				
				case Decl_Type::neighbor : {
					process_declaration<Reg_Type::neighbor>(model, scope, child);  // NOTE: we also put this here since we expect we will need referencing it inside modules eventually.
				} break;
				
				default : {
				} break;
			}
		}
	}
	
	// Note: have to do this before loading modules because any loaded modules need to know if a parameter it references was declared in the model decl scope (for now at least).
	for(auto &extend : extend_models) {
		auto body = reinterpret_cast<Decl_Body_AST *>(extend->bodies[0]);
		for(Decl_AST *child : body->child_decls) {
			if(child->type == Decl_Type::par_group)
				process_declaration<Reg_Type::par_group>(model, scope, child);
		}
	}
	
	// NOTE: process loads before the rest of the model scope declarations. (may not be necessary).
	for(auto &extend : extend_models) {
		auto body = reinterpret_cast<Decl_Body_AST *>(extend->bodies[0]);
		for(Decl_AST *child : body->child_decls) {
			//TODO: do libraries also.
			
			switch (child->type) {
				case Decl_Type::load : {
					// Load from another file using a "load" declaration
					match_declaration(child, {{Token_Type::quoted_string, {Decl_Type::module, true}}}, 0, false);
					String_View file_name = single_arg(child, 0)->string_value;
					for(int idx = 1; idx < child->args.size(); ++idx) {
						Decl_AST *module_spec = child->args[idx]->decl;
						match_declaration(module_spec, {{Token_Type::quoted_string}}, 0, true, 0);  //TODO: allow specifying the version also?
						
						auto module_name = single_arg(module_spec, 0)->string_value;
						auto module_id = load_top_decl_from_file(model, single_arg(child, 0)->location, scope, file_name, module_name, Decl_Type::module);
						
						std::string module_handle = "";
						if(module_spec->handle_name.string_value)
							module_handle = module_spec->handle_name.string_value;
						scope->add_local(module_handle, module_spec->location, module_id);
						
						process_module_declaration(model, module_id);
					}
					//TODO: should also allow loading libraries here!
				} break;
				
				case Decl_Type::module : {
					// Inline module sub-scope inside the model declaration.
					auto module_id = model->modules.standard_declaration(scope, child);
					model->modules[module_id]->decl = child;
					model->modules[module_id]->scope.parent_id = module_id;
					
					std::string module_handle = "";
					if(child->handle_name.string_value)
						module_handle = child->handle_name.string_value;
					scope->add_local(module_handle, child->location, module_id);
					
					process_module_declaration(model, module_id);
				} break;
			}
		}
	}
	
	for(auto &extend : extend_models) {
		auto body = reinterpret_cast<Decl_Body_AST *>(extend->bodies[0]);
		
		for(Decl_AST *child : body->child_decls) {

			switch (child->type) {
				
				case Decl_Type::to : {
					process_to_declaration(model, scope, child);
				} break;
				
				case Decl_Type::no_carry : {
					process_no_carry_declaration(model, scope, child);
				} break;
				
				case Decl_Type::index_set : {
					process_declaration<Reg_Type::index_set>(model, scope, child);
				} break;
				
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
				case Decl_Type::compartment :
				case Decl_Type::quantity :
				case Decl_Type::neighbor :
				case Decl_Type::extend :
				case Decl_Type::module :
				case Decl_Type::load : {
					// Don't do anything. We handled it above already
				} break;
				
				default : {
					child->location.print_error_header();
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