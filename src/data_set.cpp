
#include <set>
#include <sstream>

#include "data_set.h"
#include "ole_wrapper.h"


void
write_index_set_to_file(FILE *file, Data_Set *data_set, Index_Set_Info &index_set) {
	
	fprintf(file, "index_set(\"%s\") ", index_set.name.data());
	
	if(is_valid(index_set.sub_indexed_to)) {
		auto parent = data_set->index_sets[index_set.sub_indexed_to];
		fprintf(file, "@sub(\"%s\") ", parent->name.data());
	}
	if(!index_set.union_of.empty()) {
		fprintf(file, "@union(");
		int idx = 0;
		for(auto set_id : index_set.union_of) {
			auto set = data_set->index_sets[set_id];
			fprintf(file, "\"%s\"", set->name.data());
			if(idx != index_set.union_of.size()-1)
				fprintf(file, ", ");
			++idx;
		}
		fprintf(file, ")");
	}
	
	if(index_set.union_of.empty() && !is_valid(index_set.is_edge_of_connection)) { // NOTE: These don't have explicit index data.
		if(!is_valid(index_set.sub_indexed_to)) {
			fprintf(file, "[ ");
			data_set->index_data.write_indexes_to_file(file, index_set.id);
			fprintf(file, "]");
		} else {
			if(!index_set.union_of.empty())
				fatal_error(Mobius_Error::internal, "Unimplemented sub-indexed union index set.");
			
			fprintf(file, " [\n");
			s32 count = data_set->index_data.get_max_count(index_set.sub_indexed_to).index;
			for(int idx = 0; idx < count; ++idx) {
				Index_D parent_idx = Index_D  { index_set.sub_indexed_to, idx };
				fprintf(file, "\t");
				data_set->index_data.write_index_to_file(file, parent_idx);
				fprintf(file, " : [ ");
				data_set->index_data.write_indexes_to_file(file, index_set.id, parent_idx);
				fprintf(file, "]\n");
			}
			fprintf(file, "]");
		}
	}
	
	fprintf(file, "\n\n");
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
write_component_info_to_file(FILE *file, Component_Info &component, Data_Set *data_set, int n_tabs) {
	print_tabs(file, n_tabs+1);
	if(component.handle.empty())
		fprintf(file, "%s(\"%s\") [", name(component.decl_type), component.name.data());
	else
		fprintf(file, "%s : %s(\"%s\") [", component.handle.data(), name(component.decl_type), component.name.data());
		
	for(auto set_id : component.index_sets) {
		auto index_set = data_set->index_sets[set_id];
		fprintf(file, " \"%s\"", index_set->name.data());
	}
	fprintf(file, " ]\n");
}

void
write_indexed_compartment_to_file(FILE *file, Compartment_Ref &ref, Data_Set *data_set, Connection_Info &connection) {
	if(!is_valid(ref.id)) {
		fprintf(file, "out");
		return;
	}
	
	//TODO: This one could be written better.
	
	auto component = connection.components[ref.id];
	fprintf(file, "%s[", component->handle.data());
	int idx = 0;
	for(auto index_set_id : component->index_sets) {
		auto index_set = data_set->index_sets[index_set_id];
		
		bool quote;
		std::string index_name = data_set->index_data.get_index_name(ref.indexes, ref.indexes.indexes[idx], &quote);
		maybe_quote(index_name, quote);
		
		fprintf(file, " %s", index_name.data());
		++idx;
	}
	fprintf(file, " ]");
}

void
write_connection_info_to_file(FILE *file, Connection_Info &connection, Data_Set *data_set, int n_tabs = 0) {
	
	print_tabs(file, n_tabs);
	fprintf(file, "connection(\"%s\") {\n", connection.name.data());
	
	for(auto &component : connection.components)
		write_component_info_to_file(file, component, data_set, n_tabs);
	
	if(connection.type == Connection_Info::Type::directed_graph) {
		
		fprintf(file, "\n");
		print_tabs(file, n_tabs+1);
		if(is_valid(connection.edge_index_set)) {
			auto edge_set = data_set->index_sets[connection.edge_index_set];
			fprintf(file, "directed_graph(\"%s\") [", edge_set->name.data());
		} else
			fprintf(file, "directed_graph [");
		
		if(connection.arrows.empty()) return;
		
		// TODO!
		//   non-trivial problem to format this the best way possible... :(
		//   works nicely to print format that was got from previous file, but not if it was edited e.g. in user interface (if that is ever implemented).
		// Note that the data is always correct, it is just not necessarily the most compact representation of the graph.
		
		Compartment_Ref *prev = nullptr;
		for(auto &pair : connection.arrows) {
			if(!prev || !(pair.first == *prev)) {
				fprintf(file, "\n");
				print_tabs(file, n_tabs+2);
				write_indexed_compartment_to_file(file, pair.first, data_set, connection);
			}
			fprintf(file, " -> ");
			write_indexed_compartment_to_file(file, pair.second, data_set, connection);
			prev = &pair.second;
		}
		fprintf(file, "\n");
		print_tabs(file, n_tabs+1);
		fprintf(file, "]\n");
	} else if (connection.type == Connection_Info::Type::none) {
		// Nothing else to write.
	} else {
		fatal_error(Mobius_Error::internal, "Unimplemented connection info type in write_to_file.");
	}
	print_tabs(file, n_tabs);
	fprintf(file, "}\n\n");
}

void
write_parameter_to_file(FILE *file, Data_Set *data_set, Par_Group_Info& par_group, Par_Info &par, int tabs, bool double_newline = true) {
	
	int n_dims = std::max(1, (int)par_group.index_sets.size());
	
	int expect_count = data_set->index_data.get_instance_count(par_group.index_sets);
	if(
		   (par.type == Decl_Type::par_enum && expect_count != par.values_enum.size())
		|| (par.type != Decl_Type::par_enum && expect_count != par.values.size())
	)
		fatal_error(Mobius_Error::internal, "Somehow we have a data set where a parameter \"", par.name, "\" has a value array that is not sized correctly wrt. its index set dependencies.");
	
	print_tabs(file, tabs);
	fprintf(file, "%s(\"%s\")", name(par.type), par.name.data());
	if(n_dims == 1) {
		fprintf(file, "\n");
		print_tabs(file, tabs);
		fprintf(file, "[ ");
	} else {
		fprintf(file, " [\n");
	}
	
	int offset = 0;
	data_set->index_data.for_each(par_group.index_sets,
		[&](Indexes_D &indexes) {
			
			int val_idx = offset++;
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
		},
		[&](int pos) {
			int dist = (n_dims-1) - pos;
			if(dist == 1 && n_dims != 1)
				print_tabs(file, tabs+1);
			if(dist > 1)
				fprintf(file, "\n");
		}
		);

	if(n_dims != 1) {
		fprintf(file, "\n");
		print_tabs(file, tabs);
	}
	fprintf(file, "]\n");
	if(double_newline)
		fprintf(file, "\n");
}

void
write_par_group_to_file(FILE *file, Data_Set *data_set, Par_Group_Info &par_group, int tabs, bool double_newline = true) {
	if(par_group.error) return;
	
	print_tabs(file, tabs);
	fprintf(file, "par_group(\"%s\") ", par_group.name.data());
	if(par_group.index_sets.size() > 0) {
		fprintf(file, "[ ");
		int idx = 0;
		for(auto index_set_id : par_group.index_sets) {
			auto index_set = data_set->index_sets[index_set_id];
			fprintf(file, "\"%s\" ", index_set->name.data());
		}
		fprintf(file, "] ");
	}
	fprintf(file, "{\n");
	int idx = 0;
	for(auto &par : par_group.pars) {
		if(par.mark_for_deletion) continue;
		write_parameter_to_file(file, data_set, par_group, par, tabs+1, idx++ != par_group.pars.count()-1);
	}
	
	print_tabs(file, tabs);
	fprintf(file, "}\n");
	if(double_newline)
		fprintf(file, "\n");
}

void
write_module_to_file(FILE *file, Data_Set *data_set, Module_Info &module) {
	fprintf(file, "module(\"%s\", version(%d, %d, %d)) {\n", module.name.data(), module.version.major, module.version.minor, module.version.revision);
	int idx = 0;
	for(auto &connection : module.connections)
		write_connection_info_to_file(file, connection, data_set, 1);
	for(auto &par_group : module.par_groups)
		write_par_group_to_file(file, data_set, par_group, 1, idx++ != module.par_groups.count()-1);
	fprintf(file, "}\n\n");
}

void
write_series_to_file(FILE *file, std::string &main_file, Series_Set_Info &series, std::set<std::string> &already_processed) {
		
	// NOTE: If we read from an excel file, it will produce one Series_Set_Info record per page, but we only want to write out one import of the file.
	// TODO: 
	if(already_processed.find(series.file_name) != already_processed.end())
		return;
	
	already_processed.insert(series.file_name);
	
	//TODO: What do we do if the main file we save to is in a different folder than the original. Should we update all the relative paths of the included files?
	
	fprintf(file, "series(\"%s\")\n\n", series.file_name.data());
}

void
Data_Set::write_to_file(String_View file_name) {
	
	FILE *file = open_file(file_name, "w");
	
	bool error = false;
	try {
		if(!doc_string.empty()) {
			fprintf(file, "\"\"\"%s\n\"\"\"\n\n", doc_string.data());
		}
		
		if(time_step_was_provided) {
			std::string unit_str = time_step_unit.to_decl_str();
			fprintf(file, "time_step(%s)\n\n", unit_str.data());
		}
		
		for(auto &index_set : index_sets)
			write_index_set_to_file(file, this, index_set);
		
		for(auto &connection : global_module.connections)
			write_connection_info_to_file(file, connection, this);
		
		std::set<std::string> already_processed;
		for(auto &ser : series)
			write_series_to_file(file, main_file, ser, already_processed);
		
		for(auto &par_group : global_module.par_groups)
			write_par_group_to_file(file, this, par_group, 0);
		
		for(auto &module : modules)
			write_module_to_file(file, this, module);
		
	} catch(int) {
		error = true;
	}
	
	fclose(file);
	
	if(error)
		mobius_error_exit();
}

// TODO: rename it to something else.
void
read_string_list(Token_Stream *stream, std::vector<Token> &push_to, bool ident = false, bool allow_int = false) {
	stream->expect_token('[');
	while(true) {
		Token token = stream->peek_token();
		if((char)token.type == ']') {
			stream->read_token();
			break;
		}
		if(allow_int && token.type == Token_Type::integer)
			stream->expect_int();
		else {
			if(ident)
				stream->expect_identifier();
			else
				stream->expect_quoted_string();
		}
		push_to.push_back(token);
	}
}

Data_Id
make_sub_indexed_to(Data_Set *data_set, Index_Set_Info *data, Token *parent) {
	Index_Set_Info *sub_indexed_to = nullptr;
	
	data->sub_indexed_to = data_set->index_sets.expect_exists_idx(parent, "index_set");
	sub_indexed_to = data_set->index_sets[data->sub_indexed_to];
	if(is_valid(sub_indexed_to->sub_indexed_to)) {
		parent->source_loc.print_error_header();
		fatal_error("We currently don't support sub-indexing under an index set that is itself sub-indexed.");
	}
	
	return data->sub_indexed_to;
}

Data_Id
parse_index_set_decl(Data_Set *data_set, Token_Stream *stream, Decl_AST *decl) {
	
	match_declaration(decl,	{{Token_Type::quoted_string}}, false, false);
	
	auto name = single_arg(decl, 0);
	auto index_set_id = data_set->index_sets.create(name->string_value, name->source_loc);
	auto data = data_set->index_sets[index_set_id];
	
	Data_Id sub_indexed_to = invalid_data;
	
	while(true) {
		auto peek = stream->peek_token();
		if((char)peek.type == '@') {
			stream->read_token();
			Decl_Base_AST *note = new Decl_Base_AST();
			parse_decl_header_base(note, stream, false);
			auto str = note->decl.string_value;
			if(str == "sub") {
				match_declaration_base(note, {{Token_Type::quoted_string}}, 0);
				sub_indexed_to = make_sub_indexed_to(data_set, data, single_arg(note, 0));
			} else if (str == "union") {
				match_declaration_base(note, {{Token_Type::quoted_string, {Token_Type::quoted_string, true}}}, 0);
				for(int argidx = 0; argidx < note->args.size(); ++argidx) {
					auto set_id = data_set->index_sets.expect_exists_idx(single_arg(note, argidx), "index_set");
					if(std::find(data->union_of.begin(), data->union_of.end(), set_id) != data->union_of.end()) {
						single_arg(note, argidx)->print_error_header();
						fatal_error("Double appearance of index set in union declaration.");
					}
					auto set_data = data_set->index_sets[set_id];
					if(is_valid(set_data->sub_indexed_to) || !set_data->union_of.empty()) {
						single_arg(note, argidx)->print_error_header();
						fatal_error("It is not currently supported to have unions of sub-indexed index sets or other unions.");
					}
					data->union_of.push_back(set_id);
				}
				
			} else {
				note->decl.print_error_header();
				fatal_error("Unrecognized note '", str, "' for 'index_set' declaration.");
			}
			
			delete note;
		} else break;
	}
	
	if(!data->union_of.empty() && is_valid(data->sub_indexed_to)) {
		decl->source_loc.print_error_header();
		fatal_error("It is not currently supported that an index set can both be sub-indexed and be a union.");
	}
	
	if(!data->union_of.empty()) {
		data_set->index_data.initialize_union(index_set_id, decl->source_loc);
		return index_set_id;
	}
	
	auto peek0 = stream->peek_token();
	if((char)peek0.type != '[')
		return index_set_id;
	
	auto peek = stream->peek_token(1);
	bool found_sub_indexes = false;
	if(peek.type == Token_Type::quoted_string || peek.type == Token_Type::integer) {
		peek = stream->peek_token(2);
		if((char)peek.type == ':') {
			found_sub_indexes = true;
			if(!is_valid(sub_indexed_to)) {
				decl->source_loc.print_error_header();
				fatal_error("Got sub-indexes for an index set that is not sub-indexed.");
			}
			stream->expect_token('[');
			while(true) {
				auto token = stream->read_token();
				auto parent_idx = data_set->index_data.find_index(sub_indexed_to, &token);
				stream->expect_token(':');
				
				std::vector<Token> indexes;
				read_string_list(stream, indexes, false, true);
				
				data_set->index_data.set_indexes(index_set_id, indexes, parent_idx);
			
				peek = stream->peek_token();
				if((char)peek.type == ']') {
					stream->read_token();
					break;
				} else if (peek.type == Token_Type::eof) {
					peek.print_error_header();
					fatal_error("End of file before an index_set declaration was closed.");
				}
			}
		} else {
			if (is_valid(data->sub_indexed_to)) {
				decl->source_loc.print_error_header();
				fatal_error("Missing sub-indexes for a sub-indexed index set.");
			}
			
			std::vector<Token> indexes;
			read_string_list(stream, indexes, false, true);	
			data_set->index_data.set_indexes(index_set_id, indexes);
		}
	} else {
		peek.print_error_header();
		fatal_error("Expected a quoted string or a numeric value");
	}
	
	if(!data_set->index_data.are_all_indexes_set(index_set_id)) {
		decl->source_loc.print_error_header();
		fatal_error("The index set \"", data->name, "\" was not fully initialized.");
	}
	
	return index_set_id;
}

void
read_compartment_identifier(Data_Set *data_set, Token_Stream *stream, Compartment_Ref *read_to, Connection_Info *info) {
	Token token = stream->peek_token();
	stream->expect_identifier();
	
	if(token.string_value == "out") {
		read_to->id = invalid_data;
		return;
	}
	auto find = info->component_handle_to_id.find(token.string_value);
	if(find == info->component_handle_to_id.end()) {
		token.print_error_header();
		fatal_error("The handle '", token.string_value, "' does not refer to an already declared component.");
	}
	read_to->id = find->second;
	auto comp_data = info->components[read_to->id];
	std::vector<Token> index_names;
	read_string_list(stream, index_names, false, true);
	if(index_names.size() != comp_data->index_sets.size()) {
		token.print_error_header();
		fatal_error("The component '", token.string_value, "' should be indexed with ", comp_data->index_sets.size(), " indexes.");
	}
	
	data_set->index_data.find_indexes(comp_data->index_sets, index_names, read_to->indexes);
}

void
read_connection_sequence(Data_Set *data_set, Compartment_Ref *first_in, Token_Stream *stream, Connection_Info *info) {
	
	std::pair<Compartment_Ref, Compartment_Ref> arrow;
	if(!first_in)
		read_compartment_identifier(data_set, stream, &arrow.first, info);
	else
		arrow.first = *first_in;
	
	if(!is_valid(arrow.first.id)) {
		info->source_loc.print_error_header();
		fatal_error("An 'out' can only be the target of an arrow, not the source.");
	}
	
	auto token = stream->peek_token();
	stream->expect_token(Token_Type::arr_r);
	
	read_compartment_identifier(data_set, stream, &arrow.second, info);
	
	info->arrows.push_back(arrow);
	{	// Make an edge index for the arrow if necessary.
		auto first_comp = info->components[arrow.first.id];
	
		if(is_valid(info->edge_index_set) && first_comp->can_have_edge_index) {
			
			Index_D parent_idx = Index_D::no_index();
			auto parent_set = data_set->index_sets[info->edge_index_set]->sub_indexed_to;
			if(!is_valid(parent_set))
				fatal_error(Mobius_Error::internal, "Got an edge index set that was not sub-indexed to the connection component index.");
			
			if(first_comp->index_sets.size() != 1)  // TODO: We should allow 0 also.
				fatal_error(Mobius_Error::internal, "Got an unsupported amount of indexes for a connection component with an indexed edge.");
			
			parent_idx = arrow.first.indexes.get_index(data_set->index_data, first_comp->index_sets[0]);
			if(!is_valid(parent_idx))
				fatal_error(Mobius_Error::internal, "Something went wrong with looking up the component index.");
			
			std::string index_name;
			if(is_valid(arrow.second.id)) {
				auto count = arrow.second.indexes.count();
				if(count == 0) {
					index_name = info->components[arrow.second.id]->name;
				} else {
					if(count == 1)
						index_name = data_set->index_data.get_index_name(arrow.second.indexes, arrow.second.indexes.indexes[0]);
					else {
						std::vector<std::string> index_names;
						data_set->index_data.get_index_names(arrow.second.indexes, index_names);
						std::stringstream ss;
						for(int pos = 0; pos < index_names.size(); ++pos) {
							ss << index_names[pos];
							if(pos != index_names.size()-1) ss << ';';
						}
						index_name = ss.str();
					}
				} 
			} else
				index_name = "out";
			
			// TODO: There could be potential name clashes here.
			data_set->index_data.add_edge_index(info->edge_index_set, index_name, token.source_loc, parent_idx);
		}
	}
	
	token = stream->peek_token();
	if(token.type == Token_Type::arr_r) {
		read_connection_sequence(data_set, &arrow.second, stream, info);
	} else if(token.type == Token_Type::identifier) {
		read_connection_sequence(data_set, nullptr, stream, info);
	} else if((char)token.type == ']') {
		stream->read_token();
		return;
	} else {
		token.print_error_header();
		fatal_error("Expected a ], an -> or a component identifier.");
	}
}

void parse_connection_decl(Data_Set *data_set, Module_Info *module, Token_Stream *stream, Decl_AST *decl) {
	match_declaration(decl, {{Token_Type::quoted_string}}, false, false);
						
	auto name = single_arg(decl, 0);
	auto info_id = module->connections.create(name->string_value, name->source_loc);
	auto info    = module->connections[info_id];
	
	stream->expect_token('{');
	
	Token token = stream->peek_token();
	if((char)token.type == '}') {
		stream->read_token();
		return;
	}
	
	bool data_found = false;
	
	while(true) {
		auto token  = stream->peek_token();
		
		if((char)token.type == '}') {
			stream->read_token();
			return;
		}
		
		Decl_AST *decl = parse_decl_header(stream);
		
		if (decl->type == Decl_Type::directed_graph) {
			
			if(data_found) {
				decl->source_loc.print_error_header();
				fatal_error("Multiple data for the same connection");
			}
			data_found = true;
			
			int which = match_declaration(decl, {{}, {Token_Type::quoted_string}}, false, false);
			
			if(which == 1) {
				auto edge_set_id = data_set->index_sets.expect_exists_idx(single_arg(decl, 0), "index_set");
				auto edge_set_data = data_set->index_sets[edge_set_id];
				edge_set_data->is_edge_of_connection = info_id;
				
				info->edge_index_set = edge_set_id;
				
				if(!edge_set_data->union_of.empty()) {
					edge_set_data->source_loc.print_error_header();
					fatal_error("An edge index set can not be a union.");
				}
				
				if(data_set->index_data.are_all_indexes_set(edge_set_id)) {
					edge_set_data->source_loc.print_error_header();
					fatal_error("Edge index sets should not receive index data explicitly.");
				}
				
				data_set->index_data.initialize_edge_index_set(edge_set_id, edge_set_data->source_loc);
				
				for(auto &component : info->components) {  // NOTE: If they were not declared before this, they could not be referenced any way.
					if(component.index_sets.size() == 1) {
						if(data_set->index_data.can_be_sub_indexed_to(component.index_sets[0], edge_set_id))
							component.can_have_edge_index = true;
					} else if(component.index_sets.empty())
						component.can_have_edge_index = true;
				}
			}
			
			info->type = Connection_Info::Type::directed_graph;
			
			stream->expect_token('[');
			read_connection_sequence(data_set, nullptr, stream, info);
			
			delete decl;
		} else if (decl->type == Decl_Type::compartment || decl->type == Decl_Type::quantity) {
			match_declaration(decl, {{Token_Type::quoted_string}}, true, false);
			
			auto name = single_arg(decl, 0);
			auto comp_id = info->components.create(name->string_value, name->source_loc);
			auto data =    info->components[comp_id];
			
			data->decl_type = decl->type;
			data->handle = decl->handle_name.string_value;
			if(decl->handle_name.string_value.count)
				info->component_handle_to_id[decl->handle_name.string_value] = comp_id;
			std::vector<Token> idx_set_list;
			read_string_list(stream, idx_set_list);
			
			for(auto &name : idx_set_list) {
				auto ref = data_set->index_sets.expect_exists_idx(&name, "index_set");
				data->index_sets.push_back(ref);
				auto sub_indexed_to = data_set->index_sets[ref]->sub_indexed_to;
				if(is_valid(sub_indexed_to)) {
					bool found = false;
					for(auto prev : data->index_sets) {
						if(prev == sub_indexed_to) {
							found = true;
							break;
						}
					}
					if(!found) {
						name.print_error_header();
						fatal_error("The index set \"", name.string_value, "\" is sub-indexed to another index set \"", data_set->index_sets[sub_indexed_to]->name, "\", but it does not appear after it on the index set list for this component declaration.");
					}
				}
			}
			delete decl;
		} else {
			decl->source_loc.print_error_header();
			fatal_error("Did not expect a '", ::name(decl->type), "' declaration inside a 'connection' declaration in the data set.");
		}
	}
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
		header.source_loc = token.source_loc;
		header.name = std::string(stream->expect_quoted_string());
		while(true) {
			token = stream->peek_token();
			if((char)token.type != '[')
				break;
			stream->read_token();
			token = stream->peek_token();
			if(token.type == Token_Type::quoted_string) {
				std::vector<Data_Id> index_sets;
				std::vector<Token>   index_names;
				
				while(true) {
					stream->read_token();
					auto index_set_idx = data_set->index_sets.expect_exists_idx(&token, "index_set");
					
					stream->expect_token(':');
					auto next = stream->read_token();
					index_sets.push_back(index_set_idx);
					index_names.push_back(next);
				
					next = stream->peek_token();
					if((char)next.type == ']') {
						stream->read_token();
						break;
					}
					if(next.type != Token_Type::quoted_string) {
						next.print_error_header();
						fatal_error("Expected a ] or a new index set name.");
					}
				}
				
				data_set->index_data.check_valid_distribution(index_sets, token.source_loc);
				
				Indexes_D indexes;
				data_set->index_data.find_indexes(index_sets, index_names, indexes);
				
				header.indexes.push_back(std::move(indexes));
			} else if(token.type == Token_Type::identifier || (char)token.type == '[') {
				while(true) {
					if((char)token.type == '[') {
						auto unit_decl = parse_decl_header(stream);
						header.unit.set_data(unit_decl);
						delete unit_decl;
					} else {
						bool success = set_flag(&header.flags, token.string_value);
						if(!success) {
							token.print_error_header();
							fatal_error("Unrecognized input flag \"", token.string_value, "\".");
						}
					}
					token = stream->read_token();
					if((char)token.type == ']')
						break;
					else if(token.type != Token_Type::identifier && (char)token.type != '[') {
						token.print_error_header();
						fatal_error("Expected a ], another flag identifier or a unit declaration.");
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
	match_declaration(decl, {{Token_Type::quoted_string}}, false, false);
	Token *arg = single_arg(decl, 0);
	auto par_id = par_group->pars.create(arg->string_value, arg->source_loc);
	auto par = par_group->pars[par_id];
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
				fatal_error("Expected a parameter value of type ", name(get_value_type(decl->type)), ", or a ']'.");
			}
		}
		auto next = stream->read_token();
		if((char)next.type != ']') {
			fatal_error("Expected only ", expect_count, " values for parameter.");
		}
	}
	delete decl;
}

void
parse_par_group_decl(Data_Set *data_set, Module_Info *module, Token_Stream *stream, Decl_AST *decl) {
	match_declaration(decl, {{Token_Type::quoted_string}}, false, false);

	auto name = single_arg(decl, 0);
	auto group_id = module->par_groups.create(name->string_value, name->source_loc);
	auto group    = module->par_groups[group_id];
	
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
				auto set_id = data_set->index_sets.expect_exists_idx(&item, "index_set");
				group->index_sets.push_back(set_id);
			}
		}
	}
	data_set->index_data.check_valid_distribution(group->index_sets, token.source_loc);
	
	int expect_count = data_set->index_data.get_instance_count(group->index_sets);
	
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
	if(main_file != "")
		fatal_error(Mobius_Error::api_usage, "Tried make a data set read from a file ", file_name, ", but it already contains data from the file ", main_file, ".");
	
	main_file = std::string(file_name);
	
	auto file_data = file_handler.load_file(file_name);
	
	Token_Stream stream(file_name, file_data);
	stream.allow_date_time_tokens = true;
	
