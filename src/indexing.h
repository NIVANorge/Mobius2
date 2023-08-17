
// NOTE: This should only be included inside model_application.h . It does not have meaning separately from it.


template<typename Handle_T> void
Multi_Array_Structure<Handle_T>::check_index_bounds(Model_Application *app, Handle_T handle, Entity_Id index_set, Index_T index) {
	//TODO: This makes sure we are not out of bounds of the data, but it could still be
	//incorrect for sub-indexed things.
	if(index_set != index.index_set) {
		begin_error(Mobius_Error::internal);
		error_print("Mis-indexing the index set ", app->model->index_sets[index_set]->name, ", in one of the get_offset functions while looking up ", get_handle_name(app, handle), "\n");
		error_print("The index used to address the index set is ");
		if(is_valid(index.index_set))
			error_print(app->model->index_sets[index.index_set]);
		else
			error_print("invalid");
		mobius_error_exit();
	}
	if(index.index < 0 || index.index >= app->get_max_index_count(index_set).index)
		fatal_error(Mobius_Error::internal, "Index out of bounds for the index set ", app->model->index_sets[index_set]->name, " in one of the get_offset functions while looking up ", get_handle_name(app, handle));
}

#if INDEX_PACKING_ALTERNATIVE

// TODO: This packing alternative is not up to date with code changes.

template<typename Handle_T> s64
Multi_Array_Structure<Handle_T>::get_stride(Handle_T handle) {
	return (s64)handles.size();
}

template<typename Handle_T> s64
Multi_Array_Structure<Handle_T>::get_offset(Handle_T handle, std::vector<Index_T> &indexes, Model_Application *app) {
	s64 offset = 0;
	for(auto &index_set : index_sets) {
		auto &index = indexes[index_set.id];
		check_index_bounds(app, handle, index_set, index);
		offset *= (s64)app->get_max_index_count(index_set).index;
		offset += (s64)index.index;
	}
	return (s64)offset*handles.size() + get_offset_base(handle, app);
}

template<typename Handle_T> s64
Multi_Array_Structure<Handle_T>::get_offset(Handle_T handle, std::vector<Index_T> &indexes, Index_T mat_col, Model_Application *app) {
	// For if one of the index sets appears doubly, the 'mat_col' is the index of the second occurrence of that index set
	s64 offset = 0;
	bool once = false;
	for(auto &index_set : index_sets) {
		auto &index = indexes[index_set.id];
		check_index_bounds(app, handle, index_set, index);
		offset *= (s64)app->get_max_index_count(index_set).index;
		s64 index = (s64)index.index;
		if(index_set == mat_col.index_set) {
			if(once)
				index = (s64)mat_col.index;
			once = true;
		}
		offset += index;
	}
	return (s64)offset*handles.size() + get_offset_base(handle, app);
}

template<typename Handle_T> s64
Multi_Array_Structure<Handle_T>::get_offset_alternate(Handle_T handle, std::vector<Index_T> &indexes, Model_Application *app) {
	if(indexes.size() != index_sets.size())
		fatal_error(Mobius_Error::internal, "Got wrong amount of indexes to get_offset_alternate().");
	s64 offset = 0;
	int idx = 0;
	for(auto &index_set : index_sets) {
		auto &index = indexes[idx];
		check_index_bounds(app, handle, index_set, index);
		offset *= (s64)app->get_max_index_count(index_set).index;
		offset += (s64)index.index;
		++idx;
	}
	return (s64)offset*handles.size() + get_offset_base(handle, app);
}

template<typename Handle_T> Math_Expr_FT *
Multi_Array_Structure<Handle_T>::get_offset_code(Handle_T handle, Index_Exprs &indexes, Model_Application *app, Entity_Id &err_idx_set_out) {
	
	Math_Expr_FT *result = nullptr;
	
	int sz = index_sets.size();
	for(int idx = 0; idx < sz; ++idx) {
		auto &index_set = index_sets[idx];
		
		// If the two last index sets are the same, and a matrix column was provided, use that for indexing the second instance.
		bool matrix_column = (idx == sz-1 && sz >= 2 && index_sets[sz-2]==index_sets[sz-1]);
		Math_Expr_FT *index = indexes.get_index(app, index_set, matrix_column);
		
		if(!index) {
			err_idx_set_out = index_set;
			return result;
		}
		
		if(idx == 0)
			result = index;
		else {
			result = make_binop('*', result, make_literal((s64)app->get_max_index_count(index_set).index));
			result = make_binop('+', result, index);
		}
	}
	if(result) {
		result = make_binop('*', result, make_literal((s64)handles.size()));
		result = make_binop('+', result, make_literal((s64)(begin_offset + handle_location[handle])));
	} else // Happens if index_sets is empty.
		result = make_literal((s64)(begin_offset + handle_location[handle]))
	
	return result;
}

