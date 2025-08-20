

#include "resolve_identifier.h"

// Note: unfinished work, intended to merge (parts of) identifier resolution between function_tree and model_declaration.


void
expect_n_items(int expected, int got, const Source_Location &source_loc, bool *error) {
	if(got == expected) return;
	
	source_loc.print_error_header();
	error_print("Expected exactly ", expected, " item", (expected == 1 ? "" : "s"), " in this identifier chain, got ", got, ".");
	*error = true;
}

void
set_single_value(Mobius_Model *model, Entity_Id id, Location_Resolve &resolve, int n_items, const Source_Location &source_loc, bool *error) {
	
	// TODO: Something will go wrong if you pass an enum parameter in a loc(). It should not require the . dereference before it is actually dereferenced.
	//    We could also just disallow it. Or make new syntax for enum would be even better maybe.
	
	if(id.reg_type == Reg_Type::parameter) {
		resolve.val_id = id; 
		resolve.type = Variable_Type::parameter;
		auto par = model->parameters[id];
		int expect_num = 1;
		if(par->decl_type == Decl_Type::par_enum) {
			expect_num = 2;
		} else if (par->decl_type == Decl_Type::par_datetime) {
			source_loc.print_error_header();
			error_print("It is currently not supported to directly reference datetime parameters in code.\n");
			*error = true;
		}
		expect_n_items(expect_num, n_items, source_loc, error);
	} else if (id.reg_type == Reg_Type::constant) {
		resolve.val_id = id;
		resolve.type = Variable_Type::constant;
		expect_n_items(1, n_items, source_loc, error);
	} else if (id.reg_type == Reg_Type::connection) {
		resolve.val_id = id;
		resolve.type = Variable_Type::connection;
		expect_n_items(1, n_items, source_loc, error);
	} else
		fatal_error(Mobius_Error::internal, "Misuse of set_single_value.");
}

