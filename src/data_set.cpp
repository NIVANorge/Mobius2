
#include <algorithm>
#include <sstream>
#include <numeric>
#include <cstdarg>

#include "data_set.h"

void
read_series_data_from_csv(Data_Set *data_set, Series_Data *series_data, String_View file_name, String_View text_data);

void
read_series_data_from_spreadsheet(Data_Set *data_set, Series_Data *series, String_View file_name);

Registry_Base *
Data_Set::registry(Reg_Type reg_type) {
	switch(reg_type) {
		case Reg_Type::par_group :                return &par_groups;
		case Reg_Type::parameter :                return &parameters;
		case Reg_Type::series :                   return &series;
		case Reg_Type::component :                return &components;
		case Reg_Type::index_set :                return &index_sets;
		case Reg_Type::connection :               return &connections;
		case Reg_Type::quick_select :             return &quick_selects;
		case Reg_Type::position_map :             return &position_maps;
		case Reg_Type::module :                   return &modules;
		case Reg_Type::module_template :          return &modules; // NOTE: This is because module is associated to module_template in get_reg_type (for convenience in model_declaration)
	}
	
	fatal_error(Mobius_Error::internal, "Unhandled entity type ", name(reg_type), " in registry().");
	return nullptr;
}

Decl_Scope *
Data_Set::get_scope(Entity_Id id) {
	if(!is_valid(id))
		return &top_scope;
	else if(id.reg_type == Reg_Type::module)
		return &modules[id]->scope;
	else if(id.reg_type == Reg_Type::par_group)
		return &par_groups[id]->scope;
	else if(id.reg_type == Reg_Type::connection)
		return &connections[id]->scope;
	fatal_error(Mobius_Error::internal, "Tried to look up the scope belonging to an id that is not a module, par_group or connection.");
	return nullptr;
}

void
Module_Data::process_declaration(Catalog *catalog) {
	
	match_declaration(decl, {{Token_Type::quoted_string, Decl_Type::version}}, false, -1);
	
	set_serial_name(catalog, this);
	
	auto version_decl = decl->args[1]->decl;
	match_declaration(version_decl, {{Token_Type::integer, Token_Type::integer, Token_Type::integer}}, false);
	version.major        = single_arg(version_decl, 0)->val_int;
	version.minor        = single_arg(version_decl, 1)->val_int;
	version.revision     = single_arg(version_decl, 2)->val_int;
	
	auto parent_scope = catalog->get_scope(scope_id);
	scope.parent_id = id;
	scope.import(*parent_scope);
	
	auto body = static_cast<Decl_Body_AST *>(decl->body);
	
	const std::set<Decl_Type> allowed_decls = {
		Decl_Type::par_group,
		Decl_Type::connection,
	};
	
	for(auto child : body->child_decls)
		catalog->register_decls_recursive(&scope, child, allowed_decls);
	
	for(auto id : scope.all_ids)
		catalog->find_entity(id)->process_declaration(catalog);
}

void
Par_Group_Data::process_declaration(Catalog *catalog) {
	
	if(this->decl_type == Decl_Type::par_group) {
		match_declaration(decl,
		{
			{Token_Type::quoted_string},
			{Token_Type::quoted_string, {Decl_Type::index_set, true}},
		}, false, -1);
	} else { // option_group
		match_declaration(decl, {{Token_Type::quoted_string}}, false, -1);
	}
	
	set_serial_name(catalog, this);
	auto data_set = static_cast<Data_Set *>(catalog);
	auto parent_scope = catalog->get_scope(scope_id);
	scope.parent_id = id;
	scope.import(*parent_scope); // Needed since some parameter decls may want access to index_set identifiers.
	
	for(int argidx = 1; argidx < decl->args.size(); ++argidx) {
		auto id = parent_scope->resolve_argument(Reg_Type::index_set, decl->args[argidx]);
		index_sets.push_back(id);
	}
	
	if(!index_sets.empty())
		data_set->index_data.check_valid_distribution(index_sets, source_loc);
	
	auto body = static_cast<Decl_Body_AST *>(decl->body);
	
	std::set<Decl_Type> allowed_decls;
	if(this->decl_type == Decl_Type::par_group) {
		allowed_decls = {
			Decl_Type::par_real,
			Decl_Type::par_int,
			Decl_Type::par_bool,
			Decl_Type::par_enum,
			Decl_Type::par_datetime
		};
	} else { // option_group.
		allowed_decls = {
			Decl_Type::par_bool,
			Decl_Type::par_enum,
		};
	}
	
	for(auto child : body->child_decls)
		catalog->register_decls_recursive(&scope, child, allowed_decls);
	
	for(auto id : scope.all_ids)
		catalog->find_entity(id)->process_declaration(catalog);
}


void
parse_parameter_map_recursive(Data_Set *data_set, Data_Map_AST *map, std::vector<Entity_Id> &index_sets, std::vector<Token> &index_tokens, std::vector<Parmap_Entry> &push_to, int level) {
	
	for(auto &entry : map->entries) {
		index_tokens[level] = entry.key;
		if(level == (int)index_sets.size() - 1) {
			Indexes indexes;
			data_set->index_data.find_indexes(index_sets, index_tokens, indexes);
			if(entry.data) {
				entry.data->source_loc.print_error_header();
				fatal_error("Expected a single value.");
			}
			if(!is_numeric(entry.single_value.type)) {
				entry.single_value.print_error_header();
				fatal_error("Expected a numeric value.");
			}
			Parmap_Entry entry2;
			entry2.indexes = indexes;
			entry2.value = entry.single_value.double_value();
			if(is_numeric(entry.key.type))
				entry2.pos = entry.key.double_value();
			else
				entry2.pos = (double)indexes.indexes.back().index;
			push_to.push_back(entry2);
		} else {
			if(!entry.data || entry.data->data_type != Data_Type::map) {
				entry.key.print_error_header();
				fatal_error("Expected a nested map over the next index set.");
			}
			parse_parameter_map_recursive(data_set, static_cast<Data_Map_AST *>(entry.data), index_sets, index_tokens, push_to, level + 1);
		}
	}
	
}

