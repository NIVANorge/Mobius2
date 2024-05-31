# Package to run Mobius2 models from the Julia language. Not yet fully featured, but contains enough to run a model and extract results.

module mobius

using Libdl

mobius_dll = dlopen("../mobipy/c_abi.dll")

export setup_model, run_model, get_entity, get_var_id_from_list, get_var_id, get_steps, get_series_data, invalid_entity_id, invalid_var, no_index

setup_model_h       = dlsym(mobius_dll, "mobius_build_from_model_and_data_file")
encountered_error_h = dlsym(mobius_dll, "mobius_encountered_error")
encounterer_log_h   = dlsym(mobius_dll, "mobius_encountered_log")
run_model_h         = dlsym(mobius_dll, "mobius_run_model")
get_entity_h        = dlsym(mobius_dll, "mobius_get_entity")
get_var_id_from_list_h = dlsym(mobius_dll, "mobius_get_var_id_from_list")
get_steps_h         = dlsym(mobius_dll, "mobius_get_steps")
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

struct Mobius_Index_Value
	name::Cstring
	value::Clonglong
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

function get_var_id_from_list(data::Ptr{Cvoid}, ids::Vector{Entity_Id})::Var_Id
	result = ccall(get_var_id_from_list_h, Var_Id, (Ptr{Cvoid}, Ptr{Entity_Id}, Clonglong),
		data, ids, length(ids))
	check_error()
	return result
end

function get_var_id(data::Ptr{Cvoid}, identifiers::Vector{String}, scope_id::Entity_Id = invalid_entity_id)::Var_Id
	ids = [get_entity(data, ident, scope_id) for ident in identifiers]
	return get_var_id_from_list(data, ids)
end

function get_steps(data::Ptr{Cvoid}, var_id::Var_Id)::Int64
	result = ccall(get_steps_h, Clonglong, (Ptr{Cvoid}, Cint),
		data, var_id.type)
	check_error()
	return result
end

# TODO: make it work with numerical indexes too
function get_series_data(data::Ptr{Cvoid}, var_id::Var_Id, indexes::Vector{String})::Vector{Float64}
	steps = get_steps(data, var_id)
	result = Vector{Cdouble}(undef, steps)
	
	idxs = Vector{Mobius_Index_Value}(undef, length(indexes))
	i = 1
	for idx in indexes
		idx_c = Base.unsafe_convert(Cstring, idx)
		idxs[i] = Mobius_Index_Value(idx_c, -1)
		i += 1
	end
	
	ccall(get_series_data_h, Cvoid, (Ptr{Cvoid}, Var_Id, Ptr{Mobius_Index_Value}, Clonglong, Ptr{Cdouble}, Clonglong),
		data, var_id, idxs, length(idxs), result, length(result))
	check_error()
	
	return result
end

function get_entity_by_name(data::Ptr{Cvoid}, name::String, scope_id::Entity_Id)
	result = ccall(deserialize_entity_h, Entity_Id, (Ptr{Cvoid}, Entity_Id, Cstring),
		data, scope_id, name)
	check_error()
	return result
end

function get_var_id_by_name(data::Ptr{Cvoid}, name::String)
	result = ccall(mobius_deserialize_var_h, Var_Id, (Ptr{Cvoid}, Cstring),
		data, name)
	check_error()
	return result
end


end # module