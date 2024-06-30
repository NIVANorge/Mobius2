
import ctypes
import numpy as np
import pandas as pd
import os
import pathlib

# Volatile! These structure must match the corresponding in the c++ code
# TODO: we should have a way to auto-generate some of this.
class Entity_Id(ctypes.Structure) :
	_fields_ = [("reg_type", ctypes.c_int16), ("id", ctypes.c_int16)]

invalid_entity_id = Entity_Id(0, -1)

max_var_loc_components = 6

class Var_Id(ctypes.Structure) :
	_fields_ = [
		("type", ctypes.c_int32), 
		("id", ctypes.c_int32)
	]
	
invalid_var = Var_Id(0, -1)
	
class Time_Step_Size(ctypes.Structure) :
	_fields_ = [
		("unit", ctypes.c_int32),
		("magnitude", ctypes.c_int32)
	]
	
class Mobius_Series_Metadata(ctypes.Structure) :
	_fields_ = [
		("name", ctypes.c_char_p),
		("unit", ctypes.c_char_p)
	]

class Parameter_Value(ctypes.Union) :
	_fields_ = [
		("val_real", ctypes.c_double),
		("val_int", ctypes.c_int64)
	]

class Mobius_Index_Value(ctypes.Structure) :
	_fields_ = [
		("name", ctypes.c_char_p),
		("value", ctypes.c_int64)
	]
	
class Mobius_Index_Slice(ctypes.Structure) :
	_fields_ = [
		("name", ctypes.c_char_p),
		("is_slice", ctypes.c_bool),
		("first", ctypes.c_int64),
		("last", ctypes.c_int64)
	]
	
class Mobius_Index_Range(ctypes.Structure) :
	_fields_ = [
		("first", ctypes.c_int64),
		("last", ctypes.c_int64)
	]

class Mobius_Entity_Metadata(ctypes.Structure) :
	_fields_ = [
		("name", ctypes.c_char_p),
		("unit", ctypes.c_char_p),
		("description", ctypes.c_char_p),
		("min", Parameter_Value),
		("max", Parameter_Value)
	]


# NOTE: Must match Reg_Type: We should find a way to auto-generate this from reg_types.incl instead!
MODULE_TYPE = 1
COMPONENT_TYPE = 2
PARAMETER_TYPE = 3
FLUX_TYPE = 4
PAR_GROUP_TYPE = 6

