
#include "model_declaration.h"
#include <algorithm>


Entity_Registration *
find_entity(Module_Declaration *module, std::pair<Decl_Type, entity_handle> entity_id) {
	//TODO: this is stupid.. how to not have to write this?
	switch(entity_id.first) {
		case Decl_Type::compartment :
			return module->compartments[entity_id.second];
		case Decl_Type::par_group :
			return module->par_groups[entity_id.second];
		case Decl_Type::par_real :
			return module->parameters[entity_id.second];
		case Decl_Type::unit :
			return module->units[entity_id.second];
		case Decl_Type::substance :
			return module->substances[entity_id.second];
			
		//TODO..
		
		default :
			fatal_error(Mobius_Error::internal, "Unhandled entity type in find_entity.");
	}
	return nullptr;
}

void
see_if_handle_name_exists_in_scope(Module_Declaration *module, Token *handle_name) {
	
	// check for reserved names
	static String_View reserved[] = {
		#define ENUM_VALUE(name, body_type) #name,
		#include "decl_types.incl"
		#undef ENUM_VALUE
		"if", "otherwise", "nowhere", "out", "from", "to",
		//todo: probably more?
	};
	
	if(std::find(std::begin(reserved), std::end(reserved), handle_name->string_value) != std::end(reserved)) {
		handle_name->print_error_header();
		fatal_error("The name \"", handle_name->string_value, "\" is reserved.");
	}
	
	auto find = module->handles_in_scope.find(handle_name->string_value);
	if(find != module->handles_in_scope.end()) {
		handle_name->print_error_header();
		error_print("The name \"", handle_name->string_value, "\" was already used here:\n");
		Entity_Registration *reg = find_entity(module, find->second);
		reg->handle_name.print_error_location();
		error_print("with type ", name(reg->type), ".");
	}
}

