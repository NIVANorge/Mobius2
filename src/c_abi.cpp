
#include "c_abi.h"
#include "model_application.h"



#ifndef MOBIUS_ERROR_STREAMS
	static_assert(false, "The C ABI must be compiled with MOBIUS_ERROR_STREAMS defined in the compile script using -DMOBIUS_ERROR_STREAMS.");
#endif

// NOTE: This is just some preliminary testing of the C api. Will build it out properly later.

std::stringstream global_error_stream;
std::stringstream global_log_stream;

void
check_index_set_amount(Model_Application *app, const std::vector<Entity_Id> &index_sets, s64 indexes_count) {
	
	if(index_sets.size() == indexes_count) return;
	
	begin_error(Mobius_Error::api_usage);
	error_print("The object requires ", index_sets.size(), " index", index_sets.size()!=1?"es":"", ", got ", indexes_count, ".");
	if(index_sets.size() > 0) {
		error_print(" The following index sets need to be addressed (in that order):\n");
		for(auto index_set : index_sets)
			error_print("\"", app->model->index_sets[index_set]->name, "\" ");
	}
	mobius_error_exit();
}

template<typename Handle_T>
s64
get_offset_by_index_values(Model_Application *app, Storage_Structure<Handle_T> *storage, Handle_T handle, Mobius_Index_Value *index_values, s64 indexes_count) {
	
	const auto &index_sets = storage->get_index_sets(handle);
	
	check_index_set_amount(app, index_sets, indexes_count);
	
	//TODO: It is a bit annoying to use the Token api for this, moreover errors will not be reported correctly
	
	std::vector<Token> idx_names(indexes_count);
	
	for(s64 idxidx = 0; idxidx < indexes_count; ++idxidx) {
		Token &token = idx_names[idxidx];
		auto &idx_val = index_values[idxidx];
		if(strlen(idx_val.name) > 0) {
			token.type = Token_Type::quoted_string;
			token.string_value = idx_val.name;
		} else {
			token.type = Token_Type::integer;
			token.val_int = idx_val.value;
		}
	}
	
	Indexes indexes;
	app->index_data.find_indexes(index_sets, idx_names, indexes);
	
	// TODO: This is maybe unnecessary if it is also done in get_offset.
	if(!app->index_data.are_in_bounds(indexes)) {
		fatal_error(Mobius_Error::api_usage, "One or more of the indexes are out of bounds.");
	}
	
	return storage->get_offset(handle, indexes);
}

DLLEXPORT s64
mobius_encountered_error(char *msg_out, s64 buf_len) {
	global_error_stream.getline(msg_out, buf_len, 0);
	// If the stream is shorter than the buf_len, the eofbit flag is set. We have to clear the flag to be able to reuse the stream. This does NOT clear the stream data.
	global_error_stream.clear(); 
	return strlen(msg_out);
}

DLLEXPORT s64
mobius_encountered_log(char *msg_out, s64 buf_len) {
	global_log_stream.getline(msg_out, buf_len, 0);
	global_log_stream.clear(); // NOTE: see comment in mobius_encountered_error
	return strlen(msg_out);
}

DLLEXPORT Model_Data *
mobius_build_from_model_and_data_file(char * model_file, char * data_file, char *base_path, Mobius_Base_Config *cfg) {
	
	Mobius_Config config = *cfg;
	config.mobius_base_path = base_path;
	
	try {
		Mobius_Model *model = load_model(model_file, &config);
		auto app = new Model_Application(model);
			
		Data_Set *data_set = new Data_Set;
		data_set->read_from_file(data_file);
		
		app->build_from_data_set(data_set);
		app->compile();
		
		return &app->data;
	} catch(int) {}
	
	return nullptr;
}

DLLEXPORT void
mobius_delete_application(Model_Data *data) {
	auto app = data->app;
	delete app->data_set;
	delete app->model;
	delete app;
}

DLLEXPORT void
mobius_delete_data(Model_Data *data) {
	delete data;
}

DLLEXPORT Model_Data *
mobius_copy_data(Model_Data *data, bool copy_results) {
	try {
		return data->copy(copy_results);
	} catch(int) {}
	return nullptr;
}

DLLEXPORT void
mobius_save_data_set(Model_Data *data, char *data_file) {
	try {
		data->app->save_to_data_set(data);
		data->app->data_set->write_to_file(data_file);
	} catch(int) {}
}

DLLEXPORT bool
mobius_run_model(Model_Data *data, s64 ms_timeout) {
	
	try {
		return run_model(data, ms_timeout);
	} catch(int) {}
	return false;
}

