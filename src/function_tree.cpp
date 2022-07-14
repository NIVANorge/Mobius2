

#include "function_tree.h"
#include "model_declaration.h"
#include "emulate.h"
#include "units.h"

void
Math_Block_FT::set_id() {
	//TODO: if we ever want to paralellize code generation, we have to make a better system here:
	static s32 id_counter = 0;
	unique_block_id = id_counter++;
}

Math_Expr_FT *
make_cast(Math_Expr_FT *expr, Value_Type cast_to) {
	if(cast_to == expr->value_type) return expr;
	
	auto cast = new Math_Expr_FT(expr->scope, Math_Expr_Type::cast);
	cast->location = expr->location;     // not sure if this is good, it is just so that it has a valid location.
	cast->value_type = cast_to;
	cast->add_expr(expr);
	
	return cast;
}

Math_Expr_FT *
make_literal(Math_Block_FT *scope, s64 val_int) {
	auto literal = new Literal_FT(scope);
	literal->value_type = Value_Type::integer;
	literal->location = {}; //Hmm, we should actually make it possible to provide more location info on these generated nodes.
	literal->value.val_int = val_int;
	return literal;
}

Math_Expr_FT *
make_state_var_identifier(Math_Block_FT *scope, Var_Id state_var) {
	auto ident = new Identifier_FT(scope);
	ident->value_type    = Value_Type::real;
	ident->variable_type = Variable_Type::state_var;
	ident->state_var     = state_var;
	return ident;
}

Math_Expr_FT *
make_intrinsic_function_call(Math_Block_FT *scope, Value_Type value_type, String_View name, Math_Expr_FT *arg1, Math_Expr_FT *arg2) {
	auto fun = new Function_Call_FT(scope);
	fun->value_type = value_type;
	fun->fun_name   = name;
	fun->fun_type   = Function_Type::intrinsic;
	fun->exprs.push_back(arg1);
	fun->exprs.push_back(arg2);
	return fun;
}

Math_Expr_FT *
make_binop(Math_Block_FT *scope, char oper, Math_Expr_FT *lhs, Math_Expr_FT *rhs, Value_Type value_type) {
	auto binop = new Operator_FT(scope, Math_Expr_Type::binary_operator);
	binop->value_type = value_type;
	binop->oper = (Token_Type)oper;
	binop->exprs.push_back(lhs);
	binop->exprs.push_back(rhs);
	return binop;
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
		make_casts_for_binary_expr(&fun->exprs[0], &fun->exprs[1]);
		fun->value_type = fun->exprs[0]->value_type; //Note: value types of both arguments should be the same now.
	} else if (n == "exp") {
		fun->exprs[0] = make_cast(fun->exprs[0], Value_Type::real);
		fun->value_type = Value_Type::real;
	} else {
		fatal_error(Mobius_Error::internal, "Unhandled intrinsic \"", name, "\" in fixup_intrinsic().");
	}
}

void resolve_arguments(Mobius_Model *model, s32 module_id, Math_Expr_FT *ft, Math_Expr_AST *ast) {
	//TODO allow error check on expected number of arguments
	
	Math_Block_FT *scope = ft->scope;
	if(ft->expr_type == Math_Expr_Type::block)
		scope = reinterpret_cast<Math_Block_FT *>(ft);
	
	for(auto arg : ast->exprs) {
		ft->add_expr(resolve_function_tree(model, module_id, arg, scope));
	}
}

bool
find_local_variable(Identifier_FT *ident, String_View name, Math_Block_FT *scope) {
	if(!scope) return false;
	//warning_print("Try to resolve ", name, ".\n");
	int idx = 0;
	for(auto expr : scope->exprs) {
		if(expr->expr_type == Math_Expr_Type::local_var) {
			auto local = reinterpret_cast<Local_Var_FT *>(expr);
			//warning_print("Check ", name, " against ", local->name, ".\n");
			if(local->name == name) {
				ident->variable_type = Variable_Type::local;
				ident->local_var.index = idx;
				ident->local_var.scope_id = scope->unique_block_id;
				ident->value_type = local->value_type;
				local->is_used = true;
				return true;
			}
		}
		++idx;
	}
	if(!scope->function_name)  //NOTE: scopes should not "bleed through" function substitutions.
		return find_local_variable(ident, name, scope->scope);
	return false;
}

