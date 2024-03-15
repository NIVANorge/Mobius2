
#include "index_data.h"
#include "catalog.h"

Indexes::Indexes(Catalog *catalog) {
	lookup_ordered = false;
	if(catalog)  // NOTE: We need the check catalog!=0 since some places we need to construct an Indexed_Par before the model is created. TODO: Could we avoid that somehow?
		indexes.resize(catalog->index_sets.count(), Index_T::no_index());
}

Indexes::Indexes() {
	lookup_ordered = true;
}

Indexes::Indexes(Index_T index) {
	lookup_ordered = true;
	add_index(index);
}

void
Indexes::clear() {
	if(lookup_ordered)
		indexes.clear();
	else {
		for(auto &index : indexes)
			index = Index_T::no_index();
	}
}

s64
Indexes::count() {
	if(lookup_ordered)
		return indexes.size();
	else {
		s64 result = 0;
		for(auto &index : indexes) result += is_valid(index);
		return result;
	}
}

void
Indexes::set_index(Index_T index, bool overwrite) {
	if(!is_valid(index.index_set))
		fatal_error(Mobius_Error::internal, "Tried to set an invalid index set on an Indexes");
	if(!lookup_ordered) {
		if(!overwrite && is_valid(indexes[index.index_set.id])) {
			fatal_error(Mobius_Error::internal, "Got duplicate index for an Indexes.");
		} else
			indexes[index.index_set.id] = index;
	} else
		fatal_error(Mobius_Error::internal, "Using set_index on an Indexes that is lookup_ordered");
}

void
Indexes::add_index(Index_T index) {
	if(!is_valid(index))
		fatal_error(Mobius_Error::internal, "Tried to set an invalid index on an Indexes");
	if(lookup_ordered)
		indexes.push_back(index);
	else
		fatal_error(Mobius_Error::internal, "Using add_index on an Indexes that is not lookup_ordered");
}

void
Indexes::add_index(Entity_Id index_set, s32 idx) {
	add_index(Index_T { index_set, idx } );
}

Index_T
Indexes::get_index_base(Entity_Id index_set_id) {
	// TODO: if we are lookup_ordered and we want a matrix_column, should we check for the second instance of the index set?
	if(lookup_ordered) {
		for(auto index : indexes) {
			if(index.index_set == index_set_id)
				return index;
		}
		return Index_T::no_index();
	} else
		return indexes[index_set_id.id];
}

Index_T
Indexes::get_index(Index_Data &index_data, Entity_Id index_set_id) {
		
	auto result = get_index_base(index_set_id);
	if(is_valid(result))
		return result;
	
	// Try to index a union index set (if a direct index is absent) by an index belonging to a member of that union.
	result.index_set = index_set_id;
	result.index = 0;
	bool found = false;
	auto index_set = index_data.catalog->index_sets[index_set_id];
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
		return Index_T::no_index();
	return result;
}

void
Index_Data::initialize(Entity_Id index_set_id, Index_T parent_idx, Index_Record::Type type, Source_Location source_loc) {
	s64 id = index_set_id.id;
	if(id >= index_data.size())
		index_data.resize(id+1);
	
	auto &data = index_data[id];
	
	s32 super = is_valid(parent_idx) ? parent_idx.index : 0;
	
	auto index_set = catalog->index_sets[index_set_id];
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

void
Index_Data::initialize_edge_index_set(Entity_Id index_set_id, Source_Location source_loc) {
	
	auto parent_id = catalog->index_sets[index_set_id]->sub_indexed_to;
	if(!is_valid(parent_id))
		fatal_error(Mobius_Error::internal, "Got an edge index set that is not sub-indexed to a component index set.");
	
	s32 count = get_count_base(parent_id, Index_T::no_index());
	for(s32 par_idx = 0; par_idx < count; ++par_idx) {
		Index_T parent_idx = Index_T { parent_id, par_idx };
		initialize(index_set_id, parent_idx, Index_Record::Type::named, source_loc);
	}
	
}

void
Index_Data::add_edge_index(Entity_Id index_set_id, const std::string &index_name, Source_Location source_loc, Index_T parent_idx) {
	
	// NOTE: This is only safe while the index set is being constructed, not later.
	
	// TODO: We should check for name clashes?
	// Or in the data_set, it should check that there are no double edges?
	
	auto &data = index_data[index_set_id.id];
	
	int super = is_valid(parent_idx) ? parent_idx.index : 0;
	
	s32 val = data.index_counts[super]++;
	data.index_names[super].push_back(index_name);
	data.name_to_index[super][index_name] = val;
}

void 
Index_Data::set_indexes(Entity_Id index_set_id, const std::vector<Token> &names, Index_T parent_idx) {
	
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
		
		auto index = Index_T {index_set_id, 0};
		for(auto &name : names) {
			if(name.type != Token_Type::quoted_string) {
				name.print_error_header();
				fatal_error("Expected just quoted strings for this index data.");
			}
			std::string nn = name.string_value;
			if(nmap.find(nn) != nmap.end()) {
				name.print_error_header();
				fatal_error("The index name \"", nn, "\" is repeated for the index set \"", catalog->index_sets[index_set_id]->name, "\"");
			}
			inames.push_back(nn);
			nmap[nn] = index.index;
			++index.index;
		}
	} else
		fatal_error(Mobius_Error::internal, "Unhandled index type in set_indexes.");
}

