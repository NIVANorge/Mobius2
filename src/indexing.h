
// NOTE: This should only be included inside model_application.h . It does not have meaning separately from it.


#if INDEX_PACKING_ALTERNATIVE

template<typename Handle_T> s64
Multi_Array_Structure<Handle_T>::get_stride(Handle_T handle) {
	return (s64)handles.size();
}

template<typename Handle_T> s64
Multi_Array_Structure<Handle_T>::get_offset(Handle_T handle, std::vector<Index_T> &indexes, Model_Application *app) {
	s64 offset = 0;
	for(auto &index_set : index_sets) {
		check_index_bounds(app, index_set, indexes[index_set.id]);
		offset *= (s64)app->get_max_index_count(index_set).index;
		offset += (s64)indexes[index_set.id].index;
	}
	return (s64)offset*handles.size() + get_offset_base(handle, app);
}

template<typename Handle_T> s64
Multi_Array_Structure<Handle_T>::get_offset(Handle_T handle, std::vector<Index_T> &indexes, Index_T mat_col, Model_Application *app) {
	// For if one of the index sets appears doubly, the 'mat_col' is the index of the second occurrence of that index set
	s64 offset = 0;
	bool once = false;
	for(auto &index_set : index_sets) {
		check_index_bounds(app, index_set, indexes[index_set.id]);
		offset *= (s64)app->get_max_index_count(index_set).index;
		s64 index = (s64)indexes[index_set.id].index;
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
		check_index_bounds(app, index_set, indexes[idx]);
		auto &index = indexes[idx];
		offset *= (s64)app->get_max_index_count(index_set).index;
		offset += (s64)index.index;
		++idx;
	}
	return (s64)offset*handles.size() + get_offset_base(handle, app);
}

