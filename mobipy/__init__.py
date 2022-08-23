
import ctypes
import numpy as np
import pandas as pd

#NOTE: Just sketching out for now. It is not implemented fully yet.

dll = ctypes.CDLL("../test/c_api.dll")

# Volatile! These structure must match the corresponding in the c++ code
# TODO: we should really have a way to auto-generate them...
class Entity_Id(ctypes.Structure) :
	_fields_ = [("module_id", ctypes.c_int16), ("reg_type", ctypes.c_int16), ("id", ctypes.c_int32)]

class Module_Entity_Reference(ctypes.Structure) :
	_fields_ = [("type", ctypes.c_int16), ("entity", Entity_Id), ("value_type", ctypes.c_int32)]
	
class Model_Entity_Reference(ctypes.Structure) :
	_fields_ = [("type", ctypes.c_int16), ("module", ctypes.c_void_p), ("entity", Entity_Id)]

max_dissolved_chain = 2

class Value_Location(ctypes.Structure) :
	_fields_ = [("type", ctypes.c_int32), ("n_dissolved", ctypes.c_int32), ("neighbor", Entity_Id), ("compartment", Entity_Id), ("property_or_quantity", Entity_Id), ("dissolved_in", max_dissolved_chain*Entity_Id)]
	
class Var_Reference(ctypes.Structure) :
	_fields_ = [("id", ctypes.c_int32), ("type", ctypes.c_int16)]
	
class Time_Step_Size(ctypes.Structure) :
	_fields_ = [("unit", ctypes.c_int32), ("magnitude", ctypes.c_int32)]

dll.mobius_build_from_model_and_data_file.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
dll.mobius_build_from_model_and_data_file.restype  = ctypes.c_void_p

dll.mobius_get_model_entity_by_handle.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
dll.mobius_get_model_entity_by_handle.restype = Model_Entity_Reference

dll.mobius_get_module_reference_by_name.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
dll.mobius_get_module_reference_by_name.restype  = ctypes.c_void_p

dll.mobius_get_module_entity_by_handle.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
dll.mobius_get_module_entity_by_handle.restype = Module_Entity_Reference

dll.mobius_set_parameter_real.argtypes = [ctypes.c_void_p, Entity_Id, ctypes.POINTER(ctypes.c_char_p), ctypes.c_int64, ctypes.c_double]

dll.mobius_get_parameter_real.argtypes = [ctypes.c_void_p, Entity_Id, ctypes.POINTER(ctypes.c_char_p), ctypes.c_int64]
dll.mobius_get_parameter_real.restype  = ctypes.c_double

dll.mobius_get_value_location.argtypes = [ctypes.c_void_p, Entity_Id, Entity_Id]
dll.mobius_get_value_location.restype  = Value_Location

dll.mobius_get_dissolved_location.argtypes = [ctypes.c_void_p, Value_Location, Entity_Id]
dll.mobius_get_dissolved_location.restype  = Value_Location

dll.mobius_get_var_reference.argtypes = [ctypes.c_void_p, Value_Location]
dll.mobius_get_var_reference.restype  = Var_Reference

dll.mobius_get_conc_reference.argtypes = [ctypes.c_void_p, Value_Location]
dll.mobius_get_conc_reference.restype = Var_Reference

dll.mobius_get_steps.argtypes = [ctypes.c_void_p, ctypes.c_int16]
dll.mobius_get_steps.restype = ctypes.c_int64

dll.mobius_get_series_data.argtypes = [ctypes.c_void_p, ctypes.c_int32, ctypes.c_int16, ctypes.POINTER(ctypes.c_char_p), ctypes.c_int64, ctypes.POINTER(ctypes.c_double), ctypes.c_int64, ctypes.c_char_p, ctypes.c_int64]

dll.mobius_run_model.argtypes = [ctypes.c_void_p]

dll.mobius_get_time_step_size.argtypes = [ctypes.c_void_p]
dll.mobius_get_time_step_size.restype  = Time_Step_Size

