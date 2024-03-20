
#ifndef MOBIUS_STATE_VARIABLE_H
#define MOBIUS_STATE_VARIABLE_H

#include "common_types.h"
#include "units.h"

struct
State_Var {
	
	// TODO: This contains a lot of data that is irrelevant for input series. But annoying to have to factor it out. Could be put in yet another intermediate struct??
	
	// NOTE: Volatile! Changing these numbers could break mobipy
	enum class Type : u16 {
		declared              = 0,
		regular_aggregate     = 1,
		in_flux_aggregate     = 2,
		connection_aggregate  = 3,
		dissolved_flux        = 4,
		dissolved_conc        = 5,
		external_computation  = 6,
		step_resolution       = 7,
		parameter_aggregate   = 8,
	} type = Type::declared;
	
	enum Flags {
		none                = 0x00,
		has_aggregate       = 0x01,
		clear_series_to_nan = 0x02,
		flux                = 0x04,
		invalid             = 0x1000,
	} flags = Flags::none;
	
	Var_Id var_id;
	
	void set_flag(Flags flag) { flags = (Flags)(flags | flag); }
	bool has_flag(Flags flag) { return flags & flag; }
	// Some very common queries:
	bool is_flux() { return has_flag(flux);	}
	bool is_valid() { return !has_flag(invalid); }
	// If this is a quantity and it is not override.
	inline bool is_mass_balance_quantity();
	
	std::string name;

	Unit_Data unit; //NOTE: this can't just be an Entity_Id, because we need to be able to generate units for state variables.
	
	// If this is a quantity or property, loc1 is the location of this variable.
	// If this is a flux, loc1 and loc2 are the source and target of the flux resp.
	Specific_Var_Location   loc1 = invalid_var_location;
	Specific_Var_Location   loc2 = invalid_var_location;
	
	bool store_series = true;
	
	owns_code unit_conversion_tree = nullptr;
	owns_code specific_target = nullptr;
	
	// For fluxes only:
	bool bidirectional = false; 
	bool mixing_base   = false; // If this is a base flux that causes mixing. All dissolved fluxes of this one should be mixing.
	
	State_Var() {};
};


template<State_Var::Type type> struct
State_Var_Sub : State_Var {
	State_Var_Sub() {
		fatal_error(Mobius_Error::internal, "Instantiation of non-specified version of State_Var struct.");
	}
};

template<> struct
State_Var_Sub<State_Var::Type::declared> : State_Var {
	Decl_Type      decl_type  = Decl_Type::property;   // either flux, quantity or property. Note that this is not the same as the decl_type of the declaration. It is instead the type of the variable that is declared.
	Entity_Id      decl_id    = invalid_entity_id;     // This is the ID of the declaration, either Decl_Type::has or Decl_Type::flux
	Entity_Id      connection = invalid_entity_id;     // Set if this is a flux on a connection.
	Var_Id         conc       = invalid_var;           // If this is a mass (or volume) variable of a dissolved quantity, conc is the variable for the concentration.

	// If this is the source or target variable of one or more connection fluxes, these point to the aggregation variables for the connection fluxes.
	// (one variable per connection, not per flux) (only all_to_all have agg for the source yet.)
	std::vector<Var_Id> conn_source_aggs;
	std::vector<Var_Id> conn_target_aggs;
	
	double flux_time_unit_conv = 1.0; // If it is a flux with a declared unit, what to multiply it with to get the number to use in the model solution (time step relative).
	
	owns_code function_tree = nullptr;
	bool initial_is_conc = false;
	owns_code initial_function_tree = nullptr;
	bool override_is_conc = false;
	
	Var_Id external_computation = invalid_var; // If this variable is the result of an external computation.
	
	// These can be set for a declared flux.
	std::vector<Var_Id> no_carry;
	bool no_carry_by_default = false;
	
	Index_Set_Tuple allowed_index_sets;
	
	State_Var_Sub() {}
};

template<> struct
State_Var_Sub<State_Var::Type::in_flux_aggregate> : State_Var {
	Var_Id         in_flux_to = invalid_var; // The target state variable of the fluxes this is an aggregate for
	bool           is_out     = false;   // True if it is out_flux, otherwise in_flux.
	
	State_Var_Sub() {}
};

template<> struct
State_Var_Sub<State_Var::Type::regular_aggregate> : State_Var {
	Var_Id         agg_of                    = invalid_var;    // The variable this is an aggregate of
	Entity_Id      agg_to_compartment        = invalid_entity_id;    // From which point of view we are aggregating.
	owns_code      aggregation_weight_tree   = nullptr;
	
	State_Var_Sub() {}
};

template<> struct
State_Var_Sub<State_Var::Type::parameter_aggregate> : State_Var {
	Entity_Id      agg_of                    = invalid_entity_id;
	Entity_Id      agg_to_compartment        = invalid_entity_id;
	owns_code      aggregation_weight_tree   = nullptr;
	
	State_Var_Sub() {}
};

template<> struct
State_Var_Sub<State_Var::Type::dissolved_conc> : State_Var {
	Var_Id         conc_of            = invalid_var;
	Var_Id         conc_in            = invalid_var;
	double         unit_conversion    = 1.0;   // NOTE: The unit conversion to convert the value from (unit_of_mass / unit_of_dissolution_volume) to conc_unit, where conc_unit is the declared desired unit of the concentration.
	
	State_Var_Sub() {}
};

template<> struct
State_Var_Sub<State_Var::Type::dissolved_flux> : State_Var {
	Var_Id         flux_of_medium  = invalid_var;         // The flux of the parent substance that whatever this flux transports is dissolved in.
	
	bool           mixing          = false;
	
	Index_Set_Tuple allowed_index_sets;
	
	State_Var_Sub() {}
};

struct
Conversion_Data {
	Var_Id source_id;
	owns_code weight;
	owns_code unit_conv;
};

template<> struct
State_Var_Sub<State_Var::Type::connection_aggregate> : State_Var {
	Entity_Id      connection = invalid_entity_id;
	Var_Id         agg_for    = invalid_var;     // The state variable this is an aggregate for fluxes going to or from.
	bool           is_out     = false;   // If it aggregates sources from or targets to that state var.
	
	// If this is a target aggregate (!is_source), conversion_data contains items with data for fluxes coming from that particular source (to the target agg_for)
	std::vector<Conversion_Data> conversion_data;
	
	State_Var_Sub() {}
};

template<> struct
State_Var_Sub<State_Var::Type::external_computation> : State_Var {
	
	Entity_Id           decl_id = invalid_entity_id;
	owns_code           code    = nullptr;
	std::vector<Var_Id> targets;
	
	State_Var_Sub() {}
};

template<> struct
State_Var_Sub<State_Var::Type::step_resolution> : State_Var {
	Entity_Id      solver_id = invalid_entity_id;
	
	State_Var_Sub() {}
};

template<State_Var::Type type>
State_Var_Sub<type> *
as(State_Var *var) {
	if(var->type != type)
		fatal_error(Mobius_Error::internal, "Tried to convert a state variable to the wrong type.");
	if(var->flags & State_Var::Flags::invalid)
		fatal_error(Mobius_Error::internal, "Tried to convert an invalid variable.");
	return static_cast<State_Var_Sub<type> *>(var);
}

inline bool
State_Var::is_mass_balance_quantity() {
	if(type != Type::declared) return false;
	auto var2 = as<State_Var::Type::declared>(this);
	return (var2->decl_type == Decl_Type::quantity) && !var2->function_tree;
}

#endif // MOBIUS_STATE_VARIABLE_H
