

#include "function_tree.h"
#include "model_declaration.h"
#include "emulate.h"

Math_Expr_FT *
make_cast(Math_Expr_FT *expr, Value_Type cast_to) {
	if(cast_to == expr->value_type) return expr;
	else if(expr->expr_type == Math_Expr_Type::literal) {
		auto literal = reinterpret_cast<Literal_FT *>(expr);
		literal->value = cast_value(literal->value, literal->value_type, cast_to);
		literal->value_type = cast_to;
		return literal;
	}
	//TODO: if we cast a literal, we should just cast the value here and replace the literal.
	
	auto cast = new Math_Expr_FT();
	cast->ast = expr->ast;     // not sure if this is good, it is just so that it has a valid location.
	cast->value_type = cast_to;
	cast->unit = expr->unit;
	cast->exprs.push_back(expr);
	cast->expr_type = Math_Expr_Type::cast;
	
	return cast;
}

void try_cast(Math_Expr_FT **a, Math_Expr_FT **b) {
	if((*a)->value_type == Value_Type::integer && (*b)->value_type == Value_Type::real)
		*a = make_cast(*a, Value_Type::real);
}

void make_casts_for_binary_expr(Math_Expr_FT **left, Math_Expr_FT **right, String_View name) {
	if((*left)->value_type == Value_Type::boolean) {
		(*left)->ast->location.print_error_header(); //TODO: should the error refer to the operator instead?
		fatal_error("Expression \"", name, "\" does not accept an argument of type boolean.");
	} else if ((*right)->value_type == Value_Type::boolean) {
		(*right)->ast->location.print_error_header(); //TODO: should the error refer to the operator instead?
		fatal_error("Expression \"", name, "\" does not accept an argument of type boolean.");
	}
	try_cast(left, right);
	try_cast(right, left);
}

void fixup_intrinsic(Function_Call_FT *fun, Token *name) {
	if(name->string_value == "min" || name->string_value == "max") {
		fun->unit = fun->exprs[0]->unit;   //TODO: should also check that the units are equal.
		make_casts_for_binary_expr(&fun->exprs[0], &fun->exprs[1], name->string_value);
		fun->value_type = fun->exprs[0]->value_type; //Note: value types of both arguments should be the same now.
	} else {
		fatal_error(Mobius_Error::internal, "Unhandled intrinsic \"", name, "\" in fixup_intrinsic().");
	}
}

void resolve_arguments(Mobius_Model *model, Math_Expr_FT *ft, Math_Expr_AST *ast, State_Variable *var) {
	//TODO allow error check on expected number of arguments
	for(auto arg : ast->exprs) {
		ft->exprs.push_back(resolve_function_tree(model, arg, var));
	}
}