void
Parameter_Data::unpack_parameter_map(Data_Set *data_set) {
	//, std::vector<Entity_Id> &index_sets, std::vector<Parmap_Entry> &data, std::vector<Parameter_Value> &values) {
	
	if(decl_type != Decl_Type::par_real)
		fatal_error(Mobius_Error::internal, "Somehow tried to unpack map data for a non-double parameter.");
	
	values.clear();
	
	auto group = data_set->par_groups[this->scope_id];
	
	std::vector<Entity_Id> index_sets_upper = group->index_sets;
	Entity_Id interp_set = index_sets_upper.back();
	index_sets_upper.pop_back(); // The last index set is the one we interpolate over, and so is handled differently.
	
	data_set->index_data.for_each(index_sets_upper, [this, data_set, interp_set](Indexes &indexes) {
		
		struct
		Sorted_Entry {
			Index_T index_pos;
			double pos;
			double val;
		};
		
		// This is a bit inefficient, but will probably not matter since these typically should have few entries.
		std::vector<Sorted_Entry> inner;

		for(auto &entry : parmap_data) {

			if(!entry.indexes.lookup_ordered || !indexes.lookup_ordered)
				fatal_error(Mobius_Error::internal, "unpack_parameter_map: Implementation dependent on these indexes being lookup-ordered.");
			
			//log_print(entry.indexes.indexes.size(), " ", entry.indexes.indexes[0].index, " ", entry.value, "\n");
			
			bool match = true;
			for(int idx = 0; idx < indexes.indexes.size(); ++idx) {
				if(indexes.indexes[idx] != entry.indexes.indexes[idx])
					match = false;
			}
			if(match)
				inner.push_back({entry.indexes.indexes.back(), entry.pos, entry.value});
			
		}
		
		std::sort(inner.begin(), inner.end(), [](const auto &a, const auto &b) {
			return a.index_pos < b.index_pos;
		});
		
		int count = data_set->index_data.get_index_count(indexes, interp_set).index;
		
		std::vector<double> unpacked_values(count);
		
		// Fill constant before and after
		for(int idx = 0; idx < inner[0].index_pos.index; ++idx)
			unpacked_values[idx] = inner[0].val;
		
		for(int idx = inner.back().index_pos.index; idx < count; ++idx)
			unpacked_values[idx] = inner.back().val;
		
		// Assume linear interpolation for now. We could make more options
		// Linear interpolate inside
		for(int at = 0; at < (int)inner.size()-1; ++at) {
			
			auto &first = inner[at];
			auto &last  = inner[at+1];
			
			for(Index_T index = first.index_pos; index <= last.index_pos; ++index) {
				
				// We want the position of the beginning of the interval rather than the end.
				Index_T index2 = index;
				index2.index--;
				double pos = 0.0;
				if(index2.index >= 0)
					pos = data_set->index_data.get_position(index2);
				if(pos < first.pos || pos > last.pos) continue;
				
				double tt = (pos - first.pos) / (last.pos - first.pos);
				
				unpacked_values[index.index] = (1.0 - tt)*first.val + tt*last.val;
			}
		}
		
		for(double valr : unpacked_values) {
			Parameter_Value val;
			val.val_real = valr;
			values.push_back(val);
		}
	});
}


void
Parameter_Data::process_declaration(Catalog *catalog) {

	match_data_declaration(decl, {{Token_Type::quoted_string}}, true, 0, true, 1);
	
	set_serial_name(catalog, this);
	
	auto data_set = static_cast<Data_Set *>(catalog);
	auto par_group = data_set->par_groups[scope_id];
	// This is a bit hacky. We want to look in the top scope since that is where the index sets are visible.
	auto scope = catalog->get_scope(par_group->scope_id);
	
	for(auto note : decl->notes) {
		auto str = note->decl.string_value;
		if(str == "from_position") {
			match_declaration_base(note, {{Decl_Type::index_set}}, 0);
			from_pos = scope->resolve_argument(Reg_Type::index_set, note->args[0]);
		} else {
			note->decl.print_error_header();
			fatal_error("Unrecognized note type '", str, "' for a parameter.");
		}
	}
	
	if(is_valid(from_pos)) {
		if(decl->data) {
			decl->source_loc.print_error_header();
			fatal_error("A parameter with 'from_position' should not have a data block.");
		}
		if(decl_type != Decl_Type::par_real) {
			decl->source_loc.print_error_header();
			fatal_error("A parameter with 'from_position' must be 'par_real'.");
		}
		
		bool found = false;
		int idx_pos;
		for(idx_pos = 0; idx_pos < par_group->index_sets.size(); ++idx_pos) {
			if(par_group->index_sets[idx_pos] == from_pos) {
				found = true;
				break;
			}
		}
		if(!found) {
			decl->source_loc.print_error_header();
			fatal_error("The parameter does not index over the index set that the 'from_position' was declared with.");
		}
		
		// NOTE: Assuming delta position for now. Could also allow absolute position if desired later.
		double prev_pos = 0.0;
		data_set->index_data.for_each(par_group->index_sets, [this, data_set, idx_pos, &prev_pos](Indexes &indexes) {
			Parameter_Value val;
			auto index = indexes.indexes[idx_pos];
			// Ouch, this is a bit hacky.
			if(index.index == 0)
				prev_pos = 0.0;
			double cur_pos = data_set->index_data.get_position(index);
			val.val_real = (cur_pos - prev_pos);
			values.push_back(val);
			prev_pos = cur_pos;
		});
		
	
	} else if(decl->data->data_type == Data_Type::list) {
		
		auto &data = static_cast<Data_List_AST *>(decl->data)->list;
		
		if(decl_type == Decl_Type::par_enum) {
			values_enum.reserve(data.size());
			for(auto &token : data) {
				if(token.type != Token_Type::identifier) {
					token.print_error_header();
					fatal_error("Expected an identifier.");
				}
				values_enum.push_back(token.string_value);
			}
		} else if(decl_type == Decl_Type::par_datetime) {
			// Hmm, this is a bit clumsy now.. Only placing it seems to be an issue though.
			for(int idx = 0; idx < data.size(); ++idx) {
				Parameter_Value val;
				auto &date_token = data[idx];
				if(date_token.type != Token_Type::date) {
					date_token.print_error_header();
					fatal_error("Expected a date value.");
				}
				val.val_datetime = date_token.val_date;
				if(idx+1 < data.size() && data[idx+1].type == Token_Type::time) {
					val.val_datetime += data[idx+1].val_date;
					++idx;
				}
				values.push_back(val);
			}
		} else {
			values.reserve(data.size());
			Parameter_Value val;
			for(auto &token : data) {
				if(decl_type == Decl_Type::par_real && is_numeric(token.type))
					val.val_real    = token.double_value();
				else if(decl_type == Decl_Type::par_int && token.type == Token_Type::integer)
					val.val_integer = token.val_int;
				else if(decl_type == Decl_Type::par_bool && token.type == Token_Type::boolean)
					val.val_boolean = token.val_bool;
				else {
					token.print_error_header();
					fatal_error("Expected a parameter value of type ", ::name(get_value_type(decl_type)), ".");
				}
				values.push_back(val);
			}
		}
	} else if (decl->data->data_type == Data_Type::map) {
		
		if(decl_type != Decl_Type::par_real) {
			decl->source_loc.print_error_header();
			fatal_error("Only 'par_real' can have a data block on map form.");
		}
		
		is_on_map_form = true;
		
		if(par_group->index_sets.empty()) {
			decl->source_loc.print_error_header();
			fatal_error("For parameter data to be on map form, the par_group must index over at least one index set.");
		}
		
		auto map = static_cast<Data_Map_AST *>(decl->data);
		std::vector<Token> index_tokens(par_group->index_sets.size());
	
		
		parse_parameter_map_recursive(data_set, map, par_group->index_sets, index_tokens, parmap_data, 0);
		
		//log_print("Data size here is : ", map_data.size(), "\n");
		
		unpack_parameter_map(data_set);
		
	} else
		fatal_error(Mobius_Error::internal, "Invalid data format for parameter.");
	
	has_been_processed = true;
}

