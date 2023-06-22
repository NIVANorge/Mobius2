
import ctypes
import numpy as np
import pandas as pd

#NOTE: Just sketching out for now. It is not implemented fully yet.

dll = ctypes.CDLL("../test/c_api.dll")

# Volatile! These structure must match the corresponding in the c++ code
# TODO: we should have a way to auto-generate some of this.
class Entity_Id(ctypes.Structure) :
	_fields_ = [("reg_type", ctypes.c_int16), ("id", ctypes.c_int16)]

invalid_entity_id = Entity_Id(0, -1)

max_var_loc_components = 6

class Var_Id(ctypes.Structure) :
	_fields_ = [("type", ctypes.c_int32), ("id", ctypes.c_int32)]
	
invalid_var = Var_Id(0, -1)
	
class Time_Step_Size(ctypes.Structure) :
	_fields_ = [("unit", ctypes.c_int32), ("magnitude", ctypes.c_int32)]
	
class Mobius_Series_Metadata(ctypes.Structure) :
	_fields_ = [("name", ctypes.c_char_p), ("unit", ctypes.c_char_p)]

# TODO: Int parameters should have int min and max though...
class Mobius_Entity_Metadata(ctypes.Structure) :
	_fields_ = [("name", ctypes.c_char_p), ("unit", ctypes.c_char_p), ("description", ctypes.c_char_p), ("min", ctypes.c_double), ("max", ctypes.c_double)]

dll.mobius_encountered_error.argtypes = [ctypes.c_char_p, ctypes.c_int64]
dll.mobius_encountered_error.restype = ctypes.c_int64
	
dll.mobius_encountered_log.argtypes = [ctypes.c_char_p, ctypes.c_int64]
dll.mobius_encountered_log.restype = ctypes.c_int64

dll.mobius_build_from_model_and_data_file.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
dll.mobius_build_from_model_and_data_file.restype  = ctypes.c_void_p



dll.mobius_get_steps.argtypes = [ctypes.c_void_p, ctypes.c_int32]
dll.mobius_get_steps.restype = ctypes.c_int64

dll.mobius_run_model.argtypes = [ctypes.c_void_p, ctypes.c_int64]
dll.mobius_run_model.restype = ctypes.c_bool

dll.mobius_get_time_step_size.argtypes = [ctypes.c_void_p]
dll.mobius_get_time_step_size.restype  = Time_Step_Size

dll.mobius_get_start_date.argtypes = [ctypes.c_void_p, ctypes.c_int32]
dll.mobius_get_start_date.restype = ctypes.c_char_p

dll.mobius_deserialize_entity.argtypes = [ctypes.c_void_p, Entity_Id, ctypes.c_char_p]
dll.mobius_deserialize_entity.restype  = Entity_Id

dll.mobius_get_entity.argtypes = [ctypes.c_void_p, Entity_Id, ctypes.c_char_p]
dll.mobius_get_entity.restype = Entity_Id

dll.mobius_deserialize_var.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
dll.mobius_deserialize_var.restype = Var_Id

dll.mobius_get_var_id_from_list.argtypes = [ctypes.c_void_p, ctypes.POINTER(Entity_Id), ctypes.c_int64]
dll.mobius_get_var_id_from_list.restype = Var_Id

dll.mobius_get_special_var.argtypes = [ctypes.c_void_p, Var_Id, Var_Id, ctypes.c_int16]
dll.mobius_get_special_var.restype = Var_Id

dll.mobius_get_series_data.argtypes = [ctypes.c_void_p, Var_Id, ctypes.POINTER(ctypes.c_char_p), ctypes.c_int64, ctypes.POINTER(ctypes.c_double), ctypes.c_int64]

dll.mobius_get_series_metadata.argtypes = [ctypes.c_void_p, Var_Id]
dll.mobius_get_series_metadata.restype = Mobius_Series_Metadata


dll.mobius_get_index_set_count.argtypes = [ctypes.c_void_p, Entity_Id]
dll.mobius_get_index_set_count.restype = ctypes.c_int64


dll.mobius_get_value_type.argtypes = [ctypes.c_void_p, Entity_Id]
dll.mobius_get_value_type.restype = ctypes.c_int64

dll.mobius_set_parameter_real.argtypes = [ctypes.c_void_p, Entity_Id, ctypes.POINTER(ctypes.c_char_p), ctypes.c_int64, ctypes.c_double]

