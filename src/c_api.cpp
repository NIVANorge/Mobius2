

#include "../src/model_application.h"


#if (defined(_WIN32) || defined(_WIN64))
	#define DLLEXPORT extern "C" __declspec(dllexport)
#elif (defined(__unix__) || defined(__linux__) || defined(__unix) || defined(unix))
	#define DLLEXPORT extern "C" __attribute((visibility("default")))
#endif

// NOTE: This is just some preliminary testing of the C api. Will build it out properly later.

DLLEXPORT Model_Application *
mobius_build_from_model_and_data_file(char * model_file, char * data_file) {
	
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
mobius_run_model(Model_Application *app) {

	run_model(app);
}

struct Model_Entity_Reference {
	enum class Type : s16 {
		//NOTE: Don't change values of these without updating mobipy.
		invalid = 0, module = 1, compartment = 2,
	} type;
	Module_Declaration *module;
	Entity_Id           entity;
};

DLLEXPORT Model_Entity_Reference
mobius_get_model_entity_by_handle(Model_Application *app, char *handle_name) {
	Model_Entity_Reference result;
	result.type = Model_Entity_Reference::Type::invalid;
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

	return result;
}

DLLEXPORT Module_Declaration *
mobius_get_module_reference_by_name(Model_Application *app, char *name) {
	// TODO: better system to look up module by name
	for(auto module : app->model->modules) {
		if(module->module_name == name)
			return module;
	}
	fatal_error(Mobius_Error::api_usage, "\"", name, "\" is not a module in the model \"", app->model->model_name, "\".");
	return nullptr;
}

struct
Module_Entity_Reference {
	enum class Type : s16 {
		invalid = 0, parameter = 1, compartment = 2, prop_or_quant = 3, flux = 4,
	} type;
	Entity_Id entity;
	Value_Type value_type;
};

DLLEXPORT Module_Entity_Reference
mobius_get_module_entity_by_handle(Module_Declaration *module, char *handle_name) {
	Module_Entity_Reference result;
	result.type = Module_Entity_Reference::Type::invalid;
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
		// TODO: again this system is bad!
		auto comp = module->compartments[result.entity];
		if(is_valid(comp->global_id))
			result.entity = comp->global_id;
		return result;
	} else if(entity.reg_type == Reg_Type::property_or_quantity) {
		result.type = Module_Entity_Reference::Type::prop_or_quant;
		auto prop = module->properties_and_quantities[result.entity];
		if(is_valid(prop->global_id))
			result.entity = prop->global_id;
		return result;
	} else if(entity.reg_type == Reg_Type::flux) {
		result.type = Module_Entity_Reference::Type::flux;
		return result;
	}
	
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
mobius_set_parameter_real(Model_Application *app, Entity_Id par_id, char **index_names, s64 indexes_count, double value) {
	
	s64 offset = get_offset_by_index_names(app, &app->parameter_data, par_id, index_names, indexes_count);
	if(offset < 0)
		fatal_error(Mobius_Error::api_usage, "Wrong index for parameter in mobius_set_parameter_real().");
	
	Parameter_Value val;
	val.val_real = value;
	
	*app->parameter_data.get_value(offset) = val;
}

DLLEXPORT double
mobius_get_parameter_real(Model_Application *app, Entity_Id par_id, char **index_names, s64 indexes_count) {
	
	s64 offset = get_offset_by_index_names(app, &app->parameter_data, par_id, index_names, indexes_count);
	if(offset < 0)
		fatal_error(Mobius_Error::api_usage, "Wrong index for parameter in mobius_get_parameter_real().");
	
	double value = (*app->parameter_data.get_value(offset)).val_real;
	return value;
}


DLLEXPORT Value_Location
mobius_get_value_location(Model_Application *app, Entity_Id comp_id, Entity_Id prop_id) {
	Value_Location loc;
	loc.neighbor = invalid_entity_id; // For safety, but probably not needed.
	loc.type = Location_Type::located;
	loc.compartment = comp_id;
	loc.property_or_quantity = prop_id;
	loc.n_dissolved = 0; //TODO!
	
	return loc;
}

DLLEXPORT Value_Location
mobius_get_dissolved_location(Model_Application *app, Value_Location loc, Entity_Id prop_id) {
	return add_dissolved(app->model, loc, prop_id);
}

struct
Var_Reference {
	Var_Id id;
	s16 type;    //TODO: This should be baked into the Var_Id instead!!
};

DLLEXPORT Var_Reference
mobius_get_var_reference(Model_Application *app, Value_Location loc) {
	Var_Reference result;
	result.type = 0;
	
	auto find = app->model->state_vars.location_to_id.find(loc);
	if(find != app->model->state_vars.location_to_id.end()) {
		result.type = 1;
		result.id = find->second;
		return result;
	}
	auto find2 = app->model->series.location_to_id.find(loc);
	if(find2 != app->model->series.location_to_id.end()) {
		result.type = 2;
		result.id = find2->second;
		return result;
	}
	
	return result;
}

DLLEXPORT Var_Reference
mobius_get_conc_reference(Model_Application *app, Value_Location loc) {
	Var_Reference res = mobius_get_var_reference(app, loc);
	if(res.type == 2) res.type = 0; // Input series don't have concentrations.
	if(res.type == 0) return res;
	
	auto var = app->model->state_vars[res.id];
	if(is_valid(var->dissolved_conc))
		res.id = var->dissolved_conc;
	else
		res.type = 0;
	return res;
}

DLLEXPORT s64
mobius_get_steps(Model_Application *app, s16 type) {
	if(type == 1) {
		if(!app->result_data.has_been_set_up)
			return 0;
		return app->result_data.time_steps;
	} else if(type == 2) {
		return app->series_data.time_steps;
	}
	return 0;
}

// TODO: Getting the name should be a separate call though.
DLLEXPORT void
mobius_get_series_data(Model_Application *app, Var_Id var_id, s16 type, char **index_names, s64 indexes_count, double *series_out, s64 time_steps_out, char *name_out, s64 name_out_size) {
	if(!time_steps_out) return;
	
	s64 offset;
	String_View name;
	if(type == 1) {
		offset = get_offset_by_index_names(app, &app->result_data, var_id, index_names, indexes_count);
		name = app->model->state_vars[var_id]->name;
	} else if (type == 2) {
		offset = get_offset_by_index_names(app, &app->series_data, var_id, index_names, indexes_count);
		name = app->model->series[var_id]->name;
	}
	if(offset < 0)
		fatal_error(Mobius_Error::api_usage, "Wrong index for series in mobius_get_series_data().");
	
	if(name.count > name_out_size)
		name.count = name_out_size;
	sprintf(name_out, "%.*s", name.count, name.data);
	
	for(s64 step = 0; step < time_steps_out; ++step) {
		double value;
		if(type == 1)
			value = *app->result_data.get_value(offset, step);
		else
			value = *app->series_data.get_value(offset, step);
		series_out[step] = value;
	}
}

struct
Time_Step_Size2 {
	s32 unit;
	s32 magnitude;
};

// It somehow messes with the C calling convention if we pass a Time_Step_Size, so we have to convert it :O
DLLEXPORT Time_Step_Size2
mobius_get_time_step_size(Model_Application *app) {
	Time_Step_Size2 result;
	result.unit = (s32)app->timestep_size.unit;
	result.magnitude = app->timestep_size.magnitude;
	
	return result;
}

DLLEXPORT char *
mobius_get_start_date(Model_Application *app, s16 type) {
	// NOTE: The data for this one gets overwritten when you call it again. Not thread safe
	String_View str;
	if(type == 1)
		str = app->result_data.start_date.to_string();
	else if(type == 2)
		str = app->series_data.start_date.to_string();
	
	return str.data;
}