DLLEXPORT s64
mobius_get_steps(Model_Data *data, Var_Id::Type type) {
	
	auto &storage = data->get_storage(type);
	if(!storage.structure->has_been_set_up)
		return 0;
	return storage.time_steps;
}

DLLEXPORT Time_Step_Size
mobius_get_time_step_size(Model_Data *data) {
	return data->app->time_step_size;
}

DLLEXPORT void
mobius_get_start_date(Model_Data *data, Var_Id::Type type, char *buf) {
	// NOTE: The buf should have at least 20 capacity.
	data->get_storage(type).start_date.to_string(buf);
}

DLLEXPORT Entity_Id
mobius_deserialize_entity(Model_Data *data, Entity_Id scope_id, char *serial_name) {
	try {
		auto scope = data->app->model->get_scope(scope_id);
		return scope->deserialize(serial_name, Reg_Type::unrecognized);
	} catch(int) {}
	return invalid_entity_id;
}

DLLEXPORT Entity_Id
mobius_get_entity(Model_Data *data, Entity_Id scope_id, char *handle_name) {
	try {
		auto scope = data->app->model->get_scope(scope_id);
		auto reg = (*scope)[handle_name];
		if(!reg)
			return invalid_entity_id;
		return reg->id;
	} catch(int) {}
	return invalid_entity_id;
}

DLLEXPORT Var_Id
mobius_deserialize_var(Model_Data *data, char *serial_name) {
	return data->app->deserialize(serial_name);
}

DLLEXPORT Var_Id
mobius_get_var_id_from_list(Model_Data *data, Entity_Id *ids, s64 id_count) {
	// TODO: Check that all the ids refer to components.
	try {
		Var_Location loc;
		loc.type = Var_Location::Type::located;
		loc.n_components = id_count;
		// TODO: Check that id_count does not exceed max_var_loc_components ?
		for(int idx = 0; idx < id_count; ++idx)
			loc.components[idx] = ids[idx];
		
		return data->app->vars.id_of(loc);
		
	} catch(int) {}
	return invalid_var;
}

DLLEXPORT Var_Id
mobius_get_special_var(Model_Data *data, Var_Id parent1, Entity_Id parent2, State_Var::Type type) {
	
	auto app = data->app;
	try {
		if(type == State_Var::Type::dissolved_conc)
			return app->vars.find_conc(parent1);
		else if(type == State_Var::Type::dissolved_flux) {
			//log_print("Trying to find carry of ", app->model->find_entity(parent2)->name, " by ", app->vars[parent1]->name, ".\n");
			// TODO: Could also check if the first is a flux and the second a quantity, and give error otherwise.
			for(auto var_id : app->vars.all_fluxes()) {
				auto var = app->vars[var_id];
				if(var->type != State_Var::Type::dissolved_flux) continue;
				if(!is_located(var->loc1) || var->loc1.last() != parent2) continue;
				auto var2 = as<State_Var::Type::dissolved_flux>(var);
				if(var2->flux_of_medium != parent1) continue;
				return var_id;
			}
			return invalid_var;
		} else
			fatal_error(Mobius_Error::internal, "Unimplemented special type");
	} catch(int) {}
	return invalid_var;
}

DLLEXPORT Mobius_Series_Metadata
mobius_get_series_metadata(Model_Data *data, Var_Id var_id) {
	static char unit_buffer[128];
	
	Mobius_Series_Metadata result = {};
	try {
		auto var = data->app->vars[var_id];
		result.name = (char *)var->name.data();
		auto unit = var->unit.to_utf8();
		strcpy(unit_buffer, unit.data());
		result.unit = unit_buffer;   // TODO: Not thread safe or buffer safe.
	} catch(int) {}
	return result;
}


DLLEXPORT s64
mobius_get_index_set_count(Model_Data *data, Entity_Id id) {
	try {
		if(id.reg_type == Reg_Type::parameter)
			return data->app->data.parameters.structure->get_index_sets(id).size();
	} catch(int) {}
	return 0;
}

