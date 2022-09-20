
#ifndef MOBIUS_MODEL_APPLICATION_H
#define MOBIUS_MODEL_APPLICATION_H

#include "model_declaration.h"
#include "llvm_jit.h"
#include "data_set.h"

#include <functional>


struct Neighbor_T {
	Entity_Id neighbor;
	s32       info_id;
};

inline bool operator==(const Neighbor_T &a, const Neighbor_T& b) { return a.neighbor == b.neighbor && a.info_id == b.info_id; }

template<typename Handle_T> struct Hash_Fun {
	int operator()(const Handle_T&) const;
};

template<> struct Hash_Fun<Entity_Id> {
	int operator()(const Entity_Id& id) const { return entity_id_hash(id); }
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
	
	s64 get_offset(Handle_T handle, std::vector<Index_T> &indexes, std::vector<Index_T> &index_counts) {
		s64 offset = 0;
		for(auto &index_set : index_sets) {
			//TODO: check that the indexes and counts are in the right index set (at least with debug flags turned on)
			offset *= (s64)index_counts[index_set.id].index;
			offset += (s64)indexes[index_set.id].index;
		}
		return (s64)offset*handles.size() + get_offset_base(handle);
	}
	
	s64 get_offset_alternate(Handle_T handle, std::vector<Index_T> &indexes, std::vector<Index_T> &index_counts) {
		if(indexes.size() != index_sets.size())
			fatal_error(Mobius_Error::internal, "Got wrong amount of indexes to get_offset_alternate().");
		s64 offset = 0;
		int idx = 0;
		for(auto &index_set : index_sets) {
			auto &index = indexes[idx];
			if(index.index_set != index_set)
				fatal_error(Mobius_Error::internal, "Got mismatching index sets to get_offset_alternate().");
			//TODO: check that the indexes and counts are in the right index set (at least with debug flags turned on)
			offset *= (s64)index_counts[index_set.id].index;
			offset += (s64)index.index;
			++idx;
		}
		return (s64)offset*handles.size() + get_offset_base(handle);
	}
	
	Math_Expr_FT *get_offset_code(Handle_T handle, std::vector<Math_Expr_FT *> &indexes, std::vector<Index_T> &index_counts) {
		Math_Expr_FT *result;
		if(index_sets.empty()) result = make_literal((s64)0);
		for(int idx = 0; idx < index_sets.size(); ++idx) {
			//TODO: check that counts are in the right index set
			auto &index_set = index_sets[idx];
			Math_Expr_FT *index = indexes[index_set.id];
			if(!index)
				return nullptr;
			index = copy(index);
			
			if(idx == 0)
				result = index;
			else {
				result = make_binop('*', result, make_literal((s64)index_counts[index_set.id].index));
				result = make_binop('+', result, index);
			}
		}
		result = make_binop('*', result, make_literal((s64)handles.size()));
		result = make_binop('+', result, make_literal((s64)(begin_offset + handle_location[handle])));
		return result;
	}
	
	s64 instance_count(std::vector<Index_T> &index_counts) {
		s64 count = 1;
		for(auto &index_set : index_sets)
			count *= (s64)index_counts[index_set.id].index;
		return count;
	}
	
	s64 total_count(std::vector<Index_T> &index_counts) { 
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

// TODO: rename to storage structure
template<typename Val_T, typename Handle_T>
struct Storage_Structure {
	s64       total_count;
	bool      has_been_set_up;
	
	Model_Application *parent;
	
	std::unordered_map<Handle_T, s32, Hash_Fun<Handle_T>> handle_is_in_array;
	std::vector<Multi_Array_Structure<Handle_T>> structure;
	
	//TODO: need much more error checking in these!
	
	void set_up(std::vector<Multi_Array_Structure<Handle_T>> &&structure);
	
	s64 get_offset_base(Handle_T handle);
	s64 instance_count(Handle_T handle);
	s64 get_offset(Handle_T handle, std::vector<Index_T> &indexes);
	s64 get_offset_alternate(Handle_T handle, std::vector<Index_T> &indexes);
	
	const std::vector<Entity_Id> &
	get_index_sets(Handle_T handle);
	
	Math_Expr_FT *
	get_offset_code(Handle_T handle, std::vector<Math_Expr_FT *> &indexes);
	
	void
	for_each(Handle_T, const std::function<void(std::vector<Index_T> &, s64)>&);
	
	String_View
	get_handle_name(Handle_T handle);
	
	Storage_Structure(Model_Application *parent) : parent(parent), has_been_set_up(false), total_count(0) {}
};

template<typename Val_T, typename Handle_T>
struct Data_Storage {
	Data_Storage(Storage_Structure<Val_T, Handle_T> *structure, s64 initial_step = 0) : structure(structure), initial_step(initial_step) {}
	
	Storage_Structure<Val_T, Handle_T> *structure;
	Val_T *data = nullptr;
	s64           time_steps = 0;
	s64           initial_step;
	Date_Time     start_date = {};
	bool is_owning = false;
	
	void free_data() {
		if(data && is_owning) free(data);
		data = nullptr;
		time_steps = 0;
		is_owning = false;
	}
	
	//TODO: there should be a version of this one that checks for out of bounds indexing (or non-allocated data). But we also want the fast one that doesn't
	Val_T  *
	get_value(s64 offset, s64 time_step = 0) {
		s64 step = std::max(time_step + initial_step, (s64)0);
		return data + offset + step*structure->total_count;
	}
	
	size_t
	alloc_size() { return sizeof(Val_T) * structure->total_count * (time_steps + initial_step); }
	
	void 
	allocate(s64 time_steps = 1, Date_Time start_date = {}) {
		if(!structure->has_been_set_up)
			fatal_error(Mobius_Error::internal, "Tried to allocate data before structure was set up.");
		free_data();
		this->time_steps = time_steps;
		this->start_date = start_date;
		size_t sz = alloc_size();
		data = (Val_T *) malloc(sz);
		memset(data, 0, sz);
		is_owning = true;
	};
	
	// TODO: we could have some kind of tracking of references so that we at least get an error message if the source is deleted before all references are.
	void refer_to(Data_Storage<Val_T, Handle_T> *source) {
		if(structure != source->structure)
			fatal_error(Mobius_Error::internal, "Tried to make a data storage refer to another one that belongs to a different storage structure.");
		free_data();
		data = source->data;
		time_steps = source->time_steps;
		start_date = source->start_date;
		is_owning = false;
	}
	
	void copy_from(Data_Storage<Val_T, Handle_T> *source, bool size_only = false) {
		if(structure != source->structure)
			fatal_error(Mobius_Error::internal, "Tried to make a data storage copy from another one that belongs to a different storage structure.");
		if(source->time_steps > 0) {
			free_data();
			allocate(source->time_steps, source->start_date);
			if(!size_only)
				memcpy(data, source->data, alloc_size());
		} else {
			time_steps = 0;
			start_date = source->start_date;
		}
	}
	
	~Data_Storage() { free_data(); }
};

struct Model_Data {
	Model_Data(Model_Application *app);

	Model_Application *app;
	
	Data_Storage<Parameter_Value, Entity_Id>  parameters;
	Data_Storage<double, Var_Id>              series;
	Data_Storage<double, Var_Id>              results;
	Data_Storage<s64, Neighbor_T>             neighbors;
	Data_Storage<double, Var_Id>              additional_series;
	
	Model_Data *copy();
	Date_Time get_start_date_parameter();
	Date_Time get_end_date_parameter();
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
	std::unordered_map<Var_Id, std::vector<Entity_Id>, Hash_Fun<Var_Id>> index_sets_additional;
	Series_Metadata() : any_data_at_all(false) {}
};

struct Index_Name {
	String_View name;
};

struct
Model_Application {
	
	Model_Application(Mobius_Model *model) : 
		model(model), parameter_structure(this), series_structure(this), result_structure(this), neighbor_structure(this), 
		additional_series_structure(this), is_compiled(false), data_set(nullptr), alloc(1024), data(this) {
			
		if(!model->is_composed)
			fatal_error(Mobius_Error::internal, "Tried to create a model application before the model was composed.");
		
		auto global = model->modules[0];
		
		index_counts.resize(global->index_sets.count());
		index_names_map.resize(global->index_sets.count());
		index_names.resize(global->index_sets.count());
		
		for(auto index_set : global->index_sets) {
			index_counts[index_set.id].index_set = index_set;
			index_counts[index_set.id].index = 0;
		}
		
		// TODO: make time step size configurable.
		time_step_size.unit      = Time_Step_Size::second;
		time_step_size.magnitude = 86400;
		
		initialize_llvm();
		llvm_data = create_llvm_module();   //TODO: free it on destruction!
	}
	
	~Model_Application() {
		// TODO: should probably free more stuff.
		free_llvm_module(llvm_data);
	}
	
	Mobius_Model                                   *model;
	Model_Data                                      data;
	
	Linear_Allocator                                alloc; // For storing index names
	
	std::vector<Index_T>                            index_counts;
	std::vector<string_map<Index_T>>                index_names_map;
	std::vector<std::vector<String_View>>           index_names;
	
	Time_Step_Size                                  time_step_size;
	
	Var_Registry<2>                                 additional_series;
	
	Storage_Structure<Parameter_Value, Entity_Id>  parameter_structure;
	Storage_Structure<double, Var_Id>              series_structure;
	Storage_Structure<double, Var_Id>              result_structure;
	Storage_Structure<s64, Neighbor_T>             neighbor_structure;
	Storage_Structure<double, Var_Id>              additional_series_structure;
	
	Data_Set              *data_set;
	
	LLVM_Module_Data      *llvm_data;
	
	Run_Batch              initial_batch;
	std::vector<Run_Batch> batches;
	
	bool is_compiled;
	
	
	void set_indexes(Entity_Id index_set, std::vector<String_View> &indexes);
	Index_T get_index(Entity_Id index_set, String_View name);
	bool all_indexes_are_set();
	
	void build_from_data_set(Data_Set *data_set);
	void save_to_data_set();
	
	void set_up_parameter_structure(std::unordered_map<Entity_Id, std::vector<Entity_Id>, Hash_Fun<Entity_Id>> *par_group_index_sets = nullptr);
	void set_up_neighbor_structure();
	
	template<s32 var_type> void
	set_up_series_structure(Var_Registry<var_type> &reg, Storage_Structure<double, Var_Id> &data, Series_Metadata *metadata);
	
	// TODO: this one should maybe be on the Model_Data struct instead
	void allocate_series_data(s64 time_steps, Date_Time start_date);
	
	void compile();
};


template<> inline String_View
Storage_Structure<Parameter_Value, Entity_Id>::get_handle_name(Entity_Id par) {
	return parent->model->find_entity<Reg_Type::parameter>(par)->name;
}

template<> inline String_View
Storage_Structure<double, Var_Id>::get_handle_name(Var_Id var_id) {
	if(var_id.type == 0)
		return parent->model->state_vars[var_id]->name;
	else if(var_id.type == 1)
		return parent->model->series[var_id]->name;
	else
		return parent->additional_series[var_id]->name;
}

template<> inline String_View
Storage_Structure<s64, Neighbor_T>::get_handle_name(Neighbor_T nb) {
	return parent->model->find_entity<Reg_Type::neighbor>(nb.neighbor)->name;
}

template<typename Val_T, typename Handle_T> const std::vector<Entity_Id> &
Storage_Structure<Val_T, Handle_T>::get_index_sets(Handle_T handle) {
	auto array_idx = handle_is_in_array[handle];
	return structure[array_idx].index_sets;
}

template<typename Val_T, typename Handle_T> s64
Storage_Structure<Val_T, Handle_T>::get_offset_base(Handle_T handle) {
	auto array_idx = handle_is_in_array[handle];
	return structure[array_idx].get_offset_base(handle);
}

template<typename Val_T, typename Handle_T> s64
Storage_Structure<Val_T, Handle_T>::instance_count(Handle_T handle) {
	auto array_idx = handle_is_in_array[handle];
	return structure[array_idx].instance_count(parent->index_counts);
}

template<typename Val_T, typename Handle_T> s64
Storage_Structure<Val_T, Handle_T>::get_offset(Handle_T handle, std::vector<Index_T> &indexes) {
	auto array_idx = handle_is_in_array[handle];
	return structure[array_idx].get_offset(handle, indexes, parent->index_counts);
}

template<typename Val_T, typename Handle_T> s64
Storage_Structure<Val_T, Handle_T>::get_offset_alternate(Handle_T handle, std::vector<Index_T> &indexes) {
	auto array_idx = handle_is_in_array[handle];
	return structure[array_idx].get_offset_alternate(handle, indexes, parent->index_counts);
}
	
template<typename Val_T, typename Handle_T> Math_Expr_FT *
Storage_Structure<Val_T, Handle_T>::get_offset_code(Handle_T handle, std::vector<Math_Expr_FT *> &indexes) {
	auto array_idx = handle_is_in_array[handle];
	auto code = structure[array_idx].get_offset_code(handle, indexes, parent->index_counts);
	if(!code)
		fatal_error(Mobius_Error::internal, "We somehow referenced an index that was not properly initialized, get_offset_code(). Name of referenced variable was \"", get_handle_name(handle), "\".");
	return code;
}

template<typename Val_T, typename Handle_T> void
Storage_Structure<Val_T, Handle_T>::set_up(std::vector<Multi_Array_Structure<Handle_T>> &&structure) {
	//TODO: check that index_counts are properly set up in parent.
	
	if(has_been_set_up)
		fatal_error(Mobius_Error::internal, "Tried to set up structure twice.");
	
	this->structure = structure;
	
	s64 offset = 0;
	s32 array_idx = 0;
	for(auto &multi_array : this->structure) {
		multi_array.begin_offset = offset;
		offset += multi_array.total_count(parent->index_counts);
		for(Handle_T handle : multi_array.handles)
			handle_is_in_array[handle] = array_idx;
		++array_idx;
	}
	total_count = offset;
	
	has_been_set_up = true;
}

template<typename Val_T, typename Handle_T> void
for_each_helper(Storage_Structure<Val_T, Handle_T> *self, Handle_T handle, const std::function<void(std::vector<Index_T> &, s64)> &do_stuff, std::vector<Index_T> &indexes, int level) {
	Entity_Id index_set = indexes[level].index_set;
	
	if(level == indexes.size()-1) {
		for(int idx = 0; idx < self->parent->index_counts[index_set.id].index; ++idx) {
			indexes[level].index = idx;
			s64 offset = self->get_offset_alternate(handle, indexes);
			do_stuff(indexes, offset);
		}
	} else {
		for(int idx = 0; idx < self->parent->index_counts[index_set.id].index; ++idx) {
			indexes[level].index = idx;
			for_each_helper(self, handle, do_stuff, indexes, level+1);
		}
	}
}

template<typename Val_T, typename Handle_T> void
Storage_Structure<Val_T, Handle_T>::for_each(Handle_T handle, const std::function<void(std::vector<Index_T> &, s64)> &do_stuff) {
	auto array_idx   = handle_is_in_array[handle];
	auto &index_sets = structure[array_idx].index_sets;
	std::vector<Index_T> indexes;
	if(index_sets.empty()) {
		s64 offset = get_offset_alternate(handle, indexes);
		do_stuff(indexes, offset);
		return;
	}
	indexes.resize(index_sets.size());
	for(int level = 0; level < index_sets.size(); ++level) {
		indexes[level].index_set = index_sets[level];
		indexes[level].index = 0;
	}
	for_each_helper(this, handle, do_stuff, indexes, 0);
}

// TODO: Debug why this one doesn't work. It is in principle nicer.
#if 0
template<typename Val_T, typename Handle_T> void
Storage_Structure<Val_T, Handle_T>::for_each(Handle_T handle, const std::function<void(std::vector<Index_T> &, s64)> &do_stuff) {
	auto array_idx   = handle_is_in_array[handle];
	auto &index_sets = structure[array_idx].index_sets;
	std::vector<Index_T> indexes;
	if(index_sets.empty()) {
		s64 offset = get_offset_alternate(handle, &indexes);
		do_stuff(indexes, offset);
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
		if((indexes[level].index != parent->index_counts[level].index) && (level == bottom)) {
			s64 offset = get_offset_alternate(handle, &indexes);
			do_stuff(indexes, offset);
		}
		if(level == bottom)
			indexes[level].index++;
		if(indexes[level].index == parent->index_counts[level].index) {
			indexes[level].index = 0;
			if(level == 0) break;
			level--;
			indexes[level].index++;
			continue;
		} else if(level != bottom)
			++level;
		/*
		if(level == bottom) {
			s64 offset = get_offset_alternate(handle, &indexes);
			do_stuff(indexes, offset);
		}
		indexes[level].index++;
		if(indexes[level].index == parent->index_counts[level].index) {
			if(level == 0) break;
			indexes[level].index = 0;
			level--;
		} else if (level != bottom)
			level++;
		*/
	}
}
#endif

#endif // MOBIUS_MODEL_APPLICATION_H