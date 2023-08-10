
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
	
	std::vector<std::unique_ptr<State_Var>>                     state_vars;
	std::vector<std::unique_ptr<State_Var>>                     series;
	std::vector<std::unique_ptr<State_Var>>                     additional_series;
	
	std::vector<std::unique_ptr<State_Var>> &get_vec(Var_Id::Type type) {
		if(type == Var_Id::Type::state_var)         return state_vars;
		if(type == Var_Id::Type::series)            return series;
		if(type == Var_Id::Type::additional_series) return additional_series;
		fatal_error(Mobius_Error::internal, "Unhandled Var_Id::Type.");
	}
	
	std::unordered_map<Var_Location, Var_Id, Var_Location_Hash> location_to_id;
	std::unordered_map<std::string, std::set<Var_Id>>           name_to_id;
	
	State_Var *operator[](Var_Id id) {
		auto &vars = get_vec(id.type);
		if(!is_valid(id) || id.id >= vars.size())
			fatal_error(Mobius_Error::internal, "Tried to look up a variable using an invalid id.");
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
	
	const std::set<Var_Id> &find_by_name(const std::string &name) {
		auto find = name_to_id.find(name);
		if(find == name_to_id.end()) {
			static std::set<Var_Id> empty_set = {};
			return empty_set;
		}
		return find->second;
	}
	
	template<State_Var::Type type>
	Var_Id register_var(Var_Location loc, const std::string &name, Var_Id::Type id_type) {
		if(is_located(loc) && is_valid(id_of(loc)))
			fatal_error(Mobius_Error::internal, "Re-registering a variable with the same Var_Location.");
		
		auto var = new State_Var_Sub<type>();
		var->name = name;
		auto &vars = get_vec(id_type);

		vars.push_back(std::unique_ptr<State_Var>(var));
		Var_Id id = {id_type, (s32)vars.size()-1};
		
		if(is_located(loc))
			location_to_id[loc] = id;
		
		if(id_type != Var_Id::Type::state_var)
			name_to_id[name].insert(id);
		
		return id;
	}
	
	size_t count(Var_Id::Type type) { return get_vec(type).size(); }
	
	struct Var_Range {
		Var_Id::Type type;
		bool flux_only;
		std::vector<std::unique_ptr<State_Var>> *vars;
		Var_Range(Var_Id::Type type, std::vector<std::unique_ptr<State_Var>> *vars, bool flux_only=false)
			: flux_only(flux_only), vars(vars), type(type) {}
		
		struct Var_Iter {
			Var_Id at;
			bool flux_only;
			std::vector<std::unique_ptr<State_Var>> *vars;
			
			Var_Iter(Var_Id::Type type, std::vector<std::unique_ptr<State_Var>> *vars, bool flux_only) : flux_only(flux_only), vars(vars) {
				at = {type, 0};
				find_next_valid();
			}
			Var_Iter(Var_Id::Type type, std::vector<std::unique_ptr<State_Var>> *vars) {
				at = Var_Id {type, (s32)vars->size()};
				vars = nullptr;
			}
			
			bool operator==(const Var_Iter &other) { return at == other.at;	}
			bool operator!=(const Var_Iter &other) { return at != other.at;	}
			Var_Iter &operator++() {
				++at.id;
				find_next_valid();
				return *this;
			}
			Var_Id operator*() { return at; }
			
			void find_next_valid() {
				if(at.id >= vars->size()) return;
				auto var = (*vars)[at.id].get();
				while(!var->is_valid() || (flux_only && !var->is_flux())) {
					at.id++;
					if(at.id >= vars->size()) break;
					var = (*vars)[at.id].get();
				}
			}
		};
		
		Var_Iter begin() { return Var_Iter(type, vars, flux_only); }
		Var_Iter end()   { return Var_Iter(type, vars); }
	};
	
	Var_Range all(Var_Id::Type type)  { return Var_Range(type,                            &get_vec(type)); }
	Var_Range all_state_vars()        { return Var_Range(Var_Id::Type::state_var,         &state_vars); }
	Var_Range all_fluxes()            { return Var_Range(Var_Id::Type::state_var,         &state_vars, true); }
	Var_Range all_series()            { return Var_Range(Var_Id::Type::series,            &series); }
	Var_Range all_additional_series() { return Var_Range(Var_Id::Type::additional_series, &additional_series); }
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

// TODO: it is a bit wasteful to use a hash map for these first two at all, they could just be indexes into a vector.
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

struct
Indexes {
	bool lookup_ordered = false;
	std::vector<Index_T> indexes;
	Index_T mat_col = invalid_index;
	
	Indexes();
	Indexes(Mobius_Model *model);
	Indexes(Index_T index);
	
	void clear();
	void set_index(Index_T index, bool overwrite = false);
	void add_index(Index_T index);
	void add_index(Entity_Id index_set, s32 idx);
	//Index_T get_index(Entity_Id index_set);
};

struct
Offset_Stride_Code {
	Math_Expr_FT *offset;
	Math_Expr_FT *stride;
	Math_Expr_FT *count;
};

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
	s64 instance_count(Model_Application *app);
	
	// Hmm, we could just store an app pointer here too to avoid passing it all the time.
	
	void check_index_bounds(Model_Application *app, Handle_T, Entity_Id index_set, Index_T index);
	s64 get_offset(Handle_T, Indexes &indexes, Model_Application *app);
	Math_Expr_FT *get_offset_code(Handle_T handle, Index_Exprs &index_exprs, Model_Application *app, Entity_Id &err_idx_set_out);
	
	static const std::string &
	get_handle_name(Model_Application *app, Handle_T handle);
	
	Offset_Stride_Code
	get_special_offset_stride_code(Handle_T handle, Index_Exprs &index_exprs, Model_Application *app);
	
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
	s64 get_offset(Handle_T handle, Indexes &indexes);
	
	const std::vector<Entity_Id> &
	get_index_sets(Handle_T handle);
	
	Math_Expr_FT *
	get_offset_code(Handle_T handle, Index_Exprs &indexes);
	
	Offset_Stride_Code
	get_special_offset_stride_code(Handle_T handle, Index_Exprs &index_exprs);
	
	void
	for_each(Handle_T, const std::function<void(Indexes &, s64)>&);
	
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
	
	Data_Storage<double, Var_Id> &get_storage(Var_Id::Type type) {
		if(type == Var_Id::Type::state_var)         return results;
		if(type == Var_Id::Type::series)            return series;
		if(type == Var_Id::Type::additional_series) return additional_series;
		fatal_error(Mobius_Error::internal, "Unrecognized Var_Id::Type.");
	}
	
	Model_Data *copy(bool copy_results = true);
	Date_Time get_start_date_parameter();
	Date_Time get_end_date_parameter();
};

