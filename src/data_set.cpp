
#include <set>

#include "data_set.h"
#include "ole_wrapper.h"


void
write_index_set_to_file(FILE *file, Index_Set_Info &index_set) {
	
	fprintf(file, "index_set(\"%s\") [ ", index_set.name.data());
	for(auto &index : index_set.indexes)
		fprintf(file, "\"%s\" ", index.name.data());
	fprintf(file, "]\n\n");
}


void
write_connection_info_to_file(FILE *file, Connection_Info &connection, Data_Set *data_set) {
	
	//TODO: Must be reimplemented for the new format!
	/*
	if(connection.type != Connection_Info::Type::graph)
		fatal_error(Mobius_Error::internal, "Unimplemented connection info type in Data_Set::write_to_file.");
	
	fprintf(file, "connection(\"%s\", \"%s\") [", connection.name.data(), connection.index_set.data());
	
	Index_Set_Info *index_set = data_set->index_sets.find(connection.index_set);
	if(!index_set)
		fatal_error(Mobius_Error::internal, "Data set does not have index set that was registered with connection info.");
	// TODO!
	//   non-trivial problem to format this the best way possible... :(
	//   could keep around structure from previously loaded file, but that doesn't help if the data is edited e.g. in the user interface.
	
	// Current solution: works nicely if indexes are declared in the order they are chained.
	// Note that the data is always correct, it is just not necessarily the most compact representation of the graph.
	
	int next = -1;
	for(int idx = 0; idx < index_set->indexes.count(); ++idx) {
		int points_at = connection.points_at[idx];
		if(points_at >= 0) {
			if(idx != next) {
				fprintf(file, "\n\t\"%s\" ", index_set->indexes[idx]->name.data());
			}
			fprintf(file, " -> \"%s\"", index_set->indexes[points_at]->name.data());
			
			next = points_at;
		}
	}
	
	fprintf(file, "\n]\n\n");
	*/
}

void
print_tabs(FILE *file, int ntabs) {
	if(ntabs <= 0) return;
	if(ntabs == 1) {
		fprintf(file, "\t");
		return;
	}
	if(ntabs == 2) {
		fprintf(file, "\t\t");
		return;
	}
	if(ntabs == 3) {
		fprintf(file, "\t\t\t");
		return;
	}
	if(ntabs == 4) {
		fprintf(file, "\t\t\t\t");
		return;
	}
}

void
write_parameter_to_file(FILE *file, Par_Info &info, int tabs, bool double_newline = true) {
	
	print_tabs(file, tabs);
	fprintf(file, "%s(\"%s\")\n", name(info.type), info.name.data());
	print_tabs(file, tabs);
	fprintf(file, "[ ");
	
	if(info.type == Decl_Type::par_enum) {
		for(std::string &val : info.values_enum)
			fprintf(file, "%s ", val.data());
	} else { 
		// TODO: matrix formatting
		for(Parameter_Value &val : info.values) {
			switch(info.type) {
				case Decl_Type::par_real : {
					fprintf(file, "%.15g ", val.val_real);
				} break;
				
				case Decl_Type::par_bool : {
					fprintf(file, "%s ", val.val_boolean ? "true" : "false");
				} break;
				
				case Decl_Type::par_int : {
					fprintf(file, "%lld ", (long long)val.val_integer);
				} break;
				
				case Decl_Type::par_datetime : {
					char buf[64];
					val.val_datetime.to_string(buf);
					fprintf(file, "%s ", buf);
				} break;
			}
		}
	}
	fprintf(file, "]\n");
	if(double_newline)
		fprintf(file, "\n");
}

void
write_par_group_to_file(FILE *file, Par_Group_Info &par_group, int tabs, bool double_newline = true) {
	print_tabs(file, tabs);
	fprintf(file, "par_group(\"%s\") ", par_group.name.data());
	if(par_group.index_sets.size() > 0) {
		fprintf(file, "[ ");
		int idx = 0;
		for(auto index_set : par_group.index_sets) {
			fprintf(file, "\"%s\" ", index_set.data());
		}
		fprintf(file, "] ");
	}
	fprintf(file, "{\n");
	int idx = 0;
	for(auto &par : par_group.pars)
		write_parameter_to_file(file, par, tabs+1, idx++ != par_group.pars.count()-1);
	
	print_tabs(file, tabs);
	fprintf(file, "}\n");
	if(double_newline)
		fprintf(file, "\n");
}

