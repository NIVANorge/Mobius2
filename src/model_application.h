
#ifndef MOBIUS_MODEL_APPLICATION_H
#define MOBIUS_MODEL_APPLICATION_H

#include "model_declaration.h"
#include "llvm_jit.h"
#include "data_set.h"

#include <functional>


struct Index_T {
	Entity_Id index_set;
	s32       index;
};

struct Neighbor_T {
	Entity_Id neighbor;
	s32       info_id;
};

inline bool operator==(const Neighbor_T &a, const Neighbor_T& b) { return a.neighbor == b.neighbor && a.info_id == b.info_id; }

template<typename Handle_T> struct Hash_Fun {
	int operator()(const Handle_T&) const;
};

template<> struct Hash_Fun<Entity_Id> {
	int operator()(const Entity_Id& id) const { return 97*id.module_id + id.id; }
};

template<> struct Hash_Fun<Var_Id> {
	int operator()(const Var_Id& id) const { return id.id; }
};

template<> struct Hash_Fun<Neighbor_T> {
	int operator()(const Neighbor_T& id) const { return 97*id.neighbor.id + id.info_id; }
};

/*
inline bool
is_valid(Index_T index) {
	return is_valid(index.index_set) && index.index >= 0;
}
*/

template<typename Handle_T>
struct Multi_Array_Structure {
	std::vector<Entity_Id> index_sets;
	std::vector<Handle_T>  handles;
	
	std::unordered_map<Handle_T, s32, Hash_Fun<Handle_T>> handle_location;
	s64 begin_offset;
	
	s64 get_offset_base(Handle_T handle) {
		/*
		//TODO: checking should not be on by default (?) Need to optimize.
		auto find = handle_location.find(handle);
		if(find == handle_location.end()) fatal_error(Mobius_Error::internal, "Didn't find handle in Multi_Array_Structure.");
		*/
		//warning_print("local loc: ",begin_offset + handle_location[handle], "\n");
		return begin_offset + handle_location[handle];
	}
	
	s64 get_offset(Handle_T handle, std::vector<Index_T> *indexes, std::vector<Index_T> *index_counts) {
		s64 offset = 0;
		for(auto &index_set : index_sets) {
			//TODO: check that the indexes and counts are in the right index set (at least with debug flags turned on)
			offset *= (s64)(*index_counts)[index_set.id].index;
			offset += (s64)(*indexes)[index_set.id].index;
		}
		return (s64)offset*handles.size() + get_offset_base(handle);
	}
	
	s64 get_offset_alternate(Handle_T handle, std::vector<Index_T> *indexes, std::vector<Index_T> *index_counts) {
		if(indexes->size() != index_sets.size())
			fatal_error(Mobius_Error::internal, "Got wrong amount of indexes to get_offset_alternate().");
		s64 offset = 0;
		int idx = 0;
		for(auto &index_set : index_sets) {
			auto &index = (*indexes)[idx];
			if(index.index_set != index_set)
				fatal_error(Mobius_Error::internal, "Got mismatching index sets to get_offset_alternate().");
			//TODO: check that the indexes and counts are in the right index set (at least with debug flags turned on)
			offset *= (s64)(*index_counts)[index_set.id].index;
			offset += (s64)index.index;
			++idx;
		}
		return (s64)offset*handles.size() + get_offset_base(handle);
	}
	
	Math_Expr_FT *get_offset_code(Handle_T handle, std::vector<Math_Expr_FT *> *indexes, std::vector<Index_T> *index_counts) {
		Math_Expr_FT *result;
		if(index_sets.empty()) result = make_literal((s64)0);
		for(int idx = 0; idx < index_sets.size(); ++idx) {
			//TODO: check that counts are in the right index set
			auto &index_set = index_sets[idx];
			Math_Expr_FT *index = (*indexes)[index_set.id];
			if(!index)
				return nullptr;
			index = copy(index);
			
			if(idx == 0)
				result = index;
			else {
				result = make_binop('*', result, make_literal((s64)(*index_counts)[index_set.id].index));
				result = make_binop('+', result, index);
			}
		}
		result = make_binop('*', result, make_literal((s64)handles.size()));
		result = make_binop('+', result, make_literal((s64)(begin_offset + handle_location[handle])));
		return result;
	}
	
	s64 instance_count(std::vector<Index_T> *index_counts) {
		s64 count = 1;
		for(auto &index_set : index_sets)
			count *= (s64)(*index_counts)[index_set.id].index;
		return count;
	}
	
	s64 total_count(std::vector<Index_T> *index_counts) { 
		return (s64)handles.size() * instance_count(index_counts);
	}
	
	void finalize() {
		for(int idx = 0; idx < this->handles.size(); ++idx)
			handle_location[this->handles[idx]] = idx;
	}
	
	Multi_Array_Structure(std::vector<Entity_Id> &&index_sets, std::vector<Handle_T> &&handles) : index_sets(index_sets), handles(handles) {
		finalize();
	}
	
