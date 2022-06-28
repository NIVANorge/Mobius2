
#include "model_declaration.h"
#include <algorithm>

Registry_Base *
Module_Declaration::registry(Decl_Type decl_type) {
	switch(decl_type) {
		case Decl_Type::compartment : return &compartments;
		case Decl_Type::par_group :   return &par_groups;
		case Decl_Type::par_real :    return &parameters;
		case Decl_Type::unit :        return &units;
		case Decl_Type::substance :
		case Decl_Type::property :    return &properties_and_substances;
		case Decl_Type::has :         return &hases;
	}
	
	fatal_error(Mobius_Error::internal, "Unhandled entity type in registry().");
	
	return nullptr;
}


inline Entity_Registration_Base *
find_entity(Module_Declaration *module, std::pair<Decl_Type, entity_id> id) {
	return (*module->registry(id.first))[id.second];
}

void
see_if_handle_name_exists_in_scope(Module_Declaration *module, Token *handle_name) {
	
	// check for reserved names
	static String_View reserved[] = {
		#define ENUM_VALUE(name, body_type) #name,
		#include "decl_types.incl"
		#undef ENUM_VALUE
		
		#include "other_reserved.incl"
	};
	
	if(std::find(std::begin(reserved), std::end(reserved), handle_name->string_value) != std::end(reserved)) {
		handle_name->print_error_header();
		fatal_error("The name \"", handle_name->string_value, "\" is reserved.");
	}
	
	auto find = module->handles_in_scope.find(handle_name->string_value);
	if(find != module->handles_in_scope.end()) {
		handle_name->print_error_header();
		error_print("The name \"", handle_name->string_value, "\" was already used here:\n");
		auto reg = find_entity(module, find->second);
		reg->location.print_error();
		error_print("with type ", name(reg->type), ".");
	}
}

