
#include "model_application.h"


struct
Connection_Node_Data {
	Entity_Id id = invalid_entity_id;
	int receives_count = 0;
	bool visited       = false;
	Indexes indexes;
	std::vector<int> points_at;
	
	Connection_Node_Data() : indexes() {}
};

struct
Match_State {
	bool match;
	int path_idx;  // This is where we had to consume the path to to get the match.
};

Match_State
match_path_recursive(Decl_Scope *scope, std::vector<Connection_Node_Data> &nodes, std::vector<int> &path, int path_idx, Math_Expr_AST *regex) {
	// NOTE: Currently this does greedy matching.
	
	Match_State result = {false, path_idx};
	
	switch(regex->type) {
		case Math_Expr_Type::block : {
			bool mismatch = false;
			for(auto expr : regex->exprs) {
				auto match = match_path_recursive(scope, nodes, path, result.path_idx, expr);
				result.path_idx = match.path_idx;
				if(!match.match) {
					mismatch = true;
					break;
				}	
			}
			if(!mismatch)
				result.match = true;
		} break;
		
		case Math_Expr_Type::regex_quantifier : {
			auto quant = static_cast<Regex_Quantifier_AST *>(regex);
			
			int n_matches = 0;
			
			while(true) {
				auto match = match_path_recursive(scope, nodes, path, result.path_idx, regex->exprs[0]);
				result.path_idx = match.path_idx;
				if(match.match)
					++n_matches;
				else break;
				if((quant->max_matches >= 0 && n_matches > quant->max_matches) || result.path_idx >= path.size()) {
					result.path_idx--; // This is to make the error cursor point at the right position if this results in a full failure.
					break;
				}
			}
			
			result.match = (n_matches >= quant->min_matches) && (quant->max_matches < 0 || n_matches <= quant->max_matches);
			
			//log_print("n_matches for ", static_cast<Regex_Identifier_AST *>(quant->exprs[0])->ident.string_value, " was ", n_matches, " (", quant->min_matches, ", ", quant->max_matches, ") match: ", result.match, "\n");
			
		} break;
		
		case Math_Expr_Type::regex_or_chain : {
			for(auto expr : regex->exprs) {
				auto match = match_path_recursive(scope, nodes, path, path_idx, expr);
				if(match.match) {
					result = match;
					break;
				}
			}
		} break;
		
		case Math_Expr_Type::regex_identifier : {
			auto ident = static_cast<Regex_Identifier_AST *>(regex);
			if(ident->wildcard)
				result.match = true;
			else {
				std::string identifier = ident->ident.string_value;
				Entity_Id regex_id = invalid_entity_id;
				if(identifier != "out") {
					auto res = (*scope)[identifier];
					if(!res)
						fatal_error(Mobius_Error::internal, "Somehow we have a identifier in a regex that does not correspond to a component.");
					regex_id = res->id;
				}
				
				Entity_Id path_id = invalid_entity_id;
				int node_idx = -2;
				if(path_idx < path.size())
					node_idx = path[path_idx];
				if(node_idx >= 0)
					path_id = nodes[node_idx].id;
				
				result.match = (path_id == regex_id) && (node_idx >= -1);
			}
			if(result.match)
				result.path_idx = path_idx+1;
		} break;
		
		default : {
			fatal_error(Mobius_Error::internal, "Hit unknown regex type in match_regex().");
		} break;
	}
	
	return result;
}

void
build_graph_paths_recursive(int idx, std::vector<Connection_Node_Data> &nodes, std::vector<std::vector<int>> &paths, int path_idx, Source_Location error_loc) {
	paths[path_idx].push_back(idx);
	if(idx < 0) // Means we hit an 'out'
		return;
	auto &node = nodes[idx];
	if(node.visited) {
		error_loc.print_error_header();
		fatal_error("The graph data for this 'directed_graph' has a cycle, but that is specified to not be allowed in the model.\n");
	}
	if(node.points_at.empty()) {
		return;
	} else {
		node.visited = true;
		int edge = 0;
		for(int points_at : node.points_at) {
			int next_path_idx = path_idx;
			// If there is more than one outgoing edge we need a copy of the initial path for each edge (for the first edge we can just keep the one we are currently on).
			if(edge != 0) {
				next_path_idx = paths.size();
				paths.emplace_back(paths[path_idx]);
			}
			build_graph_paths_recursive(points_at, nodes, paths, path_idx, error_loc);
			++edge;
		}
		node.visited = false;
	}
}

void
error_print_node(Model_Application *app, Decl_Scope *scope, std::vector<Connection_Node_Data> &nodes, int nodeidx) {
	if(nodeidx < 0) {
		error_print("out");
		return;
	}
	
	auto node = nodes[nodeidx];
	error_print((*scope)[node.id], "[ ");   // Note: This is the identifier name used in the regex, not in the data set. May be confusing?
	for(auto &index : node.indexes.indexes) {
		bool quote;
		auto index_name = app->index_data.get_index_name(node.indexes, index, &quote);
		maybe_quote(index_name, quote);
		error_print(index_name, " ");
	}
	error_print(']');
}