int
Parameter_Data::get_count() {
	if(decl_type == Decl_Type::par_enum) return values_enum.size();
	else                                 return values.size();
}

void
Series_Data::process_declaration(Catalog *catalog) {
	
	match_data_declaration(decl, {{Token_Type::quoted_string}}, false, 0, false, 0);
					
	String_View other_file_name = single_arg(decl, 0)->string_value;
	
	auto data_set = static_cast<Data_Set *>(catalog);
	
	if(data_set->file_handler.is_loaded(other_file_name, data_set->path)) {
		single_arg(decl, 0)->print_error_header();
		fatal_error("The file ", other_file_name, " has already been loaded.");
	}
	
	bool success;
	String_View extension = get_extension(other_file_name, &success);
	
	if(success && (extension == ".xlsx" || extension == ".xls")) {

		String_View path = make_path_relative_to(other_file_name, data_set->path);
		this->file_name = std::string(other_file_name); // This is the path that is saved if the data_set is saved, it must be the same as what is loaded.
		read_series_data_from_spreadsheet(data_set, this, path);
		
	} else {
		String_View text_data = data_set->file_handler.load_file(other_file_name, single_arg(decl, 0)->source_loc, data_set->path);
		read_series_data_from_csv(data_set, this, other_file_name, text_data);
	}
	
	has_been_processed = true;
}

void
process_index_data(Data_Set *data_set, Decl_Scope *scope, Entity_Id index_set_id) {
	
	auto index_set = data_set->index_sets[index_set_id];
	auto decl = index_set->decl;
	
	match_data_declaration(decl, {{Token_Type::quoted_string}}, true, 0, true, 1);
	
	index_set->process_main_decl(data_set);
	
	if(!index_set->union_of.empty()) {
		if(decl->data) {
			decl->source_loc.print_error_header();
			fatal_error("A union index set should not be provided with index data.");
		}
		data_set->index_data.initialize_union(index_set_id, decl->source_loc);
		index_set->has_been_processed = true;
		return;
	}
	
	if(!decl->data) {
		if(!is_valid(index_set->sub_indexed_to)) {
			index_set->source_loc.print_error_header();
			//TODO: Better message?
			//TODO: Maybe not impose this restriction here, and instead do it in Model_Application (if at all).
			fatal_error("Only union index sets or sub-indexed index sets can be declared without data.");
		}
		index_set->has_been_processed = true;
		return;
	}
	
	if(is_valid(index_set->sub_indexed_to)) {
		
		if(decl->data->data_type != Data_Type::map) {
			decl->data->source_loc.print_error_header();
			fatal_error("Expected a map mapping parent indexes to index lists sice this index set is sub-indexed.");
		}
		
		auto data = static_cast<Data_Map_AST *>(decl->data);
		
		for(auto &entry : data->entries) {
			auto parent_idx = data_set->index_data.find_index(index_set->sub_indexed_to, &entry.key);
			
			if(!entry.data || entry.data->data_type != Data_Type::list) {
				entry.data->source_loc.print_error_header();
				fatal_error("Expected a simple list of indexes or an integer size.");
			}
			auto list = static_cast<Data_List_AST *>(entry.data);
			
			if(!list->list.empty())
				data_set->index_data.set_indexes(index_set_id, list->list, parent_idx);
		}
		
	} else {
		
		//log_print("Data type is ", (int)decl->data->data_type, "\n");
		
		if(decl->data->data_type != Data_Type::list) {
			decl->data->source_loc.print_error_header();
			fatal_error("Expected a simple list of indexes or an integer size for this index set since it is not sub-indexed.");
		}
		auto data = static_cast<Data_List_AST *>(decl->data);
		
		if(!data->list.empty())
			data_set->index_data.set_indexes(index_set_id, data->list);
	}
	
	if(!data_set->index_data.are_all_indexes_set(index_set_id)) {
		// TODO: Maybe we should allow empty index sets later.
		//    Or at least maybe just do this check in Model_Application.
		decl->source_loc.print_error_header();
		fatal_error("The index set \"", index_set->name, "\" was not fully initialized.");
	}
	
	index_set->has_been_processed = true;
}

void
process_time_step_decl(Data_Set *data_set, Decl_AST *decl) {
	
	if(data_set->time_step_was_provided) {
		decl->source_loc.print_error_header();
		fatal_error("Duplicate declaration of 'time_step'.");
	}
	match_declaration(decl, {{Decl_Type::unit}}, false);
	data_set->time_step_unit.set_data(decl->args[0]->decl);
	data_set->unit_source_loc = decl->source_loc;
	data_set->time_step_was_provided = true;
}

void
process_series_interval_decl(Data_Set *data_set, Decl_AST *decl) {
	if(data_set->series_interval_was_provided) {
		decl->source_loc.print_error_header();
		fatal_error("Duplicate declaration of 'series_interval'.");
	}
	match_declaration(decl, {{Token_Type::date, Token_Type::date}}, false);
	data_set->series_begin = single_arg(decl, 0)->val_date;
	data_set->series_end   = single_arg(decl, 1)->val_date;
	data_set->series_interval_was_provided = true;
	
	if(data_set->series_end < data_set->series_begin) {
		single_arg(decl, 0)->print_error_header();
		fatal_error("The start date can't be later than the end date for the 'series_interval'.");
	}
}

void
Component_Data::process_declaration(Catalog *catalog) {

	match_declaration(decl,
	{
		{Token_Type::quoted_string},
		{Token_Type::quoted_string, {Decl_Type::index_set, true}},
	}, true);
	
	set_serial_name(catalog, this);
	auto data_set = static_cast<Data_Set *>(catalog);
	auto scope = catalog->get_scope(scope_id);
	
	for(int argidx = 1; argidx < decl->args.size(); ++argidx) {
		auto id = scope->resolve_argument(Reg_Type::index_set, decl->args[argidx]);
		index_sets.push_back(id);
	}
	
	if(!index_sets.empty())
		data_set->index_data.check_valid_distribution(index_sets, decl->args[0]->source_loc());
}

void
maybe_make_edge_index(Data_Set *data_set, Connection_Data *connection, std::pair<Compartment_Ref, Compartment_Ref> &arrow) {
	auto first_comp = data_set->components[arrow.first.id];

	if(!is_valid(connection->edge_index_set) || !first_comp->can_have_edge_index_set) return;
		
	Index_T parent_idx = Index_T::no_index();
	auto parent_set = data_set->index_sets[connection->edge_index_set]->sub_indexed_to;
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
			index_name = data_set->components[arrow.second.id]->name;
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
	// TODO: Should provide a better source_loc...
	data_set->index_data.add_edge_index(connection->edge_index_set, index_name, connection->source_loc, parent_idx);
}

