# Package to run Mobius2 models from the Julia language. Not yet fully featured, but contains enough to run a model and extract results.

module mobius

using Libdl, Dates

dll_path = @static Sys.iswindows() ? "../mobipy/c_abi.dll" : "../mobipy/c_abi.so"
mobius_dll = dlopen(dll_path)

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
set_parameter_int_h = dlsym(mobius_dll, "mobius_set_parameter_int")
set_parameter_real_h = dlsym(mobius_dll, "mobius_set_parameter_real")
get_parameter_numeric_h = dlsym(mobius_dll, "mobius_get_parameter_numeric")
set_parameter_string_h = dlsym(mobius_dll, "mobius_set_parameter_string")
get_parameter_string_h = dlsym(mobius_dll, "mobius_get_parameter_string")
resolve_slice_h        = dlsym(mobius_dll, "mobius_resolve_slice")
get_series_data_slice_h = dlsym(mobius_dll, "mobius_get_series_data_slice")

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

struct Mobius_Index_Slice
	name::Cstring
	is_slice::Cint
	first::Clonglong
	last::Clonglong
end

struct Mobius_Index_Range
	first::Clonglong
	last::Clonglong
end

struct Var_Ref
	data::Ptr{Cvoid}
	var_id::Var_Id
end

struct Entity_Ref
	data::Ptr{Cvoid}
	entity_id::Entity_Id
end

struct Mobius_Base_Config
	store_transport_fluxes::Cint
	store_all_series::Cint
	developer_mode::Cint
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

function setup_model(model_file::String, data_file::String, ; store_transport_fluxes::Bool = false, store_all_series::Bool = false, dev_mode::Bool = false)::Model_Data
	#mobius_path = string(dirname(dirname(Base.source_path())), "\\") # Doesn't work in IJulia
	mobius_path = string(dirname(dirname(@__FILE__)), Base.Filesystem.path_separator)
	
	cfg = Mobius_Base_Config(store_transport_fluxes, store_all_series, dev_mode)
	cfgptr = Ref(cfg)
	
	result =  ccall(setup_model_h, Ptr{Cvoid}, (Cstring, Cstring, Cstring, Ptr{Mobius_Base_Config}), 
		model_file, data_file, mobius_path, cfgptr)
	check_error()
	return Model_Data(result, true)
end

function copy_data(data::Model_Data, copy_results::Bool=true, copy_inputs::Bool=false)::Model_Data
	result = ccall(copy_data_h, Ptr{Cvoid}, (Ptr{Cvoid}, Cint, Cint),
		data.ptr, copy_results, copy_inputs)
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

# TODO: Could allow callback here too..
function run_model(data::Model_Data, ms_timeout::Int64=-1)::Bool
	result = ccall(run_model_h, Cint, (Ptr{Cvoid}, Clonglong, Ptr{Cvoid}),
		data.ptr, ms_timeout, C_NULL)
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
	# It is a bit hacky that we hijack the internal memory of the String and write to it, but it works I guess.
	len = @ccall strlen(str::Cstring)::Csize_t
	result = " "^len
	@ccall memcpy(Base.unsafe_convert(Cstring, result)::Cstring, str::Cstring, len::Csize_t)::Ptr{Cvoid}
	return result
end

function get_dates(var_ref::Var_Ref)::Vector{DateTime}
	steps = get_steps(var_ref)
	start_d = " "^32
	ccall(get_start_date_h, Cstring, (Ptr{Cvoid}, Cint, Cstring),
		var_ref.data, var_ref.var_id.type, start_d)
		
	#TODO: We have to detect if the string contains timestamp or not
	
	start_d_str = first(start_d, 10)
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

function make_index(index::Any)::Mobius_Index_Value
	if typeof(index) == String
		# NOTE: The unsafe_convert is actually in this case safe.
		# The value only needs to stay in memory until the end of the
		# scope that calls make_indexes, and the String will have been
		# constructed in a higher scope than that. Just don't export
		# make_indexes for use outside this module, and it will be fine.
		return Mobius_Index_Value(Base.unsafe_convert(Cstring, index), -1)
	elseif isinteger(index) 
		return Mobius_Index_Value(C_NULL, index)
	end
	throw(ErrorException("Invalid index type, expected string or int"))
end

function make_indexes(indexes::Vector{Any})::Vector{Mobius_Index_Value}
	return [make_index(idx) for idx in indexes]
end

function has_slice(indexes::Vector{Any})::Bool
	for index in indexes
		if typeof(index) == UnitRange{Int64}
			return true
		end
	end
	return false
end

function make_slice(index::Any)::Mobius_Index_Slice
	if typeof(index) == String
		return Mobius_Index_Slice(Base.unsafe_convert(Cstring, index), false, 0, 0)
	elseif typeof(index) == UnitRange{Int64}
		# TODO: We should test that the slice has unit stride...
		return Mobius_Index_Slice(C_NULL, true, index[1], index[length(index)])
	elseif isinteger(index)
		return Mobius_Index_Slice(C_NULL, false, index, 0)
	end
	throw(ErrorException("Invalid index type, expected string or int"))
