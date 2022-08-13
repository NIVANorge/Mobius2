
import ctypes

#NOTE: Just sketching out for now. It is not implemented fully yet.

dll = ctypes.CDLL("../test/c_api.dll")

# Volatile! These structure must match the corresponding in the c++ code
class Entity_Id(ctypes.Structure) :
	_fields_ = [("module_id", ctypes.c_int16), ("reg_type", ctypes.c_int16), ("id", ctypes.c_int32)]

class DLL_Parameter_Reference(ctypes.Structure) :
	_fields_ = [("par_id", Entity_Id), ("val_type", ctypes.c_int32)]

dll.c_api_build_from_model_and_data_file.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
dll.c_api_build_from_model_and_data_file.restype  = ctypes.c_void_p

dll.c_api_get_module_reference_by_name.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
dll.c_api_get_module_reference_by_name.restype  = ctypes.c_void_p

dll.c_api_get_parameter_reference_by_handle_name.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
dll.c_api_get_parameter_reference_by_handle_name.restype = DLL_Parameter_Reference

dll.c_api_set_parameter_real.argtypes = [ctypes.c_void_p, DLL_Parameter_Reference, ctypes.POINTER(ctypes.c_char_p), ctypes.c_int64, ctypes.c_double]

dll.c_api_get_parameter_real.argtypes = [ctypes.c_void_p, DLL_Parameter_Reference, ctypes.POINTER(ctypes.c_char_p), ctypes.c_int64]
dll.c_api_get_parameter_real.restype  = ctypes.c_double


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
		
	#TODO: Also want to use __getattr__ to look up module, so it has to be able to get a structure back that could be either, then decide on what to construct.
		
	def __getattr__(self, compartment_name) :
		#id = dll.dll_get_compartment_id_by_handle_name(self.ptr, _c_str(compartment_name))
		pass
		
		
class Module :
	def __init__(self, ptr, app_ptr) :
		self.ptr     = ptr
		self.app_ptr = app_ptr
		
	def __getattr__(self, handle_name) :
		par_ref = dll.c_api_get_parameter_reference_by_handle_name(self.ptr, _c_str(handle_name))
		#TODO: check if it is valid
		return Parameter(self.app_ptr, par_ref)
	
class Parameter :
	def __init__(self, app_ptr, par_ref) :
		self.app_ptr    = app_ptr
		self.par_ref    = par_ref
		
	def __getitem__(self, indexes) :
		if not isinstance(indexes, list) :
			raise RuntimeError("Expected a list object for the indexes")
		return dll.c_api_get_parameter_real(self.app_ptr, self.par_ref, _pack_indexes(indexes), len(indexes))
	
	def __setitem__(self, indexes, value) :
		if not isinstance(indexes, list) :
			raise RuntimeError("Expected a list object for the indexes")
		dll.c_api_set_parameter_real(self.app_ptr, self.par_ref, _pack_indexes(indexes), len(indexes), value)
		
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
		
class Compartment :
	def __init__(self, app_ptr, id) :
		self.app_ptr = app_ptr
		self.id = id
		
	def __getattr__(self, handle_name) :
		return 0 #TODO look up series or state_var  . Actually has to be a value location or something like that, which maps to a series or state_var.
		
class Series :
	def __init__(self, app_ptr, id, type) :
		pass
	# __getitem__  __setitem___ etc
		




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