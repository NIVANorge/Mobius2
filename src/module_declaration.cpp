
#include "module_declaration.h"
#include <algorithm>

Registry_Base *
Module_Declaration::registry(Reg_Type reg_type) {
	switch(reg_type) {
		case Reg_Type::compartment :              return &compartments;
		case Reg_Type::par_group :                return &par_groups;
		case Reg_Type::parameter :                return &parameters;
		case Reg_Type::unit :                     return &units;
		case Reg_Type::property_or_quantity :     return &properties_and_quantities;
		case Reg_Type::has :                      return &hases;
		case Reg_Type::flux :                     return &fluxes;
		case Reg_Type::function :                 return &functions;
	}
	
	fatal_error(Mobius_Error::internal, "Unhandled entity type in registry().");
	
	return nullptr;
}

void
see_if_handle_name_exists_in_scope(Module_Declaration *module, Token *handle_name) {
	
	// check for reserved names
	static String_View reserved[] = {
		#define ENUM_VALUE(name, _a, _b) #name,
		#include "decl_types.incl"
		#undef ENUM_VALUE
		
		#include "other_reserved.incl"
	};
	
	if(std::find(std::begin(reserved), std::end(reserved), handle_name->string_value) != std::end(reserved)) {
		handle_name->print_error_header();
		fatal_error("The name \"", handle_name->string_value, "\" is reserved.");
	}
	
	Entity_Id id = module->find_handle(handle_name->string_value);
	if(is_valid(id)) {
		handle_name->print_error_header();
		error_print("The name \"", handle_name->string_value, "\" was already used here:\n");
		auto reg = module->find_entity(id);
		reg->location.print_error();
		error_print("with type ", name(reg->decl_type), ".");
		mobius_error_exit();
	}
}

template <Reg_Type reg_type> Entity_Id
Registry<reg_type>::find_or_create(Token *handle_name, Token *name, Decl_AST *declaration) {
	
	// TODO: This may be a case of a function that tries to do too many things and becomes confusing. Split it up?
	
	Entity_Id found_id = invalid_entity_id;
	
	if(is_valid(handle_name)) {
		// this is either a lookup of an already registered handle, or it is a new declaration with a given handle.
		
		auto find = handle_name_to_handle.find(handle_name->string_value);
		if(find != handle_name_to_handle.end()) {
			// the handle has already been registered
			
			found_id = find->second;
			
			if(declaration && registrations[find->second.id].has_been_declared) {
				// the handle was used for a previous declaration (of the same type), and this is also a declaration (in the same module scope), which is not allowed.
				
				handle_name->print_error_header();
				error_print("Re-declaration of handle \"", handle_name->string_value, "\". It was previously declared here:\n");
				registrations[find->second.id].location.print_error();
				mobius_error_exit();
			}
			
			if(!declaration)  // this is not a declaration, we only wanted to look up the id for this handle.
				return found_id;
		}
		
	} else if (is_valid(name)) {
		// the handle was not given. This is either an anonymous (i.e. handle-less) declaration, or it is a name-based lookup.
		
		auto find = name_to_handle.find(name->string_value);
		if(find != name_to_handle.end()) {
			// the name was already used for this type.
			
			if(declaration) {
				name->print_error_header();
				error_print("Re-declaration of an entity of name ", name->string_value, " of the same type as one previously declared here:\n");
				registrations[find->second.id].location.print_error();
				mobius_error_exit();
			}
			
			return find->second;
		} else if(!declaration)
			return invalid_entity_id;
	}
	
	/* If the control flow has reached here, either
		- This is a declaration
			- no handle, just a name
			- both a handle and a name
		- This is not a declaration, but a handle was given, and it is the first reference to the handle.
	*/
	
	bool existed_already = is_valid(found_id);
	
	if(!existed_already) {
		// The entity was not already created, so we have to do that.
	
		found_id = {parent->module_id, reg_type, (s32)registrations.size()};
		registrations.push_back({});
	
		if(is_valid(handle_name)) {
			// See if the handle is reserved or exists for another entity type.
			see_if_handle_name_exists_in_scope(parent, handle_name);
			
			handle_name_to_handle[handle_name->string_value] = found_id;
			parent->handles_in_scope[handle_name->string_value] = found_id;
		}
		
		if(name)
			name_to_handle[name->string_value] = found_id;
	}
	
	auto registration = &registrations[found_id.id];
	
	// note: this overwrites the handle name if it was already registered, but that is not really a problem (since it is the same).
	// also, we want the location to be overwritten with the declaration.
	if(is_valid(handle_name)) {
		registration->handle_name = handle_name->string_value;
		registration->location    = handle_name->location;
	} else
		registration->location    = declaration->type_name.location;
	
	if(name)
		registration->name = name->string_value;
	
	//NOTE: the type be different from the reg_type in some instances since we use the same registry for e.g. quantities and properties, or different parameter types.
	if(declaration)
		registration->decl_type   = declaration->type;
	
	registration->has_been_declared = (declaration != nullptr);
	
	return found_id;
}