	//Multi_Array_Structure() {}
};

struct Model_Application;

template<typename Val_T, typename Handle_T>
struct Structured_Storage {
	Val_T    *data;
	s64       time_steps;
	Date_Time start_date;
	s64       initial_step; //Matching the (local) start date.
	s64       total_count;
	bool      has_been_set_up;
	
	Model_Application *parent;
	
	std::unordered_map<Handle_T, s32, Hash_Fun<Handle_T>> handle_is_in_array;
	std::vector<Multi_Array_Structure<Handle_T>> structure;
	
	//TODO: need much more error checking in these!
	
	void set_up(std::vector<Multi_Array_Structure<Handle_T>> &&structure);
	
	//TODO: there should be a version of this one that checks for out of bounds indexing. But we also want the fast one that doesn't
	Val_T  *
	get_value(s64 offset_for_val, s64 time_step = 0) {
		s64 step = std::max(time_step + initial_step, (s64)0);
		return data + offset_for_val + step*total_count;
	}
	
	void 
	allocate(s64 time_steps = 1) {
		if(!has_been_set_up)
			fatal_error(Mobius_Error::internal, "Tried to allocate data before structure was set up.");
		if(data) free(data);
		this->time_steps = time_steps;
		s64 alloc_size = sizeof(Val_T) * total_count * (time_steps + initial_step);
		data = (Val_T *) malloc(alloc_size);
		memset(data, 0, alloc_size);
	};
	//TODO: also start_date
	
	s64 get_offset_base(Handle_T handle);
	s64 instance_count(Handle_T handle);
	s64 get_offset(Handle_T handle, std::vector<Index_T> *indexes);
	s64 get_offset_alternate(Handle_T handle, std::vector<Index_T> *indexes);
	
	const std::vector<Entity_Id> &
	get_index_sets(Handle_T handle);
	
	Math_Expr_FT *
	get_offset_code(Handle_T handle, std::vector<Math_Expr_FT *> *indexes);
	
	void
	for_each(Handle_T, const std::function<void(std::vector<Index_T> *, s64)>&);
	
	String_View
	get_handle_name(Handle_T handle);
	
	Structured_Storage(s64 initial_step, Model_Application *parent) : initial_step(initial_step), parent(parent), has_been_set_up(false), data(nullptr) {}
};

struct
Run_Batch {
	Solver_Function *solver_fun;
	double           h;
	double           hmin;
	s64              first_ode_offset;
	int              n_ode;
	
	Math_Expr_FT    *run_code;
	batch_function  *compiled_code;
	
	Run_Batch() : run_code(nullptr), solver_fun(nullptr), compiled_code(nullptr) {}
};

struct
Series_Metadata {
	Date_Time start_date;
	Date_Time end_date;
	bool any_data_at_all;
	std::unordered_map<Var_Id, std::vector<Entity_Id>, Hash_Fun<Var_Id>> index_sets;
	Series_Metadata() : any_data_at_all(false) {}
};

struct
Model_Application {
	Mobius_Model *model;
	
	Model_Application(Mobius_Model *model) : model(model), parameter_data(0, this), series_data(0, this), result_data(1, this), neighbor_data(0, this), is_compiled(false),
		data_set(nullptr), timestep_size {} {
		if(!model->is_composed)
			fatal_error(Mobius_Error::internal, "Tried to create a model application before the model was composed.");
		
		auto global = model->modules[0];
		
		index_counts.resize(global->index_sets.count());
		
		for(auto index_set : global->index_sets) {
			index_counts[index_set.id].index_set = index_set;
			index_counts[index_set.id].index = 0;
		}
		
		initialize_llvm();
		llvm_data = create_llvm_module();   //TODO: free it on destruction!
	}
	
	std::vector<Index_T>                            index_counts;
	
	void set_indexes(Entity_Id index_set, Array<String_View> names);
	bool all_indexes_are_set();
	
	void build_from_data_set(Data_Set *data_set);
	
	Time_Step_Size                                   timestep_size;
	
	Structured_Storage<Parameter_Value, Entity_Id>  parameter_data;
	Structured_Storage<double, Var_Id>              series_data;
	Structured_Storage<double, Var_Id>              result_data;
	Structured_Storage<s64, Neighbor_T>             neighbor_data;
	
	void set_up_parameter_structure(std::unordered_map<Entity_Id, std::vector<Entity_Id>, Hash_Fun<Entity_Id>> *par_group_index_sets = nullptr);
	void set_up_series_structure(Series_Metadata *metadata = nullptr);
	void set_up_neighbor_structure();
	
	Date_Time get_start_date_parameter();
	Date_Time get_end_date_parameter();
	
	bool is_compiled;
	void compile();
	
	Data_Set              *data_set;
	
	LLVM_Module_Data      *llvm_data;
	
	Run_Batch              initial_batch;
	std::vector<Run_Batch> batches;
};