void
write_module_to_file(FILE *file, Module_Info &module) {
	fprintf(file, "module(\"%s\", %d, %d, %d) {\n", module.name.data(), module.version.major, module.version.minor, module.version.revision);
	int idx = 0;
	for(auto &par_group : module.par_groups)
		write_par_group_to_file(file, par_group, 1, idx++ != module.par_groups.count()-1);
	fprintf(file, "}\n\n");
}

void
write_series_to_file(FILE *file, std::string &main_file, Series_Set_Info &series, std::set<std::string> &already_processed) {
	if(series.file_name == main_file) {
		fatal_error(Mobius_Error::internal, "Inlining series data in main data file is not yet supported!");
	}
	
	// NOTE: If we read from an excel file, it will produce one Series_Set_Info record per page, but we only want to write out one import of the file.
	if(already_processed.find(main_file) != already_processed.end())
		return;
	
	already_processed.insert(main_file);
	
	//TODO: What do we do if the file we save to is in a different folder than the original. Should we update all the relative paths of the included files?
	
	fprintf(file, "series(\"%s\")\n\n", series.file_name.data());
}

void
Data_Set::write_to_file(String_View file_name) {
	//fatal_error(Mobius_Error::internal, "Write to file not implemented");
	
	FILE *file = open_file(file_name, "w");
	
	if(doc_string != "") {
		fprintf(file, "\"\"\"\n%s\n\"\"\"\n\n", doc_string.data());
	}
	
	for(auto &index_set : index_sets)
		write_index_set_to_file(file, index_set);
	
	for(auto &connection : connections)
		write_connection_info_to_file(file, connection, this);
	
	std::set<std::string> already_processed;
	for(auto &ser : series)
		write_series_to_file(file, main_file, ser, already_processed);
	
	for(auto &par_group : global_module.par_groups)
		write_par_group_to_file(file, par_group, 0);
	
	for(auto &module : modules)
		write_module_to_file(file, module);
	
	fclose(file);
}

void
read_string_list(Token_Stream *stream, std::vector<Token> &push_to, bool ident = false) {
	stream->expect_token('[');
	while(true) {
		Token token = stream->peek_token();
		if((char)token.type == ']') {
			stream->read_token();
			break;
		}
		if(ident)
			stream->expect_identifier();
		else
			stream->expect_quoted_string();
		push_to.push_back(token);
	}
}

void
read_compartment_identifier(Data_Set *data_set, Token_Stream *stream, Compartment_Ref *read_to) {
	Token token = stream->peek_token();
	stream->expect_identifier();
	
	auto find = data_set->compartment_handle_to_id.find(token.string_value);
	if(find == data_set->compartment_handle_to_id.end()) {
		token.print_error_header();
		fatal_error("The handle '", token.string_value, "' does not refer to an already declared compartment.");
	}
	read_to->id = find->second;
	auto comp_data = data_set->compartments[read_to->id];
	std::vector<Token> index_names;
	read_string_list(stream, index_names);
	if(index_names.size() != comp_data->index_sets.size()) {
		token.print_error_header();
		fatal_error("The compartment '", token.string_value, "' should be indexed with ", comp_data->index_sets.size(), " indexes.");
	}
	for(int pos = 0; pos < index_names.size(); ++pos) {
		auto index_set = data_set->index_sets[comp_data->index_sets[pos]];
		int idx = index_set->indexes.expect_exists_idx(&index_names[pos], "index");
		read_to->indexes.push_back(idx);
	}
}

void
read_connection_sequence(Data_Set *data_set, Compartment_Ref *first_in, Token_Stream *stream, Connection_Info *info) {
	
	std::pair<Compartment_Ref, Compartment_Ref> entry;
	if(!first_in)
		read_compartment_identifier(data_set, stream, &entry.first);
	else
		entry.first = *first_in;
	
	stream->expect_token(Token_Type::arr_r);
	read_compartment_identifier(data_set, stream, &entry.second);
	
	info->arrows.push_back(entry);
	
	Token token = stream->peek_token();
	if(token.type == Token_Type::arr_r) {
		stream->read_token();
		read_connection_sequence(data_set, &entry.second, stream, info);
	} else if(token.type == Token_Type::identifier) {
		read_connection_sequence(data_set, nullptr, stream, info);
	} else if((char)token.type == ']') {
		stream->read_token();
		return;
	} else {
		token.print_error_header();
		fatal_error("Expected a ], an -> or a compartment identifier.");
	}
}

