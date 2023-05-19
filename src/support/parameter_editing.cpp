
#include "parameter_editing.h"
#include "../model_application.h"

// TODO: How do we handle locked index sets if there is a mat_col for that index set?

void
recursive_update_parameter(int level, std::vector<Index_T> &current_indexes, const Indexed_Parameter &par_data, Model_Data *data, Parameter_Value val) {
	//TODO: This is not necessarily safe unless current_indexes.size() >= model->modules[0]->index_sets.count();
	
	if(level == par_data.indexes.size() || !is_valid(par_data.indexes[level].index_set)) {
		// Do the actual update.
		s64 offset;
		if(is_valid(par_data.mat_col))
			offset = data->parameters.structure->get_offset(par_data.id, current_indexes, par_data.mat_col);
		else
			offset = data->parameters.structure->get_offset(par_data.id, current_indexes);
		*data->parameters.get_value(offset) = val;
	} else {
		if(par_data.locks[level]) {
			//NOTE: this assumes that the index sets in par_data.indexes are ordered the same as in app->index_counts !
			auto index_set = Entity_Id {Reg_Type::index_set, (s16)level};
			auto index_count = data->app->get_index_count(index_set, current_indexes); // NOTE: this will work if things are set up in the right order, but it is a bit volatile
			for(Index_T index = {index_set, 0}; index < index_count; ++index) {
			//for(Index_T index = {index_set, 0}; index < data->app->get_max_index_count(index_set); ++index) {
				current_indexes[level] = index;
				recursive_update_parameter(level+1, current_indexes, par_data, data, val);
			}
		} else {
			current_indexes[level] = par_data.indexes[level];
			recursive_update_parameter(level+1, current_indexes, par_data, data, val);
		}
	}
}

void
set_parameter_value(const Indexed_Parameter &par_data, Model_Data *data, Parameter_Value val) {
	if(!is_valid(par_data.id))
		fatal_error(Mobius_Error::internal, "Tried to write an invalid parameter to the data set.");
	
	std::vector<Index_T> current_indexes(par_data.indexes.size());
	
	recursive_update_parameter(0, current_indexes, par_data, data, val);
}

bool
parameter_is_subset_of(const Indexed_Parameter &par, const Indexed_Parameter &compare_to) {
	if(par.id != compare_to.id) return false;
	for(int idx = 0; idx < par.indexes.size(); ++idx) {
		if(compare_to.locks[idx]) continue;
		if(par.locks[idx]) return false;
		if(compare_to.indexes[idx] != par.indexes[idx]) return false;
	}
	if(compare_to.mat_col != par.mat_col) return false;
	return true;
}