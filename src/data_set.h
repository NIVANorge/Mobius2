
#ifndef MOBIUS_DATA_SET_H
#define MOBIUS_DATA_SET_H

#include "module_declaration.h"

#include <unordered_map>

struct Index_T {
	Entity_Id index_set_id;
	s32       index;
}

template<typename Val_T, Handle_T>
struct Multi_Array_Structure {
	std::vector<Entity_Id> index_sets;
	std::vector<Handle_T>  handles;
	
	std::unordered_map<Handle_T, s64> handle_location;
	
	s64 get_relative_offset(Handle_T handle, Index_T *index_counts);
	
	Multi_Array_Structure(std::vector<Entity_Id> &&index_sets, std::vector<Handle_T> &&handles) : index_sets(index_sets), handles(handles) {
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
	s64    time_step_advance;
	bool   has_been_set_up;
	
	std::vector<Multi_Array_Structure<Handle_T>> structure;
	//todo: offset for structure, and locate handle in structure.
	
	void setup(std::vector<Multi_Array_Structure> &&structure) {
		this->structure = structure;
		
		//TODO: compute stuff.
	}
	
	Val_T  *get_value(s64 offset_for_val, s64 time_step) {
		s64 step = std::max(time_step + initial_step, 0);
		return data + offset_for_val + time_steps*
	}
	
	void allocate(s64 time_steps); //TODO: also start_date
	
	s64 get_offset(Handle_T handle, std::vector<Index_T> &indexes);
	
	Structured_Storage(s64 initial_step) : initial_step(initial_step), has_been_set_up(false), data(nullptr) {}
	
};



struct Data_Set {
	Structured_Storage<Parameter_Value, Entity_Id> parameter_data;
	Structured_Storage<double, Var_Id>             series_data;
	Structured_Storage<double, Var_Id>             result_data;
	
	Data_Set() : parameter_data(0), series_data(0), result_data(1) {}
};


#endif // MOBIUS_DATA_SET_H