template<> inline String_View
Structured_Storage<Parameter_Value, Entity_Id>::get_handle_name(Entity_Id par) {
	return parent->model->find_entity<Reg_Type::parameter>(par)->name;
}

template<> inline String_View
Structured_Storage<double, Var_Id>::get_handle_name(Var_Id var_id) {
	// TODO: oof, this is a hack and prone to break. We should have another way of determining if we are in the state vars or series structure.
	if(initial_step == 0)
		return parent->model->series[var_id]->name;
	return parent->model->state_vars[var_id]->name;
}

template<> inline String_View
Structured_Storage<s64, Neighbor_T>::get_handle_name(Neighbor_T nb) {
	return parent->model->find_entity<Reg_Type::neighbor>(nb.neighbor)->name;
}

template<typename Val_T, typename Handle_T> const std::vector<Entity_Id> &
Structured_Storage<Val_T, Handle_T>::get_index_sets(Handle_T handle) {
	auto array_idx = handle_is_in_array[handle];
	return structure[array_idx].index_sets;
}

template<typename Val_T, typename Handle_T> s64
Structured_Storage<Val_T, Handle_T>::get_offset_base(Handle_T handle) {
	auto array_idx = handle_is_in_array[handle];
	return structure[array_idx].get_offset_base(handle);
}

template<typename Val_T, typename Handle_T> s64
Structured_Storage<Val_T, Handle_T>::instance_count(Handle_T handle) {
	auto array_idx = handle_is_in_array[handle];
	return structure[array_idx].instance_count(&parent->index_counts);
}

template<typename Val_T, typename Handle_T> s64
Structured_Storage<Val_T, Handle_T>::get_offset(Handle_T handle, std::vector<Index_T> *indexes) {
	auto array_idx = handle_is_in_array[handle];
	return structure[array_idx].get_offset(handle, indexes, &parent->index_counts);
}

template<typename Val_T, typename Handle_T> s64
Structured_Storage<Val_T, Handle_T>::get_offset_alternate(Handle_T handle, std::vector<Index_T> *indexes) {
	auto array_idx = handle_is_in_array[handle];
	return structure[array_idx].get_offset_alternate(handle, indexes, &parent->index_counts);
}
	
template<typename Val_T, typename Handle_T> Math_Expr_FT *
Structured_Storage<Val_T, Handle_T>::get_offset_code(Handle_T handle, std::vector<Math_Expr_FT *> *indexes) {
	auto array_idx = handle_is_in_array[handle];
	auto code = structure[array_idx].get_offset_code(handle, indexes, &parent->index_counts);
	if(!code)
		fatal_error(Mobius_Error::internal, "We somehow referenced an index that was not properly initialized, get_offset_code(). Name of referenced variable was \"", get_handle_name(handle), "\".");
	return code;
}

template<typename Val_T, typename Handle_T> void
Structured_Storage<Val_T, Handle_T>::set_up(std::vector<Multi_Array_Structure<Handle_T>> &&structure) {
	//TODO: check that index_counts are properly set up in parent.
	
	if(has_been_set_up)
		fatal_error(Mobius_Error::internal, "Tried to set up structure twice.");
	
	this->structure = structure;
	
	s64 offset = 0;
	s32 array_idx = 0;
	for(auto &multi_array : this->structure) {
		multi_array.begin_offset = offset;
		offset += multi_array.total_count(&parent->index_counts);
		for(Handle_T handle : multi_array.handles)
			handle_is_in_array[handle] = array_idx;
		++array_idx;
	}
	total_count = offset;
	
	has_been_set_up = true;
}


template<typename Val_T, typename Handle_T> void
Structured_Storage<Val_T, Handle_T>::for_each(Handle_T handle, const std::function<void(std::vector<Index_T> *, s64)> &do_stuff) {
	auto array_idx = handle_is_in_array[handle];
	auto &index_sets = structure[array_idx].index_sets;
	std::vector<Index_T> indexes;
	if(index_sets.empty()) {
		s64 offset = get_offset_alternate(handle, &indexes);
		do_stuff(&indexes, offset);
		return;
	}
	indexes.resize(index_sets.size());
	for(int level = 0; level < index_sets.size(); ++level) {
		indexes[level].index_set = index_sets[level];
		indexes[level].index = 0;
	}
	
	int bottom = (int)index_sets.size() - 1;
	int level = bottom;
	while(true) {
		if(level == bottom) {
			s64 offset = get_offset_alternate(handle, &indexes);
			do_stuff(&indexes, offset);
		}
		indexes[level].index++;
		if(indexes[level].index == parent->index_counts[level].index) {
			if(level == 0) break;
			indexes[level].index = 0;
			level--;
		} else if (level != bottom)
			level++;
	}
}


#endif // MOBIUS_MODEL_APPLICATION_H