
#ifndef MOBIUS_GROUPED_TOPOLOGICAL_SORT_H
#define MOBIUS_GROUPED_TOPOLOGICAL_SORT_H

#include <stdint.h>
#include <algorithm>
#include <vector>
#include <set>
#include <functional>

/*
Example of how one could make a sorting predicate for the functions
	topological_sort
	find_all_circuits
	find_strongly_connected_components
	
The 'Graph_Sorting_Predicate::graph' vector has the same length as the number of nodes in the graph it models.
For each node_index (int), graph[node_index] contains
	1. bool - if the node should participate in the sorting nor not.
	2. A set of other nodes pointed at by this node (the adjacency set).

There are also many other ways to do it. The predicate doesn't need to own the data it refers to it could hold for instance a pointer.
Moreover 'edges' does not need to return a std::set reference, only something that is iterable over 'int'.
*/
struct
Graph_Sorting_Predicate {
	std::vector<std::pair<bool, std::set<int>>> graph;
	inline bool participates(int node) {  return graph[node].first; }
	inline const std::set<int> &edges(int node) { return graph[node].second; }
};

/*
This is the depth-first search topological sort algorithm.
https://en.wikipedia.org/wiki/Topological_sorting
Tarjan (1976), "Edge-disjoint spanning trees and depth-first search", Acta Informatica, 6 (2)
The primary use case is to actually do the sorting, but it will also report a circuit if it finds one (in which case the sorting is impossible).
*/
template <typename Predicate, typename itype = int>
void
topological_sort(Predicate &predicate, std::vector<itype> &sorted, itype n_elements, const std::function<void(const std::vector<itype> &)> &report_circuit, itype zeroval = 0) {
	
	struct
	Visit_Helper {
		std::vector<uint8_t> temp_visited;
		std::vector<uint8_t> visited;
		
		std::vector<itype> potential_cycle;
		std::vector<itype> *sorted;
		Predicate *predicate;
		
		Visit_Helper(itype size, Predicate *predicate, std::vector<itype> *sorted) : temp_visited(size_t(size), false), visited(size_t(size), false), predicate(predicate), sorted(sorted) {}
		
		bool
		visit(itype node) {
			if(!predicate->participates(node)) return false;
			if(visited[size_t(node)])          return false;
			potential_cycle.push_back(node);
			if(temp_visited[size_t(node)])     return true;

			temp_visited[size_t(node)] = true;
			for(itype other_node : predicate->edges(node)) {
				bool cycle = visit(other_node);
				if(cycle) return true;
			}
			potential_cycle.resize(potential_cycle.size() - 1);
			visited[size_t(node)]      = true;
			sorted->push_back(node);
			return false;
		}
	};
	
	sorted.clear();
	Visit_Helper visits(n_elements, &predicate, &sorted);
	for(itype node = zeroval; node < n_elements; ++node) {
		visits.potential_cycle.clear();
		bool is_cycle = visits.visit(node);
		if(is_cycle) {
			auto &cycle = visits.potential_cycle;
			// Remove the first part that was not part of the cycle.
			itype starts_at = cycle.back(); // If there was a cycle, it starts at what it ended at
			auto start = std::find(cycle.begin(), cycle.end(), starts_at);
			cycle.erase(cycle.begin(), start+1);
			report_circuit(cycle);
			return; // It is not possible to sort this graph.
		}
	}
}

