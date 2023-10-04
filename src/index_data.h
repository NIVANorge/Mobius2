
// TODO: Should implement something like this to share functionality between Model_Application and Data_Set

#ifndef MOBIUS_INDEX_DATA_H
#define MOBIUS_INDEX_DATA_H

#include <functional>

template<typename Id_Type>
struct
Index_Type {
	Id_Type index_set;
	s32     index;
	
	static constexpr Index_Type no_index() { return Index_Type { Id_Type::invalid(), -1 };  }
	
	Index_Type& operator++() { index++; return *this; }
};

//TODO: should we do sanity check on the index_set in the order comparison operators?
template<typename Id_Type>
inline bool operator<(const Index_Type<Id_Type> &a, const Index_Type<Id_Type> &b) {	return a.index < b.index; }
template<typename Id_Type>
inline bool operator>=(const Index_Type<Id_Type> &a, const Index_Type<Id_Type> &b) { return a.index >= b.index; }
template<typename Id_Type>
inline bool operator==(const Index_Type<Id_Type> &a, const Index_Type<Id_Type> &b) { return a.index_set == b.index_set && a.index == b.index; }
template<typename Id_Type>
inline bool operator!=(const Index_Type<Id_Type> &a, const Index_Type<Id_Type> &b) { return a.index_set != b.index_set || a.index != b.index; }

template<typename Id_Type>
inline bool is_valid(const Index_Type<Id_Type> &index) { return is_valid(index.index_set) && index.index >= 0; }

template<typename Id_Type>
struct
Index_Data;


template<typename Id_Type>
struct
Index_Tuple {
	
	typedef Index_Type<Id_Type> Idx_T;
	
	Index_Tuple();
	Index_Tuple(Record_Type<Id_Type> *record);
	Index_Tuple(Idx_T index);
	
	void clear();
	s64  count();
	void set_index(Idx_T index, bool overwrite = false);
	void add_index(Idx_T index);
	void add_index(Id_Type index_set, s32 idx);
	Idx_T get_index(Index_Data<Id_Type> &index_data, Id_Type index_set);
	
	std::vector<Idx_T> indexes; // Should probably have this as private, but it is very inconvenient.
	bool lookup_ordered = false;
	
private :
	Idx_T get_index_base(Id_Type index_set);
};

template<typename Id_Type>
inline bool
operator==(const Index_Tuple<Id_Type> &a, const Index_Tuple<Id_Type> &b) {
	if(a.lookup_ordered != b.lookup_ordered) return false;   // TODO: We could maybe make them comparable, but there doesn't seem to be a use case
	if(a.indexes != b.indexes) return false;
	return true;
}

struct Model_Application;

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
};


template<typename Id_Type>
struct
Index_Data { 
	
	Record_Type<Id_Type> *record;
	
	Index_Data(Record_Type<Id_Type> *record) : record(record) {}
	
	typedef Index_Type<Id_Type> Idx_T;
	
	void set_indexes(Id_Type index_set, const std::vector<Token> &names, Idx_T parent_idx = Idx_T::no_index());
	void initialize_union(Id_Type index_set_id, Source_Location source_loc);
	
	void find_index(Id_Type index_set, Token *idx_name, Index_Tuple<Id_Type> &indexes_out);
	void find_indexes(const std::vector<Id_Type> &index_sets, std::vector<Token> &idx_names, Index_Tuple<Id_Type> &indexes_out);
	Idx_T find_index(Id_Type index_set, Token *idx_name, Idx_T index_of_super = Idx_T::no_index()); // Ideally we shouldn't expose this one, but it is needed once in the Data_Set
	
	bool are_in_bounds(Index_Tuple<Id_Type> &indexes);
	
	Idx_T get_max_count(Id_Type index_set);
	Idx_T get_index_count(Index_Tuple<Id_Type> &indexes, Id_Type index_set);
	
	void check_valid_distribution(std::vector<Id_Type> &index_sets, Source_Location source_loc);
	s64 get_instance_count(const std::vector<Id_Type> &index_sets);
	
	std::string get_index_name(Index_Tuple<Id_Type> &indexes, Idx_T index, bool *is_quotable = nullptr);
	std::string get_possibly_quoted_index_name(Index_Tuple<Id_Type> &indexes, Idx_T index, bool quote = true);
	void get_index_names(Index_Tuple<Id_Type> &indexes, std::vector<std::string> &names_out, bool quote = false);


	void initialize_edge_index_set(Id_Type index_set_id, Source_Location source_loc);
	void add_edge_index(Id_Type index_set_id, const std::string &index_name, Source_Location source_loc, Idx_T parent_idx);
	
	bool are_all_indexes_set(Id_Type index_set);
	
	void write_index_to_file(FILE *file, Idx_T index, Idx_T parent_idx = Idx_T::no_index());
	void write_indexes_to_file(FILE *file, Id_Type index_set, Idx_T parent_idx = Idx_T::no_index());
	
	bool can_be_sub_indexed_to(Id_Type parent_set, Id_Type other_set, s32* offset = nullptr);
	
	Index_Record::Type get_index_type(Id_Type index_set_id);
	
	void transfer_data(Index_Data<Entity_Id> &other, Id_Type index_set_id);
	
	template <typename Id_Type2> friend class Index_Data;
	
	void for_each(
		std::vector<Id_Type> &index_sets, 
		const std::function<void(Index_Tuple<Id_Type> &indexes)> &do_stuff,
		const std::function<void(int)> &new_level = [](int){}
		);
private :
	
	std::vector<Index_Record> index_data;
	
	Idx_T find_index_base(Id_Type index_set, Token *idx_name, Idx_T index_of_super = Idx_T::no_index());
	s32   get_count_base(Id_Type index_set, Idx_T index_of_super = Idx_T::no_index());
	std::string get_index_name_base(Idx_T index, Idx_T index_of_super, bool *is_quotable);
	
	void initialize(Id_Type index_set_id, Idx_T parent_idx, Index_Record::Type type, Source_Location source_loc);
	
	Idx_T lower(Idx_T union_index, Idx_T parent_idx);
	
	void for_each_helper(
		const std::function<void(Index_Tuple<Id_Type> &indexes)> &do_stuff,
		const std::function<void(int)> &new_level,
		Index_Tuple<Id_Type> &indexes,
		int pos);
};


