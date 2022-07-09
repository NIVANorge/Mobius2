
#ifndef MOBIUS_DATA_SET_H
#define MOBIUS_DATA_SET_H

#include "module_declaration.h"

#include <unordered_map>

/*
struct Index_T {
	Entity_Id index_set_id;
	s32       index;
}
*/

template<typename Val_T, Handle_T>
struct Multi_Array_Structure {
	//std::vector<Entity_Id> index_sets;
	std::vector<Handle_T>  handles;
	
	std::unordered_map<Handle_T, s64> handle_location;
	s64 begin_offset;
	
	// TODO: indexing!
	s64 get_offset(Handle_T handle) { //, Index_T *index_counts) {
		return begin_offset + handle_location[handle]
	}
	
	s64 total_count() { return handle_location.size(); } // TODO: multiply with indexes
	
	Multi_Array_Structure(/*std::vector<Entity_Id> &&index_sets,*/ std::vector<Handle_T> &&handles) : index_sets(index_sets), handles(handles) {
		for(int idx = 0; idx < handles.size(); ++idx)
			handle_location[handles[idx]] = idx;
	}
};

template<typename Val_T, Handle_T>
struct Structured_Storage {
	Val_T *data;
	s64    time_steps;
	//TODO: store start_date here too.
	s64    initial_step; //Matching the (local) start date.
	s64    total_count;
	bool   has_been_set_up;
	
	std::unordered_map<Handle_T, s32> handle_is_in_array;
	std::vector<Multi_Array_Structure<Handle_T>> structure;
	
	//TODO: need much more error checking in these!
	
	void setup(std::vector<Multi_Array_Structure> &&structure) {
		this->structure = structure;
		
		s64 offset = 0;
		s32 array_idx = 0;
		for(auto &multi_array : structure) {
			multi_array.begin_offset = offset;
			offset += multi_array.total_count();
			for(Handle_T handle : multi_array.handles)
				handle_is_in_array[handle] = array_idx;
			++array_idx;
		}
		total_count = offset;
	}
	
	Val_T  *get_value(s64 offset_for_val, s64 time_step) {
		s64 step = std::max(time_step + initial_step, 0);
		return data + offset_for_val + time_steps*total_count;
	}
	
	void allocate(s64 time_steps) {
		if(data) free(data);
		this->time_steps = time_steps + initial_step;
		s64 alloc_size = sizeof(Val_T) * total_count * this->time_steps;
		data = (Val_T *) malloc(alloc_size);
		memset(data, 0, alloc_size);
	}; //TODO: also start_date
	
	s64 get_offset(Handle_T handle) { // TODO: indexes
		s32 array_idx = handle_is_in_array[handle]
		return 
	}
	//TODO: also need to be able to generate function trees that compute these indexes!
	
	Structured_Storage(s64 initial_step) : initial_step(initial_step), has_been_set_up(false), data(nullptr) {}
	
};


struct Model_Application {
	Structured_Storage<Parameter_Value, Entity_Id> parameter_data;
	Structured_Storage<double, Var_Id>             series_data;
	Structured_Storage<double, Var_Id>             result_data;
	
	Model_Application() : parameter_data(0), series_data(0), result_data(1) {}
};


#endif // MOBIUS_DATA_SET_H