dll.mobius_get_start_date.argtypes = [ctypes.c_void_p, ctypes.c_int16]
dll.mobius_get_start_date.restype = ctypes.c_char_p


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
		ptr = dll.mobius_build_from_model_and_data_file(_c_str(model_file), _c_str(data_file))
		return cls(ptr)
		
	def run(self) :
		dll.mobius_run_model(self.ptr)
		
	def __getitem__(self, module_name) :
		ptr = dll.mobius_get_module_reference_by_name(self.ptr, _c_str(module_name))
		return Module(ptr, self.ptr)
		
	def __getattr__(self, handle_name) :
		ref = dll.mobius_get_model_entity_by_handle(self.ptr, _c_str(handle_name))
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
		ref = dll.mobius_get_module_entity_by_handle(self.ptr, _c_str(handle_name))
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
		return dll.mobius_get_parameter_real(self.app_ptr, self.par_id, _pack_indexes(indexes), len(indexes))
	
	def __setitem__(self, indexes, value) :
		if not isinstance(indexes, list) :
			raise RuntimeError("Expected a list object for the indexes")
		dll.mobius_set_parameter_real(self.app_ptr, self.par_id, _pack_indexes(indexes), len(indexes), value)
		
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
		ref = dll.mobius_get_module_entity_by_handle(self.module_ptr, _c_str(handle_name))
		if ref.type == 3 :
			loc = dll.mobius_get_value_location(self.app_ptr, self.id, ref.entity)
			return Series(loc, self.app_ptr, self.module_ptr)
		else :
			raise RuntimeError("Can only look up properties or quantities from compartments.")
		
class Series :
	def __init__(self, loc, app_ptr, module_ptr) :
		self.loc = loc
		self.app_ptr = app_ptr
		self.module_ptr = module_ptr
	
	def _get_var(self) :
		var = dll.mobius_get_var_reference(self.app_ptr, self.loc)
		if var.type == 0 :
			raise RuntimeError("Could not find a state variable with that location.")
		return var
	
	def __getitem__(self, indexes) :
		if not isinstance(indexes, list) :
			raise RuntimeError("Expected a list object for the indexes")
		
		var = self._get_var()
		
		time_steps = dll.mobius_get_steps(self.app_ptr, var.type)
		series = (ctypes.c_double * time_steps)()
		
		namebuf = ctypes.create_string_buffer(512)
		dll.mobius_get_series_data(self.app_ptr, var.id, var.type, _pack_indexes(indexes), len(indexes), series, time_steps, namebuf, 512)
		
		
		start_date = dll.mobius_get_start_date(self.app_ptr, var.type).decode('utf-8')
		start_date = pd.to_datetime(start_date)

		step_size = dll.mobius_get_time_step_size(self.app_ptr)
		step_type = 'S' if step_size.unit == 0 else 'MS'
		freq='%d%s' % (step_size.magnitude, step_type)
		
		dates = pd.date_range(start=start_date, periods=time_steps, freq=freq)
		# We have to do this, otherwise some operations on it crashes:
		dates = pd.Series(data = dates, name = 'Date')
		return pd.Series(data=np.array(series, copy=False), index=dates, name=namebuf.value.decode('utf-8'))
	
	def __getattr__(self, handle_name) :
			
		if self.loc.n_dissolved == max_dissolved_chain :
			raise RuntimeError("Can not have that many chained dissolved substances")
		
		ref = dll.mobius_get_module_entity_by_handle(self.module_ptr, _c_str(handle_name))
		if ref.type == 3 :
			loc = dll.mobius_get_dissolved_location(self.app_ptr, self.loc, ref.entity)
			return Series(loc, self.app_ptr, self.module_ptr)
		else :
			raise RuntimeError("Can only look up properties or quantities from compartments.")

class Conc_Series(Series) :
	
	def _get_var(self) :
		var = dll.mobius_get_conc_reference(self.app_ptr, self.loc)
		if var.type == 0 :
			raise RuntimeError("Could not find a state variable with that location that has a concentration.")
		return var
	
	def __getattr__(self, handle_name) :
		raise RuntimeError("Can't look up sub-proerties of a conc")
		
def conc(series) :
	return Conc_Series(series.loc, series.app_ptr, series.module_ptr)

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