
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
write_compartment_info_to_file(FILE *file, Compartment_Info &compartment, Data_Set *data_set) {
	fprintf(file, "%s : compartment(\"%s\") [", compartment.handle.data(), compartment.name.data());
	for(int idx_set_idx : compartment.index_sets) {
		auto index_set = data_set->index_sets[idx_set_idx];
		fprintf(file, " \"%s\"", index_set->name.data());
	}
	fprintf(file, " ]\n");
}

void
write_compartment_identifier_to_file(FILE *file, Compartment_Ref &ref, Data_Set *data_set) {
	auto compartment = data_set->compartments[ref.id];
	fprintf(file, "%s[", compartment->handle.data());
	for(int loc = 0; loc < ref.indexes.size(); ++loc) {
		auto index_set = data_set->index_sets[compartment->index_sets[loc]];
		fprintf(file, " \"%s\"", index_set->indexes[ref.indexes[loc]]);
	}
	fprintf(file, " ]");
}

void
write_connection_info_to_file(FILE *file, Connection_Info &connection, Data_Set *data_set) {
	
	if(connection.type != Connection_Info::Type::graph)
		fatal_error(Mobius_Error::internal, "Unimplemented connection info type in Data_Set::write_to_file.");
	
	if(connection.arrows.empty()) return;
	
	// TODO!
	//   non-trivial problem to format this the best way possible... :(
	//   works nicely to print format that was got from previous file, but not if it was edited e.g. in user interface.
	// Note that the data is always correct, it is just not necessarily the most compact representation of the graph.
	
	fprintf(file, "connection(\"%s\") [", connection.name.data());
	
	Compartment_Ref *prev = nullptr;
	for(auto &pair : connection.arrows) {
		if(!prev || !(pair.first == *prev)) {
			fprintf(file, "\n\t");
			write_compartment_identifier_to_file(file, pair.first, data_set);
		}
		fprintf(file, " -> ");
		write_compartment_identifier_to_file(file, pair.second, data_set);
		prev = &pair.second;
	}
	fprintf(file, "\n]\n\n");
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
write_parameter_recursive(FILE *file, Par_Info &par, int level, int *offset, std::vector<int> &index_counts, int tabs) {
	
	int count = index_counts[index_counts.size() - level - 1];
	
	if(level == 0) {
		if(index_counts.size() != 1)
			print_tabs(file, tabs);
		for(int idx = 0; idx < count; ++idx) {
			int val_idx = (*offset)++;
			if(par.type == Decl_Type::par_enum) {
				fprintf(file, "%s ", par.values_enum[val_idx].data());
			} else {
				Parameter_Value &val = par.values[val_idx];
				switch(par.type) {
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
	} else {
		for(int idx = 0; idx < count; ++idx) {
			write_parameter_recursive(file, par, level-1, offset, index_counts, tabs);
			fprintf(file, "\n");
			if(level >= 2)
				fprintf(file, "\n");
		}
	}
}

void
write_parameter_to_file(FILE *file, Data_Set *data_set, Par_Group_Info& par_group, Par_Info &par, int tabs, bool double_newline = true) {
	
	int total_count = 1;
	std::vector<int> index_counts(par_group.index_sets.size());
	if(index_counts.size() > 0) {
		for(int idx = 0; idx < index_counts.size(); ++idx) {
			index_counts[idx] = data_set->index_sets[par_group.index_sets[idx]]->indexes.count();
			total_count *= index_counts[idx];
		}
	} else
		index_counts.push_back(1);  // There is a single non-indexed value
	
	if((par.type == Decl_Type::par_enum && total_count != par.values_enum.size()) || (par.type != Decl_Type::par_enum && total_count != par.values.size()))
		fatal_error(Mobius_Error::internal, "Somehow we have a data set where a parameter \"", par.name, "\" has a value array that is not sized correctly wrt. its index set dependencies.");
	
	print_tabs(file, tabs);
	fprintf(file, "%s(\"%s\")", name(par.type), par.name.data());
	if(index_counts.size() == 1) {
		fprintf(file, "\n");
		print_tabs(file, tabs);
		fprintf(file, "[ ");
	} else {
		fprintf(file, " [\n");
	}
	
	int offset = 0;
	write_parameter_recursive(file, par, index_counts.size()-1, &offset, index_counts, tabs+1);
	
	/*
	if(par.type == Decl_Type::par_enum) {
		for(std::string &val : par.values_enum)
			fprintf(file, "%s ", val.data());
	} else {
		// TODO: matrix formatting
		for(Parameter_Value &val : par.values) {
			switch(par.type) {
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
	*/
	if(index_counts.size() != 1)
		print_tabs(file, tabs);
	fprintf(file, "]\n");
	if(double_newline)
		fprintf(file, "\n");
}

void
write_par_group_to_file(FILE *file, Data_Set *data_set, Par_Group_Info &par_group, int tabs, bool double_newline = true) {
	print_tabs(file, tabs);
	fprintf(file, "par_group(\"%s\") ", par_group.name.data());
	if(par_group.index_sets.size() > 0) {
		fprintf(file, "[ ");
		int idx = 0;
		for(int index_set_idx : par_group.index_sets) {
			auto index_set = data_set->index_sets[index_set_idx];
			fprintf(file, "\"%s\" ", index_set->name.data());
		}
		fprintf(file, "] ");
	}
	fprintf(file, "{\n");
	int idx = 0;
	for(auto &par : par_group.pars)
		write_parameter_to_file(file, data_set, par_group, par, tabs+1, idx++ != par_group.pars.count()-1);
	
	print_tabs(file, tabs);
	fprintf(file, "}\n");
	if(double_newline)
		fprintf(file, "\n");
}

void
write_module_to_file(FILE *file, Data_Set *data_set, Module_Info &module) {
	fprintf(file, "module(\"%s\", %d, %d, %d) {\n", module.name.data(), module.version.major, module.version.minor, module.version.revision);
	int idx = 0;
	for(auto &par_group : module.par_groups)
		write_par_group_to_file(file, data_set, par_group, 1, idx++ != module.par_groups.count()-1);
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
	
	for(auto &compartment : compartments)
		write_compartment_info_to_file(file, compartment, this);
	
	for(auto &connection : connections)
		write_connection_info_to_file(file, connection, this);
	
	std::set<std::string> already_processed;
	for(auto &ser : series)
		write_series_to_file(file, main_file, ser, already_processed);
	
	for(auto &par_group : global_module.par_groups)
		write_par_group_to_file(file, this, par_group, 0);
	
	for(auto &module : modules)
		write_module_to_file(file, this, module);
	
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
		//stream->read_token();
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
		header.loc = token.source_loc;
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
	auto par = par_group->pars.create(arg->string_value, arg->source_loc);
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
		stream->expect_token(']'); //TODO: should give better error message here, along (expected n values for parameter, ...)
	}
	delete decl;
}

void
parse_par_group_decl(Data_Set *data_set, Module_Info *module, Token_Stream *stream, Decl_AST *decl) {
	match_declaration(decl, {{Token_Type::quoted_string}}, 0, false, 0);

	auto name = single_arg(decl, 0);
	auto group = module->par_groups.create(name->string_value, name->source_loc);
	
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
				int index_set_idx = data_set->index_sets.expect_exists_idx(&item, "index_set");
				group->index_sets.push_back(index_set_idx);
				expect_count *= data_set->index_sets[index_set_idx]->indexes.count();
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
				auto data = index_sets.create(name->string_value, name->source_loc);
				std::vector<Token> indexes;
				read_string_list(&stream, indexes);
				for(int idx = 0; idx < indexes.size(); ++idx)
					data->indexes.create(indexes[idx].string_value, indexes[idx].source_loc);
			} break;
			
			case Decl_Type::compartment : {
				match_declaration(decl, {{Token_Type::quoted_string}});
				
				auto name = single_arg(decl, 0);
				auto data = compartments.create(name->string_value, name->source_loc);
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
				auto data = connections.create(name->string_value, name->source_loc);
				
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
						String_View other_data = file_handler.load_file(other_file_name, single_arg(decl, 0)->source_loc, file_name);
						Token_Stream other_stream(other_file_name, other_data);
						other_stream.allow_date_time_tokens = true;
						
						series.push_back({});
						Series_Set_Info &data = series.back();
						data.file_name = std::string(other_file_name);
						read_series_data_block(this, &other_stream, &data);
					}
				} else {
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
				auto module = modules.create(name->string_value, name->source_loc);
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
				decl->source_loc.print_error_header();
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