/*
Johnson's algorithm for finding all simple circuits in a graph.
Johson (1975), "Finding all the elementary circuits of a directed graph.", SIAM J. Comput. 4(1)
See also
https://arxiv.org/pdf/2105.10094.pdf
Note: We didn't end up using this in Mobius after all, but we keep it here in case somebody else want to use it.
*/
template<typename Predicate>
void
find_all_circuits(Predicate &predicate, int n_elements, const std::function<void(const std::vector<int> &)> &output_circuit) {
	
	struct
	Visit_Helper {
		std::vector<uint8_t>       blocked;
		std::vector<std::set<int>> Blist;
		
		std::vector<int> visit_stack;
		Predicate *predicate;
		
		Visit_Helper(int size, Predicate *predicate) : blocked(size, false), Blist(size), predicate(predicate) {}
		
		inline void
		unblock(int node) {
			blocked[node] = false;
			for(int other : Blist[node]) {
				if(blocked[other])
					unblock(other);
			}
			Blist[node].clear();
		}
		
		bool
		visit(int node, int start_node, const std::function<void(const std::vector<int> &)> &output_circuit) {
			if(!predicate->participates(node)) return false;
			bool circuit = false;
			visit_stack.push_back(node);
			blocked[node] = true;
			for(int other : predicate->edges(node)) {
				if(other < start_node) continue;
				if(other == start_node) {
					output_circuit(visit_stack);
					circuit = true;
				} else if (!blocked[other]) {
					if(visit(other, start_node, output_circuit))
						circuit = true;
				}
			}
			if(circuit)
				unblock(node);
			else {
				for(int other : predicate->edges(node)) {
					if(other < start_node) continue;
					Blist[other].insert(node);
				}
			}
			visit_stack.resize(visit_stack.size()-1);
			return circuit;
		}
	};
	
	Visit_Helper visits(n_elements, &predicate);
	
	for(int node = 0; node < n_elements; ++node) {
		for(int other = node; other < n_elements; ++other) {
			visits.blocked[other] = false;
			visits.Blist[other].clear();
		}
		visits.visit(node, node, output_circuit);
	}
}

