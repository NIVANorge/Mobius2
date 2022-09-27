

#ifndef MOBIUS_C_API_H
#define MOBIUS_C_API_H

#if (defined(_WIN32) || defined(_WIN64))
	#define DLLEXPORT extern "C" __declspec(dllexport)
#elif (defined(__unix__) || defined(__linux__) || defined(__unix) || defined(unix))
	#define DLLEXPORT extern "C" __attribute((visibility("default")))
#endif

#include "common_types.h"

struct Module_Declaration;
struct Model_Application;


struct Model_Entity_Reference {
	enum class Type : s16 {
		//NOTE: Don't change values of these without updating mobipy.
		invalid = 0, module = 1, compartment = 2,
	} type;
	Module_Declaration *module;
	Entity_Id           entity;
};

struct
Module_Entity_Reference {
	enum class Type : s16 {
		invalid = 0, parameter = 1, compartment = 2, prop_or_quant = 3, flux = 4,
	} type;
	Entity_Id entity;
	Value_Type value_type;
};


DLLEXPORT s64
mobius_encountered_error(char *msg_out, s64 buf_len);

DLLEXPORT s64
mobius_encountered_warning(char *msg_out, s64 buf_len);

DLLEXPORT Model_Application *
mobius_build_from_model_and_data_file(char * model_file, char * data_file);

DLLEXPORT void
mobius_run_model(Model_Application *app);

DLLEXPORT Model_Entity_Reference
mobius_get_model_entity_by_handle(Model_Application *app, char *handle_name);

DLLEXPORT Module_Declaration *
mobius_get_module_reference_by_name(Model_Application *app, char *name);

DLLEXPORT Module_Entity_Reference
mobius_get_module_entity_by_handle(Module_Declaration *module, char *handle_name);

DLLEXPORT void
mobius_set_parameter_real(Model_Application *app, Entity_Id par_id, char **index_names, s64 indexes_count, double value);

DLLEXPORT double
mobius_get_parameter_real(Model_Application *app, Entity_Id par_id, char **index_names, s64 indexes_count);

DLLEXPORT Var_Location
mobius_get_var_location(Model_Application *app, Entity_Id comp_id, Entity_Id prop_id);

DLLEXPORT Var_Location
mobius_get_dissolved_location(Model_Application *app, Var_Location loc, Entity_Id prop_id);

DLLEXPORT Var_Id
mobius_get_var_id(Model_Application *app, Var_Location loc);

DLLEXPORT Var_Id
mobius_get_conc_id(Model_Application *app, Var_Location loc);

DLLEXPORT Var_Id
mobius_get_additional_series_id(Model_Application *app, char *name);

DLLEXPORT s64
mobius_get_steps(Model_Application *app, Stat_Class type);

DLLEXPORT void
mobius_get_series_data(Model_Application *app, Var_Id var_id, char **index_names, s64 indexes_count, double *series_out, s64 time_steps_out, char *name_out, s64 name_out_size);

DLLEXPORT Time_Step_Size
mobius_get_time_step_size(Model_Application *app);

DLLEXPORT char *
mobius_get_start_date(Model_Application *app, Stat_Class type);



#endif