void
match_regex(Model_Application *app, Entity_Id conn_id, Source_Location data_loc) {
	
	// TODO: Handle cycles!
		// Cycles:  Need to have the cycle itself match (something)*
		// Isolated cycles (no source entry) are a bit more challenging because they would have to match from any starting point...
			// Does that mean that they have to match something of the form (a|b|c...)*  ?
	
	// TODO: Also print what part of the regex caused the the final failure.
	
	auto model = app->model;
	auto connection = model->connections[conn_id];
	Math_Expr_AST *regex = connection->regex;
	
	if(!connection->no_cycles) {
		log_print("Note: Checking the connection regular expression is not yet supported for a directed_graph that can have cycles. It is thus skipped, and the user is responsible for checking that the graph is correct.\n");
		return;
	}
	
	Storage_Structure<Entity_Id> node_structure(app);
	make_connection_component_indexing_structure(app, &node_structure, conn_id);
	
	std::vector<Connection_Node_Data> nodes(node_structure.total_count);
	
	for(auto &comp : app->connection_components[conn_id].components) {
		auto id = comp.id;
		node_structure.for_each(id, [id, &nodes](Indexes &indexes, s64 offset) {
			nodes[offset].id = id;
			nodes[offset].indexes = indexes; // TODO: Could be a bit slow if we have hundreds of nodes..
		});
	}
	
	auto scope = model->get_scope(connection->scope_id);
	
	for(auto &arr : app->connection_components[conn_id].arrows) {
		s64 target_idx = -1;
		if(is_valid(arr.target_id)) {
			target_idx = node_structure.get_offset(arr.target_id, arr.target_indexes);
			++nodes[target_idx].receives_count;
		}
		
		s64 source_idx = node_structure.get_offset(arr.source_id, arr.source_indexes);
		auto &source = nodes[source_idx];
		if(std::find(source.points_at.begin(), source.points_at.end(), target_idx) != source.points_at.end()) {
			data_loc.print_error_header(Mobius_Error::model_building);
			error_print("The following connection arrow is duplicate within the same connection:\n");
			error_print_node(app, scope, nodes, source_idx);
			error_print(" -> ");
			error_print_node(app, scope, nodes, target_idx);
			mobius_error_exit();
		}
		source.points_at.push_back(target_idx);
		
		// NOTE: We can do this check like this because the flattened indexes are ordered the same way as the index tuples, but we should maybe make the check more robust.
		//   TODO: how does it work if it is between different components? Could we get unintended behaviour in the model solver if that is ordered wrong?
		if(connection->no_cycles && (target_idx != -1) && (source_idx >= target_idx) && (nodes[source_idx].id == nodes[target_idx].id)) {
			data_loc.print_error_header(Mobius_Error::model_building);
			error_print("The directed_graph connection \"", connection->name, "\" is marked as @no_cycles. Because of this, for technical reasons, we require every arrow in the graph to go from a lower index to a higher index. The following arrow violates this:\n");
			error_print_node(app, scope, nodes, source_idx);
			error_print(" -> ");
			error_print_node(app, scope, nodes, target_idx);
			error_print("\nSee the declaration of the connection here:\n");
			connection->source_loc.print_error();
			mobius_error_exit();
		}
	}
	
	std::vector<std::vector<int>> paths;
	
	int idx = 0;
	for(auto &node : nodes) {
		if(!is_valid(node.id))
			continue;

		if(node.receives_count == 0) {
			int path_idx = paths.size();
			paths.emplace_back();
			if(connection->no_cycles)
				build_graph_paths_recursive(idx, nodes, paths, path_idx, data_loc);
			else
				fatal_error(Mobius_Error::internal, "Connection path checking for cycles unimplemented.");
			
		}
		++idx;
	}
	/*
	log_print("**** ", connection->name, "\n");
	for(auto &path : paths) {
		log_print("Path: ");
		for(int nodeidx : path) {
			if(nodeidx < 0) log_print("out");
			else {
				auto &node = nodes[nodeidx];
				auto index = node.indexes.indexes[0];
				auto index_name = app->index_data.get_index_name(node.indexes, index);
				log_print(index_name);
			}
			log_print(", ");
		}
		log_print("\n");
	}
	*/
	
	for(auto &path : paths) {
		
		auto match = match_path_recursive(scope, nodes, path, 0, regex);
		
		if(!match.match || (match.path_idx < path.size()-1)) {
			data_loc.print_error_header(Mobius_Error::model_building);
			error_print("The regular expression for the connection \"", connection->name, "\" failed to match the provided graph. See the declaration of the regex here:\n");
			regex->source_loc.print_error();
			error_print("The matching succeeded up to the marked '(***)' in the following sequence:\n");
			bool found_mark = false;
			int pathidx = 0;
			for(int nodeidx : path) {
				if(pathidx == match.path_idx) {
					error_print("(***) ");
					found_mark = true;
				}
				error_print_node(app, scope, nodes, nodeidx);
				if(pathidx != path.size()-1)
					error_print(" ");
				
				++pathidx;
			}
			if(!found_mark)
				error_print(" (***)"); // This happens if the path ended before we had matched the entire regex.
			mobius_error_exit();
		}
	}
	
}