void
read_connection_data(Data_Set *data_set, Token_Stream *stream, Connection_Info *info) {
	stream->expect_token('[');
	
	info->type = Connection_Info::Type::graph;
	
	Token token = stream->peek_token();
	if((char)token.type == ']') {
		stream->read_token();
		return;
	}
		
	if(token.type != Token_Type::identifier) {
		token.print_error_header();
		fatal_error("Expected a ] or the start of an compartment identifier.");
	}
	
	read_connection_sequence(data_set, nullptr, stream, info);
}

void
read_series_data_block(Data_Set *data_set, Token_Stream *stream, Series_Set_Info *data) {
	
	data->has_date_vector = true;
	Token token = stream->peek_token();
	
	if(token.type == Token_Type::date) {
		// If there is a date as the first token, that gives the start date, and there is no separate date for each row.
		data->start_date = stream->expect_datetime();
		data->has_date_vector = false;
	} else if(token.type != Token_Type::quoted_string) {
		token.print_error_header();
		fatal_error("Expected either a start date or the name of an input series.");
	}
	
	while(true) {
		Series_Header_Info header;
		header.loc = token.location;
		header.name = std::string(stream->expect_quoted_string());
		while(true) {
			token = stream->peek_token();
			if((char)token.type != '[')
				break;
			stream->read_token();
			token = stream->peek_token();
			if(token.type == Token_Type::quoted_string) {
				std::vector<std::pair<std::string, int>> indexes;
				while(true) {
					stream->read_token();
					auto index_set = data_set->index_sets.expect_exists(&token, "index_set");
					stream->expect_token(':');
					token = stream->peek_token();
					stream->expect_quoted_string();
					int index      = index_set->indexes.expect_exists_idx(&token, "index");
					indexes.push_back(std::pair<std::string, int>{index_set->name, index});
					token = stream->peek_token();
					if((char)token.type == ']') {
						stream->read_token();
						break;
					}
					if(token.type != Token_Type::quoted_string) {
						token.print_error_header();
						fatal_error("Expected a ] or a new index set name.");
					}
				}
				header.indexes.push_back(std::move(indexes));
			} else if(token.type == Token_Type::identifier) {
				while(true) {
					bool success = set_flag(&header.flags, token.string_value);
					if(!success) {
						token.print_error_header();
						fatal_error("Unrecognized input flag \"", token.string_value, "\".");
					}
					token = stream->read_token();
					if((char)token.type == ']')
						break;
					else if(token.type != Token_Type::identifier) {
						token.print_error_header();
						fatal_error("Expected a ] or another flag identifier");
					}
				}
				//TODO: Check for conflicting flags.
			} else {
				token.print_error_header();
				fatal_error("Expected the name of an index set or a flag");
			}
		}
		data->header_data.push_back(std::move(header));
		
		Token token = stream->peek_token();
		if(token.type != Token_Type::quoted_string)
			break;
	}
	
	if(data->header_data.empty())
		fatal_error(Mobius_Error::internal, "Empty input data header not properly detected.");
	
	int rowlen = data->header_data.size();
	
	data->raw_values.resize(rowlen);
	for(auto &vec : data->raw_values)
		vec.reserve(1024);
	
	if(data->has_date_vector) {
		Date_Time start_date;
		start_date.seconds_since_epoch = std::numeric_limits<s64>::max();
		Date_Time end_date;
		end_date.seconds_since_epoch = std::numeric_limits<s64>::min();
		
		while(true) {
			Date_Time date = stream->expect_datetime();
			if(date < start_date) start_date = date;
			if(date > end_date)   end_date   = date;
			data->dates.push_back(date);
			for(int col = 0; col < rowlen; ++col) {
				double val = stream->expect_real();
				data->raw_values[col].push_back(val);
			}
			Token token = stream->peek_token();
			if(token.type != Token_Type::date)
				break;
		}
		data->start_date = start_date;
		data->end_date = end_date;
		
	} else {
		data->time_steps = 0;
		
		while(true) {
			for(int col = 0; col < rowlen; ++col) {
				double val = stream->expect_real();
				data->raw_values[col].push_back(val);
			}
			++data->time_steps;
			Token token = stream->peek_token();
			if(!is_numeric(token.type))
				break;
		}
	}
}

