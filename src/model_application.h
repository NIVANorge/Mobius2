
#ifndef MOBIUS_MODEL_APPLICATION_H
#define MOBIUS_MODEL_APPLICATION_H

#include "model_declaration.h"
#include "llvm_jit.h"
#include "data_set.h"

#include <functional>

struct
State_Var_Dependency {
	enum Type : u32 {
		none =         0x0,
		earlier_step = 0x1,
	}                 type;
	Var_Id            var_id;
};

inline bool operator<(const State_Var_Dependency &a, const State_Var_Dependency &b) { if(a.var_id < b.var_id) return true; return (u32)a.type < (u32)b.type; }

struct
Dependency_Set {
	std::set<Entity_Id>             on_parameter;
	std::set<Var_Id>                on_series;
	std::set<State_Var_Dependency>  on_state_var;
};

struct Math_Expr_FT;

struct
Var_Location_Hash {
	int operator()(const Var_Location &loc) const {
		if(!is_located(loc))
			fatal_error(Mobius_Error::internal, "Tried to hash a non-located value location.");
		
		// hopefully this one is ok...
		constexpr int mod = 10889;
		int res = loc.compartment.id;
		res = (res*11 + loc.property_or_quantity.id) % mod;
		for(int idx = 0; idx < loc.n_dissolved; ++idx)
			res = (res*11 + loc.dissolved_in[idx].id) % mod;
		
		return res;
	}
};

// TODO; Hmm, It is a bit superfluous to have a State_Variable record for input series since most of the contents are not relevant for that.

struct
State_Variable {
	Decl_Type type; //either flux, quantity or property
	
	enum Flags {
		f_none                = 0x00,
		f_in_flux             = 0x01,
		f_in_flux_neighbor    = 0x02,
		f_is_aggregate        = 0x04, 
		f_has_aggregate       = 0x08,
		f_clear_series_to_nan = 0x10,
		f_dissolved_flux      = 0x20,
		f_dissolved_conc      = 0x40,
		f_invalid             = 0x1000,
	} flags;
	
	//TODO: could probably combine some members of this struct in a union. They are not all going to be relevant at the same time.
	
	std::string name;

	Entity_Id entity_id;  // This is the ID of the declaration (if the variable is not auto-generated), either has(...) or flux(...)
	
	Unit_Data unit; //NOTE: this can't just be an Entity_Id, because we need to be able to generate units for these.
	
	Entity_Id neighbor; // For a flux that points at a neighbor.
	
	// If this is a quantity or property, loc1 is the location of this variable.
	// If this is a flux, loc1 and loc2 are the source and target of the flux resp.
	Var_Location   loc1;
	Var_Location   loc2;
	
	// if f_is_aggregate, this is what it aggregates. if f_has_aggregate, this is who aggregates it.
	Var_Id         agg;
	Entity_Id      agg_to_compartment;
	
	// if f_in_flux is set (this is the aggregation variable for the in fluxes), agg points at the quantity that is the target of the fluxes.
	Var_Id         in_flux_target;
	
	// If this is the target variable of a neighbor flux, neighbor_agg points to the aggregation variable for the neighbor flux.
	// If this is the aggregate ( f_in_flux_neighbor is set ), neighbor_agg points to the target of the neighbor flux(es) (which is the same as the source).
	Var_Id         neighbor_agg;
	
	// If this is a generated flux for a dissolved quantity (f_dissolved_flux is set), dissolved_conc is the respective generated conc of the quantity. dissolved_flux is the flux of the quantity that this one is dissolved in.
	// If this is the generated conc (f_dissolved_conc is set), dissolved_conc is the variable for the mass of the quantity.
	// If none of the flags are set and this is the mass of the quantity, dissolved_conc also points to the conc.
	Var_Id         dissolved_conc;
	Var_Id         dissolved_flux;
	
	Math_Expr_FT *function_tree;
	bool initial_is_conc;
	Math_Expr_FT *initial_function_tree;
	Math_Expr_FT *aggregation_weight_tree;
	Math_Expr_FT *unit_conversion_tree;
	bool override_is_conc;
	Math_Expr_FT *override_tree;
	
	State_Variable() : function_tree(nullptr), initial_function_tree(nullptr), initial_is_conc(false), aggregation_weight_tree(nullptr), unit_conversion_tree(nullptr), override_tree(nullptr), override_is_conc(false), flags(f_none), agg(invalid_var), neighbor(invalid_entity_id), neighbor_agg(invalid_var), dissolved_conc(invalid_var), dissolved_flux(invalid_var) {};
};