template<typename Id_Type>
Index_Tuple<Id_Type>::Index_Tuple(Record_Type<Id_Type> *record) {
	lookup_ordered = false;
	if(record)  // NOTE: We need the check model!=0 since some places we need to construct an Indexed_Par before the model is created. TODO: Could we avoid that somehow?
		indexes.resize(record->index_sets.count(), Idx_T::no_index());
}

template<typename Id_Type>
Index_Tuple<Id_Type>::Index_Tuple() {
	lookup_ordered = true;
}

template<typename Id_Type>
Index_Tuple<Id_Type>::Index_Tuple(Idx_T index) {
	lookup_ordered = true;
	add_index(index);
}

template<typename Id_Type>
void
Index_Tuple<Id_Type>::clear() {
	if(lookup_ordered)
		indexes.clear();
	else {
		for(auto &index : indexes)
			index = Idx_T::no_index();
	}
}

template<typename Id_Type>
s64
Index_Tuple<Id_Type>::count() {
	if(lookup_ordered)
		return indexes.size();
	else {
		s64 result = 0;
		for(auto &index : indexes) result += is_valid(index);
		return result;
	}
}

template<typename Id_Type>
void
Index_Tuple<Id_Type>::set_index(Idx_T index, bool overwrite) {
	if(!is_valid(index.index_set))
		fatal_error(Mobius_Error::internal, "Tried to set an invalid index set on an Indexes");
	if(!lookup_ordered) {
		if(!overwrite && is_valid(indexes[index.index_set.id])) {
			fatal_error(Mobius_Error::internal, "Got duplicate matrix column index for an Indexes.");
		} else
			indexes[index.index_set.id] = index;
	} else
		fatal_error(Mobius_Error::internal, "Using set_index on an Indexes that is lookup_ordered");
}

template<typename Id_Type>
void
Index_Tuple<Id_Type>::add_index(Idx_T index) {
	if(!is_valid(index))
		fatal_error(Mobius_Error::internal, "Tried to set an invalid index on an Indexes");
	if(lookup_ordered)
		indexes.push_back(index);
	else
		fatal_error(Mobius_Error::internal, "Using add_index on an Indexes that is not lookup_ordered");
}

template<typename Id_Type>
void
Index_Tuple<Id_Type>::add_index(Id_Type index_set, s32 idx) {
	add_index(Idx_T { index_set, idx } );
}

template<typename Id_Type>
Index_Type<Id_Type>
Index_Tuple<Id_Type>::get_index_base(Id_Type index_set_id) {
	// TODO: if we are lookup_ordered and we want a matrix_column, should we check for the second instance of the index set?
	if(lookup_ordered) {
		for(auto index : indexes) {
			if(index.index_set == index_set_id)
				return index;
		}
		return Idx_T::no_index();
	} else
		return indexes[index_set_id.id];
}

template<typename Id_Type>
Index_Type<Id_Type>
Index_Tuple<Id_Type>::get_index(Index_Data<Id_Type> &index_data, Id_Type index_set_id) {
		
	auto result = get_index_base(index_set_id);
	if(is_valid(result))
		return result;
	
	// Try to index a union index set (if a direct index is absent) by an index belonging to a member of that union.
	result.index_set = index_set_id;
	result.index = 0;
	bool found = false;
	auto index_set = index_data.record->index_sets[index_set_id];
	for(auto ui_id : index_set->union_of) {
		auto idx = get_index_base(ui_id);
		if(is_valid(idx)) {
			found = true;
			result.index += idx.index;
			break;
		} else
			result.index += index_data.get_index_count(*this, ui_id).index;
	}
	if(!found)
		return Idx_T::no_index();
	return result;
}