template<typename Handle_T> Math_Expr_FT *
Multi_Array_Structure<Handle_T>::get_offset_code(Handle_T handle, Index_Exprs &index_exprs, Model_Application *app, Entity_Id &err_idx_set_out) {
	
	Math_Expr_FT *result = nullptr;
	
	int sz = index_sets.size();
	for(int idx = 0; idx < sz; ++idx) {
		auto &index_set = index_sets[idx];
		Math_Expr_FT *index = index_exprs.indexes[index_set.id];
		if(!index) {
			err_idx_set_out = index_set;
			return result;
		}
		// If the two last index sets are the same, and a matrix column was provided, use that for indexing the second instance.
		if(index_exprs.mat_col && idx == sz-1 && sz >= 2 && index_sets[sz-2]==index_sets[sz-1])
			index = index_exprs.mat_col;
		
		index = copy(index);
		
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
Multi_Array_Structure<Handle_T>::get_special_offset_stride_code(Handle_T handle, Index_Exprs &index_exprs, Model_Application *app) {
	
	// NOTE: This version is not yet tested
	
	Offset_Stride_Code result = {};

	s64 stride = 1;
	bool undetermined_found = false;
	
	int sz = index_sets.size();
	for(int idx = 0; idx < sz; ++idx) {
		auto &index_set = index_sets[idx];
		auto index = index_exprs.indexes[index_set.id];
		if(index) {
			index = copy(index);
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
					if(!index_exprs.indexes[idx_set.id])
						error_print(app->model->index_sets[idx_set]->name, "\n");
				}
				mobius_error_exit();
			}
			undetermined_found = true;
			
			//count = app->get_max_index_count(index_set).index;
			result.count = get_index_count_code(app, index_set, index_exprs);
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
Multi_Array_Structure<Handle_T>::get_offset(Handle_T handle, std::vector<Index_T> &indexes, Model_Application *app) {
	s64 offset = handle_location[handle];
	for(auto &index_set : index_sets) {
		check_index_bounds(app, index_set, indexes[index_set.id]);
		offset *= (s64)app->get_max_index_count(index_set).index;
		offset += (s64)indexes[index_set.id].index;
	}
	return offset + begin_offset;
}

template<typename Handle_T> s64
Multi_Array_Structure<Handle_T>::get_offset(Handle_T handle, std::vector<Index_T> &indexes, Index_T mat_col, Model_Application *app) {
	// If one of the index sets appears doubly, the 'mat_col' is the index of the second occurrence of that index set
	s64 offset = handle_location[handle];
	bool once = false;
	for(auto &index_set : index_sets) {
		check_index_bounds(app, index_set, indexes[index_set.id]);
		offset *= (s64)app->get_max_index_count(index_set).index;
		s64 index = (s64)indexes[index_set.id].index;
		if(index_set == mat_col.index_set) {
			if(once)
				index = (s64)mat_col.index;
			once = true;
		}
		offset += index;
	}
	return offset + begin_offset;
}

template<typename Handle_T> s64
Multi_Array_Structure<Handle_T>::get_offset_alternate(Handle_T handle, std::vector<Index_T> &indexes, Model_Application *app) {
	if(indexes.size() != index_sets.size())
		fatal_error(Mobius_Error::internal, "Got wrong amount of indexes to get_offset_alternate().");
	s64 offset = handle_location[handle];
	int idx = 0;
	for(auto &index_set : index_sets) {
		check_index_bounds(app, index_set, indexes[idx]);
		auto &index = indexes[idx];
		offset *= (s64)app->get_max_index_count(index_set).index;
		offset += (s64)index.index;
		++idx;
	}
	return offset + begin_offset;
}

template<typename Handle_T> Math_Expr_FT *
Multi_Array_Structure<Handle_T>::get_offset_code(Handle_T handle, Index_Exprs &index_exprs, Model_Application *app, Entity_Id &err_idx_set_out) {
	
	Math_Expr_FT *result = make_literal((s64)handle_location[handle]);
	int sz = index_sets.size();
	for(int idx = 0; idx < index_sets.size(); ++idx) {
		auto &index_set = index_sets[idx];
		Math_Expr_FT *index = index_exprs.indexes[index_set.id];
		if(!index) {
			err_idx_set_out = index_set;
			return nullptr;
		}
		// If the two last index sets are the same, and a matrix column was provided, use that for indexing the second instance.
		if(index_exprs.mat_col && idx == sz-1 && sz >= 2 && index_sets[sz-2]==index_sets[sz-1])
			index = index_exprs.mat_col;
		
		index = copy(index);
		
		result = make_binop('*', result, make_literal((s64)app->get_max_index_count(index_set).index));
		result = make_binop('+', result, index);
	}
	result = make_binop('+', result, make_literal((s64)begin_offset));
	return result;
}

template<typename Handle_T> Offset_Stride_Code
Multi_Array_Structure<Handle_T>::get_special_offset_stride_code(Handle_T handle, Index_Exprs &index_exprs, Model_Application *app) {
	
	auto &indexes = index_exprs.indexes;
	Offset_Stride_Code result = {};
	
	result.offset = make_literal((s64)handle_location[handle]);

	s64 stride = 1;
	bool undetermined_found = false;
	
	int sz = index_sets.size();
	for(int idx = 0; idx < sz; ++idx) {
		auto &index_set = index_sets[idx];
		
		result.offset = make_binop('*', result.offset, make_literal((s64)app->get_max_index_count(index_set).index));
		auto index = index_exprs.indexes[index_set.id];
		if(index) {
			index = copy(index);
			result.offset = make_binop('+', result.offset, index);
			
			if(!undetermined_found)
				stride *= app->get_max_index_count(index_set).index;
			
		} else {
			// treat the index as 0.
			
			if(undetermined_found) {
				begin_error(Mobius_Error::internal);
				error_print("Got more than one indetermined index in get_special_offset_stride_code(). The indetermined index sets were:\n");
				for(auto idx_set : index_sets) {
					if(!index_exprs.indexes[idx_set.id])
						error_print(app->model->index_sets[idx_set]->name, "\n");
				}
				mobius_error_exit();
			}
			undetermined_found = true;
			
			result.count = get_index_count_code(app, index_set, index_exprs);
		}
	}
	if(!result.count)
		result.count  = make_literal((s64)1);
	result.offset = make_binop('+', result.offset, make_literal((s64)begin_offset));
	result.stride = make_literal(stride);

	return result;
}


#endif