def load_dll() :
	shared_ext = 'dll'
	if os.name == 'posix' : shared_ext = 'so'
	dll_file = str(pathlib.Path(__file__).parent / ('c_abi.%s'%shared_ext))
	
	dll = ctypes.CDLL(dll_file)
	
	dll.mobius_encountered_error.argtypes = [ctypes.c_char_p, ctypes.c_int64]
	dll.mobius_encountered_error.restype = ctypes.c_int64
		
	dll.mobius_encountered_log.argtypes = [ctypes.c_char_p, ctypes.c_int64]
	dll.mobius_encountered_log.restype = ctypes.c_int64

	dll.mobius_build_from_model_and_data_file.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_bool, ctypes.c_bool]
	dll.mobius_build_from_model_and_data_file.restype  = ctypes.c_void_p

	dll.mobius_delete_application.argtypes = [ctypes.c_void_p]

	dll.mobius_delete_data.argtypes = [ctypes.c_void_p]

	dll.mobius_copy_data.argtypes = [ctypes.c_void_p, ctypes.c_bool]
	dll.mobius_copy_data.restype  = ctypes.c_void_p
	
	dll.mobius_save_data_set.argtypes = [ctypes.c_void_p, ctypes.c_char_p]

	dll.mobius_get_steps.argtypes = [ctypes.c_void_p, ctypes.c_int32]
	dll.mobius_get_steps.restype = ctypes.c_int64

	dll.mobius_run_model.argtypes = [ctypes.c_void_p, ctypes.c_int64]
	dll.mobius_run_model.restype = ctypes.c_bool

	dll.mobius_get_time_step_size.argtypes = [ctypes.c_void_p]
	dll.mobius_get_time_step_size.restype  = Time_Step_Size

	dll.mobius_get_start_date.argtypes = [ctypes.c_void_p, ctypes.c_int32, ctypes.c_char_p]

	dll.mobius_deserialize_entity.argtypes = [ctypes.c_void_p, Entity_Id, ctypes.c_char_p]
	dll.mobius_deserialize_entity.restype  = Entity_Id

	dll.mobius_get_entity.argtypes = [ctypes.c_void_p, Entity_Id, ctypes.c_char_p]
	dll.mobius_get_entity.restype = Entity_Id

	dll.mobius_deserialize_var.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
	dll.mobius_deserialize_var.restype = Var_Id

	dll.mobius_get_flux.argtypes = [ctypes.c_void_p, Entity_Id]
	dll.mobius_get_flux.restype = Var_Id

	dll.mobius_get_var_id_from_list.argtypes = [ctypes.c_void_p, ctypes.POINTER(Entity_Id), ctypes.c_int64]
	dll.mobius_get_var_id_from_list.restype = Var_Id

	dll.mobius_get_special_var.argtypes = [ctypes.c_void_p, Var_Id, Entity_Id, ctypes.c_int16]
	dll.mobius_get_special_var.restype = Var_Id

	dll.mobius_get_series_data.argtypes = [ctypes.c_void_p, Var_Id, ctypes.POINTER(Mobius_Index_Value), ctypes.c_int64, ctypes.POINTER(ctypes.c_double), ctypes.c_int64]

	dll.mobius_set_series_data.argtypes = [ctypes.c_void_p, Var_Id, ctypes.POINTER(Mobius_Index_Value), ctypes.c_int64, ctypes.POINTER(ctypes.c_double), ctypes.POINTER(ctypes.c_int64), ctypes.c_int64]

	dll.mobius_resolve_slice.argtypes = [ctypes.c_void_p, Var_Id, ctypes.POINTER(Mobius_Index_Slice), ctypes.c_int64, ctypes.POINTER(Mobius_Index_Range)]

	dll.mobius_get_series_data_slice.argtypes = [ctypes.c_void_p, Var_Id, ctypes.POINTER(Mobius_Index_Range), ctypes.c_int64, ctypes.POINTER(ctypes.c_double), ctypes.POINTER(ctypes.c_double), ctypes.c_int64]

	dll.mobius_get_series_metadata.argtypes = [ctypes.c_void_p, Var_Id]
	dll.mobius_get_series_metadata.restype = Mobius_Series_Metadata


	dll.mobius_get_index_set_count.argtypes = [ctypes.c_void_p, Entity_Id]
	dll.mobius_get_index_set_count.restype = ctypes.c_int64


	dll.mobius_get_value_type.argtypes = [ctypes.c_void_p, Entity_Id]
	dll.mobius_get_value_type.restype = ctypes.c_int64

	dll.mobius_set_parameter_numeric.argtypes = [ctypes.c_void_p, Entity_Id, ctypes.POINTER(Mobius_Index_Value), ctypes.c_int64, Parameter_Value]

	dll.mobius_get_parameter_numeric.argtypes = [ctypes.c_void_p, Entity_Id, ctypes.POINTER(Mobius_Index_Value), ctypes.c_int64]
	dll.mobius_get_parameter_numeric.restype  = Parameter_Value

	dll.mobius_set_parameter_string.argtypes = [ctypes.c_void_p, Entity_Id, ctypes.POINTER(Mobius_Index_Value), ctypes.c_int64, ctypes.c_char_p]

	dll.mobius_get_parameter_string.argtypes = [ctypes.c_void_p, Entity_Id, ctypes.POINTER(Mobius_Index_Value), ctypes.c_int64]
	dll.mobius_get_parameter_string.restype  = ctypes.c_char_p

	dll.mobius_get_entity_metadata.argtypes = [ctypes.c_void_p, Entity_Id]
	dll.mobius_get_entity_metadata.restype = Mobius_Entity_Metadata

	return dll

dll = load_dll()


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

def _c_str(string) :
	return string.encode('utf-8')

