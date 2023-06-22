
#include "model_application.h"


struct
Node_Data {
	Entity_Id id = invalid_entity_id;
	int receives_count = 0;
	bool visited       = false;
	std::vector<int> points_at;
};

struct
Match_State {
	bool match;
	int path_idx;  // This is where we had to consume the path to to get the match.
};

Match_State
match_path_recursive(Decl_Scope *scope, std::vector<Node_Data> &nodes, std::vector<int> &path, int path_idx, Math_Expr_AST *regex) {
	
	Match_State result = {false, path_idx};
	if(path_idx >= path.size()) {
		//log_print("* ", path_idx, "\n");
		return result;
	}
	
	switch(regex->type) {
		case Math_Expr_Type::block : {
			int use_path_idx = path_idx;
			bool mismatch = false;
			for(auto expr : regex->exprs) {
				auto match = match_path_recursive(scope, nodes, path, use_path_idx, expr);
				if(!match.match) {
					mismatch = true;
					break;
				} else
					use_path_idx = match.path_idx;
			}
			if(!mismatch) {
				result.match = true;
				result.path_idx = use_path_idx;
			}
		} break;
		
		case Math_Expr_Type::unary_operator : {
			auto unary = static_cast<Unary_Operator_AST *>(regex);
			int min_matches = 0; int max_matches = -1;
			char oper = (char)unary->oper;
			if(oper == '?') {
				max_matches = 1;
			} else if (oper == '+')
				min_matches = 1;
			int n_matches = 0;
			
			int use_path_idx = path_idx;
			while(true) {
				auto match = match_path_recursive(scope, nodes, path, use_path_idx, regex->exprs[0]);
				if(match.match) {
					use_path_idx = match.path_idx;
					++n_matches;
				} else break;
			}
			
			result.match = (n_matches >= min_matches) && (max_matches < 0 || n_matches <= max_matches);
			if(result.match)
				result.path_idx = use_path_idx;
			
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
			std::string handle = ident->ident.string_value;
			Entity_Id regex_id = invalid_entity_id;
			if(handle != "out") {
				auto res = (*scope)[handle];
				if(!res)
					fatal_error(Mobius_Error::internal, "Somehow we have a handle in a regex that does not correspond to a component.");
				regex_id = res->id;
			}
			
			Entity_Id path_id = invalid_entity_id;
			auto node_idx = path[path_idx];
			if(node_idx >= 0)
				path_id = nodes[node_idx].id;
			
			result.match = (path_id == regex_id);
			result.path_idx = path_idx+1;
		} break;
		
		default : {
			fatal_error(Mobius_Error::internal, "Hit unknown regex type in match_regex().");
		} break;
	}
	
	return result;
}




void
build_tree_paths_recursive(int idx, std::vector<Node_Data> &nodes, std::vector<int> &current, Source_Location error_loc) {
	current.push_back(idx);
	if(idx < 0) // Means we hit an 'out'
		return;
	auto &node = nodes[idx];
	if(node.visited) {
		error_loc.print_error_header();
		fatal_error("The graph data for this directed_tree has a cycle.\n");
	}
	if(node.points_at.empty()) {
		return;
	} else {
		node.visited = true;
		build_tree_paths_recursive(node.points_at[0], nodes, current, error_loc);
		node.visited = false;
	}
}

void
match_regex(Model_Application *app, Entity_Id conn_id, Source_Location data_loc) {
	

	// TODO: support things like i{n}, where n is an exact number of repetitions. i{n m} : between n and m repetitions.
	
	// TODO: Need to also be able to check isolated nodes!
	
	// TODO: Check that directed_tree is a directed tree (?)
	
	// TODO: Handle cycles!
	
	auto model = app->model;
	auto connection = model->connections[conn_id];
	Math_Expr_AST *regex = connection->regex;
	
	if(connection->type == Connection_Type::directed_graph) {
		log_print("Note: Checking the connection regular expression is not yet supported for general directed_graph. It is thus skipped, and you have to verify yourself that the graph data is correct.\n");
		return;
	}
	
	Storage_Structure<Entity_Id> node_structure(app);
	make_connection_component_indexing_structure(app, &node_structure, conn_id);
	
	std::vector<Node_Data> nodes(node_structure.total_count, Node_Data {});
	
	for(auto &pair : connection->components) {
		auto id = pair.first;
		node_structure.for_each(id, [id, &nodes](std::vector<Index_T> &indexes, s64 offset) {
			nodes[offset].id = id;
		});
	}
	
	for(auto &arr : app->connection_components[conn_id].arrows) {
		s64 target_idx = -1;
		if(is_valid(arr.target_id)) {
			target_idx = node_structure.get_offset_alternate(arr.target_id, arr.target_indexes);
			++nodes[target_idx].receives_count;
			//nodes[target_idx].id = arr.target_id;
		}
		
		// TODO: Check for duplicate arrows? (Are they allowed?)
		s64 source_idx = node_structure.get_offset_alternate(arr.source_id, arr.source_indexes);
		nodes[source_idx].points_at.push_back(target_idx);
		//nodes[source_idx].id = arr.source_id;
		
		//log_print("Found arrow ", source_idx, " to ", target_idx, "\n");
	}
	
	auto scope = model->get_scope(connection->scope_id);
	
	std::vector<std::vector<int>> paths;
	
	int idx = 0;
	for(auto &node : nodes) {
		if(!is_valid(node.id))
			continue;
		
		if(connection->type == Connection_Type::directed_tree && node.points_at.size() > 1) {
			data_loc.print_error_header();
			fatal_error("The graph for the directed tree has a node that has more than one outgoing edge.");
		}
		
		if(node.receives_count == 0) {
			std::vector<int> path = {};
			build_tree_paths_recursive(idx, nodes, path, data_loc);
			paths.push_back(std::move(path));
		}
		++idx;
	}
	
	for(auto &path : paths) {
		/*
		for(int nodeidx : path) {
			if(nodeidx < 0)
				log_print("out ");
			else
				log_print(app->model->components[nodes[nodeidx].id]->name, " ");
		}
		log_print("\n");*/
		auto match = match_path_recursive(scope, nodes, path, 0, regex);
		//log_print("Match: ", match.match, " patlen ", path.size(), " path_idx ", match.path_idx, "\n");
		
		if(!match.match || match.path_idx != path.size()) {
			data_loc.print_error_header();
			error_print("The regular expression for this connection failed to match the provided graph. See the declaration of the regex here:\n");
			regex->source_loc.print_error();
			mobius_error_exit();
		}
	}
	
}