void
Index_Data::initialize_union(Entity_Id index_set_id, Source_Location source_loc) {
	
	s64 id = index_set_id.id;
	if(id >= index_data.size())
		index_data.resize(id+1);
	
	auto &data = index_data[id];
	
	auto index_set = catalog->index_sets[index_set_id];
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
						fatal_error("The index name \"", name, "\" overlaps between the two union members \"", catalog->index_sets[ui_id1]->name, "\" and \"", catalog->index_sets[ui_id2]->name, "\" in the index set union \"", index_set->name, "\".");
					}
				}
			}
		}
	}
	
}

s32
Index_Data::get_count_base(Entity_Id index_set_id, Index_T index_of_super) {
	auto index_set = catalog->index_sets[index_set_id];
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

Index_T
Index_Data::find_index_base(Entity_Id index_set_id, Token *idx_name, Index_T index_of_super) {
	
	auto &data = index_data[index_set_id.id];
	int super = is_valid(index_of_super) ? index_of_super.index : 0;
	
	if(data.has_index_position_map && (is_numeric(idx_name->type))) {
		double val = idx_name->double_value();
		s32 mapped = data.map_index(val);
		auto count = get_count_base(index_set_id, index_of_super);
		Index_T result = Index_T { index_set_id, mapped };
		if(result.index < 0 || result.index >= count)
			return Index_T::no_index();
		return result;
	} else if(idx_name->type == Token_Type::quoted_string) {
		if(data.type != Index_Record::Type::named)
			return Index_T::no_index();
		auto &nmap = data.name_to_index[super];
		auto find = nmap.find(idx_name->string_value);
		if(find == nmap.end())
			return Index_T::no_index();
		return Index_T { index_set_id, find->second };
	} else if (idx_name->type == Token_Type::integer) {
		Index_T result = Index_T { index_set_id, (s32)idx_name->val_int };
		if(result.index < 0 || result.index >= get_count_base(index_set_id, index_of_super))
			return Index_T::no_index();
		return result;
	}
	
	return Index_T::no_index();
}

Index_T
Index_Data::find_index(Entity_Id index_set_id, Token *idx_name, Index_T index_of_super) {
	auto &data = index_data[index_set_id.id];
	
	Index_T result = Index_T::no_index();
	auto index_set = catalog->index_sets[index_set_id];
	
	if(is_valid(index_set->sub_indexed_to) && !is_valid(index_of_super))
		fatal_error(Mobius_Error::internal, "Not properly setting a parent index when trying to look up index data for a sub-indexed index set.");
	
	if(!index_set->union_of.empty() && data.type == Index_Record::Type::named) {
		if(is_valid(index_set->sub_indexed_to))
			fatal_error(Mobius_Error::internal, "Sub-indexed unions are not implemented");
		
		s32 sum = 0;
		bool found = false;
		for(auto ui_id : index_set->union_of) {
			Index_T find = find_index_base(ui_id, idx_name);
			if(is_valid(find)) {
				sum += find.index;
				found = true;
				break;
			} else
				sum += get_max_count(ui_id).index;
		}
		if(found)
			result = Index_T { index_set_id, sum };
	} else
		result = find_index_base(index_set_id, idx_name, index_of_super);
	
	if(!is_valid(result)) {
		idx_name->print_error_header();
		if(idx_name->type == Token_Type::integer)
			fatal_error("The value ", idx_name->val_int, " is out of bounds for the index set \"", index_set->name, "\".");
		else if(idx_name->type == Token_Type::quoted_string)
			fatal_error("The name \"", idx_name->string_value, "\" is not a valid index for the index set \"", index_set->name, "\".");
		else
			fatal_error("This is not a valid index for the index set \"", index_set->name, "\".");
	}
	
	return result;
}

void
Index_Data::find_index(Entity_Id index_set_id, Token *idx_name, Indexes &indexes_out) {
	
	auto index_set = catalog->index_sets[index_set_id];
	
	Index_T index_of_super = Index_T::no_index();
	if(is_valid(index_set->sub_indexed_to)) {
		index_of_super = indexes_out.get_index(*this, index_set->sub_indexed_to);

		if(!is_valid(index_of_super)) {
			idx_name->print_error_header();
			fatal_error("(find_indexes) This index belongs to an index set ", index_set->name, " that is sub-indexed to another index set ",
			catalog->index_sets[index_set->sub_indexed_to]->name, ", but this index does not appear after an index of the parent index set.");
		}
	}
	auto index = find_index(index_set_id, idx_name, index_of_super);
	indexes_out.add_index(index);
}

void 
Index_Data::find_indexes(const std::vector<Entity_Id> &index_sets, std::vector<Token> &idx_names, Indexes &indexes_out) {
	
	// TODO: Assert index_sets and idx_names are the same size?
	
	indexes_out.clear();
	
	for(int pos = 0; pos < idx_names.size(); ++pos)
		find_index(index_sets[pos], &idx_names[pos], indexes_out);
}

bool
Index_Data::are_in_bounds(Indexes &indexes) {
	
	for(int pos = 0; pos < indexes.indexes.size(); ++pos) {
		auto index = indexes.indexes[pos];
		
		if(!is_valid(index.index_set)) continue;
		if(index.index < 0) return false;
		
		auto index_set = catalog->index_sets[index.index_set];
		
		Index_T index_of_super = Index_T::no_index();
		if(is_valid(index_set->sub_indexed_to)) {
			index_of_super = indexes.get_index(*this, index_set->sub_indexed_to);
			
			if(!is_valid(index_of_super)) {
				fatal_error(Mobius_Error::internal, "(are_in_bounds) This index (position ", pos, ") belongs to an index set ", index_set->name, " that is sub-indexed to another index set ",
				catalog->index_sets[index_set->sub_indexed_to]->name, ", but this index does not appear after an index of the parent index set.");
			}
		}
		s32 count = get_count_base(index.index_set, index_of_super);
		
		if(index.index >= count)
			return false;
	}
	return true;
}

Index_T
Index_Data::get_max_count(Entity_Id index_set_id) {
	auto index_set = catalog->index_sets[index_set_id];
	if(is_valid(index_set->sub_indexed_to)) {
		Index_T result = {index_set_id, 0};
		s32 parent_count = get_count_base(index_set->sub_indexed_to);
		for(Index_T parent_idx = { index_set->sub_indexed_to, 0 }; parent_idx.index < parent_count; ++parent_idx.index)
			result.index = std::max(result.index, get_count_base(index_set_id, parent_idx));
		return result;
	}
	return Index_T {index_set_id, get_count_base(index_set_id)};
}

Index_T
Index_Data::get_index_count(Indexes &indexes, Entity_Id index_set_id) {
	
	auto index_set = catalog->index_sets[index_set_id];
	
	Index_T index_of_super = Index_T::no_index();
	if(is_valid(index_set->sub_indexed_to)) {
		index_of_super = indexes.get_index(*this, index_set->sub_indexed_to);
		
		if(!is_valid(index_of_super)) {
			begin_error(Mobius_Error::internal);
			error_print("(get_index_count) This index belongs to an index set ", index_set->name, " that is sub-indexed to another index set ",
				catalog->index_sets[index_set->sub_indexed_to]->name, ", but this index does not appear after an index of the parent index set. Tuple is: ");
			for(auto &index : indexes.indexes) {
				if(is_valid(index))
					error_print("\"", catalog->index_sets[index.index_set]->name, "\" ");
			}
			mobius_error_exit();
		}
	}
	return Index_T {index_set_id, get_count_base(index_set_id, index_of_super)};
}

bool
Index_Data::can_be_sub_indexed_to(Entity_Id parent_set, Entity_Id other_set, s32* offset) {
	if(offset) *offset = 0;
	auto index_set = catalog->index_sets[other_set];
	if(!is_valid(index_set->sub_indexed_to))
		return false;
	if(index_set->sub_indexed_to == parent_set)
		return true;
	auto super = catalog->index_sets[index_set->sub_indexed_to];
	if(super->union_of.empty())
		return false;
	for(auto ui_id : super->union_of) {
		if(parent_set == ui_id)
			return true;
		else if(offset)
			*offset += get_count_base(ui_id, Index_T::no_index());
		// NOTE: It is ok to use an invalid parent index in get_count_base here for the following reason: 
		// Even if we allowed union index sets to be sub-indexed, the supposition here is that it is a parent index set, and we don't allow double sub-indexing.
	}
	return false;
}

void
Index_Data::check_valid_distribution(std::vector<Entity_Id> &index_sets, Source_Location source_loc) {
	
	int idx = 0;
	for(auto id : index_sets) {
		auto set = catalog->index_sets[id];
		if(std::find(index_sets.begin(), index_sets.begin()+idx, id) != index_sets.begin()+idx) {
			source_loc.print_error_header();
			fatal_error("The index set \"", set->name, "\" appears twice in the same distribution.");
		}
		if(is_valid(set->sub_indexed_to)) {
			bool found = (std::find(index_sets.begin(), index_sets.begin()+idx, set->sub_indexed_to) != index_sets.begin()+idx);
			auto parent_set = catalog->index_sets[set->sub_indexed_to];
			if(!found && !parent_set->union_of.empty()) {
				for(auto ui_id : parent_set->union_of) {
					found = (std::find(index_sets.begin(), index_sets.begin()+idx, ui_id) != index_sets.begin()+idx);
					if(found) break;
				}
			}
			if(!found) {
				source_loc.print_error_header();
				fatal_error("The index set \"", set->name, "\" is sub-indexed to another index set \"", catalog->index_sets[set->sub_indexed_to]->name, "\", but the parent index set (or a union member of it) does not precede it in this distribution.");
			}
		}
		if(!set->union_of.empty()) {
			for(auto ui_id : set->union_of) {
				if(std::find(index_sets.begin(), index_sets.end(), ui_id) != index_sets.end()) {
					source_loc.print_error_header();
					fatal_error("The index set \"", set->name, "\" is a union consisting among others of the index set \"", catalog->index_sets[ui_id]->name, "\", but both appear in the same distribution.");
				}
			}
		}
		++idx;
	}
	
}

s64
Index_Data::get_instance_count(const std::vector<Entity_Id> &index_sets) {
	
	s64 count = 1;
	if(index_sets.empty()) return count;
	
	std::vector<u8> already_counted(index_sets.size(), 0);
	
	for(int pos = 0; pos < index_sets.size(); ++pos) {
		
		if(already_counted[pos]) continue;
		
		auto index_set_id = index_sets[pos];
		
		auto index_set = catalog->index_sets[index_set_id];
		if(is_valid(index_set->sub_indexed_to)) {
			// NOTE: This algorithm assures that if this index set is sub-indexed, it should already have been processed, and thus skipped (unless the tuple is incorrectly set up).
			fatal_error(Mobius_Error::internal, "(get_instance_count) Got an index set \"", index_set->name, "\" that is sub-indexed to another index set \"",
				catalog->index_sets[index_set->sub_indexed_to]->name, "\", but in this index sequence, the former doesn't follow the latter or a union member of the latter.");
		}
		
		std::vector<std::pair<Entity_Id, s32>> subs;
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
					subcount *= get_count_base(sub.first, Index_T {index_set_id, sub.second + idx});
				sum += subcount;
			}
			count *= sum;
		}
	}
	return count;
}

