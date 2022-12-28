
#ifndef MOBIUS_STATE_VARIABLE_H
#define MOBIUS_STATE_VARIABLE_H


struct
State_Variable {
	// TODO: The concept of the decl_type for the variable is a bit wishywashy for generated (non-declared) variables. It is still used so that e.g aggregates can have the same decl_type as the
	//   variable they aggregate, but this may not be best practice..
	Decl_Type decl_type; //either flux, quantity or property
	
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

	Entity_Id entity_id;  // This is the ID of the declaration (if the variable is not auto-generated), either Decl_Type::has or Decl_Type::flux
	
	Unit_Data unit; //NOTE: this can't just be an Entity_Id, because we need to be able to generate units for these.
	
	Entity_Id connection; // For a flux that points at a connection.
	
	// If this is a quantity or property, loc1 is the location of this variable.
	// If this is a flux, loc1 and loc2 are the source and target of the flux resp.
	Var_Location   loc1;
	Var_Location   loc2;
	
	// if f_is_aggregate, this is what it aggregates. if f_has_aggregate, this is who aggregates it.
	Var_Id         agg;
	Entity_Id      agg_to_compartment;
	
	// if f_in_flux is set (this is the aggregation variable for the in fluxes), agg points at the quantity that is the target of the fluxes.
	Var_Id         in_flux_target;
	
	// If this is the target variable of a connection flux, connection_agg points to the aggregation variable for the connection flux.
	// If this is the aggregate ( f_in_flux_connection is set ), connection_agg points to the target of the connection flux(es) (which is the same as the source).
	Var_Id         connection_source_agg;
	Var_Id         connection_target_agg;
	
	// If this is a generated flux for a dissolved quantity (f_dissolved_flux is set), dissolved_conc is the respective generated conc of the quantity. dissolved_flux is the flux of the quantity that this one is dissolved in.
	// If this is the generated conc (f_dissolved_conc is set), dissolved_conc is the variable for the mass of the quantity.
	// If none of the flags are set and this is the mass of the quantity, dissolved_conc also points to the conc.
	Var_Id         dissolved_conc;
	Var_Id         dissolved_flux;
	
	Math_Expr_FT *function_tree;
	bool initial_is_conc;
	Math_Expr_FT *initial_function_tree;
	Math_Expr_FT *aggregation_weight_tree;
	Math_Expr_FT *unit_conversion_tree;
	bool override_is_conc;
	Math_Expr_FT *override_tree;
	
	State_Variable() : type(Type::declared), function_tree(nullptr), initial_function_tree(nullptr), initial_is_conc(false), aggregation_weight_tree(nullptr), unit_conversion_tree(nullptr), override_tree(nullptr), override_is_conc(false), flags(Flags::none), agg(invalid_var), connection(invalid_entity_id), connection_source_agg(invalid_var), connection_target_agg(invalid_var), dissolved_conc(invalid_var), dissolved_flux(invalid_var) {};
};


# endif // MOBIUS_STATE_VARIABLE_H
