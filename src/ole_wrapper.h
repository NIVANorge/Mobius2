
#ifndef MOBIUS_OLE_WRAPPER_H
#define MOBIUS_OLE_WRAPPER_H

#if defined(_WIN32) || defined(_MSC_VER)

#include "lexer.h"
#define NOMINMAX
#include <windows.h>

#define OLE_AVAILABLE 1

struct OLE_Handles {
	IDispatch *app = nullptr;
	IDispatch *books = nullptr;
	IDispatch *book = nullptr;
	IDispatch *sheet = nullptr;
	String_View file_path;
};

void
ole_get_string(VARIANT *var, char *buf_out, size_t buf_len);

bool
ole_get_date(VARIANT *var, Date_Time *date_out);

double
ole_get_double(VARIANT *var);

void
ole_close_spreadsheet(OLE_Handles *handles);

void
ole_close_app_and_spreadsheet(OLE_Handles *handles);

void
ole_close_due_to_error(OLE_Handles *handles, int tab = -1, int col = -1, int row = -1);

void
ole_open_spreadsheet(String_View file_path, OLE_Handles *handles);

struct
OLE_Matrix {
	VARIANT var;
	int base_row;
	int base_col;
};

//OLE_Matrix;
//ole_create_matrix(int dim_x, int dim_y);

bool
ole_destroy_matrix(OLE_Matrix *matrix);

OLE_Matrix
ole_get_range_matrix(int from_row, int to_row, int from_col, int to_col, OLE_Handles *handles);

VARIANT
ole_get_matrix_value(OLE_Matrix *matrix, int row, int col, OLE_Handles *handles);

int
ole_get_num_tabs(OLE_Handles *handles);

void
ole_select_tab(OLE_Handles *handles, int tab);

VARIANT
ole_get_cell_value(OLE_Handles *handles, int col, int row);

#else  // _WIN32

#define OLE_AVAILABLE 0

#endif // _WIN32

#endif // MOBIUS_OLE_WRAPPER_H