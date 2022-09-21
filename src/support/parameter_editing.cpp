
#include "parameter_editing.h"
#include "../model_application.h"

void
recursive_update_parameter(int level, std::vector<Index_T> &current_indexes, const Indexed_Parameter &par_data, Model_Data *data, Parameter_Value val) {
	//TODO: This is not necessarily safe unless current_indexes.size() >= model->modules[0]->index_sets.count();
	
	if(level == par_data.indexes.size() || !is_valid(par_data.indexes[level].index_set)) {
		// Do the actual update.
		s64 offset = data->parameters.structure->get_offset(par_data.id, current_indexes);
		*data->parameters.get_value(offset) = val;
	} else {
		if(par_data.locks[level]) {
			//NOTE: this assumes that the index sets in par_data.indexes are ordered the same as in app->index_counts !
			for(Index_T index = {par_data.indexes[level].index_set, 0}; index < data->app->index_counts[level]; ++index) {
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