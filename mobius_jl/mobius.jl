# Package to run Mobius2 models from the Julia language. Not yet fully featured

module mobius

using Libdl

mobius_dll = dlopen("../mobipy/c_abi.dll")

export setup_model, run_model, get_entity, get_var_id_from_list, get_steps, get_series_data, invalid_entity_id, invalid_var, no_index

setup_model_h       = dlsym(mobius_dll, "mobius_build_from_model_and_data_file")
encountered_error_h = dlsym(mobius_dll, "mobius_encountered_error")
run_model_h         = dlsym(mobius_dll, "mobius_run_model")
get_entity_h        = dlsym(mobius_dll, "mobius_get_entity")
get_var_id_from_list_h = dlsym(mobius_dll, "mobius_get_var_id_from_list")
get_steps_h         = dlsym(mobius_dll, "mobius_get_steps")
get_series_data_h   = dlsym(mobius_dll, "mobius_get_series_data")

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


#TODO: Probably leaks now, do we have to free it again?
#TODO: Also, it just doesn't work..
#function malloc_cstring(s::String)
#    n = sizeof(s)+1 # size in bytes + NUL terminator
#    return GC.@preserve s @ccall memcpy(Libc.malloc(n)::Cstring,
#                                        s::Cstring, n::Csize_t)::Cstring
#end

function malloc_cstring(str::String)
    ptr = @ccall malloc((length(str) + 1)::Csize_t)::Cstring # char *
    @ccall strcpy(ptr::Cstring, str::Cstring)::Cstring       # copy to memory
    #@ccall printf("%s\n"::Cstring ; ptr::Cstring)::Cint      # prove it's really there
	return ptr #Ref(ptr)                                          # return a Ref{Cstring} for Julia to control
end

invalid_entity_id = Entity_Id(-1, -1)
invalid_var       = Var_Id(-1, -1)
no_index          = Mobius_Index_Value(C_NULL, 0)

function check_error()
	buf = " "^512 #TODO: find a better way to do declare a buffer!
	
	#TODO: Should also raise an error or similar
	len = ccall(encountered_error_h, Clonglong, (Cstring, Clonglong), buf, length(buf))
	while len > 0
		println(first(buf, len))
		len = ccall(encountered_error_h, Clonglong, (Cstring, Clonglong), buf, length(buf))
	end
end

function setup_model(model_file, data_file)
    result =  ccall(setup_model_h, Ptr{Cvoid}, (Cstring, Cstring, Cstring, Cint, Cint), 
		model_file, data_file, "C:\\Data\\Mobius2\\", true, true)   #Ooops! TODO! dynamically get the base folder
	check_error()
	return result
end

function run_model(data, ms_timeout=-1)
	result = ccall(run_model_h, Cint, (Ptr{Cvoid}, Clonglong),
		data, ms_timeout)
	check_error()
	return result
end

function get_entity(data, scope_id, identifier)
	result = ccall(get_entity_h, Entity_Id, (Ptr{Cvoid}, Entity_Id, Cstring),
		data, scope_id, identifier)
	check_error()
	return result
end

function get_var_id_from_list(data, ids)
	result = ccall(get_var_id_from_list_h, Var_Id, (Ptr{Cvoid}, Ptr{Entity_Id}, Clonglong),
		data, ids, length(ids))
	check_error()
	return result
end

function get_steps(data, var_id)
	result = ccall(get_steps_h, Clonglong, (Ptr{Cvoid}, Cint),
		data, var_id.type)
	check_error()
	return result
end

function get_series_data(data, var_id, indexes)
	steps = get_steps(data, var_id)
	result = Vector{Cdouble}(undef, steps)
	
	# TODO: make it work with numerical indexes too
	idxs = Vector{Mobius_Index_Value}(undef, length(indexes))
	i = 1
	for idx in indexes
		idx_c = malloc_cstring(idx)
		idxs[i] = Mobius_Index_Value(idx_c, -1)
		i += 1
	end
	
	ccall(get_series_data_h, Cvoid, (Ptr{Cvoid}, Var_Id, Ptr{Mobius_Index_Value}, Clonglong, Ptr{Cdouble}, Clonglong),
		data, var_id, idxs, length(idxs), result, length(result))
	check_error()
	
	return result
end


#dlclose(mobius_dll)

end # module