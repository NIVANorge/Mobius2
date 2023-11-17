
#ifndef MOBIUS_MODEL_DECLARATION_H
#define MOBIUS_MODEL_DECLARATION_H

// NOTE: this is a work-in-progress replacement for the module declaration and scope system.

#include <string>
#include <set>
#include <unordered_set>

#include "ast.h"
#include "ode_solvers.h"
#include "units.h"
#include "catalog.h"

template<> struct
Registration<Reg_Type::module_template> : Registration_Base {
	Module_Version version;
	Decl_AST      *decl = nullptr;
	std::string    doc_string;
	std::string    normalized_path;
};

template<> struct
Registration<Reg_Type::loc> : Registration_Base {
	Specific_Var_Location loc;
	Entity_Id             par_id = invalid_entity_id;    // One could also pass a parameter as a loc.
};

struct
Aggregation_Data {
	Entity_Id       to_compartment      = invalid_entity_id;
	Entity_Id       only_for_connection = invalid_entity_id;
	Math_Block_AST *code                = nullptr;
	Entity_Id       scope_id            = invalid_entity_id;
};

struct
Flux_Unit_Conversion_Data {
	Var_Location source;
	Var_Location target;
	Math_Block_AST *code;
	Entity_Id       scope_id;
};

template<> struct
Registration<Reg_Type::component> : Registration_Base {
	//Entity_Id    unit;            //NOTE: tricky. could clash between different vars. Better just to have it on the "var" ?
	
	// For compartments:
	std::vector<Aggregation_Data> aggregations;
	std::vector<Flux_Unit_Conversion_Data> unit_convs;
	
	// For compartments and quantities:
	std::vector<Entity_Id> index_sets;
	
	// For properties:
	Math_Block_AST *default_code = nullptr;
};

template<> struct
Registration<Reg_Type::parameter> : Registration_Base {
	Entity_Id       par_group = invalid_entity_id;
	Entity_Id       unit = invalid_entity_id;
	
	Parameter_Value default_val;
	Parameter_Value min_val;
	Parameter_Value max_val;
	
	std::vector<std::string> enum_values;
	
	s64 enum_int_value(const std::string &name) {
		auto find = std::find(enum_values.begin(), enum_values.end(), name);
		if(find != enum_values.end())
			return (s64)(find - enum_values.begin());
		return -1;
	}
	
	std::string     description;
};

template<> struct
Registration<Reg_Type::var> : Registration_Base {
	Var_Location   var_location          = invalid_var_location;
	Entity_Id      unit                  = invalid_entity_id;
	Entity_Id      conc_unit             = invalid_entity_id;
	
	Var_Location   additional_conc_medium = invalid_var_location;
	Entity_Id      additional_conc_unit   = invalid_entity_id;
	
	std::string    var_name;
	
	bool           store_series = true;
	
	Math_Block_AST *code = nullptr;
	bool initial_is_conc;
	Math_Block_AST *initial_code = nullptr;
	bool override_is_conc = false;
	Math_Block_AST *override_code = nullptr;
};

template<> struct
Registration<Reg_Type::flux> : Registration_Base {
	Specific_Var_Location   source;
	Specific_Var_Location   target;
	
	Entity_Id      unit           = invalid_entity_id;
	Entity_Id      discrete_order = invalid_entity_id; // A discrete_order declaration that (among others) specifies the order of computation of this flux.
	
	bool           store_series = true;
	
	Math_Block_AST  *code                = nullptr;
	Math_Block_AST  *no_carry_ast        = nullptr;
	Math_Block_AST  *specific_target_ast = nullptr;
	bool             no_carry_by_default = false;
	bool             bidirectional       = false;
};

template<> struct
Registration<Reg_Type::discrete_order> : Registration_Base {
	// TODO: eventually this one could be more complex to take into account order of when things are added or subtracted, or recomputation of values etc.
	std::vector<Entity_Id> fluxes;
};

template<> struct
Registration<Reg_Type::external_computation> : Registration_Base {
	std::string      function_name;
	
	// TODO: May need a vector of components
	Entity_Id        component = invalid_entity_id;
	
	Entity_Id        connection_component = invalid_entity_id;
	Entity_Id        connection           = invalid_entity_id;
	
	Math_Block_AST  *code;
};

enum class
Function_Type {
	decl, intrinsic, linked,
};

