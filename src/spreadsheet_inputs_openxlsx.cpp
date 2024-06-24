#include <OpenXLSX.hpp>
#include "data_set.h"


void
close_due_to_error(OpenXLSX::XLDocument &doc, int tab, u32 row, u16 col) {
	
	using namespace OpenXLSX;
	
	char buf[32];
	col_row_to_cell(col, row, &buf[0]); // TODO: Could probably use XLCellReference instead.
	
	auto sheet = doc.workbook().sheet(tab);
	begin_error(Mobius_Error::spreadsheet);
	error_print("In file \"", doc.name(), "\", tab \"", sheet.name(), "\", cell ", buf, "\n");
	doc.close();
}

bool
can_be_date(OpenXLSX::XLCellValueProxy &val, Date_Time *datetime = nullptr) {
	
	using namespace OpenXLSX;
	
	// Unfortunately OpenXLSX doesn't yet offer a better solution for checking if the original cell had a date format.
	
	bool is_float = (val.type() == XLValueType::Float);
	if(is_float || val.type() == XLValueType::Integer) {
		if(datetime) {
			if(is_float)
				*datetime = from_spreadsheet_time_fractional(val.get<double>());
			else
				*datetime = from_spreadsheet_time(val.get<s64>());
		}
		return true;
	} else if (val.type() == XLValueType::String) {
		// NOTE: Dates before 1900 will be formatted as strings in Excel
		
		// TODO: Could we do get<const char *>, and would that make it more efficient?
		// TODO: Parse hour, minute, second here too (if applicable).
		auto str = val.get<std::string>();
		int y, m, d;
		if(sscanf(str.c_str(), "%d-%d-%d", &y, &m, &d) == 3) {
			bool success = false;
			Date_Time dt(y, m, d, &success);
			if(success && datetime) *datetime = dt;
			return success;
		}
	}
	return false;
}

