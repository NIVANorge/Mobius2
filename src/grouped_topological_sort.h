
#ifndef MOBIUS_GROUPED_TOPOLOGICAL_SORT_H
#define MOBIUS_GROUPED_TOPOLOGICAL_SORT_H

#include <stdint.h>
#include <algorithm>
#include <vector>
#include <set>

/*
Example of how one could make a sorting predicate for topological_sort.
The graph vector has the same length as the number of nodes.
For each node_index (int), graph[node_index] contains
	1. bool - if the node should participate in the sorting nor not.
	2. A set of other nodes pointed at by this node (or which point at this node - depending on what order you want to sort in).
The result is a vector<int> of sorted node indexes.
There are also many other ways to do it. The predicate doesn't need to own the data it refers to it could hold for instance a pointer.
Moreover 'edges' does not need to return a std::set, only something iterable over int.
*/
struct
Graph_Sorting_Predicate {
	std::vector<std::pair<bool, std::set<int>>> graph;
	inline bool participates(int node) {  return graph[node].first; }
	inline const std::set<int> &edges(int node) { return graph[node].second; }
};

/*
This is the Tarjan (76) topological sort algorithm.
https://en.wikipedia.org/wiki/Topological_sorting     (TODO: Proper reference)
The primary use case is to actually do the sorting, but it will also report a cycle if it finds one (in which case the sorting is impossible).
*/
template <typename Predicate>
bool
topological_sort(Predicate &predicate, std::vector<int> &sorted, int n_elements, std::vector<int> &potential_cycle) {
	
	struct
	Visitation_Record {
		std::vector<uint8_t> temp_visited;
		std::vector<uint8_t> visited;
		
		Visitation_Record(int size) : temp_visited(size, false), visited(size, false) {}
		
		// TODO: Could maybe just store a pointer to predicate, sorted and potential_cycle in the struct so that we don't pass them in each call.
		bool
		visit(Predicate &predicate, int node, std::vector<int> &sorted, std::vector<int> &potential_cycle) {

			if(!predicate.participates(node)) return false;
			if(visited[node])                 return false;
			potential_cycle.push_back(node);
			if(temp_visited[node])            return true;

			temp_visited[node] = true;
			for(int other_node : predicate.edges(node)) {
				bool cycle = visit(predicate, other_node, sorted, potential_cycle);
				if(cycle) return true;
			}
			potential_cycle.resize(potential_cycle.size() - 1);
			visited[node]      = true;
			sorted.push_back(node);
			return false;
		}
	};
	
	sorted.clear();
	Visitation_Record visits(n_elements);
	for(int node = 0; node < n_elements; ++node) {
		potential_cycle.clear();
		bool cycle = visits.visit(predicate, node, sorted, potential_cycle);
		if(cycle) {
			// Remove the first part that was not part of the cycle.
			int starts_at = potential_cycle.back(); // If there was a cycle, it starts at what it ended at
			auto start = std::find(potential_cycle.begin(), potential_cycle.end(), starts_at);
			potential_cycle.erase(potential_cycle.begin(), start+1);
			return false;
		}
	}
	return true;
}

// Johnson's algorithm for finding all simple ciruits in a graph.
// TODO: Put proper reference.
//https://www.cs.tufts.edu/comp/150GA/homeworks/hw1/Johnson%2075.PDF
// See also
// https://arxiv.org/pdf/2105.10094.pdf

