
#include "model_application.h"
#include "data_set.h"

void
process_series(Model_Application *app, Series_Set_Info *series) {
	std::vector<std::vector<s64>> offsets;
	offsets.resize(series->header_data.size());
	
	auto model = app->model;
	
	// TODO: Maybe make an overwrite guard. I.e. record what input series have already been provided (which we have to do any way),
	//		then check agains that if there is an overwrite.
	
	int header_idx = 0;
	for(auto header : series->header_data) {
		std::set<Var_Id> ids = model->series[header.name];
		
		if(ids.empty()) continue; // TODO: additional_time_series.
		
		for(Var_Id id : ids) {
			const std::vector<Entity_Id> &expected_index_sets = app->series_data.get_index_sets(id);
			
			if(header.indexes.empty()) {
				if(!expected_index_sets.empty()) {
					header.location.print_error_header();
					//TODO: need better error diagnostics here, because the number of index sets expected could have come from another data block or file.
					fatal_error("Expected ", expected_index_sets.size(), " indexes for series \"", header.name, "\".");
				}
			}
			
			std::vector<Index_T> indexes_int(expected_index_sets.size());
			if(header.indexes.empty()) {
				s64 offset = app->series_data.get_offset_alternate(id, &indexes_int);
				offsets[header_idx].push_back(offset);
				continue;
			}
			
			for(auto &indexes : header.indexes) {
				int index_idx = 0;
				
				for(auto &index : indexes) {
					Entity_Id index_set = model->modules[0]->index_sets.find_by_name(index.first);
					Entity_Id expected = expected_index_sets[index_idx];
					if(index_set != expected) {
						header.location.print_error_header();
						//TODO: need better error diagnostics here, because the index sets expected could have come from another data block or file.
						fatal_error("Expected \"", model->modules[0]->index_sets[expected]->name, " to be index set number ", index_idx+1, " for input series \"", header.name, "\".");
					}
					indexes_int[index_idx] = {index_set, (s32)index.second};
					++index_idx;
				}
				
				s64 offset = app->series_data.get_offset_alternate(id, &indexes_int);
				offsets[header_idx].push_back(offset);
			}
		}
		++header_idx;
		
		if(header.flags != series_data_none) {
			header.location.print_error_header();
			fatal_error("Series flags not yet implemented.");
		}
	}
	
	s64 first_step = steps_between(app->series_data.start_date, series->start_date, app->timestep_size);
	//if(header.time_steps >= 0)
	//	max_step = first_step + header.time_steps;
	//else
	//	max_step   = steps_between(app->series_data.start_date, header.end_date, app->timestep_size);
	
	s64 nrows = series->time_steps;
	if(series->has_date_vector)
		nrows = series->dates.size();
	int ncolumns = offsets.size();
	
	if(nrows*ncolumns != series->raw_values.size())
		fatal_error(Mobius_Error::internal, "Wrong number of values for series data block.");
	
	int data_idx = 0;
	for(s64 row = 0; row < nrows; ++row) {
		s64 ts = first_step + row;
		if(series->has_date_vector)
			ts = steps_between(app->series_data.start_date, series->dates[row], app->timestep_size);
		
		for(int column = 0; column < offsets.size(); ++column) {
			double val = series->raw_values[data_idx++];
			for(s64 offset : offsets[column])
				*app->series_data.get_value(offset, ts) = val;
		}
	}
}