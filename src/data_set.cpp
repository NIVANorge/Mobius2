
#include <set>

#include "data_set.h"
#include "ole_wrapper.h"

int
Index_Set_Info::get_index(Token *idx_name, int index_of_super) {
	int super = (sub_indexed_to >= 0) ? index_of_super : 0;
	if(idx_name->type == Token_Type::quoted_string) {
		return indexes[super].indexes.expect_exists_idx(idx_name, "index");
	} else if (idx_name->type == Token_Type::integer) {
		int idx = idx_name->val_int;
		if(!check_index(idx, super)) {
			idx_name->print_error_header();
			fatal_error("Index is out of bounds for this index set.");
		}
		return idx;
	} else {
		idx_name->print_error_header();
		fatal_error("Only quoted strings and integers can be used to identify indexes.");
	}
}

int 
Index_Set_Info::get_index(const char *buf, int index_of_super) {
	int super = (sub_indexed_to >= 0) ? index_of_super : 0;
	return indexes[super].indexes.find_idx(buf);
}

bool
Index_Set_Info::check_index(int idx, int index_of_super) {
	int super = (sub_indexed_to >= 0) ? index_of_super : 0;
	return idx >= 0 && idx < indexes[super].get_count();
}

void
write_index_set_indexes_to_file(FILE *file, Sub_Indexing_Info *info) {
	fprintf(file, "[ ");
	if(info->type == Sub_Indexing_Info::Type::named) {
		for(auto &index : info->indexes)
			fprintf(file, "\"%s\" ", index.name.data());
	} else if (info->type == Sub_Indexing_Info::Type::numeric1)
		fprintf(file, "%d ", info->n_dim1);
	else
		fatal_error(Mobius_Error::internal, "Unhandled index set type in write_index_set_indexes_to_file().");
	fprintf(file, "]");
}

