
#include "c_api.h"
#include "model_application.h"



#ifndef MOBIUS_ERROR_STREAMS
	static_assert(false, "The C API must be compiled with MOBIUS_ERROR_STREAMS defined in the compile script using -DMOBIUS_ERROR_STREAMS.");
#endif

// NOTE: This is just some preliminary testing of the C api. Will build it out properly later.

std::stringstream global_error_stream;
std::stringstream global_warning_stream;

DLLEXPORT s64
mobius_encountered_error(char *msg_out, s64 buf_len) {
	global_error_stream.getline(msg_out, buf_len, 0);
	// If the stream is shorter than the buf_len, the eofbit flag is set. We have to clear the flag to be able to reuse the stream. This does NOT clear the stream data.
	global_error_stream.clear(); 
	return strlen(msg_out);
}

DLLEXPORT s64
mobius_encountered_warning(char *msg_out, s64 buf_len) {
	global_warning_stream.getline(msg_out, buf_len, 0);
	global_warning_stream.clear(); // NOTE: see comment in mobius_encountered_error
	return strlen(msg_out);
}

DLLEXPORT Model_Application *
mobius_build_from_model_and_data_file(char * model_file, char * data_file) {
	
	try {
		Mobius_Model *model = load_model(model_file);
		model->compose();
		
		auto app = new Model_Application(model);
			
		Data_Set *data_set = new Data_Set;
		data_set->read_from_file(data_file);
		
		app->build_from_data_set(data_set);

		app->compile();
		
		return app;
	} catch(int) {}
	
	return nullptr;
}

DLLEXPORT void
mobius_run_model(Model_Application *app) {
	try {
		run_model(app);
	} catch(int) {}
}

DLLEXPORT Model_Entity_Reference
mobius_get_model_entity_by_handle(Model_Application *app, char *handle_name) {
	Model_Entity_Reference result;
	result.type = Model_Entity_Reference::Type::invalid;
	
	try {
		auto find = app->model->module_ids.find(handle_name);
		if(find != app->model->module_ids.end()) {
			result.type   = Model_Entity_Reference::Type::module;
			result.module = app->model->modules[find->second];
			return result;
		} 
		Entity_Id comp_id = app->model->modules[0]->compartments.find_by_handle_name(handle_name);
		if(is_valid(comp_id)) {
			result.type   = Model_Entity_Reference::Type::compartment;
			result.entity = comp_id;
			result.module = app->model->modules[0];
			return result;
		}
	} catch(int) {}

	return result;
}

DLLEXPORT Module_Declaration *
mobius_get_module_reference_by_name(Model_Application *app, char *name) {
	try {
		// TODO: better system to look up module by name
		for(auto module : app->model->modules) {
			if(module->module_name == name)
				return module;
		}
		fatal_error(Mobius_Error::api_usage, "\"", name, "\" is not a module in the model \"", app->model->model_name, "\".");
	} catch(int) {}
	
	return nullptr;
}

DLLEXPORT Module_Entity_Reference
mobius_get_module_entity_by_handle(Module_Declaration *module, char *handle_name) {
	Module_Entity_Reference result;
	result.type = Module_Entity_Reference::Type::invalid;
	
	try {
		Entity_Id entity = module->find_handle(handle_name);
		if(!is_valid(entity))
			return result;
		result.entity = entity;
		
		if(entity.reg_type == Reg_Type::parameter) {
			result.type = Module_Entity_Reference::Type::parameter;
			result.value_type = get_value_type(module->parameters[entity]->decl_type);
			return result;
		} else if(entity.reg_type == Reg_Type::compartment) {
			result.type = Module_Entity_Reference::Type::compartment;
			result.entity = module->get_global(result.entity);
		} else if(entity.reg_type == Reg_Type::property_or_quantity) {
			result.type = Module_Entity_Reference::Type::prop_or_quant;
			result.entity = module->get_global(result.entity);
			return result;
		} else if(entity.reg_type == Reg_Type::flux) {
			result.type = Module_Entity_Reference::Type::flux;
			return result;
		}
	} catch(int) {}
	
	return result;
}

template<typename Val_T, typename Handle_T> s64
get_offset_by_index_names(Model_Application *app, Storage_Structure<Handle_T> *storage, Handle_T handle, char **index_names, s64 indexes_count) {
	
	const std::vector<Entity_Id> &index_sets = storage->get_index_sets(handle);
	if(index_sets.size() != indexes_count)
		fatal_error("The object requires ", index_sets.size(), " index", index_sets.size()>1?"es":"", ", got ", indexes_count, ".");
	
	std::vector<Index_T> indexes(indexes_count);
	for(s64 idxidx = 0; idxidx < indexes_count; ++idxidx) {
		indexes[idxidx] = app->get_index(index_sets[idxidx], index_names[idxidx]);
		if(indexes[idxidx].index < 0)
			fatal_error("The index set \"", app->model->find_entity(index_sets[idxidx])->name, "\" does not contain the index \"", index_names[idxidx], "\".");
	}
	return storage->get_offset_alternate(handle, indexes);
}

