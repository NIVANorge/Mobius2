# Package to run Mobius2 models from the Julia language. Not yet fully featured, but contains enough to run a model and extract results.

module mobius

using Libdl, Dates

mobius_dll = dlopen("../mobipy/c_abi.dll")

export setup_model, run_model, get_entity, get_var_from_list, get_var, conc, transport, get_var_by_name, get_steps, get_dates, get_series_data, invalid_entity_id, invalid_var, no_index

setup_model_h       = dlsym(mobius_dll, "mobius_build_from_model_and_data_file")
copy_data_h         = dlsym(mobius_dll, "mobius_copy_data")
free_model_h        = dlsym(mobius_dll, "mobius_delete_application")
free_data_h         = dlsym(mobius_dll, "mobius_delete_data")
encountered_error_h = dlsym(mobius_dll, "mobius_encountered_error")
encounterer_log_h   = dlsym(mobius_dll, "mobius_encountered_log")
run_model_h         = dlsym(mobius_dll, "mobius_run_model")
get_entity_h        = dlsym(mobius_dll, "mobius_get_entity")
get_var_id_from_list_h = dlsym(mobius_dll, "mobius_get_var_id_from_list")
get_special_var_h   = dlsym(mobius_dll, "mobius_get_special_var")
get_steps_h         = dlsym(mobius_dll, "mobius_get_steps")
get_time_step_size_h = dlsym(mobius_dll, "mobius_get_time_step_size")
get_start_date_h    = dlsym(mobius_dll, "mobius_get_start_date")
get_series_data_h   = dlsym(mobius_dll, "mobius_get_series_data")
deserialize_entity_h = dlsym(mobius_dll, "mobius_deserialize_entity")
deserialize_var_h   = dlsym(mobius_dll, "mobius_deserialize_var")
get_value_type_h    = dlsym(mobius_dll, "mobius_get_value_type")
set_parameter_numeric_h = dlsym(mobius_dll, "mobius_set_parameter_numeric")
get_parameter_numeric_h = dlsym(mobius_dll, "mobius_get_parameter_numeric")
set_parameter_string_h = dlsym(mobius_dll, "mobius_set_parameter_string")
get_parameter_string_h = dlsym(mobius_dll, "mobius_get_parameter_string")

struct Model_Data
	ptr::Ptr{Cvoid}
	original::Bool
end

struct Entity_Id
	reg_type::Cshort
	id::Cshort
end

struct Var_Id
	type::Cint
	id::Cint
end

struct Time_Step_Size
	unit::Cint
	magnitude::Cint
end

struct Mobius_Index_Value
	name::Cstring
	value::Clonglong
end

struct Var_Ref
	data::Ptr{Cvoid}
	var_id::Var_Id
end

struct Entity_Ref
	data::Ptr{Cvoid}
	entity_id::Entity_Id
end

invalid_entity_id = Entity_Id(-1, -1)
invalid_var       = Var_Id(-1, -1)
no_index          = Mobius_Index_Value(C_NULL, 0)
invalid_entity_ref = Entity_Ref(C_NULL, invalid_entity_id)

function check_error()
	# First check log buffer
	buf = " "^512
	len = ccall(encounterer_log_h, Clonglong, (Cstring, Clonglong), buf, length(buf))
	while len > 0
		print(first(buf, len))
		buf = " "^512
		len = ccall(encounterer_log_h, Clonglong, (Cstring, Clonglong), buf, length(buf))
	end
	
	# Then check error buffer
	was_error = false
	message::String = ""
	buf = " "^512
	len = ccall(encountered_error_h, Clonglong, (Cstring, Clonglong), buf, length(buf))
	while len > 0
		was_error = true
		message = string(message, first(buf, len))
		buf = " "^512
		len = ccall(encountered_error_h, Clonglong, (Cstring, Clonglong), buf, length(buf))
	end
	if was_error
		throw(ErrorException(message))
	end
end

function setup_model(model_file::String, data_file::String, store_series::Bool = false, dev_mode::Bool = false)::Model_Data
	#mobius_path = string(dirname(dirname(Base.source_path())), "\\") # Doesn't work in IJulia
	mobius_path = string(dirname(dirname(@__FILE__)), "\\")
	result =  ccall(setup_model_h, Ptr{Cvoid}, (Cstring, Cstring, Cstring, Cint, Cint), 
		model_file, data_file, mobius_path, store_series, dev_mode)
	check_error()
	return Model_Data(result, true)
end