def _pack_index(index) :
	if isinstance(index, str) :
		return Mobius_Index_Value(_c_str(index), -1)
	elif isinstance(index, int) :    #TODO: This may not be sufficient, there are several integer types-
		return Mobius_Index_Value(_c_str(''), index)
	else :
		raise ValueError('Unexpected index type')
	
def _pack_indexes(indexes) :
	
	if isinstance(indexes, list) or isinstance(indexes, tuple) :
		cindexes = [_pack_index(index) for index in indexes]
	else :
		cindexes = [_pack_index(indexes)]    # Just a single index was passed instead of a list/tuple
	
	return (Mobius_Index_Value * len(cindexes))(*cindexes)

def _has_slice(indexes) :
	if isinstance(indexes, list) or isinstance(indexes, tuple) :
		return any(isinstance(index, slice) for index in indexes)
	return isinstance(indexes, slice)
	
def _pack_slice(index) :
	if isinstance(index, str) :
		return Mobius_Index_Slice(_c_str(index), False, 0, 0)
	elif isinstance(index, int) :
		return Mobius_Index_Slice(_c_str(''), False, index, 0)
	elif isinstance(index, slice) :
		if index.start is None : 
			first = ctypes.c_int64(-9223372036854775808)
		else :
			first = ctypes.c_int64(index.start)
		if index.stop is None :
			last  = ctypes.c_int64(-9223372036854775808)
		else :
			last  = ctypes.c_int64(index.stop)
		if not index.step is None :
			raise ValueError('Strides in slices are not yet supported')
		return Mobius_Index_Slice(_c_str(''), True, first, last)
	else :
		raise ValueError('Unexpected index type')

def _pack_slices(indexes) :

	if isinstance(indexes, list) or isinstance(indexes, tuple):
		cindexes = [_pack_slice(index) for index in indexes]
	else :
		cindexes = [_pack_slice(indexes)]    # Just a single index was passed instead of a list/tuple
	
	return (Mobius_Index_Slice * len(cindexes))(*cindexes)
	

def _len(indexes) :
	if isinstance(indexes, list) or isinstance(indexes, tuple) : return len(indexes)
	return 1
	
def _decode_date(val) :
	#TODO: Not sure what types to support here.
	if isinstance(val, str) : return val
	elif isinstance(val, pd.Timestamp) :
		return val.strftime('%Y-%m-%d %H:%M:%S')
	raise ValueError('Unsupported date format')

def _get_par_value(data_ptr, entity_id, indexes) :
	
	type = dll.mobius_get_value_type(data_ptr, entity_id)
	if type <= 2 :
		res = dll.mobius_get_parameter_numeric(data_ptr, entity_id, _pack_indexes(indexes), _len(indexes))
		if type == 0 :
			result = res.val_real
		elif type == 1 :
			result = res.val_int
		elif type == 2 :
			result = (res.val_int == 1)
	elif type <= 4 :
		result = dll.mobius_get_parameter_string(data_ptr, entity_id, _pack_indexes(indexes), _len(indexes))
		if type == 4 :
			result = pd.to_datetime(result.decode('utf-8'))
	else :
		raise ValueError("Unimplemented parameter type")
	_check_for_errors()
	return result
	
def _set_par_value(data_ptr, entity_id, indexes, value) :
	#TODO: Other value types
	type = dll.mobius_get_value_type(data_ptr, entity_id)
	if type <= 2 :
		val = Parameter_Value()
		if type == 0 :
			val.val_real = value
		else :
			val.val_int = value
		dll.mobius_set_parameter_numeric(data_ptr, entity_id, _pack_indexes(indexes), _len(indexes), val)
	elif type <= 4 :
		# TODO: If argument is of datetime type, decode it first
		if type == 4 :
			str_val = _c_str(_decode_date(value))
		else :
			str_val = _c_str(value)
		dll.mobius_set_parameter_string(data_ptr, entity_id, _pack_indexes(indexes), _len(indexes), str_val)
	else :
		raise ValueError("Unimplemented parameter type")
	_check_for_errors()
	
def is_valid(id) :
	if isinstance(id, Entity_Id) :
		return id.reg_type > 0 and id.id >= 0
	if isinstance(id, Var_Id) :
		return id.id >= 0
		