template<typename Id_Type>
void
Index_Data<Id_Type>::initialize(Id_Type index_set_id, Idx_T parent_idx, Index_Record::Type type, Source_Location source_loc) {
	s64 id = index_set_id.id;
	if(id >= index_data.size())
		index_data.resize(id+1);
	
	auto &data = index_data[id];
	
	s32 super = is_valid(parent_idx) ? parent_idx.index : 0;
	
	auto index_set = record->index_sets[index_set_id];
	if(!index_set->union_of.empty()) {
		source_loc.print_error_header();
		fatal_error("Tried to explicitly set indexes for a union index set.");
	}
	
	if(
		 (is_valid(index_set->sub_indexed_to) != is_valid(parent_idx))
	  || (is_valid(parent_idx) && (parent_idx.index_set != index_set->sub_indexed_to))
	) {
		source_loc.print_error_header();
		fatal_error("Tried to set non-sub-indexed indexes for a sub-indexed index set or the other way around.");
	}
	
	s32 instance_count = 1;
	if(is_valid(index_set->sub_indexed_to)) {
		if(!are_all_indexes_set(index_set->sub_indexed_to))
			fatal_error(Mobius_Error::internal, "Somehow a parent index set was not initialized before we tried to set data of an index set that was sub-indexed to it.");
		instance_count = get_max_count(index_set->sub_indexed_to).index;
	}
	
	if(data.index_counts.empty())
		data.index_counts.resize(instance_count, 0);
	
	s32 count = data.index_counts[super];
	if(count != 0) {
		source_loc.print_error_header();
		fatal_error("Trying to set indexes for the same index set instance \"", index_set->name, "\" twice.");
	}

	if(data.type == Index_Record::Type::none) {
		data.type = type;
		if (type == Index_Record::Type::named && data.index_names.empty()) {
			data.name_to_index.resize(instance_count);
			data.index_names.resize(instance_count);
		}
	}
}

template<typename Id_Type>
void
Index_Data<Id_Type>::initialize_edge_index_set(Id_Type index_set_id, Source_Location source_loc) {
	
	auto parent_id = record->index_sets[index_set_id]->sub_indexed_to;
	if(!is_valid(parent_id))
		fatal_error(Mobius_Error::internal, "Got an edge index set that is not sub-indexed to a component index set.");
	
	s32 count = get_count_base(parent_id, Idx_T::no_index());
	for(s32 par_idx = 0; par_idx < count; ++par_idx) {
		Idx_T parent_idx = Idx_T { parent_id, par_idx };
		initialize(index_set_id, parent_idx, Index_Record::Type::named, source_loc);
	}
	
}

template<typename Id_Type>
void
Index_Data<Id_Type>::add_edge_index(Id_Type index_set_id, const std::string &index_name, Source_Location source_loc, Idx_T parent_idx) {
	
	// NOTE: This is only safe while the index set is being constructed, not later.
	
	// TODO: We should check for name clashes?
	// Or in the data_set, it should check that there are no double edges?
	
	auto &data = index_data[index_set_id.id];
	
	int super = is_valid(parent_idx) ? parent_idx.index : 0;
	
	s32 val = data.index_counts[super]++;
	data.index_names[super].push_back(index_name);
	data.name_to_index[super][index_name] = val;
}

template<typename Id_Type>
void 
Index_Data<Id_Type>::set_indexes(Id_Type index_set_id, const std::vector<Token> &names, Idx_T parent_idx) {
	
	Index_Record::Type type;
	if(names[0].type == Token_Type::integer)
		type = Index_Record::Type::numeric1;
	else if(names[0].type == Token_Type::quoted_string)
		type = Index_Record::Type::named;
	else
		fatal_error(Mobius_Error::internal, "Unhandled token type for setting index data.");
	
	initialize(index_set_id, parent_idx, type, names[0].source_loc);
	
	auto &data = index_data[index_set_id.id];
	
	int super = is_valid(parent_idx) ? parent_idx.index : 0;
	
	s32  &count = data.index_counts[super];
	if(data.type == Index_Record::Type::numeric1) {
		if(names.size() > 1) {
			names[0].print_error_header();
			fatal_error("Got more than one value for index set size.");
		}
		if(names[0].type != Token_Type::integer) {
			names[0].print_error_header();
			fatal_error("Got a non-numeric data type for an index set that was already designated as numeric.");
		}
		count = names[0].val_int;
		if(count < 1) {
			names[0].print_error_header();
			fatal_error("You can only have a positive number for a dimension size.");
		}
	} else if (data.type == Index_Record::Type::named) {
		count = names.size();
		auto &inames = data.index_names[super];
		auto &nmap   = data.name_to_index[super];
		
		auto index = Idx_T {index_set_id, 0};
		for(auto &name : names) {
			if(name.type != Token_Type::quoted_string) {
				name.print_error_header();
				fatal_error("Expected just quoted strings for this index data.");
			}
			std::string nn = name.string_value;
			if(nmap.find(nn) != nmap.end()) {
				name.print_error_header();
				fatal_error("The index name \"", nn, "\" is repeated for the index set \"", record->index_sets[index_set_id]->name, "\"");
			}
			inames.push_back(nn);
			nmap[nn] = index.index;
			++index.index;
		}
	} else
		fatal_error(Mobius_Error::internal, "Unhandled index type in set_indexes.");
}