void
parse_parameter_decl(Par_Group_Info *par_group, Token_Stream *stream, int expect_count) {
	auto decl = parse_decl_header(stream);
	match_declaration(decl, {{Token_Type::quoted_string}}, 0, false, 0);
	Token *arg = single_arg(decl, 0);
	auto par = par_group->pars.create(arg->string_value, arg->location);
	par->type = decl->type;
	if(par->type == Decl_Type::par_enum) {
		std::vector<Token> list;
		read_string_list(stream, list, true);
		for(auto &item : list) par->values_enum.push_back(item.string_value);
	} else {
		stream->expect_token('[');
		for(int idx = 0; idx < expect_count; ++idx) {
			Token token = stream->peek_token();
			Parameter_Value val;
			if(decl->type == Decl_Type::par_real && is_numeric(token.type)) {
				val.val_real = stream->expect_real();
				par->values.push_back(val);
			} else if(decl->type == Decl_Type::par_bool && token.type == Token_Type::boolean) {
				val.val_boolean = stream->expect_bool();
				par->values.push_back(val);
			} else if(decl->type == Decl_Type::par_int && token.type == Token_Type::integer) {
				val.val_integer = stream->expect_int();
				par->values.push_back(val);
			} else if(decl->type == Decl_Type::par_datetime && token.type == Token_Type::date) {
				val.val_datetime = stream->expect_datetime();
				par->values.push_back(val);
			} else if((char)token.type == ']') {
				token.print_error_header();
				fatal_error("Expected ", expect_count, " values for parameter, got ", idx, ".");
			} else {
				token.print_error_header();
				fatal_error("Invalid parameter value or ] .");
			}
		}
		stream->expect_token(']');
	}
	delete decl;
}

void
parse_par_group_decl(Data_Set *data_set, Module_Info *module, Token_Stream *stream, Decl_AST *decl) {
	match_declaration(decl, {{Token_Type::quoted_string}}, 0, false, 0);

	auto name = single_arg(decl, 0);
	auto group = module->par_groups.create(name->string_value, name->location);
	
	int expect_count = 1;
	Token token = stream->peek_token();
	std::vector<Token> list;
	if((char)token.type == '[') {
		read_string_list(stream, list);
		if(list.size() > 0) {
			if(module == &data_set->global_module && name->string_value == "System") {
				list[0].print_error_header();
				fatal_error("The global \"System\" parameter group should not be indexed by index sets.");
			}
			for(Token &item : list) {
				auto index_set = data_set->index_sets.expect_exists(&item, "index_set");
				group->index_sets.push_back(item.string_value);
				expect_count *= index_set->indexes.count();
			}
		}
	}
	
	stream->expect_token('{');
	while(true) {
		token = stream->peek_token();
		if(token.type == Token_Type::identifier) {
			parse_parameter_decl(group, stream, expect_count);
		} else if((char)token.type == '}') {
			stream->read_token();
			break;
		} else {
			token.print_error_header();
			fatal_error("Expected a } or a parameter declaration.");
		}
	}
}


#if OLE_AVAILABLE
void
read_series_data_from_spreadsheet(Data_Set *data_set, OLE_Handles *handles, String_View file_name);
#endif

