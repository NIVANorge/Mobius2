
#ifndef MOBIUS_MODEL_APPLICATION_H
#define MOBIUS_MODEL_APPLICATION_H

#include "model_declaration.h"
#include "llvm_jit.h"
#include "data_set.h"
#include "state_variable.h"

#include <functional>


#define INDEX_PACKING_ALTERNATIVE 0


struct Math_Expr_FT;

struct
Var_Location_Hash {
	int operator()(const Var_Location &loc) const {
		if(!is_located(loc))
			fatal_error(Mobius_Error::internal, "Tried to hash a non-located value location.");
		
		// hopefully this one is ok...
		constexpr int mod = 10889;
		int res = 0;
		for(int idx = 0; idx < loc.n_components; ++idx)
			res = (res*11 + loc.components[idx].id) % mod;
		
		return res;
	}
};

struct Var_Registry {
	
	Var_Registry(Var_Id::Type var_type) : var_type(var_type) {}
	
	Var_Id::Type                                                var_type;
	std::vector<std::unique_ptr<State_Var>>                     vars;
	std::unordered_map<Var_Location, Var_Id, Var_Location_Hash> location_to_id;
	std::unordered_map<std::string, std::set<Var_Id>>           name_to_id;
	
	State_Var *operator[](Var_Id id) {
		if(!is_valid(id) || id.id >= vars.size())
			fatal_error(Mobius_Error::internal, "Tried to look up a variable using an invalid id.");
		if(id.type != var_type)
			fatal_error(Mobius_Error::internal, "Tried to look up a variable of wrong type.");
		return vars[id.id].get();
	}
	
	Var_Id id_of(const Var_Location &loc) {
		if(!is_located(loc))
			fatal_error(Mobius_Error::internal, "Tried to look up a variable using a non-located location.");
		auto find = location_to_id.find(loc);
		if(find == location_to_id.end())
			return invalid_var;
		return find->second;
	}
	
	const std::set<Var_Id> find_by_name(const std::string &name) {
		auto find = name_to_id.find(name);
		if(find == name_to_id.end()) {
			static std::set<Var_Id> empty_set = {};
			return empty_set;
		}
		return find->second;
	}
	
	template<State_Var::Type type>
	Var_Id register_var(Var_Location loc, const std::string &name) {
		if(is_located(loc) && is_valid(id_of(loc)))
			fatal_error(Mobius_Error::internal, "Re-registering a variable.");
		
		// TODO: Better memory allocation system for these... Ideally want them in contiguous memory..
		auto var = new State_Var_Sub<type>();
		var->name = name;
		vars.push_back(std::unique_ptr<State_Var>(var));
		Var_Id id = {var_type, (s32)vars.size()-1};
		if(is_located(loc))
			location_to_id[loc] = id;
		name_to_id[name].insert(id);
		return id;
	}
	
	Var_Id begin() { return {var_type, 0}; }
	Var_Id end()   { return {var_type, (s32)vars.size()}; }
	size_t count() { return vars.size(); }
};


struct Connection_T {
	Entity_Id connection;
	Entity_Id source_compartment;
	s32       info_id;
};

inline bool operator==(const Connection_T &a, const Connection_T& b) { return a.connection == b.connection && a.source_compartment == b.source_compartment && a.info_id == b.info_id; }

template<typename Handle_T> struct Hash_Fun {
	int operator()(const Handle_T&) const;
};

// TODO: it is a bit wasteful to usa a hash map for these first two at all, they could just be indexes into a vector.
template<> struct Hash_Fun<Entity_Id> {
	int operator()(const Entity_Id& id) const { return id.id; }
};

template<> struct Hash_Fun<Var_Id> {
	int operator()(const Var_Id& id) const { return id.id; }
};

template<> struct Hash_Fun<Connection_T> {
	int operator()(const Connection_T& id) const { return 599*id.connection.id + 97*id.source_compartment.id + id.info_id; }   // No idea if the hash function is good, but it shouldn't matter that much.
};

struct Index_Exprs;
struct Model_Application;

template<typename Handle_T>
struct Multi_Array_Structure {
	std::vector<Entity_Id> index_sets;
	std::vector<Handle_T>  handles;
	
