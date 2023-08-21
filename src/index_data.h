
// TODO: Should implement something like this to share functionality between Model_Application and Data_Set

#ifndef MOBIUS_INDEX_DATA_H
#define MOBIUS_INDEX_DATA_H

template<typename Id_Type>
struct
Index_Type {
	Id_Type index_set;
	s32     index;
	
	static constexpr Index_Type no_index = { Id_Type(), -1 };
};

template<typename Id_Type>
inline is_valid(Index_Type<Id_Type> index) { return index.index >= 0; }

typedef Index_Type<Entity_Id> Index_T;
typedef Index_Type<Data_Id>   Index_D;

constexpr Index_T invalid_index = Index_T::no_index;

template<typename Record_Type, typename Id_Type>
struct
Index_Tuple {
	
	typedef Index_Type<Id_Type> Idx_T;
	
	Indexes();
	Indexes(Record_Type *record);
	Indexes(Idx_T index);
	
	void clear();
	void set_index(Idx_T index, bool overwrite = false);
	void add_index(Idx_T index);
	void add_index(Id_Type index_set, s32 idx);
	Idx_T get_index(Index_Data<Record_Type, Id_Type> &index_data, Id_Type index_set, bool matrix_column = false);
	
	std::vector<Idx_T> indexes; // Should probably have this as private, but it is very inconvenient.
private :
	bool lookup_ordered = false;
	Idx_T mat_col = Idx_T::no_index;
	
	Idx_T get_index_base(Id_Type index_set);
};

typedef Indexes Index_Tuple<Mobius_Model, Entity_Id>;


template<typename Record_Type, typename Id_Type>
struct
Index_Data { 
	
	Record_Type *record;
	
	Index_Data(Record_Type *record) : record(record) {}
	
	typedef Index_Type<Id_Type> Idx_T;
	
	void set_indexes(Id_Type index_set, std::vector<Token> &names, Idx_T parent_idx = Idx_T::no_index);
	
	void find_indexes(std::vector<Id_Type> &index_sets, std::vector<Token> &idx_names, Index_Tuple<Record_Type, Id_Type> &indexes_out);
	//find_indexes(std::vector<Id_Type> &index_sets, const char **idx_names, Index_Tuple<Idx_T> &indexes_out);  //May need something like this for
	
	bool are_in_bounds(Index_Tuple<Record_Type, Id_Type> &indexes);
	
	Idx_T get_max_count(Id_Type index_set);
	Idx_T get_index_count(Id_Type index_set, Index_Tuple<Record_Type, Id_Type> &indexes);
	
	s64 get_instance_count(std::vector<Entity_Id> &index_sets);
	
	std::string get_index_name(Index_Tuple<Record_Type, Id_Type> &indexes, Entity_Id index_set, bool *is_quotable = nullptr);
	std::string get_possibly_quoted_index_name(Index_Tuple<Record_Type, Id_Type> &indexes, Entity_Id index_set);
	void get_index_names_with_edge_naming(Model_Application *app, Index_Tuple<Record_Type, Id_Type> &indexes, std::vector<std::string> &names_out, bool quote = false);
	
	bool are_all_indexes_set(Id_Type index_set);
	
	// TODO: Some method to copy to another Index_Data.
private :
	
	std::vector<Index_Record> index_data;
	
	struct
	Index_Record {
		enum class Type {
			none = 0,
			numeric1,
			named,
		}     type = Type::none;
		
		std::vector<s32>                                    index_counts;
		std::vector<std::vector<std::string>>               index_names;
		std::vector<std::unordered_map<std::string, Idx_T>> name_to_index;
	};
	
	Idx_T find_index_base(Id_Type index_set, Token *idx_name, Idx_T index_of_super = Idx_T::no_index);
	Idx_T find_index(Id_Type index_set, Token *idx_name, Idx_T index_of_super = Idx_T::no_index);
	s32   get_count_base(Id_Type index_set, Idx_T index_of_super = Idx_T::no_index);
	std::string get_index_name_base(Index_T index, Index_T index_of_super, bool *is_quotable);
	bool  can_be_sub_indexed_to(Id_Type parent_set, Id_Type other_set, s32* offset);
	
	void put_index_name_with_edge_naming(Model_Application *app, Index_Tuple<Record_Type, Id_Type> &indexes, Id_Type index_set_id, std::vector<std::string> &names_out, int pos, bool quote);
};


