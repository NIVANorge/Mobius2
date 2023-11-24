
#include "data_set.h"


void
Parameter_Data::process_declaration(Catalog *catalog) {

	match_data_declaration(decl, {{Token_Type::quoted_string}});
	
	set_serial_name(catalog, this);
	
	auto &data = static_cast<List_Data_AST *>(decl->data)->list;
	
	// TODO: Can't do expect_count here. Must instead in the Data_Set go over and check them. Preferrably by par_group.
	
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
			val.val_date = date_token.val_date;
			if(idx+1 < data.size() && data[idx+1].type == Token_Type::time) {
				val.val_date += data[idx+1].val_date;
				++idx;
			}
			values.push_back(val);
		}
	} else {
		values.reserve(data.size());
		Parameter_Value val;
		for(auto &token : data) {
			if(decl_type == Decl_Type::par_real && is_numeric(token.type))
				val.val_real = token.double_value();
			else if(decl_type == Decl_Type::par_int && token.type == Token_Type::integer)
				val.val_int  = token.int_value;
			else if(decl_type == Decl_Type::par_bool && token.type == Token_Type::boolean)
				val.val_bool = token.bool_value;
			} else {
				token.print_error_header();
				fatal_error("Expected a parameter value of type ", name(get_value_type(decl_type), ".");
			}
			values.push_back(val);
		}
	}
	
	has_been_processed = true;
}

void
Series_Data::process_declaration(Catalog *catalog) {
	
	match_data_declaration(decl, {{Token_Type::quoted_string}}, false, 0, false, 0);
					
	String_View other_file_name = single_arg(decl, 0)->string_value;
	
	auto data_set = static_cast<Data_Set *>(catalog);
	
	if(data_set->file_handler.is_loaded(other_file_name, data_set->main_file)) {
		token.print_error_header();
		fatal_error("The file ", other_file_name, " has already been loaded.");
	}
	
	// TODO: We have to make the reading functions below put the series data in this registration.
	
	bool success;
	String_View extension = get_extension(other_file_name, &success);
	if(success && (extension == ".xlsx" || extension == ".xls")) {
		#if OLE_AVAILABLE
		String_View relative = make_path_relative_to(other_file_name, file_name);
		ole_open_spreadsheet(relative, data_set->ole_handles);
		//read_series_data_from_spreadsheet(data_set, &handles, other_file_name);
		read_series_data_from_spreadsheet(data_set, this, &handles, other_file_name);
		#else
		single_arg(decl, 0)->print_error_header();
		fatal_error("Spreadsheet reading is only available on Windows.");
		#endif
	} else {
		String_View text_data = data_set->file_handler.load_file(other_file_name, single_arg(decl, 0)->source_loc, file_name);
		//read_series_data_from_csv(data_set, other_file_name, text_data);
		read_series_data_from_csv(data_set, this, other_file_name, text_data);
	}
	
	has_been_processed = true;
}

void
Data_Set::process_index_data(Decl_Scope *scope, Entity_Id index_set_id) {
	
	auto index_set = index_sets[id];
	auto decl = index_set->decl;
	
	match_data_declaration(decl, {{Token_Type::quoted_string}}, true, 0, true, 1);
	// TODO: Call a factored-out version of the Index_Set_Registration::process_declaration
	
	if(!index_set->union_of.empty()) {
		if(decl->data) {
			decl->source_loc.print_error_header();
			fatal_error("A union index set should not be provided with index data.");
		}
		
	/* This should already be checked in the declaration processing.
		if(is_valid(data->sub_indexed_to)) {
			decl->source_loc.print_error_header();
			fatal_error("It is not currently supported that an index set can both be sub-indexed and be a union.");
		}
	*/
		index_data.initialize_union(index_set_id, decl->source_loc)
		index_set->has_been_processed = true;
		return;
	}
	
	if(is_valid(sub_indexed_to)) {
		if(decl->data->type != Data_Type::map) {
			decl->data->source_loc.print_error_header();
			fatal_error("Expected a map mapping parent indexes to index lists sice this index set is sub-indexed.");
		}
		
		auto data = static_cast<Map_Data_AST *>(decl->data);
		
		for(auto &entry : map->entries) {
			auto parent_idx = data_set->index_data.find_index(sub_indexed_to, &entry.key);
			
			if(entry.data->type != Data_Type::list) {
				entry.data->source_loc.print_error_header();
				fatal_error("Expected a simple list of indexes or an integer size.");
			}
			auto list = static_cast<List_Data_AST *>(entry.data);
			
			// TODO: What to do if it is empty?
			
			data_set->index_data.set_indexes(index_set_id, list->list, parent_idx);
		}
		
	} else {
		if(decl->data->type != Data_Type::list) {
			decl->data->source_loc.print_error_header();
			fatal_error("Expected a simple list of indexes or an integer size for this index set since it is not sub-indexed.");
		}
		auto data = static_cast<List_Data_AST *>(decl->data);
		
		// TODO: What to do if it is empty?
		
		index_data.set_indexes(index_set_id, data->list);
	}
	
	
	index_set->has_been_processed = true;
}

void
parse_time_step_decl(Decl_AST *decl, Data_Set *data_set) {
	// TODO!!!
	fatal_error(Mobius_Error::internal, "Not implemented yet.");
}


void
Data_Set::read_from_file(String_View file_name) {
	if(main_file != "")
		fatal_error(Mobius_Error::api_usage, "Tried make a data set read from a file ", file_name, ", but it already contains data from the file ", main_file, ".");
	
	main_file = std::string(file_name);
	
	
	// TODO: Open the ole handles.
	
	auto decl = read_catalog_ast_from_file(Decl_Type::data_set, &file_handler, file_name);
	match_declaration(decl, {{Token_Type::quoted_string}}, false, -1);
	
	if(body->doc_string.string_value.count)
		doc_string = body->doc_string.string_value;
	
	const std::set<Decl_Type> allowed_data_decls = {
		Decl_Type::index_set,
		Decl_Type::connection,
		Decl_Type::module,
		Decl_Type::par_group,
		Decl_Type::series,
		Decl_Type::time_step,
	};
	
	auto scope = &top_scope;
	
	// TODO: This could be problematic since it would also register version() declarations.
	//    Do we skip those here or handle it in another way.
	auto body = static_cast<Decl_Body_AST *>(decl->body);
	for(Decl_AST *child : body->child_decls) {
		if(child->type == Decl_Type::time_step)
			parse_time_step_decl(child, this);
		else
			register_decls_recursive(scope, child, allowed_data_decls);
	}
	
	for(auto &pair : scope->by_decl) {
		auto id = pair.second;
		if(id.reg_type == Reg_Type::index_set) // These have to be handled in a special way.
			process_index_data(scope, id);
	}
	
	for(auto &pair : scope->by_decl) {
		auto id = pair.second;
		auto entity = find_entity(id);
		if(entity->has_been_processed) continue;
		entity->process_declaration(this);
	}
	
	// TODO: Post-processing.
		// Check expected value count for parameters. (Although don't we also do that in the model_application?)
		// I guess it could be good to do it here anyway since we want to be able to have internal consistency for resizing etc.
		
	// TODO: Close files again.
}