end

function make_slices(indexes::Vector{Any})::Vector{Mobius_Index_Slice}
	return [make_slice(idx) for idx in indexes]
end

function get_series_data(var_ref::Var_Ref, indexes::Vector{Any}) #::Vector{Float64}
	steps = get_steps(var_ref)
	
	if has_slice(indexes)
		
		slices = make_slices(indexes)
		ranges = Vector{Mobius_Index_Range}(undef, length(indexes))
		ccall(resolve_slice_h, Cvoid, (Ptr{Cvoid}, Var_Id, Ptr{Mobius_Index_Slice}, Clonglong, Ptr{Mobius_Index_Range}),
			var_ref.data, var_ref.var_id, slices, length(slices), ranges)
		check_error()
		
		dim = steps
		idx_dim = 1
		dims = Vector{Int64}()
		#append!(dims, steps)
		for rn in ranges
			len = rn.last - rn.first
			dim *= len
			idx_dim *= len
			if len > 1
				append!(dims, len)
			end
		end
		idx_dim += 1 # We are getting boundary positions of indexes
		append!(dims, steps)
		
		series = Vector{Cdouble}(undef, dim)
		idx_pos = Vector{Cdouble}(undef, idx_dim)
		
		ccall(get_series_data_slice_h, Cvoid, (Ptr{Cvoid}, Var_Id, Ptr{Mobius_Index_Range}, Clonglong, Ptr{Cdouble}, Ptr{Cdouble}, Clonglong),
			var_ref.data, var_ref.var_id, ranges, length(ranges), idx_pos, series, steps)
		check_error()
		
		#return series
		
		out_series = reshape(series, tuple((a for a in dims)...) )
		return (out_series, idx_pos::Vector{Float64})
	else
		
		result = Vector{Cdouble}(undef, steps)
		
		idxs = make_indexes(indexes)
		
		ccall(get_series_data_h, Cvoid, (Ptr{Cvoid}, Var_Id, Ptr{Mobius_Index_Value}, Clonglong, Ptr{Cdouble}, Clonglong),
			var_ref.data, var_ref.var_id, idxs, length(idxs), result, length(result))
		check_error()
		
		return result::Vector{Float64}
	end
end

Base.getindex(var_ref::Var_Ref, indexes::Vector{Any}) = get_series_data(var_ref, indexes)
Base.getindex(var_ref::Var_Ref, indexes::Any...) = get_series_data(var_ref, Any[indexes...])
Base.getindex(var_ref::Var_Ref, index::Any) = get_series_data(var_ref, Any[index])
Base.getindex(var_ref::Var_Ref) = get_series_data(var_ref, Any[])

#TODO: set_series_data, etc.

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

#TODO: Maybe store the type in the Entity_Ref from begin with (when it is a parameter, not some other object)
function get_type(ref::Entity_Ref)
	type = ccall(get_value_type_h, Clonglong, (Ptr{Cvoid}, Entity_Id),
		ref.data, ref.entity_id)
	check_error()
	return type
end

function set_parameter(ref::Entity_Ref, indexes::Vector{Any}, value::Float64)
	type = get_type(ref)
	if type != 0
		throw(ErrorException("Tried to set a non-float parameter with float value."))
	end
	idxs = make_indexes(indexes)
	ccall(set_parameter_real_h, Cvoid, (Ptr{Cvoid}, Entity_Id, Ptr{Mobius_Index_Value}, Clonglong, Cdouble),
		ref.data, ref.entity_id, idxs, length(idxs), value)
	check_error()
end

function set_parameter(ref::Entity_Ref, indexes::Vector{Any}, value::Int64)
	type = get_type(ref)
	if type != 0 && type != 1
		throw(ErrorException("Tried to set a non-numerical parameter with int value."))
	end
	idxs = make_indexes(indexes)
	if type == 0
		ccall(set_parameter_real_h, Cvoid, (Ptr{Cvoid}, Entity_Id, Ptr{Mobius_Index_Value}, Clonglong, Cdouble),
			ref.data, ref.entity_id, idxs, length(idxs), convert(Float64, value))
	else
		ccall(set_parameter_int_h, Cvoid, (Ptr{Cvoid}, Entity_Id, Ptr{Mobius_Index_Value}, Clonglong, Clonglong),
			ref.data, ref.entity_id, idxs, length(idxs), value)
	end
	check_error()
end

function set_parameter(ref::Entity_Ref, indexes::Vector{Any}, value::Bool)
	type = get_type(ref)
	if type != 2
		throw(ErrorException("Tried to set a non-bool parameter with bool value."))
	end
	idxs = make_indexes(indexes)
	ccall(set_parameter_int_h, Cvoid, (Ptr{Cvoid}, Entity_Id, Ptr{Mobius_Index_Value}, Clonglong, Clonglong),
		ref.data, ref.entity_id, idxs, length(idxs), value)
	check_error()
end