template <Decl_Type decl_type> entity_id
Registry<decl_type>::find_or_create(Token *handle_name, Token *name, Decl_AST *declaration) {
	
	entity_id found_id = invalid_entity_id;
	
	if(is_valid(handle_name)) {
		// this is either a lookup of an already registered handle, or it is a new declaration with a given handle.
		
		auto find = handle_name_to_handle.find(handle_name->string_value);
		if(find != handle_name_to_handle.end()) {
			// the handle has already been registered
			
			found_id = find->second;
			
			if(declaration && registrations[find->second].has_been_declared) {
				// the handle was used for a previous declaration (of the same type), and this is also a declaration (in the same module scope), which is not allowed.
				
				handle_name->print_error_header();
				error_print("Re-declaration of handle \"", handle_name->string_value, "\". It was previously declared here:\n");
				registrations[find->second].location.print_error();
				mobius_error_exit();
			}
			
			if(!declaration)  // this is not a declaration, we only wanted to look up the id for this handle.
				return found_id;
		}
		
	} else if (name) {
		// the handle was not given. This is either an anonymous (i.e. handle-less) declaration, or it is a name-based lookup.
		
		auto find = name_to_handle.find(name->string_value);
		if(find != name_to_handle.end()) {
			// the name was already used for this type.
			
			if(declaration) {
				name->print_error_header();
				error_print("Re-declaration of an entity of name ", name->string_value, " of the same type as one previously declared here:\n");
				registrations[find->second].location.print_error();
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
	
	
	if(!is_valid(found_id)) {
		// The entity was not already created, so we have to do that.
	
		found_id = (entity_id)registrations.size();
		registrations.push_back({});
	
		if(is_valid(handle_name)) {
			// See if the handle is reserved or exists for another entity type.
			see_if_handle_name_exists_in_scope(parent, handle_name);
			
			handle_name_to_handle[handle_name->string_value] = found_id;
			parent->handles_in_scope[handle_name->string_value] = {decl_type, found_id};
		}
		
		if(name)
			name_to_handle[name->string_value] = found_id;
	}
	
	auto registration = &registrations[found_id];
	
	// note: this overwrites the handle name if it was already registered, but that is not really a problem (since it is the same).
	// also, we want the location to be overwritten with the declaration.
	if(is_valid(handle_name)) {
		registration->handle_name = handle_name->string_value;
		registration->location    = handle_name->location;
	} else
		registration->location    = declaration->type_name.location;
	
	if(name)
		registration->name = name->string_value;
	
	//NOTE: this could be different from the decl_type in some instances since we use the same registry for e.g. substances and properties, or different parameter types.
	registration->type              = declaration->type;        
	registration->has_been_declared = (declaration != nullptr);
	
	return found_id;
}



//TODO: Maybe classifying the arguments should be done in the AST parsing instead??
//TODO: Make a general-purpose tagged union?
struct Arg_Pattern {
	enum class Type { value, decl, unit_literal };
	Type pattern_type;
	
	union {
		Token_Type token_type;
		Decl_Type  decl_type;
	};
	
	Arg_Pattern(Token_Type token_type) : token_type(token_type), pattern_type(Type::value) {}
	Arg_Pattern(Decl_Type decl_type)   : decl_type(decl_type), pattern_type(Type::decl) {}
	Arg_Pattern() : pattern_type(Type::unit_literal) {}
	
	bool matches(Argument_AST *arg) const {
		Token_Type check_type = token_type;
		
		switch(pattern_type) {
			case Type::unit_literal : {
				int count = arg->sub_chain.size();
				if(!arg->decl && (count == 1 || (count <= 3 && arg->chain_sep == ' ')))
					return true; //NOTE: only potentially true. Must be properly checked in the process_unit_declaration
				return false;
			} break;
		
			case Type::decl : {
				if(arg->decl && (arg->decl->type == decl_type)) return true;
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
};

inline int
match_declaration(Decl_AST *decl, const std::initializer_list<std::initializer_list<Arg_Pattern>> &patterns, int allow_chain = 0, bool allow_handle = true, bool allow_multiple_bodies = false, bool allow_body_modifiers = false) {
	// allow_chain = 0 means no chain. allow_chain=-1 means any length. allow_chain = n means only of length n exactly.
	
	//TODO: need much better error messages!
	
	if(!allow_chain && !decl->decl_chain.empty()) {
		decl->decl_chain[0].print_error_header();
		fatal_error("This should not be a chained declaration.");
	}
	if(allow_chain > 0 && decl->decl_chain.size() != allow_chain) {
		decl->decl_chain[0].print_error_header();
		fatal_error("There should be ", allow_chain, " elements in the declaration chain. We found ", decl->decl_chain.size(), ".");
	}
	if(!allow_handle && decl->handle_name.string_value.count > 0) {
		decl->handle_name.print_error_header();
		fatal_error("This declaration should not have a handle");
	}
	
	int found_match = -1;
	int idx = -1;
	for(const auto &pattern : patterns) {
		++idx;
		if(decl->args.size() != pattern.size()) continue;
		
		bool cont = false;
		auto match = pattern.begin();
		for(auto arg : decl->args) {
			if(!match->matches(arg)) {
				cont = true;
				break;
			}
			++match;
		}
		if(cont) continue;
		
		found_match = idx;
		break;
	}
	
	if(found_match == -1) {
		decl->type_name.print_error_header();
		fatal_error("The arguments to ", decl->type_name.string_value, " don't match any possible declaration pattern.");
		// TODO: print the possible patterns
	}
	
	// NOTE: We already checked in the AST processing stage if the declaration is allowed to have bodies at all. This just checks if it can have more than one.
	if(!allow_multiple_bodies && decl->bodies.size() > 1) {
		decl->type_name.print_error_header();
		fatal_error("This declaration should not have multiple bodies.");
	}
	
	if(!allow_body_modifiers) {
		for(auto body : decl->bodies) {
			if(body->modifiers.size() > 0) {
				decl->type_name.print_error_header();
				fatal_error("The bodies of this declaration should not have modifiers.");
			}
		}
	}
	
	return found_match;
}

inline Token *
single_arg(Decl_AST *decl, int which) {
	return &decl->args[which]->sub_chain[0];
}

template<Decl_Type decl_type> entity_id
Registry<decl_type>::standard_declaration(Decl_AST *decl) {
	Token *name = single_arg(decl, 0);
	return find_or_create(&decl->handle_name, name, decl);
}

template<Decl_Type decl_type> entity_id
process_declaration(Module_Declaration *module, Decl_AST *decl); //NOTE: this will be template specialized below.

template<Decl_Type expected_type> entity_id
resolve_argument(Module_Declaration *module, Decl_AST *decl, int which, int max_sub_chain_size = 1) {
	// We could do more error checking here, but it should really only be called after calling match_declaration...
	
	Argument_AST *arg = decl->args[which];
	if(arg->decl) {
		if(arg->decl->type != expected_type)
			fatal_error(Mobius_Error::internal, "Mismatched type in type resolution"); // This should not have happened since we should have checked this aready.
		
		return process_declaration<expected_type>(module, arg->decl);
	} else {
		if(max_sub_chain_size > 0 && arg->sub_chain.size() > max_sub_chain_size) {
			arg->sub_chain[0].print_error_header();
			fatal_error("Units can not be a member of another entity");
		}
		
		return module->registry(expected_type)->find_or_create(&arg->sub_chain[0]);
	}
	return invalid_entity_id;
}

template<> entity_id
process_declaration<Decl_Type::unit>(Module_Declaration *module, Decl_AST *unit) {
	
	for(Argument_AST *arg : unit->args) {
		if(!Arg_Pattern().matches(arg)) {
			unit->type_name.print_error_header();
			fatal_error("Invalid argument to unit declaration.");
		}
	}
	
	return invalid_entity_id;
}

template<> entity_id
process_declaration<Decl_Type::par_real>(Module_Declaration *module, Decl_AST *decl) {
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
	
	parameter->unit                    = resolve_argument<Decl_Type::unit>(module, decl, 1);
	parameter->default_val.val_double  = single_arg(decl, 2)->double_value();
	if(which == 2 || which == 3) {
		parameter->min_val.val_double  = single_arg(decl, 3)->double_value();
		parameter->max_val.val_double  = single_arg(decl, 4)->double_value();
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

template<> entity_id
process_declaration<Decl_Type::par_group>(Module_Declaration *module, Decl_AST *decl) {
	match_declaration(decl, {{Token_Type::quoted_string}}, 1);

	auto id        = module->par_groups.standard_declaration(decl);
	auto par_group = module->par_groups[id];
	
	//TODO: Do we always need to require that a par group is tied a compartment?
	//TODO: Actually, it could be tied to a substance too, once we get to implement that!
	par_group->compartment = module->compartments.find_or_create(&decl->decl_chain[0]);
	
	auto body = reinterpret_cast<Decl_Body_AST *>(decl->bodies[0]);
	
	for(Decl_AST *child : body->child_decls) {
		if(child->type == Decl_Type::par_real) {   //TODO: do other parameter types
			auto par_handle = process_declaration<Decl_Type::par_real>(module, child);
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

template<> entity_id
process_declaration<Decl_Type::property>(Module_Declaration *module, Decl_AST *decl) {
	int which = match_declaration(decl,
		{
			{Token_Type::quoted_string},
			{Token_Type::quoted_string, Decl_Type::unit},
		});

	auto id       = module->properties_and_substances.standard_declaration(decl);
	auto property = module->properties_and_substances[id];
	
	if(which == 1)
		property->unit = resolve_argument<Decl_Type::unit>(module, decl, 1);
	else
		property->unit = invalid_entity_id;
	
	return id;
}

template<> entity_id
process_declaration<Decl_Type::substance>(Module_Declaration *module, Decl_AST *decl) {
	return process_declaration<Decl_Type::property>(module, decl);
}

template<> entity_id
process_declaration<Decl_Type::has>(Module_Declaration *module, Decl_AST *decl) {
	int which = match_declaration(decl,
		{
			{Decl_Type::property},
			{Decl_Type::property, Decl_Type::unit},
		},
		1, true, true, true);
	
	auto id  = module->hases.find_or_create(&decl->handle_name, nullptr, decl);
	auto has = module->hases[id];
	
	// TODO: can eventually be tied to a substance not only a compartment.
	has->compartment = module->compartments.find_or_create(&decl->decl_chain[0]);
	has->property_or_substance = resolve_argument<Decl_Type::property>(module, decl, 0);
	
	if(which == 1)
		has->override_unit = resolve_argument<Decl_Type::unit>(module, decl, 1);
	else
		has->override_unit = invalid_entity_id;
	
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

Module_Declaration *
process_module_declaration(Decl_AST *decl) {
	
	//TODO: have to decide whether we should copy over string views at this point.
	
	Module_Declaration *module = new Module_Declaration();
	
	match_declaration(decl, {{Token_Type::quoted_string, Token_Type::integer, Token_Type::integer, Token_Type::integer}}, 0, false, true, false);
	
	module->name  = single_arg(decl, 0)->string_value;
	module->major = single_arg(decl, 1)->val_int;
	module->minor = single_arg(decl, 2)->val_int;
	module->revision = single_arg(decl, 3)->val_int;

	auto body = reinterpret_cast<Decl_Body_AST *>(decl->bodies[0]);
	
	if(body->doc_string.type == Token_Type::quoted_string)
		module->doc_string = body->doc_string.string_value;
	
	for(Decl_AST *child : body->child_decls) {
		
		//TODO: finish!
		switch(child->type) {
			case Decl_Type::compartment : {
				match_declaration(child, {{Token_Type::quoted_string}});
				module->compartments.standard_declaration(child);;
			} break;
			
			case Decl_Type::par_group : {
				process_declaration<Decl_Type::par_group>(module, child);
			} break;
			
			case Decl_Type::substance : {
				process_declaration<Decl_Type::substance>(module, child);
			} break;
			
			case Decl_Type::property : {
				process_declaration<Decl_Type::property>(module, child);
			} break;
			
			case Decl_Type::has : {
				process_declaration<Decl_Type::has>(module, child);
			} break;
			
			case Decl_Type::flux : {
				
			} break;
			
			case Decl_Type::unit : {
				process_declaration<Decl_Type::unit>(module, child);
			} 
			
			default : {
				child->type_name.print_error_header();
				fatal_error("Did not expect a declaration of type ", child->type_name.string_value, " inside a module declaration.");
			};
		}
	}
	
	for(const auto &entry : module->handles_in_scope) {
		auto reg = find_entity(module, entry.second);
		
		if(!reg->has_been_declared) {
			reg->location.print_error_header();
			//note: the handle_name must be valid for it to have been put in the scope in the first place, so this is ok.
			fatal_error("The name \"", reg->handle_name, "\" was used, but never declared.");
		}
	}
	
	return module;
}