void
resolve_location(Mobius_Model *model, Location_Resolve &result, const std::vector<Token> &chain, Decl_Scope *scope, bool *error) {

	auto str0 = chain[0].string_value;
	
	if(str0 == "no_override") {
		result.type = Variable_Type::no_override;
		expect_n_items(1, chain.size(), chain[0].source_loc, error);
		return;
	} else if(str0 == "is_at") {
		result.type = Variable_Type::is_at;
		expect_n_items(1, chain.size(), chain[0].source_loc, error);
		return;
	} else if(str0 == "time") {
		expect_n_items(2, chain.size(), chain[0].source_loc, error);
		if(*error) return;
		auto str1 = chain[1].string_value;
		
		if(false)  {}
		#define TIME_VALUE(name, bits) \
		else if(str1 == #name) result.type = Variable_Type::time_##name;
		#include "time_values.incl"
		#undef TIME_VALUE
		else if(str1 == "fractional_step") {
			result.type = Variable_Type::time_fractional_step;
		} else {
			chain[1].print_error_header();
			error_print("The 'time' structure does not have a member \"", str1, "\".\n");
			*error = true;
		}
		return;
	} else if(str0 == "out") {
		//result.type = Variable_Type::state_var;
		result.type = Variable_Type::series;
		result.loc.type = Var_Location::Type::out;
		expect_n_items(1, chain.size(), chain[0].source_loc, error);
		return;
	}

	std::vector<Entity_Id> id_chain(chain.size(), invalid_entity_id); // TODO: The vector is unnecessary.. Could do resolution in loop below directly.
	for(int idx = 0; idx < chain.size(); ++idx) {
		auto reg = (*scope)[chain[idx].string_value];
		if(reg) {
			id_chain[idx] = reg->id;
			// Hmm, this is an ugly fix. It is needed because we don't put the enum options as symbols in the scope.
			if(idx == 0 && reg->id.reg_type == Reg_Type::parameter && model->parameters[reg->id]->decl_type == Decl_Type::par_enum)
				break;
		} else {
			chain[idx].print_error_header();
			error_print("The identifier '", chain[idx].string_value, "' has not been declared in or imported to this scope.");
			*error = true;
			return;
		}
	}
	
	int offset = 0;
	if(id_chain[0].reg_type == Reg_Type::parameter || id_chain[0].reg_type == Reg_Type::constant || id_chain[0].reg_type == Reg_Type::connection) {
		
		set_single_value(model, id_chain[0], result, id_chain.size(), chain[0].source_loc, error);
		return;
		
	} else if( id_chain[0].reg_type == Reg_Type::loc) {
		auto loc = model->locs[id_chain[0]];
		
		if(!loc->has_been_processed) {
			loc->source_loc.print_error_header(Mobius_Error::internal);
			error_print("This 'loc' was referenced before being processed. The reference is here:\n");
			chain[0].source_loc.print_error();
			*error = true;
			return;
		}
		
		result.restriction = loc->loc;
		if(is_valid(loc->val_id)) {
			set_single_value(model, loc->val_id, result, id_chain.size(), chain[0].source_loc, error);
			return;
		} else {
			result.loc = loc->loc;
			offset = 1;
		}
	}
	
	//result.type = Variable_Type::state_var; // At this stage we don't know if it is a 'series', that has to be corrected later if relevant.
	result.type = Variable_Type::series;
	
	if(offset > 0 && (result.loc.type == Var_Location::Type::out || result.loc.type == Var_Location::Type::connection)) {
		if(id_chain.size() > 1) {
			chain[0].print_error_header();
			error_print("An 'out' or a connection can't be composed with other components.");
			*error = true;
			return;
		}
		return;
	}
	
	// Note: n_components is not 0 if we have already unpacked a loc() in the first components.
	int base_offset = (int)result.loc.n_components - offset;
	result.loc.n_components = (result.loc.n_components + (int)id_chain.size() - offset);
	if(result.loc.n_components  > max_var_loc_components) {
		chain[0].print_error_header();
		error_print("Too many components in a location.");
		*error = true;
		return;
	}
	
	if((offset != 0) && (chain.size() != 1) && result.loc.type != Var_Location::Type::located) {
		chain[0].print_error_header();
		error_print("The 'loc' '", chain[0].string_value, "' is not located, and so it can't be further composed with other components.");
		*error = true;
		return;
	}
	
	result.loc.type = Var_Location::Type::located;
	
	for(int idx = offset; idx < id_chain.size(); ++idx) {
		auto comp = id_chain[idx];
		if(comp.reg_type != Reg_Type::component) {
			chain[idx].print_error_header();
			error_print("The identifier '", chain[idx].string_value, "' does not refer to a location component (compartment, quantity or property).");
			*error = true;
			return;
		}
		result.loc.components[base_offset + idx] = comp;
	}
}

// Maybe not that clean to pass the implicit_conn_id to this function... But annoying to insert it in later.
void
resolve_bracket(Mobius_Model *model, Restriction &res, Decl_Scope *scope, const std::vector<Token> &bracket, bool *error, Entity_Id implicit_conn_id = invalid_entity_id) {
	
	if(bracket.empty()) return;
	
	if(is_valid(res.connection_id) && !bracket.empty()) {
		// This could happen if we already got one from a loc().
		bracket[0].print_error_header();
		error_print("A bracketed restriction can't be added to this identifier since it already has one from a 'loc'.");
		*error = true;
		return;
	}
	Token type_token;
	if(bracket.size() == 1) {
		type_token = bracket[0];
		if(!is_valid(implicit_conn_id)) {
			type_token.print_error_header();
			error_print("No connection could be inferred from the context, so one must be explicitly provided in the bracket.");
			*error = true;
			return;
		}
		res.connection_id = implicit_conn_id;
	} else if (bracket.size() == 2) {
		type_token = bracket[1];
		res.connection_id = scope->expect(Reg_Type::connection, &bracket[0]);
	} else {
		bracket[0].print_error_header();
		error_print("Expected exactly 1 or 2 tokens in the bracket.");
		*error = true;
		return;
	}
	auto type = type_token.string_value;
	
	if(type == "top")
		res.type = Restriction::top;
	else if(type == "bottom")
		res.type = Restriction::bottom;
	else if(type == "specific")
		res.type = Restriction::specific;
	else if(type == "below")
		res.type = Restriction::below;
	else if(type == "above")
		res.type = Restriction::above;
	else {
		type_token.print_error_header();
		error_print("Unrecognized restriction type '", type, "'.");
		*error = true;
		return;
	}
}

void
resolve_full_location(Mobius_Model *model, Location_Resolve &result, const std::vector<Token> &chain, const std::vector<Token> &bracket, const std::vector<Token> &bracket2, Decl_Scope *scope, bool *error, Entity_Id implicit_conn_id) {
	
	resolve_location(model, result, chain, scope, error);
	if(*error) return;
	
	resolve_bracket(model, result.restriction.r1, scope, bracket, error, implicit_conn_id);
	if(*error) return;
	
	resolve_bracket(model, result.restriction.r2, scope, bracket2, error);
	if(*error) return;
	
	if(is_valid(result.restriction.r1.connection_id)) {
		if(result.type == Variable_Type::series) {
		//if(result.type == Variable_Type::state_var) {
			if(result.loc.type != Var_Location::Type::located) {
				bracket[0].print_error_header();
				error_print("Only a located location can have a bracketed restriction.");
				*error = true;
				return;
			}
		} else if (result.type != Variable_Type::parameter && result.type != Variable_Type::is_at) {
			bracket[0].print_error_header();
			error_print("The identifier '", chain[0].string_value, "' can't have a bracketed restriction.");
			*error = true;
			return;
		}
	} else if(result.type == Variable_Type::is_at) {
		chain[0].print_error_header();
		error_print("An 'is_at' is only valid with a bracketed restriction.");
		*error = true;
		return;
	}
}

void
resolve_loc_argument(Mobius_Model *model, Decl_Scope *scope, Argument_AST *arg, Location_Resolve &resolve) {
	
	bool error = false;
	resolve_full_location(model, resolve, arg->chain, arg->bracketed_chain, arg->secondary_bracketed, scope, &error);
	if(error) mobius_error_exit();
}

void
resolve_simple_loc_argument(Mobius_Model *model, Decl_Scope *scope, Argument_AST *arg, Var_Location &loc) {
	
	Location_Resolve resolve;
	resolve_loc_argument(model, scope, arg, resolve);

	if(resolve.type != Variable_Type::series) {
		arg->source_loc().print_error_header();
		fatal_error("This argument must resolve to a state variable.");
	}
	if(is_valid(resolve.restriction.r1.connection_id)) {
		arg->bracketed_chain[0].source_loc.print_error_header();
		fatal_error("A bracketed restriction is not allowed in this context.");
	}
	if(resolve.loc.type != Var_Location::Type::located) {
		arg->source_loc().print_error_header();
		fatal_error("Expected only a located location in this context, not 'out'.");
	}
	
	loc = resolve.loc;
}

void
resolve_flux_loc_argument(Mobius_Model *model, Decl_Scope *scope, Argument_AST *arg, Specific_Var_Location &loc) {
	
	Location_Resolve resolve;
	resolve_loc_argument(model, scope, arg, resolve);

	if(resolve.type == Variable_Type::connection) {
		
		loc.type = Var_Location::Type::connection;
		loc.r1.connection_id = resolve.val_id;
		loc.r1.type          = Restriction::below;
		
	//} else if (resolve.type == Variable_Type::state_var) {
	} else if (resolve.type == Variable_Type::series) {
		
		loc = resolve.loc;
		static_cast<Var_Loc_Restriction &>(loc) = resolve.restriction;
		
	} else {
		arg->source_loc().print_error_header();
		fatal_error("This argument must resolve to a location or connection.");
	}
}

void
resolve_loc_decl_argument(Mobius_Model *model, Decl_Scope *scope, Argument_AST *arg, Loc_Registration &loc_decl) {
	
	Location_Resolve resolve;
	resolve_loc_argument(model, scope, arg, resolve);
	
	static_cast<Var_Loc_Restriction &>(loc_decl.loc) = resolve.restriction;
	
	//if(resolve.type == Variable_Type::state_var) {
	if (resolve.type == Variable_Type::series) {
		
		static_cast<Var_Location &>(loc_decl.loc) = resolve.loc;
		
	} else if (resolve.type == Variable_Type::parameter || resolve.type == Variable_Type::constant || resolve.type == Variable_Type::connection) {
		
		loc_decl.val_id = resolve.val_id;
		
	} else {
		
		arg->source_loc().print_error_header();
		fatal_error("Only locations, parameters, constants and connections can be passed as a 'loc'.");
		
	}
	
}