struct
Run_Batch {
	Entity_Id        solver_id;
	s64              first_ode_offset;
	int              n_ode;
	
	Math_Expr_FT    *run_code;
	batch_function  *compiled_code;
	
	Run_Batch() : run_code(nullptr), solver_id(invalid_entity_id), compiled_code(nullptr) {}
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
	Entity_Id edge_index_set;
	
	bool can_be_located_source;           // if this can be a source of the connection to a located target (not 'out').
	int total_as_source;                  // How many of this type of component appears as a source (both to a located or 'out').
	std::set<Entity_Id> possible_sources; // what sources can have this as a target.
	std::set<Entity_Id> possible_targets; // what targets can have this as a source. (both to a located or 'out').
	int max_target_indexes;               // max indexes of a target that has this as the source
	
	Sub_Indexed_Component() : id(invalid_entity_id), edge_index_set(invalid_entity_id), can_be_located_source(false), max_target_indexes(0), total_as_source(0) {}
};

struct
Connection_Arrow {
	Entity_Id source_id                    = invalid_entity_id;
	Entity_Id target_id                    = invalid_entity_id;
	Indexes   source_indexes;
	Indexes   target_indexes;
	Index_T   edge_index                   = invalid_index;
	
	Connection_Arrow() : source_indexes(), target_indexes() {}
};

struct
Connection_Components {
	std::vector<Sub_Indexed_Component>   components;
	std::vector<Connection_Arrow>        arrows;
};