template<typename Id_Type>
void
Index_Data<Id_Type>::initialize_union(Id_Type index_set_id, Source_Location source_loc) {
	
	s64 id = index_set_id.id;
	if(id >= index_data.size())
		index_data.resize(id+1);
	
	auto &data = index_data[id];
	
	auto index_set = record->index_sets[index_set_id];
	if(index_set->union_of.empty()) {
		source_loc.print_error_header(Mobius_Error::internal);
		fatal_error("Tried to initialize a non-union index set as a union.");
	}
	
	for(auto ui_id : index_set->union_of) {
		auto type = index_data[ui_id.id].type;
		if(data.type == Index_Record::Type::none)
			data.type = type;
		if(type != data.type) {
			source_loc.print_error_header();
			fatal_error("It is not supported to have a union of index sets that have different index name type (e.g. numeric vs named)");
		}
	}
	
	// If these are named, check for overlapping names between them!
	if(data.type == Index_Record::Type::named) {
		if(is_valid(index_set->sub_indexed_to))
			fatal_error(Mobius_Error::internal, "Sub-indexed unions not entirely supported.");
		int super = 0; // NOTE: Have to iterate over this if we are to allow sub-indexed unions
		for(int idx = 1; idx < index_set->union_of.size(); ++idx) {
			for(int idx2 = 0; idx2 < idx; ++idx2) {
				
				auto ui_id1 = index_set->union_of[idx];
				auto ui_id2 = index_set->union_of[idx2];
				auto &names1 = index_data[ui_id1.id].index_names[super];
				auto &names2 = index_data[ui_id2.id].index_names[super];
				
				for(auto &name : names1) {
					if(std::find(names2.begin(), names2.end(), name) != names2.end()) {
						source_loc.print_error_header();
						fatal_error("The index name \"", name, "\" overlaps between the two union members \"", record->index_sets[ui_id1]->name, "\" and \"", record->index_sets[ui_id2]->name, "\" in the index set union \"", index_set->name, "\".");
					}
				}
			}
		}
	}
	
}

template<typename Id_Type>
s32
Index_Data<Id_Type>::get_count_base(Id_Type index_set_id, Idx_T index_of_super) {
	auto index_set = record->index_sets[index_set_id];
	if(is_valid(index_set->sub_indexed_to) && !is_valid(index_of_super))
		fatal_error(Mobius_Error::internal, "Wrong use of get_count_base.");
	if(!index_set->union_of.empty()) {
		s32 sum = 0;
		for(auto ui_id : index_set->union_of)
			sum += get_count_base(ui_id, index_of_super);
		return sum;
	}
	int super = is_valid(index_of_super) ? index_of_super.index : 0;
	
	if(index_data.size() <= index_set_id.id)
		fatal_error(Mobius_Error::internal, "Tried to look up count of uninitialized index set ", index_set->name, "\n");
	if(index_data[index_set_id.id].index_counts.size() <= super)
		fatal_error(Mobius_Error::internal, "Index counts were not properly set before a lookup, or the lookup was malformed.");
	return index_data[index_set_id.id].index_counts[super];
}

template<typename Id_Type>
Index_Type<Id_Type>
Index_Data<Id_Type>::find_index_base(Id_Type index_set_id, Token *idx_name, Idx_T index_of_super) {
	
	auto &data = index_data[index_set_id.id];
	int super = is_valid(index_of_super) ? index_of_super.index : 0;
	
	if(idx_name->type == Token_Type::quoted_string) {
		if(data.type != Index_Record::Type::named)
			return Idx_T::no_index();
		auto &nmap = data.name_to_index[super];
		auto find = nmap.find(idx_name->string_value);
		if(find == nmap.end())
			return Idx_T::no_index();
		return Idx_T { index_set_id, find->second };
	} else if (idx_name->type == Token_Type::integer) {
		Idx_T result = Idx_T { index_set_id, (s32)idx_name->val_int };
		if(result.index < 0 || result.index >= get_count_base(index_set_id, index_of_super))
			return Idx_T::no_index();
		return result;
	}
	
	return Idx_T::no_index();
}

