
#include "data_set.h"


void
Data_Set::write_to_file(String_View file_name) {
	fatal_error(Mobius_Error::internal, "Write to file not implemented");
}

void
read_string_list(Token_Stream *stream, std::vector<Token> &push_to) {
	stream->expect_token('[');
	while(true) {
		Token token = stream->peek_token();
		if((char)token.type == ']') {
			stream->read_token();
			break;
		}
		stream->expect_quoted_string();
		push_to.push_back(token);
	}
}

void
read_neighbor_sequence(Token *first, Token_Stream *stream, Neighbor_Info *info, Index_Set_Info *index_set) {
	
	int idx_from = index_set->indexes.expect_exists_idx(first, "index");
	stream->expect_token(Token_Type::arr_r);
	Token second = stream->peek_token();
	stream->expect_quoted_string();
	int idx_to = index_set->indexes.expect_exists_idx(&second, "index");
	info->points_at[idx_from] = idx_to;
	
	Token token = stream->read_token();
	if(token.type == Token_Type::arr_r)
		read_neighbor_sequence(&second, stream, info, index_set);
	else if(token.type == Token_Type::quoted_string) {
		read_neighbor_sequence(&token, stream, info, index_set);
	} else if((char)token.type == ']')
		return;
	else {
		token.print_error_header();
		fatal_error("Expected a ], an -> or a quoted index name.");
	}
}

void
read_neighbor_data(Token_Stream *stream, Neighbor_Info *info, Index_Set_Info *index_set) {
	stream->expect_token('[');
	
	info->type = Neighbor_Info::Type::graph;
	
	Token token = stream->read_token();
	if((char)token.type == ']')
		return;
		
	if(token.type != Token_Type::quoted_string) {
		token.print_error_header();
		fatal_error("Expected a ] or a quoted index name.");
	}
	
	read_neighbor_sequence(&token, stream, info, index_set);
}

void
read_series_data_block(Data_Set *data_set, Token_Stream *stream, Series_Set_Info *data) {
	Token token = stream->peek_token();
	if(token.type == Token_Type::date) {
		data->start_date = stream->expect_datetime();
		data->has_date_vector = false;
	} else if(token.type != Token_Type::quoted_string) {
		token.print_error_header();
		fatal_error("Expected either a start date or the name of an input series.");
	}
	
	while(true) {
		Series_Header_Info header;
		header.name = stream->expect_quoted_string();
		while(true) {
			token = stream->peek_token();
			if((char)token.type != '[')
				break;
			stream->read_token();
			token = stream->peek_token();
			if(token.type == Token_Type::quoted_string) {
				std::vector<std::pair<String_View, int>> indexes;
				while(true) {
					stream->read_token();
					auto index_set = data_set->index_sets.expect_exists(&token, "index_set");
					stream->expect_token(':');
					token = stream->peek_token();
					stream->expect_quoted_string();
					int index      = index_set->indexes.expect_exists_idx(&token, "index");
					indexes.push_back(std::pair<String_View, int>{index_set->name, index});
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
					set_flag(&header.flags, &token);
					token = stream->read_token();
					if((char)token.type == ']')
						break;
					else if(token.type != Token_Type::identifier) {
						token.print_error_header();
						fatal_error("Expected a ] or another flag identifier");
					}
				}
			} else {
				token.print_error_header();
				fatal_error("Expected the name of an index set or a flag");
			}
			data->header_data.push_back(std::move(header));
		}
		Token token = stream->peek_token();
		if(token.type != Token_Type::quoted_string)
			break;
	}
	int rowlen = data->header_data.size();
	data->raw_values.reserve(1024*rowlen);
	while(true) {
		if(data->has_date_vector) {
			Date_Time date = stream->expect_datetime();
			data->dates.push_back(date);
		}
		for(int idx = 0; idx < rowlen; ++idx) {
			double val = stream->expect_real();
			data->raw_values.push_back(val);
		}
		Token token = stream->peek_token();
		if(!((data->has_date_vector && token.type == Token_Type::date) || (!data->has_date_vector && is_numeric(token.type))))
			break;
	}
}

