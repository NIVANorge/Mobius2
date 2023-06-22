

#ifndef MOBIUS_C_API_H
#define MOBIUS_C_API_H

#if (defined(_WIN32) || defined(_WIN64))
	#define DLLEXPORT extern "C" __declspec(dllexport)
#elif (defined(__unix__) || defined(__linux__) || defined(__unix) || defined(unix))
	#define DLLEXPORT extern "C" __attribute((visibility("default")))
#endif

#include "common_types.h"
#include "model_application.h"  // TODO: Only needed here because of State_Var::Type . Maybe move that to common_types.

struct Model_Application;

DLLEXPORT s64
mobius_encountered_error(char *msg_out, s64 buf_len);

DLLEXPORT s64
mobius_encountered_log(char *msg_out, s64 buf_len);

DLLEXPORT Model_Application *
mobius_build_from_model_and_data_file(char * model_file, char * data_file);

DLLEXPORT bool
mobius_run_model(Model_Application *app, s64 ms_timeout);

DLLEXPORT s64
mobius_get_steps(Model_Application *app, Var_Id::Type type);

DLLEXPORT Time_Step_Size
mobius_get_time_step_size(Model_Application *app);

DLLEXPORT char *
mobius_get_start_date(Model_Application *app, Var_Id::Type type);


DLLEXPORT Entity_Id
mobius_deserialize_entity(Model_Application *app, Entity_Id scope_id, char *serial_name);

DLLEXPORT Entity_Id
mobius_get_entity(Model_Application *app, Entity_Id scope_id, char *handle_name);

DLLEXPORT Var_Id
mobius_deserialize_var(Model_Application *app, char *serial_name);

DLLEXPORT Var_Id
mobius_get_var_id_from_list(Model_Application *app, Entity_Id *ids, s64 id_count);

DLLEXPORT Var_Id
mobius_get_special_var(Model_Application *app, Var_Id parent1, Var_Id parent2, State_Var::Type type);


DLLEXPORT s64
mobius_get_index_set_count(Model_Application *app, Entity_Id id);


DLLEXPORT void
mobius_get_series_data(Model_Application *app, Var_Id var_id, char **index_names, s64 indexes_count, double *series_out, s64 time_steps_out);

struct
Mobius_Series_Metadata {
	char *name;
	char *unit;
};

DLLEXPORT Mobius_Series_Metadata
mobius_get_series_metadata(Model_Application *app, Var_Id var_id);


DLLEXPORT s64
mobius_get_value_type(Model_Application *app, Entity_Id id);

DLLEXPORT void
mobius_set_parameter_real(Model_Application *app, Entity_Id par_id, char **index_names, s64 indexes_count, double value);

DLLEXPORT double
mobius_get_parameter_real(Model_Application *app, Entity_Id par_id, char **index_names, s64 indexes_count);

DLLEXPORT void
mobius_set_parameter_int(Model_Application *app, Entity_Id par_id, char **index_names, s64 indexes_count, s64 value);

DLLEXPORT s64
mobius_get_parameter_int(Model_Application *app, Entity_Id par_id, char **index_names, s64 indexes_count);

DLLEXPORT void
mobius_set_parameter_string(Model_Application *app, Entity_Id par_id, char **index_names, s64 indexes_count, char *value);

DLLEXPORT char *
mobius_get_parameter_string(Model_Application *app, Entity_Id par_id, char **index_names, s64 indexes_count);

struct
Mobius_Entity_Metadata {
	char *name;
	char *unit;
	char *description;
	double min;
	double max;
};

DLLEXPORT Mobius_Entity_Metadata
mobius_get_entity_metadata(Model_Application *app, Entity_Id id);

#endif