bool
is_inside_function(Math_Block_FT *scope, String_View name) {
	Math_Block_FT *sc = scope;
	while(true) {
		sc = sc->scope;
		if(!sc) return false;
		if(sc->function_name == name) return true;
	}
	return false;
}

Math_Expr_FT *
resolve_function_tree(Mobius_Model *model, s32 module_id, Math_Expr_AST *ast, Math_Block_FT *scope){
	Math_Expr_FT *result = nullptr;
	
#define DEBUGGING_NOW 0
#if DEBUGGING_NOW
	warning_print("begin ", name(ast->type), "\n");
#endif
	
	Module_Declaration *module = model->modules[module_id];
	switch(ast->type) {
		case Math_Expr_Type::block : {
			auto new_block = new Math_Block_FT(scope);
			
			resolve_arguments(model, module_id, new_block, ast);
			
			for(auto expr : new_block->exprs)
				if(expr->expr_type == Math_Expr_Type::local_var) ++new_block->n_locals;
			
			Math_Expr_FT *last = new_block->exprs.back();
			if(last->expr_type == Math_Expr_Type::local_var) {
				last->location.print_error_header();
				fatal_error("The last statement in a block has to evaluate to a value, it can't be a declaration of a local variable.");
			}
			// the value of a block is the value of the last expression in the block.
			new_block->value_type = last->value_type;
			result = new_block;
		} break;
		
		case Math_Expr_Type::identifier_chain : {
			auto ident = reinterpret_cast<Identifier_Chain_AST *>(ast);
			auto new_ident = new Identifier_FT(scope);
			if(ident->chain.size() == 1) {
				String_View name = ident->chain[0].string_value;
				
				bool found = find_local_variable(new_ident, name, scope);
				if(!found) {
					//TODO: for now, just assume this is a parameter.
					//TODO: could also allow refering to properties by just a single identifier if the compartment is obvious.
					new_ident->variable_type = Variable_Type::parameter;
					
					Entity_Id par_id = module->find_handle(name);
					
					if(!is_valid(par_id) || par_id.reg_type != Reg_Type::parameter) {
						ident->chain[0].print_error_header();
						fatal_error("Can't resolve the name \"", name, "\".");
					}
					
					new_ident->variable_type = Variable_Type::parameter;
					new_ident->parameter = par_id;
					new_ident->value_type = get_value_type(module->parameters[par_id]->decl_type);
					// TODO: make it so that we can't reference datetime values (if we implement those)
					//  also special handling of for enum values!
				}
			} else if(ident->chain.size() == 2) {
				//TODO: may need fixup for multiple modules.
				Entity_Id first  = module->find_handle(ident->chain[0].string_value);
				Entity_Id second = module->find_handle(ident->chain[1].string_value);
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
					new_ident->value_type    = Value_Type::real;
				} else {
					var_id = model->series[loc];
					if(!is_valid(var_id)) {
						//TODO: this check is actually not sufficient if we want to require that the has declaration should have happened in the same module.
						// Maybe that is not that important (?).
						ast->location.print_error_header();
						fatal_error("There has not been a \"has\" declaration registering this location.");
					}
					new_ident->variable_type = Variable_Type::series;
					new_ident->series = var_id;
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
			auto new_literal = new Literal_FT(scope);
			
			new_literal->value_type = get_value_type(literal->value.type);
			new_literal->value      = get_parameter_value(&literal->value, literal->value.type);
			
			result = new_literal;
		} break;
		
		case Math_Expr_Type::function_call : {
			//TODO: allow accessing global functions when that is implemented.
			//TODO: should have some kind of mechanism for checking types.
			
			//TODO: if all arguments are literals we should just do the evaluation here (if possible) and replace with the literal of the result.
			
			auto fun = reinterpret_cast<Function_Call_AST *>(ast);
			
			// First check for "special" calls that are not really function calls.
			if(fun->name.string_value == "last") {
				auto new_fun = new Function_Call_FT(scope); // Hmm it is a bit annoying to have to do this only to delete it again.
				resolve_arguments(model, module_id, new_fun, ast);
				if(new_fun->exprs.size() != 1) {
					fun->name.print_error_header();
					fatal_error("A last() call only takes one argument.");
				}
				if(new_fun->exprs[0]->expr_type != Math_Expr_Type::identifier_chain) {
					new_fun->exprs[0]->location.print_error_header();
					fatal_error("A last() call only takes a state variable identifier as argument.");
				}
				auto var = reinterpret_cast<Identifier_FT *>(new_fun->exprs[0]);
				new_fun->exprs.clear();
				delete new_fun;
				if(var->variable_type != Variable_Type::state_var && var->variable_type != Variable_Type::series) {
					var->location.print_error_header();
					fatal_error("A last() call can only be applied to a state variable or input series.");
				}
				var->flags = (Identifier_Flags)(var->flags | ident_flags_last_result);
				
				result = var;
			} else {
				// Otherwise it should have been registered as an entity.
			
				Entity_Id fun_id = module->find_handle(fun->name.string_value);
				if(!is_valid(fun_id)) fun_id = module->global_scope->find_handle(fun->name.string_value);
				
				if(!is_valid(fun_id)) {
					fun->name.print_error_header();
					fatal_error("The function \"", fun->name.string_value, "\" has not been registered.");
				}
				
				if(is_valid(fun_id) && fun_id.reg_type != Reg_Type::function) {
					fun->name.print_error_header();
					fatal_error("The handle \"", fun->name.string_value, "\" is not a function.");
				}
				
				auto fun_decl = model->find_entity<Reg_Type::function>(fun_id);
				
				//TODO: should be replaced with check in resolve_arguments
				if(fun->exprs.size() != fun_decl->args.size()) {
					fun->name.print_error_header();
					fatal_error("Wrong number of arguments to function. Expected ", module->functions[fun_id]->args.size(), ", got ", fun->exprs.size(), ".");
				}
				
				auto fun_type = fun_decl->fun_type;
				
				if(fun_type == Function_Type::intrinsic) {
					auto new_fun = new Function_Call_FT(scope);
					
					resolve_arguments(model, module_id, new_fun, ast);
				
					new_fun->fun_type = fun_type;
					new_fun->fun_name = fun->name.string_value;
					fixup_intrinsic(new_fun, &fun->name);
					
					result = new_fun;
				} else if(fun_type == Function_Type::decl) {
					if(is_inside_function(scope, fun->name.string_value)) {
						fun->name.print_error_header();
						//TODO: We should print the actual stack trace. That also goes for several other error locations!
						fatal_error("The function ", fun->name.string_value, " calls itself either directly or indirectly. This is not allowed.");
					}
					// Inline in the function call as a new block with the arguments as local vars.
					auto inlined_fun = new Math_Block_FT(scope);
					
					resolve_arguments(model, module_id, inlined_fun, ast);
					
					inlined_fun->function_name = fun->name.string_value;
					inlined_fun->n_locals = inlined_fun->exprs.size();
					for(int argidx = 0; argidx < inlined_fun->exprs.size(); ++argidx) {
						auto arg = inlined_fun->exprs[argidx];
						auto inlined_arg = new Local_Var_FT(inlined_fun);
						inlined_arg->add_expr(arg);
						inlined_arg->name = fun_decl->args[argidx];
						inlined_arg->value_type = arg->value_type;
						inlined_fun->exprs[argidx] = inlined_arg;
					}
					
					inlined_fun->add_expr(resolve_function_tree(model, module_id, fun_decl->code, inlined_fun));
					inlined_fun->value_type = inlined_fun->exprs.back()->value_type; // The value type is whatever the body of the function resolves to given these arguments.
					
					result = inlined_fun;
				} else {
					fatal_error(Mobius_Error::internal, "Unhandled function type.");
				}
			}
		} break;
		
		//NOTE: currently we don't allow integers and reals to auto-cast to boolean
		
		case Math_Expr_Type::unary_operator : {
			auto unary = reinterpret_cast<Unary_Operator_AST *>(ast);
			auto new_unary = new Operator_FT(scope, Math_Expr_Type::unary_operator);
			
			resolve_arguments(model, module_id, new_unary, ast);
			
			new_unary->oper = unary->oper;
			
			if((char)unary->oper == '-') {
				if(new_unary->exprs[0]->value_type == Value_Type::boolean) {
					new_unary->exprs[0]->location.print_error_header();
					fatal_error("Unary minus can not have an argument of type boolean.");
				}
				new_unary->value_type = new_unary->exprs[0]->value_type;
			} else if((char)unary->oper == '!') {
				if(new_unary->exprs[0]->value_type != Value_Type::boolean) {
					new_unary->exprs[0]->location.print_error_header();
					fatal_error("Negation must have an argument of type boolean.");
				}
				new_unary->value_type = Value_Type::boolean;
			} else
				fatal_error(Mobius_Error::internal, "Unhandled unary operator type in resolve_function_tree().");
			
			result = new_unary;
		} break;
		
		case Math_Expr_Type::binary_operator : {
			auto binary = reinterpret_cast<Binary_Operator_AST *>(ast);
			auto new_binary = new Operator_FT(scope, Math_Expr_Type::binary_operator);
			
			resolve_arguments(model, module_id, new_binary, ast);
			
			new_binary->oper = binary->oper;
			char op = (char)binary->oper;
			
			if(op == '|' || op == '&') {
				if(new_binary->exprs[0]->value_type != Value_Type::boolean || new_binary->exprs[1]->value_type != Value_Type::boolean) {
					fatal_error("Operator ", name(binary->oper), " can only take boolean arguments.");
				}
				new_binary->value_type = Value_Type::boolean;
			} else if (op == '^') {
				//Note: we could implement pow for lhs of type int too, but llvm does not have an intrinsic for it, and there is unlikely to be any use case.
				new_binary->value_type = Value_Type::real;
				new_binary->exprs[0]   = make_cast(new_binary->exprs[0], Value_Type::real);
				if(new_binary->exprs[1]->value_type == Value_Type::boolean) new_binary->exprs[1] = make_cast(new_binary->exprs[1], Value_Type::integer);
			} else {
				make_casts_for_binary_expr(&new_binary->exprs[0], &new_binary->exprs[1]);
				
				if(op == '+' || op == '-' || op == '*' || op == '/')
					new_binary->value_type = new_binary->exprs[0]->value_type;
				else
					new_binary->value_type = Value_Type::boolean;
			}
			
			result = new_binary;
		} break;
		
		case Math_Expr_Type::if_chain : {
			auto ifexpr = reinterpret_cast<If_Expr_AST *>(ast);
			auto new_if = new Math_Expr_FT(scope, Math_Expr_Type::if_chain);
			
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
			
			result = new_if;
		} break;
		
		case Math_Expr_Type::local_var : {
			
			auto local = reinterpret_cast<Local_Var_AST *>(ast);
			
			for(auto loc : scope->exprs) {
				if(loc->expr_type == Math_Expr_Type::local_var) {
					auto loc2 = reinterpret_cast<Local_Var_FT *>(loc);
					if(loc2->name == local->name.string_value) {
						local->location.print_error_header();
						fatal_error("Re-declaration of local variable \"", loc2->name, "\" in the same scope.");
					}
				}
			}
			
			auto new_local = new Local_Var_FT(scope);
			
			resolve_arguments(model, module_id, new_local, ast);
			
			new_local->name = local->name.string_value;
			new_local->value_type = new_local->exprs[0]->value_type;
			
			result = new_local;
		} break;
		
		default : {
			fatal_error(Mobius_Error::internal, "Unhandled math expr type in resolve_function_tree().");
		} break;
	}
	
	if(!result)
		fatal_error(Mobius_Error::internal, "Result unassigned in resolve_function_tree().");
	
	result->location = ast->location;
	
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
			//todo: replace with last statement if only one statement ? Probably not, sice it could mess up scopes! Or we would have to fix that in that case
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
				auto literal = new Literal_FT(expr->scope);
				literal->location = expr->location;
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
				auto literal = new Literal_FT(expr->scope);
				literal->value = val;
				literal->value_type = expr->value_type;
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
				auto literal = new Literal_FT(expr->scope);
				literal->value = val;
				literal->value_type = expr->value_type;
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
				auto literal = new Literal_FT(expr->scope);
				literal->value = apply_cast({old_literal->value, old_literal->value_type}, expr->value_type);
				literal->value_type = expr->value_type;
				literal->location = expr->location;
				delete expr;
				return literal;
			}
		} break;
		
		case Math_Expr_Type::identifier_chain : {
			auto ident = reinterpret_cast<Identifier_FT *>(expr);
			if(ident->variable_type == Variable_Type::local) {
				Math_Block_FT *sc = ident->scope;
				while(sc->unique_block_id != ident->local_var.scope_id) {
					sc = sc->scope;
					if(!sc)
						fatal_error(Mobius_Error::internal, "Something went wrong with the scope of an identifier.");
				}
				int index = 0;
				for(auto loc : sc->exprs) {
					if((loc->expr_type == Math_Expr_Type::local_var) 
						&& (index == ident->local_var.index) 
						&& (loc->exprs[0]->expr_type == Math_Expr_Type::literal)) {
							auto literal = new Literal_FT(expr->scope);
							auto loc2        = reinterpret_cast<Local_Var_FT *>(loc);
							auto loc_literal = reinterpret_cast<Literal_FT *>(loc->exprs[0]);
							literal->value =      loc_literal->value;
							literal->value_type = loc_literal->value_type;
							literal->location = ident->location;
							loc2->is_used = false; //   note. we can't remove the local var itself since that would invalidate other local var references, but we could just ignore it in code generation later.
							delete expr;
							return literal;
					}
					++index;
				}
			}
		} break;
		
		
	}
	return expr;
}