dll.mobius_get_parameter_real.argtypes = [ctypes.c_void_p, Entity_Id, ctypes.POINTER(ctypes.c_char_p), ctypes.c_int64]
dll.mobius_get_parameter_real.restype  = ctypes.c_double

dll.mobius_set_parameter_int.argtypes = [ctypes.c_void_p, Entity_Id, ctypes.POINTER(ctypes.c_char_p), ctypes.c_int64, ctypes.c_int64]

dll.mobius_get_parameter_int.argtypes = [ctypes.c_void_p, Entity_Id, ctypes.POINTER(ctypes.c_char_p), ctypes.c_int64]
dll.mobius_get_parameter_int.restype  = ctypes.c_int64

dll.mobius_set_parameter_string.argtypes = [ctypes.c_void_p, Entity_Id, ctypes.POINTER(ctypes.c_char_p), ctypes.c_int64, ctypes.c_char_p]

dll.mobius_get_parameter_string.argtypes = [ctypes.c_void_p, Entity_Id, ctypes.POINTER(ctypes.c_char_p), ctypes.c_int64]
dll.mobius_get_parameter_string.restype  = ctypes.c_char_p

dll.mobius_get_entity_metadata.argtypes = [ctypes.c_void_p, Entity_Id]
dll.mobius_get_entity_metadata.restype = Mobius_Entity_Metadata

#dll.mobius_get_conc_id.argtypes = [ctypes.c_void_p, Var_Id]
#dll.mobius_get_conc_id.restype = Var_Id

# NOTE: Must match Reg_Types: We should find a way to auto-generate this instead!
MODULE_TYPE = 1
COMPONENT_TYPE = 2
PARAMETER_TYPE = 3


def _c_str(string) :
	return string.encode('utf-8')

def _pack_indexes(indexes) :
	#TODO: Allow integer indexes somehow
	#print("Type is %s" % type(indexes))
	
	if isinstance(indexes, list) or isinstance(indexes, tuple):
		cindexes = [index.encode('utf-8') for index in indexes]
	elif isinstance(indexes, str) :
		cindexes = [indexes.encode('utf-8')]
	else :
		raise ValueError('Expected a single string or list of strings for the index(es)')
	return (ctypes.c_char_p * len(cindexes))(*cindexes)
	
def _len(indexes) :
	if isinstance(indexes, str) : return 1
	return len(indexes)
	
def _check_for_errors() :
	buflen = 1024
	msgbuf = ctypes.create_string_buffer(buflen)
	
	logmsg = ''
	log = False
	loglen = dll.mobius_encountered_log(msgbuf, buflen)
	while loglen > 0 :
		log = True
		logmsg += msgbuf.value.decode('utf-8')
		loglen = dll.mobius_encountered_log(msgbuf, buflen)
		
	if log :
		print(logmsg)

	error = False
	errmsg = ''
	errlen = dll.mobius_encountered_error(msgbuf, buflen)
	while errlen > 0 :
		error = True
		errmsg += msgbuf.value.decode('utf-8')
		errlen = dll.mobius_encountered_error(msgbuf, buflen)
	
	if error : raise RuntimeError(errmsg)

def _get_par_value(app_ptr, entity_id, indexes) :
	#TODO: Other value types
	type = dll.mobius_get_value_type(app_ptr, entity_id)
	if type == 0 :
		result = dll.mobius_get_parameter_real(app_ptr, entity_id, _pack_indexes(indexes), _len(indexes))
	elif type == 1 :
		result = dll.mobius_get_parameter_int(app_ptr, entity_id, _pack_indexes(indexes), _len(indexes))
	elif type == 2 :
		res = dll.mobius_get_parameter_int(app_ptr, entity_id, _pack_indexes(indexes), _len(indexes))
		result = (res == 1)
	elif type == 3 :
		result = dll.mobius_get_parameter_string(app_ptr, entity_id, _pack_indexes(indexes), _len(indexes))
	elif type == 4 :
		val_str = dll.mobius_get_parameter_string(app_ptr, entity_id, _pack_indexes(indexes), _len(indexes))
		result = pd.to_datetime(val_str)
	else :
		raise ValueError("Unimplemented parameter type")
	_check_for_errors()
	return result
	