template<typename Id_Type>
Index_Type<Id_Type>
Index_Data<Id_Type>::find_index(Id_Type index_set_id, Token *idx_name, Idx_T index_of_super) {
	auto &data = index_data[index_set_id.id];
	
	Idx_T result = Idx_T::no_index();
	auto index_set = record->index_sets[index_set_id];
	
	if(is_valid(index_set->sub_indexed_to) && !is_valid(index_of_super))
		fatal_error(Mobius_Error::internal, "Not properly setting a parent index when trying to look up index data for a sub-indexed index set.");
	
	if(!index_set->union_of.empty() && data.type == Index_Record::Type::named) {
		if(is_valid(index_set->sub_indexed_to))
			fatal_error(Mobius_Error::internal, "Sub-indexed unions are not implemented");
		
		s32 sum = 0;
		bool found = false;
		for(auto ui_id : index_set->union_of) {
			Idx_T find = find_index_base(ui_id, idx_name);
			if(is_valid(find)) {
				sum += find.index;
				found = true;
				break;
			} else
				sum += get_max_count(ui_id).index;
		}
		if(found)
			result = Idx_T { index_set_id, sum };
	} else
		result = find_index_base(index_set_id, idx_name, index_of_super);
	
	if(!is_valid(result)) {
		idx_name->print_error_header();
		fatal_error("This is not a valid index for the index set \"", index_set->name, "\".");
	}
	
	return result;
}

template<typename Id_Type>
void
Index_Data<Id_Type>::find_index(Id_Type index_set_id, Token *idx_name, Index_Tuple<Id_Type> &indexes_out) {
	
	auto index_set = record->index_sets[index_set_id];
	
	Idx_T index_of_super = Idx_T::no_index();
	if(is_valid(index_set->sub_indexed_to)) {
		index_of_super = indexes_out.get_index(*this, index_set->sub_indexed_to);

		if(!is_valid(index_of_super)) {
			idx_name->print_error_header();
			fatal_error("(find_indexes) This index belongs to an index set ", index_set->name, " that is sub-indexed to another index set ",
			record->index_sets[index_set->sub_indexed_to]->name, ", but this index does not appear after an index of the parent index set.");
		}
	}
	auto index = find_index(index_set_id, idx_name, index_of_super);
	indexes_out.add_index(index);
}

template<typename Id_Type>
void 
Index_Data<Id_Type>::find_indexes(const std::vector<Id_Type> &index_sets, std::vector<Token> &idx_names, Index_Tuple<Id_Type> &indexes_out) {
	
	// TODO: Assert index_sets and idx_names are the same size?
	
	indexes_out.clear();
	
	for(int pos = 0; pos < idx_names.size(); ++pos)
		find_index(index_sets[pos], &idx_names[pos], indexes_out);
}

template<typename Id_Type>
bool
Index_Data<Id_Type>::are_in_bounds(Index_Tuple<Id_Type> &indexes) {
	
	for(int pos = 0; pos < indexes.indexes.size(); ++pos) {
		auto index = indexes.indexes[pos];
		
		if(!is_valid(index.index_set)) continue;
		if(index.index < 0) return false;
		
		auto index_set = record->index_sets[index.index_set];
		
		Idx_T index_of_super = Idx_T::no_index();
		if(is_valid(index_set->sub_indexed_to)) {
			index_of_super = indexes.get_index(*this, index_set->sub_indexed_to);
			
			if(!is_valid(index_of_super)) {
				fatal_error(Mobius_Error::internal, "(are_in_bounds) This index (position ", pos, ") belongs to an index set ", index_set->name, " that is sub-indexed to another index set ",
				record->index_sets[index_set->sub_indexed_to]->name, ", but this index does not appear after an index of the parent index set.");
			}
		}
		s32 count = get_count_base(index.index_set, index_of_super);
		
		if(index.index >= count)
			return false;
	}
	return true;
}

template<typename Id_Type>
Index_Type<Id_Type>
Index_Data<Id_Type>::get_max_count(Id_Type index_set_id) {
	auto index_set = record->index_sets[index_set_id];
	if(is_valid(index_set->sub_indexed_to)) {
		Idx_T result = {index_set_id, 0};
		s32 parent_count = get_count_base(index_set->sub_indexed_to);
		for(Idx_T parent_idx = { index_set->sub_indexed_to, 0 }; parent_idx.index < parent_count; ++parent_idx.index)
			result.index = std::max(result.index, get_count_base(index_set_id, parent_idx));
		return result;
	}
	return Idx_T {index_set_id, get_count_base(index_set_id)};
}

