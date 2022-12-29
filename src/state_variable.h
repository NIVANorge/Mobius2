
#ifndef MOBIUS_STATE_VARIABLE_H
#define MOBIUS_STATE_VARIABLE_H


struct
State_Var {
	
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
		flux                = 0x04,
		invalid             = 0x1000,
	} flags;
	
	//TODO: could probably combine some members of this struct in a union. They are not all going to be relevant at the same time.
	
	std::string name;

	Unit_Data unit; //NOTE: this can't just be an Entity_Id, because we need to be able to generate units for these.
	
	// If this is a quantity or property, loc1 is the location of this variable.
	// If this is a flux, loc1 and loc2 are the source and target of the flux resp.
	Var_Location   loc1;
	Var_Location   loc2;
	
	
	// TODO: Some of the below could be moved to ::declared
	Math_Expr_FT *function_tree;
	bool initial_is_conc;
	Math_Expr_FT *initial_function_tree;
	Math_Expr_FT *aggregation_weight_tree;
	Math_Expr_FT *unit_conversion_tree;
	bool override_is_conc;
	Math_Expr_FT *override_tree;
	
	State_Var() : type(Type::declared), function_tree(nullptr), initial_function_tree(nullptr), initial_is_conc(false), aggregation_weight_tree(nullptr), unit_conversion_tree(nullptr), override_tree(nullptr), override_is_conc(false), flags(Flags::none) {};
	
	// Because these are very common queries
	bool is_flux() { return (flags & flux);	}
	bool is_valid() { return !(flags & invalid); }
};


template<State_Var::Type type> struct
State_Var_Sub : State_Var {
	State_Var_Sub() {
		fatal_error(Mobius_Error::internal, "Instantiation of non-specified version of State_Var struct.");
	}
};

template<> struct
State_Var_Sub<State_Var::Type::declared> : State_Var {
	Decl_Type      decl_type;        // either flux, quantity or property. Note that this is not the same as the decl_type of the declaration. It is instead the type of the variable that is declared.
	Entity_Id      decl_id;          // This is the ID of the declaration, either Decl_Type::has or Decl_Type::flux
	Entity_Id      connection;       // Set if this is a flux on a connection.
	Var_Id         conc;             // If this is a mass (or volume) variable of a dissolved quantity, conc is the variable for the concentration.
	
	// If this is the target variable of a connection flux, connection_agg points to the aggregation variable for the connection flux.
	Var_Id         conn_source_agg;
	Var_Id         conn_target_agg;
	
	State_Var_Sub() : decl_id(invalid_entity_id), connection(invalid_entity_id), conc(invalid_var), conn_source_agg(invalid_var), conn_target_agg(invalid_var) {}
};

template<> struct
State_Var_Sub<State_Var::Type::in_flux_aggregate> : State_Var {
	Var_Id         in_flux_to; // The target state variable of the fluxes this is an aggregate for
	
	State_Var_Sub() : in_flux_to(invalid_var) {}
};

template<> struct
State_Var_Sub<State_Var::Type::regular_aggregate> : State_Var {
	Var_Id         agg_of;                // The variable this is an aggregate of
	Entity_Id      agg_to_compartment;    // From which point of view we are aggregating.
	
	State_Var_Sub() : agg_of(invalid_var), agg_to_compartment(invalid_entity_id) {}
};

template<> struct
State_Var_Sub<State_Var::Type::dissolved_conc> : State_Var {
	Var_Id         conc_of;
	
	State_Var_Sub() : conc_of(invalid_var) {}
};

template<> struct
State_Var_Sub<State_Var::Type::dissolved_flux> : State_Var {
	Entity_Id      connection;
	Var_Id         conc;                   // The concentration variable for the source of whatever this flux transports.
	Var_Id         flux_of_medium;         // The flux of the parent substance that whatever this flux transports is dissolved in.
	
	State_Var_Sub() : flux_of_medium(invalid_var), conc(invalid_var) {}
};

template<> struct
State_Var_Sub<State_Var::Type::connection_aggregate> : State_Var {
	Entity_Id      connection;
	Var_Id         agg_for;     // The state variable this is an aggregate for fluxes going to or from.
	bool           is_source;   // If it aggregates sources from or targets to that state var.
	
	State_Var_Sub() : connection(invalid_entity_id), agg_for(invalid_var) {}
};

template<State_Var::Type type>
State_Var_Sub<type> *as(State_Var *var) {
	if(var->type != type)
		fatal_error(Mobius_Error::internal, "Tried to convert a state variable to the wrong type.");
	if(var->flags & State_Var::Flags::invalid)
		fatal_error(Mobius_Error::internal, "Tried to convert an invalid variable.");
	return reinterpret_cast<State_Var_Sub<type> *>(var);
}

inline Entity_Id
connection_of_flux(State_Var *var) {
	if(var->type == State_Var::Type::declared)
		return as<State_Var::Type::declared>(var)->connection;
	else if(var->type == State_Var::Type::dissolved_flux)
		return as<State_Var::Type::dissolved_flux>(var)->connection;
	return invalid_entity_id;
}

# endif // MOBIUS_STATE_VARIABLE_H
