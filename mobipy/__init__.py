
import ctypes
import numpy as np
import pandas as pd

#NOTE: Just sketching out for now. It is not implemented fully yet.

dll = ctypes.CDLL("../test/c_api.dll")

# Volatile! These structure must match the corresponding in the c++ code
class Entity_Id(ctypes.Structure) :
	_fields_ = [("module_id", ctypes.c_int16), ("reg_type", ctypes.c_int16), ("id", ctypes.c_int32)]

class Module_Entity_Reference(ctypes.Structure) :
	_fields_ = [("type", ctypes.c_int16), ("entity", Entity_Id), ("value_type", ctypes.c_int32)]
	
class Model_Entity_Reference(ctypes.Structure) :
	_fields_ = [("type", ctypes.c_int16), ("module", ctypes.c_void_p), ("entity", Entity_Id)]
	
class Var_Reference(ctypes.Structure) :
	_fields_ = [("id", ctypes.c_int32), ("type", ctypes.c_int16)]
	
class Time_Step_Size(ctypes.Structure) :
	_fields_ = [("unit", ctypes.c_int32), ("magnitude", ctypes.c_int32)]

dll.c_api_build_from_model_and_data_file.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
dll.c_api_build_from_model_and_data_file.restype  = ctypes.c_void_p

dll.c_api_get_model_entity_by_handle.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
dll.c_api_get_model_entity_by_handle.restype = Model_Entity_Reference

dll.c_api_get_module_reference_by_name.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
dll.c_api_get_module_reference_by_name.restype  = ctypes.c_void_p

dll.c_api_get_module_entity_by_handle.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
dll.c_api_get_module_entity_by_handle.restype = Module_Entity_Reference

dll.c_api_set_parameter_real.argtypes = [ctypes.c_void_p, Entity_Id, ctypes.POINTER(ctypes.c_char_p), ctypes.c_int64, ctypes.c_double]

dll.c_api_get_parameter_real.argtypes = [ctypes.c_void_p, Entity_Id, ctypes.POINTER(ctypes.c_char_p), ctypes.c_int64]
dll.c_api_get_parameter_real.restype  = ctypes.c_double

dll.c_api_get_var_reference.argtypes = [ctypes.c_void_p, Entity_Id, Entity_Id]
dll.c_api_get_var_reference.restype  = Var_Reference

dll.c_api_get_steps.argtypes = [ctypes.c_void_p, ctypes.c_int16]
dll.c_api_get_steps.restype = ctypes.c_int64

dll.c_api_get_series_data.argtypes = [ctypes.c_void_p, ctypes.c_int32, ctypes.c_int16, ctypes.POINTER(ctypes.c_char_p), ctypes.c_int64, ctypes.POINTER(ctypes.c_double), ctypes.c_int64, ctypes.c_char_p, ctypes.c_int64]

dll.c_api_run_model.argtypes = [ctypes.c_void_p]

dll.c_api_get_time_step_size.argtypes = [ctypes.c_void_p]
dll.c_api_get_time_step_size.restype  = Time_Step_Size

dll.c_api_get_start_date.argtypes = [ctypes.c_void_p, ctypes.c_int16]
dll.c_api_get_start_date.restype = ctypes.c_char_p


def _c_str(string) :
	return string.encode('utf-8')   #TODO: We should figure out what encoding is best to use here.

def _pack_indexes(indexes) :
	cindexes = [index.encode('utf-8') for index in indexes]
	return (ctypes.c_char_p * len(cindexes))(*cindexes)

class Model_Application :
	def __init__(self, ptr) :
		self.ptr = ptr
		
	@classmethod
	def build_from_model_and_data_file(cls, model_file, data_file) :
		ptr = dll.c_api_build_from_model_and_data_file(_c_str(model_file), _c_str(data_file))
		return cls(ptr)
		
	def run(self) :
		dll.c_api_run_model(self.ptr)
		
	def __getitem__(self, module_name) :
		ptr = dll.c_api_get_module_reference_by_name(self.ptr, _c_str(module_name))
		return Module(ptr, self.ptr)
		
	def __getattr__(self, handle_name) :
		ref = dll.c_api_get_model_entity_by_handle(self.ptr, _c_str(handle_name))
		if ref.type == 0 :
			raise RuntimeError("Invalid model entity handle %s" % handle_name)
		elif ref.type == 1 :
			return Module(ref.module, self.ptr) 
		elif ref.type == 2 :
			return Compartment(ref.entity, self.ptr, ref.module)
		else :
			raise RuntimeError("Unimplemented model entity reference type")
		
		