def _set_par_value(app_ptr, entity_id, indexes, value) :
	#TODO: Other value types
	type = dll.mobius_get_value_type(app_ptr, entity_id)
	if type == 0 :
		dll.mobius_set_parameter_real(app_ptr, entity_id, _pack_indexes(indexes), _len(indexes), value)
	elif type == 1 or type == 2:
		dll.mobius_set_parameter_int(app_ptr, entity_id, _pack_indexes(indexes), _len(indexes), value)
	elif type == 3 or type == 4 :
		# TODO: If argument is of datetime type, decode it first
		str_val = _c_str(value)
		dll.mobius_set_parameter_string(app_ptr, entity_id, _pack_indexes(indexes), _len(indexes), str_val)
	else :
		raise ValueError("Unimplemented parameter type")
	_check_for_errors()
	
def is_valid(id) :
	if isinstance(id, Entity_Id) :
		return id.reg_type > 0 and id.id >= 0
	if isinstance(id, Var_Id) :
		return id.id >= 0

class Scope :
	def __init__(self, app_ptr, scope_id) :
		self.app_ptr = app_ptr
		self.scope_id = scope_id
	
	# TODO: If either of these are a flux, it should return a State_Var
	# TODO: Check validity of the id.
	def __getitem__(self, serial_name) :
		entity_id = dll.mobius_deserialize_entity(self.app_ptr, self.scope_id, _c_str(serial_name))
		_check_for_errors()
		if not is_valid(entity_id) :
			raise ValueError('The serial name "%s" does not refer to a valid entity' % serial_name)
		return Entity(self.app_ptr, self.scope_id, entity_id)

	def __getattr__(self, handle_name) :
		entity_id = dll.mobius_get_entity(self.app_ptr, self.scope_id, _c_str(handle_name))
		if not is_valid(entity_id) :
			raise ValueError("The handle name '%s' does not refer to a valid entity" % handle_name)
		_check_for_errors()
		return Entity(self.app_ptr, self.scope_id, entity_id)
	
	# TODO: Would be nice if we could override __setattr__ for parameters with a single instance, but it is very tricky since it overrides also things like self.app_ptr = ..
	
	def list_all(type) :
		# TODO
		pass

class Model_Application(Scope) :
	def __init__(self, app_ptr) :
		super().__init__(app_ptr, invalid_entity_id)
		
	
	def __del__(self) :
		#TODO
		pass
	
	@classmethod
	def build_from_model_and_data_file(cls, model_file, data_file) :
		app_ptr = dll.mobius_build_from_model_and_data_file(_c_str(model_file), _c_str(data_file))
		_check_for_errors()
		return cls(app_ptr)
		
	def run(self, ms_timeout=-1) :
		finished = dll.mobius_run_model(self.app_ptr, ms_timeout)
		_check_for_errors()
		return finished
		
	def var(self, serial_name) :
		var_id = dll.mobius_deserialize_var(self.app_ptr, _c_str(serial_name))
		if not is_valid(var_id) :
			raise ValueError('The serial name "%s" does not refer to a valid state variable or series.' % serial_name)
		_check_for_errors()
		return State_Var(self.app_ptr, invalid_entity_id, [], var_id) #TODO: Should maybe retrieve the entity id list from loc1 (if relevant)?
	
		
class Entity(Scope) :
	def __init__(self, app_ptr, scope_id, entity_id) :		
		self.entity_id = entity_id
		self.superscope_id = scope_id
		
		if entity_id.reg_type == MODULE_TYPE :
			Scope.__init__(self, app_ptr, entity_id)
		else :
			Scope.__init__(self, app_ptr, scope_id)
			
	def __getitem__(self, name_or_indexes) :
		if self.entity_id.reg_type == MODULE_TYPE :
			return Scope.__getitem__(self, name_or_indexes)
		elif self.entity_id.reg_type == PARAMETER_TYPE :
			return _get_par_value(self.app_ptr, self.entity_id, name_or_indexes)
		else :
			raise ValueError("This entity can't be accessed using []")
	
	def __setitem__(self, name_or_indexes, value) :
		if self.entity_id.reg_type == PARAMETER_TYPE :
			_set_par_value(self.app_ptr, self.entity_id, name_or_indexes, value)
		else :
			raise ValueError("This entity can't be accessed using []")
	
	def __getattr__(self, handle_name) :
		if self.entity_id.reg_type == MODULE_TYPE :
			return Scope.__getattr__(self, handle_name)
		elif self.entity_id.reg_type == COMPONENT_TYPE :
			scope = Scope(self.app_ptr, self.superscope_id)
			other = scope.__getattr__(handle_name)
			return State_Var.from_id_list(self.app_ptr, self.superscope_id, [self.entity_id, other.entity_id])
		else :
			raise ValueError("This entity doesn't have accessible fields.")

	def name(self) :
		data = dll.mobius_get_entity_metadata(self.app_ptr, self.entity_id)
		_check_for_errors()
		return data.name.decode('utf-8')
	
	def min(self) :
		if self.entity_id.reg_type != PARAMETER_TYPE :
			raise ValueError("This entity doesn't have a min value.")
		type = dll.mobius_get_value_type(app_ptr, entity_id)
		if type != 0 :
			raise ValueError("This parameter does not have a min value.")
		data = dll.mobius_get_entity_metadata(self.app_ptr, self.entity_id)
		_check_for_errors()
		return data.min
		
	def max(self) :
		if self.entity_id.reg_type != PARAMETER_TYPE :
			raise ValueError("This entity doesn't have a max value.")
		type = dll.mobius_get_value_type(app_ptr, entity_id)
		if type != 0 :
			raise ValueError("This parameter does not have a max value.")
		data = dll.mobius_get_entity_metadata(self.app_ptr, self.entity_id)
		_check_for_errors()
		return data.max
		
	def description(self) :
		if self.entity_id.reg_type != PARAMETER_TYPE :
			raise ValueError("This entity doesn't have a description.")
		data = dll.mobius_get_entity_metadata(self.app_ptr, self.entity_id)
		_check_for_errors()
		return data.description.decode('utf-8')
		
	def unit(self) :
		if self.entity_id.reg_type != PARAMETER_TYPE :
			raise ValueError("This entity doesn't have a description.")
		data = dll.mobius_get_entity_metadata(self.app_ptr, self.entity_id)
		_check_for_errors()
		return data.unit.decode('utf-8')

