
#include "ole_wrapper.h"

#if OLE_AVAILABLE

#include "lexer.h"

bool
ole_auto_wrap(int auto_type, VARIANT *pv_result, IDispatch *p_disp, const wchar_t *name, int c_args, ...) {
    // Begin variable-argument list...
    va_list marker;
    va_start(marker, c_args);

    if(!p_disp) {
		begin_error(Mobius_Error::ole);
        error_print("NULL idispatch passed to ole_auto_wrap() failed.");
		return false;
	}

    // Variables used...
    DISPPARAMS dp = { NULL, NULL, 0, 0 };
    DISPID dispid_named = DISPID_PROPERTYPUT;
    DISPID disp_id;
    HRESULT hr;

    // Get DISPID for name passed...
    hr = p_disp->GetIDsOfNames(IID_NULL, (wchar_t**)&name, 1, LOCALE_USER_DEFAULT, &disp_id);
    if(FAILED(hr)) {
		begin_error(Mobius_Error::ole);
        error_print("ole_auto_wrap() failed.");
		return false;
	}

    // Allocate memory for arguments...
    VARIANT *p_args = new VARIANT[c_args+1];
    // Extract arguments...
    for(int i = 0; i < c_args; i++) {
        p_args[i] = va_arg(marker, VARIANT);
    }

    // Build DISPPARAMS
    dp.cArgs = c_args;
    dp.rgvarg = p_args;

    // Handle special-case for property-puts!
    if(auto_type & DISPATCH_PROPERTYPUT) {
        dp.cNamedArgs = 1;
        dp.rgdispidNamedArgs = &dispid_named;
    }

    // Make the call!
    hr = p_disp->Invoke(disp_id, IID_NULL, LOCALE_SYSTEM_DEFAULT, auto_type, &dp, pv_result, NULL, NULL);
    if(FAILED(hr)) {
        begin_error(Mobius_Error::ole);
        error_print("ole_auto_wrap() failed.");
		return false;
	}
	
    // End variable-argument section...
    va_end(marker);

    delete [] p_args;

    return true;
}

VARIANT
ole_new_variant() {
	VARIANT result;
	VariantInit(&result);
	VariantClear(&result);
	return result;
}


IDispatch *
ole_create_object(const wchar_t *Name) {
	CLSID clsid;
	HRESULT hr = CLSIDFromProgID(Name, &clsid);	// Get CLSID for our server
	if(FAILED(hr)) {
		begin_error(Mobius_Error::ole);
        error_print("CLSIDFromProgID() failed.");
		return nullptr;
	}
	IDispatch *app;
	// Start server and get IDispatch
	hr = CoCreateInstance(clsid, NULL, CLSCTX_LOCAL_SERVER, IID_IDispatch, (void **)&app);
	if(FAILED(hr)) {
		begin_error(Mobius_Error::ole);
        error_print("Application Excel is not registered.");
		return nullptr;
	}
	return app;
}

IDispatch *
ole_get_object(IDispatch *from, const wchar_t *which) {
	VARIANT result = ole_new_variant();
	bool success = ole_auto_wrap(DISPATCH_PROPERTYGET, &result, from, which, 0);
	if(!success)
		return nullptr;
	return result.pdispVal;
}

IDispatch *
ole_get_object(IDispatch *from, const wchar_t *which, VARIANT *value) {
	VARIANT result = ole_new_variant();
	bool success = ole_auto_wrap(DISPATCH_PROPERTYGET, &result, from, which, 1, *value);
	if(!success)
		return nullptr;
	return result.pdispVal;
}

IDispatch *
ole_get_object(IDispatch *from, const wchar_t *which, VARIANT *value1, VARIANT *value2) {
	VARIANT result = ole_new_variant();
	bool success = ole_auto_wrap(DISPATCH_PROPERTYGET, &result, from, which, 2, *value1, *value2);
	if(!success)
		return nullptr;
	return result.pdispVal;
}