template<typename Predicate>
void
find_all_circuits(Predicate &predicate, int n_elements, const std::function<void(const std::vector<int> &)> &output_circuit) {
	
	struct
	Blocking_Tracker {
		std::vector<uint8_t>       blocked;
		std::vector<std::set<int>> Blist;
		
		std::vector<int> visit_stack;
		
		Blocking_Tracker(int size) : blocked(size, false), Blist(size) {}
		
		inline void
		unblock(int node) {
			blocked[node] = false;
			for(int other : Blist[node]) {
				if(blocked[other])
					unblock(other);
			}
			Blist[node].clear();
		}
		
		// TODO: Could probably store pointers/references to predicate, and output_circuit so that we don't have to pass them in every function call.
		bool
		visit(Predicate &predicate, int node, int start_node, const std::function<void(const std::vector<int> &)> &output_circuit) {
			if(!predicate.participates(node)) return false;
			bool circuit = false;
			visit_stack.push_back(node);
			blocked[node] = true;
			for(int other : predicate.edges(node)) {
				if(other < start_node) continue;
				if(other == start_node) {
					output_circuit(visit_stack);
					circuit = true;
				} else if (!blocked[other]) {
					if(visit(predicate, other, start_node, output_circuit));
						circuit = true;
				}
			}
			if(circuit)
				unblock(node);
			else {
				for(int other : predicate.edges(node)) {
					if(other < start_node) continue;
					Blist[other].insert(node);
				}
			}
			visit_stack.resize(visit_stack.size()-1);
			return circuit;
		}
	};
	
	Blocking_Tracker tracker(n_elements);
	
	for(int node = 0; node < n_elements; ++node) {
		for(int other = node; other < n_elements; ++other) {
			tracker.blocked[other] = false;
			tracker.Blist[other].clear();
		}
		tracker.visit(predicate, node, node, output_circuit);
	}
}


template<typename Label>
struct
Node_Group {
	Label label;
	std::vector<int> nodes;
};

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
Constraint_Cycle {
	std::set<int> nodes;
	Label label;
	std::set<int> edges_to_cycle;
	std::set<int> blocks_cycle;
};

template<typename Predicate, typename Label>
bool
find_maximal_labeled_cycles(Predicate &predicate, std::vector<Constraint_Cycle<Label>> &max_cycles, int n_elements) {
	
	// TODO: Properly document this algorithm
	
	// Find maximal cycles by finding simple circuits, then merging those that overlap (hopefully this is the fastest way to do it ?).
	
	bool singly_labeled_cycles = true;
	
	find_all_circuits(predicate, n_elements, [&](const std::vector<int> &circuit) {
		
		Label label = predicate.label(circuit[0]);
		for(int idx = 1; idx < circuit.size(); ++idx) {
			if(predicate.label(circuit[idx]) != label)
				singly_labeled_cycles = false;
		}
		
		// See if any of the nodes in the new circuit are in another cycle (in that case merge the circuit into ).
		std::vector<int> found_in;
		for(int cycle_idx = 0; cycle_idx < max_cycles.size(); ++cycle_idx) {
			auto &existing_cycle = max_cycles[cycle_idx];
			if(std::any_of(circuit.begin(), circuit.end(), [&](int node) {
				return existing_cycle.nodes.find(node) != existing_cycle.nodes.end();
			}))
				found_in.push_back(cycle_idx);
		}
		if(found_in.empty()) {
			// It was not found in a previous cycle, so we create a new one.
			Constraint_Cycle<Label> new_cycle;
			new_cycle.nodes.insert(circuit.begin(), circuit.end());
			new_cycle.label = label;
			max_cycles.push_back(std::move(new_cycle));
		} else {
			// Merge other cycles together including the new circuit that overlaps them.
			// TODO: Check the labels (return false if mismatch)
			auto &c = max_cycles[found_in[0]];
			c.nodes.insert(circuit.begin(), circuit.end());
			if(c.label != label) singly_labeled_cycles = false;
			for(int idxidx = (int)found_in.size()-1; idxidx > 0; --idxidx) { // NOTE: > 0 to omit the first one, which we already set.
				int cycle_idx = found_in[idxidx];
				auto &c2 = max_cycles[cycle_idx];
				c.nodes.insert(c2.nodes.begin(), c2.nodes.end());
				max_cycles.erase(max_cycles.begin()+cycle_idx);
				if(c2.label != label) singly_labeled_cycles = false;
			}
		}
	});
	
	std::vector<int> node_is_in_cycle(n_elements, -1);
	
	int max_proper_cycles = max_cycles.size();
	
	for(int node = 0; node < n_elements; ++node) {
		if(!predicate.participates(node)) continue;
		for(int cycle_idx = 0; cycle_idx < max_proper_cycles; ++cycle_idx) {
			auto &c = max_cycles[cycle_idx];
			if(c.nodes.find(node) != c.nodes.end()) {
				node_is_in_cycle[node] = cycle_idx;
				break;
			}
		}
		if(node_is_in_cycle[node] == -1) {
			//Make "cycles" for the singletons too.
			Constraint_Cycle<Label> new_cycle;
			new_cycle.nodes.insert(node);
			new_cycle.label = predicate.label(node);
			max_cycles.push_back(std::move(new_cycle));
			node_is_in_cycle[node] = max_cycles.size()-1;
		}
	}
	
	for(int cycle_idx = 0; cycle_idx < max_cycles.size(); ++cycle_idx) {
		auto &cycle = max_cycles[cycle_idx];
		for(int node : cycle.nodes) {
			for(int other : predicate.edges(node)) {
				int other_cycle = node_is_in_cycle[other];
				if(other_cycle >= 0 && (other_cycle != cycle_idx))
					cycle.edges_to_cycle.insert(other_cycle);
			}
			for(int other : predicate.blocks(node)) {
				// TODO: We need to check that a cycle doesn't block itself!
				if(node_is_in_cycle[other] >= 0)
					cycle.blocks_cycle.insert(node_is_in_cycle[other]);
			}
		}
	}
	
	return singly_labeled_cycles;
}