void
parse_parameter_decl(Par_Group_Info *par_group, Token_Stream *stream, int expect_count) {
	auto decl = parse_decl_header(stream);
	match_declaration(decl, {{Token_Type::quoted_string}}, 0, false);
	auto par = par_group->pars.find_or_create(single_arg(decl, 0));
	par->type = decl->type;
	if(par->type == Decl_Type::par_enum) {
		std::vector<Token> list;
		read_string_list(stream, list);
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
parse_par_group_decl(Data_Set *data_set, Module_Info *module, Token_Stream *stream) {
	auto decl = parse_decl_header(stream);
	match_declaration(decl, {{Token_Type::quoted_string}}, 0, false, 0);

	auto name = single_arg(decl, 0);
	auto group = module->par_groups.find_or_create(name);
	
	int expect_count = 1;
	Token token = stream->peek_token();
	std::vector<Token> list;
	if((char)token.type == '[') {
		read_string_list(stream, list);
		for(Token &item : list) {
			auto index_set = data_set->index_sets.expect_exists(&item, "index_set");
			group->index_sets.push_back(item.string_value);
			expect_count *= index_set->indexes.count();
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
	
	delete decl;
}

void
Data_Set::read_from_file(String_View file_name) {
	if(main_file) {
		fatal_error(Mobius_Error::api_usage, "Tried make a data set read from a file ", file_name, ", but it already contains data from the file ", main_file, ".");
	}
	main_file = file_name;
	
	//TODO: have a file handler instead
	auto file_data = read_entire_file(file_name);
	
	Token_Stream stream(file_name, file_data);
	stream.allow_date_time_tokens = true;
	
	while(true) {
		Token token = stream.peek_token();
		if(token.type == Token_Type::eof) break;
		else if(token.type == Token_Type::quoted_string) {
			if(doc_string) {
				token.print_error_header();
				fatal_error("Duplicate doc strings for data set.");
			}
			doc_string = stream.expect_quoted_string();
			continue;
		} else if(token.type != Token_Type::identifier) {
			token.print_error_header();
			fatal_error("Expected an identifier (index_set, neighbor, series, module, or par_datetime)."); 
		}
		
		if(token.string_value == "index_set") {
			auto decl = parse_decl_header(&stream);
			match_declaration(decl, {{Token_Type::quoted_string}}, 0, false);
			
			auto name = single_arg(decl, 0);
			auto data = index_sets.find_or_create(name);
			std::vector<Token> indexes;
			read_string_list(&stream, indexes);
			for(int idx = 0; idx < indexes.size(); ++idx)
				data->indexes.find_or_create(&indexes[idx]);
			delete decl;
		} else if(token.string_value == "neighbor") {
			auto decl = parse_decl_header(&stream);
			match_declaration(decl, {{Token_Type::quoted_string, Token_Type::quoted_string}}, 0, false);
			
			auto name = single_arg(decl, 0);
			auto data = neighbors.find_or_create(name);
			
			Index_Set_Info *index_set = index_sets.expect_exists(single_arg(decl, 1), "index_set");
			data->index_set = index_set->name;
			data->points_at.resize(index_set->indexes.count(), -1);
			read_neighbor_data(&stream, data, index_set);
			delete decl;
		} else if(token.string_value == "series") {
			Token token2 = stream.peek_token(1);
			if((char)token2.type == '(') {
				auto decl = parse_decl_header(&stream);
				match_declaration(decl, {{Token_Type::quoted_string}}, 0, false);
				
				String_View file_name = single_arg(decl, 0)->string_value;
				warning_print("Reading series data from separate file not yet implemented.");
				
				delete decl;
			} else {
				stream.read_token();
				stream.expect_token('[');
				series.push_back({});
				Series_Set_Info &data = series.back();
				data.file_name = file_name;
				read_series_data_block(this, &stream, &data);
				stream.expect_token(']');
			}
		} else if(token.string_value == "par_datetime") {
			parse_parameter_decl(&global_pars, &stream, 1);
		} else if(token.string_value == "module") {
			auto decl = parse_decl_header(&stream);
			match_declaration(decl, {{Token_Type::quoted_string, Token_Type::integer, Token_Type::integer, Token_Type::integer}}, 0, false, 0);
			
			auto name = single_arg(decl, 0);
			auto module = modules.find_or_create(name);
			module->major    = single_arg(decl, 1)->val_int;
			module->minor    = single_arg(decl, 2)->val_int;
			module->revision = single_arg(decl, 3)->val_int;
			
			stream.expect_token('{');
			while(true) {
				token = stream.peek_token();
				if(token.type == Token_Type::identifier && token.string_value == "par_group") {
					parse_par_group_decl(this, module, &stream);
				} else if((char)token.type == '}') {
					stream.read_token();
					break;
				} else {
					token.print_error_header();
					fatal_error("Expected a } or a par_group declaration.");
				}
			}
			
		} else {
			token.print_error_header();
			fatal_error("Unknown declaration type \"", token.string_value, ".");
		}
	}
}