VARIANT
ole_get_value(IDispatch *from, const wchar_t *which) {
	VARIANT result = ole_new_variant();
	bool success = ole_auto_wrap(DISPATCH_PROPERTYGET|DISPATCH_METHOD, &result, from, which, 0);
	if(!success)
		result.vt = VT_EMPTY;
	
	return result;
}

bool
ole_method(IDispatch *from, const wchar_t *which) {
	VARIANT result;
	VariantInit(&result);
	return ole_auto_wrap(DISPATCH_METHOD, &result, from, which, 0);
}

bool
ole_set_value(IDispatch *from, const wchar_t *which, VARIANT *value) {
	return ole_auto_wrap(DISPATCH_PROPERTYPUT, NULL, from, which, 1, *value);
}

VARIANT
ole_string_variant(const char *name) {
	VARIANT var = ole_new_variant();
	
	WCHAR name2[1024*sizeof(WCHAR)];

	MultiByteToWideChar(CP_UTF8, 0, name, -1, name2, sizeof(name2)/sizeof(name2[0]));
	var.vt = VT_BSTR;
	var.bstrVal = SysAllocString(name2);
	
	return var;
}

void
ole_destroy_string(VARIANT *string) {
	SysFreeString(string->bstrVal);
	string->bstrVal = nullptr;
}

VARIANT
ole_int4_variant(int value) {
	VARIANT var = ole_new_variant();
	var.vt = VT_I4;
	var.lVal = value;
	return var;
}

void
ole_get_string(VARIANT *var, char *buf_out, size_t buf_len) {
	buf_out[0] = 0;
	if(var->vt == VT_BSTR) {
		WideCharToMultiByte(CP_ACP, 0, var->bstrVal, -1, buf_out, buf_len, NULL, NULL);
		ole_destroy_string(var); //Hmm, do we want to do this here?
	}
}

char *
col_row_to_cell(int col, int row, char *buf) {
	int num_A_Z = 'Z' - 'A' + 1;
	int n_col = col;
	while (n_col > 0) {
		int letter = n_col/num_A_Z;
		if (letter == 0) {
			letter = n_col;
			*buf = char('A' + letter - 1);
			buf++;
			break;
		} else {
			n_col -= letter*num_A_Z;
			*buf = char('A' + letter - 1);
			buf++;
		}
	}
	itoa(row, buf, 10)+1;   //TODO: the +1 doesn't do anything? Remove.
	while(*buf != 0) ++buf;
	return buf;
}

void
ole_close_spreadsheet(OLE_Handles *handles) {
	if(handles->sheet) handles->sheet->Release();
	handles->sheet = nullptr;
	if(handles->book) handles->book->Release();
	handles->book = nullptr;
	if(handles->books) handles->books->Release();
	handles->books = nullptr;
}

void
ole_close_app_and_spreadsheet(OLE_Handles *handles) {
	bool app_open = handles->app;
	
	if(handles->app)
		ole_auto_wrap(DISPATCH_METHOD, NULL, handles->app, L"Quit", 0);
	
	ole_close_spreadsheet(handles);
	
	if(handles->app) handles->app->Release();
	handles->app = nullptr;

	if(app_open) {
		OleUninitialize();
		// Uninitialize COM for this thread...
		CoUninitialize();
	}
}

void
ole_close_due_to_error(OLE_Handles *handles, int tab, int col, int row) {
	begin_error(Mobius_Error::ole);
	error_print("In file \"", handles->file_path, "\" ");
	if(tab >= 0) {
		VARIANT var_id = ole_int4_variant(tab + 1);
		IDispatch *sheet = ole_get_object(handles->app, L"Sheets", &var_id);
		if(sheet) {
			VARIANT name = ole_get_value(sheet, L"Name");
			char buf[512];
			ole_get_string(&name, buf, 512);
			error_print("tab \"", buf, "\" ");
			sheet->Release();
		}
	}
	if(col >= 1 && row >= 1) {
		char buf[32];
		col_row_to_cell(col, row, &buf[0]);
		error_print("cell ", buf);
	}
	ole_close_app_and_spreadsheet(handles);
}

