
#include "model_application.h"

// TODO: have to make Regex_Body_FT * etc. And the function to resolve the AST.

bool
match_recursive(int &arr_idx, std::vector<int> &points_at, std::vector<Entity_Id> &ids, Regex_Body_FT *regex) {
	switch(regex->expr_type) {
		
		case Math_Expr_Type::block : {
			int reset_arr = arr_idx;
			for(int expr_idx = 0; expr_idx < regex->exprs.size(); ++expr_idx) {
				auto sub_regex = regex->exprs[expr_idx];
				bool success = match_recursive(arr_idx, points_at, ids, sub_regex);
				if(success) {
					if(arr_idx < 0) return (expr_idx == (regex->exprs.size()-1)); // if we ran out of things to match, we match the list only if we reached the end of the list
				} else {
					arr_idx = reset_arr; // Needed if this is a sub-expression of an outer one. In that case, the outer one could still succeed with a different match on the same item.
					return false;
				}
			}
			return true; // If we got here, it means that we matched all the expressions in the list, so we matched the list.
		} break;
		
		case Math_Expr_Type::unary_operator : {
			auto unary = reinterpret_cast<Operator_FT *>(regex);
			int match_count = 0;
			int minimum = unary->oper == '+' ? 1 : 0;
			while(true) {
				bool success = match_recursive(arr_idx, points_at, ids, unary->exprs[0]);
				if(!success) break;
				++match_count;
				if(arr_idx < 0) break;
				if(unary->oper == '?') break;
			}
			return match_count >= minimum;
		} break;
		
		case Math_Expr_Type::regex_or_chain : {
			bool success = false;
			for(auto sub_regex : regex->exprs) {
				if(match_recursive(arr_idx, points_at, ids, sub_regex)) {
					success = true;
					break;
				}
			}
			return success;
		} break;
		
		case Math_Expr_Type::regex_identifier : {
			auto ident = reinterpret_cast<Regex_Identifer_FT *>(regex);
			if(ident->id == ids[arr_idx]) {
				arr_idx = points_at[arr_idx];   // Advance the arrow in the inner match.
				return true;
			}
		} break;
	}
	fatal_error(Mobius_Error::internal, "Unhandled regex case in match_recursive()");
	
	return false;
}

void
match_regex(Model_Application *app, Data_Set *data_set, Connection_Info *conn, Entity_Id conn_id) {
	
	// TODO: check on conn->type
	
	// TODO: This doesn't quite work, since not all nodes are necessarily represented by a source arrow.
	// Also we should maybe match free-standing nodes against the regex too. (e.g. if the regex is (i j), then an i without an arrow going out should not match.
	// TODO: support things like i{n}, where n is an exact number of repetitions. i{n m} : between n and m repetitions.
	
	auto model = app->model;
	auto connection = model->connections[conn_id];
	Regex_Body_AST *regex = connection->regex;
	
	std::vector<int> points_at(conn->arrows.size(), -1);
	std::vector<int> receives_count(conn->arrows.size(), 0);
	std::vector<Entity_Id> ids(conn->arrows.size());
	
	for(int arr_idx = 0; arr_idx < conn->arrows.size(); ++arr_idx) {
		auto &arr = conn->arrows[arr_idx];
		ids[arr_idx] = model->components.find_by_name(data_set->components[arr.first]->name);
		
		for(int arr_idx2 = 0; arr_idx2 < conn->arrows.size(); ++arr_idx2) {
			auto &arr2 = conn->arrows[arr_idx2];
			if(arr.second == arr2.first) {
				points_at[arr_idx] = arr_idx2;
				receives_count[arr_idx2]++;
			}
			if(arr.first == arr2.first) {
				//TODO: store Source_Location on the arrow.
				conn->loc.print_error_header();
				fatal_error("The graph is not a singly directed tree (one of the nodes has more than one outgoing vertex.)");
			}
		}
	}
	
	for(int arr_idx = 0; arr_idx < conn->arrows.size(); ++arr_idx) {
		if(receives_count[arr_idx] == 0) {
			int idx = arr_idx; // NOTE: it will be mutated by the recursive call, so we need to copy it first.
			bool success = match_recursive(idx, points_at, ids, regex);
			//TODO error:
		}
	}
	
}