template<typename Record_Type, typename Id_Type>
Index_Tuple<Record_Type, Id_Type>::Index_Tuple(Record_Type *record) {
	lookup_ordered = false;
	if(record)  // NOTE: We need the check model!=0 since some places we need to construct an Indexed_Par before the model is created. TODO: Could we avoid that somehow?
		indexes.resize(record->index_sets.count(), Idx_T::no_index);
}

template<typename Record_Type, typename Id_Type>
Index_Tuple<Record_Type, Id_Type>::Index_Tuple() {
	lookup_ordered = true;
}

template<typename Record_Type, typename Id_Type>
Index_Tuple<Record_Type, Id_Type>::Index_Tuple(Idx_T index) {
	lookup_ordered = true;
	add_index(index);
}

template<typename Record_Type, typename Id_Type>
void
Index_Tuple<Record_Type, Id_Type>::clear() {
	if(lookup_ordered)
		indexes.clear();
	else {
		for(auto &index : indexes)
			index = Idx_T::no_index;
	}
	mat_col = Idx_T::no_index;
}

template<typename Record_Type, typename Id_Type>
void
Index_Tuple<Record_Type, Id_Type>::set_index(Idx_T index, bool overwrite) {
	if(!is_valid(index))
		fatal_error(Mobius_Error::internal, "Tried to set an invalid index on an Indexes");
	if(!lookup_ordered) {
		if(!overwrite && is_valid(indexes[index.index_set.id])) {
			if(is_valid(mat_col))
				fatal_error(Mobius_Error::internal, "Got duplicate matrix column index for an Indexes.");
			mat_col = index;
		} else
			indexes[index.index_set.id] = index;
	} else
		fatal_error(Mobius_Error::internal, "Using set_index on an Indexes that is lookup_ordered");
}

template<typename Record_Type, typename Id_Type>
void
Index_Tuple<Record_Type, Id_Type>::add_index(Idx_T index) {
	if(!is_valid(index))
		fatal_error(Mobius_Error::internal, "Tried to set an invalid index on an Indexes");
	if(lookup_ordered)
		indexes.push_back(index);
	else
		fatal_error(Mobius_Error::internal, "Using add_index on an Indexes that is not lookup_ordered");
}

template<typename Record_Type, typename Id_Type>
void
Index_Tuple<Record_Type, Id_Type>::add_index(Id_Type index_set, s32 idx) {
	add_index(Idx_T { index_set, idx } );
}

template<typename Record_Type, typename Id_Type>
Idx_T
Index_Tuple<Record_Type, Id_Type>::get_index_base(Id_Type index_set_id) {
	// TODO: if we are lookup_ordered and we want a matrix_column, should we check for the second instance of the index set?
	if(lookup_ordered) {
		for(auto index : indexes) {
			if(index.index_set == index_set_id)
				return index;
		}
		return Idx_T::no_index;
	} else
		return indexes[index_set_id.id];
}

template<typename Record_Type, typename Id_Type>
Idx_T
Index_Tuple<Record_Type, Id_Type>::get_index(Index_Data<Record_Type, Id_Type> &index_data, Id_Type index_set_id, bool matrix_column) {
	
	// TODO: How to handle matrix column for union index sets? Probably disallow it? (See also Index_Exprs)
	
	if(matrix_column && is_valid(mat_col)) {
		if(index_set_id != mat_col.index_set)
			fatal_error(Mobius_Error::internal, "Unexpected matrix column index set in Index_Tuple::get_index.");
		return mat_col;
	} else {
		auto result = get_index_base(index_set_id);
		if(is_valid(result))
			return result;
		
		// Try to index a union index set (if a direct index is absent) by an index belonging to a member of that union.
		result.index_set = index_set_id;
		result.index = 0;
		bool found = false;
		auto index_set = index_data.record->index_sets[index_set_id];
		for(auto ui_id : index_set->union_of) {
			auto idx = get_index_base(index_set_id);
			if(is_valid(idx)) {
				found = true;
				result.index += idx.index;
				break;
			} else
				result.index += index_data.get_index_count(ui_id, *this).index;
		}
		if(!found)
			return Idx_T::no_index;
		return result;
	}
	return Idx_T::no_index;
}