template <typename Registration_Type, Decl_Type decl_type> entity_handle
Registry<Registration_Type, decl_type>::find_or_create(Token *handle_name, Token *name, bool this_is_the_declaration) {
		
	if(handle_name) {
		auto find = handle_name_to_handle.find(handle_name->string_value);
		if(find != handle_name_to_handle.end()) {
			if(this_is_the_declaration && registrations[find->second].has_been_declared) {
				handle_name->print_error_header();
				fatal_error("Re-declaration of handle \"", handle_name->string_value, "\". It was previously declared here:\n");
				registrations[find->second].handle_name.print_error_location();
			}
			
			return find->second;
		}
	} else if (name) {
		auto find = name_to_handle.find(name->string_value);
		if(find != name_to_handle.end())
			return find->second;
		else
			return invalid_entity_handle;
	}
	
	// See if the handle is reserved or exists for another entity type.
	if(handle_name)
		see_if_handle_name_exists_in_scope(parent, handle_name);
	
	
	// We did not find it, so we create it.
	entity_handle h = (entity_handle)registrations.size();
	Registration_Type registration = {};
	if(handle_name) registration.handle_name = *handle_name;
	registration.type        = decl_type;
	if(name) registration.name = name->string_value;
	registration.has_been_declared = this_is_the_declaration;
	registrations.push_back(registration);
	
	if(handle_name) {
		handle_name_to_handle[handle_name->string_value] = h;
		parent->handles_in_scope[handle_name->string_value] = {decl_type, h};
	}
	if(name)
		name_to_handle[name->string_value] = h;
	
	return h;
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

entity_handle
process_unit_declaration(Module_Declaration *, Decl_AST *);

//TODO: this also has to handle the chain somehow..
inline entity_handle
resolve_argument(Module_Declaration *module, Decl_AST *decl, int which, Decl_Type expected_type) {
	// We could do more error checking here, but it should really only be called after calling match_declaration...
	
	Argument_AST *arg = decl->args[which];
	if(arg->decl) {
		if(arg->decl->type != expected_type)
			fatal_error(Mobius_Error::internal, "Mismatched type in type resolution"); // This should not have happened since we should have checked this aready.
		
		switch(expected_type) {
			case Decl_Type::unit : {
				return process_unit_declaration(module, arg->decl);
			} break;
			
			default :
				fatal_error(Mobius_Error::internal, "Undhandled type in type resolution.");
		};
	} else {
		switch(expected_type) {
			case Decl_Type::unit : {
				if(arg->sub_chain.size() != 1) {
					arg->sub_chain[0].print_error_header();
					fatal_error("Units can not be a member of another entity");
				}
				return module->units.find_or_create(&arg->sub_chain[0]);
			} break;
			
			default :
				fatal_error(Mobius_Error::internal, "Undhandled type in type resolution.");
		};
	}
	return invalid_entity_handle;
}

entity_handle
process_unit_declaration(Module_Declaration *module, Decl_AST *unit) {
	
	for(Argument_AST *arg : unit->args) {
		if(!Arg_Pattern().matches(arg)) {
			fatal_error("Invalid argument to unit declaration.");   //TODO: give location!!!!
		}
	}
	
	return invalid_entity_handle;
}

entity_handle
process_par_declaration(Module_Declaration *module, Decl_AST *decl, entity_handle par_group) {
	Token_Type value_type = Token_Type::real; //TODO: allow other types.
	
	int which = match_declaration(decl,
		{
			{Token_Type::quoted_string, Decl_Type::unit, value_type},                                                      // 0
			{Token_Type::quoted_string, Decl_Type::unit, value_type, Token_Type::quoted_string},                           // 1
			{Token_Type::quoted_string, Decl_Type::unit, value_type, value_type, value_type},                              // 2
			{Token_Type::quoted_string, Decl_Type::unit, value_type, value_type, value_type, Token_Type::quoted_string},   // 3
		});
	
	Token *name = single_arg(decl, 0);
	auto handle = module->parameters.find_or_create(&decl->handle_name, name, true);
	auto parameter = module->parameters[handle];
	
	parameter->par_group               = par_group;
	parameter->unit                    = resolve_argument(module, decl, 1, Decl_Type::unit);
	parameter->default_val.val_double  = single_arg(decl, 2)->double_value();
	if(which == 2 || which == 3) {
		parameter->min_val.val_double  = single_arg(decl, 3)->double_value();
		parameter->max_val.val_double  = single_arg(decl, 4)->double_value();
	}
	if(which == 1)
		parameter->description         = single_arg(decl, 3)->string_value;
	if(which == 3)
		parameter->description         = single_arg(decl, 5)->string_value;
	
	return handle;
}

entity_handle
process_par_group_declaration(Module_Declaration *module, Decl_AST *decl) {
	match_declaration(decl, {{Token_Type::quoted_string}}, 1);
	Token *name = single_arg(decl, 0);
	auto handle = module->par_groups.find_or_create(&decl->handle_name, name, true);
	auto par_group = module->par_groups[handle];
	
	//TODO: Do we always need to require that a par group is tied a compartment?
	//TODO: Actually, it could be tied to a substance too, once we get to implement that!
	Token *compartment_handle = &decl->decl_chain[0];	
	par_group->compartment = module->compartments.find_or_create(compartment_handle);
	
	auto body = reinterpret_cast<Decl_Body_AST *>(decl->bodies[0]);
	
	for(Decl_AST *child : body->child_decls) {
		if(child->type == Decl_Type::par_real) {   //TODO: do other parameter types
			auto par_handle = process_par_declaration(module, child, handle);
			par_group->parameters.push_back(par_handle);
		}
	}
	
	return handle;
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
				Token *name = single_arg(child, 0);
				module->compartments.find_or_create(&child->handle_name, name, true);
			} break;
			
			case Decl_Type::par_group : {
				process_par_group_declaration(module, child);
			} break;
			
			case Decl_Type::substance : {
				match_declaration(child, {{Token_Type::quoted_string}});
				Token *name = single_arg(child, 0);
				module->substances.find_or_create(&child->handle_name, name, true);
				//TODO: could maybe allow declarations of substances with a unit directly (which would be default if it is not given in the compartment) ?
			} break;
			
			case Decl_Type::property : {
			
			} break;
			
			case Decl_Type::has : {
				
			} break;
			
			case Decl_Type::flux : {
				
			} break;
			
			case Decl_Type::unit : {
				process_unit_declaration(module, child);
			} 
			
			default : {
				child->type_name.print_error_header();
				fatal_error("Did not expect a declaration of type ", child->type_name.string_value, " inside a module declaration.");
			};
		}
	}
	
	for(const auto &entry : module->handles_in_scope) {
		Entity_Registration *entity = find_entity(module, entry.second);
		
		if(!entity->has_been_declared) {
			entity->handle_name.print_error_header();    //note: the handle_name must exist for it to have been put in the scope!
			fatal_error("The name \"", entity->handle_name.string_value, "\" was used, but never declared.");
		}
	}
	
	return module;
}



