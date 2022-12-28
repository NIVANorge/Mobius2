
#ifndef MOBIUS_STATE_VARIABLE_H
#define MOBIUS_STATE_VARIABLE_H


struct
State_Var {
	// TODO: The concept of the decl_type for the variable is a bit wishywashy for generated (non-declared) variables. It is still used so that e.g aggregates can have the same decl_type as the
	//   variable they aggregate, but this may not be best practice..
	Decl_Type decl_type; //either flux, quantity or property
	
	// TODO: This contains a lot of data that is irrelevant for input series. But annoying to have to factor it out. Could be put in yet another intermediate struct??
	
	enum class Type {
		declared,
		regular_aggregate,
		in_flux_aggregate,
		connection_aggregate,
		dissolved_flux,
		dissolved_conc,
	} type;
	
	enum Flags {
		none                = 0x00,
		has_aggregate       = 0x01,
		clear_series_to_nan = 0x02,
		invalid             = 0x1000,
	} flags;
	
	//TODO: could probably combine some members of this struct in a union. They are not all going to be relevant at the same time.
	
	std::string name;

	//Entity_Id entity_id;  // This is the ID of the declaration (if the variable is not auto-generated), either Decl_Type::has or Decl_Type::flux
	
	Unit_Data unit; //NOTE: this can't just be an Entity_Id, because we need to be able to generate units for these.
	
	Entity_Id connection; // For a flux that points at a connection.
	
	// If this is a quantity or property, loc1 is the location of this variable.
	// If this is a flux, loc1 and loc2 are the source and target of the flux resp.
	Var_Location   loc1;
	Var_Location   loc2;
	
	// If this is the target variable of a connection flux, connection_agg points to the aggregation variable for the connection flux.
	// If this is the aggregate ( f_in_flux_connection is set ), connection_agg points to the target of the connection flux(es) (which is the same as the source).
	Var_Id         connection_source_agg;
	Var_Id         connection_target_agg;
	
	// TODO: put these on  declared and dissolved_flux respectively!
	// If this state variable is the mass of a dissolved quantity, the 'conc' variable is the concentration of it.
	// If this state variable is the flux of a dissolved quantity, the 'conc' variable is the conc variable of the source of the flux.
	//Var_Id         conc;
	
	// TODO: Some of the below could be moved to ::declared
	Math_Expr_FT *function_tree;
	bool initial_is_conc;
	Math_Expr_FT *initial_function_tree;
	Math_Expr_FT *aggregation_weight_tree;
	Math_Expr_FT *unit_conversion_tree;
	bool override_is_conc;
	Math_Expr_FT *override_tree;
	
	State_Var() : type(Type::declared), function_tree(nullptr), initial_function_tree(nullptr), initial_is_conc(false), aggregation_weight_tree(nullptr), unit_conversion_tree(nullptr), override_tree(nullptr), override_is_conc(false), flags(Flags::none), connection(invalid_entity_id), connection_source_agg(invalid_var), connection_target_agg(invalid_var) {};
};


template<State_Var::Type type> struct
State_Var_Sub : State_Var {
	// TODO: When all things are in place, don't allow instantiation of this one.
};

template<> struct
State_Var_Sub<State_Var::Type::declared> : State_Var {
	Entity_Id      decl_id;          // This is the ID of the declaration (if the variable is not auto-generated), either Decl_Type::has or Decl_Type::flux
	//Entity_Id      connection;
	Var_Id         conc;
	
	State_Var_Sub() : decl_id(invalid_entity_id), conc(invalid_var) {}
};

template<> struct
State_Var_Sub<State_Var::Type::in_flux_aggregate> : State_Var {
	Var_Id         in_flux_to; // The target state variable of the fluxes this is an aggregate for
	
	State_Var_Sub() : in_flux_to(invalid_var) {}
};

template<> struct
State_Var_Sub<State_Var::Type::regular_aggregate> : State_Var {
	Var_Id         agg_of;
	Entity_Id      agg_to_compartment;
	
	State_Var_Sub() : agg_of(invalid_var), agg_to_compartment(invalid_entity_id) {}
};

template<> struct
State_Var_Sub<State_Var::Type::dissolved_conc> : State_Var {
	Var_Id         conc_of;
	
	State_Var_Sub() : conc_of(invalid_var) {}
};

template<> struct
State_Var_Sub<State_Var::Type::dissolved_flux> : State_Var {
	Var_Id         conc;
	Var_Id         flux_of_medium;         // The flux of the parent substance that whatever this flux transports is dissolved in.
	
	State_Var_Sub() : flux_of_medium(invalid_var), conc(invalid_var) {}
};

/*

template<> struct
State_Var_Sub<State_Var::Type::connection_aggregate> : State_Var {
	//Entity_Id      connection;
	Var_Id         agg_for;
	bool           is_source;
};


*/

template<State_Var::Type type>
State_Var_Sub<type> *as(State_Var *var) {
	if(var->type != type)
		fatal_error(Mobius_Error::internal, "Tried to convert a state variable to the wrong type.");
	if(var->flags & State_Var::Flags::invalid)
		fatal_error(Mobius_Error::internal, "Tried to convert an invalid variable.");
	return reinterpret_cast<State_Var_Sub<type> *>(var);
}



# endif // MOBIUS_STATE_VARIABLE_H