function copy_data(data::Model_Data, copy_results::Bool=false)::Model_Data
	result = ccall(copy_data_h, Ptr{Cvoid}, (Ptr{Cvoid}, Cint),
		data.ptr, copy_results)
	return Model_Data(result, false)
end

# TODO: As in the python wrapper, it would be nice if we could invalidate all other references to the ptr after the free..
function free(data::Model_Data)
	if data.original
		ccall(free_model_h, Cvoid, (Ptr{Cvoid},), data.ptr)
	else
		ccall(free_data_h, Cvoid, (Ptr{Cvoid},), data.ptr)
	end
end

finalize!(data::Model_Data) = free(data)

function run_model(data::Model_Data, ms_timeout::Int64=-1)::Bool
	result = ccall(run_model_h, Cint, (Ptr{Cvoid}, Clonglong),
		data.ptr, ms_timeout)
	check_error()
	return result
end

function get_entity(data::Model_Data, identifier::String, scope_id::Entity_Ref = invalid_entity_ref)::Entity_Ref
	result = ccall(get_entity_h, Entity_Id, (Ptr{Cvoid}, Entity_Id, Cstring),
		data.ptr, scope_id.entity_id, identifier)
	check_error()
	return Entity_Ref(data.ptr, result)
end

function get_var_from_list(data::Model_Data, ids::Vector{Entity_Id})::Var_Ref
	result = ccall(get_var_id_from_list_h, Var_Id, (Ptr{Cvoid}, Ptr{Entity_Id}, Clonglong),
		data.ptr, ids, length(ids))
	check_error()
	return Var_Ref(data.ptr, result)
end

function get_var(data::Model_Data, identifiers::Vector{String}, scope_id::Entity_Ref = invalid_entity_ref)::Var_Ref
	ids = [get_entity(data, ident, scope_id).entity_id for ident in identifiers]
	result = get_var_from_list(data, ids)
	check_error()
	
	return result
end

function conc(var_ref::Var_Ref)::Var_Ref
	result = ccall(get_special_var_h, Var_Id, (Ptr{Cvoid}, Var_Id, Entity_Id, Cshort),
		var_ref.data, var_ref.var_id, invalid_entity_id, 5)
	check_error()
	return Var_Ref(var_ref.data, result)
end

function transport(var_ref::Var_Ref, q::String)::Var_Ref
	q_id = ccall(get_entity_h, Entity_Id, (Ptr{Cvoid}, Entity_Id, Cstring),
		var_ref.data, invalid_entity_id, q)
	
	result = ccall(get_special_var_h, Var_Id, (Ptr{Cvoid}, Var_Id, Entity_Id, Cshort),
		var_ref.data, var_ref.var_id, q_id, 4)
	check_error()
	return Var_Ref(var_ref.data, result)
end

function get_steps(var_ref::Var_Ref)::Int64
	result = ccall(get_steps_h, Clonglong, (Ptr{Cvoid}, Cint),
		var_ref.data, var_ref.var_id.type)
		
	check_error()
	return result
end

function copy_str(str::Cstring)::String
	# This is super weird, there should be an inbuilt function for converting Cstring->String, but I can't find it.
	len = @ccall strlen(str::Cstring)::Csize_t
	result = " "^len
	@ccall memcpy(Base.unsafe_convert(Cstring, result)::Cstring, str::Cstring, len::Csize_t)::Ptr{Cvoid}
	return result
end

function get_dates(var_ref::Var_Ref)::Vector{DateTime}
	steps = get_steps(var_ref)
	start_d = ccall(get_start_date_h, Cstring, (Ptr{Cvoid}, Cint),
		var_ref.data, var_ref.var_id.type)
	start_d_str = copy_str(start_d)
	#TODO: We have to detect if the string contains timestamp or not
	start_date = DateTime(Date(start_d_str))
	
	step_size = ccall(get_time_step_size_h, Time_Step_Size, (Ptr{Cvoid},),
		var_ref.data)
	check_error()
	
	mag = step_size.magnitude
	if step_size.unit == 0 # seconds
		return start_date .+ Second.(0:mag:((steps-1)*mag))
	end
	throw(ErrorException("Not yet implemented for monthly time steps."))
	
	return []
end

# TODO: make it work with numerical indexes too
function make_indexes(indexes::Vector{String})::Vector{Mobius_Index_Value}
	# NOTE: The unsafe_convert is safe since the value only needs to stay in memory until the end of this
	# function call
	return [Mobius_Index_Value(Base.unsafe_convert(Cstring, idx), -1) for idx in indexes]
end