class State_Var :
	
	@classmethod
	def from_id_list(cls, app_ptr, scope_id, id_list) :
		if len(id_list) > max_var_loc_components :
			raise ValueError("Too many chained component to a variable location. The maximum is %d" % max_var_loc_components)
		lst = (Entity_Id * len(id_list))(*id_list)
		var_id = dll.mobius_get_var_id_from_list(app_ptr, lst, len(id_list))
		if not is_valid(var_id) :
			raise ValueError("This does not refer to a valid state variable")
		_check_for_errors()
		return State_Var(app_ptr, scope_id, id_list, var_id)
	
	#TODO: Also introduce concept of variable_type so that we can do things like .conc(), .dissolved(), .in_flux() etc.
	
	def __init__(self, app_ptr, scope_id, id_list, var_id) :
		self.app_ptr = app_ptr
		self.scope_id = scope_id
		self.id_list = id_list
		self.var_id = var_id
		
	def __getitem__(self, indexes) :
		time_steps = dll.mobius_get_steps(self.app_ptr, self.var_id.type)
		series = (ctypes.c_double * time_steps)()
		
		dll.mobius_get_series_data(self.app_ptr, self.var_id, _pack_indexes(indexes), _len(indexes), series, time_steps)
		_check_for_errors()
		
		start_date = dll.mobius_get_start_date(self.app_ptr, self.var_id.type).decode('utf-8')
		start_date = pd.to_datetime(start_date)
		_check_for_errors()

		step_size = dll.mobius_get_time_step_size(self.app_ptr)
		step_type = 'S' if step_size.unit == 0 else 'MS'
		freq='%d%s' % (step_size.magnitude, step_type)
		
		dates = pd.date_range(start=start_date, periods=time_steps, freq=freq)
		# We have to do this, otherwise some operations on it crashes:
		dates = pd.Series(data = dates, name = 'Date')
		return pd.Series(data=np.array(series, copy=False), index=dates, name=self.name())
	
	def __setitem__(self, indexes, values) :
		# TODO
		pass
		
	def __getattr__(self, handle_name) :
		scope = Scope(self.app_ptr, self.scope_id)
		other = scope.__getattr__(handle_name)
		new_list = self.id_list + [other.entity_id]
		return State_Var.from_id_list(self.app_ptr, self.scope_id, new_list)

	def conc(self) :
		conc_id = dll.mobius_get_special_var(self.app_ptr, self.var_id, invalid_var, 5)
		if not is_valid(conc_id) :
			raise ValueError("This variable does not have a concentration.")
		_check_for_errors()
		return State_Var(self.app_ptr, self.scope_id, [], conc_id)

	def name(self) :
		data = dll.mobius_get_series_metadata(self.app_ptr, self.var_id)
		_check_for_errors()
		return data.name.decode('utf-8')
		
	def unit(self) :
		data = dll.mobius_get_series_metadata(self.app_ptr, self.var_id)
		_check_for_errors()
		return data.unit.decode('utf-8')
