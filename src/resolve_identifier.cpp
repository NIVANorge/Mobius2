

// Note: unfinished work intended to merge identifier resolution between function_tree and model_declaration.


struct
Location_Resolve {
	Variable_Type       type;
	Entity_Id           val_id = invalid_entity_id;
	Var_Location        loc;
	Var_Loc_Restriction restriction;
	// TODO: Probably need a Source_Location decl_loc  so that we can untrace loc() declarations in errors.
};


// Have to figure out how to do errors here, because they should be reported differently depending on context.

void
expect_n_items(int expected, int got, Source_Location &source_loc, bool *error) {
	if(got == expected) return;
	
	source_loc.print_error_header();
	error_print("Expected exactly ", expected, " item", (expected == 1 ? "", "s"), " in this identifier chain, got ", got, ".");
	*error = true;
}

void
set_single_value(Mobius_Model *model, Entity_Id id, Location_Resolve &resolve, int n_items, Source_Location &source_loc, bool *error) {
	
	// TODO: Something will go wrong if you pass an enum parameter in a loc(). It should not require the . dereference before it is actually dereferenced.
	//    We could also just disallow it. Or make new syntax for enum would be even better maybe.
	
	if(id.reg_type == Reg_Type::parameter) {
		result.val_id = id; 
		result.type = Variable_Type::parameter;
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
		result.val_id = id;
		result.variable_type = Variable_Type::constant; // Doesn't exist yet. May not want it and instead just say parameter, then do fixup in function_scope_resolve...
		expect_n_items(1, n_items, source_loc, error);
	} else if (id.reg_type == Reg_Type::connection) {
		result.val_id = id;
		result.variable_type = Variable_Type::connection;
		expect_n_items(1, n_items, source_loc, error);
	} else
		fatal_error(Mobius_Error::internal, "Misuse of set_single_value.");
}

// This should be an internal function, combined with something that processes bracket(s), into an exported function.
void
resolve_location(Mobius_Model *model, Location_Resolve &result, std::vector<Token> &chain, Decl_Scope *scope, bool *error) {

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
		result.type = Variable_Type::state_var;
		result.loc.type = Var_Location::Type::out;
		expect_n_items(1, chain.size(), chain[0].source_loc, error);
		return;
	}

	std::vector<Entity_Id> id_chain(chain.size(), invalid_entity_id); // TODO: This one is unnecessary.. Could do resolution in loop below directly.
	for(int idx = 0; idx < chain.size(); ++idx) {
		auto reg = (*scope)[chain[idx].string_value];
		if(reg) id_chain[idx] = reg->id;
		else {
			chain[idx].print_error_header();
			error_print("The symbol '", chain[idx].string_value, "' can not be resolved in this scope.");
			*error = true;
			return;
		}
	}
	
	int offset = 0;
	if(id_chain[0].reg_type == Reg_Type::parameter || id_chain[0].reg_type == Reg_Type::constant || id_chain[0].reg_type == Reg_Type::connection) {
		
		set_single_value(model, id_chain[0], result, id_chain.size(), id_chain[0].source_loc, error);
		return;
		
	} else if( id_chain[0].reg_type == Reg_Type::loc) {
		auto loc = model->locs[id_chain[0]];
		
		result.restriction = loc;
		if(is_valid(loc->val_id)) {
			set_single_value(model, loc->val_id, result, id_chain.size(), id_chain[0].source_loc, error);
			return;
		} else {
			result.loc = loc->loc;
			offset = 1;
		}
	}
	
	result.type = Variable_Type::state_var; // At this stage we don't know if it is a 'series', that has to be corrected later if relevant.
	
	// Note: n_components is not 0 if we have already unpacked a loc() in the first components.
	int base_offset = (int)result.loc.n_components - offset;
	result.loc.n_components = (result.loc.n_components + (int)id_chain.size() - offset)
	if(result.loc.n_components  > max_var_loc_components) {
		chain[0].print_error_header();
		error_print("Too many components in a location.");
		*error = true;
		return;
	}
	
	if((offset != 0) && (chain.size() != 1) && result.loc.type != Var_Location::Type::located) {
		// TODO: error, can't compose unless it is a located loc.
	}
	
	result.loc.type = Var_Location::Type::located;
	
	for(int idx = offset; idx < id_chain.size(); ++idx) {
		auto comp = id_chain[idx];
		if(comp.reg_type != Reg_Type::component) {
			chain[idx].print_error_header();
			error_print("Expected a location component (compartment, quantity or property).");
			*error = true;
			return;
		}
		result.loc.components[base_offset + idx] = comp;
	}
	
	return;
}