function get_series_data(var_ref::Var_Ref, indexes::Vector{String})::Vector{Float64}
	steps = get_steps(var_ref)
	result = Vector{Cdouble}(undef, steps)
	
	idxs = make_indexes(indexes)
	
	ccall(get_series_data_h, Cvoid, (Ptr{Cvoid}, Var_Id, Ptr{Mobius_Index_Value}, Clonglong, Ptr{Cdouble}, Clonglong),
		var_ref.data, var_ref.var_id, idxs, length(idxs), result, length(result))
	check_error()
	
	return result
end

Base.getindex(var_ref::Var_Ref, indexes::Vector{String})::Vector{Float64} = get_series_data(var_ref, indexes)
Base.getindex(var_ref::Var_Ref, index::String)::Vector{Float64} = get_series_data(var_ref, [index])

#TODO: set_series_data, get_series_data_slice, etc.

function get_entity_by_name(data::Model_Data, name::String, scope_id::Entity_Ref=invalid_entity_ref)::Entity_Ref
	result = ccall(deserialize_entity_h, Entity_Id, (Ptr{Cvoid}, Entity_Id, Cstring),
		data.ptr, scope_id.entity_id, name)
	check_error()
	return Entity_Ref(data.ptr, result)
end

function get_var_by_name(data::Model_Data, name::String)::Var_Ref
	result = ccall(deserialize_var_h, Var_Id, (Ptr{Cvoid}, Cstring),
		data.ptr, name)
	check_error()
	return Var_Ref(data.ptr, result)
end


#DLLEXPORT s64
#mobius_get_value_type(Model_Data *data, Entity_Id id);
#DLLEXPORT void
#mobius_set_parameter_numeric(Model_Data *data, Entity_Id par_id, Mobius_Index_Value *indexes, s64 indexes_count, Parameter_Value_Simple value);

#DLLEXPORT Parameter_Value_Simple
#mobius_get_parameter_numeric(Model_Data *data, Entity_Id par_id, Mobius_Index_Value *indexes, s64 indexes_count);

#DLLEXPORT void
#mobius_set_parameter_string(Model_Data *data, Entity_Id par_id, Mobius_Index_Value *indexes, s64 indexes_count, char *value);

#DLLEXPORT char *
#mobius_get_parameter_string(Model_Data *data, Entity_Id par_id, Mobius_Index_Value *indexes, s64 indexes_count);

#TODO: Maybe store the type in the Entity_Ref from begin with (when it is a parameter, not some other object)
function get_type(ref::Entity_Ref)
	type = ccall(get_value_type_h, Clonglong, (Ptr{Cvoid}, Entity_Id),
		ref.data, ref.entity_id)
	check_error()
	return type
end

function set_parameter(ref::Entity_Ref, indexes::Vector{String}, value::Float64)
	type = get_type(ref)
	if type != 0
		throw(ErrorException("Tried to set a non-float parameter with float value."))
	end
	idxs = make_indexes(indexes)
	ccall(set_parameter_numeric_h, Cvoid, (Ptr{Cvoid}, Entity_Id, Ptr{Mobius_Index_Value}, Clonglong, Float64),
		ref.data, ref.entity_id, idxs, length(idxs), value)
	check_error()
end

function set_parameter(ref::Entity_Ref, indexes::Vector{String}, value::String)
	type = get_type(ref)
	if type != 3 && type != 4
		throw(ErrorException("Tried to set a parameter that is not datetime or enum with a string value."))
	end
	idxs = make_indexes(indexes)
	ccall(set_parameter_string_h, Cvoid, (Ptr{Cvoid}, Entity_Id, Ptr{Mobius_Index_Value}, Clonglong, Cstring),
		ref.data, ref.entity_id, idxs, length(idxs), Base.unsafe_convert(Cstring, value))
	check_error()
end

#TODO: Other types also, as well as get value

Base.setindex!(ref::Entity_Ref, indexes::Vector{String}, value::Float64) = set_parameter(ref, indexes, value)
Base.setindex!(ref::Entity_Ref, indexes::Vector{String}, value::String) = set_parameter(ref, indexes, value)
Base.setindex!(ref::Entity_Ref, index::String, value::Float64) = set_parameter(ref, [index], value)
Base.setindex!(ref::Entity_Ref, index::String, value::String) = set_parameter(ref, [index], value)
Base.setindex!(ref::Entity_Ref, value::Float64) = set_parameter(ref, String[], value)
Base.setindex!(ref::Entity_Ref, value::String) = set_parameter(ref, String[], value)


end # module