template <Reg_Type reg_type> Entity_Id
Registry<reg_type>::create_compiler_internal(String_View handle_name, Decl_Type decl_type) {
	Entity_Id id = {parent->module_id, reg_type, (s32)registrations.size()};
	registrations.push_back({});
	
	auto registration = &registrations[id.id];
	registration->handle_name = handle_name;
	registration->decl_type   = decl_type;
	registration->has_been_declared = true;
	
	handle_name_to_handle[handle_name] = id;
	parent->handles_in_scope[handle_name] = id;
	
	return id;
}

template<Reg_Type expected_type> Entity_Id
resolve_argument(Module_Declaration *module, Decl_AST *decl, int which, int max_sub_chain_size = 1) {
	// We could do more error checking here, but it should really only be called after calling match_declaration...
	
	Argument_AST *arg = decl->args[which];
	if(arg->decl) {
		if(get_reg_type(arg->decl->type) != expected_type)
			fatal_error(Mobius_Error::internal, "Mismatched type in type resolution."); // This should not have happened since we should have checked this aready.
		
		return process_declaration<expected_type>(module, arg->decl);
	} else {
		if(max_sub_chain_size > 0 && arg->sub_chain.size() > max_sub_chain_size) {
			arg->sub_chain[0].print_error_header();
			fatal_error("Did not expect a chained declaration.");
		}
		
		return module->registry(expected_type)->find_or_create(&arg->sub_chain[0]);
	}
	return invalid_entity_id;
}

template<> Entity_Id
process_declaration<Reg_Type::unit>(Module_Declaration *module, Decl_AST *decl) {
	
	for(Argument_AST *arg : decl->args) {
		if(!Arg_Pattern().matches(arg)) {
			decl->type_name.print_error_header();
			fatal_error("Invalid argument to unit declaration.");
		}
	}
	
	// TODO: implement this!
	
	return invalid_entity_id;
}

template<> Entity_Id
process_declaration<Reg_Type::parameter>(Module_Declaration *module, Decl_AST *decl) {
	Token_Type value_type = Token_Type::real; //TODO: allow other types.
	
	int which = match_declaration(decl,
		{
			{Token_Type::quoted_string, Decl_Type::unit, value_type},                                                      // 0
			{Token_Type::quoted_string, Decl_Type::unit, value_type, Token_Type::quoted_string},                           // 1
			{Token_Type::quoted_string, Decl_Type::unit, value_type, value_type, value_type},                              // 2
			{Token_Type::quoted_string, Decl_Type::unit, value_type, value_type, value_type, Token_Type::quoted_string},   // 3
		});
	
	auto id        = module->parameters.standard_declaration(decl);
	auto parameter = module->parameters[id];
	
	parameter->unit                    = resolve_argument<Reg_Type::unit>(module, decl, 1);
	parameter->default_val             = get_parameter_value(single_arg(decl, 2), Token_Type::real);
	if(which == 2 || which == 3) {
		parameter->min_val             = get_parameter_value(single_arg(decl, 3), Token_Type::real);
		parameter->max_val             = get_parameter_value(single_arg(decl, 4), Token_Type::real);
	} else {
		parameter->min_val.val_double  = -std::numeric_limits<double>::infinity();
		parameter->max_val.val_double  =  std::numeric_limits<double>::infinity();
	}
	
	if(which == 1)
		parameter->description         = single_arg(decl, 3)->string_value;
	if(which == 3)
		parameter->description         = single_arg(decl, 5)->string_value;
	
	return id;
}

template<> Entity_Id
process_declaration<Reg_Type::par_group>(Module_Declaration *module, Decl_AST *decl) {
	match_declaration(decl, {{Token_Type::quoted_string}}, 1);

	auto id        = module->par_groups.standard_declaration(decl);
	auto par_group = module->par_groups[id];
	
	//TODO: Do we always need to require that a par group is tied a compartment?
	//TODO: Actually, it could be tied to a quantity too, once we get to implement that!
	par_group->compartment = module->compartments.find_or_create(&decl->decl_chain[0]);
	
	auto body = reinterpret_cast<Decl_Body_AST *>(decl->bodies[0]);
	
	for(Decl_AST *child : body->child_decls) {
		if(child->type == Decl_Type::par_real) {   //TODO: do other parameter types
			auto par_handle = process_declaration<Reg_Type::parameter>(module, child);
			par_group->parameters.push_back(par_handle);
			module->parameters[par_handle]->par_group = id;
		} else {
			child->type_name.print_error_header();
			fatal_error("Did not expect a declaration of type ", child->type_name.string_value, " inside a par_group declaration.");
		}
	}
	//TODO: process doc string(s) in the par group?
	
	return id;
}


