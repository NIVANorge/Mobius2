

#include "../src/model_application.h"


#if (defined(_WIN32) || defined(_WIN64))
	#define DLLEXPORT extern "C" __declspec(dllexport)
#elif (defined(__unix__) || defined(__linux__) || defined(__unix) || defined(unix))
	#define DLLEXPORT extern "C" __attribute((visibility("default")))
#endif

// NOTE: This is just some preliminary testing of the C api. Will build it out properly later.

DLLEXPORT Model_Application *
c_api_build_from_model_and_data_file(char * model_file, char * data_file) {
	
	Mobius_Model *model = load_model(model_file);
	model->compose();
	
	auto app = new Model_Application(model);
		
	Data_Set *data_set = new Data_Set;
	data_set->read_from_file(data_file);
	
	app->build_from_data_set(data_set);

	app->compile();
	
	return app;
}

DLLEXPORT void
c_api_run_model(Model_Application *app) {
	
	run_model(app);
}

DLLEXPORT Module_Declaration *
c_api_get_module_reference_by_name(Model_Application *app, char *name) {
	// TODO: better system to look up module by name
	for(auto module : app->model->modules) {
		if(module->module_name == name)
			return module;
	}
	fatal_error(Mobius_Error::api_usage, "\"", name, "\" is not a module in the model \"", app->model->model_name, "\".");
	return nullptr;
}

// TODO: should also have one to get it by handle name. But then we need to store the module handle names.

// TODO: get global module.

struct
DLL_Parameter_Reference {
	Entity_Id par_id;
	Value_Type value_type;
};

DLLEXPORT DLL_Parameter_Reference
c_api_get_parameter_reference_by_handle_name(Module_Declaration *module, char *handle_name) {
	DLL_Parameter_Reference result;
	result.par_id = module->parameters.find_by_handle_name(handle_name);
	if(is_valid(result.par_id))
		result.value_type = get_value_type(module->parameters[result.par_id]->decl_type);
	else
		fatal_error(Mobius_Error::api_usage, "\"", handle_name, "\" is not the name of a parameter in the module \"", module->module_name, "\".");
	
	//warning_print("Found parameter id ", result.par_id.id, " module ", result.par_id.module_id, "\n");
	
	return result;
}

template<typename Val_T, typename Handle_T> s64
get_offset_by_index_names(Model_Application *app, Structured_Storage<Val_T, Handle_T> *storage, Handle_T handle, char **index_names, s64 indexes_count) {
	
	const std::vector<Entity_Id> &index_sets = storage->get_index_sets(handle);
	if(index_sets.size() != indexes_count)
		return -1;
	
	std::vector<Index_T> indexes(indexes_count);
	for(s64 idxidx = 0; idxidx < indexes_count; ++idxidx) {
		indexes[idxidx] = app->get_index(index_sets[idxidx], index_names[idxidx]);
		if(indexes[idxidx].index < 0) {
			return -1;
		}
	}
	return storage->get_offset_alternate(handle, indexes);
}

DLLEXPORT void
c_api_set_parameter_real(Model_Application *app, DLL_Parameter_Reference ref, char **index_names, s64 indexes_count, double value) {
	
	if(ref.value_type != Value_Type::real)
		fatal_error(Mobius_Error::api_usage, "Wrong type for parameter in c_api_set_parameter_real().");
	
	s64 offset = get_offset_by_index_names(app, &app->parameter_data, ref.par_id, index_names, indexes_count);
	if(offset < 0)
		fatal_error(Mobius_Error::api_usage, "Wrong index for parameter in c_api_set_parameter_real().");
	
	Parameter_Value val;
	val.val_real = value;
	
	//warning_print("se value: offset: ", offset, " id : ", ref.par_id.id, " module ", ref.par_id.module_id, "\n");
	
	*app->parameter_data.get_value(offset) = val;
}

DLLEXPORT double
c_api_get_parameter_real(Model_Application *app, DLL_Parameter_Reference ref, char **index_names, s64 indexes_count) {
	
	if(ref.value_type != Value_Type::real)
		fatal_error(Mobius_Error::api_usage, "Wrong type for parameter in c_api_get_parameter_real().");
	
	s64 offset = get_offset_by_index_names(app, &app->parameter_data, ref.par_id, index_names, indexes_count);
	if(offset < 0)
		fatal_error(Mobius_Error::api_usage, "Wrong index for parameter in c_api_get_parameter_real().");
	
	double value = (*app->parameter_data.get_value(offset)).val_real;
	//warning_print("se value: offset: ", offset, " id : ", ref.par_id.id, " module ", ref.par_id.module_id, " value: ", value, "\n");
	return value;
}