void
write_index_set_to_file(FILE *file, Data_Set *data_set, Index_Set_Info &index_set) {
	
	if(index_set.is_edge_index_set) return; // These are not given explicitly, only as arrows in a connection.
	
	if(index_set.sub_indexed_to < 0) {
		fprintf(file, "index_set(\"%s\") ", index_set.name.data());
		write_index_set_indexes_to_file(file, &index_set.indexes[0]);
	} else {
		auto parent = data_set->index_sets[index_set.sub_indexed_to];
		fprintf(file, "index_set(\"%s\", \"%s\") [\n", parent->name.data(), index_set.name.data());

		int count = parent->get_max_count();
		if(count != index_set.indexes.size())
			fatal_error(Mobius_Error::internal, "Got a sub-indexed index set without a set for each index of the parent in write_index_set_to_file().");
		auto &parent_info = parent->indexes[0];
		for(int idx = 0; idx < count; ++idx) {
			if(parent_info.type == Sub_Indexing_Info::Type::named)
				fprintf(file, "\t\"%s\" : ", parent_info.indexes[idx]->name.data());
			else if (parent_info.type == Sub_Indexing_Info::Type::numeric1)
				fprintf(file, "\t%d : ", idx);
			else
				fatal_error(Mobius_Error::internal, "Unhandled index set type in write_index_set_to_file().");
			write_index_set_indexes_to_file(file, &index_set.indexes[idx]);
			fprintf(file, "\n");
		}
		fprintf(file, "]");
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
		
	for(int idx_set_idx : component.index_sets) {
		auto index_set = data_set->index_sets[idx_set_idx];
		fprintf(file, " \"%s\"", index_set->name.data());
	}
	fprintf(file, " ]");
	if(component.edge_index_set >= 0) {
		auto index_set = data_set->index_sets[component.edge_index_set];
		fprintf(file, " [ \"%s\" ]", index_set->name.data());
	}
	fprintf(file, "\n");
}

void
write_indexed_compartment_to_file(FILE *file, Compartment_Ref &ref, Data_Set *data_set, Connection_Info &connection) {
	if(ref.id < 0) {
		fprintf(file, "out");
		return;
	}
	auto component = connection.components[ref.id];
	fprintf(file, "%s[", component->handle.data());
	for(int loc = 0; loc < ref.indexes.size(); ++loc) {
		auto index_set = data_set->index_sets[component->index_sets[loc]];
		int super_idx = 0;
		if(index_set->sub_indexed_to >= 0) {
			super_idx = -1;
			for(int loc2 = 0; loc2 < loc; ++loc2) {
				if(component->index_sets[loc2] == index_set->sub_indexed_to) {
					super_idx = ref.indexes[loc2];
					break;
				}
			}
			if(super_idx < 0)
				fatal_error(Mobius_Error::internal, "Parent index set of sub-indexed index set was not set up correctly before write_indexed_compartment_to_file.");
		}
		auto &indexes = index_set->indexes[super_idx];
		if(indexes.type == Sub_Indexing_Info::Type::named)
			fprintf(file, " \"%s\"", indexes.indexes[ref.indexes[loc]]->name.data());
		else if(indexes.type == Sub_Indexing_Info::Type::numeric1)
			fprintf(file, " %d", ref.indexes[loc]);
		else
			fatal_error(Mobius_Error::internal, "Unhandled index type in write_indexed_compartment_to_file().");
	}
	fprintf(file, " ]");
}

void
write_connection_info_to_file(FILE *file, Connection_Info &connection, Data_Set *data_set, int n_tabs = 0) {
	
	print_tabs(file, n_tabs);
	fprintf(file, "connection(\"%s\") [\n", connection.name.data());
	
	for(auto &component : connection.components)
		write_component_info_to_file(file, component, data_set, n_tabs);
	
	if(connection.type == Connection_Info::Type::graph) {
		if(connection.arrows.empty()) return;
		
		// TODO!
		//   non-trivial problem to format this the best way possible... :(
		//   works nicely to print format that was got from previous file, but not if it was edited e.g. in user interface (if that is ever implemented).
		// Note that the data is always correct, it is just not necessarily the most compact representation of the graph.
		
		Compartment_Ref *prev = nullptr;
		for(auto &pair : connection.arrows) {
			if(!prev || !(pair.first == *prev)) {
				fprintf(file, "\n");
				print_tabs(file, n_tabs+1);
				write_indexed_compartment_to_file(file, pair.first, data_set, connection);
			}
			fprintf(file, " -> ");
			write_indexed_compartment_to_file(file, pair.second, data_set, connection);
			prev = &pair.second;
		}
		fprintf(file, "\n");
	} else if (connection.type == Connection_Info::Type::none) {
		// Nothing else to write.
	} else {
		fatal_error(Mobius_Error::internal, "Unimplemented connection info type in write_to_file.");
	}
	print_tabs(file, n_tabs);
	fprintf(file, "]\n\n");
}

int
get_instance_count(Data_Set *data_set, const std::vector<int> &index_sets, Source_Location *error_loc = nullptr){
	int count = 1;
	std::vector<u8> already_counted(index_sets.size());
	
	for(int level = index_sets.size()-1; level >= 0; --level) {
		if(already_counted[level]) continue;
		
		auto index_set = data_set->index_sets[index_sets[level]];
		if(index_set->sub_indexed_to >= 0) {
			bool found = false;
			for(int level_parent = 0; level_parent < level; ++level_parent) {
				if(index_sets[level_parent] == index_set->sub_indexed_to) {
					found = true;
					already_counted[level_parent] = true;
					break;
				}
			}
			if(!found) {
				if(error_loc) error_loc->print_error_header();
				fatal_error("Got an index set \"", index_set->name, "\" that is sub-indexed to another index set \"",
					data_set->index_sets[index_set->sub_indexed_to]->name, "\", but in this index sequence, the former doesn't follow the latter.");
			}
			int sum = 0;
			for(auto &idxs : index_set->indexes) sum += idxs.get_count();
			count *= sum;
		} else
			count *= index_set->get_max_count();
	}
	return count;
}

void
write_parameter_recursive(FILE *file, Data_Set *data_set, Par_Info &par, int level, std::vector<int> &indexes, int *offset, const std::vector<int> &index_sets, int tabs) {
	
	int count = 1;
	if(!index_sets.empty()) {
		auto index_set = data_set->index_sets[index_sets[level]];
		if(index_set->sub_indexed_to >= 0) {
			int parent_idx = -1;
			for(int l2 = 0; l2 < index_sets.size(); ++l2) {
				if(index_sets[l2] == index_set->sub_indexed_to) {
					parent_idx = indexes[l2];
					break;
				}
			}
			if(parent_idx < 0)
				fatal_error(Mobius_Error::internal, "Got a sub-indexed index set without the parent index set in write_parameter_recursive.");
			count = index_set->get_count(parent_idx);
		} else
			count = index_set->get_max_count();
	}
	
	if(index_sets.empty() || level == (int)index_sets.size()-1) {
		if(index_sets.size() > 1)
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
			indexes[level] = idx;
			write_parameter_recursive(file, data_set, par, level+1, indexes, offset, index_sets, tabs);
			fprintf(file, "\n");
			if(level < (int)index_sets.size() - 2 && idx != count-1)
				fprintf(file, "\n");
		}
	}
}

