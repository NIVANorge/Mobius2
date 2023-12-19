
#include <algorithm>

#include "data_set.h"
#include "ole_wrapper.h"

void
read_series_data_from_csv(Data_Set *data_set, Series_Data *series_data, String_View file_name, String_View text_data);

void
read_series_data_from_spreadsheet(Data_Set *data_set, Series_Data *series_data, OLE_Handles *handles, String_View file_name);

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
	
	match_declaration(decl,
	{
		{Token_Type::quoted_string},
		{Token_Type::quoted_string, {Decl_Type::index_set, true}},
	}, false, -1);
	
	set_serial_name(catalog, this);
	auto data_set = static_cast<Data_Set *>(catalog);
	auto parent_scope = catalog->get_scope(scope_id);
	scope.parent_id = id;
	scope.import(*parent_scope); // Not sure if this is necessary here. Nice to do it for consistency maybe.
	
	for(int argidx = 1; argidx < decl->args.size(); ++argidx) {
		auto id = parent_scope->resolve_argument(Reg_Type::index_set, decl->args[argidx]);
		index_sets.push_back(id);
	}
	
	if(!index_sets.empty())
		data_set->index_data.check_valid_distribution(index_sets, source_loc);
	
	auto body = static_cast<Decl_Body_AST *>(decl->body);
	
	const std::set<Decl_Type> allowed_decls = {
		Decl_Type::par_real,
		Decl_Type::par_int,
		Decl_Type::par_bool,
		Decl_Type::par_enum,
		Decl_Type::par_datetime
	};
	
	for(auto child : body->child_decls)
		catalog->register_decls_recursive(&scope, child, allowed_decls);
	
	for(auto id : scope.all_ids)
		catalog->find_entity(id)->process_declaration(catalog);
}

void
Parameter_Data::process_declaration(Catalog *catalog) {

	match_data_declaration(decl, {{Token_Type::quoted_string}});
	
	set_serial_name(catalog, this);
	
	if(decl->data->data_type != Data_Type::list)
		fatal_error(Mobius_Error::internal, "Somehow got non-list data for a parameter.");
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
	
	has_been_processed = true;
}

// NOTE: We had to put these as a global, because if we put them as a member of the Data_Set, we have to put them in data_set.h, and that creates all sorts of problems with double-inclusion of windows.h (for some reason, strange that the include guards don't work..)
#if OLE_AVAILABLE
OLE_Handles ole_handles = {};
#endif

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
		#if OLE_AVAILABLE
		String_View relative = make_path_relative_to(other_file_name, data_set->path);
		ole_open_spreadsheet(relative, &ole_handles);
		read_series_data_from_spreadsheet(data_set, this, &ole_handles, other_file_name);
		#else
		single_arg(decl, 0)->print_error_header();
		fatal_error("Spreadsheet reading is only available on Windows.");
		#endif
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
		fatal_error("Duplicate declaration of time_step.");
	}
	match_declaration(decl, {{Decl_Type::unit}}, false);
	data_set->time_step_unit.set_data(decl->args[0]->decl);
	data_set->unit_source_loc = decl->source_loc;
	data_set->time_step_was_provided = true;
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
				
		for(auto &component_id : scope.by_type<Reg_Type::component>()) {
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
	
	
	// TODO: Ouch, we need to store the raw data from the declaration in order to be able to save it out again, so we need to store it as a registration..
	//    including    index_vals_raw, y_vals_raw, raw_is_widths, linear_interp
	
	match_data_declaration(decl, {{Decl_Type::index_set}}, true, 0, true);
	
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
	
	s64 max_count = data_set->index_data.get_max_count(index_set_id).index;
	
	auto map_data = static_cast<Data_Map_AST *>(decl->data);
	
	for(auto &entry : map_data->entries) {
		if(entry.key.type != Token_Type::integer) {
			entry.key.print_error_header();
			fatal_error("Expected an integer index value as the key.");
		}
		if(entry.data || !is_numeric(entry.single_value.type)) {
			map_data->source_loc.print_error_header();
			fatal_error("All value entries in the map must be numeric.");
		}
		index_vals_raw.push_back(entry.key.val_int);
		double value = entry.single_value.double_value();
		if(value < 0.0) {
			entry.single_value.print_error_header();
			fatal_error("All values must be positive numbers.");
		}
		y_vals_raw.push_back(value);
	}
	
	if(index_vals_raw.empty()) {
		map_data->source_loc.print_error_header();
		fatal_error("The position_map data can't be empty.");
	}
	
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
	
	// TODO: This should be factored out, for reuse with parameter values.
	
	std::vector<int> order(index_vals_raw.size());
	std::sort(order.begin(), order.end(), [this](int row_a, int row_b) -> bool { return index_vals_raw[row_a] < index_vals_raw[row_b]; });
	
	expanded_ys.resize(max_count);
	s64 prev_idx = -1;
	double prev_val = 0.0;
	for(int pos : order) {
		s64 index_val = index_vals_raw[pos];
		double y_val  = y_vals_raw[pos];
		
		if(!raw_is_widths) {
			if(y_val <= prev_val) {
				map_data->source_loc.print_error_header();
				fatal_error("The position values must be in ascending order.");
			}
		}
		
		if(prev_idx == -1) {
			for(int idx = 0; idx <= std::min(index_val, max_count-1); ++idx)
				expanded_ys[idx] = y_val;
		} else {
			for(int idx = prev_idx; idx <= std::min(index_val, max_count-1); ++idx) {
				if(linear_interp) {
					double t = double(idx-prev_idx)/double(index_val - prev_idx);
					expanded_ys[idx] = (1.0-t)*prev_val + t*y_val;
				} else
					expanded_ys[idx] = prev_val;
			}
		}
		prev_idx = index_val;
		prev_val = y_val;
	}
	for(int idx = prev_idx + 1; idx < max_count; ++idx)
		expanded_ys[idx] = prev_val;
	
	std::vector<double> final_ys = expanded_ys;
	if(raw_is_widths) {
		for(int idx = 1; idx < final_ys.size(); ++idx)
			final_ys[idx] += final_ys[idx-1];
	}
	
	data_set->index_data.set_position_map(index_set_id, std::move(final_ys), decl->source_loc);
	
	has_been_processed = true;
}