DLLEXPORT void
mobius_get_series_data(Model_Data *data, Var_Id var_id, Mobius_Index_Value *indexes, s64 indexes_count, double *series_out, s64 time_steps) {
	
	auto app = data->app;
	try {
		if(!is_valid(var_id))
			fatal_error(Mobius_Error::api_usage, "Tried to get data for an invalid id.");
	
		if(var_id.type == Var_Id::Type::temp_var)
			fatal_error(Mobius_Error::api_usage, "The time series for the variable \"", app->vars[var_id]->name, "\" is not stored.");
		
		if(!time_steps) return;
	
		auto &storage = data->get_storage(var_id.type);
		s64 offset = get_offset_by_index_values(app, storage.structure, var_id, indexes, indexes_count);

		for(s64 step = 0; step < time_steps; ++step)
			series_out[step] = *storage.get_value(offset, step);
		
	} catch(int) {}
}

DLLEXPORT void
mobius_set_series_data(Model_Data *data, Var_Id var_id, Mobius_Index_Value *indexes, s64 indexes_count, double *values, s64 *dates, s64 time_steps) {
	
	auto app = data->app;
	try {
	
		if(var_id.type != Var_Id::Type::series && var_id.type != Var_Id::Type::additional_series)
			fatal_error(Mobius_Error::api_usage, "The variable \"", app->vars[var_id]->name, "\" is not an input series.");
		
		if(!time_steps) return;
		
		auto &storage = data->get_storage(var_id.type);
		s64 offset = get_offset_by_index_values(app, storage.structure, var_id, indexes, indexes_count);

		for(s64 step_idx = 0; step_idx < time_steps; ++step_idx) {
			Date_Time date;
			date.seconds_since_epoch = dates[step_idx];
			s64 step = steps_between(storage.start_date, date, app->time_step_size);
			if(step < 0 || step >= storage.time_steps) continue;
			*storage.get_value(offset, step) = values[step_idx];
		}
	
	} catch(int) {}
}


inline bool
is_none_dim(s64 slice_dim) {
	return slice_dim == std::numeric_limits<s64>::min();
}

DLLEXPORT void
mobius_resolve_slice(Model_Data *data, Var_Id var_id, Mobius_Index_Slice *indexes_in, s64 indexes_count, Mobius_Index_Range *ranges_out) {
	
	try {
	// TODO: Maybe generalize so that it can also be used for parameters for instance.
	auto app = data->app;
	Indexes indexes;
	auto &storage = data->get_storage(var_id.type);
	const auto &index_sets = storage.structure->get_index_sets(var_id);
	
	check_index_set_amount(app, index_sets, indexes_count);
	
	// TODO: We should maybe have a guard against slicing something that is sub-indexed to something else that was sliced.
	//  Although we could just deal with that when extracting by setting non-existent parts of the data to NaN.
	
	for(int idxidx = 0; idxidx < indexes_count; ++idxidx) {
		
		auto index_set = index_sets[idxidx];
		auto &slice = indexes_in[idxidx];
		
		s64 count = app->index_data.get_index_count(indexes, index_set).index;
		s64 first = 0;
		s64 last  = count;
		
		bool was_string = false;
		if(strlen(slice.name) > 0) {
			Token idx_name;
			idx_name.type = Token_Type::quoted_string;
			idx_name.string_value = slice.name;
			app->index_data.find_index(index_set, &idx_name, indexes);
			first = indexes.indexes.back().index;
			last = first+1;
			was_string = true;
		} else if(slice.is_slice) {
			if(!is_none_dim(slice.first)) {
				if(slice.first >= 0)
					first = slice.first;
				else
					first = count + slice.first;
			}
			if(!is_none_dim(slice.last)) {
				if(slice.last >= 0)
					last = slice.last;
				else
					last = count + slice.last;
			}
		} else {
			first = slice.first;
			last  = first + 1;
		}
		if(!was_string && !slice.is_slice) {
			Token idx_name;
			idx_name.type = Token_Type::integer;
			idx_name.val_int = first;
			app->index_data.find_index(index_set, &idx_name, indexes);
		}
		
		if(last <= first)
			fatal_error(Mobius_Error::api_usage, "The slice in position ", idxidx, " has an end that is smaller than or equal to the begin.");
		if(first >= count || last > count)
			fatal_error(Mobius_Error::api_usage, "The index or slice in position ", idxidx, " is out of bounds.");
		
		ranges_out[idxidx] = Mobius_Index_Range { first, last };
	}
	
	} catch(int) {}
}