/*
Path-based strongly connected component algorithm
https://en.wikipedia.org/wiki/Path-based_strong_component_algorithm
Dijkstra, Edsger (1976), A Discipline of Programming, NJ: Prentice Hall, Ch. 25.
*/
template <typename Predicate>
void
find_strongly_connected_components(Predicate &predicate, int n_elements, const std::function<void(const std::vector<int> &)> &output_component) {
	
	struct
	Visit_Helper {
		int counter = 0;
		std::vector<int> preorder;
		std::vector<uint8_t> is_assigned;
		std::vector<int> unassigned_stack;
		std::vector<int> ambiguous_stack;
		Predicate *predicate;
		
		Visit_Helper(int size, Predicate *predicate) : 
			preorder(size, -1), is_assigned(size, false), predicate(predicate) {}
		
		void
		visit(int node, const std::function<void(const std::vector<int> &)> &output_component) {
			if(!predicate->participates(node)) return;
			
			preorder[node] = counter++;
			unassigned_stack.push_back(node);
			ambiguous_stack.push_back(node);
			for(int other : predicate->edges(node)) {
				if(!predicate->participates(other)) continue;
				if(preorder[other] == -1)
					visit(other, output_component);
				else if(!is_assigned[other]) {
					while(true) {
						if(ambiguous_stack.empty()) break;
						int top = ambiguous_stack.back();
						if(preorder[top] <= preorder[other]) break;
						ambiguous_stack.resize(ambiguous_stack.size() - 1);
					}
				}
			}
			if(!ambiguous_stack.empty() && node == ambiguous_stack.back()) {
				std::vector<int> new_component;
				while(true) {
					if(unassigned_stack.empty()) break; // Should not happen though. Maybe give an error?
					int top = unassigned_stack.back();
					unassigned_stack.resize(unassigned_stack.size() - 1);
					new_component.push_back(top);
					is_assigned[top] = true;
					if(top == node) break;
				}
				output_component(new_component);
				ambiguous_stack.resize(ambiguous_stack.size() - 1);
			}
		}
	};
	
	Visit_Helper visits(n_elements, &predicate);
	
	for(int node = 0; node < n_elements; ++node) {
		if(visits.preorder[node] == -1)
			visits.visit(node, output_component);
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
			
			for(int idx = (int)group.nodes.size()-1; idx >= 0; --idx) {
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
Strongly_Connected_Component {
	std::set<int> nodes;
	Label label;
	std::set<int> edges_to_cycle;
	std::set<int> blocks_cycle;
};

template<typename Predicate, typename Label>
bool
condense_labeled_strongly_connected_components(Predicate &predicate, std::vector<Strongly_Connected_Component<Label>> &components, int n_elements) {
	
	bool singly_labeled_cycles = true;
	
	find_strongly_connected_components(predicate, n_elements, [&](const std::vector<int> &component) {
		// Figure out the label of the component.
		Label label = predicate.label(component[0]);
		for(int idx = 1; idx < component.size(); ++idx) {
			if(predicate.label(component[idx]) != label)
				singly_labeled_cycles = false;
		}
		Strongly_Connected_Component<Label> new_component;
		new_component.nodes.insert(component.begin(), component.end());
		new_component.label = label;
		components.push_back(std::move(new_component));
	});
	
	std::vector<int> node_is_in_cycle(n_elements, -1);
	
	int max_proper_cycles = components.size();
	
	for(int node = 0; node < n_elements; ++node) {
		if(!predicate.participates(node)) continue;
		for(int cycle_idx = 0; cycle_idx < max_proper_cycles; ++cycle_idx) {
			auto &c = components[cycle_idx];
			if(c.nodes.find(node) != c.nodes.end()) {
				node_is_in_cycle[node] = cycle_idx;
				break;
			}
		}
		if(node_is_in_cycle[node] == -1) {
			//Make "cycles" for the singletons too.
			Strongly_Connected_Component<Label> new_cycle;
			new_cycle.nodes.insert(node);
			new_cycle.label = predicate.label(node);
			components.push_back(std::move(new_cycle));
			node_is_in_cycle[node] = components.size()-1;
		}
	}
	
	for(int cycle_idx = 0; cycle_idx < components.size(); ++cycle_idx) {
		auto &cycle = components[cycle_idx];
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
label_grouped_topological_sort_additional_weak_constraint(
	Predicate &predicate, std::vector<Node_Group<Label>> &groups, 
	std::vector<Strongly_Connected_Component<Label>> &max_components, 
	int n_elements, int max_passes = 10
) {
	
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
		std::vector<Strongly_Connected_Component<Label>> *collapsed_graph;
		
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
	
	// Condense strongly connected components (graph with weak+strong edges) into single nodes.
	bool singly_labeled_cycles = condense_labeled_strongly_connected_components(cycle_predicate, max_components, n_elements);
	if(!singly_labeled_cycles)
		fatal_error(Mobius_Error::internal, "The cycles were not singly labeled."); // This is a user error, it is not possible if the algorithm is used correctly.
	
	// TODO: We could simplify the algorithm in the case where we only get singleton components by grouping those directly instead of working with the condensed graph (No need to construct a new graph predicate and then expanding the result after).
	
	Cycle_Sorting_Predicate sort_predicate { &max_components };
	std::vector<int> sorted_components;

	topological_sort<Cycle_Sorting_Predicate, int>(sort_predicate, sorted_components, max_components.size(), [&](const std::vector<int> &cycle){
		// Note: There should be no cycles in the condensed graph (or the component finding algorithm is wrong)
		fatal_error(Mobius_Error::internal, "Got a cycle among the cycles!!!\n");
	});
	
	std::vector<Node_Group<Label>> cycle_groups;
	label_grouped_sort_first_pass(sort_predicate, cycle_groups, sorted_components);
	
	bool success = optimize_label_group_packing(sort_predicate, cycle_groups, max_passes);
	if(!success)
		fatal_error(Mobius_Error::internal, "Unable to pack cycle groups optimally.");
	
	struct
	Cycle_Internal_Sort_Predicate {
		std::vector<std::set<int>> edges_;
		
		inline bool participates(int node) { return true; }
		inline std::set<int> &edges(int node) { return edges_[node]; }
		Cycle_Internal_Sort_Predicate(int size) : edges_(size) {}
	};
	
	// Unpack the nodes from the condensed groups into the groups for the original graph.
	for(auto &group : cycle_groups) {
		Node_Group<Label> unpacked;
		unpacked.label = group.label;
		for(int cycle_idx : group.nodes) {
			auto &cycle = max_components[cycle_idx];
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
				// Alternatively, we could reuse the graph sorting predicate declared at the top of the file.
				Cycle_Internal_Sort_Predicate sort_predicate2(cycle_nodes.size());
				for(int idx = 0; idx < cycle_nodes.size(); ++idx) {
					for(int edg : predicate.edges(cycle_nodes[idx])) {
						int edg_idx = idx_of_node[edg];
						if(edg_idx >= 0)
							sort_predicate2.edges_[idx].insert(edg_idx);
					}
				}
				
				std::vector<int> sorted;
				topological_sort<Cycle_Internal_Sort_Predicate, int>(sort_predicate2, sorted, cycle_nodes.size(), [&](const std::vector<int> &cycle){
					fatal_error(Mobius_Error::internal, "Unable to unpack-sort a cycle.");
				});
				
				for(int idx : sorted)
					unpacked.nodes.push_back(cycle_nodes[idx]);
			}
		}
		
		groups.push_back(std::move(unpacked));
	}
	
	return true;
}




#endif // MOBIUS_GROUPED_TOPOLOGICAL_SORT_H