struct
All_Connection_Components {
	std::vector<Connection_Components>   components;
	
	Connection_Components &operator[](Entity_Id id) {
		if(id.reg_type != Reg_Type::connection || !is_valid(id) || components.size() <= id.id)
			fatal_error(Mobius_Error::internal, "Tried to access connection components with the wrong type of id, or at the wrong time (before they were set up).");
		return components[id.id];
	}
	
	void initialize(Mobius_Model *model) {
		components.resize(model->connections.count());
	}
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
	// TODO: could we find a way of getting rid of this and just using the index_counts structure in the Model_Data ?
	//   In that case it could also be easier to have multiple parent indexes.
	std::vector<std::vector<Index_T>>                        index_counts;
	std::vector<std::unordered_map<std::string, Index_T>>    index_names_map;
	std::vector<std::vector<std::string>>                    index_names;
public :
	
	Unit_Data                                                time_step_unit;
	Time_Step_Size                                           time_step_size;
	
	Var_Registry                                             vars;
	
	Storage_Structure<Entity_Id>                             parameter_structure;
	Storage_Structure<Connection_T>                          connection_structure;
	Storage_Structure<Var_Id>                                result_structure;
	Storage_Structure<Var_Id>                                series_structure;
	Storage_Structure<Var_Id>                                additional_series_structure;
	Storage_Structure<Entity_Id>                             index_counts_structure;
	
	Storage_Structure<Var_Id> &get_storage_structure(Var_Id::Type type) {
		if(type == Var_Id::Type::state_var)         return result_structure;
		if(type == Var_Id::Type::series)            return series_structure;
		if(type == Var_Id::Type::additional_series) return additional_series_structure;
		fatal_error(Mobius_Error::internal, "Unrecognized Var_Id::Type.");
	}
	
	Data_Set                                                *data_set;
	
	All_Connection_Components                                connection_components;
	
	LLVM_Module_Data                                        *llvm_data;
	
	Run_Batch                                                initial_batch;
	std::vector<Run_Batch>                                   batches;
	
	//std::unordered_map<std::string, owns_code>               separate_functions;
	
	bool                                                     is_compiled = false;
	std::vector<Entity_Id>                                   baked_parameters;
	
	void        set_indexes(Entity_Id index_set, std::vector<std::string> &indexes, Index_T parent_idx = invalid_index);
	void        set_index_count(Entity_Id index_set, int count, Index_T parent_idx = invalid_index);
	Index_T     get_max_index_count(Entity_Id index_set);
	Index_T     get_index_count(Entity_Id index_set, Indexes &indexes);
	Index_T     get_index(Entity_Id index_set, const std::string &name);
	std::string get_index_name(Index_T index, bool *is_quotable = nullptr);
	std::string get_possibly_quoted_index_name(Index_T index);
	bool        all_indexes_are_set();
	s64         active_instance_count(const std::vector<Entity_Id> &index_sets); // TODO: consider putting this on the Storage_Structure instead.
	bool        is_in_bounds(Indexes &indexes); // same?
	void        get_index_names_with_edge_naming(Indexes &indexes, std::vector<std::string> &names_out, bool quote);
	
	Math_Expr_FT *
	get_index_count_code(Entity_Id index_set, Index_Exprs &indexes);
	
	Sub_Indexed_Component *find_connection_component(Entity_Id conn_id, Entity_Id comp_id, bool make_error = true);
	Entity_Id              get_single_connection_index_set(Entity_Id conn_id);
	
	void build_from_data_set(Data_Set *data_set);
	void save_to_data_set();
	
	void set_up_parameter_structure(std::unordered_map<Entity_Id, std::vector<Entity_Id>, Hash_Fun<Entity_Id>> *par_group_index_sets = nullptr);
	void set_up_connection_structure();
	void set_up_index_count_structure();
	
	void
	set_up_series_structure(Var_Id::Type type, Series_Metadata *metadata);
	
	// TODO: this one should maybe be on the Model_Data struct instead
	void allocate_series_data(s64 time_steps, Date_Time start_date);
	
	void compile(bool store_code_strings = false);
	
	std::string serialize  (Var_Id id);
	Var_Id      deserialize(const std::string &name);
	
	std::unordered_map<std::string, Var_Id> serial_to_id;
	
	std::string batch_structure;
	std::string batch_code;
	std::string llvm_ir;
};

Entity_Id
avoid_index_set_dependency(Model_Application *app, Var_Loc_Restriction restriction);