void
Connection_Data::process_declaration(Catalog *catalog) {
	
	match_declaration(decl, {{Token_Type::quoted_string}}, false, -1);
	
	set_serial_name(catalog, this);
	
	scope.parent_id = id; // This is the scope inside the connection declaration.
	auto parent_scope = catalog->get_scope(scope_id); // This is the outer scope where the connection is declared.
	scope.import(*parent_scope); // To make e.g. index sets visible to component declarations.
	
	
	auto body = static_cast<Decl_Body_AST *>(decl->body);
	
	const std::set<Decl_Type> allowed_decls = { Decl_Type::compartment };//, Decl_Type::quantity };
	
	Decl_AST *graph_decl = nullptr;
	for(Decl_AST *child : body->child_decls) {
		if(child->type == Decl_Type::directed_graph) {
			// Don't make a registration for the directed_graph itself.
			for(auto arg : child->args) {
				if(arg->decl) catalog->register_decls_recursive(&scope, arg->decl, allowed_decls);
			}
			if(graph_decl) {
				graph_decl->source_loc.print_error_header();
				fatal_error("Multiple data declarations for connection.");
			}
			graph_decl = child;
			type = Type::directed_graph; // This is the only type we have data for for now.
		} else
			catalog->register_decls_recursive(&scope, child, allowed_decls);
		
	}
	
	for(auto id : scope.all_ids)
		catalog->find_entity(id)->process_declaration(catalog);
	
	if(!graph_decl) {
		has_been_processed = true;
		return;
	}
	
	auto data_set = static_cast<Data_Set *>(catalog);
	
	// Process graph data.
	
	int which = match_data_declaration(graph_decl, {{}, {Decl_Type::index_set}});
	
	if(which == 1) {
		edge_index_set = scope.resolve_argument(Reg_Type::index_set, graph_decl->args[0]);
		auto edge_set = catalog->index_sets[edge_index_set];
		
		if(is_valid(edge_set->is_edge_of_connection)) {
			graph_decl->args[0]->source_loc().print_error_header();
			fatal_error("This index set is already the edge of another connection.");
		}
		edge_set->is_edge_of_connection = id;
		
		if(!edge_set->union_of.empty()) {
			edge_set->source_loc.print_error_header();
			fatal_error("An edge index set can not be a union.");
		}
		
		if(data_set->index_data.are_all_indexes_set(edge_index_set)) {
			edge_set->source_loc.print_error_header();
			fatal_error("Edge index sets should not receive index data explicitly.");
		}
		
		data_set->index_data.initialize_edge_index_set(edge_index_set, edge_set->source_loc);
				
		for(auto component_id : scope.by_type(Reg_Type::component)) {
			auto component = data_set->components[component_id];
			if(component->index_sets.size() == 1) {
				if(data_set->index_data.can_be_sub_indexed_to(component->index_sets[0], edge_index_set))
					component->can_have_edge_index_set = true;
			} else if(component->index_sets.empty())
				component->can_have_edge_index_set = true;   // TODO: Hmm, wouldn't this only be the case if all the components were not indexed?
		}
	}
	
	auto graph_data = static_cast<Directed_Graph_AST *>(graph_decl->data);
	
	auto process_node = [](Data_Set *data_set, Decl_Scope &scope, Directed_Graph_AST::Node &node, Compartment_Ref &ref) {
		if(node.identifier.string_value == "out") {
			ref.id = invalid_entity_id;
			if(!node.indexes.empty()) {
				node.identifier.print_error_header();
				fatal_error("Didn't expect indexes for 'out'.");
			}
		} else {
			ref.id = scope.expect(Reg_Type::component, &node.identifier);
			auto component = data_set->components[ref.id];
			data_set->index_data.find_indexes(component->index_sets, node.indexes, ref.indexes);
		}
	};
	
	for(auto &arrow : graph_data->arrows) {
		auto &node1 = graph_data->nodes[arrow.first];
		auto &node2 = graph_data->nodes[arrow.second];
		// Hmm, we are actually double-storing node data here. Could store it the same way as in the AST instead. Could even de-duplicate at this stage, but maybe not necessary.
		arrows.emplace_back();
		auto &arr = arrows.back();
		process_node(data_set, scope, node1, arr.first);
		process_node(data_set, scope, node2, arr.second);
		// If necessary, create an edge index for this arrow in the edge index set of the graph.
		maybe_make_edge_index(data_set, this, arr);
	}
	
	has_been_processed = true;
}

void
Quick_Select_Data::process_declaration(Catalog *catalog) {
	
	has_been_processed = true;
	
	match_data_declaration(decl, {{Token_Type::quoted_string}});
	
	set_serial_name(catalog, this);
	
	if(decl->data->data_type != Data_Type::map) {
		decl->data->source_loc.print_error_header();
		fatal_error("Expected data in a map format.");
	}
	
	auto map_data = static_cast<Data_Map_AST *>(decl->data);
	
	for(auto &entry : map_data->entries) {
		Quick_Select select;
		if(entry.key.type != Token_Type::quoted_string) {
			entry.key.print_error_header();
			fatal_error("Expected a quoted string name of the entry.");
		}
		select.name = entry.key.string_value;
		if(!entry.data || entry.data->data_type != Data_Type::list) {
			entry.data->source_loc.print_error_header();
			fatal_error("Expected a list of series names.");
		}
		auto list_data = static_cast<Data_List_AST *>(entry.data);
		for(auto &item : list_data->list) {
			if(item.type != Token_Type::quoted_string) {
				item.print_error_header();
				fatal_error("Expected a quoted string name of a series.");
			}
			select.series_names.push_back(item.string_value);
		}
		selects.emplace_back(std::move(select));
	}
}