std::string
Index_Data::get_index_name_base(Index_T index, Index_T index_of_super, bool *is_quotable) {
	auto &data = index_data[index.index_set.id];
	
	// TODO: Remove this once we fix MobiView2
	auto set = catalog->index_sets[index.index_set];
	bool invalid_name_support = (is_valid(set->sub_indexed_to) && !is_valid(index_of_super)) || !set->union_of.empty();
	
	if(data.type == Index_Record::Type::numeric1 || invalid_name_support) {
		if(is_quotable) *is_quotable = false;
		if(data.has_index_position_map) {
			static char buf[64];
			double from = 0.0;
			if(index.index > 0)
				from = data.pos_vals[index.index-1];
			double to = data.pos_vals[index.index];
			sprintf(buf, "%.15g-%.15g", from, to);
			//sprintf(buf, ".15g", from);
			return buf;
		} else
			return std::to_string(index.index);
	} else if (data.type == Index_Record::Type::named) {
		if(is_quotable) *is_quotable = true;

		int super = is_valid(index_of_super) ? index_of_super.index : 0;
		if(data.index_names.size() <= super)
			fatal_error(Mobius_Error::internal, "Trying to look up uninitialized index names for index set ", catalog->index_sets[index.index_set]->name, "\".");
		if(data.index_names[super].size() <= index.index)
			fatal_error(Mobius_Error::internal, "Trying to look up uninitialized index name for index set ", catalog->index_sets[index.index_set]->name, "\" and index ", index.index, ".");
		return data.index_names[super][index.index];
	} else
		fatal_error(Mobius_Error::internal, "Unhandled index type in get_index_name_base.");
}

