
#ifndef MOBIUS_GROUPED_TOPOLOGICAL_SORT_H
#define MOBIUS_GROUPED_TOPOLOGICAL_SORT_H

#include <stdint.h>

template<typename Label>
struct
Node_Group {
	Label label;
	std::vector<int> nodes;
};

struct
Visitation_Record {
	bool temp_visited = false;
	bool visited      = false;
};

template <typename Sort_Predicate>
bool
topological_sort_visit(Sort_Predicate &predicate, std::vector<Visitation_Record> &visits, int node, std::vector<int> &sorted, std::vector<int> &potential_cycle) { //...

	if(!predicate.is_valid(node)) return true;
	if(visits[node].visited)      return true;
	potential_cycle.push_back(node);
	if(visits[node].temp_visited) return false;

	visits[node].temp_visited = true;
	for(int other_node : predicate.edges(node)) {
		bool success = topological_sort_visit(predicate, visits, other_node, sorted, potential_cycle);
		if(!success) return false;
	}
	potential_cycle.resize(potential_cycle.size() - 1);
	visits[node].visited = true;
	sorted.push_back(node);
	return true;
}

template <typename Sort_Predicate>
bool
topological_sort(Sort_Predicate &predicate, std::vector<int> &sorted, int n_elements, std::vector<int> &potential_cycle) {
	
	sorted.clear();
	std::vector<Visitation_Record> visits(n_elements);
	for(int node = 0; node < n_elements; ++node) {
		potential_cycle.clear();
		bool success = topological_sort_visit(predicate, visits, node, sorted, potential_cycle);
		if(!success) return false;
	}
	return true;
}

/*
template <typename Grouping_Predicate>
bool
label_grouped_sort_first_pass(Grouping_Predicate &predicate, std::vector<Node_Group> &groups, std::vector<int> &sorted) {
	
	// TODO: Thoroughly document the algorithm
	
	groups.clear();
	for(int node : sorted) {
		auto label = predicate.label(node);
		
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
*/

template <typename Grouping_Predicate, typename Label>
bool
optimize_label_groups(Grouping_Predicate &predicate, std::vector<Node_Group<Label>> &groups, int max_passes = 10) {
	
	// TODO: Thoroughly document the algorithm.
	
	bool changed = false;
	
	for(int pass = 0; pass < max_passes; ++pass) {
		changed = false;
		
		for(int group_idx = 0; group_idx < groups.size(); ++group_idx) {
			auto &group = groups[group_idx];
			
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
				
				int last_suitable_group = -1;
				for(int group_behind_idx = group_idx + 1; group_behind_idx < groups.size(); ++ group_behind_idx) {
					
					auto &group_behind = groups[group_behind_idx];
					bool group_depends_on_node = false;
					bool group_is_blocked      = false;
					for(int behind : group_behind.nodes) {
						if(predicate.depends(behind, node)) group_depends_on_node = true;
						if(predicate.blocks(behind, node))  group_is_blocked      = true;   // TODO: Figure out the terminology here.
					}
					if(!group_is_blocked && (group.label == group_behind.label))
						last_suitable_group = group_behind_idx;
					if(group_depends_on_node)
						break;
				}
				if(last_suitable_group > 0) {
					// We are allowed to move to a later group. Move to the beginning of the last other group that is suitable.
					auto &move_to_group = groups[last_suitable_group];
					move_to_group.nodes.insert(move_to_group.nodes.begin(), node);
					group.nodes.erase(group.nodes.begin() + idx);
					changed = true;
				}
			}
		}
		// If we did a full pass without being able to move more nodes, we are finished.
		if(!changed) break;
	}
	
	// Remove groups that were left empty after moving out all their nodes.
	for(int group_idx = (int)groups.size()-1; group_idx >= 0; --group_idx) {
		auto &group = groups[group_idx];
		if(group.nodes.empty())
			groups.erase(groups.begin() + group_idx); // NOTE: OK since we are iterating group_idx backwards.
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




#endif // MOBIUS_GROUPED_TOPOLOGICAL_SORT_H