template <Var_Id::Type var_type>
struct Var_Registry {
	std::vector<State_Variable> vars;
	std::unordered_map<Var_Location, Var_Id, Var_Location_Hash> location_to_id;
	std::unordered_map<std::string, std::set<Var_Id>>           name_to_id;
	
	State_Variable *operator[](Var_Id id) {
		if(id.type != var_type)
			fatal_error(Mobius_Error::internal, "Tried to look up a variable of wrong type.");
		if(!is_valid(id) || id.id >= vars.size())
			fatal_error(Mobius_Error::internal, "Tried to look up a variable using an invalid id.");
		return &vars[id.id];
	}
	
	Var_Id operator[](const Var_Location &loc) {
		if(!is_located(loc))
			fatal_error(Mobius_Error::internal, "Tried to look up a variable using a non-located location.");
		auto find = location_to_id.find(loc);
		if(find == location_to_id.end())
			return invalid_var;
		return find->second;
	}
	
	//NOTE: wanted this to return a reference, but then it can't return {} when something is not found.
	const std::set<Var_Id> operator[](std::string name) {
		auto find = name_to_id.find(name);
		if(find == name_to_id.end())
			return {};
			//fatal_error(Mobius_Error::internal, "Tried to look up a variable using an invalid name \"", name, "\".");
		return find->second;
	}
	
	Var_Id register_var(State_Variable var, Var_Location loc) {
		if(is_located(loc)) {
			Var_Id id = (*this)[loc];
			if(is_valid(id))
				fatal_error(Mobius_Error::internal, "Re-registering a variable."); //TODO: hmm, this only catches cases where the location was valid.
		}
		
		vars.push_back(var);
		Var_Id id = {var_type, (s32)vars.size()-1};
		if(is_located(loc))
			location_to_id[loc] = id;
		name_to_id[var.name].insert(id);
		return id;
	}
	
	Var_Id begin() { return {var_type, 0}; }
	Var_Id end()   { return {var_type, (s32)vars.size()}; }
	size_t count() { return vars.size(); }
};


struct Neighbor_T {
	Entity_Id neighbor;
	s32       info_id;
};

inline bool operator==(const Neighbor_T &a, const Neighbor_T& b) { return a.neighbor == b.neighbor && a.info_id == b.info_id; }

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

// TODO: remove Val_T template parameter (it is unused)
template<typename Handle_T>
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
	
	const std::string &
	get_handle_name(Handle_T handle);
	
	Storage_Structure(Model_Application *parent) : parent(parent), has_been_set_up(false), total_count(0) {}
};

template<typename Val_T, typename Handle_T>
struct Data_Storage {
	Data_Storage(Storage_Structure<Handle_T> *structure, s64 initial_step = 0) : structure(structure), initial_step(initial_step) {}
	
	Storage_Structure<Handle_T> *structure;
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
	std::string name;
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
	
	std::vector<Index_T>                                     index_counts;
	std::vector<std::unordered_map<std::string, Index_T>>    index_names_map;
	std::vector<std::vector<std::string>>                    index_names;
	
	Time_Step_Size                                           time_step_size;
	
	Var_Registry<Var_Id::Type::state_var>                    state_vars;
	Var_Registry<Var_Id::Type::series>                       series;
	Var_Registry<Var_Id::Type::additional_series>            additional_series;
	
	Storage_Structure<Entity_Id>                             parameter_structure;
	Storage_Structure<Neighbor_T>                            neighbor_structure;
	Storage_Structure<Var_Id>                                result_structure;
	Storage_Structure<Var_Id>                                series_structure;
	Storage_Structure<Var_Id>                                additional_series_structure;
	