void
write_parameter_to_file(FILE *file, Data_Set *data_set, Par_Group_Info& par_group, Par_Info &par, int tabs, bool double_newline = true) {
	
	int n_dims = std::max(1, (int)par_group.index_sets.size());
	
	int expect_count = get_instance_count(data_set, par_group.index_sets);
	if((par.type == Decl_Type::par_enum && expect_count != par.values_enum.size()) || (par.type != Decl_Type::par_enum && expect_count != par.values.size()))
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
	std::vector<int> indexes(par_group.index_sets.size(), -1);
	write_parameter_recursive(file, data_set, par, 0, indexes, &offset, par_group.index_sets, tabs+1);
	
	if(n_dims != 1)
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
	if(series.file_name == main_file) {
		fatal_error(Mobius_Error::internal, "Inlining series data in main data file is not yet supported!");
	}
	
	// NOTE: If we read from an excel file, it will produce one Series_Set_Info record per page, but we only want to write out one import of the file.
	if(already_processed.find(series.file_name) != already_processed.end())
		return;
	
	already_processed.insert(series.file_name);
	
	//TODO: What do we do if the file we save to is in a different folder than the original. Should we update all the relative paths of the included files?
	
	fprintf(file, "series(\"%s\")\n\n", series.file_name.data());
}

