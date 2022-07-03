

#include "function_tree.h"
#include "model_declaration.h"
#include "emulate.h"

Math_Expr_FT *
make_cast(Math_Expr_FT *expr, Value_Type cast_to) {
	if(cast_to == expr->value_type) return expr;
	
	//TODO: if we cast a literal, we should just cast the value here and replace the literal.
	
	auto cast = new Math_Expr_FT();
	cast->location = expr->location;     // not sure if this is good, it is just so that it has a valid location.
	cast->value_type = cast_to;
	cast->unit = expr->unit;
	cast->exprs.push_back(expr);
	cast->expr_type = Math_Expr_Type::cast;
	
	return cast;
}

void try_cast(Math_Expr_FT **a, Math_Expr_FT **b) {
	if((*a)->value_type != Value_Type::real && (*b)->value_type == Value_Type::real)
		*a = make_cast(*a, Value_Type::real);
	else if((*a)->value_type == Value_Type::boolean && (*b)->value_type == Value_Type::integer)
		*a = make_cast(*a, Value_Type::integer);
}

void make_casts_for_binary_expr(Math_Expr_FT **left, Math_Expr_FT **right) {
	try_cast(left, right);
	try_cast(right, left);
}

void fixup_intrinsic(Function_Call_FT *fun, Token *name) {
	String_View n = name->string_value;
	if(n == "min" || n == "max") {
		fun->unit = fun->exprs[0]->unit;   //TODO: should also check that the units are equal.
		make_casts_for_binary_expr(&fun->exprs[0], &fun->exprs[1]);
		fun->value_type = fun->exprs[0]->value_type; //Note: value types of both arguments should be the same now.
	} else if (n == "exp") {
		fun->unit = fun->exprs[0]->unit;  //TODO: wrong! Should probably be dimensionless. But also wants argument to be dimensionless.
		fun->exprs[0] = make_cast(fun->exprs[0], Value_Type::real);
		fun->value_type = Value_Type::real;
	} else {
		fatal_error(Mobius_Error::internal, "Unhandled intrinsic \"", name, "\" in fixup_intrinsic().");
	}
}

void resolve_arguments(Mobius_Model *model, s32 module_id, Math_Expr_FT *ft, Math_Expr_AST *ast) {
	//TODO allow error check on expected number of arguments
	for(auto arg : ast->exprs) {
		ft->exprs.push_back(resolve_function_tree(model, module_id, arg));
	}
}