template<typename Record_Type, typename Id_Type>
void 
Index_Data<Record_Type, Id_Type>::set_indexes(Id_Type index_set_id, std::vector<Token> &names, Idx_T parent_idx) {
	
	s64 id = index_set_id.id;
	if(id >= index_data.size())
		index_data.resize(id+1);
	
	auto &data = index_data[id];
	
	s32 super_idx = 0;
	if(is_valid(parent_idx)) {
		super = parent_idx.index;
	
	auto index_set = record->index_sets[index_set_id];
	if(!index_set->union_of.empty()) {
		names[0].print_error_header();
		fatal_error("Tried to explicitly set indexes for a union index set.");
	}
	if(index_set->sub_indexed_to != parent_idx.index_set) {
		names[0].print_error_header();
		fatal_error("Tried to set non-sub-indexed indexes for a sub-indexed index set or the other way around.");
	}
	
	s32 instance_count = 1;
	if(is_valid(index_set->sub_indexed_to)) {
		instance_count = get_max_count(index_set->sub_indexed_to);
		if(instance_count <= 0)
			fatal_error(Mobius_Error::internal, "Somehow a parent index set was not initialized before we tried to set data of an index set that was sub-indexed to it.");
	}
	
	if(data.index_counts.empty())
		data.index_counts.resize(instance_count);
	
	s32  &count = data.index_counts[super];

	if(count != 0) {
		names[0].print_error_header();
		fatal_error("Trying to set indexes for the same index set instance \"", index_set->name, "\" twice.");
	}

	if(data.type == Index_Record::Type::none) {
		bool error = false;
		if(names[0].type == Token_Type::integer) {
			data.type = Index_Record::Type::numeric1;
			if(names.size() > 1)
				error = true;
		} else if (names[0].type == Token_Type::quoted_string) {
			data.type = Index_Record::Type::named;
			if(data.index_names.empty()) {
				data.name_to_index.resize(instance_count);
				data.index_names.resize(instance_count);
			}
		} else
			error = true;
		if(error) {
			names[0].print_error_header();
			fatal_error("Expected a list of quoted strings or a single integer.");
		}
	}
	
	if(data.type == Index_Record::type::numeric1) {
		if(names[0].type != Token_Type::integer) {
			names[0].print_error_header()
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
		
		Index_T index = {index_set_id, 0};
		for(auto &name : names) {
			if(name.type != Token_Type::quoted_string) {
				name.print_error_header();
				fatal_error("Expected just quoted strings for this index data.");
			}
			std::string nn = name.string_value;
			inames.push_back(nn);
			nmap[nn] = index;
			++index.index;
		}
	} else
		fatal_error(Mobius_Error::internal, "Unhandled index type.");
}

template<typename Record_Type, typename Id_Type>
s32
Index_Data<Record_Type, Id_Type>::get_count_base(Id_Type index_set_id, Idx_T index_of_super) {
	auto index_set = record->index_sets[index_set_id];
	if(is_valid(index_set->sub_indexed_to) && !is_valid(index_of_super))
		fatal_error(Mobius_Error::internal, "Wrong use of get_count_base.");
	if(!index_set->union_of.empty()) {
		s32 sum = 0;
		for(auto ui_id : index_set->union_of)
			sum += get_count_base(ui_id, index_of_super);
		return sum;
	}
	int super = is_valid(index_of_super) = index_of_super.index : 0;
	return index_data[index_set_id.id].index_counts[super];
}

template<typename Record_Type, typename Id_Type>
Idx_T
Index_Data<Record_Type, Id_Type>::find_index_base(Id_Type index_set_id, Token *idx_name, Idx_T index_of_super) {
	
	auto &data = index_data[index_set_id.id];
	int super = is_valid(index_of_super) ? index_of_super.index : 0;
	
	if(idx_name->type == Token_Type::quoted_string) {
		if(data.type != Index_Record::Type::named) {
			return Idx_T::no_index;
		auto &nmap = data.name_to_index[super]
		auto find = nmap.find(idx_name->string_value);
		if(find == nmap.end())
			return Idx_T::no_index;
		return find->second;
	} else if (idx_name->type == Token_Type::integer) {
		Idx_T result = Idx_T { index_set_id, idx_name->val_int };
		if(result.index < 0 || result.index >= get_count_base(index_set_id, index_of_super))
			return Idx_T::no_index;
		return result;
	}
	
	return Idx_T::no_index;
}

template<typename Record_Type, typename Id_Type>
Idx_T
Index_Data<Record_Type, Id_Type>::find_index(Id_Type index_set_id, Token *idx_name, Idx_T index_of_super) {
	auto &data = index_data[index_set.id];
	
	Idx_T result = Idx_T::no_index;
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


template<typename Record_Type, typename Id_Type>
void 
Index_Data<Record_Type, Id_Type>::find_indexes(std::vector<Id_Type> &index_sets, std::vector<Token> &idx_names, Index_Tuple<Record_Type, Id_Type> &indexes_out) {
	
	for(int pos = 0; pos < index_names.size(); ++pos) {
		auto index_set = record->index_sets[index_sets[pos]];
		
		Idx_T index_of_super = Idx_T::no_index;
		if(is_valid(index_set->sub_indexed_to)) {
			index_of_super = indexes_out.get_index(*this, index_set->sub_indexed_to);

			if(!is_valid(index_of_super)) {
				idx_names[pos].print_error_header();
				// TODO: Print some names here!
				fatal_error("This index belongs to an index set that is sub-indexed to another index set, but this index does not appear after an index of the parent index set.");
			}
		}
		auto index = find_index(index_sets[pos], &idx_names[pos], index_of_super);
		indexes_out.add_index(index);
	}
}

template<typename Record_Type, typename Id_Type>
bool
Index_Data<Record_Type, Id_Type>::are_in_bounds(Index_Tuple<Record_Type, Id_Type> &indexes) {
	
	for(int pos = 0; pos < indexes.indexes.size(); ++pos) {
		auto index = indexes.indexes[pos];
		
		if(!is_valid(index.index_set)) continue;
		if(index.index < 0) return false;
		
		auto index_set = record->index_sets[index.index_set];
		
		Idx_T index_of_super = Idx_T::no_index;
		if(is_valid(index_set->sub_indexed_to)) {
			index_of_super = indexes.get_index(*this, index_set->sub_indexed_to);
			
			if(!is_valid(index_of_super)) {
				// TODO: Print some names here!
				fatal_error(Mobius_Error::internal, "Got an index belonging to an index set that is sub-indexed to another index set, but this index does not appear after an index of the parent index set.");
			}
		}
		s32 count = get_count_base(index.index_set, index_of_super);
		
		if(index.index >= count)
			return false;
	}
	return true;
}

template<typename Record_Type, typename Id_Type>
Idx_T
Index_Data<Record_Type, Id_Type>::get_max_count(Id_Type index_set_id) {
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

template<typename Record_Type, typename Id_Type>
Idx_T
Index_Data<Record_Type, Id_Type>::get_index_count(Id_Type index_set_id, Index_Tuple<Idx_T> &indexes) {
	
	auto index_set = record->index_sets[index_set_id];
	
	Idx_T index_of_super = Idx_T::no_index;
	if(is_valid(index_set->sub_indexed_to)) {
		index_of_super = indexes.get_index(*this, index_set->sub_indexed_to);
		
		if(!is_valid(index_of_super)) {
			// TODO: Print some names here!
			fatal_error(Mobius_Error::internal, "Got an index that belongs to an index set that is sub-indexed to another index set, but this index does not appear after an index of the parent index set.");
		}
	}
	return Idx_T {index_set_id, get_count_base(index_set_id, index_of_super)};
}

template<typename Record_Type, typename Id_Type>
bool
Index_Data<Record_Type, Id_Type>::can_be_sub_indexed_to(Id_Type parent_set, Id_Type other_set, s32* offset) {
	*offset = 0;
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
		else
			*offset += get_max_count(ui_id).index;
		// NOTE: It is ok to use get_max_count for the following reason: Even if we allowed union index sets to be sub-indexed, the supposition here is that it is a parent index set, and we don't allow double sub-indexing.
	}
	return false;
}

template<typename Record_Type, typename Id_Type>
s64
Index_Data<Record_Type, Id_Type>::get_instance_count(std::vector<Entity_Id> &index_sets) {
	
	s64 count = 1;
	if(index_sets.empty()) return count;
	
	std::vector<u8> already_counted(index_sets.size(), 0);
	
	for(int pos = 0; pos < index_sets.size(); ++pos) {
		
		if(already_counted[pos]) continue;
		
		auto index_set_id = index_sets[pos];
		
		auto index_set = record->index_sets[index_set_id];
		if(is_valid(index_set->sub_indexed_to)) {
			// NOTE: This algorithm assures that if this index set is sub-indexed, it should already have been processed, and thus skipped (unless the tuple is incorrectly set up).
			fatal_error(Mobius_Error::internal, "Got an index set \"", index_set->name, "\" that is sub-indexed to another index set \"",
				record->index_sets[index_set->sub_indexed_to]->name, "\", but in this index sequence, the former doesn't follow the latter or a union member of the latter.");
		}
		
		std::vector<std::pair<Id_Type, s32>> subs;
		for(int pos2 = pos+1; pos2 < index_sets.size(); ++pos2) {
			s32 offset = 0;
			if(can_be_sub_indexed_to(this, index_set_id, index_sets[pos2], &offset)) {
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
					subcount *= get_count_base(sub.first.id, Idx_T {index_set_id, sub.second + idx}).index;
				sum += subcount;
			}
			count *= sum;
		}
	}
	return count;
}

template<typename Record_Type, typename Id_Type>
std::string
Index_Data<Record_Type, Id_Type>::get_index_name_base(Index_T index, Index_T index_of_super, bool *is_quotable) {
	auto &data = index_data[index.index_set.id];
	if(data.type == Index_Data::Type::numeric1) {
		if(is_quotable) *is_quotable = false;
		char buf[16];
		itoa(index.index, buf, 10);
		return buf;
	} else if (data.type == Index_Data::Type::named) {
		if(is_quotable) *is_quotable = true;

		int super = is_valid(index_of_super) ? index_of_super.index : 0;
		return data.index_names[super][index.index];
	} else
		fatal_error(Mobius_Error::internal, "Unhandled index type.");
}

template<typename Record_Type, typename Id_Type>
std::string
Index_Data<Record_Type, Id_Type>::get_index_name(Index_Tuple<Record_Type, Id_Type> &indexes, Entity_Id index_set_id, bool *is_quotable) {
	
	auto index = indexes.get_index(*this, index_set_id);
	
	auto index_set = record->index_sets[index_set_id];
	Idx_T index_of_super = Idx_T::no_index;
	
	if(is_valid(index_set->sub_indexed_to)) {
		index_of_super = indexes.get_index(*this, index_set->sub_indexed_to);
		
		if(!is_valid(index_of_super))
			fatal_error(Mobius_Error::internal, "Invalid index tuple in get_index_name.");
	}
	
	if(!is_valid(index) || index.index >= get_count_base(index_set_id, index_of_super))
		fatal_error(Mobius_Error::internal, "Index out of bounds in get_index_name");
	
	if(!index_set->union_of.empty()) {
		Idx_T lower = index;
		for(auto ui_id : index_set->union_of) {
			s32 count = get_count_base(ui_id, index_of_super);
			if(lower.index < count) {
				lower.index_set = ui_id;
				return get_index_name_base(lower, index_of_super, is_quotable);
			}
			lower.index -= count;
		}
		fatal_error(Mobius_Error::internal, "Something went wrong with setting up union index sets.");
	}
	
	return get_index_name_base(index, index_of_super, is_quotable);
}

inline void
maybe_quote(std::string &str, bool quote) {
	if(quote)
		str = "\"" + str + "\"";
}

template<typename Record_Type, typename Id_Type>
std::string
Index_Data<Record_Type, Id_Type>::get_possibly_quoted_index_name(Index_Tuple<Record_Type, Id_Type> &indexes, Entity_Id index_set_id) {
	bool is_quotable;
	std::string result = get_index_name(indexes, index_set_id, &is_quotable);
	maybe_quote(result, is_quotable);
	return result;
}

template<typename Record_Type, typename Id_Type>
void
Index_Data<Record_Type, Id_Type>::put_index_name_with_edge_naming(Model_Application *app, Index_Tuple<Record_Type, Id_Type> &indexes, Id_Type index_set_id, std::vector<std::string> &names_out, int pos, bool quote) {
	fatal_error(Mobius_Error::internal, "Index name with edge nameing only implemented for Mobius_Model.");
}

template<>
void
Index_Data<Mobius_Model, Entity_Id>::put_index_name_with_edge_naming(Model_Application *app, Indexes &indexes, Id_Type index_set_id, std::vector<std::string> &names_out, int pos, bool quote) {
	
	auto model = app->model; // = record;
	
	bool is_quotable = false;
	
	auto index_set = model->index_sets[index_set_id];
	auto conn_id = index_set->is_edge_of_connection;
	if(!is_valid(conn_id) || model->connections[conn_id]->type != Connection_Type::directed_graph) {
		names_out[pos] = get_index_name(indexes, index_set_id, &is_quotable);
		maybe_quote(names_out[pos], is_quotable);
		return;
	}
	
	Index_T index_of_super = invalid_index;
	if(is_valid(index_set->sub_indexed_to)) {
		index_of_super = indexes.get_index(index_set->sub_indexed_to);
		if(!is_valid(index_of_super))
			fatal_error(Mobius_Error::internal, "Invalid index tuple in put_index_name_with_edge_naming.");
	}
	
	// If this index set is the edge index set of a graph connection, we generate a name for the index by the target of the edge (arrow).
	//TODO: Can we make a faster way to find the arrow
	auto &arrows = app->connection_components[conn_id].arrows;
	for(auto arr : arrows) {
		auto source_idx = invalid_index;
		if(arr.source_indexes.indexes.size() == 1)
			source_idx = arr.source_indexes.indexes[0];
		else if(arr.source_indexes.indexes.size() > 1)
			fatal_error(Mobius_Error::internal, "Graph arrows with multiple source indexes not supported by get_index_name.");
		
		if(arr.edge_index == index && source_idx == index_of_super) {
			if(is_valid(arr.target_id)) {
				if(arr.target_indexes.indexes.empty()) {
					names_out[pos] = model->components[arr.target_id]->name;
					is_quotable = true;
				} else if (arr.target_indexes.indexes.size() == 1) {
					names_out[pos] = get_index_name(indexes, arr.target_indexes.indexes[0], &is_quotable);
				} else
					fatal_error(Mobius_Error::internal, "Graph arrows with multiple target indexes not supported by get_index_name.");
			} else {
				is_quotable = false;
				names_out[pos] = "out";
			}
		}
	}
	maybe_quote(names_out[pos], is_quotable);
}

template<typename Record_Type, typename Id_Type>
void
Index_Data<Record_Type, Id_Type>::get_index_names_with_edge_naming(Model_Application *app, Index_Tuple<Idx_T> &indexes, std::vector<std::string> &names_out, bool quote) {
	int count = indexes.indexes.size();
	if(is_valid(indexes.mat_col)) ++count;
	names_out.resize(count);
	
	int pos = 0;
	for(auto &index : indexes.indexes) {
		if(!is_valid(index)) { pos++; continue; }
		put_index_name_with_edge_naming(app, indexes, index.index_set, names_out, pos, quote);
		++pos;
	}
	if(is_valid(indexes.mat_col))
		put_index_name_with_edge_naming(this, indexes.mat_col, indexes, names_out, count-1, quote);
}

template<typename Record_Type, typename Id_Type>
bool
Index_Data<Record_Type, Id_Type>::are_all_indexes_set(Id_Type index_set_id) {
	
	auto index_set = record->index_sets[index_set_id];
	if(!index_set->union_of.empty()) {
		for(auto ui_id : index_set->union_of) {
			if(!are_all_indexes_set(ui_id))
				return false;
		}
		return true;
	}
	auto &data = index_data[index_set_id.id];
	if(data.index_counts.empty())
		return false;
	for(s32 count : data.index_counts)
		if(!count) return false;
	return true;
}



#endif // MOBIUS_INDEX_DATA_H
