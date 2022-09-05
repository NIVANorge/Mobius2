
#include "module_declaration.h"
#include "model_declaration.h"
#include <algorithm>

bool
Arg_Pattern::matches(Argument_AST *arg) const {
	Token_Type check_type = token_type;
	
	switch(pattern_type) {
		case Type::unit_literal : {
			int count = arg->sub_chain.size();
			if(!arg->decl && (count == 1 || (count <= 3 && arg->chain_sep == ' ')))
				return true; //NOTE: only potentially true. Must be properly checked in the process_unit_declaration
			return false;
		} break;
	
		case Type::decl : {
			if(arg->decl && (get_reg_type(arg->decl->type) == get_reg_type(decl_type))) return true;
			//NOTE: we could still have an identifier that could potentially resolve to this type
			check_type = Token_Type::identifier;
		} // fall through to the next case to see if we have an identifier (chain).
		
		case Type::value : {
			if(arg->sub_chain.size() == 1) {
				if(check_type == Token_Type::real)
					return is_numeric(arg->sub_chain[0].type);
				return arg->sub_chain[0].type == check_type;
			}
			else if(arg->sub_chain.size() > 1 && check_type == Token_Type::identifier) {
				if(arg->chain_sep != '.') return false;
				for(Token &token : arg->sub_chain) {
					if(token.type != Token_Type::identifier)
						return false;
				}
				return true;
			}
		}
	}
	return false;
}

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
		case Reg_Type::index_set :                return &index_sets;
		case Reg_Type::solver :                   return &solvers;
		case Reg_Type::solve :                    return &solves;
		case Reg_Type::constant :                 return &constants;
		case Reg_Type::neighbor :                 return &neighbors;
	}
	
	fatal_error(Mobius_Error::internal, "Unhandled entity type ", name(reg_type), " in registry().");
	
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
		registration->location    = declaration->location;
	
	if(name) {
		registration->name = name->string_value;
		name_to_handle[name->string_value] = found_id;  // This is needed if something is referred to first only by handle, then later declared by name.
	}
	
	//NOTE: the type be different from the reg_type in some instances since we use the same registry for e.g. quantities and properties, or different parameter types.
	if(declaration)
		registration->decl_type   = declaration->type;
	
	registration->has_been_declared = (declaration != nullptr);
	
	return found_id;
}