DLLEXPORT void
mobius_get_series_data_slice(Model_Data *data, Var_Id var_id, Mobius_Index_Range *indexes_in, s64 indexes_count, double *pos_out, double *series_out, s64 time_steps) {
	
	try {
	auto app = data->app;
	
	if(var_id.type == Var_Id::Type::temp_var)
		fatal_error(Mobius_Error::api_usage, "The time series for the variable \"", app->vars[var_id]->name, "\" is not stored.");
		
	if(!time_steps) return;
	
	Indexes indexes;
	auto &storage = data->get_storage(var_id.type);
	const auto &index_sets = storage.structure->get_index_sets(var_id);

	s64 first = 0;
	s64 last = 0;
	s64 dim = 1;
	int dim_pos = -1;
	for(int idxidx = 0; idxidx < indexes_count; ++idxidx) {
		auto &idx = indexes_in[idxidx];
		s64 dim0 = idx.last - idx.first;
		if(dim > 1 && dim0 > 1) // TODO: make it work for more than one (have to do recursive)
			fatal_error(Mobius_Error::api_usage, "For now we only support slicing one index set at a time");
		if(dim0 > 1) {
			dim = dim0;
			dim_pos = idxidx;
			first = idx.first;
			last = idx.last;
		}
		indexes.add_index(index_sets[idxidx], idx.first);
	}
	
	for(int idx = first; idx < last; ++idx) {
		indexes.indexes[dim_pos].index = idx;
		s64 offset = storage.structure->get_offset(var_id, indexes);
		
		for(s64 step = 0; step < time_steps; ++step)
			series_out[step*dim + idx] = *storage.get_value(offset, step);
	}
	
	auto set = index_sets[dim_pos];
	Index_T index = Index_T { set, (s32)first-1 };
	pos_out[0] = app->index_data.get_position(index);
	
	int i = 1;
	for(int idx = first; idx < last; ++idx) {
		index.index = idx;
		pos_out[i++] = app->index_data.get_position(index);
	}
	} catch(int) {}
}

// TODO: We could just have a get_decl_type eventually
DLLEXPORT s64
mobius_get_value_type(Model_Data *data, Entity_Id id) {
	try {
		if(id.reg_type == Reg_Type::parameter) {
			Decl_Type type = data->app->model->parameters[id]->decl_type;
			return (s64)type - (s64)Decl_Type::par_real;
		}
	} catch(int) {}
	return -1;
}

// TODO: For some parameters we need to check if they are baked, and then set a flag on the Model_Application telling it it has to be recompiled before further use.
//   This must then be reflected in mobipy so that it actually does the recompilation.
DLLEXPORT void
mobius_set_parameter_numeric(Model_Data *data, Entity_Id par_id, Mobius_Index_Value *indexes, s64 indexes_count, Parameter_Value_Simple value) {
	auto app = data->app;
	try {
		s64 offset = get_offset_by_index_values(app, &app->parameter_structure, par_id, indexes, indexes_count);
		(*data->parameters.get_value(offset)).val_real = value.val_real; // Again, shouldn't matter what type we copy since the bytes will be correct.
	} catch(int) {}
}

// For some reason it doesn't create the correct C linkage if we return a Parameter_Value, probably because it contains a more complex C++ type.
DLLEXPORT Parameter_Value_Simple
mobius_get_parameter_numeric(Model_Data *data, Entity_Id par_id, Mobius_Index_Value *indexes, s64 indexes_count) {
	auto app = data->app;
	Parameter_Value_Simple result;
	try {
		s64 offset = get_offset_by_index_values(app, &app->parameter_structure, par_id, indexes, indexes_count);
		auto res = *data->parameters.get_value(offset);
		result.val_real = res.val_real; // Shouldn't matter what type we copy since the bytes will be copied correctly any way.
	} catch(int) {}
	return result;
}

DLLEXPORT void
mobius_set_parameter_string(Model_Data *data, Entity_Id par_id, Mobius_Index_Value *indexes, s64 indexes_count, char *value) {
	// TODO: All the others also need to check on types.
	auto app = data->app;
	try {
		s64 offset = get_offset_by_index_values(app, &app->parameter_structure, par_id, indexes, indexes_count);
		Parameter_Value val;
		auto par = app->model->parameters[par_id];
		if(par->decl_type == Decl_Type::par_datetime) {
			Token_Stream stream("app", value);
			stream.allow_date_time_tokens = true;
			val.val_datetime = stream.expect_datetime();
		} else if (par->decl_type == Decl_Type::par_enum) {
			s64 intval = par->enum_int_value(value);
			val.val_integer = intval;
		} else
			fatal_error(Mobius_Error::api_usage, "mobius_set_parameter_string can not be called on parameters of this type.");
		*data->parameters.get_value(offset) = val;
	} catch(int) {}
}