// The point of this one is to make a structure that can be used for local computations, this is not the same as the connection_structure, which is used in the model code generation itself.
void
make_connection_component_indexing_structure(Model_Application *app, Storage_Structure<Entity_Id> *components, Entity_Id connection_id);

void
match_regex(Model_Application *app, Entity_Id conn_id, Source_Location source_loc);

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
	
	void copy(Index_Exprs &other) {
		clean();
		indexes = other.indexes;
		for(auto &idx : indexes) {
			if(idx) idx = ::copy(idx);
		}
		if(other.mat_col)
			mat_col = ::copy(other.mat_col);
		mat_index_set = other.mat_index_set;
	}
};

#include "indexing.h"

template<typename Handle_T> s64
Multi_Array_Structure<Handle_T>::instance_count(Model_Application *app) {
	s64 count = 1;
	for(auto &index_set : index_sets)
		count *= (s64)app->get_max_index_count(index_set).index;
	return count;
}

template<> inline const std::string&
Multi_Array_Structure<Entity_Id>::get_handle_name(Model_Application *app, Entity_Id id) {
	return app->model->find_entity(id)->name;
}

template<> inline const std::string&
Multi_Array_Structure<Var_Id>::get_handle_name(Model_Application *app, Var_Id var_id) {
	return app->vars[var_id]->name;
}

template<> inline const std::string &
Multi_Array_Structure<Connection_T>::get_handle_name(Model_Application *app, Connection_T nb) {
	return app->model->connections[nb.connection]->name;
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
Storage_Structure<Handle_T>::get_offset(Handle_T handle, Indexes &indexes) {
	auto array_idx = handle_is_in_array.at(handle);
	return structure[array_idx].get_offset(handle, indexes, parent);
}
	
template<typename Handle_T> Math_Expr_FT *
Storage_Structure<Handle_T>::get_offset_code(Handle_T handle, Index_Exprs &indexes) {
	auto array_idx = handle_is_in_array.at(handle);
	Entity_Id err_idx_set;
	auto code = structure[array_idx].get_offset_code(handle, indexes, parent, err_idx_set);
	if(!code) {
		auto handle_name = Multi_Array_Structure<Handle_T>::get_handle_name(parent, handle);
		fatal_error(Mobius_Error::internal, "A call to get_offset_code() somehow referenced an index that was not properly initialized. The name of the referenced variable was \"", handle_name , "\". The index set was \"", parent->model->index_sets[err_idx_set]->name, "\".");
	}
	return code;
}

template<typename Handle_T> Offset_Stride_Code
Storage_Structure<Handle_T>::get_special_offset_stride_code(Handle_T handle, Index_Exprs &indexes) {
	auto array_idx = handle_is_in_array.at(handle);
	return structure[array_idx].get_special_offset_stride_code(handle, indexes, parent);
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
for_each_helper(Storage_Structure<Handle_T> *self, Handle_T handle, const std::function<void(Indexes &, s64)> &do_stuff, Indexes &indexes, int level) {
	Entity_Id index_set = indexes.indexes[level].index_set;
	
	if(level == indexes.indexes.size()-1) {
		for(int idx = 0; idx < self->parent->get_index_count(index_set, indexes).index; ++idx) {
			indexes.indexes[level].index = idx;
			s64 offset = self->get_offset(handle, indexes);
			do_stuff(indexes, offset);
		}
	} else {
		for(int idx = 0; idx < self->parent->get_index_count(index_set, indexes).index; ++idx) {
			indexes.indexes[level].index = idx;
			for_each_helper(self, handle, do_stuff, indexes, level+1);
		}
	}
}

template<typename Handle_T> void
Storage_Structure<Handle_T>::for_each(Handle_T handle, const std::function<void(Indexes &, s64)> &do_stuff) {
	auto array_idx   = handle_is_in_array.at(handle);
	auto &index_sets = structure[array_idx].index_sets;
	Indexes indexes;
	if(index_sets.empty()) {
		s64 offset = get_offset(handle, indexes);
		do_stuff(indexes, offset);
		return;
	}
	for(int pos = 0; pos < index_sets.size(); ++pos) indexes.add_index( Index_T { index_sets[pos], 0 } );
	for_each_helper(this, handle, do_stuff, indexes, 0);
}



#endif // MOBIUS_MODEL_APPLICATION_H