void
Data_Set::read_from_file(String_View file_name) {
	
	// NOTE: read_from_file is not thread safe since it is accessing global ole_handles.
	
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
		Decl_Type::par_group,
		Decl_Type::series,
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
		else if(child->type == Decl_Type::module)
			register_single_decl(scope, child, allowed_data_decls); // Just so that it doesn't try to process the 'version' argument separately
		else
			register_decls_recursive(scope, child, allowed_data_decls);
	}
	
	// Almost everything depends on the index sets being correctly set up before the data is processed.
	for(auto id : scope->by_type<Reg_Type::index_set>())
		process_index_data(this, scope, id); // Can't call process_declaration on index_sets since we can't reuse the same function as in the model.
	
	for(auto id : scope->by_type<Reg_Type::position_map>())
		find_entity(id)->process_declaration(this);
	
	// Also have to process connections in order to set indexes for edge index sets before anything else is done.
	for(auto id : scope->by_type<Reg_Type::connection>())
		find_entity(id)->process_declaration(this);
	
	for(auto id : scope->all_ids) {
		auto entity = find_entity(id);
		if(entity->has_been_processed) continue;
		entity->process_declaration(this);
	}
	
#if OLE_AVAILABLE
	ole_close_app_and_spreadsheet(&ole_handles);
#endif
	file_handler.unload_all();
	delete main_decl;
	
	// TODO: Post-processing.
		// Check expected value count for parameters. (Although don't we also do that in the model_application?)
		// I guess it could be good to do it here anyway since we want to be able to have internal consistency for resizing etc.
}

void
Data_Set::generate_index_data(const std::string &name, const std::string &sub_indexed_to, const std::vector<std::string> &union_of) {
	
	// NOTE: We don't check for correctness of sub-indexing vs. union here since we assume this was done by the Model_Application.
	auto id = index_sets.create_internal(&top_scope, "", name, Decl_Type::index_set);

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
					auto index_set_id = data_set->top_scope.expect(Reg_Type::index_set, &token);
					
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
				//TODO: Check for conflicting flags.
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
			fprintf(file, '[');
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
write_scope_to_file(Data_Set *data_set, Decl_Scope *scope, Scope_Writer *writer);

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
	writer->open_decl(data_set, module_id);
	writer->write(", version(%d, %d, %d)) ", module->version.major, module->version.minor, module->version.revision);
	writer->open_scope('{');
	
	write_scope_to_file(data_set, &module->scope, writer);
	
	writer->close_scope();
}

