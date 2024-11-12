
#ifndef MOBIUS_MODEL_DECLARATION_H
#define MOBIUS_MODEL_DECLARATION_H

// NOTE: this is a work-in-progress replacement for the module declaration and scope system.

#include "ast.h"
#include "ode_solvers.h"
#include "units.h"
#include "catalog.h"

#include <string>
#include <set>
#include <unordered_set>
#include <algorithm>

struct
Module_Template_Registration : Registration_Base {
	
	Module_Version version;
	std::string    doc_string;
	std::string    normalized_path;
	bool           was_inline_declared = false;
	
	void process_declaration(Catalog *catalog);
};

struct
Module_Registration : Registration_Base {
	
	Entity_Id      template_id = invalid_entity_id;
	Decl_Scope     scope;
	std::string    full_name;
	
	void process_declaration(Catalog *catalog);
};

struct
Par_Group_Registration : Registration_Base {
	
	std::vector<Entity_Id> components;
	//std::vector<Entity_Id> direct_index_sets;
	Decl_Scope             scope;
	bool                   must_fully_distribute = false;
	
	Index_Set_Tuple        max_index_sets; // This one will not be correctly set until the model is fully loaded.
	
	void process_declaration(Catalog *catalog);
};

struct
Library_Registration : Registration_Base {
	
	Decl_Scope     scope;
	bool           is_being_processed = false;
	std::string    doc_string;
	std::string    normalized_path;
	
	void process_declaration(Catalog *catalog);
};