void
register_dependencies(Math_Expr_FT *expr, Dependency_Set *depends) {
	for(auto arg : expr->exprs) register_dependencies(arg, depends);
	
	if(expr->expr_type == Math_Expr_Type::identifier_chain) {
		auto ident = reinterpret_cast<Identifier_FT *>(expr);
		if(ident->variable_type == Variable_Type::parameter)
			depends->on_parameter.insert(ident->parameter);
		else if(ident->variable_type == Variable_Type::state_var) {
			State_Var_Dependency dep {ident->state_var, dep_type_none};
			if(ident->flags & ident_flags_last_result)
				dep.type = (Dependency_Type)(dep.type | dep_type_earlier_step);
			
			depends->on_state_var.insert(dep);
		}
		else if(ident->variable_type == Variable_Type::series)
			depends->on_series.insert(ident->series);
	}
}



Math_Expr_FT *
restrict_flux(Math_Expr_FT *flux, Var_Id source) {
	// note: create something like
	// 		min(flux, source)
	//NOTE: we are actually also making an assumption here that the flux is not negative...
	// should we restrict in the other direction as well?
	
	//ident->flags         = ident_flags_last_result;    //ouch, we don't want this actually, because if there are multiple fluxes, they should depend on the removal of the previous...
	auto arg1 = flux;
	auto arg2 = make_state_var_identifier(flux->scope, source);
	
	return make_intrinsic_function_call(flux->scope, Value_Type::real, "min", arg1, arg2);
}


Standardized_Unit
check_units(Math_Expr_FT *expr, Standardized_Unit *expected_top = nullptr) {
	Standardized_Unit result;
	
	//TODO!
	
	switch(expr->expr_type) {
		
		case Math_Expr_Type::block : {
		} break;
		
		
		default : {
			fatal_error(Mobius_Error::internal, "Unhandled expression type in check_units()");
		}
	}
	
	return result;
}