class Module :
	def __init__(self, ptr, app_ptr) :
		self.ptr     = ptr
		self.app_ptr = app_ptr
		
	def __getattr__(self, handle_name) :
		ref = dll.c_api_get_module_entity_by_handle(self.ptr, _c_str(handle_name))
		if ref.type == 0 :
			raise RuntimeError("Invalid module entity handle %s" % handle_name)
		elif ref.type == 1 :
			return Parameter(ref.entity, self.app_ptr)
		elif ref.type == 2 :
			return Compartment(ref.entity, self.app_ptr, self.ptr)
		else :
			raise RuntimeError("Can't reference entities of that type directly (at least for now)")
	
class Parameter :
	def __init__(self, par_id, app_ptr) :
		self.app_ptr    = app_ptr
		self.par_id    = par_id
	
	#TODO: handle different parameter types
	
	def __getitem__(self, indexes) :
		if not isinstance(indexes, list) :
			raise RuntimeError("Expected a list object for the indexes")
		return dll.c_api_get_parameter_real(self.app_ptr, self.par_id, _pack_indexes(indexes), len(indexes))
	
	def __setitem__(self, indexes, value) :
		if not isinstance(indexes, list) :
			raise RuntimeError("Expected a list object for the indexes")
		dll.c_api_set_parameter_real(self.app_ptr, self.par_id, _pack_indexes(indexes), len(indexes), value)
		
	def __getattr__(self, name) :
		#TODO:
		if name == 'default' :
			return 0
		elif name == 'min' :
			return 0
		elif name == 'max' :
			return 0
		elif name == 'unit' :
			return 0
		elif name == 'description' :
			return 0
		elif name == 'name' :
			return 0
		else :
			raise RuntimeError("Unknown field %s for Parameter" % name)
		
class Compartment :
	def __init__(self, id, app_ptr, module_ptr) :
		self.module_ptr = module_ptr
		self.app_ptr = app_ptr
		self.id = id
		
	def __getattr__(self, handle_name) :
		ref = dll.c_api_get_module_entity_by_handle(self.module_ptr, _c_str(handle_name))
		if ref.type == 3 :
			#TODO: eventually we just have to return a value location here
			var = dll.c_api_get_var_reference(self.app_ptr, self.id, ref.entity)
			if var.type == 0 :
				raise RuntimeError("Could not find a state variable with that location.")
			return Series(var.id, var.type, self.app_ptr)
		else :
			raise RuntimeError("Can only look up properties or quantities from compartments.")
		
class Series :
	def __init__(self, id, type, app_ptr) :
		self.id = id
		self.type = type
		self.app_ptr = app_ptr
	
	def __getitem__(self, indexes) :
		if not isinstance(indexes, list) :
			raise RuntimeError("Expected a list object for the indexes")
			
		time_steps = dll.c_api_get_steps(self.app_ptr, self.type)
		series = (ctypes.c_double * time_steps)()
		
		namebuf = ctypes.create_string_buffer(512)
		dll.c_api_get_series_data(self.app_ptr, self.id, self.type, _pack_indexes(indexes), len(indexes), series, time_steps, namebuf, 512)
		
		
		start_date = dll.c_api_get_start_date(self.app_ptr, self.type).decode('utf-8')
		start_date = pd.to_datetime(start_date)

		step_size = dll.c_api_get_time_step_size(self.app_ptr)
		step_type = 'S' if step_size.unit == 0 else 'MS'
		freq='%d%s' % (step_size.magnitude, step_type)
		
		dates = pd.date_range(start=start_date, periods=time_steps, freq=freq)
		# We have to do this, otherwise some operations on it crashes:
		dates = pd.Series(data = dates, name = 'Date')
		return pd.Series(data=np.array(series, copy=False), index=dates, name=namebuf.value.decode('utf-8'))




# Usage code
#     simplyq = Model_Application.build_from_model_and_data_file("simplyq_model.txt", "simplyq_data_tarland.dat")
#     simplyq["SimplyQ land"].bfi[["Arable"]] = 0.5
# or
#     simplyq.sw.bfi[["Arable]] = 0.5
#
#     simplyq.run()
# # these should be pandas.series and pandas.dataframes
#     simplq.gw.water[[]].plot()
#     simplyq[('gw.water', []), ('gw.flow', [])].plot()


# Should we instead do
#     simplyq.sw.bfi[["Arable"]].value = bla    ? Would allow us to store   simplyq.sw.bfi[["Arable"]] as an object, which could just contain the offset integer, and reuse it.

# Also want
#     simplyq.sw.bfi.default   simplyq.sw.bfi.min   simplyq.sw.bfi.description etc.