void
Data_Set::write_to_file(String_View file_name) {
	
	FILE *file = open_file(file_name, "w");
	
	bool error = false;
	try {
		if(!doc_string.empty()) {
			fprintf(file, "\"\"\"\n%s\n\"\"\"\n\n", doc_string.data());
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

void
parse_sub_indexes(Sub_Indexing_Info *set, Token_Stream *stream) {
	//TODO: Error if these indexes were already given (?)
	auto peek = stream->peek_token(1);
	if(peek.type == Token_Type::quoted_string) {
		set->type = Sub_Indexing_Info::Type::named;
		std::vector<Token> indexes;
		read_string_list(stream, indexes);
		for(int idx = 0; idx < indexes.size(); ++idx)
			set->indexes.create(indexes[idx].string_value, indexes[idx].source_loc);
	} else if (peek.type == Token_Type::integer) {
		set->type = Sub_Indexing_Info::Type::numeric1;
		set->n_dim1 = peek.val_int;
		if(set->n_dim1 < 1) {
			peek.print_error_header();
			fatal_error("You can only have a positive number for a dimension size.");
		}
		stream->expect_token('[');
		stream->expect_int();
		stream->expect_token(']');
	} else {
		peek.print_error_header();
		fatal_error("Expected a list of quoted strings or a single integer.");
	}
}

Index_Set_Info *
make_sub_indexed_to(Data_Set *data_set, Index_Set_Info *data, Token *parent) {
	Index_Set_Info *sub_indexed_to = nullptr;
	
	data->sub_indexed_to = data_set->index_sets.expect_exists_idx(parent, "index_set");
	sub_indexed_to = data_set->index_sets[data->sub_indexed_to];
	if(sub_indexed_to->sub_indexed_to != -1) {
		parent->source_loc.print_error_header();
		fatal_error("We currently don't support sub-indexing under an index set that is itself sub-indexed.");
	}
	data->indexes.resize(sub_indexed_to->get_max_count()); // NOTE: Should be correct to use max count since what we are sub-indexed to can't itself be sub-indexed.
	
	return sub_indexed_to;
}

void
parse_index_set_decl(Data_Set *data_set, Token_Stream *stream, Decl_AST *decl) {
	int which = match_declaration(decl,
		{
			{Token_Type::quoted_string},
			{Token_Type::quoted_string, Token_Type::quoted_string},
		}, false, false);
				
	auto name = single_arg(decl, which);
	auto data = data_set->index_sets.create(name->string_value, name->source_loc);
	
	Index_Set_Info *sub_indexed_to = nullptr;
	if(which == 1) {
		sub_indexed_to = make_sub_indexed_to(data_set, data, single_arg(decl, 0));
	}
	
	auto peek = stream->peek_token(1);
	bool found_sub_indexes = false;
	if(peek.type == Token_Type::quoted_string || peek.type == Token_Type::integer) {
		peek = stream->peek_token(2);
		if((char)peek.type == ':') {
			found_sub_indexes = true;
			if(!sub_indexed_to) {
				decl->source_loc.print_error_header();
				fatal_error("Got sub-indexes for an index set that is not sub-indexed.");
			}
			stream->expect_token('[');
			while(true) {
				auto token = stream->read_token();
				int indexed_under = sub_indexed_to->get_index(&token, 0); // NOTE: The super index set is itself not sub-indexed, hence the 0.
				stream->expect_token(':');
				parse_sub_indexes(&data->indexes[indexed_under], stream);
			
				peek = stream->peek_token();
				if((char)peek.type == ']') {
					stream->read_token();
					break;
				} else if (peek.type == Token_Type::eof) {
					peek.print_error_header();
					fatal_error("End of file before an index_set declaration was closed.");
				}
			}
		}
	}
	
	if (!found_sub_indexes && (data->sub_indexed_to >= 0)) {
		decl->source_loc.print_error_header();
		fatal_error("Missing sub-indexes for a sub-indexed index set.");
	}
	
	for(auto &idxs : data->indexes) {
		if(idxs.get_count() <= 0) {
			decl->source_loc.print_error_header();
			if(data->sub_indexed_to >= 0)
				fatal_error("Did not get an index set for all indexes of the parent index set.");
			else
				fatal_error("Empty index set.");
		}
	}
	
	if(!found_sub_indexes) {
		data->indexes.resize(1);
		parse_sub_indexes(&data->indexes[0], stream);
	}
}

void
get_indexes(Data_Set *data_set, std::vector<int> &index_sets, std::vector<Token> &index_names, std::vector<int> &indexes_out) {
	//TODO: Assertions on counts etc.
	indexes_out.resize(index_sets.size());
	for(int pos = 0; pos < index_names.size(); ++pos) {
		auto index_set = data_set->index_sets[index_sets[pos]];
		int parent_idx = 0;
		if(index_set->sub_indexed_to >= 0) {
			int parent_pos = -1;
			for(int par_pos = 0; par_pos < pos; ++par_pos) {
				if(index_sets[par_pos] == index_set->sub_indexed_to) {
					parent_pos = par_pos;
					break;
				}
			}
			if(parent_pos < 0) {
				index_names[pos].print_error_header();
				// TODO: Should include some names here.
				fatal_error("This index belongs to an index set that is sub-indexed to another index set, but this index does not appear after an index of the parent index set.");
			}
			parent_idx = indexes_out[parent_pos];
		}
		indexes_out[pos] = index_set->get_index(&index_names[pos], parent_idx);
	}
}


void
read_compartment_identifier(Data_Set *data_set, Token_Stream *stream, Compartment_Ref *read_to, Connection_Info *info) {
	Token token = stream->peek_token();
	stream->expect_identifier();
	
	if(token.string_value == "out") {
		read_to->id = -1;
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
	
	get_indexes(data_set, comp_data->index_sets, index_names, read_to->indexes);
}

void
read_connection_sequence(Data_Set *data_set, Compartment_Ref *first_in, Token_Stream *stream, Connection_Info *info) {
	
	std::pair<Compartment_Ref, Compartment_Ref> entry;
	if(!first_in)
		read_compartment_identifier(data_set, stream, &entry.first, info);
	else
		entry.first = *first_in;
	
	if(entry.first.id < 0) {
		info->source_loc.print_error_header();
		fatal_error("An 'out' can only be the target of an arrow, not the source.");
	}
	
	stream->expect_token(Token_Type::arr_r);
	read_compartment_identifier(data_set, stream, &entry.second, info);
	
	info->arrows.push_back(entry);
	{	// Make an edge index for the arrow if necessary.
		auto first_comp = info->components[entry.first.id];
		if(first_comp->edge_index_set >= 0) {
			auto edge_set = data_set->index_sets[first_comp->edge_index_set];
			int parent_idx = 0;
			if(!entry.first.indexes.empty())
				parent_idx = entry.first.indexes[0];
			edge_set->indexes[parent_idx].n_dim1++; // For now we just have numerical indexes.
		}
	}
	
	Token token = stream->peek_token();
	if(token.type == Token_Type::arr_r) {
		read_connection_sequence(data_set, &entry.second, stream, info);
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
	auto info = module->connections.create(name->string_value, name->source_loc);
	
	stream->expect_token('[');
	
	Token token = stream->peek_token();
	if((char)token.type == ']') {
		stream->read_token();
		return;
	}
	
	while(true) {
		auto token  = stream->peek_token();
		auto token2 = stream->peek_token(1);
		if(token.type != Token_Type::identifier || ((char)token2.type != ':' && (char)token2.type != '(')) break;
			
		Decl_AST *decl = parse_decl_header(stream);
		match_declaration(decl, {{Token_Type::quoted_string}});
		
		auto name = single_arg(decl, 0);
		auto data = info->components.create(name->string_value, name->source_loc);
		int comp_id = info->components.find_idx(name->string_value);
		data->decl_type = decl->type;
		data->handle = decl->handle_name.string_value;
		if(decl->handle_name.string_value.count)
			info->component_handle_to_id[decl->handle_name.string_value] = comp_id;
		std::vector<Token> idx_set_list;
		read_string_list(stream, idx_set_list);
		//int prev = -1;
		for(auto &name : idx_set_list) {
			int ref = data_set->index_sets.expect_exists_idx(&name, "index_set");
			data->index_sets.push_back(ref);
			int sub_indexed_to = data_set->index_sets[ref]->sub_indexed_to;
			if(sub_indexed_to >= 0 ) {
				bool found = false;
				for(int prev : data->index_sets) {
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
			//prev = ref;
		}
		auto next = stream->peek_token();
		if((char)next.type == '[') {
			std::vector<Token> edge_list;
			read_string_list(stream, edge_list);
			if(edge_list.size() > 1 || data->index_sets.size() > 1) {
				next.source_loc.print_error_header();
				fatal_error("A component can have at most one edge index set, and if it does have one it must have at most one main index set.");
			}
			auto edge_set_data = data_set->index_sets.create(edge_list[0].string_value, edge_list[0].source_loc);
			data->edge_index_set = data_set->index_sets.find_idx(edge_list[0].string_value);
			edge_set_data->is_edge_index_set = true;
			
			if(!idx_set_list.empty())
				make_sub_indexed_to(data_set, edge_set_data, &idx_set_list[0]);
			// For now at least: May allow naming edges at a later point.
			for(auto &idxs : edge_set_data->indexes)
				idxs.type = Sub_Indexing_Info::Type::numeric1;
		}
		delete decl;
	}
	
	token = stream->peek_token();
	if((char)token.type == ']') {
		stream->read_token();
		return;
	}
	if(token.type != Token_Type::identifier) {
		token.print_error_header();
		fatal_error("Expected a ] or the start of an component identifier.");
	}
	
	auto token2 = stream->peek_token(1);
	if((char)token2.type == '[') {
		info->type = Connection_Info::Type::graph;
		read_connection_sequence(data_set, nullptr, stream, info);
	} else if ((char)token2.type != ']') {
		token.print_error_header();
		fatal_error("Unrecognized connection data format.");
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
				std::vector<int> index_sets;
				std::vector<Token> index_names;
				
				while(true) {
					stream->read_token();
					auto index_set_idx = data_set->index_sets.expect_exists_idx(&token, "index_set");
					
					stream->expect_token(':');
					token = stream->read_token();
					index_sets.push_back(index_set_idx);
					index_names.push_back(token);
				
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
				std::vector<int> indexes_int;
				get_indexes(data_set, index_sets, index_names, indexes_int); 
				
				// TODO: Just maybe organize the data differently so that we don't need to do this zip.
				std::vector<std::pair<int, int>> indexes(index_sets.size());
				for(int lev = 0; lev < index_sets.size(); ++lev)
					indexes[lev] = std::pair<int, int>{index_sets[lev], indexes_int[lev]};
				
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
				fatal_error("Expected a parameter value of type ", name(get_value_type(decl->type)), ", or a ']'.");
			}
		}
		stream->expect_token(']'); //TODO: should give better error message here, along (expected n values for parameter, ...)
	}
	delete decl;
}

void
parse_par_group_decl(Data_Set *data_set, Module_Info *module, Token_Stream *stream, Decl_AST *decl) {
	match_declaration(decl, {{Token_Type::quoted_string}}, false, false);

	auto name = single_arg(decl, 0);
	auto group = module->par_groups.create(name->string_value, name->source_loc);
	
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
			}
		}
	}
	int expect_count = get_instance_count(data_set, group->index_sets, &decl->source_loc);
	
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
					int which = match_declaration(decl, {{Token_Type::quoted_string}, {}}, false, false);
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
					int which = match_declaration(decl, 
					{
						{Token_Type::quoted_string, Decl_Type::version},
						{Token_Type::quoted_string, Token_Type::integer, Token_Type::integer, Token_Type::integer}
					}, false, false);
				
					auto name = single_arg(decl, 0);
					auto module = modules.create(name->string_value, name->source_loc);
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
							fatal_error("Expected a } or a par_group declaration.");
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