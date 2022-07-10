
#ifndef MOBIUS_MODEL_APPLICATION_H
#define MOBIUS_MODEL_APPLICATION_H

#include "model_declaration.h"



template<typename Handle_T> struct Hash_Fun {
	int operator()(const Handle_T&) const;
};

template<> struct Hash_Fun<Entity_Id> {
	int operator()(const Entity_Id& id) const { return 97*id.module_id + id.id; }
};

template<> struct Hash_Fun<Var_Id> {
	int operator()(const Var_Id& id) const { return id.id; }
};

/*
struct Index_T {
	Entity_Id index_set_id;
	s32       index;
}
*/

template<typename Handle_T>
struct Multi_Array_Structure {
	//std::vector<Entity_Id> index_sets;
	std::vector<Handle_T>  handles;
	
	std::unordered_map<Handle_T, s32, Hash_Fun<Handle_T>> handle_location;
	s64 begin_offset;
	
	// TODO: indexing!
	s64 get_offset(Handle_T handle) { //, Index_T *index_counts) {
		/*
		//TODO: checking should not be on by default (?) Need to optimize.
		auto find = handle_location.find(handle);
		if(find == handle_location.end()) fatal_error(Mobius_Error::internal, "Didn't find handle in Multi_Array_Structure.");
		*/
		//warning_print("local loc: ",begin_offset + handle_location[handle], "\n");
		return begin_offset + handle_location[handle];
	}
	
	Math_Expr_FT *get_offset_code(Math_Block_FT *scope, Handle_T handle) {
		return make_literal(scope, begin_offset + handle_location[handle]);
	}
	
	s64 total_count() { return handle_location.size(); } // TODO: multiply with indexes
	
	Multi_Array_Structure(/*std::vector<Entity_Id> &&index_sets,*/ std::vector<Handle_T> &&handles) : /*index_sets(index_sets),*/ handles(handles) {
		this->handles = handles;
		for(int idx = 0; idx < this->handles.size(); ++idx)
			handle_location[this->handles[idx]] = idx;
	}
};

template<typename Val_T, typename Handle_T>
struct Structured_Storage {
	Val_T *data;
	s64    time_steps;
	//TODO: store start_date here too.
	s64    initial_step; //Matching the (local) start date.
	s64    total_count;
	bool   has_been_set_up;
	
	std::unordered_map<Handle_T, s32, Hash_Fun<Handle_T>> handle_is_in_array;
	std::vector<Multi_Array_Structure<Handle_T>> structure;
	
	//TODO: need much more error checking in these!
	
	void set_up(std::vector<Multi_Array_Structure<Handle_T>> &&structure) {
		if(has_been_set_up)
			fatal_error(Mobius_Error::internal, "Tried to set up structure twice.");
		
		this->structure = structure;
		
		s64 offset = 0;
		s32 array_idx = 0;
		for(auto &multi_array : this->structure) {
			multi_array.begin_offset = offset;
			offset += multi_array.total_count();
			for(Handle_T handle : multi_array.handles)
				handle_is_in_array[handle] = array_idx;
			++array_idx;
		}
		total_count = offset;
		
		has_been_set_up = true;
	}
	
	//TODO: there should be a version of this one that checks for out of bounds indexing. But we also want the fast one that doesn't
	Val_T  *get_value(s64 offset_for_val, s64 time_step = 0) {
		s64 step = std::max(time_step + initial_step, (s64)0);
		return data + offset_for_val + step*total_count;
	}
	
	void allocate(s64 time_steps = 1) {
		if(!has_been_set_up)
			fatal_error(Mobius_Error::internal, "Tried to allocate data before structure was set up.");
		if(data) free(data);
		this->time_steps = time_steps;// + initial_step;
		s64 alloc_size = sizeof(Val_T) * total_count * (time_steps + initial_step);
		data = (Val_T *) malloc(alloc_size);
		memset(data, 0, alloc_size);
	}; //TODO: also start_date
	
	s64 get_offset(Handle_T handle) { // TODO: indexes
		/*
		auto find = handle_is_in_array.find(handle);           //TODO: checking shoud not be here by default. Need to optimize
		if(find == handle_is_in_array.end())
			fatal_error(Mobius_Error::internal, "Handle was not registered in structure.");
		*/
		s32 array_idx = handle_is_in_array[handle];
		return structure[array_idx].get_offset(handle);
	}
	
	Math_Expr_FT *get_offset_code(Math_Block_FT *scope, Handle_T handle) {
		s32 array_idx = handle_is_in_array[handle];
		return structure[array_idx].get_offset_code(scope, handle);
	}
	
	Structured_Storage(s64 initial_step) : initial_step(initial_step), has_been_set_up(false), data(nullptr) {}
	
};


struct
Run_Batch {
	std::vector<Var_Id> state_vars;
	
	Math_Expr_FT *run_code;
	//TODO: also function pointer to llvm compiled code.
};

struct
Model_Application {
	Mobius_Model *model;
	
	Model_Application(Mobius_Model *model) : model(model), parameter_data(0), series_data(0), result_data(1), is_compiled(false) {
		if(!model->is_composed)
			fatal_error(Mobius_Error::internal, "Tried to create a model application before the model was composed.");
	}
	
	Structured_Storage<Parameter_Value, Entity_Id>  parameter_data;
	Structured_Storage<double, Var_Id>              series_data;
	Structured_Storage<double, Var_Id>              result_data;
	
	void set_up_parameter_structure();
	void set_up_series_structure();
	void set_up_result_structure();
	
	bool is_compiled;
	void compile();
	
	Run_Batch batch;
	Run_Batch initial_batch;
};


#endif // MOBIUS_MODEL_APPLICATION_H