void
Position_Map_Data::process_declaration(Catalog *catalog) {
	
	match_data_declaration(decl, {{Decl_Type::index_set}});
	
	if(decl->data->data_type != Data_Type::map) {
		decl->data->source_loc.print_error_header();
		fatal_error("Expected data in a map format.");
	}
	
	auto data_set = static_cast<Data_Set *>(catalog);
	auto scope = catalog->get_scope(scope_id);
	index_set_id = scope->resolve_argument(Reg_Type::index_set, decl->args[0]);
	
	if(!data_set->index_data.are_all_indexes_set(index_set_id)) {
		decl->source_loc.print_error_header();
		fatal_error("Can not provide a position_map for an index set that has not received dimensions.");
	}
	if(data_set->index_data.get_index_type(index_set_id) != Index_Record::Type::numeric1) {
		decl->source_loc.print_error_header();
		fatal_error("Can not provide a position_map for an index set that is not numeric.");
	}
	
	auto map_data = static_cast<Data_Map_AST *>(decl->data);
	
	for(auto &entry : map_data->entries) {
		if(!is_numeric(entry.key.type)) {
			entry.key.print_error_header();
			fatal_error("Expected an numeric position value as the key.");
		}
		if(entry.data || !is_numeric(entry.single_value.type)) {
			map_data->source_loc.print_error_header();
			fatal_error("All value entries in the map must be numeric.");
		}
		double pos = entry.key.double_value();
		double value = entry.single_value.double_value();
		if(value < 0.0 || pos < 0.0) {
			entry.key.print_error_header();
			fatal_error("All position map values must be positive numbers.");
		}
		pos_vals_raw.push_back(pos);
		width_vals_raw.push_back(value);
	}
	
	if(pos_vals_raw.empty()) {
		map_data->source_loc.print_error_header();
		fatal_error("The position_map data can't be empty.");
	}
	
	// Widths is default for now. Make option for linear_interpolate later instead.
	/*
	for(auto note : decl->notes) {
		auto str = note->decl.string_value;
		if(str == "widths")
			raw_is_widths = true;
		else if(str == "linear_interpolate")
			linear_interp = true;
		else {
			note->decl.print_error_header();
			fatal_error("Unexpected note '", str, "' for position_map declaration.");
		}
	}
	*/
	
	// TODO: This should be factored out, for reuse with parameter values.
	
	std::vector<int> order(pos_vals_raw.size());
	std::iota(order.begin(), order.end(), 0);
	std::sort(order.begin(), order.end(), [this](int row_a, int row_b) -> bool { return pos_vals_raw[row_a] < pos_vals_raw[row_b]; });
	
	// Reinterpret the index set size as a max "depth".
	double max_width = (double)data_set->index_data.get_max_count(index_set_id).index;
	
	std::vector<double> expanded_ys;
	
	double at_pos = 0.0;
	
	for(int idxidx = 0; idxidx < order.size(); ++idxidx) {
		int idx = order[idxidx];
		
		double width = width_vals_raw[idx];
		at_pos += width;
		expanded_ys.push_back(at_pos);
		
		bool at_end = true;
		double pos_next = max_width;
		if(idxidx < (int)order.size()-1) {
			int idxp1 = order[idxidx+1];
			pos_next = pos_vals_raw[idxp1];
			at_end = false;
		}
		
		while(at_pos < pos_next) {
			at_pos += width; // TODO: Implement linear interp option later.
			if(at_end) at_pos = std::min(at_pos, pos_next); // TODO: Do we always do this, or only towards the end like now?
			expanded_ys.push_back(at_pos);
		}
	}
	
	data_set->index_data.set_position_map(index_set_id, expanded_ys, decl->source_loc);
	
	has_been_processed = true;
}


void
Data_Set::read_from_file(String_View file_name) {
	
	if(path != "")
		fatal_error(Mobius_Error::api_usage, "Tried make a data set read from a file ", file_name, ", but it already contains data from the file ", path, ".");
	
	path = std::string(file_name);
	
	auto decl = read_catalog_ast_from_file(Decl_Type::data_set, &file_handler, file_name);
	match_declaration(decl, {{}}, false, -1);
	
	auto body = static_cast<Decl_Body_AST *>(decl->body);
	if(body->doc_string.string_value.count)
		doc_string = body->doc_string.string_value;
	
	const std::set<Decl_Type> allowed_data_decls = {
		Decl_Type::index_set,
		Decl_Type::connection,
		Decl_Type::module,
		Decl_Type::preamble,
		Decl_Type::par_group,
		Decl_Type::option_group,
		Decl_Type::series,
		Decl_Type::series_interval,
		Decl_Type::time_step,
		Decl_Type::quick_select,
		Decl_Type::position_map
	};
	
	auto scope = &top_scope;
	
	// TODO: This could be problematic since it would also register version() declarations.
	//    Do we skip those here or handle it in another way.
	for(Decl_AST *child : body->child_decls) {
		if(child->type == Decl_Type::time_step)
			process_time_step_decl(this, child);
		else if(child->type == Decl_Type::series_interval)
			process_series_interval_decl(this, child);
		else if(child->type == Decl_Type::module || child->type == Decl_Type::preamble)
			register_single_decl(scope, child, allowed_data_decls); // Just so that it doesn't try to process the 'version' argument separately
		else
			register_decls_recursive(scope, child, allowed_data_decls);
	}
	
	// Almost everything depends on the index sets being correctly set up before the data is processed.
	for(auto id : scope->by_type(Reg_Type::index_set))
		process_index_data(this, scope, id); // Can't call process_declaration on index_sets since we can't reuse the same function as in the model.
	
	for(auto id : scope->by_type(Reg_Type::position_map))
		find_entity(id)->process_declaration(this);
	
	// Also have to process connections in order to set indexes for edge index sets before anything else is done.
	for(auto id : scope->by_type(Reg_Type::connection))
		find_entity(id)->process_declaration(this);
	
	for(auto id : scope->all_ids) {
		auto entity = find_entity(id);
		if(entity->has_been_processed) continue;
		entity->process_declaration(this);
	}
	
	file_handler.unload_all();
	delete main_decl;
	
	// TODO: Post-processing.
		// Check expected value count for parameters. (Although don't we also do that in the model_application?)
		// I guess it could be good to do it here anyway since we want to be able to have internal consistency for resizing etc.
}