void
Data_Set::read_from_file(String_View file_name) {
	if(main_file != "") {
		fatal_error(Mobius_Error::api_usage, "Tried make a data set read from a file ", file_name, ", but it already contains data from the file ", main_file, ".");
	}
	main_file = std::string(file_name);
	
	//TODO: have a file handler instead
	auto file_data = file_handler.load_file(file_name);
	
	Token_Stream stream(file_name, file_data);
	stream.allow_date_time_tokens = true;
	
#if OLE_AVAILABLE
	OLE_Handles handles = {};
#endif
	
	while(true) {
		Token token = stream.peek_token();
		if(token.type == Token_Type::eof) break;
		else if(token.type == Token_Type::quoted_string) {
			if(!doc_string.empty()) {
				token.print_error_header();
				fatal_error("Duplicate doc strings for data set.");
			}
			doc_string = std::string(stream.expect_quoted_string());
			continue;
		} else if(token.type != Token_Type::identifier) {
			token.print_error_header();
			fatal_error("Expected an identifier (index_set, compartment, connection, series, module, par_group, or par_datetime).");
		}
		
		Decl_AST *decl = parse_decl_header(&stream);
		
		switch(decl->type) {
			case Decl_Type::index_set : {
				match_declaration(decl, {{Token_Type::quoted_string}}, 0, false);
				
				auto name = single_arg(decl, 0);
				auto data = index_sets.create(name->string_value, name->location);
				std::vector<Token> indexes;
				read_string_list(&stream, indexes);
				for(int idx = 0; idx < indexes.size(); ++idx)
					data->indexes.create(indexes[idx].string_value, indexes[idx].location);
			} break;
			
			case Decl_Type::compartment : {
				match_declaration(decl, {{Token_Type::quoted_string}});
				
				auto name = single_arg(decl, 0);
				auto data = compartments.create(name->string_value, name->location);
				int comp_id = compartments.find_idx(name->string_value);
				data->handle = decl->handle_name.string_value;
				if(decl->handle_name.string_value) // It is a bit pointless to declare one without a handle, but it is maybe annoying to have to require it??
					compartment_handle_to_id[decl->handle_name.string_value] = comp_id;
				std::vector<Token> idx_set_list;
				read_string_list(&stream, idx_set_list);
				for(auto &name : idx_set_list) {
					int ref = index_sets.expect_exists_idx(&name, "index_set");
					data->index_sets.push_back(ref);
				}
			} break;
			
			case Decl_Type::connection : {
				match_declaration(decl, {{Token_Type::quoted_string}}, 0, false, 0);
				
				auto name = single_arg(decl, 0);
				auto data = connections.create(name->string_value, name->location);
				
				read_connection_data(this, &stream, data);
			} break;
			
			case Decl_Type::series : {
				int which = match_declaration(decl, {{Token_Type::quoted_string}, {}}, 0, false);
				if(which == 0) {
					String_View other_file_name = single_arg(decl, 0)->string_value;
					
					if(file_handler.is_loaded(other_file_name, file_name)) {
						token.print_error_header();
						fatal_error("The file ", other_file_name, " has already been loaded.");
					}
					
					bool success;
					String_View extension = get_extension(other_file_name, &success);
					if(success && (extension == ".xlsx" || extension == ".xls")) {
						#if OLE_AVAILABLE
						String_View relative = make_path_relative_to(other_file_name, file_name);
						ole_open_spreadsheet(relative, &handles);
						read_series_data_from_spreadsheet(this, &handles, other_file_name);
						#else
						single_arg(decl, 0)->print_error_header();
						fatal_error("Spreadsheet reading is only available on Windows.");
						#endif
					} else {
						String_View other_data = file_handler.load_file(other_file_name, single_arg(decl, 0)->location, file_name);
						Token_Stream other_stream(other_file_name, other_data);
						other_stream.allow_date_time_tokens = true;
						
						series.push_back({});
						Series_Set_Info &data = series.back();
						data.file_name = std::string(other_file_name);
						read_series_data_block(this, &other_stream, &data);
					}
				} else {
					stream.read_token();
					stream.expect_token('[');
					series.push_back({});
					Series_Set_Info &data = series.back();
					data.file_name = std::string(file_name);
					read_series_data_block(this, &stream, &data);
					stream.expect_token(']');
				}
			} break;
			
			case Decl_Type::par_group : {
				parse_par_group_decl(this, &global_module, &stream, decl);
			} break;

			case Decl_Type::module : {
				match_declaration(decl, {{Token_Type::quoted_string, Token_Type::integer, Token_Type::integer, Token_Type::integer}}, 0, false, 0);
			
				auto name = single_arg(decl, 0);
				auto module = modules.create(name->string_value, name->location);
				module->version.major    = single_arg(decl, 1)->val_int;
				module->version.minor    = single_arg(decl, 2)->val_int;
				module->version.revision = single_arg(decl, 3)->val_int;
				
				stream.expect_token('{');
				while(true) {
					token = stream.peek_token();
					if(token.type == Token_Type::identifier && token.string_value == "par_group") {
						Decl_AST *decl2 = parse_decl_header(&stream);
						parse_par_group_decl(this, module, &stream, decl2);
						delete decl2;
					} else if((char)token.type == '}') {
						stream.read_token();
						break;
					} else {
						token.print_error_header();
						fatal_error("Expected a } or a par_group declaration.");
					}
				}
			} break;
			
			default : {
				decl->location.print_error_header();
				fatal_error("Did not expect a declaration of type '", name(decl->type), "' in a data set.");
			} break;
		}
		
		delete decl;
	}
	
#if OLE_AVAILABLE
	ole_close_app_and_spreadsheet(&handles);
#endif
	
	file_handler.unload_all(); // Free the file data.
}