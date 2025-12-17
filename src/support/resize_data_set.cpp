
#include "resize_data_set.h"

void
resize_data_set(
	Data_Set *data_set,
	std::vector<New_Indexes> &new_indexes,
	std::vector<New_Connections> &new_connections
) {

	Index_Data old_index_data = data_set->index_data;
	
	// TODO: We should think about making the operation so that nothing is changed if there is an error underway,
	//   otherwise the dataset will be corrupted.
	
	// TODO: If you edit a parent index set, every sub-indexed index set should be cleared
		// Or at least reshape it if no new indexes were provided for it. (Depending on what new indexes are for the parent)
		// (see also comment in index_data.cpp)
	
	//// Setting new index values.
	
	std::set<Entity_Id> modified_sets;
	
	for(auto &entry : new_indexes) {
		
		auto set_id = data_set->top_scope.deserialize(entry.index_set, Reg_Type::index_set);
		if(!is_valid(set_id))
			fatal_error(Mobius_Error::api_usage, "Could not find an index set with name \"", entry.index_set, "\" in the data set.");
		
		modified_sets.insert(set_id);
	}
	
	// NOTE: The index sets are now sorted by the entity id, so that they are processed in the correct order.
	
	for(auto set_id : modified_sets) {
		
		auto index_set = data_set->index_sets[set_id];
		if(entry.data.size() > 1 && !is_valid(index_set->sub_indexed_to))
			// Better error message? However, this is unlikely to be triggered.
			fatal_error(Mobius_Error::api_usage, "Received multiple index data for index set \"", entry.index_set, "\" that is not sub-indexed");
		
		if(old_index_data.has_position_map(set_id))
			fatal_error(Mobius_Error::api_usage, "Unable to resize index set \"", entry.index_set, "\" that is on position map form.");
		
		data_set->index_data.clear_index_data(set_id);
		
		for(auto &pair : entry.data) {
			
			auto &parent_name = pair.first;
			auto &index_list = pair.second;
			
			Index_T parent_index = Index_T::no_index();
			if(is_valid(&parent_name)) {
				if(!is_valid(index_set->sub_indexed_to))
					fatal_error(Mobius_Error::api_usage, "Trying to set sub-indexing scheme for index set \"", entry.index_set, "\", which is not sub-indexed.");
				parent_index = data_set->index_data.find_index(index_set->sub_indexed_to, &parent_name);
			}
			
			data_set->index_data.set_indexes(set_id, index_list, parent_index);
			
		}
		
	}
	
	// TODO: Allow user to input position map reshapings.
	
	//// Reshaping connection data
	
	std::set<Entity_Id> modified_conns;
	
	for(auto &entry : new_connections) {
		
		auto conn_id = data_set->top_scope.deserialize(entry.connection, Reg_Type::connection);
		if(!is_valid(conn_id))
			fatal_error(Mobius_Error::api_usage, "Could not find a connection with name \"", entry.connection, "\" in the data set.");
		
		auto connection = data_set->connections[conn_id];
		
		if(connection->type != Connection_Data::Type::directed_graph)
			fatal_error(Mobius_Error::api_usage, "Currently only support reshaping connections of type directed_graph.");
		
		if(is_valid(connection->edge_index_set))
			data_set->index_data.clear_index_data(connection->edge_index_set);
		
		connection->arrows.clear();
		
		Token_Stream stream("(API call)", entry.graph_data);
		Directed_Graph_AST *graph_data = static_cast<Directed_Graph_AST *>(parse_directed_graph(&stream));
		
		Source_Location err_loc = {};
		connection->process_directed_graph(data_set, connection->edge_index_set, graph_data, err_loc);
		
		modified_conns.insert(conn_id);
		
		delete graph_data;
	}
	
	// Fix union index sets that had members that were edited.
	std::set<Entity_Id> modified_unions;
	for(auto set_id : data_set->index_sets) {
		auto index_set = data_set->index_sets[set_id];
		if(index_set->union_of.empty()) continue;
		
		bool reset = false;
		for(auto other_id : modified_sets) {
			if(index_set->union_of.find(other_id) != index_set->union_of.end()) {
				reset = true;
				break;
			}
		}
		if(reset) {
			data_set->index_data.clear_index_data(set_id);
			Source_Location err_loc = {};
			data_set->index_data.initialize_union(set_id, err_loc);
			modified_unions.insert(set_id);
		}
	}
	modified_sets.insert(modified_unions.begin(), modified_unions.end());
	
	
	
	// Check if connections that were not edited were invalidated by an index set edit.
	// NOTE: We could instead try to fix up the indexes and delete arrows where needed, but probably not worth it
	//   since the user most likely would want to input new data in this case.
	for(auto conn_id : data_set->connections) {
		if(modified_conns.find(conn_id) != modified_conns.end()) continue;
		
		auto connection = data_set->connections[conn_id];
		
		for(Entity_Id comp_id : connection->scope.by_type(Reg_Type::component)) {
			auto comp = data_set->components[comp_id];
			for(auto set_id : comp->index_sets) {
				if(modified_sets.find(set_id) != modified_sets.end()) {
					auto set = data_set->index_sets[set_id];
					fatal_error(Mobius_Error::api_usage, "The connection \"", connection->name, "\" has components that are distributed over the reshaped index set \"", set->name, "\". For this reason you need to input new connection data for this connection in the reshape call.");
				}
			}
		}
	}
	
	//// Reshaping parameter data
	
	for(auto par_id : data_set->parameters) {
		
		auto parameter = data_set->parameters[par_id];
		auto group = data_set->par_groups[parameter->scope_id];
		
		bool reshaped = false;
		for(auto set_id : group->index_sets) {
			if(modified_sets.find(set_id) != modified_sets.end()) {
				reshaped = true;
				break;
			}
		}
		if(!reshaped) continue;
		
		if(!parameter->is_on_map_form) {
			
			std::vector<Parameter_Value> old_values = parameter->values;
			std::vector<std::string> old_values_enum = parameter->values_enum;
			
			parameter->values.clear();
			parameter->values_enum.clear();
			
			data_set->index_data.for_each(group->index_sets,
				[&old_index_data, &parameter, &old_values, &old_values_enum, &group, data_set](Indexes &new_idx) {
					
					std::vector<std::string> new_names;
					data_set->index_data.get_index_names(new_idx, new_names);
					
					// See if the new index tuple is named the same as an old index tuple, and in that case copy the corresponding data.
					//TODO: This implementation is very inefficient, but the loops are not that big in practice.
						// moreover, it shouldn't need to deserialize and compare every tuple, could do it one level at a time..
					int old_pos = 0;
					bool found = false;
					old_index_data.for_each(group->index_sets,
						[&old_index_data, &old_pos, &found, &new_names](Indexes &old_idx) {
							
							std::vector<std::string> old_names;
							old_index_data.get_index_names(old_idx, old_names);
							
							if(old_names == new_names) found = true;
							// ugh, we would want to break here, but this calling pattern doesn't allow us :(
							
							if(!found) ++old_pos;
						}
					);
					if(!found) old_pos = 0;
					
					if(parameter->decl_type == Decl_Type::par_enum)
						parameter->values_enum.push_back(old_values_enum[old_pos]);
					else
						parameter->values.push_back(old_values[old_pos]);
				}
			);
		} else {
			fatal_error(Mobius_Error::api_usage, "Resizing data sets with parameters on map form is not yet supported");
		}

	}
	
	//// TODO: Checking consistency of input data (what to do if error?).
		// In case of error, probably best just to delete that series and output a WARNING telling the user to fix the input source file.
		
	for(auto ser_id : data_set->series) {
		auto ser = data_set->series[ser_id];
		for(auto &series_set : ser->series) {
			
		
			for(int i = 0; i < series_set.header_data.size(); ++i) {
				
				auto &hdr = series_set.header_data[i];
				
				for(auto &old_idx : hdr.indexes) {
					
					bool need_update = false;
					
					for(auto &idx : old_idx.indexes) {
						if(modified_sets.find(idx.index_set) != modified_sets.end())
							need_update = true;
					}
					
					if(need_update) {
						//std::vector<std::string> old_names;
						//old_index_data.get_index_names(old_idx, old_names);
						// Currently mapping them is annoying. We should write support code for it in index_data.cpp
						// For now, just error.
						
						begin_error(Mobius_Error::api_usage);
						error_print("The file \"", ser->file_name, "\"");
						if(!series_set.sheet.empty())
							error_print(" sheet \"", series_set.sheet, "\"");
						error_print(" contains a series \"", hdr.name, "\" that is distributed over an index set that was edited. This is currently not supported.");
						mobius_error_exit();
					}
				}
			}
			
			
			
		}
	}
}