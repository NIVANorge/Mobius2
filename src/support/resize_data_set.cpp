
#include "resize_data_set.h"

void
resize_data_set(
	Data_Set *data_set,
	std::vector<New_Indexes> &new_indexes
	// TODO: Connection data
) {

	Index_Data old_index_data = data_set->index_data;
	
	// TODO: We should think about making the operation so that nothing is changed if there is an error underway,
	//   otherwise the dataset will be corrupted.
	
	// TODO: Would be better to sort entries on entity_id to guarantee having sub-indexed index sets after their parents.
	// TODO: If you edit a parent index set, every sub-indexed index set should be cleared
		// Or at least reshape it if no new indexes were provided for it. (Depending on what new indexes are for the parent)
		// (see also comment in index_data.cpp)
	// TODO: Does it 
	
	//// Setting new index values.
	
	std::set<Entity_Id> was_changed;
	
	for(auto &entry : new_indexes) {
		
		auto reg = data_set->top_scope[entry.index_set];
		if(!reg || !is_valid(reg->id) || reg->id.reg_type != Reg_Type::index_set)
			fatal_error(Mobius_Error::api_usage, "Could not find an index set with identifier '", entry.index_set, "' in the data set.");
		
		auto index_set = data_set->index_sets[reg->id];
		if(entry.data.size() > 1 && !is_valid(index_set->sub_indexed_to))
			// Better error message? However, this is unlikely to be triggered.
			fatal_error(Mobius_Error::api_usage, "Received multiple index data for index set '", entry.index_set, "' that is not sub-indexed");
		
		if(old_index_data.has_position_map(reg->id))
			fatal_error(Mobius_Error::api_usage, "Unable to resize index set '", entry.index_set, "' that is on position map form.");
		
		data_set->index_data.clear_index_data(reg->id);
		
		for(auto &pair : entry.data) {
			
			auto &parent_name = pair.first;
			auto &index_list = pair.second;
			
			Index_T parent_index = Index_T::no_index();
			if(is_valid(&parent_name)) {
				//log_print(name(parent_name.type), " ", parent_name.string_value, " ", parent_name.val_int, "\n");
				fatal_error("Sub-indexing not yet supported in resize_dataset."); // TODO!!
			}
			
			data_set->index_data.set_indexes(reg->id, index_list, parent_index);
			
		}
		
		was_changed.insert(reg->id);
		
	}
	
	//// Reshaping parameter data
	
	for(auto par_id : data_set->parameters) {
		
		auto parameter = data_set->parameters[par_id];
		auto group = data_set->par_groups[parameter->scope_id];
		
		bool reshaped = false;
		for(auto set_id : group->index_sets) {
			if(was_changed.find(set_id) != was_changed.end()) {
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
	
	
	//// TODO: Reshaping connection data
	
	
	//// TODO: Checking consistency of input data (what to do if error?).
}