std::string
Index_Data::get_index_name(Indexes &indexes, Index_T index, bool *is_quotable) {
	
	auto index_set = catalog->index_sets[index.index_set];
	Index_T index_of_super = Index_T::no_index();
	
	if(is_valid(index_set->sub_indexed_to)) {
		index_of_super = indexes.get_index(*this, index_set->sub_indexed_to);
		
		if(!is_valid(index_of_super))
			fatal_error(Mobius_Error::internal, "Invalid index tuple in get_index_name.");
	}
	
	if(!is_valid(index) || index.index >= get_count_base(index.index_set, index_of_super))
		fatal_error(Mobius_Error::internal, "Index out of bounds in get_index_name");
	
	auto &data = index_data[index.index_set.id];
	
	if(!index_set->union_of.empty() && data.type == Index_Record::Type::named) {
		Index_T below = lower(index, index_of_super);
		return get_index_name_base(below, index_of_super, is_quotable);
	}
	
	return get_index_name_base(index, index_of_super, is_quotable);
}

std::string
Index_Data::get_possibly_quoted_index_name(Indexes &indexes, Index_T index, bool quote) {
	bool is_quotable;
	std::string result = get_index_name(indexes, index, &is_quotable);
	maybe_quote(result, quote && is_quotable);
	return result;
}