	std::unordered_map<Handle_T, s32, Hash_Fun<Handle_T>> handle_location;
	s64 begin_offset;
	
#if INDEX_PACKING_ALTERNATIVE
	s64 get_offset_base(Handle_T handle, Model_Application *app) {
		return begin_offset + handle_location[handle];
	}
#else
	s64 get_offset_base(Handle_T handle, Model_Application *app) {
		return begin_offset + handle_location[handle]*instance_count(app);
	}
#endif
	
	s64 get_stride(Handle_T handle);
	
	s64 get_offset(Handle_T handle, std::vector<Index_T> &indexes, Model_Application *app);
	s64 get_offset(Handle_T handle, std::vector<Index_T> &indexes, Index_T mat_col, Model_Application *app);
	s64 get_offset_alternate(Handle_T handle, std::vector<Index_T> &indexes, Model_Application *app);
	Math_Expr_FT *get_offset_code(Handle_T handle, Index_Exprs &index_exprs, Model_Application *app, Entity_Id &err_idx_set_out);
	s64 instance_count(Model_Application *app);
	
	s64 total_count(Model_Application *app) {
		return (s64)handles.size() * instance_count(app);
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

// TODO: remove Val_T template parameter (it is unused)
template<typename Handle_T>
struct Storage_Structure {
	s64       total_count;
	bool      has_been_set_up;
	
	Model_Application *parent;
	
	std::unordered_map<Handle_T, s32, Hash_Fun<Handle_T>> handle_is_in_array;
	std::vector<Multi_Array_Structure<Handle_T>> structure;
	
	void set_up(std::vector<Multi_Array_Structure<Handle_T>> &&structure);
	
	s64 get_offset_base(Handle_T handle);
	s64 get_stride(Handle_T handle);
	s64 instance_count(Handle_T handle);
	s64 get_offset(Handle_T handle, std::vector<Index_T> &indexes);
	s64 get_offset(Handle_T handle, std::vector<Index_T> &indexes, Index_T mat_col);
	s64 get_offset_alternate(Handle_T handle, std::vector<Index_T> &indexes);
	
	const std::vector<Entity_Id> &
	get_index_sets(Handle_T handle);
	
	Math_Expr_FT *
	get_offset_code(Handle_T handle, Index_Exprs &indexes);
	
	void
	for_each(Handle_T, const std::function<void(std::vector<Index_T> &, s64)>&);
	
	const std::string &
	get_handle_name(Handle_T handle);
	
	Storage_Structure(Model_Application *parent) : parent(parent), has_been_set_up(false), total_count(0) {}
};

template<typename Val_T, typename Handle_T>
struct Data_Storage {
	Data_Storage(Storage_Structure<Handle_T> *structure, s64 initial_step = 0) : structure(structure), initial_step(initial_step), data(nullptr), is_owning(false) {}
	
	Storage_Structure<Handle_T> *structure;
	Val_T *data;
	s64           time_steps = 0;
	s64           initial_step;
	Date_Time     start_date = {};
	bool is_owning;
	
	void free_data() {
		//if(data && is_owning) _aligned_free(data);
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
	allocate(s64 time_steps = 1, Date_Time start_date = {});
	
	// TODO: we could have some kind of tracking of references so that we at least get an error message if the source is deleted before all references are.
	void
	refer_to(Data_Storage<Val_T, Handle_T> *source);
	
	void
	copy_from(Data_Storage<Val_T, Handle_T> *source, bool size_only = false);
	
	~Data_Storage() { free_data(); }
};

struct Model_Data {
	Model_Data(Model_Application *app);

	Model_Application *app;
	
	Data_Storage<Parameter_Value, Entity_Id>  parameters;
	Data_Storage<double, Var_Id>              series;
	Data_Storage<double, Var_Id>              results;
	Data_Storage<s32, Connection_T>           connections;
	Data_Storage<double, Var_Id>              additional_series;
	Data_Storage<s32, Entity_Id>              index_counts;
	
	Model_Data *copy(bool copy_results = true);
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

struct
Sub_Indexed_Component {
	Entity_Id id;
	std::vector<Entity_Id> index_sets;
	
	bool can_be_source;                   // if this can be a source of the connection.
	std::set<Entity_Id> possible_sources; // what sources can have this as a target.
	int max_target_indexes;               // max indexes of a target that has this as the source
	
	Sub_Indexed_Component() : id(invalid_entity_id), can_be_source(false), max_target_indexes(0) {}
};

struct
Model_Application {
	
	Model_Application(Mobius_Model *model);
	
	~Model_Application() {
		// TODO: should probably free more stuff.
		free_llvm_module(llvm_data);
	}
	
	Mobius_Model                                            *model;
	Model_Data                                               data;
	
private :
	//std::vector<Index_T>                                     index_counts;
	std::vector<std::vector<Index_T>>                        index_counts;        // TODO: could we find a way of getting rid of this and just using the index_counts structure in the Model_Data ?
	std::vector<std::unordered_map<std::string, Index_T>>    index_names_map;
	std::vector<std::vector<std::string>>                    index_names;
public :
	
	Unit_Data                                                time_step_unit;
	Time_Step_Size                                           time_step_size;
	
	Var_Registry                                             state_vars;
	Var_Registry                                             series;
	Var_Registry                                             additional_series;
	
	Storage_Structure<Entity_Id>                             parameter_structure;
	Storage_Structure<Connection_T>                          connection_structure;
	Storage_Structure<Var_Id>                                result_structure;
	Storage_Structure<Var_Id>                                series_structure;
	Storage_Structure<Var_Id>                                additional_series_structure;
	Storage_Structure<Entity_Id>                             index_counts_structure;
	
	Data_Set                                                *data_set;
	
	std::vector<std::vector<Sub_Indexed_Component>>          connection_components;
	
	LLVM_Module_Data                                        *llvm_data;
	
	Run_Batch                                                initial_batch;
	std::vector<Run_Batch>                                   batches;
	
	bool                                                     is_compiled = false;
	std::vector<Entity_Id>                                   baked_parameters;
	
	void        set_indexes(Entity_Id index_set, std::vector<std::string> &indexes, Index_T parent_idx = invalid_index);
	void        set_indexes(Entity_Id index_set, int count, Index_T parent_idx = invalid_index);
	Index_T     get_max_index_count(Entity_Id index_set);
	Index_T     get_index_count(Entity_Id index_set, std::vector<Index_T> &indexes);
	Index_T     get_index_count_alternate(Entity_Id index_set, std::vector<Index_T> &indexes);
	Index_T     get_index(Entity_Id index_set, const std::string &name);
	std::string get_index_name(Index_T index);
	std::string get_possibly_quoted_index_name(Index_T index);
	bool        all_indexes_are_set();
	s64         active_instance_count(const std::vector<Entity_Id> &index_sets); // TODO: consider putting this on the Storage_Structure instead.
	bool        is_in_bounds(std::vector<Index_T> &indexes); // same?
	
	Sub_Indexed_Component *find_connection_component(Entity_Id conn_id, Entity_Id comp_id, bool make_error = true);
	Entity_Id              get_single_connection_index_set(Entity_Id conn_id);
	
	void build_from_data_set(Data_Set *data_set);
	void save_to_data_set();
	
	void set_up_parameter_structure(std::unordered_map<Entity_Id, std::vector<Entity_Id>, Hash_Fun<Entity_Id>> *par_group_index_sets = nullptr);
	void set_up_connection_structure();
	void set_up_index_count_structure();
	
	void
	set_up_series_structure(Var_Registry &reg, Storage_Structure<Var_Id> &data, Series_Metadata *metadata);
	
	// TODO: this one should maybe be on the Model_Data struct instead
	void allocate_series_data(s64 time_steps, Date_Time start_date);
	
	void compile(bool store_code_strings = false);
	
	std::string serialize  (Var_Id id);
	Var_Id      deserialize(const std::string &name);
	
	std::unordered_map<std::string, Var_Id> serial_to_id;
	
	Var_Registry *registry(Var_Id id) {
		if(id.type == Var_Id::Type::state_var) return &state_vars;
		if(id.type == Var_Id::Type::series)    return &series;
		if(id.type == Var_Id::Type::additional_series) return &additional_series;
		fatal_error(Mobius_Error::internal, "Invalid Var_Id type in registry().");
		return nullptr;
	}
	
	std::string batch_structure;
	std::string batch_code;
	std::string llvm_ir;
};

Entity_Id
avoid_index_set_dependency(Model_Application *app, Var_Loc_Restriction restriction);

struct
Index_Exprs {
	std::vector<Math_Expr_FT *> indexes;
	Math_Expr_FT               *mat_col;
	Entity_Id                   mat_index_set;
	
	Index_Exprs(Mobius_Model *model) : mat_col(nullptr), indexes(model->index_sets.count(), nullptr), mat_index_set(invalid_entity_id) { }
	~Index_Exprs() { clean(); }
	
	void clean() {
		for(int idx = 0; idx < indexes.size(); ++idx) {
			delete indexes[idx];
			indexes[idx] = nullptr;
		}
		delete mat_col;
		mat_col = nullptr;
		mat_index_set = invalid_entity_id;
	}
	
	void transpose() {
		if(!is_valid(mat_index_set)) return;
		auto tmp = mat_col;
		mat_col = indexes[mat_index_set.id];
		indexes[mat_index_set.id] = tmp;
	}
	
	void swap(std::vector<Math_Expr_FT *> &other_indexes) {
		for(int idx = 0; idx < indexes.size(); ++idx) {
			if(other_indexes[idx]) {
				auto tmp = indexes[idx];
				indexes[idx] = other_indexes[idx];
				other_indexes[idx] = tmp;
			}
		}
	}
};

inline void
check_index_bounds(Model_Application *app, Entity_Id index_set, Index_T index) {
	//TODO: This makes sure we are not out of bounds of the data, but it could still be
	//incorrect for sub-indexed things.
	if(index_set != index.index_set ||
		index.index < 0 || index.index >= app->get_max_index_count(index_set).index)
			fatal_error(Mobius_Error::internal, "Mis-indexing in one of the get_offset functions.");
}

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
	
	auto &indexes = index_exprs.indexes;
	Math_Expr_FT *result;
	if(index_sets.empty()) result = make_literal((s64)0);
	int sz = index_sets.size();
	for(int idx = 0; idx < index_sets.size(); ++idx) {
		auto &index_set = index_sets[idx];
		Math_Expr_FT *index = indexes[index_set.id];
		if(!index) {
			err_idx_set_out = index_set;
			return nullptr;
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
	result = make_binop('*', result, make_literal((s64)handles.size()));
	result = make_binop('+', result, make_literal((s64)(begin_offset + handle_location[handle])));
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
	
	auto &indexes = index_exprs.indexes;
	Math_Expr_FT *result = make_literal((s64)handle_location[handle]);
	int sz = index_sets.size();
	for(int idx = 0; idx < index_sets.size(); ++idx) {
		auto &index_set = index_sets[idx];
		Math_Expr_FT *index = indexes[index_set.id];
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

#endif

template<typename Handle_T> s64
Multi_Array_Structure<Handle_T>::instance_count(Model_Application *app) {
	s64 count = 1;
	for(auto &index_set : index_sets)
		count *= (s64)app->get_max_index_count(index_set).index;
	return count;
}

template<> inline const std::string&
Storage_Structure<Entity_Id>::get_handle_name(Entity_Id id) {
	return (*parent->model->registry(id.reg_type))[id]->name;
}

template<> inline const std::string&
Storage_Structure<Var_Id>::get_handle_name(Var_Id var_id) {
	if(var_id.type == Var_Id::Type::state_var)
		return parent->state_vars[var_id]->name;
	else if(var_id.type == Var_Id::Type::series)
		return parent->series[var_id]->name;
	else
		return parent->additional_series[var_id]->name;
}

template<> inline const std::string &
Storage_Structure<Connection_T>::get_handle_name(Connection_T nb) {
	return parent->model->connections[nb.connection]->name;
}

template<typename Handle_T> const std::vector<Entity_Id> &
Storage_Structure<Handle_T>::get_index_sets(Handle_T handle) {
	auto array_idx = handle_is_in_array.at(handle);
	return structure[array_idx].index_sets;
}

template<typename Handle_T> s64
Storage_Structure<Handle_T>::get_offset_base(Handle_T handle) {
	auto array_idx = handle_is_in_array.at(handle);
	return structure[array_idx].get_offset_base(handle, parent);
}

template<typename Handle_T> s64
Storage_Structure<Handle_T>::get_stride(Handle_T handle) {
	auto array_idx = handle_is_in_array.at(handle);
	return structure[array_idx].get_stride(handle);
}

template<typename Handle_T> s64
Storage_Structure<Handle_T>::instance_count(Handle_T handle) {
	auto array_idx = handle_is_in_array.at(handle);
	return structure[array_idx].instance_count(parent);
}

template<typename Handle_T> s64
Storage_Structure<Handle_T>::get_offset(Handle_T handle, std::vector<Index_T> &indexes) {
	auto array_idx = handle_is_in_array.at(handle);
	return structure[array_idx].get_offset(handle, indexes, parent);
}

template<typename Handle_T> s64
Storage_Structure<Handle_T>::get_offset(Handle_T handle, std::vector<Index_T> &indexes, Index_T mat_col) {
	auto array_idx = handle_is_in_array.at(handle);
	return structure[array_idx].get_offset(handle, indexes, mat_col, parent);
}

template<typename Handle_T> s64
Storage_Structure<Handle_T>::get_offset_alternate(Handle_T handle, std::vector<Index_T> &indexes) {
	auto array_idx = handle_is_in_array.at(handle);
	return structure[array_idx].get_offset_alternate(handle, indexes, parent);
}
	
template<typename Handle_T> Math_Expr_FT *
Storage_Structure<Handle_T>::get_offset_code(Handle_T handle, Index_Exprs &indexes) {
	auto array_idx = handle_is_in_array.at(handle);
	Entity_Id err_idx_set;
	auto code = structure[array_idx].get_offset_code(handle, indexes, parent, err_idx_set);
	if(!code) {
		fatal_error(Mobius_Error::internal, "A call to get_offset_code() somehow referenced an index that was not properly initialized. The name of the referenced variable was \"", get_handle_name(handle), "\". The index set was \"", parent->model->index_sets[err_idx_set]->name, "\".");
	}
	return code;
}

template<typename Handle_T> void
Storage_Structure<Handle_T>::set_up(std::vector<Multi_Array_Structure<Handle_T>> &&structure) {
	//TODO: check that index_counts are properly set up in parent.
	
	if(has_been_set_up)
		fatal_error(Mobius_Error::internal, "Tried to set up structure twice.");
	
	this->structure = structure;
	
	s64 offset = 0;
	s32 array_idx = 0;
	for(auto &multi_array : this->structure) {
		multi_array.begin_offset = offset;
		offset += multi_array.total_count(parent);
		for(Handle_T handle : multi_array.handles)
			handle_is_in_array[handle] = array_idx;
		++array_idx;
	}
	total_count = offset;
	
	has_been_set_up = true;
}

template<typename Handle_T> void
for_each_helper(Storage_Structure<Handle_T> *self, Handle_T handle, const std::function<void(std::vector<Index_T> &, s64)> &do_stuff, std::vector<Index_T> &indexes, int level) {
	Entity_Id index_set = indexes[level].index_set;
	
	if(level == indexes.size()-1) {
		for(int idx = 0; idx < self->parent->get_index_count_alternate(index_set, indexes).index; ++idx) {
			indexes[level].index = idx;
			s64 offset = self->get_offset_alternate(handle, indexes);
			do_stuff(indexes, offset);
		}
	} else {
		for(int idx = 0; idx < self->parent->get_index_count_alternate(index_set, indexes).index; ++idx) {
			indexes[level].index = idx;
			for_each_helper(self, handle, do_stuff, indexes, level+1);
		}
	}
}

template<typename Handle_T> void
Storage_Structure<Handle_T>::for_each(Handle_T handle, const std::function<void(std::vector<Index_T> &, s64)> &do_stuff) {
	auto array_idx   = handle_is_in_array[handle];
	auto &index_sets = structure[array_idx].index_sets;
	std::vector<Index_T> indexes;
	if(index_sets.empty()) {
		s64 offset = get_offset_alternate(handle, indexes);
		do_stuff(indexes, offset);
		return;
	}
	indexes.resize(index_sets.size());
	for(int level = 0; level < index_sets.size(); ++level) indexes[level] = Index_T { index_sets[level], 0 };
	for_each_helper(this, handle, do_stuff, indexes, 0);
}

/*
inline Entity_Id
get_flux_decl_id(Model_Application *app, State_Var *var) {
	if(!var->is_valid() || !var->is_flux()) return invalid_entity_id;
	if(var->type == State_Var::Type::declared)
		return as<State_Var::Type::declared>(var)->decl_id;
	else if(var->type == State_Var::Type::dissolved_flux)
		return get_flux_decl_id(app, app->state_vars[as<State_Var::Type::dissolved_flux>(var)->flux_of_medium]);
	else if(var->type == State_Var::Type::regular_aggregate)
		return get_flux_decl_id(app, app->state_vars[as<State_Var::Type::regular_aggregate>(var)->agg_of]);
	return invalid_entity_id;
}
*/

#endif // MOBIUS_MODEL_APPLICATION_H