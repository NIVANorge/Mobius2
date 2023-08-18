
// TODO: Should implement something like this to share functionality between Model_Application and Data_Set

#ifndef MOBIUS_INDEX_DATA_H
#define MOBIUS_INDEX_DATA_H

template<typename Id_Type>
struct
Index_Type {
	Id_Type index_set;
	s32     index;
	
	Index_Type() : index(-1);
};

template<typename Id_Type>
is_valid(Index_Type<Id_Type> index) { return index.index >= 0; }


// typedef Index_Type<Entity_Id> Index_T;

template<typename Record_Type, typename Id_Type>
struct
Index_Tuple {
	
	typedef Index_Type<Id_Type> Idx_T;
	
	// TODO: This should be what Indexes in model_application is now.
};

// typedef Indexes Index_Tuple<Entity_Id>;  //etc.

template<typename Id_Type>
s64 get_id_of(Id_Type id);
template<>
s64 get_id_of<Entity_Id>(Entity_Id id) { return id.id; }
template<>
s64 get_id_of<int>(int id) { return id; }

template<typename Record_Type, typename Id_Type>
struct
Index_Data {
	
	Record_Type *record;
	
	Index_Data(Record_Type *record) : record(record) {}
	
	typedef Index_Type<Id_Type> Idx_T;
	
	void set_indexes(Id_Type index_set, std::vector<Token> &names, Idx_T parent_idx = Idx_T());
	void set_indexes(Id_Type index_set, s64 count, Idx_T parent_idx = Idx_T());
	
	Idx_T get_index(Id_Type index_set, Token *idx_name); //Only valid if not sub-indexed. //, Idx_T parent_idx = invalid_index<Idx_T>);
	get_indexes(std::vector<Id_Type> &index_sets, std::vector<Token> &idx_names, Index_Tuple<Idx_T> &indexes_out);
	get_indexes(std::vector<Id_Type> &index_sets, const char **idx_names, Index_Tuple<Idx_T> &indexes_out);  //Needed for mobipy
	
	bool are_in_bounds(Index_Tuple<Idx_T> &indexes);
	
	Idx_T get_max_count(Id_Type index_set);
	s64 get_instance_count(Index_Tuple<Idx_T> &indexes);
	
	std::string get_index_name(Idx_T index);  // Only valid if not sub-indexed. Or maybe just returns integer if sub-indexed (for use in MobiView2 for now)
	void get_index_names_with_edge_naming(Index_Tuple<Idx_T> &indexes, std::vector<std::string> &names_out, bool quote);
	
	// TODO: Some method to copy to another Index_Data.
private :
	
	std::vector<Index_Data_Record> index_data;
	
	struct
	Index_Data_Record {
		enum class Type {
			none = 0,
			numeric,
			named,
		}     type = Type::none;
		
		std::vector<Idx_T>                                  index_counts;
		std::vector<std::vector<std::string>>               index_names;
		std::vector<std::unordered_map<std::string, Idx_T>> name_to_index;
	};
};



template<typename Record_Type, typename Id_Type>
void 
Index_Data<Record_Type, Id_Type>::set_indexes(Id_Type index_set_id, std::vector<Token> &names, Idx_T parent_idx) {
	
	s64 id = get_id_of(index_set_id);
	if(id >= index_data.size())
		index_data.resize(id+1);
	
	s32 super = 0;
	if(is_valid(parent_idx)) {
		super = parent_idx.index;
	
	auto index_set = record->index_sets[index_set_id];
	if(!index_set->union_of.empty())
		fatal_error(Mobius_Error::internal, "Tried to explicitly set indexes for a union index set.");
	if(index_set->sub_indexed_to != parent_idx.index_set)
		fatal_error(Mobius_Error::internal, "Tried to set non-sub-indexed indexes for a sub-indexed index set or the other way around.");
	
	s32 count = index_counts[super].index;
	auto &names = index_names[super];
	auto &name_map = index_names[super];
	
	if(count != 0)
		fatal_error(Mobius_Error::internal, "Trying to set indexes for the same index set twice.");
	
	// TODO: Finish!
}


#endif // MOBIUS_INDEX_DATA_H