void
write_par_group_to_file(Data_Set *data_set, Scope_Writer *writer, Entity_Id par_group_id) {
	
	auto par_group = data_set->par_groups[par_group_id];
	if(par_group->error) return;

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
write_parameter_to_file(Data_Set *data_set, Scope_Writer *writer, Entity_Id parameter_id) {
	
	auto par = data_set->parameters[parameter_id];
	if(par->mark_for_deletion) return;
	
	writer->open_decl(data_set, parameter_id);
	writer->write(") ");
	auto par_group = data_set->par_groups[par->scope_id];
	
	int n_dims = std::max(1, (int)par_group->index_sets.size());
	bool multiline = n_dims > 1;
	if(!multiline) writer->newline();
	writer->open_scope('[', multiline);
	
	int expect_count = data_set->index_data.get_instance_count(par_group->index_sets);
	if(expect_count != par->get_count())
		fatal_error(Mobius_Error::internal, "Somehow we have a data set where a parameter \"", par->name, "\" has a value array that is not sized correctly wrt. its index set dependencies.");
	
	int offset = 0;
	data_set->index_data.for_each(par_group->index_sets,
		[&](Indexes &indexes) {
			
			int val_idx = offset++;
			if(par->decl_type == Decl_Type::par_enum) {
				writer->write("%s ", par->values_enum[val_idx].data());
			} else {
				Parameter_Value val = par->values[val_idx];
				char buf[64];
				switch(par->decl_type) {
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
		},
		[&](int pos) {
			if(multiline && (offset > 0) && (pos == n_dims-1))
				writer->newline();
		}
	);
	if(multiline)
		writer->newline();
	writer->close_scope();
}

void
write_position_map_to_file(Data_Set *data_set, Scope_Writer *writer, Entity_Id map_id) {
	auto map = data_set->position_maps[map_id];
	
	writer->open_decl(data_set, map_id);
	writer->write("%s) ", data_set->get_symbol(map->index_set_id).c_str());
	if(map->raw_is_widths)
		writer->write("@widths ");
	if(map->linear_interp)
		writer->write("@linear_interpolate ");
	writer->open_scope('!');
	for(int idx = 0; idx < map->index_vals_raw.size(); ++idx) {
		writer->write("%lld : %.15g", (long long)map->index_vals_raw[idx], map->y_vals_raw[idx]);
		writer->newline();
	}
	writer->close_scope();
}

void
write_scope_to_file(Data_Set *data_set, Decl_Scope *scope, Scope_Writer *writer) {
	
	writer->newline();
	
	for(auto id : scope->by_type<Reg_Type::index_set>())
		write_index_set_to_file(data_set, writer, id);
	
	for(auto id : scope->by_type<Reg_Type::position_map>())
		write_position_map_to_file(data_set, writer, id);
	
	for(auto id : scope->by_type<Reg_Type::connection>())
		write_connection_to_file(data_set, writer, id);
	
	for(auto id : scope->by_type<Reg_Type::quick_select>())
		write_quick_select_to_file(data_set, writer, id);
	
	for(auto id : scope->by_type<Reg_Type::component>())
		write_component_to_file(data_set, writer, id);
		
	for(auto id : scope->by_type<Reg_Type::series>())
		write_series_to_file(data_set, writer, id);
	
	for(auto id : scope->by_type<Reg_Type::par_group>())
		write_par_group_to_file(data_set, writer, id);
	
	for(auto id : scope->by_type<Reg_Type::module>())
		write_module_to_file(data_set, writer, id);
	
	for(auto id : scope->by_type<Reg_Type::parameter>())
		write_parameter_to_file(data_set, writer, id);
}

void
Data_Set::write_to_file(String_View file_name) {
	
	//String_View backup_data = {};
	
	FILE *file = nullptr;
	
	bool error = false;
	try {
		//backup_data = read_entire_file(file_name); // Oops, this obviously only works if it doesn't exist already.
		
		// read_entire_file has closed it. Open it again for writing.
		file = open_file(file_name, "w");
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
			writer.newline();
		}
		
		write_scope_to_file(this, &top_scope, &writer);
		
		writer.close_scope();
		
	} catch(int) {
		error = true;
	}
	
	bool file_was_opened = file;
	if(file_was_opened)
		fclose(file);
	
	if(error) {
		error_print("Error occured during data set saving. ");
		/*
		if(backup_data.count && file_was_opened) {
			try {
				error_print("Trying to back up the file to its original state.");
				file = open_file(file_name, "w");
				if(file) {
					fwrite(backup_data.data, backup_data.count, 1, file);
					fclose(file);
				}
			} catch(int) {
			}
		}*/
		mobius_error_exit();
	}
}