DLLEXPORT void
mobius_set_parameter_real(Model_Application *app, Entity_Id par_id, char **index_names, s64 indexes_count, double value) {
	try {
		s64 offset = get_offset_by_index_names(app, &app->parameter_structure, par_id, index_names, indexes_count);
		
		Parameter_Value val;
		val.val_real = value;
		
		*app->data.parameters.get_value(offset) = val;
	} catch(int) {}
}

DLLEXPORT double
mobius_get_parameter_real(Model_Application *app, Entity_Id par_id, char **index_names, s64 indexes_count) {
	try {
		s64 offset = get_offset_by_index_names(app, &app->parameter_structure, par_id, index_names, indexes_count);
		
		double value = (*app->data.parameters.get_value(offset)).val_real;
		return value;
	} catch(int) {}
	return 0.0;
}


DLLEXPORT Var_Location
mobius_get_var_location(Model_Application *app, Entity_Id comp_id, Entity_Id prop_id) {
	Var_Location loc;
	loc.type = Var_Location::Type::located;
	loc.compartment = comp_id;
	loc.property_or_quantity = prop_id;
	loc.n_dissolved = 0; //TODO!
	
	return loc;
}

DLLEXPORT Var_Location
mobius_get_dissolved_location(Model_Application *app, Var_Location loc, Entity_Id prop_id) {
	try {
		return add_dissolved(app->model, loc, prop_id);
	} catch(int) {}
	return loc;
}

DLLEXPORT Var_Id
mobius_get_var_id(Model_Application *app, Var_Location loc) {
	Var_Id result = invalid_var;
	
	try {
		auto find = app->model->state_vars.location_to_id.find(loc);
		if(find != app->model->state_vars.location_to_id.end())
			return find->second;
		auto find2 = app->model->series.location_to_id.find(loc);
		if(find2 != app->model->series.location_to_id.end())
			return find2->second;
	} catch(int) {}
	
	return result;
}

DLLEXPORT Var_Id
mobius_get_conc_id(Model_Application *app, Var_Location loc) {
	
	Var_Id res = mobius_get_var_id(app, loc);
	if(res.type == 1 || res.type == 2) res.type = -1; // Input series don't have concentrations.
	if(res.type == -1) return res;
	
	auto var = app->model->state_vars[res];
	if(is_valid(var->dissolved_conc))
		res = var->dissolved_conc;
	else
		res.type = -1;
	return res;
}

DLLEXPORT Var_Id
mobius_get_additional_series_id(Model_Application *app, char *name) {
	
	std::set<Var_Id> ids = app->additional_series[name];
	
	try {
		if(ids.empty())
			fatal_error(Mobius_Error::api_usage, "The series by the name \"", name, "\" was not provided in the input data.");
	} catch(int) {}
	
	return *ids.begin();     // NOTE: For additional series there will only be one per name.
}

DLLEXPORT s64
mobius_get_steps(Model_Application *app, s16 type) {
	if(type == 0) {
		if(!app->result_structure.has_been_set_up)
			return 0;
		return app->data.results.time_steps;
	} else if(type == 1)
		return app->data.series.time_steps;
	else if(type == 2)
		return app->data.additional_series.time_steps;
	
	return 0;
}

// TODO: Getting the name should be a separate call though.
DLLEXPORT void
mobius_get_series_data(Model_Application *app, Var_Id var_id, char **index_names, s64 indexes_count, double *series_out, s64 time_steps_out, char *name_out, s64 name_out_size) {
	if(!time_steps_out) return;
	
	try {
		s64 offset;
		String_View name;
		if(var_id.type == 0) {
			offset = get_offset_by_index_names(app, &app->result_structure, var_id, index_names, indexes_count);
			name = app->model->state_vars[var_id]->name;
		} else if (var_id.type == 1) {
			offset = get_offset_by_index_names(app, &app->series_structure, var_id, index_names, indexes_count);
			name = app->model->series[var_id]->name;
		} else if (var_id.type == 2) {
			offset = get_offset_by_index_names(app, &app->additional_series_structure, var_id, index_names, indexes_count);
			name = app->additional_series[var_id]->name;
		}
		
		if(name.count > name_out_size)
			name.count = name_out_size;
		sprintf(name_out, "%.*s", name.count, name.data);
		
		for(s64 step = 0; step < time_steps_out; ++step) {
			double value;
			if(var_id.type == 0)
				value = *app->data.results.get_value(offset, step);
			else if(var_id.type == 1)
				value = *app->data.series.get_value(offset, step);
			else if(var_id.type == 2)
				value = *app->data.additional_series.get_value(offset, step);
			series_out[step] = value;
		}
	} catch(int) {}
}


DLLEXPORT Time_Step_Size
mobius_get_time_step_size(Model_Application *app) {
	
	return app->time_step_size;
}

DLLEXPORT char *
mobius_get_start_date(Model_Application *app, s16 type) {
	// NOTE: The data for this one gets overwritten when you call it again. Not thread safe
	String_View str;
	if(type == 0)
		str = app->data.results.start_date.to_string();
	else if(type == 1)
		str = app->data.series.start_date.to_string();
	else if(type == 2)
		str = app->data.additional_series.start_date.to_string();
	
	return str.data;
}