void
Index_Data::get_index_names(Indexes &indexes, std::vector<std::string> &names_out, bool quote) {
	
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

bool
Index_Data::are_all_indexes_set(Entity_Id index_set_id) {
	
	auto index_set = catalog->index_sets[index_set_id];
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

void
Index_Data::write_index_to_file(FILE *file, Index_T index, Index_T parent_idx) {
	
	auto index_set = catalog->index_sets[index.index_set];
	
	if(is_valid(index_set->sub_indexed_to) && !is_valid(parent_idx))
		fatal_error(Mobius_Error::internal, "Missing super index in write_index_to_file");
	
	auto &data = index_data[index.index_set.id];
	
	// TODO: could we reuse code between this and get_index_name ?
	if(!index_set->union_of.empty() && data.type == Index_Record::Type::named) {
		Index_T below = lower(index, parent_idx);
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

void
Index_Data::write_index_to_file(FILE *file, Indexes &indexes, Index_T index) {
	// We could also make this more efficient by not copying the string..
	bool is_quotable;
	std::string result = get_index_name(indexes, index, &is_quotable);
	maybe_quote(result, is_quotable);
	fprintf(file, "%s", result.c_str());
}

void
Index_Data::write_indexes_to_file(FILE *file, Entity_Id index_set, Index_T parent_idx) {
	
	auto set = catalog->index_sets[index_set];
	if((is_valid(set->sub_indexed_to) && !is_valid(parent_idx))
		|| !set->union_of.empty())
		fatal_error(Mobius_Error::internal, "Misuse of write_indexes_to_file");
	
	auto &data = index_data[index_set.id];
	
	int super = is_valid(parent_idx) ? parent_idx.index : 0;
	
	if(data.type == Index_Record::Type::named) {
		for(auto &name : data.index_names[super])
			fprintf(file, "\"%s\" ", name.data());
	} else if (data.type == Index_Record::Type::numeric1) {
		if(!data.has_index_position_map)
			fprintf(file, "%d ", data.index_counts[super]);
		else
			fprintf(file, "%d ", data.backup_counts[super]);
	} else
		fatal_error(Mobius_Error::internal, "Unhandled index type in write_indexes_to_file().");
}

void
Index_Data::for_each_helper(
	const std::function<void(Indexes &indexes)> &do_stuff,
	const std::function<void(int)> &new_level,
	Indexes &indexes,
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

void
Index_Data::for_each(
	std::vector<Entity_Id> &index_sets, 
	const std::function<void(Indexes &indexes)> &do_stuff,
	const std::function<void(int)> &new_level
) {
	Indexes indexes;
	if(index_sets.empty()) {
		do_stuff(indexes);
		return;
	}
	for(int pos = 0; pos < index_sets.size(); ++pos) indexes.add_index( Index_T { index_sets[pos], 0 } );
	for_each_helper(do_stuff, new_level, indexes, 0);
}

Index_Record::Type
Index_Data::get_index_type(Entity_Id index_set_id) {
	return index_data[index_set_id.id].type;
}

Index_T
Index_Data::lower(Index_T union_index, Index_T parent_idx) {
	// Lower an index from a union index set to a union member.
	auto set = catalog->index_sets[union_index.index_set];
	if(set->union_of.empty())
		fatal_error(Mobius_Error::internal, "Misuse of lower() for non-union index set.");
	
	Index_T below = union_index;
	for(auto ui_id : set->union_of) {
		s32 count = get_count_base(ui_id, parent_idx);
		if(below.index < count) {
			below.index_set = ui_id;
			return below;
		}
		below.index -= count;
	}
	fatal_error(Mobius_Error::internal, "Union index set was incorrectly set up.");
	return Index_T::no_index();
}

Index_T
Index_Data::raise(Index_T member_idx, Entity_Id union_set) {
	auto set = catalog->index_sets[union_set];
	Index_T above = Index_T { union_set, member_idx.index };
	for(auto ui_id : set->union_of) {
		if(ui_id == member_idx.index_set) return above;
		s32 count = get_count_base(ui_id, Index_T::no_index()); // TODO: Would break if we allowed sub-indexing union index sets.
		above.index += count;
	}
	fatal_error(Mobius_Error::internal, "Union index set was incorrectly passed to raise().");
	return Index_T::no_index();
}

void
Index_Data::set_position_map(Entity_Id index_set_id, std::vector<double> &pos_vals, Source_Location &source_loc) {
	if(!are_all_indexes_set(index_set_id))
		fatal_error(Mobius_Error::internal, "Tried to set an index map for an uninitialized index set.");
	
	auto &data = index_data[index_set_id.id];
	if(data.has_index_position_map) {
		source_loc.print_error_header();
		fatal_error("Tried to set a position map for the same index set twice.");
	}
	if(data.type != Index_Record::Type::numeric1)
		fatal_error(Mobius_Error::internal, "Tried to set an index map for a non-numeric index set.");
	// TODO: May also be problematic if it is a union member
	auto set = catalog->index_sets[index_set_id];
	if(!set->union_of.empty()) {
		source_loc.print_error_header();
		fatal_error("Can not set a position map for an index set that is a union.");
	}
	if(is_valid(set->is_edge_of_connection)) {
		source_loc.print_error_header();
		fatal_error("Can not set a position map for an index set that indexes a connection edge.");
	}
	for(auto id : catalog->index_sets) {
		if(catalog->index_sets[id]->sub_indexed_to == index_set_id) {
			source_loc.print_error_header();
			fatal_error("Can not set a position map for index sets that have other index sets sub-indexed to it.");
		}
	}
	
	data.backup_counts = data.index_counts; // We need these in case we want to write them out to a file again.
	
	// Resize the index set so that it the previously provided size is now reinterpreted as a "max position" instead.
	for(int instanceidx = 0; instanceidx < data.index_counts.size(); ++instanceidx) {
		double max_width = (double)data.index_counts[instanceidx];
		
		int idx = 0;
		for(; idx < pos_vals.size(); ++idx) {
			if(pos_vals[idx] > max_width) break;
		}
		data.index_counts[instanceidx] = idx;
	}
	
	data.has_index_position_map = true;
	data.pos_vals = pos_vals;
}


s32
Index_Record::map_index(double value) {
	
	if(!has_index_position_map)
		fatal_error(Mobius_Error::internal, "Called map_index on an index set that doesn't have an index_map.");
	
	s32 size = (s32)pos_vals.size();
	
	double prev = 0.0;
	for(s32 idx = 0; idx < (s32)pos_vals.size(); ++idx) {
		if(prev <= value && value < pos_vals[idx]) return idx;
	}
	return -1;
	
	// Could finish implementing the smart search, but it is maybe not necessary for this use case.
	// binary search with "smart" guessing
	/*
	s32 guess = s32(double(size)*value/(pos_vals[size-1]));
	while(true) {
		guess = std::max(std::min(guess, size-1), 0);
		
		if(pos_vals[guess] > value) {
			if(guess == 0 || pos_vals[guess-1] <= value) break;
			else {
				guess -= 
			}
		} else {
			if(guess == size-1 || pos_vals[guess+1] > value) break;
			else {
				//TODO!
				guess +=
			}
		}
	}
	return guess;
	*/
}

double
Index_Data::get_position(Index_T index) {
	auto &data = index_data[index.index_set.id];
	
	if(data.has_index_position_map) {
		if(index.index < 0 || index.index >= data.pos_vals.size())
			fatal_error(Mobius_Error::internal, "Index out of bounds when looking up position.");
		return data.pos_vals[index.index];
	}
	return (double)index.index;
}

bool
Index_Data::has_position_map(Entity_Id index_set_id) {
	return index_data[index_set_id.id].has_index_position_map;
}


void
Index_Data::transfer_data(Index_Data &other, Entity_Id data_id) {
	
	auto target_id = map_id(catalog, other.catalog, data_id);
	
	auto set_data = catalog->index_sets[data_id];
	if(!is_valid(target_id)) {
		// TODO: Should be just a warning here instead, but then we have to follow up and make it properly handle declarations of series data that is indexed over this index set.
		set_data->source_loc.print_error_header();
		fatal_error("\"", set_data->name, "\" has not been declared as an index set in the model \"", other.catalog->model_name, "\".");
	}
	auto set = other.catalog->index_sets[target_id];
	
	if(!set->union_of.empty()) {
		// Check that the unions match
		bool error = false;
		if(set_data->union_of.size() != set->union_of.size())
			error = true;
		else {
			int idx = 0;
			for(auto ui_id : set_data->union_of) {
				auto ui_id_model = map_id(catalog, other.catalog, ui_id);
				
				if(!is_valid(ui_id_model) || ui_id_model != set->union_of[idx]) {
					error = true;
					break;
				}
				++idx;
			}
		}
		
		if(error) {
			set_data->source_loc.print_error_header();
			fatal_error("The index set \"", set_data->name, "\" is declared as a union in the model, but is not the same union in the data set.");
		}
		
		other.initialize_union(target_id, set_data->source_loc);
		return; // NOTE: There is no separate index data to copy for a union.
		
	} else if (!set_data->union_of.empty()) {
		set_data->source_loc.print_error_header();
		fatal_error("The index set \"", set_data->name, "\" is not declared as a union in the model, but is in the data set");
	}
	
	Entity_Id sub_indexed_to = invalid_entity_id;
	if(is_valid(set_data->sub_indexed_to))
		sub_indexed_to = map_id(catalog, other.catalog, set_data->sub_indexed_to);
	if(set->sub_indexed_to != sub_indexed_to) {
		set_data->source_loc.print_error_header();
		fatal_error("The sub-indexing of the index set \"", set_data->name, "\" does not match between the model and the data set.");
	}
	
	auto &data = index_data[data_id.id];
	
	for(int super = 0; super < data.index_counts.size(); ++super) {
		Index_T parent_idx = Index_T { sub_indexed_to, super };
		if(!is_valid(sub_indexed_to))
			parent_idx = Index_T::no_index();
		
		other.initialize(target_id, parent_idx, data.type, set_data->source_loc);
		
		auto &new_data = other.index_data[target_id.id];
		
		new_data.index_counts[super] = data.index_counts[super];
		if(data.type == Index_Record::Type::numeric1) {
			// Nothing else to do.
		} else if (data.type == Index_Record::Type::named) {
			new_data.index_names[super] = data.index_names[super];
			new_data.name_to_index[super] = data.name_to_index[super];
		} else
			fatal_error(Mobius_Error::internal, "Unimplemented index data type.");
	}
	
	// Transfer position maps.
	auto &other_data = other.index_data[target_id.id];
	other_data.has_index_position_map = data.has_index_position_map;
	other_data.pos_vals = data.pos_vals;
}