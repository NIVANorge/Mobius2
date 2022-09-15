
#include "model_application.h"
#include "data_set.h"

#include <algorithm>

void
fill_constant_range(Model_Application *app, Date_Time d0, Date_Time d1, double y, std::vector<s64> &write_offsets, Structured_Storage<double, Var_Id> *storage) {
	s64 first = steps_between(storage->start_date, d0, app->time_step_size);
	s64 last  = steps_between(storage->start_date, d1, app->time_step_size);
	for(s64 ts = first; ts <= last; ++ts) {
		for(s64 offset : write_offsets) {
			*storage->get_value(offset, ts) = y;
		}
	}
}

void
interpolate(Model_Application *app, std::vector<Date_Time> &dates, 
	std::vector<double> &vals, std::vector<s64> &write_offsets, Series_Data_Flags &flags, Date_Time end_date, Structured_Storage<double, Var_Id> *storage) {
	
	std::vector<Date_Time> x_vals;
	std::vector<double>    y_vals;
	std::vector<int>       order;
	x_vals.reserve(dates.size());
	y_vals.reserve(dates.size());
	order.reserve(dates.size());
	
	int valid = 0;
	for(int row = 0; row < dates.size(); ++row) {
		if(std::isfinite(vals[row])) {
			x_vals.push_back(dates[row]);
			y_vals.push_back(vals[row]);
			order.push_back(valid++);
		}
	}
	
	std::sort(order.begin(), order.end(), [&x_vals](int row_a, int row_b) -> bool { return x_vals[row_a] < x_vals[row_b]; });
	
	if(flags & series_data_interp_step) {
		for(int row = 0; row < x_vals.size()-1; ++row) {
			int at   = order[row];
			int atp1 = order[row + 1];
			fill_constant_range(app, dates[at], dates[atp1], y_vals[at], write_offsets, storage);
		}
	} else if(flags & series_data_interp_linear || ((flags & series_data_interp_spline) && x_vals.size() <= 2) ) {
		for(int row = 0; row < dates.size()-1; ++row) {
			int at   = order[row];
			int atp1 = order[row + 1];
			
			Date_Time first = x_vals[at];
			Date_Time last  = x_vals[atp1];
			double    y0    = y_vals[at];
			double    y1    = y_vals[atp1];
			
			Expanded_Date_Time date(first, app->time_step_size);
			double x_range = (double)(last.seconds_since_epoch - first.seconds_since_epoch);
				
			s64 step      = steps_between(storage->start_date, first, app->time_step_size);
			s64 last_step = steps_between(storage->start_date, last,  app->time_step_size);
			
			date.step = step;
				
			while(date.step <= last_step) {
				if(date.step >= 0) {
					double t = (double)(date.date_time.seconds_since_epoch - first.seconds_since_epoch) / x_range;
					double y = t*y1 + (1.0 - t)*y0;
					for(s64 offset : write_offsets)
						*storage->get_value(offset, date.step) = y;
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
			Expanded_Date_Time date(x_vals[at], app->time_step_size);
		
			double x_range = (double)(x_vals[atp1].seconds_since_epoch - x_vals[at].seconds_since_epoch);
			double y_range = y_vals[atp1] - y_vals[at];
			double a =  k_col[row]*x_range - y_range;
			double b = -k_col[row+1]*x_range + y_range;
			
			s64 step      = steps_between(storage->start_date, x_vals[at],   app->time_step_size);
			s64 last_step = steps_between(storage->start_date, x_vals[atp1], app->time_step_size);
			
			date.step = step;
			
			while(date.step <= last_step) {
				double t   = (double)(date.date_time.seconds_since_epoch - x_vals[at].seconds_since_epoch) / x_range;			
				double y = (1.0-t)*y_vals[at] + t*y_vals[atp1] + t*(1.0-t)*( (1.0-t)*a + t*b );
				
				for(s64 offset : write_offsets)
					*storage->get_value(offset, date.step) = y;
				
				date.advance();
			}
		}
	}
	
	// If there is some segment missing at the beginning and end (and the "inside" flag is not set), fill them in with a constant equal to the first/last value.
	if(!(flags & series_data_interp_inside)) {
		int first = order[0];
		int last  = order[x_vals.size()-1];
		if(x_vals[first] > storage->start_date)
			fill_constant_range(app, storage->start_date, x_vals[first], y_vals[first], write_offsets, storage);
		if(x_vals[last] < end_date)
			fill_constant_range(app, x_vals[last], end_date, y_vals[last], write_offsets, storage);
	}
}

void
process_series(Model_Application *app, Series_Set_Info *series, Date_Time end_date) {
	std::vector<std::vector<s64>> offsets;
	offsets.resize(series->header_data.size());
	
	std::vector<int> series_type;
	series_type.resize(series->header_data.size());
	
	auto model = app->model;
	
	// TODO: Maybe make an overwrite guard. I.e. record what input series have already been provided (which we have to do any way),
	//		then check agains that if there is an overwrite.
	
	int header_idx = 0;
	for(auto header : series->header_data) {
		std::set<Var_Id> ids = model->series[header.name];
		
		auto *storage = &app->series_data;
		series_type[header_idx] = 1;
		
		if(ids.empty()) {
			ids = app->additional_series[header.name];
			storage = &app->additional_series_data;
			series_type[header_idx] = 2;
		}
		
		for(Var_Id id : ids) {
			
			const std::vector<Entity_Id> &expected_index_sets = storage->get_index_sets(id);
			
			if(header.indexes.empty()) {
				if(!expected_index_sets.empty()) {
					header.loc.print_error_header();
					//TODO: need better error diagnostics here, because the number of index sets expected could have come from another data block or file.
					fatal_error("Expected ", expected_index_sets.size(), " indexes for series \"", header.name, "\".");
				}
			}
			
			std::vector<Index_T> indexes_int(expected_index_sets.size());
			if(header.indexes.empty()) {
				s64 offset = storage->get_offset_alternate(id, indexes_int);
				offsets[header_idx].push_back(offset);
				continue;
			}
			
			for(auto &indexes : header.indexes) {
				int index_idx = 0;
				
				if(indexes.size() != expected_index_sets.size()) {
					header.loc.print_error_header();
					fatal_error("Got wrong number of index sets for input series.");
				}
				
				for(auto &index : indexes) {
					Entity_Id index_set = model->modules[0]->index_sets.find_by_name(index.first);
					Entity_Id expected = expected_index_sets[index_idx];
					if(index_set != expected) {
						header.loc.print_error_header();
						//TODO: need better error diagnostics here, because the index sets expected could have come from another data block or file.
						fatal_error("Expected \"", model->modules[0]->index_sets[expected]->name, " to be index set number ", index_idx+1, " for input series \"", header.name, "\".");
					}
					indexes_int[index_idx] = {index_set, (s32)index.second};
					++index_idx;
				}
				
				s64 offset = storage->get_offset_alternate(id, indexes_int);
				offsets[header_idx].push_back(offset);
			}
		}
		++header_idx;
	}
	
	// NOTE: Start date is set up to be the same for series and additional series.
	s64 first_step = steps_between(app->series_data.start_date, series->start_date, app->time_step_size);
	
	int nrows = series->time_steps;
	if(series->has_date_vector)
		nrows = series->dates.size();
	
	int ncols = offsets.size();
	
	if(ncols != series->raw_values.size())
		fatal_error(Mobius_Error::internal, "Wrong number of rows for series data block.");
	for(auto &col : series->raw_values)
		if(nrows != col.size())
			fatal_error(Mobius_Error::internal, "Wrong number of values for series data block.");
	
	for(int col = 0; col < ncols; ++col) {
		auto &header = series->header_data[col];
		
		Structured_Storage<double, Var_Id> *storage = series_type[col]==1 ? &app->series_data : &app->additional_series_data;
		
		if(    (header.flags & series_data_interp_step) 
			|| (header.flags & series_data_interp_linear)
			|| (header.flags & series_data_interp_spline)) {
			
			if(!series->has_date_vector) {
				header.loc.print_error_header();
				fatal_error("Interpolation is only available when a date is provided per row of data.");
			}
			interpolate(app, series->dates, series->raw_values[col], offsets[col], header.flags, end_date, storage);
		} else {
			for(s64 row = 0; row < nrows; ++row) {
				s64 ts = first_step + row;
				if(series->has_date_vector)
					ts = steps_between(storage->start_date, series->dates[row], app->time_step_size);
				
				double val = series->raw_values[col][row];
				for(s64 offset : offsets[col])
					*storage->get_value(offset, ts) = val;
			}
		}
	}
	
}