template<> struct
Registration<Reg_Type::function> : Registration_Base {
	std::vector<std::string> args;
	std::vector<Entity_Id> expected_units;
	
	Function_Type    fun_type;
	Math_Block_AST  *code = nullptr;
	
	// TODO: may need some info on expected argument types (especially for externals)
};

template<> struct
Registration<Reg_Type::constant> : Registration_Base {
	double    value;   //Hmm, should we allow integer constants too? But that would require two declaration types.
	Entity_Id unit;
};

template<> struct
Registration<Reg_Type::unit> : Registration_Base {
	Unit_Data data;
};

enum class
Connection_Type {
	unrecognized = 0, directed_graph, grid1d,
};

template<> struct
Registration<Reg_Type::connection> : Registration_Base {
	Connection_Type type = Connection_Type::unrecognized;
	
	Entity_Id node_index_set = invalid_entity_id;  // Only for grid1d. For directed graph, the nodes could be indexed variously
	Entity_Id edge_index_set = invalid_entity_id;  // Only for directed graph for now.
	
	bool no_cycles = false;
	
	std::vector<Entity_Id> components;
	Math_Expr_AST *regex = nullptr;
};

template<> struct
Registration<Reg_Type::solver> : Registration_Base {
	Entity_Id solver_fun = invalid_entity_id;
	Entity_Id h_unit = invalid_entity_id;
	double hmin;
	Entity_Id h_par = invalid_entity_id;
	Entity_Id hmin_par = invalid_entity_id;
	std::vector<std::pair<Specific_Var_Location, Source_Location>> locs; // NOTE: We use a specific_var_location to merge some functionality in model_composition.cpp, but all the data we need is really just in Var_Location
};

template<> struct
Registration<Reg_Type::solver_function> : Registration_Base {
	Solver_Function *solver_fun = nullptr;
};

struct
Mobius_Config {
	std::string mobius_base_path;
	bool store_all_series = false;
};

struct
Mobius_Model : Catalog {
	
	std::string model_name;	
	Mobius_Config config;
	
	Registry<Reg_Type::module_template> module_templates;
	Registry<Reg_Type::unit>        units;
	Registry<Reg_Type::parameter>   parameters;  // par_real, par_int, par_bool, par_enum, par_datetime
	Registry<Reg_Type::function>    functions;
	Registry<Reg_Type::constant>    constants;
	Registry<Reg_Type::component>   components;  // compartment, quantity, property
	Registry<Reg_Type::var>         vars;
	Registry<Reg_Type::flux>        fluxes;
	Registry<Reg_Type::discrete_order> discrete_orders;
	Registry<Reg_Type::external_computation> external_computations;
	Registry<Reg_Type::solver>      solvers;
	Registry<Reg_Type::solver_function> solver_functions;
	Registry<Reg_Type::connection>  connections;
	Registry<Reg_Type::loc>         locs;
	
	Decl_Scope global_scope;
	
	Registry_Base *registry(Reg_Type reg_type);
	
	File_Data_Handler file_handler;
	std::unordered_map<std::string, std::unordered_map<std::string, Entity_Id>> parsed_decls;
	
	// NOTE: The ASTs are reused every time you create a Model_Application from the model, so you should only free them if you know you are not going to create more Model_Applications from them.
	//    A Model_Application is not dependent on the ASTs still existing after it is constructed though.
	// TODO: Or do we get problems with some stored Source_Locations ?
	void free_asts();
	~Mobius_Model() {
		//free_asts();   // TODO: Hmm, seems like it causes a problem some times??
	}
};

Mobius_Config
load_config(String_View config = "config.txt");

Mobius_Model *
load_model(String_View file_name, Mobius_Config *config = nullptr);

void
process_location_argument(Mobius_Model *model, Decl_Scope *scope, Argument_AST *arg, Var_Location *location,
	bool allow_unspecified = false, bool allow_restriction = false, Entity_Id *par_id = nullptr);

// TODO: these could be moved to common_types.h (along with impl.)
Var_Location
remove_dissolved(const Var_Location &loc);

Var_Location
add_dissolved(const Var_Location &loc, Entity_Id quantity);

void
error_print_location(Mobius_Model *model, const Specific_Var_Location &loc);
void
debug_print_location(Mobius_Model *model, const Specific_Var_Location &loc);

void
check_valid_distribution(Mobius_Model *model, std::vector<Entity_Id> &index_sets, Source_Location &err_loc);

#endif // MOBIUS_MODEL_DECLARATION_H