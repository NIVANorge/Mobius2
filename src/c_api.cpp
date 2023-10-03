
#include "c_api.h"
#include "model_application.h"



#ifndef MOBIUS_ERROR_STREAMS
	static_assert(false, "The C API must be compiled with MOBIUS_ERROR_STREAMS defined in the compile script using -DMOBIUS_ERROR_STREAMS.");
#endif

// NOTE: This is just some preliminary testing of the C api. Will build it out properly later.

std::stringstream global_error_stream;
std::stringstream global_log_stream;

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

DLLEXPORT Model_Application *
mobius_build_from_model_and_data_file(char * model_file, char * data_file) {
	
	try {
		Mobius_Model *model = load_model(model_file);
		auto app = new Model_Application(model);
			
		Data_Set *data_set = new Data_Set;
		data_set->read_from_file(data_file);
		
		app->build_from_data_set(data_set);
		app->compile();
		
		return app;
	} catch(int) {}
	
	return nullptr;
}

// TODO: We should probably have most of these work with Model_Data instances instead of Model_Application instances.

DLLEXPORT bool
mobius_run_model(Model_Application *app, s64 ms_timeout) {
	try {
		return run_model(app, ms_timeout);
	} catch(int) {}
}

DLLEXPORT s64
mobius_get_steps(Model_Application *app, Var_Id::Type type) {
	auto &storage = app->data.get_storage(type);
	if(!storage.structure->has_been_set_up)
		return 0;
	return storage.time_steps;
}

DLLEXPORT Time_Step_Size
mobius_get_time_step_size(Model_Application *app) {
	return app->time_step_size;
}

DLLEXPORT char *
mobius_get_start_date(Model_Application *app, Var_Id::Type type) {
	// NOTE: The data for this one gets overwritten when you call it again. Not thread safe
	return app->data.get_storage(type).start_date.to_string().data;
}