DLLEXPORT char *
mobius_get_parameter_string(Model_Data *data, Entity_Id par_id, Mobius_Index_Value *indexes, s64 indexes_count) {
	auto app = data->app;
	try {
		s64 offset = get_offset_by_index_values(app, &app->parameter_structure, par_id, indexes, indexes_count);
		auto par = app->model->parameters[par_id];
		if(par->decl_type == Decl_Type::par_datetime) {
			return (*data->parameters.get_value(offset)).val_datetime.to_string().data; // Oops, not thread safe.
		} else if (par->decl_type == Decl_Type::par_enum) {
			s64 intval = (*data->parameters.get_value(offset)).val_integer;
			return (char *)par->enum_values[intval].data();
		} else
			fatal_error(Mobius_Error::api_usage, "mobius_get_parameter_string can not be called on parameters of this type.");
	} catch(int) {}
	return (char *)"";
}

DLLEXPORT Mobius_Entity_Metadata
mobius_get_entity_metadata(Model_Data *data, Entity_Id id) {
	static char unit_buffer[128];
	
	auto app = data->app;
	Mobius_Entity_Metadata result = {};
	try {
		auto reg = app->model->find_entity(id);
		result.name = (char *)reg->name.data();
		if(id.reg_type == Reg_Type::parameter) {
			
			auto par = app->model->parameters[id];
			result.description = (char *)par->description.data();
			result.min = par->min_val;
			result.max = par->max_val;
			if(is_valid(par->unit)) {
				auto unit = app->model->units[par->unit]->data.to_utf8();
				strcpy(unit_buffer, unit.data());
			}
			result.unit = unit_buffer;   // TODO: Not thread safe or buffer safe.
		}
	} catch(int) {}
	return result;
}

DLLEXPORT Var_Id
mobius_get_flux(Model_Data *data, Entity_Id decl_id) {
	try {
		fatal_error(Mobius_Error::internal, "Unimplemented mobius_get_flux!");
	} catch(int) {}
	return invalid_var;
}

DLLEXPORT s64
mobius_entity_count(Model_Data *data, Entity_Id scope_id, Reg_Type type) {
	
	try {
		auto scope = data->app->model->get_scope(scope_id);
		return scope->by_type(type).size();
	} catch(int) {}
	return 0;
}

DLLEXPORT void
mobius_list_all_entities(Model_Data *data, Entity_Id scope_id, Reg_Type type, const char **idents_out, const char **names_out) {
	
	try {
		auto model = data->app->model;
		auto scope = model->get_scope(scope_id);
		auto iter = scope->by_type(type);
		int idx = 0;
		for(Entity_Id id : iter) {
			
			names_out[idx] = model->find_entity(id)->name.c_str();
			idents_out[idx] = (*scope)[id].c_str();
			
			++idx;
		}
	} catch(int) {}
}

DLLEXPORT s64
mobius_index_count(Model_Data *data, Entity_Id index_set_id) {
	
	try {
		auto app = data->app;
		auto set = app->model->index_sets[index_set_id];
		if(is_valid(set->sub_indexed_to))
			fatal_error(Mobius_Error::api_usage, "For now we don't support extracting the count of a sub-indexed index set.");
		
		return app->index_data.get_max_count(index_set_id).index;
	} catch(int) {}
	return -1;
}

DLLEXPORT void
mobius_index_names(Model_Data *data, Entity_Id index_set_id, Mobius_Index_Value *indexes_out) {
	try {
		auto app = data->app;
		auto set = app->model->index_sets[index_set_id];
		if(is_valid(set->sub_indexed_to))
			fatal_error(Mobius_Error::api_usage, "For now we don't support extracting the count of a sub-indexed index set.");
		
		Index_T count = app->index_data.get_max_count(index_set_id);
		auto type = app->index_data.get_index_type(index_set_id);
		if(type == Index_Record::Type::numeric1) {
			// Could we have a more robust way to signal this?
			for(s64 i = 0; i < count.index; ++i) {
				indexes_out[i].name = "";
				indexes_out[i].value = i;
			}
		} else if (type == Index_Record::Type::named) {
			for(Index_T index = Index_T { index_set_id, 0 }; index < count; ++index) {
				// Unsafe conversion, but we won't modify the contents in actual use case.
				indexes_out[index.index].name = (char *)app->index_data.unsafe_get_index_name_reference(index).c_str();
				indexes_out[index.index].value = index.index;
			}
		} else
			fatal_error(Mobius_Error::internal, "Unsupported index data type in mobius_index_names.");
		
	} catch(int) {}
}