void
ole_open_spreadsheet(String_View file_path, OLE_Handles *handles) {
	handles->file_path = file_path;
	
	if(!handles->app) {
		OleInitialize(NULL);
		//Initialize COM for this thread
		CoInitialize(NULL);
		
		handles->app = ole_create_object(L"Excel.Application");
		if(!handles->app) {
			ole_close_due_to_error(handles);
			fatal_error("Failed to open Excel application.");
		}
		
		handles->books = ole_get_object(handles->app, L"Workbooks");
		if(!handles->books) {
			ole_close_due_to_error(handles);
			fatal_error("Failed to initialize excel workbooks.");
		}
	}
	
	// NOTE: The "Open" command of the Excel workbooks object can only take full paths.
	char full_path[_MAX_PATH];
	std::string path(file_path.data, file_path.data + file_path.count); // This is just to get something that is 0-terminated
	if(_fullpath(full_path, path.data(), _MAX_PATH ) == NULL) {
		ole_close_app_and_spreadsheet(handles);
		fatal_error(Mobius_Error::ole, "Unable to convert the relative path \"", file_path, "\" to a full path.");
	}
	
	VARIANT file_var = ole_string_variant(full_path);
	
	handles->book = ole_get_object(handles->books, L"Open", &file_var);
	if(!handles->book) {
		ole_close_due_to_error(handles);
		fatal_error("Failed to open file \"", file_path, "\".");
	}
	
	// TODO: this one should be unnecessary since we explicitly open other sheets later?
	handles->sheet = ole_get_object(handles->book, L"ActiveSheet");
	if(!handles->sheet) {
		ole_close_due_to_error(handles);
		fatal_error("Failed to open excel active sheet.");
	}
	
	ole_destroy_string(&file_var);
	
	//Don't make this open the excel application in a way that is visible to the user.
	// TODO: why is this set here and not earlier?
	VARIANT visible = ole_int4_variant(0);
	ole_set_value(handles->app, L"Visible", &visible);
}

/*
OLE_Matrix
ole_create_matrix(int dim_x, int dim_y) {
	OLE_Matrix result;
	result.var = ole_new_variant();
	result.base_row = 1;
	result.base_col = 1;
	result.var.vt = VT_ARRAY | VT_VARIANT;
    SAFEARRAYBOUND sab[2];
	sab[0].lLbound = 1; sab[0].cElements = dim_y;
	sab[1].lLbound = 1; sab[1].cElements = dim_x;
	result.var.parray = SafeArrayCreate(VT_VARIANT, 2, sab);
	
	return result;
}
*/

bool
ole_destroy_matrix(OLE_Matrix *matrix) {
	bool success = (SafeArrayDestroy(matrix->var.parray) == S_OK);
	matrix->var.parray = nullptr;
	return success;
}

OLE_Matrix
ole_get_range_matrix(int from_row, int to_row, int from_col, int to_col, OLE_Handles *handles) {
	IDispatch *range;
	char range_buf[256];
	char *range0 = &range_buf[0];
	range0 = col_row_to_cell(from_col, from_row, range0);
	*range0 = ':';
	col_row_to_cell(to_col, to_row, range0+1);
	//warning_print("range is ", range_buf, "\n");
	
	VARIANT range_string = ole_string_variant(range_buf);
	range = ole_get_object(handles->sheet, L"Range", &range_string);
	if(!range) {
		ole_close_due_to_error(handles);
		fatal_error("Failed to select range ", range_buf, ".");
	}
	
	OLE_Matrix result;
	result.var = ole_get_value(range, L"Value");
	result.base_row = from_row;
	result.base_col = from_col;
	
	ole_destroy_string(&range_string);
	range->Release();
	
	return result;
}