void
read_series_data_from_spreadsheet(Data_Set *data_set, Series_Data *series, String_View file_name) {
	
	using namespace OpenXLSX;
	
	XLDocument doc;
	
	try {
		doc.open(std::string(file_name));
	} catch(...) {
		fatal_error(Mobius_Error::parsing, "The file \"", file_name, "\" can't be opened.");
	}
	
	auto wb = doc.workbook();
	auto n_tabs = wb.sheetCount();
	
	for(u16 tab = 1; tab <= n_tabs; ++tab) {
		
		// TODO: Will this cause an error if it is a different type of sheet?
		auto &basesheet = wb.sheet(tab);
		if(!basesheet.isType<XLWorksheet>())
			continue;
		
		auto &sheet = basesheet.get<XLWorksheet>();
		
		if(sheet.cell("A1").value() == "NOREAD")
			continue;
		
		series->series.push_back({});
		auto &data = series->series.back();
		data.has_date_vector = true;
		
		std::vector<Entity_Id> index_sets;
		
		int potential_flag_row = -1;
		int first_date_row = -1;
		
		// ***** parse the first column to see what index sets are used and on what row there are flags and where the date column starts
		
		u32 search_len = 128; // We should never encounter a header larger than this. TODO: Could use rowCount() instead?
		for(u32 row = 2; row <= search_len; ++row) {
			auto &cell = sheet.cell(row, 1);
			auto &val = cell.value();
			
			if(can_be_date(val)) {
				
				first_date_row = row;
				break;
				
			} else if (val.type() == XLValueType::String) {
				auto name = val.get<std::string>();
				
				// Expect the name of an index set.
				auto index_set_id = data_set->deserialize(name, Reg_Type::index_set);
				if(!is_valid(index_set_id)) {
					close_due_to_error(doc, tab, row, 1);
					fatal_error("The index set ", name, " was not previously declared in the data set.");
				}
				index_sets.push_back(index_set_id);
				
			} else if(val.type() == XLValueType::Empty) {
				if(potential_flag_row > 0) {
					close_due_to_error(doc, tab, row, 1);
					fatal_error("There should not be empty cells in column A except in row 1, or potentially in the row right above the dates, or at the end.");
				}
				potential_flag_row = row;
			} else {
				close_due_to_error(doc, tab, row, 1);
				fatal_error("The cell is not the name of an index set or a date, and it is not an empty cell signifying a flag row.");
			}
		}
		
		if(first_date_row < 0) {
			close_due_to_error(doc, tab, 1, 1);
			fatal_error("Could not find any dates in column A.");
		}
		
		// ********* Parse the header data
		
		std::vector<Token> prev_index(index_sets.size());
		std::vector<u8>    has_prev_index(index_sets.size(), false);
		std::vector<std::string> index_names_str(index_sets.size()); // This is needed to have temporary storage for the data...
		std::string current_input_name = "";
		
		for(u16 col = 2; col <= sheet.columnCount(); ++col) {
			bool got_name_this_column = false;
			bool got_new_indexes_this_column = false;
			
			auto &cell = sheet.cell(1, col);
			auto &inname = cell.value();
			
			if(inname.type() == XLValueType::String) {
				got_name_this_column = true;
				current_input_name = inname.get<std::string>();
			} else if(inname.type() != XLValueType::Empty) {
				close_due_to_error(doc, tab, 1, col);
				fatal_error("Expected a series name in string format.");
			} else if(current_input_name.empty()) {
				close_due_to_error(doc, tab, 1, col);
				fatal_error("Missing a series name.");
			}
			
			std::vector<Token> index_names;
			std::vector<Entity_Id> active_index_sets;
			
			Token token = {};
			token.source_loc.filename = series->file_name;
			token.source_loc.type = Source_Location::Type::spreadsheet;
			
			// ****** Read indexes if any
			
			if(!index_sets.empty()) {
				
				auto &rowrange = sheet.range(XLCellReference(2, col), XLCellReference(index_sets.size()+1, col));
				u32 row = 0;
				int idx = 0;
				for(auto &cell : rowrange) {
					++row;
					
					token.source_loc.tab = tab;
					token.source_loc.column = col;
					token.source_loc.line = row;
					
					auto &val = cell.value();
					
					auto idxtype = data_set->index_data.get_index_type(index_sets[idx]);
					
					// TODO: We should maybe do some checking depending on the expected type of the index.
					bool empty = false;
					if(val.type() == XLValueType::Integer) {
						
						token.type = Token_Type::integer;
						token.val_int = val.get<s64>();
						
					} else if(val.type() == XLValueType::Float) {
						
						token.type = Token_Type::real;
						token.val_double = val.get<double>();
						
					} else if(val.type() == XLValueType::String) {
						
						index_names_str[idx] = val.get<std::string>();
						if(idxtype == Index_Record::Type::numeric1) {
							// It could be a string-formatted number. We allow that for indexes.
							// TODO: If the stream errors, that will not be the correct behaviour. We need an option to turn it off.
							Token_Stream stream(file_name, String_View(index_names_str[idx]));
							auto token2 = stream.read_token();
							if(token2.type == Token_Type::integer || token2.type == Token_Type::real) {
								token.type = token2.type;
								if(token2.type == Token_Type::integer)
									token.val_int = token2.val_int;
								else
									token.val_double = token2.val_double;
							} else {
								close_due_to_error(doc, tab, row, 1);
								fatal_error("This index set requires numeric indexes.");
							}
						} else {
							token.type = Token_Type::quoted_string;
							token.string_value = String_View(index_names_str[idx].c_str());
						}
						
					} else if(val.type() == XLValueType::Empty) {
						
						empty = true;
						if(has_prev_index[idx]) {
							active_index_sets.push_back(index_sets[idx]);
							index_names.push_back(prev_index[idx]);
						}
						++idx;
						continue;
						
					} else {
						close_due_to_error(doc, tab, row, col);
						fatal_error("This is not a valid index name.");
					}
					
					if(!empty)
						got_new_indexes_this_column = true;
					
					active_index_sets.push_back(index_sets[idx]);
					index_names.push_back(token);
					has_prev_index[idx] = true;
					prev_index[idx] = token;
					
					++idx;
				}
			}
			
			// There was no name on top of the column and no indexes. This means there are no more data columns
			if(!got_name_this_column && !got_new_indexes_this_column)
				break;
			
			// TODO: We need to intercept this error (if there is one) in order to properly close the doc
			data_set->index_data.check_valid_distribution(active_index_sets, token.source_loc);
			
			// TODO: Same here, need to intercept error.
			Indexes indexes;
			data_set->index_data.find_indexes(active_index_sets, index_names, indexes); 
			
			data.header_data.push_back({});
			auto &header = data.header_data.back();
			
			header.name = current_input_name;
			header.source_loc.filename = series->file_name;
			header.source_loc.type = Source_Location::Type::spreadsheet;
			header.source_loc.column = col;
			header.source_loc.line   = 1;
			header.source_loc.tab    = tab;
			// TODO: Make a system for having more than one index tuple for the same series, like we have for the csv format.
			header.indexes.push_back(std::move(indexes));
			
			
			// ********* Read flag data for the series
			if(potential_flag_row > 0) {
				auto &cell = sheet.cell(potential_flag_row, col);
				auto &val = cell.value();
				
				if(val.type() != XLValueType::Empty) {
				
					if(val.type() != XLValueType::String) {
						close_due_to_error(doc, tab, potential_flag_row, col);
						fatal_error("This is not a valid value for time series flags.");
					}
					
					auto str = val.get<std::string>();
					Token_Stream stream("", str.c_str());
					while(true) {
						// TODO: make it possible to turn off errors in the stream and instead have it return an invalid token, so that it doesn't quit the program on us if there is a parsing error.
						// We could intercept it, but it would still print an error message with the wrong source location.
						Token token = stream.peek_token();
						if(token.type == Token_Type::identifier) {
							bool success = set_flag(&header.flags, token.string_value);
							if(success) {
								stream.read_token();
								continue;
							} else {
								close_due_to_error(doc, tab, potential_flag_row, col);
								fatal_error("Unrecognized input flag \"", token.string_value, "\".");
							}
						} else if ((char)token.type == '[') {
							Decl_AST *unit_decl = parse_decl_header(&stream);
							header.unit.set_data(unit_decl);
							delete unit_decl;
						} else if (token.type == Token_Type::eof) {
							break;
						} else {
							close_due_to_error(doc, tab, potential_flag_row, col);
							fatal_error("Unexpected token \"", token.string_value, "\".");
						}
					}
				}
			}
		}
		
		data.start_date.seconds_since_epoch = std::numeric_limits<s64>::max();
		data.end_date.seconds_since_epoch   = std::numeric_limits<s64>::min();
		
		// ********* Read all the dates in the date column
		auto &range = sheet.range(XLCellReference(first_date_row, 1), XLCellReference(sheet.rowCount(), 1));
		
		int row = first_date_row;
		for(auto &cell : range) {
			
			auto &val = cell.value();
			Date_Time date;
			if(val.type() == XLValueType::Empty)
				break;
			if(!can_be_date(val, &date)) {
				close_due_to_error(doc, tab, row, 1);
				fatal_error("This is not a valid date value.");
			}
			data.dates.push_back(date);
			
			if(date < data.start_date) data.start_date = date;
			if(date > data.end_date)   data.end_date = date;
			
			++row;
		}
		
		data.raw_values.resize(data.header_data.size());
		for(auto &vals : data.raw_values)
			vals.resize(data.dates.size());
		
		// ***** Read in the actual input data values
		
		int hidx = 0;
		for(u16 col = 2; col < 2 + data.header_data.size(); ++col) {
			auto &range = sheet.range(XLCellReference(first_date_row, col), XLCellReference(first_date_row - 1 + data.dates.size(), col));
			
			int row = first_date_row;
			int idx = 0;
			for(auto &cell : range) {
				
				auto &val = cell.value();
				
				double result;
				if(val.type() == XLValueType::Float)
					result = val.get<double>();
				else if(val.type() == XLValueType::Integer)
					result = (double)val.get<s64>();
				else if(val.type() == XLValueType::Empty)
					result = std::numeric_limits<double>::quiet_NaN();
				else {
					// TODO: Should we attempt to parse strings as numbers?
					// Should we default to NaN instead of having error? (Probably not, better to alert the user).
					close_due_to_error(doc, tab, row, col);
					fatal_error("This is not a valid number representation.");
				}
				
				data.raw_values[hidx][idx] = result;
				
				++idx;
				++row;
			}
			++hidx;
		}
	}
	
}