function set_parameter(ref::Entity_Ref, indexes::Vector{Any}, value::String)
	type = get_type(ref)
	if type != 3 && type != 4
		throw(ErrorException("Tried to set a parameter that is not datetime or enum with a string value."))
	end
	idxs = make_indexes(indexes)
	ccall(set_parameter_string_h, Cvoid, (Ptr{Cvoid}, Entity_Id, Ptr{Mobius_Index_Value}, Clonglong, Cstring),
		ref.data, ref.entity_id, idxs, length(idxs), Base.unsafe_convert(Cstring, value))
	check_error()
end

function set_parameter(ref::Entity_Ref, indexes::Vector{Any}, value::DateTime)
	type = get_type(ref)
	if type != 4
		throw(ErrorException("Tried to set a non-datetime parameter with a datetime value."))
	end
	datestr = Dates.format(value, "yyyy-mm-dd HH:MM:SS")
	idxs = make_indexes(indexes)
	ccall(set_parameter_string_h, Cvoid, (Ptr{Cvoid}, Entity_Id, Ptr{Mobius_Index_Value}, Clonglong, Cstring),
		ref.data, ref.entity_id, idxs, length(idxs), Base.unsafe_convert(Cstring, datestr))
	check_error()
end

Base.setindex!(ref::Entity_Ref, indexes::Vector{Any}, value::Float64) = set_parameter(ref, indexes, value)
Base.setindex!(ref::Entity_Ref, indexes::Vector{Any}, value::String) = set_parameter(ref, indexes, value)
Base.setindex!(ref::Entity_Ref, indexes::Vector{Any}, value::Int64) = set_parameter(ref, indexes, value)
Base.setindex!(ref::Entity_Ref, indexes::Vector{Any}, value::Bool) = set_parameter(ref, indexes, value)
Base.setindex!(ref::Entity_Ref, indexes::Vector{Any}, value::DateTime) = set_parameter(ref, indexes, value)
Base.setindex!(ref::Entity_Ref, index::Any, value::Float64) = set_parameter(ref, Any[index], value)
Base.setindex!(ref::Entity_Ref, index::Any, value::String) = set_parameter(ref, Any[index], value)
Base.setindex!(ref::Entity_Ref, index::Any, value::Int64) = set_parameter(ref, Any[index], value)
Base.setindex!(ref::Entity_Ref, index::Any, value::Bool) = set_parameter(ref, Any[index], value)
Base.setindex!(ref::Entity_Ref, index::Any, value::DateTime) = set_parameter(ref, Any[index], value)
Base.setindex!(ref::Entity_Ref, value::Float64) = set_parameter(ref, Any[], value)
Base.setindex!(ref::Entity_Ref, value::String) = set_parameter(ref, Any[], value)
Base.setindex!(ref::Entity_Ref, value::Int64) = set_parameter(ref, Any[], value)
Base.setindex!(ref::Entity_Ref, value::Bool) = set_parameter(ref, Any[], value)
Base.setindex!(ref::Entity_Ref, value::DateTime) = set_parameter(ref, Any[], value)

function get_parameter(ref::Entity_Ref, indexes::Vector{Any})::Any
	type = get_type(ref)
	idxs = make_indexes(indexes)
	result::Any = undef
	if type == 0
		result = ccall(get_parameter_numeric_h, Cdouble, (Ptr{Cvoid}, Entity_Id, Ptr{Mobius_Index_Value}, Clonglong),
			ref.data, ref.entity_id, idxs, length(idxs))::Float64
	elseif type == 1
		result = ccall(get_parameter_numeric_h, Clonglong, (Ptr{Cvoid}, Entity_Id, Ptr{Mobius_Index_Value}, Clonglong),
			ref.data, ref.entity_id, idxs, length(idxs))::Int64
	elseif type == 2
		result = ccall(get_parameter_numeric_h, Clonglong, (Ptr{Cvoid}, Entity_Id, Ptr{Mobius_Index_Value}, Clonglong),
			ref.data, ref.entity_id, idxs, length(idxs))::Bool
	elseif type == 3
		str = ccall(get_parameter_string_h, Cstring, (Ptr{Cvoid}, Entity_Id, Ptr{Mobius_Index_Value}, Clonglong),
			ref.data, ref.entity_id, idxs, length(idxs))
		result = copy_str(str)
	elseif type == 4
		str = ccall(get_parameter_string_h, Cstring, (Ptr{Cvoid}, Entity_Id, Ptr{Mobius_Index_Value}, Clonglong),
			ref.data, ref.entity_id, idxs, length(idxs))
		datestr = copy_str(str)
		# TODO! Should detect if it has a timestamp or not!
		result = DateTime(Date(datestr))
	end
	check_error()
	return result
end

Base.getindex(ref::Entity_Ref, indexes::Vector{Any}) = get_parameter(ref, indexes)
Base.getindex(ref::Entity_Ref, index::Any) = get_parameter(ref, Any[index])
Base.getindex(ref::Entity_Ref) = get_parameter(ref, Any[])

end # module