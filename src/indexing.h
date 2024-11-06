
// NOTE: This should only be included inside model_application.h . It does not have meaning separately from it.


template<typename Handle_T> void
Multi_Array_Structure<Handle_T>::check_index_bounds(Model_Application *app, Handle_T handle, Entity_Id index_set, Index_T index) {
	//TODO: This makes sure we are not out of bounds of the data, but it could still be
	//incorrect for sub-indexed things.
	
	//TODO: Should just use Index_Data::are_in_bounds instead!
	
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
	if(index.index < 0 || index.index >= app->index_data.get_max_count(index_set).index)
		fatal_error(Mobius_Error::internal, "Index out of bounds for the index set ", app->model->index_sets[index_set]->name, " in one of the get_offset functions while looking up ", get_handle_name(app, handle));
}
	
// Theoretically this version of the code is more vectorizable, but we should probably also align the memory and explicitly add vectorization passes in llvm
//  (not seeing much of a difference in run speed at the moment)

template<typename Handle_T> s64
Multi_Array_Structure<Handle_T>::get_stride(Handle_T handle) {
	return 1;
}

template<typename Handle_T> s64
Multi_Array_Structure<Handle_T>::get_offset(Handle_T handle, Indexes &indexes, Model_Application *app) {
	
	s64 offset = handle_location[handle];
	
	//TODO: Refactor this to make better use of the new index data system!
	if(indexes.lookup_ordered) {
		
		if(indexes.indexes.size() != index_sets.size())
			fatal_error(Mobius_Error::internal, "Got wrong amount of indexes to get_offset() (loookup_ordered = true).");
		
		int idx = 0;
		for(auto &index_set : index_sets) {
			auto index = indexes.indexes[idx];
			check_index_bounds(app, handle, index_set, index);
			offset *= (s64)app->index_data.get_max_count(index_set).index;
			offset += (s64)index.index;
			++idx;
		}
		return offset + begin_offset;
	} else {

		for(auto &index_set : index_sets) {
			auto index = indexes.indexes[index_set.id];
			check_index_bounds(app, handle, index_set, index);
			offset *= (s64)app->index_data.get_max_count(index_set).index;
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
		
		Entity_Id actual_index_set;
		Math_Expr_FT *index = indexes.get_index(app, index_set, &actual_index_set);
		
		if(!index) {
			err_idx_set_out = index_set;
			return nullptr;
		}
		
		//NOTE: It was not correct to multiply with count of the union member, because what we are actually indexing is the union itself!!
		// TODO: Clean up this code once we have verified that nothing is broken (get_index doesn't seem to need to pass back the 'actual index set').
		actual_index_set = index_set;
		
		result = make_binop('*', result, make_literal((s64)app->index_data.get_max_count(actual_index_set).index));
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
		
		Entity_Id actual_index_set;
		auto index = indexes.get_index(app, index_set, &actual_index_set);
		
		result.offset = make_binop('*', result.offset, make_literal((s64)app->index_data.get_max_count(actual_index_set).index));
		
		if(index) {
			result.offset = make_binop('+', result.offset, index);
			
			//if(!undetermined_found)
			if(undetermined_found)
				stride *= app->index_data.get_max_count(actual_index_set).index;
			
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