Math_Expr_FT *
resolve_function_tree(Mobius_Model *model, s32 module_id, Math_Expr_AST *ast){
	Math_Expr_FT *result;
	
#define DEBUGGING_NOW 0
#if DEBUGGING_NOW
	warning_print("begin ", name(ast->type), "\n");
#endif
	
	Module_Declaration *module = model->modules[module_id];
	switch(ast->type) {
		case Math_Expr_Type::block : {
			auto new_block = new Math_Block_FT();
			
			resolve_arguments(model, module_id, new_block, ast);
			
			// the value of a block is the value of the last expression in the block.
			//TODO: if there is only one expr in the block, we could replace the block with that expr.
			new_block->value_type = new_block->exprs.back()->value_type;
			new_block->unit       = new_block->exprs.back()->unit;
			result = new_block;
		} break;
		
		case Math_Expr_Type::identifier_chain : {
			auto ident = reinterpret_cast<Identifier_Chain_AST *>(ast);
			auto new_ident = new Identifier_FT();
			if(ident->chain.size() == 1) {
				//TODO: for now, just assume this is a parameter. Could eventually be a local variable.
				//TODO: could also allow refering to properties by just a single identifier if the compartment is obvious.
				new_ident->variable_type = Variable_Type::parameter;
				
				Entity_Id par_id = find_handle(module, ident->chain[0].string_value);
				
				if(!is_valid(par_id) || par_id.reg_type != Reg_Type::parameter) {
					ident->chain[0].print_error_header();
					fatal_error("Can not resolve the name \"", ident->chain[0].string_value, "\".");
				}
				
				new_ident->variable_type = Variable_Type::parameter;
				new_ident->parameter = par_id;
				new_ident->unit      = module->parameters[par_id]->unit;
				new_ident->value_type = get_value_type(module->parameters[par_id]->decl_type);
				// TODO: make it so that we can't reference datetime values (if we implement those)
				//  also special handling of for enum values!
			} else if(ident->chain.size() == 2) {
				//TODO: may need fixup for multiple modules.
				Entity_Id first  = find_handle(module, ident->chain[0].string_value);
				Entity_Id second = find_handle(module, ident->chain[1].string_value);
				//TODO: may need fixup for special identifiers.
				
				if(!is_valid(first) || !is_valid(second) || first.reg_type != Reg_Type::compartment || second.reg_type != Reg_Type::property_or_quantity) {
					ident->chain[0].print_error_header();
					fatal_error("Unable to resolve value.");  //TODO: make message more specific.
				}
				Value_Location loc = make_value_location(module, first, second);
				Var_Id var_id = model->state_vars[loc];
				if(is_valid(var_id)) {
					new_ident->variable_type = Variable_Type::state_var;
					new_ident->state_var     = var_id;
					new_ident->unit          = module->hases[model->state_vars[var_id]->entity_id]->unit;
					new_ident->value_type    = Value_Type::real;
				} else {
					var_id = model->series[loc];
					if(!is_valid(var_id)) {
						// register new input series
						State_Variable var = {}; //TODO: put data on it
						var_id = model->series.register_var(var, loc);
					}
					new_ident->variable_type = Variable_Type::series;
					new_ident->series = var_id;
					new_ident->unit = module->dimensionless_unit;  //TODO: instead inherit from the property.
					new_ident->value_type    = Value_Type::real;
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
			
			resolve_arguments(model, module_id, new_fun, ast);
			
			new_fun->fun_type = module->functions[fun_id]->fun_type;
			new_fun->fun_name = fun->name.string_value;
			
			if(new_fun->fun_type == Function_Type::intrinsic) {
				fixup_intrinsic(new_fun, &fun->name);
			} else {
				//TODO!
				fatal_error(Mobius_Error::internal, "Unhandled function type.");
			}
			
			result = new_fun;
			
		} break;
		
		//NOTE: currently we don't allow integers and reals to auto-cast to boolean
		
		case Math_Expr_Type::unary_operator : {
			auto unary = reinterpret_cast<Unary_Operator_AST *>(ast);
			auto new_unary = new Operator_FT();
			
			resolve_arguments(model, module_id, new_unary, ast);
			
			new_unary->oper = unary->oper;
			
			if((char)unary->oper == '-') {
				if(new_unary->exprs[0]->value_type == Value_Type::boolean) {
					new_unary->exprs[0]->location.print_error_header();
					fatal_error("Unary minus can not have an argument of type boolean.");
				}
				new_unary->unit = new_unary->exprs[0]->unit;
				new_unary->value_type = new_unary->exprs[0]->value_type;
			} else if((char)unary->oper == '!') {
				if(new_unary->exprs[0]->value_type != Value_Type::boolean) {
					new_unary->exprs[0]->location.print_error_header();
					fatal_error("Negation must have an argument of type boolean.");
				}
				
				new_unary->unit = module->dimensionless_unit;
				new_unary->value_type = Value_Type::boolean;
			} else
				fatal_error(Mobius_Error::internal, "Unhandled unary operator type in resolve_function_tree().");
			
			result = new_unary;
		} break;
		
		case Math_Expr_Type::binary_operator : {
			auto binary = reinterpret_cast<Binary_Operator_AST *>(ast);
			auto new_binary = new Operator_FT();
			
			resolve_arguments(model, module_id, new_binary, ast);
			
			new_binary->oper = binary->oper;
			char op = (char)binary->oper;
			
			if(op == '|' || op == '&') {
				if(new_binary->exprs[0]->value_type != Value_Type::boolean || new_binary->exprs[1]->value_type != Value_Type::boolean) {
					fatal_error("Operator ", name(binary->oper), " can only take boolean arguments.");
				}
				new_binary->value_type = Value_Type::boolean;
				new_binary->unit       = module->dimensionless_unit;
			} else if (op == '^') {
				//Note: we could implement pow for lhs of type int too, but llvm does not have an intrinsic for it, and there is unlikely to be any use case.
				new_binary->value_type = Value_Type::real;
				new_binary->unit       = module->dimensionless_unit; //TODO: could make new unit if rhs is an integer or exact fraction.
				new_binary->exprs[0]   = make_cast(new_binary->exprs[0], Value_Type::real);
				if(new_binary->exprs[1]->value_type == Value_Type::boolean) new_binary->exprs[1] = make_cast(new_binary->exprs[1], Value_Type::integer);
			} else {
				make_casts_for_binary_expr(&new_binary->exprs[0], &new_binary->exprs[1]);
				
				if(op == '+' || op == '-' || op == '*' || op == '/') {
					new_binary->value_type = new_binary->exprs[0]->value_type;
					new_binary->unit = new_binary->exprs[0]->unit;              //TODO: instead do unit arithmetic when that is implemented!
				} else {
					new_binary->value_type = Value_Type::boolean;
					new_binary->unit       = module->dimensionless_unit;
				}
			}
			
			result = new_binary;
		} break;
		
		case Math_Expr_Type::if_chain : {
			auto ifexpr = reinterpret_cast<If_Expr_AST *>(ast);
			auto new_if = new Math_Expr_FT();
			
			resolve_arguments(model, module_id, new_if, ast);
			
			// Cast all possible result values up to the same type
			Value_Type value_type = new_if->exprs[0]->value_type;
			for(int idx = 0; idx < (int)new_if->exprs.size()-1; idx+=2) {
				if(new_if->exprs[idx+1]->value_type != Value_Type::boolean) {
					new_if->exprs[idx+1]->location.print_error_header();
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
			
			result = new_if;
		} break;
	}
	
	result->location = ast->location;
	result->expr_type = ast->type;
	
	if(result->value_type == Value_Type::unresolved) {
		ast->location.print_error_header();
		fatal_error("(internal error) did not resolve value type of expression.");
	}
	
#if DEBUGGING_NOW
	warning_print("end ", name(ast->type), "\n");
#endif

	return result;
}

Math_Expr_FT *
prune_tree(Math_Expr_FT *expr) {
	
	for(auto &arg : expr->exprs)
		arg = prune_tree(arg);
	
	switch(expr->expr_type) {
		case Math_Expr_Type::block : {
			//todo: replace with last statement if only one statement
		} break;
		
		case Math_Expr_Type::function_call : {
			auto fun = reinterpret_cast<Function_Call_FT *>(expr);
			if(fun->fun_type != Function_Type::intrinsic)           //TODO: implement for others.
				return expr;
			
			bool all_literal = true;
			for(auto arg : expr->exprs) {
				if(arg->expr_type != Math_Expr_Type::literal) { all_literal = false; break; }
			}
			if(all_literal) {
				auto literal = new Literal_FT();
				literal->location = expr->location;
				literal->unit = expr->unit;
				literal->value_type = expr->value_type;
				
				if(expr->exprs.size() == 1) {
					auto arg1 = reinterpret_cast<Literal_FT *>(expr->exprs[0]);
					literal->value = apply_intrinsic({arg1->value, arg1->value_type}, fun->fun_name);
					delete expr;
					return literal;
				} else if(expr->exprs.size() == 2) {
					auto arg1 = reinterpret_cast<Literal_FT *>(expr->exprs[0]);
					auto arg2 = reinterpret_cast<Literal_FT *>(expr->exprs[1]);
					literal->value = apply_intrinsic({arg1->value, arg1->value_type}, {arg2->value, arg2->value_type}, fun->fun_name);
					delete expr;
					return literal;
				} else
					fatal_error(Mobius_Error::internal, "Unhandled number of arguments to intrinsic.");
			}
		} break;
		
		case Math_Expr_Type::unary_operator : {
			// If the argument is a literal, just apply the operator directly on the unary and replace the unary with the literal.
			if(expr->exprs[0]->expr_type == Math_Expr_Type::literal) {
				auto unary = reinterpret_cast<Operator_FT *>(expr);
				auto arg = reinterpret_cast<Literal_FT *>(expr->exprs[0]);
				Parameter_Value val = apply_unary({arg->value, arg->value_type}, unary->oper);
				auto literal = new Literal_FT();
				literal->value = val;
				literal->value_type = expr->value_type;
				literal->unit = expr->unit;
				literal->location = expr->location;
				delete expr;
				return literal;
			}
		} break;
		
		case Math_Expr_Type::binary_operator : {
			if(expr->exprs[0]->expr_type == Math_Expr_Type::literal && expr->exprs[1]->expr_type == Math_Expr_Type::literal) {
				auto lhs  = reinterpret_cast<Literal_FT *>(expr->exprs[0]);
				auto rhs = reinterpret_cast<Literal_FT *>(expr->exprs[1]);
				auto binary = reinterpret_cast<Operator_FT *>(expr);
				//NOTE: have to pass rhs type since that matters for pow.. not clean, instead Parameter_Value should carry its type, or we should pass both
				Parameter_Value val = apply_binary({lhs->value, lhs->value_type}, {rhs->value, rhs->value_type}, binary->oper);
				auto literal = new Literal_FT();
				literal->value = val;
				literal->value_type = expr->value_type;
				literal->unit = expr->unit;
				literal->location = expr->location;
				delete expr;
				return literal;
			}
		} break;
	
		case Math_Expr_Type::if_chain : {
			
			std::vector<Math_Expr_FT *> remains;
			bool found_true = false;
			for(int idx = 0; idx < (int)expr->exprs.size()-1; idx+=2) {
				bool is_false = false;
				bool is_true  = false;
				if(!found_true && expr->exprs[idx+1]->expr_type == Math_Expr_Type::literal) {
					auto literal = reinterpret_cast<Literal_FT *>(expr->exprs[idx+1]);
					if(literal->value.val_bool) is_true  = true;
					else                        is_false = true;
				}
				
				// If this clause is constantly false, or a previous one was true, delete the value.
				// If this clause was true, this value becomes the 'otherwise'. Only delete clause, not value.
				if(is_false || found_true)     
					delete expr->exprs[idx];
				else
					remains.push_back(expr->exprs[idx]);
				
				if(is_true || is_false || found_true)
					delete expr->exprs[idx+1];
				else
					remains.push_back(expr->exprs[idx+1]);
				
				if(is_true) found_true = true;
			}
			if(found_true)
				delete expr->exprs.back();
			else	
				remains.push_back(expr->exprs.back());
			
			if(remains.size() == 1) {
				expr->exprs.clear(); //To not invoke destructor on children.
				delete expr;
				return remains[0];
			}
			expr->exprs = remains;
			return expr;
			
		} break;
		
		case Math_Expr_Type::cast : {
			if(expr->exprs[0]->expr_type == Math_Expr_Type::literal) {
				auto old_literal = reinterpret_cast<Literal_FT*>(expr->exprs[0]);
				auto literal = new Literal_FT();
				literal->value = apply_cast({old_literal->value, old_literal->value_type}, expr->value_type);
				literal->value_type = expr->value_type;
				literal->unit = expr->unit;
				literal->location = expr->location;
				delete expr;
				return literal;
			}
		} break;
	}
	return expr;
}

void
register_dependencies(Math_Expr_FT *expr, State_Variable *var) {
	for(auto arg : expr->exprs) register_dependencies(arg, var);
	
	if(expr->expr_type == Math_Expr_Type::identifier_chain) {
		auto ident = reinterpret_cast<Identifier_FT *>(expr);
		if(ident->variable_type == Variable_Type::parameter)
			var->depends_on_parameter.insert(ident->parameter);
		else if(ident->variable_type == Variable_Type::state_var)
			var->depends_on_state_var.insert(ident->state_var);
		else if(ident->variable_type == Variable_Type::series)
			var->depends_on_series.insert(ident->series);
	}
}


Math_Expr_FT *
quantity_codegen(Mobius_Model *model, Entity_Id id) {
	auto module = model->modules[id.module_id];
	auto has = module->hases[id];
	
	auto location = has->value_location;
	//TODO: check that the location is a valid quantity location.
	
	std::vector<std::pair<Var_Id, char>> fluxes;
	//TODO: make proper id system.
	//TODO: make convenience lookup functions for some of the below!
	for(Var_Id var_id : model->state_vars) {
		State_Variable *state_var = model->state_vars[var_id];
		auto entity_id = state_var->entity_id;
		if(entity_id.reg_type == Reg_Type::flux) {
			auto flux = model->modules[entity_id.module_id]->fluxes[entity_id];
			if(flux->source == location)
				fluxes.push_back({var_id, '+'});
			if(flux->target == location)
				fluxes.push_back({var_id, '-'});
		}
	}
	
	Var_Id quant_id = model->state_vars[location];
	if(!is_valid(quant_id))
		fatal_error(Mobius_Error::internal, "Somehow got a non-existing location in quantity_codegen().");
	
	//TODO: The below can be compressed a lot with helper functions
	Identifier_FT *quant = new Identifier_FT();
	quant->variable_type = Variable_Type::state_var;
	quant->state_var = quant_id;
	quant->value_type = Value_Type::real;
	quant->unit = module->dimensionless_unit; //TODO! also, what do we do about unit checking for fluxes? Can't be done at function tree stage since then error messages will be strange.
	
	Math_Expr_FT *result = quant;
	//TODO: have to make fixes to function tree to get this to work.
	for(auto &pair : fluxes) {
		auto flx = new Identifier_FT();
		flx->variable_type = Variable_Type::state_var;
		flx->state_var = pair.first;
		flx->value_type = Value_Type::real;
		flx->unit = module->dimensionless_unit; //TODO
		
		auto sum = new Operator_FT();
		sum->value_type = Value_Type::real;
		sum->expr_type = Math_Expr_Type::binary_operator;
		sum->oper = (Token_Type)pair.second;
		sum->unit = module->dimensionless_unit; //TODO
		
		sum->exprs.push_back(result);
		sum->exprs.push_back(flx);
		result = sum;
	}
	
	return result;
}