def mobius2_path() :
	#NOTE: We have to add a trailing slash to the path for Mobius2 to understand it.
	return f'{pathlib.Path(__file__).parent.resolve().parent}{os.sep}'

class Scope :
	def __init__(self, data_ptr, scope_id) :
		self.data_ptr = data_ptr
		self.scope_id = scope_id
	
	# TODO: If either of these are a flux, it should return a State_Var
	def __getitem__(self, serial_name) :
		entity_id = dll.mobius_deserialize_entity(self.data_ptr, self.scope_id, _c_str(serial_name))
		_check_for_errors()
		if not is_valid(entity_id) :
			raise ValueError('The serial name "%s" does not refer to a valid entity' % serial_name)
		return Entity(self.data_ptr, self.scope_id, entity_id)

	def __getattr__(self, identifier) :
		entity_id = dll.mobius_get_entity(self.data_ptr, self.scope_id, _c_str(identifier))
		if not is_valid(entity_id) :
			raise ValueError("The identifier '%s' does not refer to a valid entity" % identifier)
		_check_for_errors()
		return Entity(self.data_ptr, self.scope_id, entity_id)
		
	#def get(self, identifier) :
	#	return self.__getattr_(identifier)
		
	def __setattr__(self, identifier, value) :
		if identifier in ['data_ptr', 'scope_id', 'entity_id', 'superscope_id', 'is_main'] :
			super().__setattr__(identifier, value)
		else :
			self.__getattr__(identifier).__setitem__((), value)
	
	def list_all(self, type) :
		# TODO
		pass

class Model_Application(Scope) :
	def __init__(self, data_ptr, is_main) :
		super().__init__(data_ptr, invalid_entity_id)
		self.is_main = is_main
		
	
	def __del__(self) :
		#TODO: If we have made copies and the main is deleted, the copies should be invalidated somehow.
		if self.is_main :
			dll.mobius_delete_application(self.data_ptr)
		else :
			dll.mobius_delete_data(self.data_ptr)
	
	def __enter__(self) :
		return self
	
	def __exit__(self, type, value, tb) :
		self.__del__()
	
	@classmethod
	def build_from_model_and_data_file(cls, model_file, data_file, store_all_series=False, dev_mode=False) :
		base_path = mobius2_path()
		data_ptr = dll.mobius_build_from_model_and_data_file(_c_str(model_file), _c_str(data_file), _c_str(base_path), store_all_series, dev_mode)
		_check_for_errors()
		return cls(data_ptr, True)
	
	def copy(self, copy_results = False) :
		new_ptr = dll.mobius_copy_data(self.data_ptr, copy_results)
		return Model_Application(new_ptr, False)
		
	def run(self, ms_timeout=-1) :
		finished = dll.mobius_run_model(self.data_ptr, ms_timeout)
		_check_for_errors()
		return finished
		
	def save_data_set(self, file_name) :
		dll.mobius_save_data_set(self.data_ptr, _c_str(file_name))
		_check_for_errors()
		
	def var(self, serial_name) :
		var_id = dll.mobius_deserialize_var(self.data_ptr, _c_str(serial_name))
		if not is_valid(var_id) :
			raise ValueError('The serial name "%s" does not refer to a valid state variable or series.' % serial_name)
		_check_for_errors()
		return State_Var(self.data_ptr, invalid_entity_id, [], var_id) #TODO: Should maybe retrieve the entity id list from loc1 (if relevant)?
	
		