#if OLE_AVAILABLE
	OLE_Handles handles = {};
#endif
	
	bool error = false;
	try {
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
					parse_index_set_decl(this, &stream, decl);
				} break;
				
				case Decl_Type::connection : {
					parse_connection_decl(this, &global_module, &stream, decl);
				} break;
				
				case Decl_Type::series : {
					
					match_declaration(decl, {{Token_Type::quoted_string}}, false, false);
					
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
					
				} break;
				
				case Decl_Type::par_group : {
					parse_par_group_decl(this, &global_module, &stream, decl);
				} break;

				case Decl_Type::module : {
					int which = match_declaration(decl, 
					{
						{Token_Type::quoted_string, Decl_Type::version},
						{Token_Type::quoted_string, Token_Type::integer, Token_Type::integer, Token_Type::integer}
					}, false, false);
				
					auto name = single_arg(decl, 0);
					auto module_id = modules.create(name->string_value, name->source_loc);
					auto module = modules[module_id];
					if(which == 0) {
						auto version_decl = decl->args[1]->decl;
						match_declaration(version_decl, {{Token_Type::integer, Token_Type::integer, Token_Type::integer}}, false, false);
						module->version.major    = single_arg(version_decl, 0)->val_int;
						module->version.minor    = single_arg(version_decl, 1)->val_int;
						module->version.revision = single_arg(version_decl, 2)->val_int;
					} else {
						decl->source_loc.print_log_header();
						log_print("The format module(name, major, minor, revision) is deprecated in favor of module(name, version(major, minor, revision)) (this will be fixed if you save the data again).\n");
						module->version.major    = single_arg(decl, 1)->val_int;
						module->version.minor    = single_arg(decl, 2)->val_int;
						module->version.revision = single_arg(decl, 3)->val_int;
					}
					
					stream.expect_token('{');
					while(true) {
						token = stream.peek_token();
						if(token.type == Token_Type::identifier && token.string_value == "par_group") {
							Decl_AST *decl2 = parse_decl_header(&stream);
							parse_par_group_decl(this, module, &stream, decl2);
							delete decl2;
						} else if(token.type == Token_Type::identifier && token.string_value == "connection") {
							Decl_AST *decl2 = parse_decl_header(&stream);
							parse_connection_decl(this, module, &stream, decl2);
							delete decl2;
						} else if((char)token.type == '}') {
							stream.read_token();
							break;
						} else {
							token.print_error_header();
							fatal_error("Expected a } or a 'par_group' declaration.");
						}
					}
				} break;
				
				case Decl_Type::time_step : {
					match_declaration(decl, {{Decl_Type::unit}}, false);
					time_step_unit.set_data(decl->args[0]->decl);
					unit_source_loc = decl->source_loc;
					time_step_was_provided = true;
				} break;
				
				default : {
					decl->source_loc.print_error_header();
					fatal_error("Did not expect a declaration of type '", name(decl->type), "' in a data set.");
				} break;
			}
			
			delete decl;
		}
		
		for(auto &index_set : index_sets) {
			if(!index_data.are_all_indexes_set(index_set.id)) {
				index_set.source_loc.print_error_header();
				fatal_error("The index set \"", index_set.name, "\" did not receive index data.");
			}
		}
		
	} catch(int) {
		// NOTE: Catch it so that we get to properly unload file data below.
		error = true;
	}
	
