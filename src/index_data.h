
// TODO: Should implement something like this to share functionality between Model_Application and Data_Set

#ifndef MOBIUS_INDEX_DATA_H
#define MOBIUS_INDEX_DATA_H

#include <functional>

#include "common_types.h"
#include "lexer.h" // For declaration of Token

struct
Index_T {
	Entity_Id index_set;
	s32       index;
	
	static constexpr Index_T no_index() { return Index_T { Entity_Id::invalid(), -1 };  }
	
	Index_T& operator++() { index++; return *this; }
};

//TODO: should we do sanity check on the index_set in the order comparison operators?
inline bool operator<(const Index_T &a, const Index_T &b) {	return a.index < b.index; }
inline bool operator>(const Index_T &a, const Index_T &b) {	return a.index > b.index; }
inline bool operator>=(const Index_T &a, const Index_T &b) { return a.index >= b.index; }
inline bool operator<=(const Index_T &a, const Index_T &b) { return a.index <= b.index; }
inline bool operator==(const Index_T &a, const Index_T &b) { return a.index_set == b.index_set && a.index == b.index; }
inline bool operator!=(const Index_T &a, const Index_T &b) { return a.index_set != b.index_set || a.index != b.index; }

inline bool is_valid(const Index_T &index) { return is_valid(index.index_set) && index.index >= 0; }


struct Index_Data;
struct Catalog;

struct
Indexes {
	
	Indexes();
	Indexes(Catalog *catalog);
	Indexes(Index_T index);
	
	void clear();
	s64  count();
	void set_index(Index_T index, bool overwrite = false);
	void add_index(Index_T index);
	void add_index(Entity_Id index_set, s32 idx);
	
	Index_T get_index(Index_Data &index_data, Entity_Id index_set);
	
	std::vector<Index_T> indexes; // Should probably have this as private, but it is very inconvenient.
	bool lookup_ordered = false;
	
private :
	Index_T get_index_base(Entity_Id index_set);
};


inline bool
operator==(const Indexes &a, const Indexes &b) {
	if(a.lookup_ordered != b.lookup_ordered) return false;   // TODO: We could maybe make them comparable, but there doesn't seem to be a use case
	if(a.indexes != b.indexes) return false;
	return true;
}

struct
Index_Record {
	enum class Type {
		none = 0,
		numeric1,
		named,
	}     type = Type::none;
	
	std::vector<s32>                                    index_counts;
	std::vector<std::vector<std::string>>               index_names;
	std::vector<std::unordered_map<std::string, s32>>   name_to_index;
	
	bool has_index_position_map = false;
	std::vector<double> pos_vals;
	s32 map_index(double value);
};

struct
Index_Data { 
	
	Catalog *catalog;
	
	Index_Data(Catalog *catalog) : catalog(catalog) {}
	
	void set_indexes(Entity_Id index_set, const std::vector<Token> &names, Index_T parent_idx = Index_T::no_index());
	void initialize_union(Entity_Id index_set_id, Source_Location source_loc);
	
	void set_position_map(Entity_Id index_set_id, std::vector<double> &pos_vals, Source_Location &source_loc);
	
	void find_index(Entity_Id index_set, Token *idx_name, Indexes &indexes_out);
	void find_indexes(const std::vector<Entity_Id> &index_sets, std::vector<Token> &idx_names, Indexes &indexes_out);
	Index_T find_index(Entity_Id index_set, Token *idx_name, Index_T index_of_super = Index_T::no_index()); // Ideally we shouldn't expose this one, but it is needed once in the Data_Set
	
	bool are_in_bounds(Indexes &indexes);
	
	Index_T get_max_count(Entity_Id index_set);
	Index_T get_index_count(Indexes &indexes, Entity_Id index_set);
	
	void check_valid_distribution(std::vector<Entity_Id> &index_sets, Source_Location source_loc);
	s64 get_instance_count(const std::vector<Entity_Id> &index_sets);
	
	std::string get_index_name(Indexes &indexes, Index_T index, bool *is_quotable = nullptr);
	std::string get_possibly_quoted_index_name(Indexes &indexes, Index_T index, bool quote = true);
	void get_index_names(Indexes &indexes, std::vector<std::string> &names_out, bool quote = false);


	void initialize_edge_index_set(Entity_Id index_set_id, Source_Location source_loc);
	void add_edge_index(Entity_Id index_set_id, const std::string &index_name, Source_Location source_loc, Index_T parent_idx);
	
	bool are_all_indexes_set(Entity_Id index_set);
	
	void write_index_to_file(FILE *file, Index_T index, Index_T parent_idx = Index_T::no_index());
	void write_indexes_to_file(FILE *file, Entity_Id index_set, Index_T parent_idx = Index_T::no_index());
	
	bool can_be_sub_indexed_to(Entity_Id parent_set, Entity_Id other_set, s32* offset = nullptr);
	
	Index_Record::Type get_index_type(Entity_Id index_set_id);
	
	void transfer_data(Index_Data &other, Entity_Id index_set_id);
	
	void for_each(
		std::vector<Entity_Id> &index_sets, 
		const std::function<void(Indexes &indexes)> &do_stuff,
		const std::function<void(int)> &new_level = [](int){}
		);
		
	Index_T raise(Index_T member_idx, Entity_Id union_set);
private :
	
	std::vector<Index_Record> index_data;
	
	Index_T find_index_base(Entity_Id index_set, Token *idx_name, Index_T index_of_super = Index_T::no_index());
	s32   get_count_base(Entity_Id index_set, Index_T index_of_super = Index_T::no_index());
	std::string get_index_name_base(Index_T index, Index_T index_of_super, bool *is_quotable);
	
	void initialize(Entity_Id index_set_id, Index_T parent_idx, Index_Record::Type type, Source_Location source_loc);
	
	Index_T lower(Index_T union_index, Index_T parent_idx);
	
	void for_each_helper(
		const std::function<void(Indexes &indexes)> &do_stuff,
		const std::function<void(int)> &new_level,
		Indexes &indexes,
		int pos);
};

inline void
maybe_quote(std::string &str, bool quote) {
	if(quote)
		str = "\"" + str + "\"";
}

#endif // MOBIUS_INDEX_DATA_H