template<typename Handle_T> s64
get_offset_by_index_values(Model_Application *app, Storage_Structure<Handle_T> *storage, Handle_T handle, Mobius_Index_Value *index_values, s64 indexes_count) {
	
	const std::vector<Entity_Id> &index_sets = storage->get_index_sets(handle);
	if(index_sets.size() != indexes_count) {
		begin_error(Mobius_Error::api_usage);
		error_print("The object requires ", index_sets.size(), " index", index_sets.size()>1?"es":"", ", got ", indexes_count, ". The following index sets need to be addressed (in that order):\n");
		for(auto index_set : index_sets)
			error_print("\"", app->model->index_sets[index_set]->name, "\" ");
		mobius_error_exit();
	}
	
	//TODO: It is a bit annoying to use the Token api for this, moreover errors will not be reported correctly
	
	std::vector<Token> idx_names(indexes_count);
	
	for(s64 idxidx = 0; idxidx < indexes_count; ++idxidx) {
		Token &token = idx_names[idxidx];
		auto &idx_val = index_values[idxidx];
		if(idx_val.value >= 0) {
			token.type = Token_Type::integer;
			token.val_int = idx_val.value;
		} else {
			token.type = Token_Type::quoted_string;
			token.string_value = idx_val.name;
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

DLLEXPORT Entity_Id
mobius_deserialize_entity(Model_Application *app, Entity_Id scope_id, char *serial_name) {
	try {
		auto scope = app->model->get_scope(scope_id);
		return scope->deserialize(serial_name, Reg_Type::unrecognized);
	} catch(int) {}
	return invalid_entity_id;
}

DLLEXPORT Entity_Id
mobius_get_entity(Model_Application *app, Entity_Id scope_id, char *handle_name) {
	try {
		auto scope = app->model->get_scope(scope_id);
		auto reg = (*scope)[handle_name];
		if(!reg)
			return invalid_entity_id;
		return reg->id;
	} catch(int) {}
	return invalid_entity_id;
}

DLLEXPORT Var_Id
mobius_deserialize_var(Model_Application *app, char *serial_name) {
	return app->deserialize(serial_name);
}

DLLEXPORT Var_Id
mobius_get_var_id_from_list(Model_Application *app, Entity_Id *ids, s64 id_count) {
	// TODO: Check that all the ids refer to components.
	try {
		Var_Location loc;
		loc.type = Var_Location::Type::located;
		loc.n_components = id_count;
		// TODO: Check that id_count does not exceed max_var_loc_components ?
		for(int idx = 0; idx < id_count; ++idx)
			loc.components[idx] = ids[idx];
		
		return app->vars.id_of(loc);
		
	} catch(int) {}
	return invalid_var;
}

DLLEXPORT Var_Id
mobius_get_special_var(Model_Application *app, Var_Id parent1, Var_Id parent2, State_Var::Type type) {
	try {
		if(type == State_Var::Type::dissolved_conc)
			return app->vars.find_conc(parent1);
		else
			fatal_error(Mobius_Error::internal, "Unimplemented special type");
	} catch(int) {}
	return invalid_var;
}

DLLEXPORT Mobius_Series_Metadata
mobius_get_series_metadata(Model_Application *app, Var_Id var_id) {
	Mobius_Series_Metadata result = {};
	try {
		auto var = app->vars[var_id];
		result.name = (char *)var->name.data();
		//result.unit = var->unit.to_utf8()    //TODO: Won't work, have to store it in some buffer.
	} catch(int) {}
	return result;
}


DLLEXPORT s64
mobius_get_index_set_count(Model_Application *app, Entity_Id id) {
	try {
		if(id.reg_type == Reg_Type::parameter)
			return app->data.parameters.structure->get_index_sets(id).size();
	} catch(int) {}
	return 0;
}

DLLEXPORT void
mobius_get_series_data(Model_Application *app, Var_Id var_id, Mobius_Index_Value *indexes, s64 indexes_count, double *series_out, s64 time_steps_out) {
	if(!time_steps_out) return;
	
	try {
		auto &storage = app->data.get_storage(var_id.type);
		s64 offset = get_offset_by_index_values(app, storage.structure, var_id, indexes, indexes_count);

		for(s64 step = 0; step < time_steps_out; ++step)
			series_out[step] = *storage.get_value(offset, step);
		
	} catch(int) {}
}

// TODO: We could just have a get_decl_type eventually
DLLEXPORT s64
mobius_get_value_type(Model_Application *app, Entity_Id id) {
	try {
		if(id.reg_type == Reg_Type::parameter) {
			Decl_Type type = app->model->parameters[id]->decl_type;
			return (s64)type - (s64)Decl_Type::par_real;
		}
	} catch(int) {}
	return -1;
}

// TODO: For some parameters we need to check if they are baked, and then set a flag on the Model_Application telling it it has to be recompiled before further use.
//   This must then be reflected in mobipy so that it actually does the recompilation.

DLLEXPORT void
mobius_set_parameter_numeric(Model_Application *app, Entity_Id par_id, Mobius_Index_Value *indexes, s64 indexes_count, Parameter_Value value) {
	try {
		s64 offset = get_offset_by_index_values(app, &app->parameter_structure, par_id, indexes, indexes_count);
		*app->data.parameters.get_value(offset) = value;
	} catch(int) {}
}

DLLEXPORT Parameter_Value
mobius_get_parameter_numeric(Model_Application *app, Entity_Id par_id, Mobius_Index_Value *indexes, s64 indexes_count) {
	try {
		s64 offset = get_offset_by_index_values(app, &app->parameter_structure, par_id, indexes, indexes_count);
		return *app->data.parameters.get_value(offset);
	} catch(int) {}
	return Parameter_Value();
}

DLLEXPORT void
mobius_set_parameter_string(Model_Application *app, Entity_Id par_id, Mobius_Index_Value *indexes, s64 indexes_count, char *value) {
	// TODO: All the others also need to check on types.
	try {
		s64 offset = get_offset_by_index_values(app, &app->parameter_structure, par_id, indexes, indexes_count);
		Parameter_Value val;
		auto par = app->model->parameters[par_id];
		if(par->decl_type == Decl_Type::par_datetime) {
			Token_Stream stream("app", value);
			stream.allow_date_time_tokens = true;
			val.val_datetime = stream.expect_datetime();
		} else if (par->decl_type == Decl_Type::par_enum) {
			s64 intval = enum_int_value(par, value);
			val.val_integer = intval;
		} else
			fatal_error(Mobius_Error::api_usage, "mobius_set_parameter_string can not be called on parameters of this type.");
		*app->data.parameters.get_value(offset) = val;
	} catch(int) {}
}

DLLEXPORT char *
mobius_get_parameter_string(Model_Application *app, Entity_Id par_id, Mobius_Index_Value *indexes, s64 indexes_count) {
	try {
		s64 offset = get_offset_by_index_values(app, &app->parameter_structure, par_id, indexes, indexes_count);
		auto par = app->model->parameters[par_id];
		if(par->decl_type == Decl_Type::par_datetime) {
			return (*app->data.parameters.get_value(offset)).val_datetime.to_string().data; // Oops, not thread safe.
		} else if (par->decl_type == Decl_Type::par_enum) {
			s64 intval = (*app->data.parameters.get_value(offset)).val_integer;
			return (char *)par->enum_values[intval].data();
		} else
			fatal_error(Mobius_Error::api_usage, "mobius_get_parameter_string can not be called on parameters of this type.");
	} catch(int) {}
	return (char *)"";
}

DLLEXPORT Mobius_Entity_Metadata
mobius_get_entity_metadata(Model_Application *app, Entity_Id id) {
	Mobius_Entity_Metadata result = {};
	try {
		auto *reg = (*app->model->registry(id.reg_type))[id];
		result.name = (char *)reg->name.data();
		if(id.reg_type == Reg_Type::parameter) {
			// TODO: Unit;
			auto par = app->model->parameters[id];
			result.description = (char *)par->description.data();
			result.min = par->min_val;
			result.max = par->max_val;
		}
	} catch(int) {}
	return result;
}
