
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

/*
Example of how one could make a sorting predicate for topological_sort.
The graph vector has the same length as the number of nodes.
For each node_index (int), graph[node_index] contains
	1. bool - if the node should participate in the sorting nor not.
	2. A set of other nodes pointed at by this node (or which point at this node - depending on what order you want to sort in).
The result is a vector<int> of sorted node indexes.
There are also many other ways to do it. The predicate doesn't need to own the data it refers to it could hold for instance a pointer.
Also 'edges' does not need to return a std::set, only something that can give an int iterator.
*/
struct
Graph_Sorting_Predicate {
	std::vector<std::pair<bool, std::set<int>>> graph;
	bool participates(int node) {  return graph[node].first; }
	const std::set<int> &edges(int node) { return graph[node].second; }
};

template <typename Predicate>
bool
topological_sort_visit(Predicate &predicate, std::vector<Visitation_Record> &visits, int node, std::vector<int> &sorted, std::vector<int> &potential_cycle) {

	if(!predicate.participates(node)) return true;
	if(visits[node].visited)      return true;
	potential_cycle.push_back(node);
	if(visits[node].temp_visited) return false;

	visits[node].temp_visited = true;
	for(int other_node : predicate.edges(node)) {
		bool no_cycle = topological_sort_visit(predicate, visits, other_node, sorted, potential_cycle);
		if(!no_cycle) return false;
	}
	potential_cycle.resize(potential_cycle.size() - 1);
	visits[node].visited = true;
	sorted.push_back(node);
	return true;
}

template <typename Predicate>
bool
topological_sort(Predicate &predicate, std::vector<int> &sorted, int n_elements, std::vector<int> &potential_cycle) {
	
	sorted.clear();
	std::vector<Visitation_Record> visits(n_elements);
	for(int node = 0; node < n_elements; ++node) {
		potential_cycle.clear();
		bool success = topological_sort_visit(predicate, visits, node, sorted, potential_cycle);
		if(!success) {
			// Remove the first part that was not part of the cycle.
			int starts_at = potential_cycle.back(); // If there was a cycle, it starts at what it ended at
			auto start = std::find(potential_cycle.begin(), potential_cycle.end(), starts_at);
			potential_cycle.erase(potential_cycle.begin(), start); // If we erase to start+1 then the element that triggered the cycle only appears once in it. Maybe that is better.
			return false;
		}
	}
	return true;
}

template <typename Predicate, typename Label>
void
label_grouped_sort_first_pass(Predicate &predicate, std::vector<Node_Group<Label>> &groups, std::vector<int> &sorted_nodes) {
	
	// TODO: Thoroughly document the algorithm
	
	groups.clear();
	for(int node : sorted_nodes) {
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
				earliest_suitable_group = group_idx;
			
			if(found_dependency || blocked) break;
			earliest_suitable_pos = group_idx;
		}
		if(earliest_suitable_group < groups.size())
			groups[earliest_suitable_group].nodes.push_back(node);
		else {
			Node_Group<Label> group;
			group.label = label;
			group.nodes.push_back(node);
			groups.insert(groups.begin() + earliest_suitable_pos, group);
		}
	}
}

template <typename Predicate, typename Label>
bool
optimize_label_group_packing(Predicate &predicate, std::vector<Node_Group<Label>> &groups, int max_passes = 10) {
	
	// TODO: Thoroughly document the algorithm.
	
	bool changed = false;
	
	for(int pass = 0; pass < max_passes; ++pass) {
		changed = false;
		
		for(int group_idx = 0; group_idx < groups.size(); ++group_idx) {
			auto &group = groups[group_idx];
			
			if(!predicate.allow_move(group.label)) continue;
			
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

template<typename Label>
struct
Loose_Constraint_Cycle {
	std::vector<int> nodes;
	Label label;
	std::set<int> depends_on_cycle;
};

// TODO: The predicate used for this one (participates) should also rule out things that are on the wrong solver (even though the "Label" is the index set dependencies)
//   i.e. we don't care about things that are in another batch entirely.
template<typename Predicate, typename Label>
void
find_cycles(Predicate &predicate, std::vector<Loose_Constraint_Cycle<Label>> &cycles, int n_elements) {
	
	// TODO: This is not finished
	
	std::vector<Visitation_Record> visits(n_elements);
	std::vector<int> potential_cycle;
	std::vector<int> visited;
	
	for(int node = 0; node < n_elements; ++node) {
		// We have to do skip if visited, otherwise we end up re-processing things that were already processed as part of a cycle.
		if(visits[node].visited) continue;
		
		potential_cycle.clear();
		visited.clear();
		bool no_cycle = topological_sort_visit(predicate, visits, node, visited, potential_cycle);
		int first_on_cycle = potential_cycle.back();
		if(!no_cycle) {
			
		} else {
			for(int node : visited) {
				//cycles.push_back(
			}
		}
	}
	
	// TODO: One element could be part of multiple cycles, so we have to merge cycles that have overlapping elements.
	//   The problem is that since it is "visited" the first time, the second cycle won't register! How do we fix that?
	
	// Maybe we need a modfied version of the visit function where it doesn't immediately return when it finds a cycle (??).
	
	return true;
}

// TODO: We should also pass out the cycles since we want to know that for one of the applications.
template <typename Predicate, typename Label>
void
label_grouped_topological_sort_additional_constraint(Predicate &predicate, std::vector<Node_Group<Label>> &groups, std::vector<int> &sorted_nodes, int max_passes = 10) {
	
	// TODO: This is not finished
	
	// Would assume that the label of all "loose" cycles are the same, but that should be fine. (should sanity check for it)
	
	// We should pass the cycles out for checking 
	
	// Group by loose cycles, then sort the groups using label_grouped_topological_sort
	//   Have to make a new predicate that works correctly...
	
	// Then unpack the cycles into the groups again.
	
}




#endif // MOBIUS_GROUPED_TOPOLOGICAL_SORT_H