template<typename Id_Type>
Index_Type<Id_Type>
Index_Data<Id_Type>::get_index_count(Index_Tuple<Id_Type> &indexes, Id_Type index_set_id) {
	
	auto index_set = record->index_sets[index_set_id];
	
	Idx_T index_of_super = Idx_T::no_index();
	if(is_valid(index_set->sub_indexed_to)) {
		index_of_super = indexes.get_index(*this, index_set->sub_indexed_to);
		
		if(!is_valid(index_of_super)) {
			begin_error(Mobius_Error::internal);
			error_print("(get_index_count) This index belongs to an index set ", index_set->name, " that is sub-indexed to another index set ",
				record->index_sets[index_set->sub_indexed_to]->name, ", but this index does not appear after an index of the parent index set. Tuple is: ");
			for(auto &index : indexes.indexes) {
				if(is_valid(index))
					error_print("\"", record->index_sets[index.index_set]->name, "\" ");
			}
			mobius_error_exit();
		}
	}
	return Idx_T {index_set_id, get_count_base(index_set_id, index_of_super)};
}

template<typename Id_Type>
bool
Index_Data<Id_Type>::can_be_sub_indexed_to(Id_Type parent_set, Id_Type other_set, s32* offset) {
	if(offset) *offset = 0;
	auto index_set = record->index_sets[other_set];
	if(!is_valid(index_set->sub_indexed_to))
		return false;
	if(index_set->sub_indexed_to == parent_set)
		return true;
	auto super = record->index_sets[index_set->sub_indexed_to];
	if(super->union_of.empty())
		return false;
	for(auto ui_id : super->union_of) {
		if(parent_set == ui_id)
			return true;
		else if(offset)
			*offset += get_count_base(ui_id, Idx_T::no_index());
		// NOTE: It is ok to use an invalid parent index in get_count_base here for the following reason: 
		// Even if we allowed union index sets to be sub-indexed, the supposition here is that it is a parent index set, and we don't allow double sub-indexing.
	}
	return false;
}

template<typename Id_Type>
void
Index_Data<Id_Type>::check_valid_distribution(std::vector<Id_Type> &index_sets, Source_Location source_loc) {
	
	int idx = 0;
	for(auto id : index_sets) {
		auto set = record->index_sets[id];
		if(std::find(index_sets.begin(), index_sets.begin()+idx, id) != index_sets.begin()+idx) {
			source_loc.print_error_header();
			fatal_error("The index set \"", set->name, "\" appears twice in the same distribution.");
		}
		if(is_valid(set->sub_indexed_to)) {
			bool found = (std::find(index_sets.begin(), index_sets.begin()+idx, set->sub_indexed_to) != index_sets.begin()+idx);
			auto parent_set = record->index_sets[set->sub_indexed_to];
			if(!found && !parent_set->union_of.empty()) {
				for(auto ui_id : parent_set->union_of) {
					found = (std::find(index_sets.begin(), index_sets.begin()+idx, ui_id) != index_sets.begin()+idx);
					if(found) break;
				}
			}
			if(!found) {
				source_loc.print_error_header();
				fatal_error("The index set \"", set->name, "\" is sub-indexed to another index set \"", record->index_sets[set->sub_indexed_to]->name, "\", but the parent index set (or a union member of it) does not precede it in this distribution.");
			}
		}
		if(!set->union_of.empty()) {
			for(auto ui_id : set->union_of) {
				if(std::find(index_sets.begin(), index_sets.end(), ui_id) != index_sets.end()) {
					source_loc.print_error_header();
					fatal_error("The index set \"", set->name, "\" is a union consisting among others of the index set \"", record->index_sets[ui_id]->name, "\", but both appear in the same distribution.");
				}
			}
		}
		++idx;
	}
	
}

template<typename Id_Type>
s64
Index_Data<Id_Type>::get_instance_count(const std::vector<Id_Type> &index_sets) {
	
	s64 count = 1;
	if(index_sets.empty()) return count;
	
	std::vector<u8> already_counted(index_sets.size(), 0);
	
	for(int pos = 0; pos < index_sets.size(); ++pos) {
		
		if(already_counted[pos]) continue;
		
		auto index_set_id = index_sets[pos];
		
		auto index_set = record->index_sets[index_set_id];
		if(is_valid(index_set->sub_indexed_to)) {
			// NOTE: This algorithm assures that if this index set is sub-indexed, it should already have been processed, and thus skipped (unless the tuple is incorrectly set up).
			fatal_error(Mobius_Error::internal, "(get_instance_count) Got an index set \"", index_set->name, "\" that is sub-indexed to another index set \"",
				record->index_sets[index_set->sub_indexed_to]->name, "\", but in this index sequence, the former doesn't follow the latter or a union member of the latter.");
		}
		
		std::vector<std::pair<Id_Type, s32>> subs;
		for(int pos2 = pos+1; pos2 < index_sets.size(); ++pos2) {
			s32 offset = 0;
			if(can_be_sub_indexed_to(index_set_id, index_sets[pos2], &offset)) {
				already_counted[pos2] = true;
				subs.push_back({index_sets[pos2], offset});
			}
		}
		s32 count0 = get_max_count(index_set_id).index; // NOTE: The assumption from above is that the current index set is not sub-indexed, so get_max_count is safe.
		if(subs.empty())
			count *= count0;
		else {
			s64 sum = 0;
			for(int idx = 0; idx < count0; ++idx) {
				int subcount = 1;
				for(auto &sub : subs)
					subcount *= get_count_base(sub.first, Idx_T {index_set_id, sub.second + idx});
				sum += subcount;
			}
			count *= sum;
		}
	}
	return count;
}