template<Reg_Type expected_type> Entity_Id
resolve_argument(Module_Declaration *module, Decl_AST *decl, int which, int max_sub_chain_size=1) {
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

template <Reg_Type reg_type> Entity_Id
Registry<reg_type>::create_compiler_internal(String_View handle_name, Decl_Type decl_type, String_View name) {
	Entity_Id id = {parent->module_id, reg_type, (s32)registrations.size()};
	registrations.push_back({});
	
	auto registration = &registrations[id.id];
	registration->handle_name = handle_name;
	registration->decl_type   = decl_type;
	registration->has_been_declared = true;
	
	handle_name_to_handle[handle_name] = id;
	parent->handles_in_scope[handle_name] = id;
	
	if(name) {
		registration->name = name;
		name_to_handle[name] = id;
	}
	
	return id;
}

template<Reg_Type reg_type> inline Entity_Id
Registry<reg_type>::standard_declaration(Decl_AST *decl) {
	Token *name = single_arg(decl, 0);
	return find_or_create(&decl->handle_name, name, decl);
}

template<Reg_Type reg_type> Entity_Id
process_declaration(Module_Declaration *module, Decl_AST *decl); //NOTE: this will be template specialized below.

template<> inline Entity_Id
process_declaration<Reg_Type::compartment>(Module_Declaration *module, Decl_AST *decl) {
	match_declaration(decl, {{Token_Type::quoted_string}});
	
	auto name = single_arg(decl, 0);
	
	Entity_Id global_id = invalid_entity_id;
	
	if(module->global_scope) {
		global_id = module->global_scope->compartments.find_or_create(nullptr, name);
		if(!is_valid(global_id)) 
			global_id = module->global_scope->compartments.find_or_create(nullptr, name, decl);
	}
	
	Entity_Id id = module->compartments.standard_declaration(decl);
	module->compartments[id]->global_id = global_id;
	
	return id;
}

template<> inline Entity_Id
process_declaration<Reg_Type::property_or_quantity>(Module_Declaration *module, Decl_AST *decl) {
	//TODO: If we want to allow units on this declaration directly, we have to check for mismatches between decls in different modules.
	// For now it is safer to just have it on the "has", but we could go over this later and see if we could make it work.
	int which = match_declaration(decl,
		{
			{Token_Type::quoted_string},
			//{Token_Type::quoted_string, Decl_Type::unit},
		});
		
	auto name = single_arg(decl, 0);
	
	Entity_Id global_id = invalid_entity_id;
	
	if(module->global_scope) {
		global_id = module->global_scope->properties_and_quantities.find_or_create(nullptr, name);
		if(!is_valid(global_id)) 
			global_id = module->global_scope->properties_and_quantities.find_or_create(nullptr, name, decl);
	}

	auto id       = module->properties_and_quantities.standard_declaration(decl);
	auto property = module->properties_and_quantities[id];
	
	property->global_id = global_id;
	/*
	if(which == 1)
		property->unit = resolve_argument<Reg_Type::unit>(module, decl, 1);
	else
		property->unit = invalid_entity_id;
	*/
	return id;
}

template<> Entity_Id
process_declaration<Reg_Type::unit>(Module_Declaration *module, Decl_AST *decl) {
	
	for(Argument_AST *arg : decl->args) {
		if(!Arg_Pattern().matches(arg)) {
			decl->location.print_error_header();
			fatal_error("Invalid argument to unit declaration.");
		}
	}
	
	// TODO: implement this!
	
	return invalid_entity_id;
}

template<> Entity_Id
process_declaration<Reg_Type::parameter>(Module_Declaration *module, Decl_AST *decl) {
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
	
	auto id        = module->parameters.standard_declaration(decl);
	auto parameter = module->parameters[id];
	
	int mt0 = 2;
	if(token_type == Token_Type::boolean) mt0--;
	if(decl->type != Decl_Type::par_enum)
		parameter->default_val             = get_parameter_value(single_arg(decl, mt0), token_type);
	
	if(decl->type == Decl_Type::par_real) {
		parameter->unit                    = resolve_argument<Reg_Type::unit>(module, decl, 1);
		if(which == 2 || which == 3) {
			parameter->min_val             = get_parameter_value(single_arg(decl, 3), Token_Type::real);
			parameter->max_val             = get_parameter_value(single_arg(decl, 4), Token_Type::real);
		} else {
			parameter->min_val.val_real    = -std::numeric_limits<double>::infinity();
			parameter->max_val.val_real    =  std::numeric_limits<double>::infinity();
		}
	} else if (decl->type == Decl_Type::par_int) {
		parameter->unit                    = resolve_argument<Reg_Type::unit>(module, decl, 1);
		if(which == 2 || which == 3) {
			parameter->min_val             = get_parameter_value(single_arg(decl, 3), Token_Type::integer);
			parameter->max_val             = get_parameter_value(single_arg(decl, 4), Token_Type::integer);
		} else {
			parameter->min_val.val_real    = std::numeric_limits<s64>::lowest();
			parameter->max_val.val_real    = std::numeric_limits<s64>::max();
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
		String_View default_val_name = single_arg(decl, 1)->string_value;
		s64 default_val = enum_int_value(parameter, default_val_name);
		if(default_val < 0) {
			single_arg(decl, 1)->print_error_header();
			fatal_error("The given default value \"", default_val_name, "\" does not appear on the list of possible values for this enum parameter.");
		}
		parameter->default_val.val_integer = default_val;
		parameter->min_val.val_integer = 0;
		parameter->max_val.val_integer = (s64)parameter->enum_values.size() - 1;
	}
	
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
		if(child->type == Decl_Type::par_real || child->type == Decl_Type::par_int || child->type == Decl_Type::par_bool || child->type == Decl_Type::par_enum) {
			auto par_id = process_declaration<Reg_Type::parameter>(module, child);
			par_group->parameters.push_back(par_id);
			module->parameters[par_id]->par_group = id;
		} else {
			child->location.print_error_header();
			fatal_error("Did not expect a declaration of type ", name(child->type), " inside a par_group declaration.");
		}
	}
	//TODO: process doc string(s) in the par group?
	
	return id;
}

// NOTE: This one should not be called on a module where all declarations are not loaded yet. This is because the global ids don't exist before the corresponding entity is declared, and it could be declared before it is referenced.
void
make_global(Module_Declaration *module, Value_Location *loc) {
	if(loc->type == Location_Type::located && module->global_scope) {
		auto comp_id = module->compartments[loc->compartment]->global_id;
		if(is_valid(comp_id)) loc->compartment = comp_id;
		auto quant_id = module->properties_and_quantities[loc->property_or_quantity]->global_id;
		if(is_valid(quant_id)) loc->property_or_quantity = quant_id;
		for(int idx = 0; idx < loc->n_dissolved; ++idx) {
			auto quant_id = module->properties_and_quantities[loc->dissolved_in[idx]]->global_id;
			if(is_valid(quant_id)) loc->dissolved_in[idx] = quant_id;
		}
	}
}

// NOTE: The following three should not be called on a module where all declarations are not loaded yet. This is because the global ids don't exist before the corresponding entity is declared, and it could be declared before it is referenced.
Value_Location
make_value_location(Mobius_Model *model, Entity_Id compartment, Entity_Id property_or_quantity) {
	Value_Location result;
	result.n_dissolved = 0;
	result.type = Location_Type::located;
	auto comp = model->find_entity<Reg_Type::compartment>(compartment);
	if(is_valid(comp->global_id))
		result.compartment = comp->global_id;
	else
		result.compartment = compartment;
	
	auto prop = model->find_entity<Reg_Type::property_or_quantity>(property_or_quantity);
	if(is_valid(prop->global_id))
		result.property_or_quantity = prop->global_id;
	else
		result.property_or_quantity = property_or_quantity;
	
	return result;
}

Value_Location
remove_dissolved(const Value_Location &loc) {
	Value_Location result = loc;
	if(loc.n_dissolved == 0)
		fatal_error(Mobius_Error::internal, "Tried to find a value location above one that is not dissolved in anything.");
	result.n_dissolved--;
	result.property_or_quantity = loc.dissolved_in[loc.n_dissolved-1];
	return result;
}

Value_Location
add_dissolved(Mobius_Model *model, const Value_Location &loc, Entity_Id quantity) {
	Value_Location result = loc;
	if(loc.n_dissolved == max_dissolved_chain)
		fatal_error(Mobius_Error::internal, "Tried to find a value location with a dissolved chain that is too long.");
	result.n_dissolved++;
	result.dissolved_in[loc.n_dissolved] = loc.property_or_quantity;
	
	auto quant = model->find_entity<Reg_Type::property_or_quantity>(quantity);
	if(is_valid(quant->global_id)) quantity = quant->global_id;
	
	result.property_or_quantity = quantity;
	
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
		-1, true, -1, true);
	
	Token *name = nullptr;
	if(which == 2) name = single_arg(decl, 1);
	else if(which == 3) name = single_arg(decl, 2);
	
	auto id  = module->hases.find_or_create(&decl->handle_name, name, decl);
	auto has = module->hases[id];
	
	int chain_size = decl->decl_chain.size();
	if(chain_size == 0 || chain_size > max_dissolved_chain + 1) {
		decl->decl_chain.back().print_error_header();
		fatal_error("A \"has\" declaration must either be of the form compartment.has(property_or_quantity) or compartment.<chain>.has(quantity) where <chain> is a .-separated chain of quantity handles that is no more than ", max_dissolved_chain, " long.");
	}
	
	// TODO: can eventually be tied to just a quantity not only a compartment or compartment.quantities
	has->value_location.type = Location_Type::located;
	has->value_location.compartment = module->compartments.find_or_create(&decl->decl_chain[0]);
	if(chain_size > 1) {
		for(int idx = 1; idx < chain_size; ++idx)
			has->value_location.dissolved_in[idx-1] = module->properties_and_quantities.find_or_create(&decl->decl_chain[idx]);
	}
	has->value_location.n_dissolved = chain_size - 1;
	has->value_location.property_or_quantity = resolve_argument<Reg_Type::property_or_quantity>(module, decl, 0);
		
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
			auto str = function->modifiers[0].string_value;
			if(str == "initial" || str == "initial_conc") {
				if(has->initial_code) {
					function->opens_at.print_error_header();
					fatal_error("Declaration has more than one \".initial\" or \".initial_conc\" block.");
				}
				has->initial_code = function->block;
				has->initial_is_conc = (str == "initial_conc");
			} else if(str == "override" || str == "override_conc") {
				if(has->override_code) {
					function->opens_at.print_error_header();
					fatal_error("Declaration has more than one \".override\" or \".override_conc\" block.");
				}
				has->override_code = function->block;
				has->override_is_conc = (str == "override_conc");
			} else {
				function->opens_at.print_error_header();
				fatal_error("Expected either no function body tags, \".initial\" or \".override_conc\".");   //TODO: should maybe come up with a better name than "tag" (?)
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
process_location_argument(Module_Declaration *module, Decl_AST *decl, int which, Value_Location *location, bool allow_unspecified) {
	if(decl->args[which]->decl) {
		decl->args[which]->decl->location.print_error_header();
		fatal_error("Expected a single identifier or a .-separated chain of identifiers.");
	}
	std::vector<Token> &symbol = decl->args[which]->sub_chain;
	int count = symbol.size();
	if(count == 1 && allow_unspecified) {
		Token *token = &symbol[0];
		if(token->string_value == "nowhere")
			location->type = Location_Type::nowhere;
		else if(token->string_value == "out") {
			if(which == 0) {
				token->print_error_header();
				fatal_error("The source of a flux can never be \"out\".");
			}
			location->type = Location_Type::out;
		} else {
			token->print_error_header();
			fatal_error("Invalid variable location.");
		}
	} else if (count >= 2 && count <= max_dissolved_chain + 2) {
		if(decl->args[which]->chain_sep != '.') {
			symbol[0].print_error_header();
			fatal_error("Expected a single identifier or a .-separated chain of identifiers.");
		}
		location->type     = Location_Type::located;
		location->compartment = module->compartments.find_or_create(&symbol[0]);
		//NOTE: this does not guarantee that these are quantities and not properties, so that is checked in post (in model_composition).
		for(int idx = 0; idx < count-2; ++idx) {
			location->dissolved_in[idx] = module->properties_and_quantities.find_or_create(&symbol[idx+1]);
		}
		location->n_dissolved            = count-2;
		location->property_or_quantity   = module->properties_and_quantities.find_or_create(&symbol.back());    
	} else {
		//TODO: Give a reason for why it failed
		symbol[0].print_error_header();
		fatal_error("Invalid variable location.");
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
	
	process_location_argument(module, decl, 0, &flux->source, true);
	process_location_argument(module, decl, 1, &flux->target, true);
	flux->target_was_out = (flux->target.type == Location_Type::out);
	
	if(flux->source == flux->target && flux->source.type == Location_Type::located) {
		decl->location.print_error_header();
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
			decl->location.print_error_header();
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

template<> Entity_Id
process_declaration<Reg_Type::constant>(Module_Declaration *module, Decl_AST *decl) {
	match_declaration(decl, {{Token_Type::quoted_string, Decl_Type::unit, Token_Type::real}});
	
	auto id        = module->constants.standard_declaration(decl);
	auto constant  = module->constants[id];
	
	constant->unit = resolve_argument<Reg_Type::unit>(module, decl, 1);
	constant->value = single_arg(decl, 2)->double_value();
	
	return id;
}


void
check_for_missing_declarations(Module_Declaration *module) {
	for(const auto &entry : module->handles_in_scope) {
		auto reg = module->find_entity(entry.second);
		
		if(!reg->has_been_declared) {
			reg->location.print_error_header();
			//note: the handle_name must be valid for it to have been put in the scope in the first place, so this is ok.
			fatal_error("The handle name \"", reg->handle_name, "\" was referenced, but never declared in this scope.");
		}
	}
}

Decl_AST *
load_top_decl_from_file(Mobius_Model *model, String_View file_name, String_View decl_name, Decl_Type type, String_View rel_path, String_View *normalized_path_out) {
	
	String_View normalized_path;
	auto path_ptr = normalized_path_out ? normalized_path_out : &normalized_path;
	
	String_View file_data = model->file_handler.load_file(file_name, rel_path, path_ptr);
	
	//warning_print("Try to load ", decl_name, " from ", *path_ptr, "\n");
	
	bool already_parsed_file = false;
	Decl_AST *result = nullptr;
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
		Token_Stream stream(file_name, file_data);
			
		while(true) {
			if(stream.peek_token().type == Token_Type::eof) break;
			Decl_AST *decl = parse_decl(&stream);
			if(decl->type != Decl_Type::module && decl->type != Decl_Type::library) {
				decl->location.print_error_header();
				fatal_error("Module files should only have modules or libraries in the top scope. Encountered a \"", name(decl->type), "\".");
			}
			if(!(decl->args.size() >= 1 && decl->args[0]->sub_chain.size() >= 1 && decl->args[0]->sub_chain[0].type == Token_Type::quoted_string)) {
				decl->location.print_error_header();
				fatal_error("Encountered a top-level declaration without a name.");
			}
			String_View name = decl->args[0]->sub_chain[0].string_value;
			if(decl_name == name)
				result = decl;
			model->parsed_decls[*path_ptr][name] = decl;
		}
	}
	
	if(!result)
		fatal_error(Mobius_Error::parsing, "Could not find the ", name(type), " \"", decl_name, "\" in the file ", file_name, " .");
	
	if(result->type != type) {
		result->location.print_error_header();
		fatal_error(Mobius_Error::parsing, "The declaration \"", decl_name, "\" is of type ", name(result->type), ", expected ", name(type), ".");
	}
	
	return result;
}

void
process_load_library_declaration(Module_Declaration *module, Decl_AST *load_decl) {
	match_declaration(load_decl, {{Token_Type::quoted_string, { Decl_Type::library, true } }});
	
	String_View file_name = single_arg(load_decl, 0)->string_value;
	
	for(int idx = 1; idx < load_decl->args.size(); ++idx) {
		auto lib_load_decl = load_decl->args[idx]->decl;
		
		match_declaration(lib_load_decl, {{Token_Type::quoted_string}}, 0, true, 0);
		
		String_View library_name = single_arg(lib_load_decl, 0)->string_value;
		
		Decl_AST *lib_decl = load_top_decl_from_file(module->model, file_name, library_name, Decl_Type::library, module->source_path, nullptr);
		
		match_declaration(lib_decl, {{Token_Type::quoted_string}});
		
		auto body = reinterpret_cast<Decl_Body_AST *>(lib_decl->bodies[0]);

		for(Decl_AST *child : body->child_decls) {
			if(child->type == Decl_Type::function) {
				process_declaration<Reg_Type::function>(module, child);
			} else if(child->type == Decl_Type::constant) {
				process_declaration<Reg_Type::constant>(module, child);
			} else {
				child->location.print_error_header();
				fatal_error("Did not expect a declaration of type ", name(child->type), " inside a library.");
			}
		}
		
		//TODO: we should somehow check that the loaded functions only access other functions (and constants) that were declared in their own scope.
		//   we should probably make a better "declaration scope" system and separate the concept of declaration scope from the concept of module.
	}
}

Module_Declaration *
process_module_declaration(Mobius_Model *model, Module_Declaration *global_scope, s16 module_id, Decl_AST *decl, String_View source_path) {
	
	//TODO: have to decide whether we should copy over string views at this point.
	
	Module_Declaration *module = new Module_Declaration();
	module->global_scope = global_scope;
	module->model = model;
	module->module_id = module_id;
	module->source_path = source_path;
	
	match_declaration(decl, {{Token_Type::quoted_string, Token_Type::integer, Token_Type::integer, Token_Type::integer}});
	
	module->module_name          = single_arg(decl, 0)->string_value;
	module->version.major        = single_arg(decl, 1)->val_int;
	module->version.minor        = single_arg(decl, 2)->val_int;
	module->version.revision     = single_arg(decl, 3)->val_int;

	auto body = reinterpret_cast<Decl_Body_AST *>(decl->bodies[0]);
	
	if(body->doc_string.string_value)
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
			
			case Decl_Type::constant : {
				process_declaration<Reg_Type::constant>(module, child);
			} break;
			
			case Decl_Type::load : {
				process_load_library_declaration(module, child);
			} break;
			
			default : {
				child->location.print_error_header();
				fatal_error("Did not expect a declaration of type ", name(child->type), " inside a module declaration.");
			};
		}
	}
	
	check_for_missing_declarations(module);
	
	// NOTE: this has to be done after all declarations are processed. This is because the global id of a compartment, quantity or property does not exist before it is declared, and that could happen after it was referenced.
	for(auto has : module->hases)
		make_global(module, &module->hases[has]->value_location);
	for(auto flux : module->fluxes) {
		make_global(module, &module->fluxes[flux]->source);
		make_global(module, &module->fluxes[flux]->target);
	}
	// hmm, this one is probably never necessary since it always happens at model scope:
	for(auto solve : module->solves)
		make_global(module, &module->solves[solve]->loc);
	
	return module;
}


Entity_Id
expect_exists(Module_Declaration *module, Token *handle_name, Reg_Type reg_type) {
	auto result = module->find_handle(handle_name->string_value);
	
	if(!is_valid(result)) {
		handle_name->print_error_header();
		fatal_error("There is no entity with the identifier \"", handle_name->string_value, "\" in the referenced scope.");
	}
	if(result.reg_type != reg_type) {
		handle_name->print_error_header();
		fatal_error("The entity \"", handle_name->string_value, "\" does not have the type ", name(reg_type), ".");
	}
	
	return result;
}

void
process_to_declaration(Mobius_Model *model, string_map<s16> *module_ids, Decl_AST *decl) {
	// Process a "to" declaration
	
	match_declaration(decl, {{Token_Type::identifier}}, 2, false);
	
	String_View module_handle = decl->decl_chain[0].string_value;
	auto find = module_ids->find(module_handle);
	if(find == module_ids->end()) {
		decl->decl_chain[0].print_error_header();
		fatal_error("The module identifier \"", module_handle, "\" was not declared.");
	}
	s16 module_id = find->second;
	Module_Declaration *module = model->modules[module_id];
	
	Token *flux_handle = &decl->decl_chain[1];
	Entity_Id flux_id = expect_exists(module, flux_handle, Reg_Type::flux);
	
	auto flux = module->fluxes[flux_id];
	if(flux->target.type != Location_Type::out) {
		decl->decl_chain[1].print_error_header();
		fatal_error("The flux \"", flux_handle->string_value, "\" does not have the target \"out\", and so we can't re-assign its target.");
	}
	
	auto &chain = decl->args[0]->sub_chain;
	
	if(chain.size() >= 2) {
		process_location_argument(model->modules[0], decl, 0, &flux->target, false);
	} else if (chain.size() == 1) {
		Token *neigh_tk = &chain[0];
		auto neigh_id = expect_exists(model->modules[0], neigh_tk, Reg_Type::neighbor);
		
		flux->target.type     = Location_Type::neighbor;
		flux->target.neighbor = neigh_id;
	} else {
		chain[0].print_error_header();
		fatal_error("This is not a well-formatted flux target. Expected something on the form compartment.quantity or neighbor .");
	}
	
	// NOTE: in model scope, all compartment and quantity/property declarations are processed first, so it is ok to just look them up here.
}

void
process_no_carry_declaration(Mobius_Model *model, string_map<s16> *module_ids, Decl_AST *decl) {
	// Oops, lots of code duplication from process_to_declaration
	match_declaration(decl, {{Token_Type::identifier}}, 2, false);
	
	String_View module_handle = decl->decl_chain[0].string_value;
	auto find = module_ids->find(module_handle);
	if(find == module_ids->end()) {
		decl->decl_chain[0].print_error_header();
		fatal_error("The module identifier \"", module_handle, "\" was not declared.");
	}
	s16 module_id = find->second;
	Module_Declaration *module = model->modules[module_id];
	
	Token *flux_handle = &decl->decl_chain[1];
	Entity_Id flux_id = expect_exists(module, flux_handle, Reg_Type::flux);
	
	auto flux = module->fluxes[flux_id];
	
	Value_Location loc;
	process_location_argument(model->modules[0], decl, 0, &loc, false);
	
	bool found = false;
	auto above = loc;
	while(above.n_dissolved > 0) {
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
process_declaration<Reg_Type::index_set>(Module_Declaration *module, Decl_AST *decl) {
	//TODO: index set type
	
	match_declaration(decl, {{Token_Type::quoted_string}});
	return module->index_sets.standard_declaration(decl);
}

template<> Entity_Id
process_declaration<Reg_Type::solver>(Module_Declaration *module, Decl_AST *decl) {
	int which = match_declaration(decl, {
		{Token_Type::quoted_string, Token_Type::quoted_string, Token_Type::real},
		{Token_Type::quoted_string, Token_Type::quoted_string, Token_Type::real, Token_Type::real},
	});
	auto id = module->solvers.standard_declaration(decl);
	auto solver = module->solvers[id];
	
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
process_declaration<Reg_Type::solve>(Module_Declaration *module, Decl_AST *decl) {
	match_declaration(decl, {{Token_Type::identifier}}, 1, false);
	
	auto id = module->solves.find_or_create(nullptr, nullptr, decl);
	auto solve = module->solves[id];
	
	solve->solver = module->solvers.find_or_create(&decl->decl_chain[0]);
	process_location_argument(module, decl, 0, &solve->loc, false);
	
	return id;
}

template<> Entity_Id
process_declaration<Reg_Type::neighbor>(Module_Declaration *module, Decl_AST *decl) {
	match_declaration(decl, {{Token_Type::quoted_string, Token_Type::identifier}}, 1);
	
	auto id       = module->neighbors.standard_declaration(decl);
	auto neighbor = module->neighbors[id];
	
	auto index_set            = module->index_sets.find_or_create(&decl->decl_chain[0]);
	String_View structure_type = single_arg(decl, 1)->string_value;
	
	neighbor->index_set = index_set;
	module->index_sets[index_set]->neighbor_structure = id;
	if(structure_type == "directed_tree")
		neighbor->type = Neighbor_Structure_Type::directed_tree;
	else {
		single_arg(decl, 1)->print_error_header();
		fatal_error("Unsupported neighbor structure type \"", structure_type, "\".");
	}
	
	return id;
}

void
process_distribute_declaration(Module_Declaration *module, Decl_AST *decl) {
	match_declaration(decl, {{{Decl_Type::index_set, true}}}, 1, false);
	
	auto compartment = module->compartments.find_or_create(&decl->decl_chain[0]);
	
	//TODO: some guard against overlapping / contradictory declarations.
	for(int idx = 0; idx < decl->args.size(); ++idx) {
	
		auto index_set = resolve_argument<Reg_Type::index_set>(module, decl, idx);
		module->compartments[compartment]->index_sets.push_back(index_set);
	}
}

void
process_aggregation_weight_declaration(Module_Declaration *module, Decl_AST *decl) {
	match_declaration(decl, {{Decl_Type::compartment, Decl_Type::compartment}}, 0, false);
	
	auto from_comp = resolve_argument<Reg_Type::compartment>(module, decl, 0);
	auto to_comp   = resolve_argument<Reg_Type::compartment>(module, decl, 1);
	
	//TODO: some guard against overlapping / contradictory declarations.
	
	auto function = reinterpret_cast<Function_Body_AST *>(decl->bodies[0]);
	module->compartments[from_comp]->aggregations.push_back({to_comp, function->block});
}

void
process_unit_conversion_declaration(Module_Declaration *module, Decl_AST *decl) {
	match_declaration(decl, {{Token_Type::identifier, Token_Type::identifier}});
	
	Flux_Unit_Conversion_Data data = {};
	
	//TODO: some guard against overlapping / contradictory declarations.
	//TODO: guard against nonsensical declarations (e.g. going between the same compartment).
	
	process_location_argument(module, decl, 0, &data.source, false);
	process_location_argument(module, decl, 1, &data.target, false);
	data.code = reinterpret_cast<Function_Body_AST *>(decl->bodies[0])->block;
	
	module->compartments[data.source.compartment]->unit_convs.push_back(data);
}

void
register_intrinsics(Module_Declaration *module) {
	//module->dimensionless_unit = module->units.create_compiler_internal("__dimensionless__", Decl_Type::unit); //TODO: give it data if necessary.
	
	#define MAKE_INTRINSIC1(name, emul, llvm, ret_type, type1) \
		{ \
			auto fun = module->functions.create_compiler_internal(#name, Decl_Type::function); \
			module->functions[fun]->fun_type = Function_Type::intrinsic; \
			module->functions[fun]->args = {"a"}; \
		}
	#define MAKE_INTRINSIC2(name, emul, ret_type, type1, type2) \
		{ \
			auto fun = module->functions.create_compiler_internal(#name, Decl_Type::function); \
			module->functions[fun]->fun_type = Function_Type::intrinsic; \
			module->functions[fun]->args = {"a", "b"}; \
		}
	#include "intrinsics.incl"
	#undef MAKE_INTRINSIC1
	#undef MAKE_INTRINSIC2
	
	//TODO: We should actually make it so that these can't be referenced in code (i.e. not have a handle)
	auto system_id = module->par_groups.create_compiler_internal("system", Decl_Type::par_group, "System");
	auto start_id  = module->parameters.create_compiler_internal("start_date", Decl_Type::par_datetime, "Start date");
	auto end_id    = module->parameters.create_compiler_internal("end_date", Decl_Type::par_datetime, "End date");
	
	auto system = module->par_groups[system_id];
	auto start  = module->parameters[start_id];
	auto end    = module->parameters[end_id];
	
	Date_Time default_start(1970, 1, 1);
	Date_Time default_end(1970, 1, 16);
	Date_Time min_date(1000, 1, 1);
	Date_Time max_date(3000, 1, 1);
	
	system->parameters.push_back(start_id);
	system->parameters.push_back(end_id);
	system->compartment = invalid_entity_id;
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


s16
Mobius_Model::load_module(String_View file_name, String_View module_name) {
	
	auto global_scope = modules[0];
	String_View normalized_path;
	Decl_AST *module_decl = load_top_decl_from_file(this, file_name, module_name, Decl_Type::module, global_scope->source_path, &normalized_path);
	
	for(int module_idx = 1; module_idx < modules.size(); ++module_idx) {
		if(modules[module_idx]->module_name == module_name && modules[module_idx]->source_path == normalized_path)
			return module_idx;    // NOTE: we already loaded this module from this file.
	}
	
	s16 module_id = (s16)modules.size();
	Module_Declaration *module = process_module_declaration(this, global_scope, module_id, module_decl, normalized_path); //TODO: should be the normalized file name.
	modules.push_back(module);
	return module_id;
}


Decl_AST *
read_model_ast_from_file(File_Data_Handler *handler, String_View file_name, String_View rel_path = {}, String_View *normalized_path_out = nullptr) {
	String_View model_data = handler->load_file(file_name, rel_path, normalized_path_out);
	
	Token_Stream stream(file_name, model_data);
	Decl_AST *decl = parse_decl(&stream);
	if(decl->type != Decl_Type::model || stream.peek_token().type != Token_Type::eof) {
		decl->location.print_error_header();
		fatal_error("Model files should only have a single model declaration in the top scope.");
	}
	
	match_declaration(decl, {{Token_Type::quoted_string}}, 0, false);
	
	return decl;
}

bool
load_model_extensions(File_Data_Handler *handler, Decl_AST *from_decl, string_map<Decl_AST *> &loaded_out, String_View rel_path) {
	auto body = reinterpret_cast<Decl_Body_AST *>(from_decl->bodies[0]);
	
	for(Decl_AST *child : body->child_decls) {
		if(child->type == Decl_Type::extend) {
			match_declaration(child, {{Token_Type::quoted_string}});
			String_View extend_file_name = single_arg(child, 0)->string_value;
			
			// TODO: It is a bit unnecessary to read the AST from the file before we check that the normalized path is not already in the dictionary.
			//  but right now that happens in the same function call.
			String_View normalized_path;
			auto extend_model = read_model_ast_from_file(handler, extend_file_name, rel_path, &normalized_path);
			
			if(loaded_out.find(normalized_path) != loaded_out.end()) {
				begin_error(Mobius_Error::parsing);
				error_print("There is circularity in the model extensions:\n", extend_file_name, "\n");
				delete extend_model;
				return false;
			}
			
			loaded_out[normalized_path] = extend_model;
			
			// Load extensions of extensions.
			bool success = load_model_extensions(handler, extend_model, loaded_out, normalized_path);
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
	
	model->model_name = single_arg(decl, 0)->string_value;
	
	auto body = reinterpret_cast<Decl_Body_AST *>(decl->bodies[0]);
	
	if(body->doc_string.type == Token_Type::quoted_string)
		model->doc_string = body->doc_string.string_value;
	
	auto global_scope = new Module_Declaration();
	global_scope->module_id = 0;
	global_scope->source_path = file_name;
	
	register_intrinsics(global_scope);
	
	string_map<Decl_AST *> extend_models;
	extend_models[file_name] = decl;
	load_model_extensions(&model->file_handler, decl, extend_models, file_name);
	
	//TODO: now we just throw everything into a single namespace, but what happens if we have re-declarations of handles because of multiple extensions?
	
	//note: this must happen before we load the modules, otherwise these declarations would be re-declarations into the global scope of something that was created by the modules.
	for(auto &extend : extend_models) {
		auto body = reinterpret_cast<Decl_Body_AST *>(extend.second->bodies[0]);
		for(Decl_AST *child : body->child_decls) {
			switch (child->type) {
				case Decl_Type::compartment : {
					process_declaration<Reg_Type::compartment>(global_scope, child);
				} break;
				
				case Decl_Type::quantity : {
					process_declaration<Reg_Type::property_or_quantity>(global_scope, child);
				} break;
				
				case Decl_Type::neighbor : {
					process_declaration<Reg_Type::neighbor>(global_scope, child);  // NOTE: we also put this here since we expect we will need referencing it inside modules eventually.
				} break;
			}
		}
	}
	
	model->modules.push_back(global_scope);
	
	// We then have to do all loads before we start linking things up.
	for(auto &extend : extend_models) {
		auto body = reinterpret_cast<Decl_Body_AST *>(extend.second->bodies[0]);
		for(Decl_AST *child : body->child_decls) {
			//TODO: do libraries also.
			
			switch (child->type) {
				case Decl_Type::load : {
					match_declaration(child, {{Token_Type::quoted_string, {Decl_Type::module, true}}}, 0, false);
					String_View file_name = single_arg(child, 0)->string_value;
					for(int idx = 1; idx < child->args.size(); ++idx) {
						Decl_AST *module_spec = child->args[idx]->decl;
						match_declaration(module_spec, {{Token_Type::quoted_string}}, 0, true, 0);  //TODO: allow specifying the version also?
						String_View module_name = single_arg(module_spec, 0)->string_value;
						
						s16 module_id = model->load_module(file_name, module_name);
						auto hn = module_spec->handle_name.string_value;
						if(hn) {
							if(model->module_ids.find(hn) != model->module_ids.end()) {
								module_spec->handle_name.print_error_header();
								fatal_error("Re-declaration of handle ", hn, ".");
							}
							model->module_ids[hn] = module_id;
						}
					}
				} break;
				
				case Decl_Type::module : {
					s16 module_id = (s16)model->modules.size();
					Module_Declaration *module = process_module_declaration(model, global_scope, module_id, child, global_scope->source_path);
					model->modules.push_back(module);
					auto hn = child->handle_name.string_value;
					if(hn) {
						if(model->module_ids.find(hn) != model->module_ids.end()) {
							child->handle_name.print_error_header();
							fatal_error("Re-declaration of handle ", hn, ".");
						}
						model->module_ids[hn] = module_id;
					}
				} break;
			}
		}
	}
	
	for(auto &extend : extend_models) {
		auto body = reinterpret_cast<Decl_Body_AST *>(extend.second->bodies[0]);
		
		for(Decl_AST *child : body->child_decls) {
			switch (child->type) {
				case Decl_Type::to : {
					process_to_declaration(model, &model->module_ids, child);
				} break;
				
				case Decl_Type::no_carry : {
					process_no_carry_declaration(model, &model->module_ids, child);
				} break;
				
				case Decl_Type::index_set : {
					process_declaration<Reg_Type::index_set>(global_scope, child);
				} break;
				
				case Decl_Type::distribute : {
					process_distribute_declaration(global_scope, child);
				} break;
				
				case Decl_Type::solver : {
					process_declaration<Reg_Type::solver>(global_scope, child);
				} break;
				
				case Decl_Type::solve : {
					process_declaration<Reg_Type::solve>(global_scope, child);
				} break;
				
				case Decl_Type::par_group : {
					process_declaration<Reg_Type::par_group>(global_scope, child);
				} break;
				
				case Decl_Type::aggregation_weight : {
					process_aggregation_weight_declaration(global_scope, child);
				} break;
				
				case Decl_Type::unit_conversion : {
					process_unit_conversion_declaration(global_scope, child);
				} break;
				
				case Decl_Type::extend :
				case Decl_Type::compartment :
				case Decl_Type::quantity :
				case Decl_Type::neighbor :
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
	
	check_for_missing_declarations(global_scope);
	
	return model;
}


void
error_print_location(Mobius_Model *model, Value_Location &loc) {
	//TODO: only works for located ones right now.
	//TODO: this only works if these compartments and quantities were declared with a handle in the model scope. It "forgets" what handle was used in the scope of the original declaration of the location! Maybe print the "name" instead of the "handle_name" ???
	
	auto comp = model->find_entity(loc.compartment);
	error_print(comp->handle_name, '.');
	for(int idx = 0; idx < loc.n_dissolved; ++idx) {
		auto quant = model->find_entity(loc.dissolved_in[idx]);
		error_print(quant->handle_name, '.');
	}
	auto quant = model->find_entity(loc.property_or_quantity);
	error_print(quant->handle_name);
}

