
#include "model_application.h"
#include "data_set.h"

#include <algorithm>

void
fill_constant_range(Model_Application *app, Date_Time d0, Date_Time d1, double y, std::vector<s64> &write_offsets, Data_Storage<double, Var_Id> *data) {
	s64 first = steps_between(data->start_date, d0, app->time_step_size);
	s64 last  = steps_between(data->start_date, d1, app->time_step_size);
	first = std::max(first, (s64)0);
	last  = std::min(last, data->time_steps-1);
	for(s64 ts = first; ts <= last; ++ts) {
		for(s64 offset : write_offsets)
			*data->get_value(offset, ts) = y;
	}
}

void
interpolate(Model_Application *app, std::vector<Date_Time> &dates, 
	std::vector<double> &vals, std::vector<s64> &write_offsets, Series_Data_Flags &flags, Date_Time end_date, Data_Storage<double, Var_Id> *data) {
	
	std::vector<Date_Time> x_vals;
	std::vector<double>    y_vals;
	std::vector<int>       order;
	x_vals.reserve(dates.size());
	y_vals.reserve(dates.size());
	order.reserve(dates.size());
	
	int valid = 0;
	// NOTE: We can't rule out dates that fall outside the date range here already, because we
	// may use partially overlapping intervals for the interpolation.
	for(int row = 0; row < dates.size(); ++row) {
		if(std::isfinite(vals[row])) {
			x_vals.push_back(dates[row]);
			y_vals.push_back(vals[row]);
			order.push_back(valid++);
		}
	}
	
	std::sort(order.begin(), order.end(), [&x_vals](int row_a, int row_b) -> bool { return x_vals[row_a] < x_vals[row_b]; });
	
	if(flags & series_data_interp_step) {
		for(int row = 0; row < (int)x_vals.size()-1; ++row) {
			int at   = order[row];
			int atp1 = order[row + 1];
			if(x_vals[at] < data->start_date && x_vals[atp1] < data->start_date) continue;
			fill_constant_range(app, x_vals[at], x_vals[atp1], y_vals[at], write_offsets, data);
		}
	} else if(flags & series_data_interp_linear || ((flags & series_data_interp_spline) && x_vals.size() <= 2) ) {
		for(int row = 0; row < x_vals.size()-1; ++row) {
			int at   = order[row];
			int atp1 = order[row + 1];
			
			Date_Time first = x_vals[at];
			Date_Time last  = x_vals[atp1];
			double    y0    = y_vals[at];
			double    y1    = y_vals[atp1];
			
			if(first < data->start_date && last < data->start_date) continue;
			
			Expanded_Date_Time date(first, app->time_step_size);
			double x_range = (double)(last.seconds_since_epoch - first.seconds_since_epoch);
				
			s64 step      = steps_between(data->start_date, first, app->time_step_size);
			s64 last_step = steps_between(data->start_date, last,  app->time_step_size);
			
			date.step = step;
				
			while(date.step <= last_step) {
				if(date.step >= 0) {
					if(date.step >= data->time_steps) break; // TODO: It could break out of the outer loop too.
					double t = (double)(date.date_time.seconds_since_epoch - first.seconds_since_epoch) / x_range;
					double y = t*y1 + (1.0 - t)*y0;
					for(s64 offset : write_offsets)
						*data->get_value(offset, date.step) = y;
				}
				date.advance();
			}
		}
	} else if(flags & series_data_interp_spline) {
		//TODO: This is a straight-forward implementation of the math, but we don't know how numerically stable it is?
		
		//TODO: The way we fill end points (series_data_interp_inside) is not good now. It should either be constant or more specific. May also want to force option to have first derivatives in the end points be 0.
		
		//TODO: We should allow the user to specify that the value should never be negative (or just have this as a default?)
		
		int n_pt  = (int)x_vals.size();
		
		std::vector<double> b_col(n_pt);
		std::vector<double> k_col(n_pt);
		std::vector<double> diag(n_pt);
		std::vector<double> off_diag(n_pt-1);
		
		/* Set up the tridiagonal symmetric linear system  A*k = b ( see https://en.wikipedia.org/wiki/Spline_interpolation )
			The vector of ks is the first derivatives of the polynomials at each control point, which is what we solve the system to obtain.
		
		|  diag[0]    off_diag[0]   0            0          ...  0             0        | | k_col[0]   |     | b_col[0]   |
		| off_diag[0]   diag[1]    off_diag[1]   0          ...  0             0        | | k_col[1]   |     | b_col[1]   |
		|  0          off_diag[1]   diag[2]     off_diag[2] ...  0             0        | | k_col[2]   |     | b_col[2]   |
		|                        .....                                                  | |  ...       |  =  |  ...       |
		|  0           0           0             0         ... off_diag[N-2]  diag[N-1] | | k_col[N-1] |     | b_col[N-1] |
		
		*/
		
		for(int row = 0; row < n_pt; ++row) {
			int ap1, am1;
			int at                = order[row];
			if(row != 0) am1      = order[row-1];
			if(row != n_pt-1) ap1 = order[row+1];
			
			double dx0 = 0.0;
			double dx1 = 0.0;
			double dy0 = 0.0;
			double dy1 = 0.0;
			if(row != 0) {
				dx0 = 1.0 / ((double)(x_vals[at].seconds_since_epoch - x_vals[am1].seconds_since_epoch));
				dy0 = y_vals[at] - y_vals[am1];
			}
			if(row != n_pt-1) {
				dx1 = 1.0 / ((double)(x_vals[ap1].seconds_since_epoch - x_vals[at].seconds_since_epoch));
				dy1 = y_vals[ap1] - y_vals[at];
				off_diag[row] = dx1;
			}
			diag[row]  = 2.0 * (dx0 + dx1);
			b_col[row] = 3.0 * (dy0*dx0*dx0 + dy1*dx1*dx1);
		}
		
		// Make the system upper triangular by subtracting a multiple of row i-1 from row i (for each 1 <= i < NPt).
		for(int row = 1; row < n_pt; ++row) {
			double coeff = off_diag[row-1]/diag[row-1];
			diag[row]  = diag[row]  - off_diag[row-1]*coeff;
			b_col[row] = b_col[row] - b_col[row-1]*coeff;
		}
		
		// Back-solve the upper triangular system to obtain the Ks
		k_col[n_pt-1] = b_col[n_pt-1]/diag[n_pt-1];
		for(int row = n_pt-2; row >= 0; --row) {
			k_col[row] = (b_col[row] - off_diag[row]*k_col[row+1]) / diag[row];
		}
		
		for(int row = 0; row < n_pt-1; ++row) {
			
			int at   = order[row];
			int atp1 = order[row+1];
			
			if(x_vals[at] < data->start_date && x_vals[atp1] < data->start_date) continue;
			
			Expanded_Date_Time date(x_vals[at], app->time_step_size);
		
			double x_range = (double)(x_vals[atp1].seconds_since_epoch - x_vals[at].seconds_since_epoch);
			double y_range = y_vals[atp1] - y_vals[at];
			double a =  k_col[row]*x_range - y_range;
			double b = -k_col[row+1]*x_range + y_range;
			
			s64 step      = steps_between(data->start_date, x_vals[at],   app->time_step_size);
			s64 last_step = steps_between(data->start_date, x_vals[atp1], app->time_step_size);
			
			date.step = step;
			
			while(date.step <= last_step) {
				
				if(date.step >= 0) {
					if(date.step >= data->time_steps) break; // TODO: It could break out of the outer loop too!
					
					double t   = (double)(date.date_time.seconds_since_epoch - x_vals[at].seconds_since_epoch) / x_range;			
					double y = (1.0-t)*y_vals[at] + t*y_vals[atp1] + t*(1.0-t)*( (1.0-t)*a + t*b );
					
					for(s64 offset : write_offsets)
						*data->get_value(offset, date.step) = y;
				}
				date.advance();
			}
		}
	}
	
	// If there is some segment missing at the beginning and end (and the "inside" flag is not set), fill them in with a constant equal to the first/last value.
	if(!(flags & series_data_interp_inside)) {
		int first = order[0];
		int last  = order[x_vals.size()-1];
		if(x_vals[first] > data->start_date)
			fill_constant_range(app, data->start_date, x_vals[first], y_vals[first], write_offsets, data);
		if(x_vals[last] < end_date)
			fill_constant_range(app, x_vals[last], end_date, y_vals[last], write_offsets, data);
	}
}