Math_Expr_FT *
resolve_function_tree(Mobius_Model *model, Math_Expr_AST *ast, State_Variable *var){
	Math_Expr_FT *result;
	
	Module_Declaration *module = model->modules[var->entity_id.module_id];
	switch(ast->type) {
		case Math_Expr_Type::block : {
			auto new_block = new Math_Block_FT();
			
			resolve_arguments(model, new_block, ast, var);
			
			// the value of a block is the value of the last expression in the block.
			new_block->value_type = new_block->exprs.back()->value_type;
			new_block->unit       = new_block->exprs.back()->unit;
			result = new_block;
		} break;
		
		case Math_Expr_Type::identifier_chain : {
			auto ident = reinterpret_cast<Identifier_Chain_AST *>(ast);
			auto new_ident = new Identifier_FT();
			if(ident->chain.size() == 1) {
				//TODO: for now, just assume this is a parameter. Could eventually be a local variable.
				new_ident->variable_type = Variable_Type::parameter;
				
				Entity_Id par_id = find_handle(module, ident->chain[0].string_value);
				
				if(!is_valid(par_id) || par_id.reg_type != Reg_Type::parameter) {
					ident->chain[0].print_error_header();
					fatal_error("Can not resolve the name \"", ident->chain[0].string_value, "\".");
				}
				//TODO: could allow refering to properties by just a single identifier if the compartment is obvious.
				
				new_ident->variable_type = Variable_Type::parameter;
				new_ident->parameter = par_id;
				new_ident->unit      = module->parameters[par_id]->unit;
				new_ident->value_type = get_value_type(module->parameters[par_id]->decl_type);
				// TODO: make it so that we can't reference datetime values (if we implement those)
				//  also special handling of for enum values!
				
				var->depends_on_par_group.insert(module->parameters[par_id]->par_group);
			} else if(ident->chain.size() == 2) {
				//TODO: may need fixup for multiple modules.
				Entity_Id first  = find_handle(module, ident->chain[0].string_value);
				Entity_Id second = find_handle(module, ident->chain[1].string_value);
				//TODO: may need fixup for special identifiers.
				
				if(!is_valid(first) || !is_valid(second) || first.reg_type != Reg_Type::compartment || second.reg_type != Reg_Type::property_or_substance) {
					ident->chain[0].print_error_header();
					fatal_error("Unable to resolve value.");  //TODO: make message more specific.
				}
				state_var_id var_id = find_state_var(model, make_value_location(module, first, second));
				if(var_id < 0) {    // TODO: make proper id system...
					// unresolved, but valid. Assume this is an input series instead.
					warning_print("Input series unimplemented.\n"); //TODO!!!
					
					state_var_id input_id = 0; //TODO: instead register it
					new_ident->variable_type = Variable_Type::input_series;
					new_ident->series = input_id;
					new_ident->unit = module->dimensionless_unit;  //TODO: instead inherit from the property.
					new_ident->value_type    = Value_Type::real;
					
					var->depends_on_input_series.insert(input_id);
				} else {
					new_ident->variable_type = Variable_Type::state_var;
					new_ident->state_var     = var_id;
					new_ident->unit          = module->hases[model->state_variables[var_id].entity_id]->unit;
					new_ident->value_type    = Value_Type::real;
					
					var->depends_on_state_var.insert(var_id);
				}
			} else {
				ident->chain[0].print_error_header();
				fatal_error("Too many identifiers in chain.");
			}
			result = new_ident;
		} break;
		
		case Math_Expr_Type::literal : {
			auto literal = reinterpret_cast<Literal_AST *>(ast);
			auto new_literal = new Literal_FT();
			
			new_literal->value_type = get_value_type(literal->value.type);
			new_literal->unit       = module->dimensionless_unit;
			new_literal->value      = get_parameter_value(&literal->value);
			
			result = new_literal;
		} break;
		
		case Math_Expr_Type::function_call : {
			//TODO: allow accessing global functions when that is implemented.
			//TODO: should have different mechanisms for checking units
			//TODO: should have some kind of mechanism for checking types.
			
			//TODO: if all arguments are literals we should just do the evaluation here (if possible) and replace with the literal of the result.
			
			auto fun = reinterpret_cast<Function_Call_AST *>(ast);
			auto new_fun = new Function_Call_FT();
			
			Entity_Id fun_id = find_handle(module, fun->name.string_value);
			if(!is_valid(fun_id) || fun_id.reg_type != Reg_Type::function) {
				fun->name.print_error_header();
				fatal_error("The function \"", fun->name.string_value, "\" has not been declared.");
			}
			
			//TODO: should be replaced with check in resolve_arguments
			if(fun->exprs.size() != module->functions[fun_id]->args.size()) {
				fun->name.print_error_header();
				fatal_error("Wrong number of arguments to function. Expected ", module->functions[fun_id]->args.size(), ", got ", fun->exprs.size(), ".");
			}
			
			resolve_arguments(model, new_fun, ast, var);
			
			new_fun->fun_type = module->functions[fun_id]->fun_type;
			
			bool replaced = false;
			if(new_fun->fun_type == Function_Type::intrinsic) {
				fixup_intrinsic(new_fun, &fun->name);
				
				bool all_literal = true;
				for(auto expr : new_fun->exprs) {
					if(expr->expr_type != Math_Expr_Type::literal) { all_literal = false; break; }
				}
				if(all_literal) {
					auto literal = new Literal_FT();
					literal->ast = new_fun->ast;
					literal->unit = new_fun->unit;
					literal->value_type = new_fun->value_type;
					
					if(new_fun->exprs.size() == 2) {
						auto arg1 = reinterpret_cast<Literal_FT *>(new_fun->exprs[0]);
						auto arg2 = reinterpret_cast<Literal_FT *>(new_fun->exprs[1]);
						literal->value = apply_intrinsic(arg1->value, arg2->value, new_fun->value_type, fun->name.string_value);
						delete new_fun;
						result = literal;
						replaced = true;
					} else
						fatal_error(Mobius_Error::internal, "Unhandled number of arguments to intrinsic.");
				}
			} else {
				//TODO!
				fatal_error(Mobius_Error::internal, "Unhandled function type.");
			}
			
			if(!replaced)
				result = new_fun;
		} break;
		
		//NOTE: currently we don't allow integers and reals to auto-cast to boolean
		
		case Math_Expr_Type::unary_operator : {
			auto unary = reinterpret_cast<Unary_Operator_AST *>(ast);
			auto new_unary = new Math_Expr_FT();
			
			resolve_arguments(model, new_unary, ast, var);
			
			if(unary->oper == "-") {
				if(new_unary->exprs[0]->value_type == Value_Type::boolean) {
					new_unary->exprs[0]->ast->location.print_error_header();
					fatal_error("Unary minus can not have an argument of type boolean.");
				}
				new_unary->unit = new_unary->exprs[0]->unit;
				new_unary->value_type = new_unary->exprs[0]->value_type;
			} else if(unary->oper == "!") {
				if(new_unary->exprs[0]->value_type != Value_Type::boolean) {
					new_unary->exprs[0]->ast->location.print_error_header();
					fatal_error("Negation must have an argument of type boolean.");
				}
				
				new_unary->unit = module->dimensionless_unit;
				new_unary->value_type = Value_Type::boolean;
			} else
				fatal_error(Mobius_Error::internal, "Unhandled unary operator type in resolve_function_tree().");
			
			// If the argument is a literal, just apply the operator directly on the unary and replace the unary with the literal.
			if(new_unary->exprs[0]->expr_type == Math_Expr_Type::literal) {
				auto literal = reinterpret_cast<Literal_FT *>(new_unary->exprs[0]);
				Parameter_Value val = apply_unary(literal->value, literal->value_type, unary->oper);
				new_unary->exprs.clear(); //To not invoke destructors when we delete it;
				literal->value = val;
				literal->value_type = new_unary->value_type;
				literal->unit = new_unary->unit;
				delete new_unary;
				result = literal;
			} else
				result = new_unary;
		} break;
		
		case Math_Expr_Type::binary_operator : {
			//TODO: if both arguments are literals we should just do the evaluation here and replace with the literal of the result.
			auto binary = reinterpret_cast<Binary_Operator_AST *>(ast);
			auto new_binary = new Math_Expr_FT();
			
			resolve_arguments(model, new_binary, ast, var);
			
			if(binary->oper == "|" || binary->oper == "&") {
				if(new_binary->exprs[0]->value_type != Value_Type::boolean || new_binary->exprs[1]->value_type != Value_Type::boolean) {
					fatal_error("Operator ", binary->oper, " can only take boolean arguments.");
				}
				new_binary->value_type = Value_Type::boolean;
				new_binary->unit       = module->dimensionless_unit;
			} else {
				make_casts_for_binary_expr(&new_binary->exprs[0], &new_binary->exprs[1], binary->oper);
				
				if(binary->oper == "+" || binary->oper == "-" || binary->oper == "*" || binary->oper == "/") {
					new_binary->value_type = new_binary->exprs[0]->value_type;
					new_binary->unit = new_binary->exprs[0]->unit;              //TODO: instead do unit arithmetic when that is implemented!
				} else {
					new_binary->value_type = Value_Type::boolean;
					new_binary->unit       = module->dimensionless_unit;
				}
			}
			
			if(new_binary->exprs[0]->expr_type == Math_Expr_Type::literal && new_binary->exprs[1]->expr_type == Math_Expr_Type::literal) {
				auto left = reinterpret_cast<Literal_FT *>(new_binary->exprs[0]);
				auto right = reinterpret_cast<Literal_FT *>(new_binary->exprs[1]);
				//NOTE: after the above operations, the value type of left and right should be the same..
				Parameter_Value val = apply_binary(left->value, right->value, left->value_type, binary->oper);
				new_binary->exprs.clear(); //To not invoke destructors of subexprs when we delete it;
				left->value = val;
				left->value_type = new_binary->value_type;
				left->unit = new_binary->unit;
				delete new_binary;
				delete right;
				result = left;
			} else
				result = new_binary;
		} break;
		
		case Math_Expr_Type::if_chain : {
			auto ifexpr = reinterpret_cast<If_Expr_AST *>(ast);
			auto new_if = new Math_Expr_FT();
			
			resolve_arguments(model, new_if, ast, var);
			
			// Cast all possible result values up to the same type
			Value_Type value_type = new_if->exprs[0]->value_type;
			for(int idx = 0; idx < (int)new_if->exprs.size()-1; idx+=2) {
				if(new_if->exprs[idx+1]->value_type != Value_Type::boolean) {
					new_if->exprs[idx+1]->ast->location.print_error_header();
					fatal_error("Value of condition in if expression must be of type boolean.");
				}
				if(new_if->exprs[idx]->value_type == Value_Type::real) value_type = Value_Type::real;
				else if(new_if->exprs[idx]->value_type == Value_Type::integer && value_type == Value_Type::boolean) value_type = Value_Type::boolean;
			}
			int otherwise_idx = (int)new_if->exprs.size()-1;
			if(new_if->exprs[otherwise_idx]->value_type == Value_Type::real) value_type = Value_Type::real;
			else if(new_if->exprs[otherwise_idx]->value_type == Value_Type::integer && value_type == Value_Type::boolean) value_type = Value_Type::boolean;
			
			for(int idx = 0; idx < (int)new_if->exprs.size()-1; idx+=2)
				new_if->exprs[idx] = make_cast(new_if->exprs[idx], value_type);
			new_if->exprs[otherwise_idx] = make_cast(new_if->exprs[otherwise_idx], value_type);
			
			new_if->value_type = value_type;
			new_if->unit       = new_if->exprs[0]->unit; //TODO: We actually need to check that it is the same for all cases.
			
			//resolve directly if some of the conditions are literals
			std::vector<Math_Expr_FT *> remains;
			int true_idx = -1;
			for(int idx = 0; idx < (int)new_if->exprs.size()-1; idx+=2) {
				bool is_false = false;
				if(new_if->exprs[idx]->expr_type == Math_Expr_Type::literal) {
					auto literal = reinterpret_cast<Literal_FT *>(new_if->exprs[idx]);
					if(literal->value.val_bool == false) {
						is_false = true;
						delete literal;
						new_if->exprs[idx] = nullptr;
						delete new_if->exprs[idx+1]; //Also delete the condition
						new_if->exprs[idx+1] = nullptr;
					} else {
						true_idx = idx; break;
					}
				}
				if(!is_false) {
					remains.push_back(new_if->exprs[idx]);
					remains.push_back(new_if->exprs[idx]+1);
				}
			}
			remains.push_back(new_if->exprs[otherwise_idx]);
			
			bool replaced = false;
			if(true_idx > 0) {
				for(int idx = 0; idx < new_if->exprs.size(); ++idx) if(idx != true_idx && new_if->exprs[idx]) delete new_if->exprs[idx];
				result = new_if->exprs[true_idx];
				replaced = true;
				new_if->exprs.clear(); //to not invoke destructor on the new result
				delete new_if;
			} else {
				if(remains.size() == 1) { // Only the otherwise value remained, so we can replace the entire expression with that.
					result = new_if->exprs[0];
					new_if->exprs.clear(); // to not invoke destructor on child element.
					replaced = true;
					delete new_if;
				} else
					new_if->exprs = remains;
			}
			
			if(!replaced)
				result = new_if;
		} break;
	}
	
	result->ast = ast;
	result->expr_type = ast->type;
	
	if(result->value_type == Value_Type::unresolved) {
		ast->location.print_error_header();
		fatal_error("(internal error) did not resolve value type of expression.");
	}

	return result;
}