// Maybe not that clean to pass the implicit_conn_id to this function... But annoying to insert it in later.
void
process_bracket(Mobius_Model *model, Restriction &res, Decl_Scope *scope, std::vector<Token> &bracket, bool *error, Entity_Id *implicit_conn_id = nullptr) {
	
	if(bracket.empty()) return;
	
	if(is_valid(res.connection_id) && !bracket.empty()) {
		// This could happen if we already got one from a loc().
		bracket[0].print_error_header();
		error_print("A bracket can't be added to this identifier since it already has one from a 'loc'.");
		*error = true;
		return;
	}
	String_View type;
	if(bracket.size() == 1) {
		type = bracket[0].string_value;
		if(!implicit_conn_id) {
			//error
		}
		res.conn_id = *implicit_conn_id;
	} else if (bracket.size() == 2) {
		type = bracket[1].string_value;
		res.connection_id = scope->expect(Reg_Type::connection, &bracket[0]);
	} else {
		// error
	}
	
	if(type == "top")
		res.type = Restriction::top;
	else if(type == "bottom")
		res.type = Restriction::bottom;
	else if(type == "specific")
		res.type = Restriction::specific;
	else if(type == "below")
		res.type = Restriction::below;
	else {
		// TODO: error
	}
}
//TODO: Rembember to check if a bracket is allowed or required (is_at) for the given variable type (not just the context).


// TODO: have convenience functions to unpack the resolve to specific context.
//    These should give errors if it contains something that is not appropriate to the context. E.g. restriction on simple loc, simple loc or flux loc is parameter, etc.


// This calls the combined resolve function that also processes brackets. Calls mobius_error_exit() on error.
void
resolve_loc_argument(Mobius_Model *model, Decl_Scope *scope, Argument_AST *arg, Location_Resolve &resolve); // TODO

void
resolve_simple_loc_argument(Mobius_Model *model, Decl_Scope *scope, Argument_AST *arg, Var_Location &loc) {
	
	Location_Resolve resolve;
	resolve_loc_argument(model, scope, arg, resolve);

	if(resolve.type != Variable_Type::state_var) {
		arg->source_loc().print_error_header();
		fatal_error("This argument must resolve to a state variable.");
	}
	if(is_valid(resolve.restriction.r1.connection_id)) {
		arg->bracketed_chain[0].source_loc.print_error_header();
		fatal_error("A bracketed restriction is not allowed in this context.");
	}
	
	loc = resolve.loc;
}

// remember to unpack a 'connection' identifier to a Specific_Var_Location also.
void
resolve_flux_loc_argument(Mobius_Model *model, Decl_Scope *scope, Argument_AST *arg, Specific_Var_Location &loc) {
	
	Location_Resolve resolve;
	resolve_loc_argument(model, scope, arg, resolve);

	if(resolve.type == Variable_Type::connection) {
		
		loc.type = Var_Location::Type::connection;
		loc.r1.connection_id = resolve.val_id;
		loc.r1.type          = Restriction::below;
		
	} else if (resolve.type == Variable_Type::state_var) {
		
		loc = resolve.loc;
		static_cast<Var_Loc_Restriction &>(loc) = resolve.restriction;
		
	} else {
		arg->source_loc().print_error_header();
		fatal_error("This argument must resolve to a state variable or connection.");
	}
}

// This should allow state_var, parameter, constant, connection.
void
resolve_loc_decl_argument(Mobius_Model *model, Decl_Scope *scope, Argument_AST *arg, Loc_Declaration &loc_decl);


// This is the function tree one, should reuse most of what we do in resolve_identifier there.
// Need to pass the chain in case of enum parameters
// todo: also pass function resolve context.
// This one must also resolve value type, and inline constants (as literals).
void
unpack_function_resolve(Model_Application *app, Location_Resolve &resolve, std::vector<Token> &chain, Function_Resolve_Result &result);