class Entity(Scope) :
	def __init__(self, data_ptr, scope_id, entity_id) :		
		self.entity_id = entity_id
		self.superscope_id = scope_id
		
		if entity_id.reg_type == MODULE_TYPE :
			Scope.__init__(self, data_ptr, entity_id)
		else :
			Scope.__init__(self, data_ptr, scope_id)
			
	def __getitem__(self, name_or_indexes) :
		if self.entity_id.reg_type == MODULE_TYPE or self.entity_id.reg_type == PAR_GROUP_TYPE :
			return Scope.__getitem__(self, name_or_indexes)
		elif self.entity_id.reg_type == PARAMETER_TYPE :
			return _get_par_value(self.data_ptr, self.entity_id, name_or_indexes)
		else :
			raise ValueError("This entity can't be accessed using []")
	
	def __setitem__(self, name_or_indexes, value) :
		if self.entity_id.reg_type == PARAMETER_TYPE :
			_set_par_value(self.data_ptr, self.entity_id, name_or_indexes, value)
		else :
			raise ValueError("This entity can't be accessed using []")
	
	def __getattr__(self, identifier) :
		if self.entity_id.reg_type == MODULE_TYPE :
			return Scope.__getattr__(self, identifier)
		elif self.entity_id.reg_type == COMPONENT_TYPE :
			scope = Scope(self.data_ptr, self.superscope_id)
			other = scope.__getattr__(identifier)
			return State_Var.from_id_list(self.data_ptr, self.superscope_id, [self.entity_id, other.entity_id])
		else :
			raise ValueError("This entity doesn't have accessible fields.")

	def name(self) :
		data = dll.mobius_get_entity_metadata(self.data_ptr, self.entity_id)
		_check_for_errors()
		return data.name.decode('utf-8')
	
	def min(self) :
		if self.entity_id.reg_type != PARAMETER_TYPE :
			raise ValueError("This entity doesn't have a min value.")
		type = dll.mobius_get_value_type(self.data_ptr, entity_id)
		if type > 1 :
			raise ValueError("This parameter does not have a min value.")
		data = dll.mobius_get_entity_metadata(self.data_ptr, self.entity_id)
		_check_for_errors()
		if type == 0 :
			return data.min.val_real
		else :
			return data.min.val_int
		
	def max(self) :
		if self.entity_id.reg_type != PARAMETER_TYPE :
			raise ValueError("This entity doesn't have a max value.")
		type = dll.mobius_get_value_type(self.data_ptr, self.entity_id)
		if type > 1 :
			raise ValueError("This parameter does not have a max value.")
		data = dll.mobius_get_entity_metadata(self.data_ptr, self.entity_id)
		_check_for_errors()
		if type == 0 :
			return data.max.val_real
		else :
			return data.max.val_int
			
	# TODO: default(self) :
		
	def description(self) :
		if self.entity_id.reg_type != PARAMETER_TYPE :
			raise ValueError("This entity doesn't have a description.")
		data = dll.mobius_get_entity_metadata(self.data_ptr, self.entity_id)
		_check_for_errors()
		return data.description.decode('utf-8')
		
	def unit(self) :
		if self.entity_id.reg_type != PARAMETER_TYPE :
			raise ValueError("This entity doesn't have a unit.")
		data = dll.mobius_get_entity_metadata(self.data_ptr, self.entity_id)
		_check_for_errors()
		return data.unit.decode('utf-8')