struct
Spatial_Interp_Spec {
	Series_Data_Flags flags;
	Entity_Id index_set;
	std::vector<Indexes> indexes;
};

void
do_spatial_interpolations(Model_Application *app, std::map<Var_Id, Spatial_Interp_Spec> &spatial_interpolations);

void
process_series(Model_Application *app, Data_Set *data_set, Entity_Id series_data_id, Date_Time end_date) {
	
	auto series_data = data_set->series[series_data_id];
	
	for(auto &series : series_data->series) {
	
		std::vector<std::vector<s64>> offsets;
		offsets.resize(series.header_data.size());
		
		std::vector<Var_Id::Type> series_type;
		series_type.resize(series.header_data.size());
		
		auto model = app->model;
		
		std::map<Var_Id, Spatial_Interp_Spec> spatial_interpolations;
		
		// TODO: Maybe make an overwrite guard. I.e. record what input series have already been provided (which we have to do any way),
		//		then check against that if there is an overwrite.
		
		
		int header_idx = 0;
		for(auto &header : series.header_data) {
			std::set<Var_Id> ids = app->vars.find_by_name(header.name);
			
			// NOTE: Due to preprocessing steps, we should be guaranteed that at this point, for any given name all ids attached to it are of the same type, and at least one id is attached to each name.
			
			auto type = ids.begin()->type;
			series_type[header_idx] = type;
			auto *data = &app->data.get_storage(type);
			
			for(Var_Id id : ids) {
				
				const std::vector<Entity_Id> &expected_index_sets = data->structure->get_index_sets(id);
				
				if(header.indexes.empty()) {
					if(!expected_index_sets.empty()) {
						header.source_loc.print_error_header();
						//TODO: need better error diagnostics here, because the number of index sets expected could have come from another data block or file.
						fatal_error("Expected ", expected_index_sets.size(), " indexes for series \"", header.name, "\".");
					}
				}
				
				Indexes indexes_int;
				if(header.indexes.empty()) {
					s64 offset = data->structure->get_offset(id, indexes_int);
					offsets[header_idx].push_back(offset);
					continue;
				}
				
				for(auto &indexes : header.indexes) {
					int index_idx = 0;
					
					if(indexes.indexes.size() != expected_index_sets.size()) {
						header.source_loc.print_error_header();
						fatal_error("Got wrong number of index sets for input series. Expected ", expected_index_sets.size(), ", got ", indexes.indexes.size(), ".");
					}
					
					Entity_Id interp_index_set = invalid_entity_id;
					Series_Data_Flags interp_flags = series_data_none;
					
					for(auto &index : indexes.indexes) {
						auto idx_set = data_set->index_sets[index.index_set];
						Entity_Id index_set = model->top_scope.deserialize(idx_set->name, Reg_Type::index_set);
						Entity_Id expected = expected_index_sets[index_idx];
						
						auto use_index = Index_T { index_set, index.index };
						
						if(index_set != expected) {
							auto expected_set = model->index_sets[expected];
							if(std::find(expected_set->union_of.begin(), expected_set->union_of.end(), index_set) != expected_set->union_of.end()) {
								// Remap to a higher union index set if that is what we should have indexed over.
								use_index = app->index_data.raise(use_index, expected);
							} else {
								header.source_loc.print_error_header();
								//TODO: need better error diagnostics here, because the index sets expected could have come from another data block or file.
								fatal_error("Expected \"", model->index_sets[expected]->name, " to be index set number ", index_idx+1, " for input series \"", header.name, "\".");
							}
						}
						// TODO: Does this break if we have multiple index tuples given for a series (as is possible in csv?)
						// probably need to do indexes_int.indexes.clear() at end of each loop or something?
						indexes_int.add_index(use_index);
						
						for(auto &pair : series.spatial_flags) {
							if(pair.first == index.index_set) {
								if(is_valid(interp_index_set)) {
									header.source_loc.print_error_header();
									fatal_error("Spatial interpolation flags can for now only be provided on one index set per series.");
								}
								interp_index_set = use_index.index_set;
								interp_flags = pair.second;
								break;
							}
						}
						
						++index_idx;
					}
					
					s64 offset = data->structure->get_offset(id, indexes_int);
					offsets[header_idx].push_back(offset);
					
					if(is_valid(interp_index_set) && interp_flags != series_data_none) {
						auto &sp = spatial_interpolations[id];
						sp.flags = interp_flags;
						sp.index_set = interp_index_set;
						sp.indexes.push_back(indexes_int);
					}
				}
			}
			++header_idx;
		}
		
		// NOTE: Start date is set up to be the same for series and additional series.
		s64 first_step = steps_between(app->data.series.start_date, series.start_date, app->time_step_size);
		
		int nrows = series.time_steps;
		if(series.has_date_vector)
			nrows = series.dates.size();
		
		int ncols = offsets.size();
		
		if(ncols != series.raw_values.size())
			fatal_error(Mobius_Error::internal, "Wrong number of rows for series data block.");
		for(auto &col : series.raw_values)
			if(nrows != col.size())
				fatal_error(Mobius_Error::internal, "Wrong number of values for series data block.");
		
		for(int col = 0; col < ncols; ++col) {
			auto &header = series.header_data[col];
			
			Data_Storage<double, Var_Id> *data = series_type[col]==Var_Id::Type::series ? &app->data.series : &app->data.additional_series;
			
			if(    (header.flags & series_data_interp_step)
				|| (header.flags & series_data_interp_linear)
				|| (header.flags & series_data_interp_spline)) {
				
				if(!series.has_date_vector) {
					header.source_loc.print_error_header();
					fatal_error("Interpolation is only available when a date is provided per row of data.");
				}
				interpolate(app, series.dates, series.raw_values[col], offsets[col], header.flags, end_date, data);
			} else {
				
				// Write the data in directly.
				for(s64 row = 0; row < nrows; ++row) {
					s64 ts = first_step + row;
					
					if(series.has_date_vector)
						ts = steps_between(data->start_date, series.dates[row], app->time_step_size);
					
					if(ts < 0) continue;
					if(ts >= data->time_steps) break;
					
					double val = series.raw_values[col][row];
					for(s64 offset : offsets[col])
						*data->get_value(offset, ts) = val;
				}
			}
			
			// TODO: This processing should somehow happen before the interpolation, otherwise it
			// is not nice in the year boundary. But then it must operate on the provided data rather than the processed data,
			// and that is a bit tricky...
			
			if(header.flags & series_data_repeat_yearly) {
				s32 y, m, d, h, mt, s;
				
				if(series.start_date < data->start_date) {
					header.source_loc.print_error_header();
					fatal_error("A 'repeat_yearly' can only be used when the series starts at or after the 'series_interval' start (when a 'series_interval' is provided).");
				}
				
				Date_Time behind = series.start_date;
				
				behind.year_month_day(&y, &m, &d);
				behind.hour_minute_second(&h, &mt, &s);
				
				Date_Time ahead(y+1, m, d);
				ahead.add_timestamp(h, mt, s);
				s64 first_new = steps_between(data->start_date, ahead, app->time_step_size);
				s64 nrows     = steps_between(ahead, end_date, app->time_step_size);
				if(nrows < 0) continue;
				
				Expanded_Date_Time iter(behind, app->time_step_size);
				for(s64 row = 0; row < nrows; ++row) {
					s64 lookup_ts = first_step + iter.step;
					s64 ts = first_new + row;
					
					for(s64 offset : offsets[col]) {
						double val = *data->get_value(offset, lookup_ts);
						*data->get_value(offset, ts) = val;
					}
					iter.advance();
					if(iter.year != y)
						iter = Expanded_Date_Time(behind, app->time_step_size);
				}
			}
		}
		
		do_spatial_interpolations(app, spatial_interpolations);
	}
}