template<typename Id_Type>
std::string
Index_Data<Id_Type>::get_index_name_base(Idx_T index, Idx_T index_of_super, bool *is_quotable) {
	auto &data = index_data[index.index_set.id];
	
	// TODO: Remove this once we fix MobiView2
	auto set = record->index_sets[index.index_set];
	bool invalid_name_support = (is_valid(set->sub_indexed_to) && !is_valid(index_of_super)) || !set->union_of.empty();
	
	if(data.type == Index_Record::Type::numeric1 || invalid_name_support) {
		if(is_quotable) *is_quotable = false;
		return std::to_string(index.index);
	} else if (data.type == Index_Record::Type::named) {
		if(is_quotable) *is_quotable = true;

		int super = is_valid(index_of_super) ? index_of_super.index : 0;
		if(data.index_names.size() <= super)
			fatal_error(Mobius_Error::internal, "Trying to look up uninitialized index names for index set ", record->index_sets[index.index_set]->name, "\".");
		if(data.index_names[super].size() <= index.index)
			fatal_error(Mobius_Error::internal, "Trying to look up uninitialized index name for index set ", record->index_sets[index.index_set]->name, "\" and index ", index.index, ".");
		return data.index_names[super][index.index];
	} else
		fatal_error(Mobius_Error::internal, "Unhandled index type in get_index_name_base.");
}

template<typename Id_Type>
std::string
Index_Data<Id_Type>::get_index_name(Index_Tuple<Id_Type> &indexes, Idx_T index, bool *is_quotable) {
	
	auto index_set = record->index_sets[index.index_set];
	Idx_T index_of_super = Idx_T::no_index();
	
	if(is_valid(index_set->sub_indexed_to)) {
		index_of_super = indexes.get_index(*this, index_set->sub_indexed_to);
		
		if(!is_valid(index_of_super))
			fatal_error(Mobius_Error::internal, "Invalid index tuple in get_index_name.");
	}
	
	if(!is_valid(index) || index.index >= get_count_base(index.index_set, index_of_super))
		fatal_error(Mobius_Error::internal, "Index out of bounds in get_index_name");
	
	auto &data = index_data[index.index_set.id];
	
	if(!index_set->union_of.empty() && data.type == Index_Record::Type::named) {
		Idx_T below = lower(index, index_of_super);
		return get_index_name_base(below, index_of_super, is_quotable);
	}
	
	return get_index_name_base(index, index_of_super, is_quotable);
}

inline void
maybe_quote(std::string &str, bool quote) {
	if(quote)
		str = "\"" + str + "\"";
}

template<typename Id_Type>
std::string
Index_Data<Id_Type>::get_possibly_quoted_index_name(Index_Tuple<Id_Type> &indexes, Idx_T index, bool quote) {
	bool is_quotable;
	std::string result = get_index_name(indexes, index, &is_quotable);
	maybe_quote(result, quote && is_quotable);
	return result;
}

template<typename Id_Type>
void
Index_Data<Id_Type>::get_index_names(Index_Tuple<Id_Type> &indexes, std::vector<std::string> &names_out, bool quote) {
	
	// TODO: Should probably change this to only put the valid ones in the vector
	
	int count = indexes.indexes.size();
	names_out.resize(count);
	
	int pos = 0;
	for(auto &index : indexes.indexes) {
		if(!is_valid(index)) { ++pos; continue; }
		names_out[pos] = get_possibly_quoted_index_name(indexes, index);
		++pos;
	}
}

template<typename Id_Type>
bool
Index_Data<Id_Type>::are_all_indexes_set(Id_Type index_set_id) {
	
	auto index_set = record->index_sets[index_set_id];
	if(!index_set->union_of.empty()) {
		for(auto ui_id : index_set->union_of) {
			if(!are_all_indexes_set(ui_id))
				return false;
		}
		return true;
	}
	if(index_data.size() <= index_set_id.id)
		return false;
	auto &data = index_data[index_set_id.id];
	if(data.index_counts.empty())
		return false;
	//for(s32 count : data.index_counts)
	//	if(count) return true;
	
	return true;
}

