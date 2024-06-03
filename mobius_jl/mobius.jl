# Package to run Mobius2 models from the Julia language. Not yet fully featured, but contains enough to run a model and extract results.

module mobius

using Libdl, Dates

mobius_dll = dlopen("../mobipy/c_abi.dll")

export setup_model, run_model, get_entity, get_var_from_list, get_var, get_var_by_name, get_steps, get_dates, get_series_data, invalid_entity_id, invalid_var, no_index

setup_model_h       = dlsym(mobius_dll, "mobius_build_from_model_and_data_file")
encountered_error_h = dlsym(mobius_dll, "mobius_encountered_error")
encounterer_log_h   = dlsym(mobius_dll, "mobius_encountered_log")
run_model_h         = dlsym(mobius_dll, "mobius_run_model")
get_entity_h        = dlsym(mobius_dll, "mobius_get_entity")
get_var_id_from_list_h = dlsym(mobius_dll, "mobius_get_var_id_from_list")
get_steps_h         = dlsym(mobius_dll, "mobius_get_steps")
get_time_step_size_h = dlsym(mobius_dll, "mobius_get_time_step_size")
get_start_date_h    = dlsym(mobius_dll, "mobius_get_start_date")
get_series_data_h   = dlsym(mobius_dll, "mobius_get_series_data")
deserialize_entity_h = dlsym(mobius_dll, "mobius_deserialize_entity")
deserialize_var_h   = dlsym(mobius_dll, "mobius_deserialize_var")

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

invalid_entity_id = Entity_Id(-1, -1)
invalid_var       = Var_Id(-1, -1)
no_index          = Mobius_Index_Value(C_NULL, 0)

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

function setup_model(model_file::String, data_file::String, store_series::Bool = false, dev_mode::Bool = false)::Ptr{Cvoid}
    #mobius_path = string(dirname(dirname(Base.source_path())), "\\")
	mobius_path = string(dirname(dirname(@__FILE__)), "\\")
	result =  ccall(setup_model_h, Ptr{Cvoid}, (Cstring, Cstring, Cstring, Cint, Cint), 
		model_file, data_file, mobius_path, store_series, dev_mode)
	check_error()
	return result
end

function run_model(data::Ptr{Cvoid}, ms_timeout::Int64=-1)
	result = ccall(run_model_h, Cint, (Ptr{Cvoid}, Clonglong),
		data, ms_timeout)
	check_error()
	return result
end

function get_entity(data::Ptr{Cvoid}, identifier::String, scope_id::Entity_Id = invalid_entity_id)::Entity_Id
	result = ccall(get_entity_h, Entity_Id, (Ptr{Cvoid}, Entity_Id, Cstring),
		data, scope_id, identifier)
	check_error()
	return result
end

function get_var_from_list(data::Ptr{Cvoid}, ids::Vector{Entity_Id})::Var_Ref
	result = ccall(get_var_id_from_list_h, Var_Id, (Ptr{Cvoid}, Ptr{Entity_Id}, Clonglong),
		data, ids, length(ids))
	check_error()
	return Var_Ref(data, result)
end

function get_var(data::Ptr{Cvoid}, identifiers::Vector{String}, scope_id::Entity_Id = invalid_entity_id)::Var_Ref
	ids = [get_entity(data, ident, scope_id) for ident in identifiers]
	result = get_var_from_list(data, ids)
	check_error()
	
	return result
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
function get_series_data(var_ref::Var_Ref, indexes::Vector{String})::Vector{Float64}
	steps = get_steps(var_ref)
	result = Vector{Cdouble}(undef, steps)
	
	# NOTE: The unsafe_convert is safe since the value only needs to stay in memory until the end of this
	# function call.
	idxs = [Mobius_Index_Value(Base.unsafe_convert(Cstring, idx), -1) for idx in indexes]
	
	ccall(get_series_data_h, Cvoid, (Ptr{Cvoid}, Var_Id, Ptr{Mobius_Index_Value}, Clonglong, Ptr{Cdouble}, Clonglong),
		var_ref.data, var_ref.var_id, idxs, length(idxs), result, length(result))
	check_error()
	
	return result
end

Base.getindex(var_ref::Var_Ref, indexes::Vector{String})::Vector{Float64} = get_series_data(var_ref, indexes)
Base.getindex(var_ref::Var_Ref, index::String)::Vector{Float64} = get_series_data(var_ref, [index])

function get_entity_by_name(data::Ptr{Cvoid}, name::String, scope_id::Entity_Id=invalid_entity_id)::Entity_Id
	result = ccall(deserialize_entity_h, Entity_Id, (Ptr{Cvoid}, Entity_Id, Cstring),
		data, scope_id, name)
	check_error()
	return result
end

function get_var_by_name(data::Ptr{Cvoid}, name::String)::Var_Ref
	result = ccall(deserialize_var_h, Var_Id, (Ptr{Cvoid}, Cstring),
		data, name)
	check_error()
	return Var_Ref(data, result)
end




end # module