Value_Location
make_global(Module_Declaration *module, Value_Location loc) {
	Value_Location result = loc;
	if(loc.type == Location_Type::located && module->global_scope) {
		auto comp_id = module->compartments[loc.compartment]->global_id;
		if(is_valid(comp_id)) result.compartment = comp_id;
		auto quant_id = module->properties_and_quantities[loc.property_or_quantity]->global_id;
		if(is_valid(quant_id)) result.property_or_quantity = quant_id;
	}
	return result;
}

template<> Entity_Id
process_declaration<Reg_Type::has>(Module_Declaration *module, Decl_AST *decl) {
	int which = match_declaration(decl,
		{
			{Decl_Type::property},
			{Decl_Type::property, Decl_Type::unit},
			{Decl_Type::property, Token_Type::quoted_string},
			{Decl_Type::property, Decl_Type::unit, Token_Type::quoted_string},
		},
		1, true, true, true);
	
	Token *name = nullptr;
	if(which == 2) name = single_arg(decl, 1);
	else if(which == 3) name = single_arg(decl, 2);
	
	auto id  = module->hases.find_or_create(&decl->handle_name, name, decl);
	auto has = module->hases[id];
	
	// TODO: can eventually be tied to a quantity not only a compartment.
	has->value_location.type = Location_Type::located;
	has->value_location.compartment = module->compartments.find_or_create(&decl->decl_chain[0]);
	has->value_location.property_or_quantity = resolve_argument<Reg_Type::property_or_quantity>(module, decl, 0);
	
	has->value_location = make_global(module, has->value_location);
	
	if(which == 1 || which == 3)
		has->unit = resolve_argument<Reg_Type::unit>(module, decl, 1);
	else
		has->unit = invalid_entity_id;
	
	for(Body_AST *body : decl->bodies) {
		auto function = reinterpret_cast<Function_Body_AST *>(body);
		if(function->modifiers.size() > 1) {
			function->opens_at.print_error_header();
			fatal_error("Bodies belonging to declarations of type \"has\" can only have one modifier.");
		} else if(function->modifiers.size() == 1) {
			if(function->modifiers[0].string_value == "initial") {
				if(has->initial_code) {
					function->opens_at.print_error_header();
					fatal_error("Declaration has more than one \".initial\" block.");
				}
				has->initial_code = function->block;
			} else {
				function->opens_at.print_error_header();
				fatal_error("Expected either no function body modifiers, or \".initial\".");   //TODO: should maybe come up with a better name than "modifier" (?)
			}
		} else {
			if(has->code) {
				function->opens_at.print_error_header();
				fatal_error("Declaration has more than one main block.");
			}
			has->code = function->block;
		}
	}
	
	return id;
}

void
process_flux_argument(Module_Declaration *module, Decl_AST *decl, int which, Value_Location *location) {
	std::vector<Token> *symbol = &decl->args[which]->sub_chain;
	if(symbol->size() == 1) {
		Token *token = &(*symbol)[0];
		if(token->string_value == "nowhere")
			location->type = Location_Type::nowhere;
		else if(token->string_value == "out")
			location->type = Location_Type::out;
		else {
			token->print_error_header();
			fatal_error("Invalid flux location.");
		}
	} else if (symbol->size() == 2) {
		location->type     = Location_Type::located;
		location->compartment = module->compartments.find_or_create(&(*symbol)[0]);
		location->property_or_quantity   = module->properties_and_quantities.find_or_create(&(*symbol)[1]);    //NOTE: this does not guarantee that this is a quantity and not a property, so that must be checked in post.
		*location = make_global(module, *location);
	} else {
		//TODO: this should eventually be allowed when having dissolved quantity
		(*symbol)[0].print_error_header();
		fatal_error("Invalid flux location.");
	}
}

template<> Entity_Id
process_declaration<Reg_Type::flux>(Module_Declaration *module, Decl_AST *decl) {
	
	int which = match_declaration(decl,
		{
			// it is actually safer and very time saving just to require a name.
			//{Token_Type::identifier, Token_Type::identifier},
			{Token_Type::identifier, Token_Type::identifier, Token_Type::quoted_string},
		});
	
	Token *name = nullptr;
	//if(which == 1)
	name = single_arg(decl, 2);
	
	auto id   = module->fluxes.find_or_create(&decl->handle_name, name, decl);
	auto flux = module->fluxes[id];
	
	process_flux_argument(module, decl, 0, &flux->source);
	process_flux_argument(module, decl, 1, &flux->target);
	
	if(flux->source == flux->target && flux->source.type == Location_Type::located) {
		decl->type_name.print_error_header();
		fatal_error("The source and the target of a flux can't be the same.");
	}
	
	auto body = reinterpret_cast<Function_Body_AST *>(decl->bodies[0]); //NOTE: In parsing and match_declaration it has already been checked that we have exactly one.
	flux->code = body->block;
	
	return id;
}

