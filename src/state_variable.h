
#ifndef MOBIUS_STATE_VARIABLE_H
#define MOBIUS_STATE_VARIABLE_H

struct
State_Var {
	
	// TODO: This contains a lot of data that is irrelevant for input series. But annoying to have to factor it out. Could be put in yet another intermediate struct??
	
	enum class Type : u16 {
		declared,
		regular_aggregate,
		in_flux_aggregate,
		connection_aggregate,
		dissolved_flux,
		dissolved_conc,
		special_computation,
	} type;
	
	enum Flags {
		none                = 0x00,
		has_aggregate       = 0x01,
		clear_series_to_nan = 0x02,
		flux                = 0x04,
		invalid             = 0x1000,
	} flags;
	
	void set_flag(Flags flag) { flags = (Flags)(flags | flag); }
	bool has_flag(Flags flag) { return flags & flag; }
	// Because these are very common queries
	bool is_flux() { return has_flag(flux);	}
	bool is_valid() { return !has_flag(invalid); }
	
	std::string name;

	Unit_Data unit; //NOTE: this can't just be an Entity_Id, because we need to be able to generate units for state variables.
	
	// If this is a quantity or property, loc1 is the location of this variable.
	// If this is a flux, loc1 and loc2 are the source and target of the flux resp.
	Specific_Var_Location   loc1;
	Specific_Var_Location   loc2;
	
	owns_code unit_conversion_tree;
	owns_code specific_target;
	
	State_Var() : type(Type::declared), unit_conversion_tree(nullptr), flags(Flags::none), loc1(invalid_var_location), loc2(invalid_var_location) {};
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
	
	// If this is the source or target variable of one or more connection fluxes, these point to the aggregation variables for the connection fluxes.
	// (one variable per connection, not per flux) (only all_to_all have agg for the source yet.)
	std::vector<Var_Id> conn_source_aggs;
	std::vector<Var_Id> conn_target_aggs;
	
	double flux_time_unit_conv; // If it is a flux with a declared unit, what to multiply it with to get the number to use in the model solution (time step relative).
	
	owns_code function_tree;
	bool initial_is_conc;
	owns_code initial_function_tree;
	bool override_is_conc;
	owns_code override_tree;
	
	Var_Id special_computation; // If this variable is the result of a special computation.
	
	// These can be set for a declared flux.
	std::vector<Var_Id> no_carry;
	bool no_carry_by_default;
	
	State_Var_Sub() : decl_type(Decl_Type::property), decl_id(invalid_entity_id), connection(invalid_entity_id), conc(invalid_var), function_tree(nullptr), initial_function_tree(nullptr), initial_is_conc(false), override_tree(nullptr), override_is_conc(false), flux_time_unit_conv(1.0), special_computation(invalid_var), no_carry_by_default(false) {}
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
	owns_code      aggregation_weight_tree;
	
	State_Var_Sub() : agg_of(invalid_var), agg_to_compartment(invalid_entity_id), aggregation_weight_tree(nullptr) {}
};

template<> struct
State_Var_Sub<State_Var::Type::dissolved_conc> : State_Var {
	Var_Id         conc_of;
	double         unit_conversion;   // NOTE: The unit conversion to convert the value from (unit_of_mass / unit_of_dissolution_volume) to conc_unit, where conc_unit is the declared desired unit of the concentration.
	
	State_Var_Sub() : conc_of(invalid_var), unit_conversion(1.0) {}
};

template<> struct
State_Var_Sub<State_Var::Type::dissolved_flux> : State_Var {
	Var_Id         conc;                   // The concentration variable for the source of whatever this flux transports.
	Var_Id         flux_of_medium;         // The flux of the parent substance that whatever this flux transports is dissolved in.
	
	State_Var_Sub() : flux_of_medium(invalid_var), conc(invalid_var) {}
};

struct
Conversion_Data {
	Var_Id source_id;
	owns_code weight;
	owns_code unit_conv;
};

template<> struct
State_Var_Sub<State_Var::Type::connection_aggregate> : State_Var {
	Entity_Id      connection;
	Var_Id         agg_for;     // The state variable this is an aggregate for fluxes going to or from.
	bool           is_source;   // If it aggregates sources from or targets to that state var.
	
	// If this is a target aggregate (!is_source), conversion_data contains items with data for fluxes coming from that particular source (to the target agg_for)
	std::vector<Conversion_Data> conversion_data;
	
	State_Var_Sub() : connection(invalid_entity_id), agg_for(invalid_var), is_source(false) {}
};

template<> struct
State_Var_Sub<State_Var::Type::special_computation> : State_Var {
	Entity_Id       decl_id;
	
	owns_code       code;
	
	std::vector<Var_Id>          targets;
	
	State_Var_Sub() : decl_id(invalid_entity_id), code(nullptr) {}
};

template<State_Var::Type type>
State_Var_Sub<type> *as(State_Var *var) {
	if(var->type != type)
		fatal_error(Mobius_Error::internal, "Tried to convert a state variable to the wrong type.");
	if(var->flags & State_Var::Flags::invalid)
		fatal_error(Mobius_Error::internal, "Tried to convert an invalid variable.");
	return static_cast<State_Var_Sub<type> *>(var);
}

inline Var_Loc_Restriction &
restriction_of_flux(State_Var *var) {
	// TODO: Should this check if it is actually a flux?
	if(is_valid(var->loc1.connection_id))
		return var->loc1;
	return var->loc2;
}


#endif // MOBIUS_STATE_VARIABLE_H