template <typename Predicate, typename Label>
bool
label_grouped_topological_sort_additional_weak_constraint(Predicate &predicate, std::vector<Node_Group<Label>> &groups, std::vector<Constraint_Cycle<Label>> &max_cycles, int n_elements, int max_passes = 10) {
	
	// TODO: Properly document this algorithm
	
	struct
	Cycle_Finding_Predicate {
		std::vector<std::set<int>> *all_edges;
		Predicate                  *base_predicate;
		inline bool participates(int node) { return base_predicate->participates(node); }
		inline std::set<int> &edges(int node) { return (*all_edges)[node]; }
		inline const std::set<int> &blocks(int node) { return base_predicate->blocks(node); }
		inline Label label(int node) { return base_predicate->label(node); }
	};
	
	struct
	Cycle_Sorting_Predicate {
		std::vector<Constraint_Cycle<Label>> *collapsed_graph;
		
		inline bool depends(int node, int on) {
			auto &edg = (*collapsed_graph)[node].edges_to_cycle;
			return edg.find(on) != edg.end();
		}
		inline bool blocks(int node, int on) {
			auto &blk = (*collapsed_graph)[node].blocks_cycle;
			return blk.find(on) != blk.end();
		}
		inline std::set<int> &edges(int node) { return (*collapsed_graph)[node].edges_to_cycle; }
		inline Label label(int node) { return (*collapsed_graph)[node].label; }
		inline bool allow_move(Label label) { return true; } // It doesn't seem necessary to freeze any for this pass.
		inline bool participates(int node) { return true; }
	};
	
	std::vector<std::set<int>> all_edges(n_elements);
	Cycle_Finding_Predicate cycle_predicate { &all_edges, &predicate };
	
	for(int node = 0; node < n_elements; ++node) {
		auto &edg = predicate.edges(node);
		auto &wedg = predicate.weak_edges(node);
		all_edges[node].insert(edg.begin(), edg.end());
		all_edges[node].insert(wedg.begin(), wedg.end());
	}
	
	// Collapse cycles consisting of weak+strong edges into single nodes.
	bool singly_labeled_cycles = find_maximal_labeled_cycles(cycle_predicate, max_cycles, n_elements);
	// TODO: Error if !singly_labeled_cycles ( This is not possible if the algorithm is used correctly though ).
	if(!singly_labeled_cycles)
		fatal_error(Mobius_Error::internal, "The cycles were not singly labeled.");
	
	// TODO: We could simplify (speed up) the algorithm if we only get singleton cycles.
	
	// Note: There should be no cycles among the cycles (or the cycle grouping algorithm is wrong), so we should not have to check for that in this case.
	Cycle_Sorting_Predicate sort_predicate { &max_cycles };
	std::vector<int> sorted_cycles, potential_cycle;
	bool success = topological_sort(sort_predicate, sorted_cycles, max_cycles.size(), potential_cycle);
	if(!success) {
		begin_error(Mobius_Error::internal);
		error_print("Got a cycle among the cycles!!!\n");
		
		for(int cycle_idx : potential_cycle) {
			error_print(cycle_idx, ": ");
			for(int node : max_cycles[cycle_idx].nodes)
				error_print(node, " ");
			error_print("\n");
		}
		// TODO: Print this out.
		
		mobius_error_exit();
	}
	
	//log_print("Cycles length: ", max_cycles.size(), "\n");
	//log_print("Sorted cycles length: ", sorted_cycles.size(), "\n");
	
	std::vector<Node_Group<Label>> cycle_groups;
	label_grouped_sort_first_pass(sort_predicate, cycle_groups, sorted_cycles);
	
	//TODO: Maybe make different error codes for this function.
	success = optimize_label_group_packing(sort_predicate, cycle_groups, max_passes);
	if(!success)
		fatal_error(Mobius_Error::internal, "Unable to pack cycle groups optimally.");
	//if(!success) return false;
	
	
	struct
	Cycle_Internal_Sort_Predicate {
		std::vector<std::set<int>> edges_;
		
		inline bool participates(int node) { return true; }
		inline std::set<int> &edges(int node) { return edges_[node]; }
		Cycle_Internal_Sort_Predicate(int size) : edges_(size) {}
	};
	
	// Then unpack the nodes from the cycles into the resulting groups.
	for(auto &group : cycle_groups) {
		Node_Group<Label> unpacked;
		unpacked.label = group.label;
		for(int cycle_idx : group.nodes) {
			auto &cycle = max_cycles[cycle_idx];
			if(cycle.nodes.size() == 1) {
				unpacked.nodes.push_back(*cycle.nodes.begin());
			} else {
				// Topological-sort cycle_nodes on the strong edges and put them into unpacked.nodes.
				// TODO: Could maybe have optimizations of this for small cycles e.g size 2, 3.
				
				std::vector<int> cycle_nodes(cycle.nodes.begin(), cycle.nodes.end());
				std::vector<int> idx_of_node(n_elements, -1);
				for(int idx = 0; idx < cycle_nodes.size(); ++idx)
					idx_of_node[cycle_nodes[idx]] = idx;
				
				// We have to reindex the edges for the internal topological sort.
				// TODO: If we made an iterator for it we would not need to allocate the new edges as sets.
				Cycle_Internal_Sort_Predicate sort_predicate2(cycle_nodes.size());
				for(int idx = 0; idx < cycle_nodes.size(); ++idx) {
					for(int edg : predicate.edges(cycle_nodes[idx])) {
						int edg_idx = idx_of_node[edg];
						if(edg_idx >= 0)
							sort_predicate2.edges_[idx].insert(edg_idx);
					}
				}
				
				std::vector<int> sorted;
				potential_cycle.clear();
				bool success = topological_sort(sort_predicate2, sorted, cycle_nodes.size(), potential_cycle);
				//if(!success) return false;
				if(!success)
					fatal_error(Mobius_Error::internal, "Unable to unpack-sort a cycle.");
				
				for(int idx : sorted)
					unpacked.nodes.push_back(cycle_nodes[idx]);
			}
		}
		
		groups.push_back(std::move(unpacked));
	}
	
	return true;
}




#endif // MOBIUS_GROUPED_TOPOLOGICAL_SORT_H