	Data_Set                                                *data_set;
	
	LLVM_Module_Data                                        *llvm_data;
	
	Run_Batch                                                initial_batch;
	std::vector<Run_Batch>                                   batches;
	
	bool                                                     is_compiled = false;
	std::vector<Entity_Id>                                   baked_parameters;       // Hmm, may not be the best to have this here.

	
	void set_indexes(Entity_Id index_set, std::vector<std::string> &indexes);
	Index_T get_index(Entity_Id index_set, const std::string &name);
	bool all_indexes_are_set();
	
	void build_from_data_set(Data_Set *data_set);
	void save_to_data_set();
	
	void set_up_parameter_structure(std::unordered_map<Entity_Id, std::vector<Entity_Id>, Hash_Fun<Entity_Id>> *par_group_index_sets = nullptr);
	void set_up_neighbor_structure();
	
	template<Var_Id::Type var_type> void
	set_up_series_structure(Var_Registry<var_type> &reg, Storage_Structure<Var_Id> &data, Series_Metadata *metadata);
	
	// TODO: this one should maybe be on the Model_Data struct instead
	void allocate_series_data(s64 time_steps, Date_Time start_date);
	
	//void compose();
	void compile();
};


template<> inline const std::string&
Storage_Structure<Entity_Id>::get_handle_name(Entity_Id par) {
	return parent->model->parameters[par]->name;
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
Storage_Structure<Neighbor_T>::get_handle_name(Neighbor_T nb) {
	return parent->model->neighbors[nb.neighbor]->name;
}

template<typename Handle_T> const std::vector<Entity_Id> &
Storage_Structure<Handle_T>::get_index_sets(Handle_T handle) {
	auto array_idx = handle_is_in_array[handle];
	return structure[array_idx].index_sets;
}

template<typename Handle_T> s64
Storage_Structure<Handle_T>::get_offset_base(Handle_T handle) {
	auto array_idx = handle_is_in_array[handle];
	return structure[array_idx].get_offset_base(handle);
}

template<typename Handle_T> s64
Storage_Structure<Handle_T>::instance_count(Handle_T handle) {
	auto array_idx = handle_is_in_array[handle];
	return structure[array_idx].instance_count(parent->index_counts);
}

template<typename Handle_T> s64
Storage_Structure<Handle_T>::get_offset(Handle_T handle, std::vector<Index_T> &indexes) {
	auto array_idx = handle_is_in_array[handle];
	return structure[array_idx].get_offset(handle, indexes, parent->index_counts);
}

template<typename Handle_T> s64
Storage_Structure<Handle_T>::get_offset_alternate(Handle_T handle, std::vector<Index_T> &indexes) {
	auto array_idx = handle_is_in_array[handle];
	return structure[array_idx].get_offset_alternate(handle, indexes, parent->index_counts);
}
	
template<typename Handle_T> Math_Expr_FT *
Storage_Structure<Handle_T>::get_offset_code(Handle_T handle, std::vector<Math_Expr_FT *> &indexes) {
	auto array_idx = handle_is_in_array[handle];
	auto code = structure[array_idx].get_offset_code(handle, indexes, parent->index_counts);
	if(!code)
		fatal_error(Mobius_Error::internal, "We somehow referenced an index that was not properly initialized, get_offset_code(). Name of referenced variable was \"", get_handle_name(handle), "\".");
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
		offset += multi_array.total_count(parent->index_counts);
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
	for(int level = 0; level < index_sets.size(); ++level) {
		indexes[level].index_set = index_sets[level];
		indexes[level].index = 0;
	}
	for_each_helper(this, handle, do_stuff, indexes, 0);
}

// TODO: Debug why this one doesn't work. It is in principle nicer (not requiring recursion).
#if 0
template<typename Handle_T> void
Storage_Structure<Handle_T>::for_each(Handle_T handle, const std::function<void(std::vector<Index_T> &, s64)> &do_stuff) {
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