#if OLE_AVAILABLE
	ole_close_app_and_spreadsheet(&handles);
#endif
	file_handler.unload_all(); // Free the file data.
	
	if(error)
		mobius_error_exit(); // Re-throw.
}


void
Data_Set::generate_index_data(const std::string &name, const std::string &sub_indexed_to, const std::vector<std::string> &union_of) {
	
	// NOTE: We don't check for correctness of sub-indexing vs. union here since we assume this was done by the model.
	
	Source_Location loc = {};
	auto id = index_sets.create(name, loc);
	auto set = index_sets[id];
	
	auto sub_to = invalid_data;
	if(!sub_indexed_to.empty()) {
		sub_to = index_sets.find_idx(sub_indexed_to);
		if(!is_valid(sub_to))
			fatal_error(Mobius_Error::internal, "Did not find the parent index set ", sub_indexed_to, " in generate_index_data.");
		set->sub_indexed_to = sub_to;
	}
		
	if(!union_of.empty()) {
		for(auto ui : union_of) {
			auto ui_id = index_sets.find_idx(ui);
			if(!is_valid(ui_id))
				fatal_error(Mobius_Error::internal, "Did not find the union member ", ui, " in generate_index_data.");
			set->union_of.push_back(ui_id);
		}
		
		index_data.initialize_union(id, loc);
		return;
	}
	
	s32 instance_count = 1;
	if(is_valid(sub_to))
		instance_count = index_data.get_max_count(sub_to).index;
	
	Token count;
	count.source_loc = loc;
	count.type = Token_Type::integer;
	count.val_int = 1;
	
	for(int par_idx = 0; par_idx < instance_count; ++par_idx) {
		Index_D parent_idx = Index_D { sub_to, par_idx };
		index_data.set_indexes(id, { count }, parent_idx);
	}
}