VARIANT
ole_get_matrix_value(OLE_Matrix *matrix, int row, int col, OLE_Handles *handles) {
	VARIANT result = ole_new_variant();
	
	int rel_row = row - matrix->base_row + 1;
	int rel_col = col - matrix->base_col + 1;

	long indices[2] = {rel_row, rel_col};
	HRESULT hr = SafeArrayGetElement(matrix->var.parray, indices, (void *)&result);
	if(hr != S_OK) {
		ole_close_due_to_error(handles);
		fatal_error("Error when indexing matrix. Row ", row, " Col ", col, ". Relative to matrix: Row ", rel_row, " Col ", rel_col, ".");
	}
	
	return result;
}

bool
ole_get_date(VARIANT *var, Date_Time *date_out) {
	Date_Time result;
	if(var->vt == VT_DATE) {
		SYSTEMTIME stime;
     	VariantTimeToSystemTime(var->date, &stime);
		
		bool success;
     	*date_out = Date_Time(stime.wYear, stime.wMonth, stime.wDay, &success);
		success = success && date_out->add_timestamp(stime.wHour, stime.wMinute, stime.wSecond);
		return success;
	} else if(var->vt == VT_BSTR) {
		char buf[256];
		WideCharToMultiByte(CP_ACP, 0, var->bstrVal, -1, buf, 256, NULL, NULL);
		Token_Stream stream("", buf);
		stream.allow_date_time_tokens = true;
		// TODO: this could give errors if there is a misformatted token, and it won't print location correctly. We should make it possible to turn off errors in the Token_Stream and just have it return a token of unrecognized type when that happens.
		Token token = stream.peek_token();
		if(token.type == Token_Type::date) {
			*date_out = stream.expect_datetime();
			return true;
		}
		return false;
	}
	return false;
}

#include "../third_party/fast_double_parser/fast_double_parser.h"

double
ole_get_double(VARIANT *var) {
	double result = std::numeric_limits<double>::quiet_NaN();
	
	//TODO: Do we need to handle other types?
	if(var->vt == VT_I2)
		result = (double) var->iVal;
	else if(var->vt == VT_I4)
		result = (double) var->lVal;
	else if(var->vt == VT_R4)
		result = (double) var->fltVal;
	else if(var->vt == VT_R8)
		result = var->dblVal;
	else if(var->vt == VT_BSTR) {
		char buf[256];
		WideCharToMultiByte(CP_ACP, 0, var->bstrVal, -1, buf, 256, NULL, NULL);
		char *c = &buf[0];
		while(*c != 0) {
			if(*c == ',') *c = '.';   //NOTE: Replace ',' with '.' in case of nonstandard formats.
			++c;
		}
		//int count = sscanf(buf, "%f", &result);
		const char *endptr = fast_double_parser::parse_number(buf, &result);
		if(!endptr) result = std::numeric_limits<double>::quiet_NaN();
		ole_destroy_string(var);
	}
	
	return result;
}

int
ole_get_num_tabs(OLE_Handles *handles) {
	IDispatch *sheets = ole_get_object(handles->app, L"Sheets");
	VARIANT count = ole_get_value(sheets, L"Count");
	sheets->Release();
	return count.lVal;
}

void
ole_select_tab(OLE_Handles *handles, int tab) {
	VARIANT var_id = ole_int4_variant(tab + 1);
	handles->sheet = ole_get_object(handles->app, L"Sheets", &var_id);
	if(!handles->sheet) {
		ole_close_due_to_error(handles);
		fatal_error("Failed to get Sheet object for tab number ", tab, ".");
	}
	bool success = ole_method(handles->sheet, L"Select");
	if(!success) {
		ole_close_due_to_error(handles);
		fatal_error("Failed to select tab ", tab, ".");
	}
}

VARIANT
ole_get_cell_value(OLE_Handles *handles, int col, int row) {
	VARIANT x = ole_int4_variant(col);
	VARIANT y = ole_int4_variant(row);
	
	IDispatch *cell = ole_get_object(handles->sheet, L"Cells", &x, &y);
	VARIANT var = ole_get_value(cell, L"Value");
	
	cell->Release();
	
	return var;
}

#endif // OLE_AVAILABLE