struct
Loc_Registration : Registration_Base {
	
	Specific_Var_Location loc;
	Entity_Id             val_id = invalid_entity_id;    // One can pass a parameter or constant as a loc.
	
	void process_declaration(Catalog *catalog);
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

struct
Component_Registration : Registration_Base {
	//Entity_Id    unit;            //NOTE: tricky. could clash between different vars. Better just to have it on the "var" ?
	
	// For compartments:
	std::vector<Aggregation_Data> aggregations;
	std::vector<Flux_Unit_Conversion_Data> unit_convs;
	
	// For compartments and quantities:
	std::vector<Entity_Id> index_sets;
	
	// For properties:
	Math_Block_AST *default_code = nullptr;
	
	void process_declaration(Catalog *catalog);
};

struct
Parameter_Registration : Registration_Base {
	
	Entity_Id       unit = invalid_entity_id;
	Parameter_Value default_val;
	Parameter_Value min_val;
	Parameter_Value max_val;
	std::string     description;
	
	std::vector<std::string> enum_values;
	
	std::string former_name;
	
	s64 enum_int_value(const std::string &name) {
		auto find = std::find(enum_values.begin(), enum_values.end(), name);
		if(find != enum_values.end())
			return (s64)(find - enum_values.begin());
		return -1;
	}
	
	void process_declaration(Catalog *catalog);
};

struct
Var_Registration : Registration_Base {
	Var_Location   var_location          = invalid_var_location;
	Entity_Id      unit                  = invalid_entity_id;
	Entity_Id      conc_unit             = invalid_entity_id;
	
	Var_Location   additional_conc_medium = invalid_var_location;
	Entity_Id      additional_conc_unit   = invalid_entity_id;
	
	std::string    var_name;
	
	bool           store_series = true;
	bool           clear_nan    = false; // If this ends up being an input series, clear it to NaN initially.
	
	Math_Block_AST *code = nullptr;
	bool initial_is_conc;
	Math_Block_AST *initial_code = nullptr;
	bool override_is_conc = false;
	Math_Block_AST *override_code = nullptr;
	
	Math_Block_AST *adds_code_to_existing = nullptr;
	
	void process_declaration(Catalog *catalog);
};

struct
Flux_Registration : Registration_Base {
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
	bool             mixing              = false;
	
	void process_declaration(Catalog *catalog);
};

struct
Discrete_Order_Registration : Registration_Base {
	// TODO: eventually this one could be more complex to take into account order of when things are added or subtracted, or recomputation of values etc.
	std::vector<Entity_Id> fluxes;
	
	void process_declaration(Catalog *catalog);
};

struct
External_Computation_Registration : Registration_Base {
	std::string      function_name;
	
	// TODO: May need a vector of components
	Entity_Id        component = invalid_entity_id;
	
	Entity_Id        connection_component = invalid_entity_id;
	Entity_Id        connection           = invalid_entity_id;
	
	Math_Block_AST  *code = nullptr;
	
	std::string      init_function_name;
	
	Math_Block_AST  *init_code = nullptr;
	
	void process_declaration(Catalog *catalog);
};

enum class
Function_Type {
	decl, intrinsic, linked,
};

struct
Function_Registration : Registration_Base {
	std::vector<std::string> args;
	std::vector<Entity_Id> expected_units;
	
	Function_Type    fun_type;
	Math_Block_AST  *code = nullptr;
	
	// TODO: may need some info on expected argument types (especially for externals)
	
	void process_declaration(Catalog *catalog);
};

struct
Constant_Registration : Registration_Base {
	Value_Type value_type;
	Parameter_Value value;
	Entity_Id unit;
	
	void process_declaration(Catalog *catalog);
};

struct
Unit_Registration : Registration_Base {
	
	void process_declaration(Catalog *catalog);

	Unit_Data data; // This is only guaranteed to be valid after model is finalized.
	
	// The below ones are only used until the model is finalized.
	std::vector<Entity_Id> composed_of;
	Entity_Id unit_of        = invalid_entity_id;
	Var_Location unit_of_loc = invalid_var_location;
	Entity_Id refers_to      = invalid_entity_id;
};

enum class
Connection_Type {
	unrecognized = 0, directed_graph, grid1d,
};

struct
Connection_Registration : Registration_Base {
	Connection_Type type = Connection_Type::unrecognized;
	
	Entity_Id node_index_set = invalid_entity_id;  // Only for grid1d. For directed graph, the nodes could be indexed variously
	Entity_Id edge_index_set = invalid_entity_id;  // Only for directed_graph for now.
	
	bool no_cycles = false;
	
	std::vector<Entity_Id> components;
	Math_Expr_AST *regex = nullptr;
	
	void process_declaration(Catalog *catalog);
};

struct
Solver_Registration : Registration_Base {
	Entity_Id solver_fun = invalid_entity_id;
	Entity_Id h_unit = invalid_entity_id;
	double hmin;
	Entity_Id h_par = invalid_entity_id;
	Entity_Id hmin_par = invalid_entity_id;
	std::vector<std::pair<Specific_Var_Location, Source_Location>> locs; // NOTE: We use a specific_var_location to merge some functionality in model_composition.cpp, but all the data we need is really just in Var_Location
	
	void process_declaration(Catalog *catalog);
};

struct
Assert_Registration : Registration_Base {
	
	Var_Location loc;
	Math_Block_AST *check = nullptr;
	
	void process_declaration(Catalog *catalog);
};

struct
Solver_Function_Registration : Registration_Base {
	Solver_Function *solver_fun = nullptr;
	
	void process_declaration(Catalog *catalog);
};

struct
Mobius_Base_Config {
	bool store_transport_fluxes = false;
	bool store_all_series = false;
	bool developer_mode   = false;
};

struct
Mobius_Config : Mobius_Base_Config {
	std::string mobius_base_path;
	
	Mobius_Config() = default;
	Mobius_Config(const Mobius_Base_Config &c) : Mobius_Base_Config(c) {}
};

struct
Mobius_Model : Catalog {
	
	Mobius_Config config;
	
	Registry<Module_Template_Registration,      Reg_Type::module_template>      module_templates;
	Registry<Module_Registration,               Reg_Type::module>               modules;
	Registry<Library_Registration,              Reg_Type::library>              libraries;
	Registry<Par_Group_Registration,            Reg_Type::par_group>            par_groups;
	Registry<Unit_Registration,                 Reg_Type::unit>                 units;
	Registry<Parameter_Registration,            Reg_Type::parameter>            parameters;  // par_real, par_int, par_bool, par_enum, par_datetime
	Registry<Function_Registration,             Reg_Type::function>             functions;
	Registry<Constant_Registration,             Reg_Type::constant>             constants;
	Registry<Component_Registration,            Reg_Type::component>            components;  // compartment, quantity, property
	Registry<Var_Registration,                  Reg_Type::var>                  vars;
	Registry<Flux_Registration,                 Reg_Type::flux>                 fluxes;
	Registry<Discrete_Order_Registration,       Reg_Type::discrete_order>       discrete_orders;
	Registry<External_Computation_Registration, Reg_Type::external_computation> external_computations;
	Registry<Solver_Registration,               Reg_Type::solver>               solvers;
	Registry<Solver_Function_Registration,      Reg_Type::solver_function>      solver_functions;
	Registry<Connection_Registration,           Reg_Type::connection>           connections;
	Registry<Loc_Registration,                  Reg_Type::loc>                  locs;
	Registry<Assert_Registration,               Reg_Type::assert>               asserts;
	
	// This is the global scope that is visible everywhere, as opposed to the top_scope of the model, which is not visible inside externally declared modules or libraries.
	Decl_Scope global_scope;
	
	Registry_Base *registry(Reg_Type reg_type);
	Decl_Scope    *get_scope(Entity_Id id);
	
	std::unordered_map<std::string, std::unordered_map<std::string, Entity_Id>> parsed_decls;
	
	// NOTE: The ASTs are reused every time you create a Model_Application from the model, so you should only free them if you know you are not going to create more Model_Applications from them.
	void free_asts();
	~Mobius_Model() {
		free_asts();
	}
};

void
insert_dependency(Mobius_Model *model, Index_Set_Tuple &index_sets, Entity_Id index_set);

Mobius_Config
load_config(String_View config = "config.txt");

Mobius_Model *
load_model(String_View file_name, Mobius_Config *config = nullptr, Model_Options *options = nullptr);

//void
//process_location_argument(Mobius_Model *model, Decl_Scope *scope, Argument_AST *arg, Var_Location *location,
//	bool allow_unspecified = false, bool allow_restriction = false, Entity_Id *par_id = nullptr);

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