template<> Entity_Id
process_declaration<Reg_Type::function>(Module_Declaration *module, Decl_AST *decl) {
	match_declaration(decl, {});
	
	auto id =  module->functions.find_or_create(&decl->handle_name, nullptr, decl);
	auto function = module->functions[id];
	
	for(auto arg : decl->args) {
		if(arg->decl || arg->sub_chain.size() != 1) {
			decl->type_name.print_error_header();
			fatal_error("The arguments to a function declaration should be just single identifiers.");
		}
		function->args.push_back(arg->sub_chain[0].string_value);
	}
	
	for(int idx = 1; idx < function->args.size(); ++idx) {
		for(int idx2 = 0; idx2 < idx; ++idx2) {
			if(function->args[idx] == function->args[idx2]) {
				decl->args[idx]->sub_chain[0].print_error_header();
				fatal_error("Duplicate argument name \"", function->args[idx], "\" in function declaration.");
			}
		}
	}
	
	auto body = reinterpret_cast<Function_Body_AST *>(decl->bodies[0]);
	function->code = body->block;
	function->fun_type = Function_Type::decl;
	
	return id;
}


Module_Declaration *
process_module_declaration(Module_Declaration *global_scope, s16 module_id, Decl_AST *decl) {
	
	//TODO: have to decide whether we should copy over string views at this point.
	
	Module_Declaration *module = new Module_Declaration();
	module->global_scope = global_scope;
	module->decl = decl; // Keep it so that we can free it later.
	module->module_id = module_id;
	
	match_declaration(decl, {{Token_Type::quoted_string, Token_Type::integer, Token_Type::integer, Token_Type::integer}}, 0, false, true, false);
	
	module->name  = single_arg(decl, 0)->string_value;
	module->major = single_arg(decl, 1)->val_int;
	module->minor = single_arg(decl, 2)->val_int;
	module->revision = single_arg(decl, 3)->val_int;
	
	
	
	//TODO: it does not make that much sense to have special units and intrinsic functions registered on every module. We instead need a global scope for some things.
	//module->dimensionless_unit = module->units.create_compiler_internal("__dimensionless__", Decl_Type::unit); //TODO: give it data if necessary.
	
	auto minfun = module->functions.create_compiler_internal("min", Decl_Type::function);
	module->functions[minfun]->fun_type = Function_Type::intrinsic;
	module->functions[minfun]->args = {"a", "b"};
	auto maxfun = module->functions.create_compiler_internal("max", Decl_Type::function);
	module->functions[maxfun]->fun_type = Function_Type::intrinsic;
	module->functions[maxfun]->args = {"a", "b"};
	auto expfun = module->functions.create_compiler_internal("exp", Decl_Type::function);
	module->functions[expfun]->fun_type = Function_Type::intrinsic;
	module->functions[expfun]->args = {"a"};
	

	auto body = reinterpret_cast<Decl_Body_AST *>(decl->bodies[0]);
	
	if(body->doc_string.type == Token_Type::quoted_string)
		module->doc_string = body->doc_string.string_value;
	
	for(Decl_AST *child : body->child_decls) {
		
		// hmm, this is a bit annoying to have to write out..
		switch(child->type) {
			case Decl_Type::compartment : {
				process_declaration<Reg_Type::compartment>(module, child);
			} break;
			
			case Decl_Type::par_group : {
				process_declaration<Reg_Type::par_group>(module, child);
			} break;
			
			case Decl_Type::quantity :
			case Decl_Type::property : {
				process_declaration<Reg_Type::property_or_quantity>(module, child);
			} break;
			
			case Decl_Type::has : {
				process_declaration<Reg_Type::has>(module, child);
			} break;
			
			case Decl_Type::flux : {
				process_declaration<Reg_Type::flux>(module, child);
			} break;
			
			case Decl_Type::unit : {
				process_declaration<Reg_Type::unit>(module, child);
			} break;
			
			case Decl_Type::function : {
				process_declaration<Reg_Type::function>(module, child);
			} break;
			
			default : {
				child->type_name.print_error_header();
				fatal_error("Did not expect a declaration of type ", child->type_name.string_value, " inside a module declaration.");
			};
		}
	}
	
	for(const auto &entry : module->handles_in_scope) {
		auto reg = module->find_entity(entry.second);
		
		if(!reg->has_been_declared) {
			reg->location.print_error_header();
			//note: the handle_name must be valid for it to have been put in the scope in the first place, so this is ok.
			fatal_error("The name \"", reg->handle_name, "\" was used, but never declared.");
		}
	}
	
	return module;
}