template<typename Handle_T> Offset_Stride_Code
Multi_Array_Structure<Handle_T>::get_special_offset_stride_code(Handle_T handle, Index_Exprs &indexes, Model_Application *app) {
	
	// NOTE: This version is not yet tested
	
	Offset_Stride_Code result = {};

	s64 stride = 1;
	bool undetermined_found = false;
	
	int sz = index_sets.size();
	for(int idx = 0; idx < sz; ++idx) {
		auto &index_set = index_sets[idx];
		auto index = indexes.get_index(app, index_set);
		if(index) {
			if(idx == 0)
				result.offset = index;
			else {
				result.offset = make_binop('*', result.offset, make_literal((s64)app->get_max_index_count(index_set).index));
				result.offset = make_binop('+', result.offset, index);
			}
			if(!undetermined_found)
				stride *= app->get_max_index_count(index_set).index;
			
		} else {
			if(idx == 0)
				result.offset = make_literal((s64)0);
			else
				result.offset = make_binop('*', result.offset, make_literal((s64)app->get_max_index_count(index_set).index));
				// treat the index as 0.
			if(undetermined_found) {
				error_print(Mobius_Error::internal, "Got more than one indetermined index in get_offset_code(). The indetermined index sets were:\n");
				for(auto idx_set : index_sets) {
					if(!indexes.get_index(app, idx_set))
						error_print(app->model->index_sets[idx_set]->name, "\n");
				}
				mobius_error_exit();
			}
			undetermined_found = true;
			
			result.count = app->get_index_count_code(index_set, index_exprs);
		}
	}
	if(!result.count)
		result.count  = make_literal((s64)1);
	result.stride = make_literal(stride*(s64)handles.size());
	if(result.offset) {
		result.offset = make_binop('*', result.offset, make_literal((s64)handles.size()));
		result.offset = make_binop('+', result.offset, make_literal((s64)(begin_offset + handle_location[handle])));
	} else // Happens if index_sets is empty.
		result = make_literal((s64)(begin_offset + handle_location[handle]))

	return result;
}

#else // INDEX_PACKING_ALTERNATIVE
	
// Theoretically this version of the code is more vectorizable, but we should probably also align the memory and explicitly add vectorization passes in llvm
//  (not seeing much of a difference in run speed at the moment)

template<typename Handle_T> s64
Multi_Array_Structure<Handle_T>::get_stride(Handle_T handle) {
	return 1;
}

template<typename Handle_T> s64
Multi_Array_Structure<Handle_T>::get_offset(Handle_T handle, Indexes &indexes, Model_Application *app) {
	
	s64 offset = handle_location[handle];
	
	if(indexes.lookup_ordered) {
		
		if(indexes.indexes.size() != index_sets.size())
			fatal_error(Mobius_Error::internal, "Got wrong amount of indexes to get_offset() (loookup_ordered = true).");
		
		int idx = 0;
		for(auto &index_set : index_sets) {
			auto index = indexes.indexes[idx];
			check_index_bounds(app, handle, index_set, index);
			offset *= (s64)app->get_max_index_count(index_set).index;
			offset += (s64)index.index;
			++idx;
		}
		return offset + begin_offset;
	} else {

		bool once = false;
		for(auto &index_set : index_sets) {
			auto index = indexes.indexes[index_set.id];
			if(index_set == indexes.mat_col.index_set) {
				if(once)
					index = indexes.mat_col;
				once = true;
			}
			check_index_bounds(app, handle, index_set, index);
			offset *= (s64)app->get_max_index_count(index_set).index;
			offset += (s64)index.index;
		}
		return offset + begin_offset;
	}
}

template<typename Handle_T> Math_Expr_FT *
Multi_Array_Structure<Handle_T>::get_offset_code(Handle_T handle, Index_Exprs &indexes, Model_Application *app, Entity_Id &err_idx_set_out) {
	
	Math_Expr_FT *result = make_literal((s64)handle_location[handle]);
	int sz = index_sets.size();
	for(int idx = 0; idx < index_sets.size(); ++idx) {
		auto &index_set = index_sets[idx];
		
		// If the two last index sets are the same, and a matrix column was provided, use that for indexing the second instance.
		bool matrix_column = (idx == sz-1 && sz >= 2 && index_sets[sz-2]==index_sets[sz-1]);
		Math_Expr_FT *index = indexes.get_index(app, index_set, matrix_column);
		
		if(!index) {
			err_idx_set_out = index_set;
			return nullptr;
		}
		
		result = make_binop('*', result, make_literal((s64)app->get_max_index_count(index_set).index));
		result = make_binop('+', result, index);
	}
	result = make_binop('+', result, make_literal((s64)begin_offset));
	return result;
}

template<typename Handle_T> Offset_Stride_Code
Multi_Array_Structure<Handle_T>::get_special_offset_stride_code(Handle_T handle, Index_Exprs &indexes, Model_Application *app) {
	
	Offset_Stride_Code result = {};
	
	result.offset = make_literal((s64)handle_location[handle]);

	s64 stride = 1;
	bool undetermined_found = false;
	
	int sz = index_sets.size();
	for(int idx = 0; idx < sz; ++idx) {
		auto &index_set = index_sets[idx];
		
		result.offset = make_binop('*', result.offset, make_literal((s64)app->get_max_index_count(index_set).index));
		auto index = indexes.get_index(app, index_set);
		if(index) {
			result.offset = make_binop('+', result.offset, index);
			
			if(!undetermined_found)
				stride *= app->get_max_index_count(index_set).index;
			
		} else {
			// treat the index as 0.
			
			if(undetermined_found) {
				begin_error(Mobius_Error::internal);
				error_print("Got more than one indetermined index in get_special_offset_stride_code(). The indetermined index sets were:\n");
				for(auto idx_set : index_sets) {
					if(!indexes.get_index(app, idx_set))
						error_print(app->model->index_sets[idx_set]->name, "\n");
				}
				mobius_error_exit();
			}
			undetermined_found = true;
			
			result.count = app->get_index_count_code(index_set, indexes);
		}
	}
	if(!result.count)
		result.count  = make_literal((s64)1);
	result.offset = make_binop('+', result.offset, make_literal((s64)begin_offset));
	result.stride = make_literal(stride);

	return result;
}


#endif