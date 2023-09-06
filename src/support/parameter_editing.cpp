
#include "parameter_editing.h"

void
recursive_update_parameter(int level, Indexes &current_indexes, const Indexed_Parameter &par_data, Model_Data *data, Parameter_Value val) {
	
	if(level == current_indexes.indexes.size()) {
		// Do the actual update.
		s64	offset = data->parameters.structure->get_offset(par_data.id, current_indexes);
		*data->parameters.get_value(offset) = val;
	} else {
		auto index = par_data.indexes.indexes[level];
		if(is_valid(index) && par_data.locks[level]) {
			auto index_set = Entity_Id {Reg_Type::index_set, (s16)level};
			auto index_count = data->app->index_data.get_index_count(current_indexes, index_set);
			for(Index_T index2 = {index_set, 0}; index2 < index_count; ++index2) {
				current_indexes.indexes[level] = index2;
				recursive_update_parameter(level+1, current_indexes, par_data, data, val);
			}
		} else {
			current_indexes.indexes[level] = index;
			recursive_update_parameter(level+1, current_indexes, par_data, data, val);
		}
	}
}

void
set_parameter_value(const Indexed_Parameter &par_data, Model_Data *data, Parameter_Value val) {
	if(!is_valid(par_data.id))
		fatal_error(Mobius_Error::internal, "Tried to write an invalid parameter to the data set.");
	
	Indexes current_indexes(data->app->model);
	
	recursive_update_parameter(0, current_indexes, par_data, data, val);
}

bool
parameter_is_subset_of(const Indexed_Parameter &par, const Indexed_Parameter &compare_to) {
	if(par.id != compare_to.id) return false;
	for(int idx = 0; idx < par.indexes.indexes.size(); ++idx) {
		if(compare_to.locks[idx]) continue;
		if(par.locks[idx]) return false;
		if(compare_to.indexes.indexes[idx] != par.indexes.indexes[idx]) return false;
	}
	return true;
}