class State_Var :
	
	@classmethod
	def from_id_list(cls, data_ptr, scope_id, id_list) :
		if len(id_list) > max_var_loc_components :
			raise ValueError("Too many chained component to a variable location. The maximum is %d" % max_var_loc_components)
		lst = (Entity_Id * len(id_list))(*id_list)
		var_id = dll.mobius_get_var_id_from_list(data_ptr, lst, len(id_list))
		if not is_valid(var_id) :
			raise ValueError("This does not refer to a valid state variable")
		_check_for_errors()
		return State_Var(data_ptr, scope_id, id_list, var_id)
	
	def __init__(self, data_ptr, scope_id, id_list, var_id) :
		self.data_ptr = data_ptr
		self.scope_id = scope_id
		self.id_list = id_list
		self.var_id = var_id
		
	def __getitem__(self, indexes) :
		time_steps = dll.mobius_get_steps(self.data_ptr, self.var_id.type)
		
		start_date = ctypes.create_string_buffer(32)
		dll.mobius_get_start_date(self.data_ptr, self.var_id.type, start_date)
		start_date = pd.to_datetime(start_date.value.decode('utf-8'))
		_check_for_errors()

		step_size = dll.mobius_get_time_step_size(self.data_ptr)
		step_type = 's' if step_size.unit == 0 else 'MS'
		freq='%d%s' % (step_size.magnitude, step_type)
		
		if _has_slice(indexes) :
			dates = pd.date_range(start=start_date, periods=time_steps+1, freq=freq)
			ilen = _len(indexes)
			ranges = (Mobius_Index_Range * ilen)()
			dll.mobius_resolve_slice(self.data_ptr, self.var_id, _pack_slices(indexes), ilen, ranges)
			_check_for_errors()
			
			dim = time_steps
			idx_dim = 1
			for rn in ranges :
				length = (rn.last - rn.first)
				dim *= length
				idx_dim *= length
			idx_dim+=1 # This is because we want the boundary positions of the indexes.
		
			series = (ctypes.c_double * dim)()
			idx_pos = (ctypes.c_double * idx_dim)()
			dll.mobius_get_series_data_slice(self.data_ptr, self.var_id, ranges, ilen, idx_pos, series, time_steps)
			_check_for_errors()
			
			data = np.array(series, copy=False)
			dims = (time_steps,)
			for rn in ranges :
				dm = int(rn.last - rn.first)
				if dm > 1 :
					dims += (dm,)
			return np.reshape(data, dims), np.array(idx_pos, copy=False), dates
			
		else :
			dates = pd.date_range(start=start_date, periods=time_steps, freq=freq)
		
			series = (ctypes.c_double * time_steps)()
			dll.mobius_get_series_data(self.data_ptr, self.var_id, _pack_indexes(indexes), _len(indexes), series, time_steps)
			_check_for_errors()

			# We have to do this, otherwise some operations on it crashes:
			date_idx = pd.Series(data = dates, name = 'Date')
			
			return pd.Series(data=np.array(series, copy=False), index=date_idx, name=self.name())
		
		# Would also be nice to eventually allow list slices or masks like
		#    data, dates = app.layer.water.temp[["Drammensfjorden", "Breiangen"], 0]
		# Although that should maybe return a pd.DataFrame with those two as different columns instead (?). In that case it could not be combined with slices.
	
	def __setitem__(self, indexes, values) :
		
		if _has_slice(indexes) :
			raise ValueError("Slices not yet supported for setting input series")
		
		if not isinstance(values, pd.Series) :
			raise ValueError("Expected a pandas Series object for the values")
		
		time_steps = len(values)
		dates = (ctypes.c_int64 * time_steps)(*(ts.astype('datetime64[s]').astype(np.int64) for ts in values.index.values))
		# alternatively ts.astype(np.timedelta64) / np.timedelta64(1, 's')
		series = (ctypes.c_double * time_steps)(*values.values)
		
		dll.mobius_set_series_data(self.data_ptr, self.var_id, _pack_indexes(indexes), _len(indexes), series, dates, time_steps)
		_check_for_errors()
		
	def __getattr__(self, identifier) :
		# TODO: Should better check what type of variable this is (quantity, flux, special ..)
		scope = Scope(self.data_ptr, self.scope_id)
		if len(self.id_list) > 0 :
			other = scope.__getattr__(identifier)
			new_list = self.id_list + [other.entity_id]
			return State_Var.from_id_list(self.data_ptr, self.scope_id, new_list)
		else :
			quant = scope.__getattr__(identifier)
			carry_id = dll.mobius_get_special_var(self.data_ptr, self.var_id, quant.entity_id, 4)
			_check_for_errors()
			if not is_valid(carry_id) :
				raise ValueError("This variable does not carry this quantity.")
			return State_Var(self.data_ptr, self.scope_id, [], carry_id)
			
	def conc(self) :
		conc_id = dll.mobius_get_special_var(self.data_ptr, self.var_id, invalid_entity_id, 5)
		_check_for_errors()
		if not is_valid(conc_id) :
			raise ValueError("This variable does not have a concentration.")
		return State_Var(self.data_ptr, self.scope_id, [], conc_id)		

	def name(self) :
		data = dll.mobius_get_series_metadata(self.data_ptr, self.var_id)
		_check_for_errors()
		return data.name.decode('utf-8')
		
	def unit(self) :
		data = dll.mobius_get_series_metadata(self.data_ptr, self.var_id)
		_check_for_errors()
		return data.unit.decode('utf-8')