template<typename Id_Type>
void
Index_Data<Id_Type>::write_index_to_file(FILE *file, Idx_T index, Idx_T parent_idx) {
	
	auto index_set = record->index_sets[index.index_set];
	
	if(is_valid(index_set->sub_indexed_to) && !is_valid(parent_idx))
		fatal_error(Mobius_Error::internal, "Missing super index in write_index_to_file");
	
	auto &data = index_data[index.index_set.id];
	
	// TODO: could we reuse code between this and get_index_name ?
	if(!index_set->union_of.empty() && data.type == Index_Record::Type::named) {
		Idx_T below = lower(index, parent_idx);
		write_index_to_file(file, below, parent_idx);
		return;
	}
	
	int super = is_valid(parent_idx) ? parent_idx.index : 0;
	
	if(data.type == Index_Record::Type::named) {
		if(super >= data.index_names.size() || index.index >= data.index_names[super].size())
			fatal_error(Mobius_Error::internal, "Looked up non-existing name for index set ", index_set->name.data(), ", index: ", index.index, ", super: ", super, ".");
		fprintf(file, "\"%s\" ", data.index_names[super][index.index].data());
	} else if (data.type == Index_Record::Type::numeric1)
		fprintf(file, "%d ", index.index);
	else
		fatal_error(Mobius_Error::internal, "Unhandled index type in write_index_to_file().");
}

template<typename Id_Type>
void
Index_Data<Id_Type>::write_indexes_to_file(FILE *file, Id_Type index_set, Idx_T parent_idx) {
	
	auto set = record->index_sets[index_set];
	if((is_valid(set->sub_indexed_to) && !is_valid(parent_idx))
		|| !set->union_of.empty())
		fatal_error(Mobius_Error::internal, "Misuse of write_indexes_to_file");
	
	auto &data = index_data[index_set.id];
	
	int super = is_valid(parent_idx) ? parent_idx.index : 0;
	
	if(data.type == Index_Record::Type::named) {
		for(auto &name : data.index_names[super])
			fprintf(file, "\"%s\" ", name.data());
	} else if (data.type == Index_Record::Type::numeric1)
		fprintf(file, "%d ", data.index_counts[super]);
	else
		fatal_error(Mobius_Error::internal, "Unhandled index type in write_indexes_to_file().");
}

template<typename Id_Type>
void
Index_Data<Id_Type>::for_each_helper(
	const std::function<void(Index_Tuple<Id_Type> &indexes)> &do_stuff,
	const std::function<void(int)> &new_level,
	Index_Tuple<Id_Type> &indexes,
	int pos
) {
	auto index_set = indexes.indexes[pos].index_set;
	
	new_level(pos);
	for(int idx = 0; idx < get_index_count(indexes, index_set).index; ++idx) {
		indexes.indexes[pos].index = idx;
		if(pos == indexes.indexes.size()-1)
			do_stuff(indexes);
		else
			for_each_helper(do_stuff, new_level, indexes, pos+1);
	}
}

template<typename Id_Type>
void
Index_Data<Id_Type>::for_each(
	std::vector<Id_Type> &index_sets, 
	const std::function<void(Index_Tuple<Id_Type> &indexes)> &do_stuff,
	const std::function<void(int)> &new_level
) {
	Index_Tuple<Id_Type> indexes;
	if(index_sets.empty()) {
		do_stuff(indexes);
		return;
	}
	for(int pos = 0; pos < index_sets.size(); ++pos) indexes.add_index( Idx_T { index_sets[pos], 0 } );
	for_each_helper(do_stuff, new_level, indexes, 0);
}

template<typename Id_Type>
Index_Record::Type
Index_Data<Id_Type>::get_index_type(Id_Type index_set_id) {
	return index_data[index_set_id.id].type;
}

template<typename Id_Type>
Index_Type<Id_Type>
Index_Data<Id_Type>::lower(Idx_T union_index, Idx_T parent_idx) {
	// Lower an index from a union index set to a union member.
	auto set = record->index_sets[union_index.index_set];
	if(set->union_of.empty())
		fatal_error(Mobius_Error::internal, "Misuse of lower() for non-union index set.");
	
	Idx_T below = union_index;
	for(auto ui_id : set->union_of) {
		s32 count = get_count_base(ui_id, parent_idx);
		if(below.index < count) {
			below.index_set = ui_id;
			return below;
		}
		below.index -= count;
	}
	fatal_error(Mobius_Error::internal, "Union index set was incorrectly set up.");
	return Idx_T::no_index();
}

#endif // MOBIUS_INDEX_DATA_H




