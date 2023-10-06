




// Have to pass some predicates for 'is_valid', 'all_dependencies', 'depends', 'loose_depends' and 'label'

// Hmm, have to make an error trace somehow so that we can document cyclical dependencies.

// TODO: Could we do without knowing the label type?
//    The problem is that we need to keep an identifier of it in the Group
//    Could work if the impl. of the Sort_Predicate has a map Label->int so that labels are always ints in this use case.
typedef u64 uint64_t;

struct
Visitation_Record {
	bool temp_visited = false;
	bool visited      = false;
};

// We also need a modified version of this that keeps track of cycles. Could that be combined somehow (using the "stack trace" we want to implement below) ?
//  That has slightly different requirements as it should not short-circuit the first time it finds a cycle.

bool
topological_sort_visit(Sort_Predicate &predicate, std::vector<Visitation_Record> &visits, int node, std::vector<int> &sorted) { //...

	if(!predicate.is_valid(node)) return true;
	if(visits[node].visited)      return true;
	if(visits[node].temp_visited) {
		// Do stack trace somehow. E.g. stored in a vector, but we have to both push to and remove from it when we return true.
		return false;
	}
	visits[node].temp_visited = true;
	for(int other_node : predicate.edges(node)) {
		bool success = topological_sort_visit(predicate, visits, other_node, sorted);
		if(!success) {
			// Continue the stack trace
			return false;
		}
	}
	visits[node].visited = true;
	sorted.push_back(node);
	return true;
}

void
topological_sort(Sort_Predicate &predicate, std::vector<int> &sorted, int n_elements) {
	
	indexes_out.clear();
	std::vector<Visitation_Record> visits(n_elements);
	for(int node = 0; node < n_elements; ++node) {
		bool success = topological_sort_visit(predicate, visits, node, sorted);
		if(!success) {
			// ... TODO pass cycle to caller.
			return false;
		}
	}
	return true;
}

bool
label_grouped_topological_sort_first_pass(Sort_Predicate &predicate, std::vector<Node_Group> &groups, int n_elements) {
	
	// TODO: Thoroughly document the algorithm
	
	std::vector<int> sorted;
	bool success = topological_sort(predicate, sorted, n_elements);
	
	if(!success) return false; // TODO: May pass error cycle to caller so that they can report it?
	
	groups.clear();
	for(int node : sorted) {
		u64 label = predicate.label(node);
		
		int earliest_suitable_group = groups.size();
		int earliest_suitable_pos   = groups.size();
		
		for(int group_idx = groups.size()-1; group_idx >= 0; --group_idx) {
			
			auto &group = groups[group_idx];
			
			bool blocked = false;
			bool found_dependency = false;
			
			for(int other_node : group.nodes) {
				if(predicate.depends(node, other_node))
					found_dependency = true;
				if(predicate.blocks(node, other_node))
					blocked = true;
			}
			
			if((group.label == label) && !blocked)
				earliest_possible_group = group_idx;
			
			if(found_dependency || blocked) break;
			earliest_suitable_pos = group_idx;
		}
		if(earliest_possible_group < groups.size())
			groups[earliest_possible_group].nodes.push_back(node);
		else
			groups.emplace(groups.begin() + earliest_suitable_pos, {label, {node}});
	}
	return true;
}

bool
optimize_groupings(Sort_Predicate &predicate, std::vector<Node_Group> &groups, int max_passes = 10) {
	bool changed = false;
	
	for(int pass = 0; pass < max_passes; ++pass) {
		changed = false;
		
		for(int group_idx = 0; group_idx < groups_out.size(); ++group_idx) {
			auto &group = groups_out[group_idx];
			
			for(int idx = group.nodes.size()-1; idx > 0; --idx) {
				int node = group.nodes[idx];
				
				// If another instruction behind us in the same batch depends on us, we are not allowed to move.
				bool cont = false;
				for(int behind_idx = idx+1; behind_idx < group.nodes.size(); ++behind_idx){
					
					int behind = group.nodes[behind_idx];
					if(predicate.depends(behind, node)) {
						cont = true;
						break;
					}
				}
				if(cont) continue;
				
				int last_suitable_group = group_idx;
				for(group_behind_idx = group_idx + 1; group_behind_idx < groups_out.size(); ++ group_behind_idx) {
					
					auto &group_behind = groups_out[group_behind_idx];
					bool group_depends_on_node = false;
					bool group_is_blocked      = false;
					for(int behind : group_behind.nodes) {
						if(predicate.depends(behind, node)) group_depends_on_node = true;
						if(predicate.blocks(behind, node))  group_is_blocked      = true;   // TODO: Figure out the terminology here.
					}
					if(!group_is_blocked && (group.label == group_behind.label))
						last_suitable_group = group_behind_idx;
					if(group_depends_on_node || group_behind_idx = groups_out.size() - 1) {
						if(group_behind_idx != group_idx) {
							// We are allowed to move to a later group. Move to the beginning of the last other group that is suitable.
							auto &move_to_group = groups_out[last_suitable_group];
							move_to_group.nodes.insert(move_to_group.nodes.begin(), node);
							group.nodes.erase(group.nodes.begin() + idx);
							changed = true;
						}
						break;
					}
				}
			}
		}
		// If we did a full pass without being able to move more nodes, we are finished.
		if(!changed) break;
	}
	
	// Remove groups that were left empty after moving out all their nodes.
	for(int group_idx = (int)groups_out.size()-1; group_idx >= 0; --group_idx) {
		auto &group = groups_out[group_idx];
		if(group.nodes.empty())
			groups_out.erase(groups_out.begin() + group_idx); // NOTE: OK since we are iterating group_idx backwards.
	}
	
	if(changed)    // If something changed during the last pass, we may not have reached an optimal "steady state"
		return false;
	return true;
}

/*
template <typename Element, typename Label>
void
label_grouped_topological_sort_with_loose(const std::vector<Element> &elements) {
	
	// Would assume that the label of all groups are the same, but that should be fine. (should sanity check for it)
	
	// Group by loose cycles, then sort the groups using label_grouped_topological_sort
	//   Have to make a new predicate that works correctly...
	
}
*/