void
Data_Set::generate_index_data(const std::string &name, const std::string &identifier, const std::string &sub_indexed_to, const std::vector<std::string> &union_of) {
	
	// TODO: If the identifier was not declared in the model (so is "") or there is a conflicting identifier in the data set, this will lead to a problem.
	
	// NOTE: We don't check for correctness of sub-indexing vs. union here since we assume this was done by the Model_Application.
	auto id = index_sets.create_internal(&top_scope, identifier, name, Decl_Type::index_set);

	auto set = index_sets[id];
	
	auto sub_to = invalid_entity_id;
	if(!sub_indexed_to.empty()) {
		sub_to = deserialize(sub_indexed_to, Reg_Type::index_set);
		if(!is_valid(sub_to))
			fatal_error(Mobius_Error::internal, "Did not find the parent index set ", sub_indexed_to, " in generate_index_data.");
		set->sub_indexed_to = sub_to;
	}
	
	Source_Location loc = {};
		
	if(!union_of.empty()) {
		for(auto ui : union_of) {
			auto ui_id = deserialize(ui, Reg_Type::index_set);
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
		Index_T parent_idx = Index_T { sub_to, (s16)par_idx };
		index_data.set_indexes(id, { count }, parent_idx);
	}
}

void
read_series_data_from_csv(Data_Set *data_set, Series_Data *series_data, String_View file_name, String_View text_data) {
	
	Token_Stream stream(file_name, text_data);
	stream.allow_date_time_tokens = true;
	
	series_data->series.push_back({});
	Series_Set &data = series_data->series.back();
	series_data->file_name = std::string(file_name);
	
	data.has_date_vector = true;
	Token token = stream.peek_token();
	
	if(token.type == Token_Type::date) {
		// If there is a date as the first token, that gives the start date, and there is no separate date for each row.
		data.start_date = stream.expect_datetime();
		data.has_date_vector = false;
	} else if(token.type != Token_Type::quoted_string) {
		token.print_error_header();
		fatal_error("Expected either a start date or the name of an input series.");
	}
	
	while(true) {
		Series_Header header;
		header.source_loc = token.source_loc;
		header.name = std::string(stream.expect_quoted_string());
		while(true) {
			token = stream.peek_token();
			if((char)token.type != '[')
				break;
			stream.read_token();
			token = stream.peek_token();
			if(token.type == Token_Type::quoted_string) {
				std::vector<Entity_Id> index_sets;
				std::vector<Token>   index_names;
				
				while(true) {
					stream.read_token();
					auto index_set_id = data_set->deserialize(token.string_value, Reg_Type::index_set);
					
					stream.expect_token(':');
					auto next = stream.read_token();
					index_sets.push_back(index_set_id);
					index_names.push_back(next);
				
					next = stream.peek_token();
					if((char)next.type == ']') {
						stream.read_token();
						break;
					}
					if(next.type != Token_Type::quoted_string) {
						next.print_error_header();
						fatal_error("Expected a ] or a new index set name.");
					}
				}
				
				data_set->index_data.check_valid_distribution(index_sets, token.source_loc);
				
				Indexes indexes;
				data_set->index_data.find_indexes(index_sets, index_names, indexes);
				
				header.indexes.push_back(std::move(indexes));
			} else if(token.type == Token_Type::identifier || (char)token.type == '[') {
				while(true) {
					if((char)token.type == '[') {
						auto unit_decl = parse_decl_header(&stream);
						header.unit.set_data(unit_decl);
						delete unit_decl;
					} else {
						bool success = set_flag(&header.flags, token.string_value);
						if(!success) {
							token.print_error_header();
							fatal_error("Unrecognized input flag \"", token.string_value, "\".");
						}
					}
					token = stream.read_token();
					if((char)token.type == ']')
						break;
					else if(token.type != Token_Type::identifier && (char)token.type != '[') {
						token.print_error_header();
						fatal_error("Expected a ], another flag identifier or a unit declaration.");
					}
				}
				
			} else {
				token.print_error_header();
				fatal_error("Expected the name of an index set or a flag");
			}
		}
		data.header_data.push_back(std::move(header));
		
		Token token = stream.peek_token();
		if(token.type != Token_Type::quoted_string)
			break;
	}
	
	if(data.header_data.empty())
		fatal_error(Mobius_Error::internal, "Empty input data header not properly detected.");
	
	int rowlen = data.header_data.size();
	
	data.raw_values.resize(rowlen);
	for(auto &vec : data.raw_values)
		vec.reserve(1024);
	
	if(data.has_date_vector) {
		Date_Time start_date;
		start_date.seconds_since_epoch = std::numeric_limits<s64>::max();
		Date_Time end_date;
		end_date.seconds_since_epoch = std::numeric_limits<s64>::min();
		
		while(true) {
			Date_Time date = stream.expect_datetime();
			if(date < start_date) start_date = date;
			if(date > end_date)   end_date   = date;
			data.dates.push_back(date);
			for(int col = 0; col < rowlen; ++col) {
				double val = stream.expect_real();
				data.raw_values[col].push_back(val);
			}
			Token token = stream.peek_token();
			if(token.type != Token_Type::date)
				break;
		}
		data.start_date = start_date;
		data.end_date = end_date;
		
	} else {
		data.time_steps = 0;
		
		while(true) {
			for(int col = 0; col < rowlen; ++col) {
				double val = stream.expect_real();
				data.raw_values[col].push_back(val);
			}
			++data.time_steps;
			Token token = stream.peek_token();
			if(!is_numeric(token.type))
				break;
		}
	}
}

void
Data_Set::get_model_options(Model_Options &options) {
	
	auto iter = top_scope.by_type(Reg_Type::par_group);
	for(auto group_id : iter) {
		auto group = par_groups[group_id];
		if(group->decl_type != Decl_Type::option_group) continue;
		
		auto iter2 = group->scope.by_type(Reg_Type::parameter);
		for(auto par_id : iter2) {
			auto par = parameters[par_id];
			
			std::string par_val;
			if(par->decl_type == Decl_Type::par_bool) {
				if(par->values.size() != 1)
					fatal_error(Mobius_Error::internal, "Expected exactly one value for an option_group parameter.");
				auto val = par->values[0];
				par_val = val.val_boolean ? "true" : "false";
			} else if(par->decl_type == Decl_Type::par_enum) {
				if(par->values_enum.size() != 1)
					fatal_error(Mobius_Error::internal, "Expected exactly one value for an option_group parameter.");
				par_val = par->values_enum[0];
			} else
				fatal_error(Mobius_Error::internal, "An option_group parameter is of wrong type.");
			
			//TODO: Check if we overwrite an existing option?? Should not really happen unless we make weird API for this.
			std::string par_key = serialize(par_id);
			options.options[par_key] = par_val;
		}
	}
	
}


struct
Scope_Writer {
	FILE *file = nullptr;
	std::vector<char> scope_stack;
	bool same_line = false;
	
	int wanted_newlines = 0;
	bool do_ident = true;
	
	void newline(int amount = 1, bool tabs = true) {
		if(tabs != do_ident) process_newlines();
		do_ident = tabs;
		wanted_newlines += amount;
	}
	
	void process_newlines() {
		while(wanted_newlines > 0) {
			fprintf(file, "\n");
			if(do_ident) {
				for(int idx = 0; idx < scope_stack.size(); ++idx)
					fprintf(file, "\t");
			}
			wanted_newlines--;
		}
	}
	
	void open_scope(char type, bool new_line = true) {
		process_newlines();
		if(same_line)
			fatal_error(Mobius_Error::internal, "Tried to open a new scope after a same_line scope.");
		if(type != '{' && type != '[' && type != '!')
			fatal_error(Mobius_Error::internal, "Invalid scope type ", type, ".");
		if(type == '!')
			fprintf(file, "[");
		fprintf(file, "%c", type);
		scope_stack.push_back(type);
		if(new_line) {
			same_line = false;
			newline();
		} else {
			same_line = true;
			fprintf(file, " ");
		}
	}
	
	void close_scope(bool extra_new_line_after = true) {
		
		if(scope_stack.empty())
			fatal_error(Mobius_Error::internal, "Closed too many scopes.");
		char type = scope_stack.back() == '{' ? '}' : ']';
		scope_stack.resize(scope_stack.size()-1);
		if(wanted_newlines > 2)
			wanted_newlines--;
		process_newlines();
		same_line = false;
		fprintf(file, "%c", type);
		newline(1 + (int)extra_new_line_after);
	}
	
	void write(const char *fmt, ...) {
		process_newlines();
		va_list args;
		va_start(args, fmt);
		vfprintf(file, fmt, args);
		va_end(args);
	}
	
	void open_decl(Data_Set *data_set, Entity_Id id) {
		process_newlines();
		auto entity = data_set->find_entity(id);
		auto sym = data_set->get_symbol(id);
		if(!sym.empty())
			write("%s : ", sym.c_str());
		write("%s(", name(entity->decl_type));
		if(!entity->name.empty())
			write("\"%s\"", entity->name.c_str());
	}
	
	void write_identifier_list(Data_Set *data_set, const std::vector<Entity_Id> &idents, bool leading_comma=false) {
		process_newlines();
		for(int idx = 0; idx < idents.size(); ++idx) {
			if(idx != 0 || leading_comma)
				write(", ");
			auto sym = data_set->get_symbol(idents[idx]);
			write("%s", sym.c_str());
		}
	}
};

void
write_scope_to_file(Data_Set *data_set, Decl_Scope *scope, Scope_Writer *writer, bool leading_newline = true);

void
write_index_set_to_file(Data_Set *data_set, Scope_Writer *writer, Entity_Id index_set_id) {
	
	auto index_set = data_set->index_sets[index_set_id];
	
	writer->open_decl(data_set, index_set_id);
	writer->write(") ");
	
	if(is_valid(index_set->sub_indexed_to))
		writer->write("@sub(%s) ", data_set->get_symbol(index_set->sub_indexed_to).data());
	if(!index_set->union_of.empty()) {
		writer->write("@union(");
		writer->write_identifier_list(data_set, index_set->union_of);
		writer->write(") ");
	}
	
	if(!index_set->union_of.empty() || is_valid(index_set->is_edge_of_connection)) {
		// These should not have any data.
		writer->newline(2);
		return;
	}
	
	if(is_valid(index_set->sub_indexed_to)) {
		writer->open_scope('!');
		s32 count = data_set->index_data.get_max_count(index_set->sub_indexed_to).index;
		for(int idx = 0; idx < count; ++idx) {
			Index_T parent_idx = Index_T  { index_set->sub_indexed_to, idx };
			writer->process_newlines(); // Ouch. A bit hacky that we have to do this.
			data_set->index_data.write_index_to_file(writer->file, parent_idx);
			writer->write(" : ");
			writer->open_scope('[', false);
			data_set->index_data.write_indexes_to_file(writer->file, index_set_id, parent_idx);
			writer->close_scope(false);
		}
		writer->close_scope();
	} else {
		writer->open_scope('[', false);
		data_set->index_data.write_indexes_to_file(writer->file, index_set_id);
		writer->close_scope();
	}
}

void
write_component_ref(Data_Set *data_set, Scope_Writer *writer, Compartment_Ref &ref) {
	
	if(is_valid(ref.id)) {
		auto component = data_set->components[ref.id];
		writer->write("%s[", data_set->get_symbol(ref.id).data());
		int idx = 0;
		for(auto index_set_id : component->index_sets) {
			auto index_set = data_set->index_sets[index_set_id];
			
			bool quote;
			std::string index_name = data_set->index_data.get_index_name(ref.indexes, ref.indexes.indexes[idx], &quote);
			maybe_quote(index_name, quote);
			
			writer->write(" %s", index_name.data());
			++idx;
		}
		writer->write(" ]");
	} else
		writer->write("out");
}

void
write_component_to_file(Data_Set *data_set, Scope_Writer *writer, Entity_Id component_id) {
	
	auto component = data_set->components[component_id];
	writer->open_decl(data_set, component_id);
	writer->write_identifier_list(data_set, component->index_sets, true);
	writer->write(")");
	writer->newline();
}

void
write_connection_to_file(Data_Set *data_set, Scope_Writer *writer, Entity_Id connection_id) {
	
	auto connection = data_set->connections[connection_id];
	
	writer->open_decl(data_set, connection_id);
	writer->write(") ");
	writer->open_scope('{');
	
	write_scope_to_file(data_set, &connection->scope, writer); // Writes the component declarations.
	
	writer->newline();
	
	if(connection->type == Connection_Data::Type::none) {
		writer->close_scope();
		return;
	}
	if(connection->type != Connection_Data::Type::directed_graph)
		fatal_error(Mobius_Error::internal, "Unimplemented writing of connection type.");
	
	writer->write("directed_graph");
	if(is_valid(connection->edge_index_set))
		writer->write("(%s)", data_set->get_symbol(connection->edge_index_set).data());
	writer->write(" ");
	writer->open_scope('[', false);
	Compartment_Ref *last_node = nullptr;
	for(auto &arr : connection->arrows) {
		if(!last_node || !(*last_node == arr.first)) {
			writer->newline();
			write_component_ref(data_set, writer, arr.first);
		}
		writer->write(" -> ");
		write_component_ref(data_set, writer, arr.second);
		last_node = &arr.second;
	}
	writer->newline();
	writer->close_scope(false);
	
	writer->close_scope();
}

void
write_series_to_file(Data_Set *data_set, Scope_Writer *writer, Entity_Id series_id) {
	
	// NOTE: This only rewrites the load declaration, it doesn't write the data back to the csv or xlsx files (yet?).
	auto series_data = data_set->series[series_id];
	writer->open_decl(data_set, series_id);
	writer->write("\"%s\")", series_data->file_name.data());
	writer->newline(2);
}

void
write_module_to_file(Data_Set *data_set, Scope_Writer *writer, Entity_Id module_id) {
	
	auto module = data_set->modules[module_id];
	if(module->scope.all_ids.empty()) return; // Don't bother to write out empty modules.
	
	writer->open_decl(data_set, module_id);
	writer->write(", version(%d, %d, %d)) ", module->version.major, module->version.minor, module->version.revision);
	writer->open_scope('{');
	
	write_scope_to_file(data_set, &module->scope, writer);
	
	writer->close_scope();
}

void
write_par_group_to_file(Data_Set *data_set, Scope_Writer *writer, Entity_Id par_group_id) {
	
	auto par_group = data_set->par_groups[par_group_id];
	if(par_group->error || par_group->mark_for_deletion) return;

	writer->open_decl(data_set, par_group_id);
	writer->write_identifier_list(data_set, par_group->index_sets, true);
	writer->write(") ");
	writer->open_scope('{');
	
	write_scope_to_file(data_set, &par_group->scope, writer);
	
	writer->close_scope();
}

void
write_quick_select_to_file(Data_Set *data_set, Scope_Writer *writer, Entity_Id select_id) {
	
	auto quick_select = data_set->quick_selects[select_id];
	
	writer->open_decl(data_set, select_id);
	writer->write(") ");
	//writer->write("quick_select ");
	writer->open_scope('!');
	
	for(auto &select : quick_select->selects) {
		writer->write("\"%s\" : ", select.name.c_str());
		writer->open_scope('[', false);
		for(auto &series_name : select.series_names)
			writer->write("\"%s\" ", series_name.c_str());
		writer->close_scope(false);
	}
	
	writer->close_scope();
}

void
write_parameter_value(Scope_Writer *writer, Parameter_Value val, Decl_Type decl_type) {
	char buf[64];
	switch(decl_type) {
		case Decl_Type::par_real :
			writer->write("%.15g ", val.val_real);
			break;
		
		case Decl_Type::par_bool :
			writer->write("%s ", val.val_boolean ? "true" : "false");
			break;
		
		case Decl_Type::par_int :
			writer->write("%lld ", (long long)val.val_integer);
			break;
		
		case Decl_Type::par_datetime :
			val.val_datetime.to_string(buf);
			writer->write("%s ", buf);
			break;
	}
}

void
write_parameter_to_file(Data_Set *data_set, Scope_Writer *writer, Entity_Id parameter_id) {
	
	auto par = data_set->parameters[parameter_id];
	if(par->mark_for_deletion) return;
	
	writer->open_decl(data_set, parameter_id);
	writer->write(") ");
	auto par_group = data_set->par_groups[par->scope_id];
	
	if(is_valid(par->from_pos)) {
		writer->write("@from_position(%s)", data_set->get_symbol(par->from_pos).data());
		writer->newline(2);
		
	} else if(!par->is_on_map_form) {
	
		int expect_count = data_set->index_data.get_instance_count(par_group->index_sets);
		
		if(expect_count == par->get_count()) {
			
			int n_dims = std::max(1, (int)par_group->index_sets.size());
			bool multiline = n_dims > 1;
			if(!multiline) writer->newline();
			writer->open_scope('[', multiline);
			
			int offset = 0;
			data_set->index_data.for_each(par_group->index_sets,
				[&](Indexes &indexes) {
					
					int val_idx = offset++;
					if(par->decl_type == Decl_Type::par_enum)
						writer->write("%s ", par->values_enum[val_idx].c_str());
					else
						write_parameter_value(writer, par->values[val_idx], par->decl_type);
				},
				[&](int pos) {
					if(multiline && (offset > 0) && (pos == n_dims-1))
						writer->newline();
				}
			);
			if(multiline)
				writer->newline();
			writer->close_scope();
		} else {
			//fatal_error(Mobius_Error::internal, "Somehow we have a data set where a parameter \"", par->name, "\" has a value array that is not sized correctly wrt. its index set dependencies.");
			
			// NOTE: This can legitimately happen if this comes from a module that was not loaded, hence it would not have triggered an error on load if it was not resized properly after an index count change.
			// Instead of trying to write it out formatted, just write it as a list, but don't delete it.
			// The reason to do this is to allow the user to deal with the error later if they re-enable the module (and not just decide for them that this data should be discarded).
			// Alternatively we could do a post-load check on the data set if the parameter count is correct, not just when building a model application with it in order to rule out ill-formed data sets whatsoever?
			writer->open_scope('[', false);
			if(par->decl_type == Decl_Type::par_enum) {
				for(auto &val : par->values_enum)
					writer->write("%s ", val.c_str());
			} else {
				for(auto val : par->values)
					write_parameter_value(writer, val, par->decl_type);
			}
			writer->close_scope();
		}
	} else {
		writer->open_scope('!');
		
		int rewind = 0;
		Parmap_Entry *prev = nullptr;
		int size;
		
		for(auto &entry : par->parmap_data) {
			
			size = (int)entry.indexes.indexes.size(); // This should be the same for all entries.
			
			rewind = 0;
			if(prev) {
				for(; rewind < size; ++rewind) {
					if(prev->indexes.indexes[rewind] != entry.indexes.indexes[rewind])
						break;
				}
				rewind = std::min(size-1, rewind); // TODO: Why is this necessary???? Compiler bug?
				for(int p = size-1; p > rewind; --p)
					writer->close_scope(false);
			}
			prev = &entry;
			
			for(int p = rewind; p < size; ++p) {
				auto index = entry.indexes.indexes[p];
				
				if(p != size-1) {
					writer->process_newlines();
					data_set->index_data.write_index_to_file(writer->file, entry.indexes, index);
					writer->write(" : ");
					writer->open_scope('!');
				} else {
					// TODO: this is not entirely correct if the inner index was not numeric.
					writer->write("%.15g : %.15g", entry.pos, entry.value);
					writer->newline();
				}
			}
		}
		for(int idx = 0; idx < size-1; ++idx) // TODO: is this correct?
			writer->close_scope(false);
		writer->close_scope();
	}
}

void
write_position_map_to_file(Data_Set *data_set, Scope_Writer *writer, Entity_Id map_id) {
	auto map = data_set->position_maps[map_id];
	
	writer->open_decl(data_set, map_id);
	writer->write("%s) ", data_set->get_symbol(map->index_set_id).c_str());
	//if(map->raw_is_widths)
	//	writer->write("@widths ");
	//if(map->linear_interp)
	//	writer->write("@linear_interpolate ");
	writer->open_scope('!');
	for(int idx = 0; idx < map->pos_vals_raw.size(); ++idx) {
		writer->write("%.15g : %.15g", map->pos_vals_raw[idx], map->width_vals_raw[idx]);
		writer->newline();
	}
	writer->close_scope();
}

void
write_scope_to_file(Data_Set *data_set, Decl_Scope *scope, Scope_Writer *writer, bool leading_newline) {
	
	if(leading_newline)
		writer->newline();
	
	for(auto id : scope->by_type(Reg_Type::series))
		write_series_to_file(data_set, writer, id);
	
	for(auto id : scope->by_type(Reg_Type::index_set))
		write_index_set_to_file(data_set, writer, id);
	
	for(auto id : scope->by_type(Reg_Type::position_map))
		write_position_map_to_file(data_set, writer, id);
	
	for(auto id : scope->by_type(Reg_Type::connection))
		write_connection_to_file(data_set, writer, id);
	
	for(auto id : scope->by_type(Reg_Type::component))
		write_component_to_file(data_set, writer, id);
	
	for(auto id : scope->by_type(Reg_Type::quick_select))
		write_quick_select_to_file(data_set, writer, id);
	
	for(auto id : scope->by_type(Reg_Type::par_group))
		write_par_group_to_file(data_set, writer, id);
	
	for(auto id : scope->by_type(Reg_Type::module))
		write_module_to_file(data_set, writer, id);
	
	for(auto id : scope->by_type(Reg_Type::parameter))
		write_parameter_to_file(data_set, writer, id);
}

void
Data_Set::write_to_file(String_View file_name) {
	
	FILE *file = nullptr;
	
	bool error = false;
	try {
		
		file = open_file(file_name, "wb");
		Scope_Writer writer;
		writer.file = file;
		
		writer.write("data_set ");
		writer.open_scope('{');
	
		if(!doc_string.empty()) {
			writer.newline(1, false);
			writer.write("\"\"\"");
			writer.newline(1, false);
			writer.write("%s", doc_string.c_str());
			writer.newline(1, false);
			writer.write("\"\"\"");
			writer.newline(2);
		}
		
		if(time_step_was_provided) {
			std::string unit_str = time_step_unit.to_decl_str();
			writer.write("time_step(%s)", unit_str.data());
			writer.newline(2);
		}
		
		if(series_interval_was_provided) {
			char buf1[64], buf2[64];
			series_begin.to_string(buf1);
			series_end.to_string(buf2);
			writer.write("series_interval(%s, %s)", buf1, buf2);
			writer.newline(2);
		}
		
		write_scope_to_file(this, &top_scope, &writer, false);
		
		writer.close_scope();
		
	} catch(int) {
		error = true;
	}
	
	bool file_was_opened = file;
	if(file_was_opened)
		fclose(file);
	
	if(error) {
		error_print("Error occured during data set saving. ");
		mobius_error_exit();
	}
}