void
do_spatial_interpolations(Model_Application *app, std::map<Var_Id, Spatial_Interp_Spec> &spatial_interpolations) {
	
	// TODO: This does not quite work. We need also to pass the state of the other index sets and group by the values of those.
	
	
	if(spatial_interpolations.empty())
		return;
	
	for(auto &pair : spatial_interpolations) {
		
		auto id = pair.first;
		auto flags = pair.second.flags;
		auto index_set = pair.second.index_set;
		auto index_tuples = pair.second.indexes;
		
		// TODO: We need to group the index tuples on the non-varying indexes.
		// Current implementation assumes they are all the same!!!!!
		int varying_pos = 0;
		for(auto &idx : index_tuples[0].indexes) {
			if(idx.index_set == index_set)
				break;
			varying_pos++;
		}
		if(varying_pos < 0 || varying_pos >= index_tuples[0].indexes.size()) {
			fatal_error(Mobius_Error::internal, "Something went wrong with spatial interpolation");
		}
		std::vector<Index_T> varying_indexes(index_tuples.size());
		int i = 0;
		for(auto &t : index_tuples) {
			varying_indexes[i++] = t.indexes[varying_pos];
		}
		
		std::sort(varying_indexes.begin(), varying_indexes.end());
		
		if(flags == series_data_none)
			continue;
		if(flags != series_data_interp_linear) {
			// TODO: We need access to a Source_Loc here for the error reporting.
			fatal_error("For now we only support linear_interpolation for spatial interpolation");
		}
		
		// TODO: Ideally we could reuse some code between temporal and spatial interpolation
		
		auto &data = app->data.get_storage(id.type);
		
		Indexes indexes_a = index_tuples[0];
		Indexes indexes_b = index_tuples[0];
		Indexes indexes_t = index_tuples[0];
		
		// TODO: This does interp inside only. Harmonize functionality wrt. outside extrapolation and how this is specified in flags.
		for(int i = 0; i < (int)varying_indexes.size()-1; ++i) {
			
			Index_T idx_a = varying_indexes[i];
			Index_T idx_b = varying_indexes[i+1];
			
			indexes_a.indexes[varying_pos] = idx_a;
			indexes_b.indexes[varying_pos] = idx_b;
			
			s64 offset_a = data.structure->get_offset(id, indexes_a);
			s64 offset_b = data.structure->get_offset(id, indexes_b);
			
			double pos_a = app->index_data.get_position(idx_a);
			double pos_b = app->index_data.get_position(idx_b);
			
			Index_T idx_t = idx_a;
			idx_t.index++;
			for(; idx_t.index < idx_b.index; idx_t.index++) {
				
				indexes_t.indexes[varying_pos] = idx_t;
				s64 offset_t = data.structure->get_offset(id, indexes_t);
				double pos_t = app->index_data.get_position(idx_t);
				
				double tt = (pos_b - pos_t)/(pos_b - pos_a);
				
				// TODO: Should we only interpolate within the start and end date of the passed series header? In that case we need to pass those here.
				for(s64 ts = 0; ts < data.time_steps; ++ts) {
					
					// TODO: Hmm, may be better to reorder loops?
					double val_a = *data.get_value(offset_a, ts);
					double val_b = *data.get_value(offset_b, ts);
					
					*data.get_value(offset_t, ts) = val_a*(1.0 - tt) + val_b*tt;
				}
			}
			
		}
		
	}
}