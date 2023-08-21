
#include "ole_wrapper.h"

#if OLE_AVAILABLE

#include "data_set.h"

#include <limits>

void
read_series_data_from_spreadsheet(Data_Set *data_set, OLE_Handles *handles, String_View file_name) {
	
	int n_tabs = ole_get_num_tabs(handles);
	
	constexpr size_t buf_size = 512;
	char buf[buf_size];
	
	for(int tab = 0; tab < n_tabs; ++tab) {
		ole_select_tab(handles, tab);
		
		VARIANT A1 = ole_get_cell_value(handles, 1, 1);
		ole_get_string(&A1, buf, buf_size);
		bool skip_tab = (strcmp(buf, "NOREAD") == 0);
		if(skip_tab) continue;
		
		data_set->series.push_back({});
		Series_Set_Info &data = data_set->series.back();
		data.has_date_vector = true;
		data.file_name = std::string(file_name);
		
		std::vector<Data_Id> index_sets;
		
		int search_len = 128; //NOTE: We only search for index sets among the first 128 rows since anything more than that would be ridiculous.
		auto matrix = ole_get_range_matrix(2, search_len + 1, 1, 1, handles);
		
		int potential_flag_row = -1;
		int first_date_row = -1;
		
		// ***** parse the first column to see what index sets are used and on what row there are flags and where the date column starts
		
		for(int row = 0; row < search_len; ++row) {
			VARIANT value = ole_get_matrix_value(&matrix, row+2, 1, handles);

			if(value.vt == VT_DATE) {
				first_date_row = row+2;
				break;
			}
			
			ole_get_string(&value, buf, buf_size);
			
			if(strlen(buf) > 0) {
				// Check if this is a date that was formatted as a string (especially important since Excel can't format dates before 1900 as other than strings).
				//TODO: couldn't we instead call ole_get_date() above though and check for success?
				if(sscanf(buf, "%d-%d-%d") == 3) {
					first_date_row = row+2;
					break;
				}
				
				// Otherwise, if it is some non-date string we assume it to be the name of an index set.
				auto index_set_id = data_set->index_sets.find_idx(buf);
				if(!is_valid(index_set_id)) {
					ole_close_due_to_error(handles, tab, 1, row+2);
					fatal_error("The index set ", buf, " was not previously declared in the data set.");
				}
				index_sets.push_back(index_set_id);
			} else {
				// Empty row (or at least it did not have date or string format (TODO: check if there is some other data here).
				// This row could have flags for the time series
				if(potential_flag_row > 0) {
					ole_close_due_to_error(handles, tab, 1, row+2);
					fatal_error("There should not be empty cells in column A except in row 1, or potentially in the row right above the dates, or at the end.");
				}
				potential_flag_row = row+2;
			}
		}
		
		ole_destroy_matrix(&matrix);
		
		int row_range = (int)(1+index_sets.size());
		if(potential_flag_row > 0) row_range = potential_flag_row;
		search_len = 256;
		
		//TODO: Ideally also do this in a loop in case there are more than 256 columns!
		matrix = ole_get_range_matrix(1, row_range, 2, 1 + search_len, handles);
		
		// ********* Parse the header data
		
		std::string current_input_name = "";
		for(int col = 0; col < search_len; ++col) {
			VARIANT name = ole_get_matrix_value(&matrix, 1, col+2, handles);
			
			bool got_name_this_column = false;
			
			ole_get_string(&name, buf, buf_size);
			if(strlen(buf) > 0) {
				got_name_this_column = true;
				current_input_name = buf;
			} else if (current_input_name == "") {
				ole_close_due_to_error(handles, tab, 2, 1);
				fatal_error("Missing an input name.");
			}
			
			std::vector<std::string> index_names_str(index_sets.size()); // This is needed to have temporary storage for the data...
			std::vector<Token> index_names;
			for(int row = 0; row < index_sets.size(); ++row) {
				
				auto index_set = data_set->index_sets[index_sets[row]];
				VARIANT index_name = ole_get_matrix_value(&matrix, row+2, col+2, handles);
				
				auto type = index_set->get_type(0); // TODO: This currently only works because index name convention is the same for all sub-indexing. But that is maybe something we should enforce any way.
				
				//TODO: This should be done properly instead with a "ole_get_token(VARIANT*)" function or something like that which properly checks the type itself.
				//   That should actually be the main way to read matrix values since that would streamline other parts of the code too.
				Token token = {};
				token.type = Token_Type::unknown;
				token.source_loc.type = Source_Location::Type::spreadsheet;
				token.source_loc.tab = tab;
				token.source_loc.line = row+2;
				token.source_loc.column = col+2;
				token.source_loc.filename = file_name;
				bool empty = false;
				
				// TODO: Could maybe check for emptyness in a more failsafe way.
				
				if(type == Sub_Indexing_Info::Type::numeric1) {
					token.val_int = ole_get_int(&index_name);
					if(token.val_int == std::numeric_limits<int>::lowest()) empty = true;
					if(token.val_int >= 0)
						token.type = Token_Type::integer;
				} else if(type == Sub_Indexing_Info::Type::named) {
					ole_get_string(&index_name, buf, buf_size);
					if(strlen(buf) > 0) {
						index_names_str[row] = buf;
						token.string_value = String_View(index_names_str[row].data());
						token.type = Token_Type::quoted_string;
					} else
						empty = true;
				}
				
				if(empty) continue;
				
				if(token.type == Token_Type::unknown) {
					ole_close_due_to_error(handles, tab, row+2, col+2);
					fatal_error("This is not a valid index name.");
				}
				index_names.push_back(token);
			}
			
			if(!got_name_this_column && index_names.empty()) // There was no name on top of the column and no indexes. This means there are no more data columns
				break;
			
			std::vector<int> indexes_int;
			get_indexes(data_set, index_sets, index_names, indexes_int); 
			
			// TODO: (see same comment in data_set.cpp) Just maybe organize the data differently so that we don't need to do this zip.
			std::vector<std::pair<Data_Id, int>> indexes(index_sets.size());
			for(int lev = 0; lev < index_sets.size(); ++lev)
				indexes[lev] = std::pair<Data_Id, int>{index_sets[lev], indexes_int[lev]};
			
			data.header_data.push_back({});
			auto &header = data.header_data.back();
			// TODO: Make system for having more than one index tuple for the same series.
			header.name = current_input_name;
			header.source_loc.filename = handles->file_path;
			header.source_loc.type = Source_Location::Type::spreadsheet;
			header.source_loc.column = col+2;
			header.source_loc.line   = 1;
			header.source_loc.tab    = tab+1;
			
			header.indexes.push_back(std::move(indexes));
			
			if(potential_flag_row > 0) {
				
				VARIANT flags_var = ole_get_matrix_value(&matrix, potential_flag_row, col+2, handles);
				ole_get_string(&flags_var, buf, buf_size);
				
				//log_print("Flag string \"", buf, "\" tab ", tab, "\n");
				if(strlen(buf) > 0) {
					// TODO: make it possible to turn off errors in the stream and instead have it return an invalid token, so that it doesn't quit the program on us.
					// TODO: or make it possible to have it have a custom source location for its error printing (since we support spreadsheet location types now)
					Token_Stream stream("", buf);
					while(true) {
						Token token = stream.peek_token();
						if(token.type == Token_Type::identifier) {
							bool success = set_flag(&header.flags, token.string_value);
							if(success) {
								stream.read_token();
								continue;
							} else {
								ole_close_due_to_error(handles, tab, col+2, potential_flag_row);
								fatal_error("Unrecognized input flag \"", token.string_value, "\".");
							}
						} else if ((char)token.type == '[') {
							Decl_AST *unit_decl = parse_decl_header(&stream);
							header.unit.set_data(unit_decl);
							delete unit_decl;
						} else if (token.type == Token_Type::eof) {
							break;
						} else {
							ole_close_due_to_error(handles, tab, col+2, potential_flag_row);
							fatal_error("Unexpected token \"", token.string_value, "\".");
						}
					}
				}
			}
		}
		
		ole_destroy_matrix(&matrix);
		
		if(first_date_row < 0) {
			ole_close_due_to_error(handles, tab);
			fatal_error("Could not find any dates in column A.");
		}
		
		int start_row = first_date_row;
		
		search_len = 1024;   // Search this many rows at a time for date values.
		data.dates.reserve(search_len);
		
		data.start_date.seconds_since_epoch = std::numeric_limits<s64>::max();
		data.end_date.seconds_since_epoch   = std::numeric_limits<s64>::min();
		
		// ********* Read all the dates in the date column
		
		while(true) {
			bool break_out = false;
			auto matrix = ole_get_range_matrix(start_row, start_row + search_len - 1, 1, 1, handles);
			for(int row = 0; row < search_len; ++row) {
				VARIANT var = ole_get_matrix_value(&matrix, start_row+row, 1, handles);
				
				Date_Time date;
				bool success = ole_get_date(&var, &date);
				
				if(!success) {
					break_out = true;
					break;
				}
				
				if(date < data.start_date) data.start_date = date;
				if(date > data.end_date)   data.end_date = date;
				
				data.dates.push_back(date);
			}
			
			start_row += search_len;
			
			ole_destroy_matrix(&matrix);
			
			if(break_out) break;
		}
		
		data.raw_values.resize(data.header_data.size());
		for(auto &vals : data.raw_values)
			vals.resize(data.dates.size());
		
		// ***** Read in the actual input data values
		
		matrix = ole_get_range_matrix(first_date_row, first_date_row + data.dates.size() - 1, 2, 1 + data.header_data.size(), handles);
		
		for(int col = 0; col < data.header_data.size(); ++col) {
			for(int row = 0; row < data.dates.size(); ++row) {
				VARIANT value = ole_get_matrix_value(&matrix, first_date_row + row, col+2, handles);
				double val = ole_get_double(&value);
				if(!std::isfinite(val)) val = std::numeric_limits<double>::quiet_NaN();
				data.raw_values[col][row] = val;
			}
		}
		
		ole_destroy_matrix(&matrix);
	}
}

#endif // OLE_AVAILABLE