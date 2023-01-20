

#include "function_tree.h"
#include "emulate.h"
#include "units.h"
#include "model_application.h"

void
Math_Block_FT::set_id() {
	//TODO: if we ever want to paralellize code generation, we have to make a better system here:
	static s32 id_counter = 0;
	unique_block_id = id_counter++;
}

Math_Expr_FT *
make_cast(Math_Expr_FT *expr, Value_Type cast_to) {
	if(cast_to == expr->value_type) return expr;
	
	auto cast = new Math_Expr_FT(Math_Expr_Type::cast);
	cast->source_loc = expr->source_loc;     // not sure if this is good, it is just so that it has a valid location.
	cast->value_type = cast_to;
	cast->exprs.push_back(expr);
	
	return cast;
}

Math_Expr_FT *
make_literal(s64 val_integer) {
	auto literal = new Literal_FT();
	literal->value_type = Value_Type::integer;
	literal->value.val_integer = val_integer;
	return literal;
}

Math_Expr_FT *
make_literal(double val_real) {
	auto literal = new Literal_FT();
	literal->value_type = Value_Type::real;
	literal->value.val_real = val_real;
	return literal;
}

Math_Expr_FT *
make_state_var_identifier(Var_Id state_var) {
	auto ident = new Identifier_FT();
	ident->value_type    = Value_Type::real;
	ident->variable_type = Variable_Type::state_var;
	ident->state_var     = state_var;
	return ident;
}

Math_Expr_FT *
make_local_var_reference(s32 index, s32 scope_id, Value_Type value_type) {
	auto ident = new Identifier_FT();
	ident->value_type    = value_type;
	ident->variable_type = Variable_Type::local;
	ident->local_var.index = index;
	ident->local_var.scope_id = scope_id;
	return ident;
}

Math_Expr_FT *
add_local_var(Math_Block_FT *scope, Math_Expr_FT *val) {
	// NOTE: this should only be called on a block that is under construction.
	auto local = new Local_Var_FT();
	local->exprs.push_back(val);
	local->value_type = val->value_type;
	s32 index = scope->n_locals;
	scope->exprs.push_back(local);
	++scope->n_locals;
	return make_local_var_reference(index, scope->unique_block_id, val->value_type);
}

Math_Expr_FT *
make_intrinsic_function_call(Value_Type value_type, const std::string &name, Math_Expr_FT *arg) {
	auto fun = new Function_Call_FT();
	fun->value_type = value_type;
	fun->fun_name   = name;
	fun->fun_type   = Function_Type::intrinsic;
	fun->exprs.push_back(arg);
	return fun;
}

Math_Expr_FT *
make_intrinsic_function_call(Value_Type value_type, const std::string &name, Math_Expr_FT *arg1, Math_Expr_FT *arg2) {
	auto fun = new Function_Call_FT();
	fun->value_type = value_type;
	fun->fun_name   = name;
	fun->fun_type   = Function_Type::intrinsic;
	fun->exprs.push_back(arg1);
	fun->exprs.push_back(arg2);
	return fun;
}

inline bool
is_boolean_operator(Token_Type op) {
	char c = (char)op;
	return (op == Token_Type::leq || op == Token_Type::geq || c == '=' || c == '&' || c == '|' || c == '<' || c == '>');
}

Math_Expr_FT *
make_binop(Token_Type oper, Math_Expr_FT *lhs, Math_Expr_FT *rhs) {
	if((char)oper != '^' && (lhs->value_type != rhs->value_type))                   // Should we have a separate check for validity of '^' ?
		fatal_error(Mobius_Error::internal, "Mismatching types of arguments in make_binop().");
	auto binop = new Operator_FT(Math_Expr_Type::binary_operator);
	binop->value_type = lhs->value_type;
	if(is_boolean_operator(oper))
		binop->value_type = Value_Type::boolean;
	binop->oper = oper;
	binop->exprs.push_back(lhs);
	binop->exprs.push_back(rhs);
	return binop;
}

Math_Block_FT *
make_for_loop() {
	auto for_loop = new Math_Block_FT();
	for_loop->n_locals = 1;
	for_loop->is_for_loop = true;
	return for_loop;
}

Math_Expr_FT *
make_simple_if(Math_Expr_FT *first, Math_Expr_FT *condition, Math_Expr_FT *otherwise) {
	auto if_expr = new Math_Expr_FT();
	if_expr->expr_type = Math_Expr_Type::if_chain;
	if_expr->value_type = first->value_type;
	if_expr->exprs.push_back(first);
	if_expr->exprs.push_back(condition);
	if_expr->exprs.push_back(otherwise);
	return if_expr;
}

Math_Expr_FT *
make_safe_divide(Math_Expr_FT *lhs, Math_Expr_FT *rhs) {
	/*
	Equivalent to:
	
	res := lhs / rhs
	res if is_finite(res),
	0   otherwise
	*/
	auto block = new Math_Block_FT();
	block->value_type = Value_Type::real;
	auto res = make_binop('/', lhs, rhs);
	auto res_ref = add_local_var(block, res);
	auto cond = make_intrinsic_function_call(Value_Type::boolean, "is_finite", res_ref);
	auto if_expr = make_simple_if(res_ref, cond, make_literal((double)0.0));
	block->exprs.push_back(if_expr);
	return block;
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
	std::string n = name->string_value;
	if(false) {}
	#define MAKE_INTRINSIC1(fun_name, emul, llvm, ret_type, type1) \
		else if(n == #fun_name) { \
			fun->exprs[0] = make_cast(fun->exprs[0], Value_Type::type1); \
			fun->value_type = Value_Type::ret_type; \
		}
	#define MAKE_INTRINSIC2(fun_name, emul, ret_type, type1, type2) \
		else if(n == #fun_name) { \
			if(Value_Type::type1 == Value_Type::unresolved) { \
				make_casts_for_binary_expr(&fun->exprs[0], &fun->exprs[1]); \
				fun->value_type = fun->exprs[0]->value_type; \
			} else { \
				fun->exprs[0] = make_cast(fun->exprs[0], Value_Type::type1); \
				fun->exprs[1] = make_cast(fun->exprs[1], Value_Type::type2); \
			} \
		}
	#include "intrinsics.incl"
	#undef MAKE_INTRINSIC1
	#undef MAKE_INTRINSIC2
	else {
		fatal_error(Mobius_Error::internal, "Unhandled intrinsic \"", name, "\" in fixup_intrinsic().");
	}
}

struct
Function_Scope {
	Function_Scope *parent;
	Math_Block_FT *block;
	std::vector<Standardized_Unit> local_var_units;
	std::string function_name;
	
	Function_Scope() : parent(nullptr), block(nullptr), function_name("") {}
};

void
resolve_arguments(Math_Expr_FT *ft, Math_Expr_AST *ast, Function_Resolve_Data *data, Function_Scope *scope, std::vector<Standardized_Unit> &units) {
	//TODO allow error check on expected number of arguments
	for(auto arg : ast->exprs) {
		auto result = resolve_function_tree(arg, data, scope);
		ft->exprs.push_back(result.fun);
		if(result.fun->expr_type == Math_Expr_Type::local_var) {
			scope->local_var_units.push_back(result.unit);
			scope->block->n_locals++;
		}
		units.push_back(std::move(result.unit));
	}
}

bool
find_local_variable(Identifier_FT *ident, Standardized_Unit &unit, const std::string &name, Function_Scope *scope) {
	if(!scope) return false;
	
	auto block = scope->block;
	if(!block->is_for_loop) {
		int idx = 0;
		for(auto expr : block->exprs) {
			if(expr->expr_type == Math_Expr_Type::local_var) {
				auto local = static_cast<Local_Var_FT *>(expr);
				//warning_print("Check ", name, " against ", local->name, ".\n");
				if(local->name == name) {
					ident->variable_type = Variable_Type::local;
					ident->local_var.index = idx;
					ident->local_var.scope_id = block->unique_block_id;
					ident->value_type = local->value_type;
					local->is_used = true;
					unit = scope->local_var_units[idx];
					return true;
				}
			}
			++idx;
		}
	}
	if(scope->function_name.empty())  //NOTE: scopes should not "bleed through" function substitutions.
		return find_local_variable(ident, unit, name, scope->parent);
	return false;
}

bool
is_inside_function(Function_Scope *scope, const std::string &name) {
	Function_Scope *sc = scope;
	while(true) {
		if(!sc) return false;
		if(sc->function_name == name) return true;
		sc = sc->parent;
	}
	return false;
}

bool
is_inside_function(Function_Scope *scope) {
	Function_Scope *sc = scope;
	while(true) {
		if(!sc) return false;
		if(!sc->function_name.empty()) return true;
		sc = sc->parent;
	}
	return false;
}

void
fatal_error_trace(Function_Scope *scope) {
	Function_Scope *sc = scope;
	error_print("\n");
	while(true) {
		if(!sc) mobius_error_exit();
		if(!sc->function_name.empty()) {
			error_print("In function \"", sc->function_name, "\", called from ");
			sc->block->source_loc.print_error();
		}
		sc = sc->parent;
	}
}

//TODO: could probably be factored as a utility function for the model. Similar to functionality in MobiView and c_api. Try to make a better api for accessing state variables in general.
inline Var_Id
find_var_at_location(Var_Location &loc, Model_Application *app) {
	auto var_id = app->state_vars.id_of(loc);
	if(!is_valid(var_id))
	var_id = app->series.id_of(loc);
	return var_id;
}


Var_Location
make_var_location(const std::vector<Entity_Id> &chain) {
	Var_Location result = {};
	result.type = Var_Location::Type::located;
	result.n_components = chain.size();
	for(int idx = 0; idx < result.n_components; ++idx)
		result.components[idx] = chain[idx];
	return result;
}

Var_Id
try_to_locate_variable(Var_Location &context, const std::vector<Entity_Id> &chain, std::vector<Token> &tokens, Model_Application *app, Function_Scope *scope) {

	if(chain.size() > max_var_loc_components) {
		tokens[0].print_error_header();
		error_print("Too long chain of identifiers.");
		fatal_error_trace(scope);
	}

	if(app->model->components[chain[0]]->decl_type == Decl_Type::compartment) {
		// In this case, assume this is a full location specifier.
		//TODO test that the chain is valid in the sense of the middle ones being quantities and the last being property or quantity.
		//also validity of chain size.
		Var_Location loc = make_var_location(chain);
		return find_var_at_location(loc, app);
	}
	// TODO: test validity of the chain in the sense of the first ones being quantities and the last being property or quantity.
	
	Var_Id result = invalid_var;
	if(!is_located(context))
		return result;
	
	// Try out various combinations based on the context.
	std::vector<Entity_Id> context_chain;
	for(int idx = 0; idx < context.n_components; ++idx)
		context_chain.push_back(context.components[idx]); // TODO: maybe try the last one only if it is a quantity..
	
	// NOTE: we test the "closest" locations first, then go back. E.g.  temp will match to soil.water.temp in the context of soil.water if soil.water.temp exists, otherwise try soil.temp .
	while(true) {
		if(context_chain.size() + chain.size() > max_var_loc_components) {
			context_chain.pop_back();
			continue;
		}
		std::vector<Entity_Id> try_chain = context_chain;
		try_chain.insert(try_chain.end(), chain.begin(), chain.end());
		Var_Location loc = make_var_location(try_chain);
		result = find_var_at_location(loc, app);
		if(is_valid(result))
			return result;
		if(context_chain.empty())
			return result;
		context_chain.pop_back();
	}
}

void
set_identifier_location(Function_Resolve_Data *data, Standardized_Unit &unit, Identifier_FT *ident, Var_Id var_id, std::vector<Token> &chain, Function_Scope *scope) {
	Source_Location sl = chain[0].source_loc;
	if(!is_valid(var_id)) {
		sl.print_error_header();
		error_print("The identifier\n");
		int idx = 0;
		for(Token &token : chain)
			error_print(token.string_value, idx++ == chain.size()-1 ? "" : ".");
		error_print("\ncan not be inferred as a valid state variable that has been created using a 'has' declaration. ");
		if(is_located(data->in_loc)) {
			error_print("It was being resolved in the following context: ");
			error_print_location(data->scope, data->in_loc);
		} else
			error_print("The location can not be inferred from the context.");
		fatal_error_trace(scope);
	}
	if(var_id.type == Var_Id::Type::state_var) {
		ident->variable_type = Variable_Type::state_var;
		ident->state_var     = var_id;
		unit = data->app->state_vars[var_id]->unit.standard_form;
	} else {
		ident->variable_type = Variable_Type::series;
		ident->series        = var_id;
		unit = data->app->series[var_id]->unit.standard_form;
	}
	ident->value_type = Value_Type::real;
}

Math_Expr_FT *
fixup_potentially_baked_value(Model_Application *app, Math_Expr_FT *expr, std::vector<Entity_Id> *baked_parameters) {
	if(expr->expr_type != Math_Expr_Type::identifier_chain) return expr;
	
	auto ident = static_cast<Identifier_FT *>(expr);
	if(ident->variable_type != Variable_Type::parameter) return expr; //TODO: Could maybe eventually bake others.
	auto par_id = ident->parameter;
	if(std::find(baked_parameters->begin(), baked_parameters->end(), par_id) == baked_parameters->end()) return expr;
	
	// NOTE: currently we only do this for single-instanced parameters.
	s64 offset = app->parameter_structure.get_offset_base(par_id);
	Parameter_Value val = *app->data.parameters.get_value(offset);
	
	auto literal = new Literal_FT();
	literal->value_type = ident->value_type;
	literal->source_loc   = ident->source_loc;
	literal->value      = val;
	
	warning_print("Baking ", app->model->parameters[par_id]->name, "\n");
	
	delete ident;
	return literal;
}

void
set_time_unit(Standardized_Unit &unit, Model_Application *app, Variable_Type type) {
	//TODO: Could we find a way to use time_values.incl for the units too somehow?
	switch(type) {
		case Variable_Type::time_year : {
			unit = unit_atom(Base_Unit::year);
		} break;
		
		case Variable_Type::time_month : {
			unit = unit_atom(Base_Unit::month);
		} break;
		
		case Variable_Type::time_day_of_year :
		case Variable_Type::time_day_of_month : {
			unit = unit_atom(Base_Unit::s, 86400);
		} break;
		
		case Variable_Type::time_days_this_year : {
			unit = unit_atom(Base_Unit::s, 86400);
			unit.powers[(int)Base_Unit::year] = -1;
		} break;
		
		case Variable_Type::time_days_this_month : {
			unit = unit_atom(Base_Unit::s, 86400);
			unit.powers[(int)Base_Unit::month] = -1;
		} break;
		
		case Variable_Type::time_second_of_day :
		case Variable_Type::time_step_length_in_seconds : {
			unit = unit_atom(Base_Unit::s);
		} break;
		
		case Variable_Type::time_step : {
			unit = app->time_step_unit.standard_form;
		} break;
		
		default : {
			fatal_error(Mobius_Error::internal, "Unhandled time variable type in set_time_unit()");
		} break;
	}
}

void
set_intrinsic_unit(Standardized_Unit &result_unit, Standardized_Unit &arg_unit, const std::string &fun_name, Source_Location &error_loc, Function_Scope *scope) {
	if(fun_name == "is_finite")
		return;
	if(fun_name == "abs" || fun_name == "floor" || fun_name == "ceil") {
		result_unit = arg_unit;
		return;
	}
	bool success = true;
	if(fun_name == "sqrt" || fun_name == "cbrt") {
		s16 pw = 2;
		if(fun_name == "cbrt") pw = 3;
		success = pow(arg_unit, result_unit, Rational<s16>(1, pw));
	} else if(!arg_unit.is_fully_dimensionless()) {
		success = false;
	}
	if(!success) {
		error_loc.print_error_header();
		error_print("Unable to apply function ", fun_name, " to unit on standardized form ", arg_unit.to_utf8(), ". Try to force convert the argument  =>[] if necessary.");
		fatal_error_trace(scope);
	}
}

void
check_boolean_dimensionless(Standardized_Unit &unit, Source_Location &error_loc, Function_Scope *scope) {
	if(!unit.is_dimensionless()) {
		error_loc.print_error_header();
		error_print("A value that is treated as a boolean value should be dimensionless. This value has unit on standard form ", unit.to_utf8(), ". Try to force convert the argument  =>[] if necessary.");
		fatal_error_trace(scope);
	}
}

bool
is_constant_dimensionless_integer(Standardized_Unit &unit, Math_Expr_FT **expr, Function_Scope *scope, s64 *result) {
	*expr = prune_tree(*expr, scope);	// TODO: It is not that good to do this at this stage as it may mask other consistency checks in model_composition, but it is very hard to get around :(
	auto expr0 = *expr;
	if(expr0->expr_type != Math_Expr_Type::literal) {
		//warning_print("\nUnresolved tree:\n");
		//print_tree(expr0);
		//warning_print("\n\n");
		return false;
	}
	auto literal = static_cast<Literal_FT *>(expr0);
	bool found = false;
	if(literal->value_type == Value_Type::real && literal->value.val_real == 0.0) {  // Not sure if we should accept other values.
		*result = 0;
		found = true;
	} else if(literal->value_type == Value_Type::integer) {
		*result = literal->value.val_integer;
		found = true;
	} else {
		*result = literal->value.val_boolean;
		found = true;
	}
	bool success = found && (unit.is_dimensionless() || *result==0);
	
	return success;
}

void
apply_binop_to_units(Token_Type oper, const std::string &name, Standardized_Unit &result, Standardized_Unit &a, Standardized_Unit &b, Source_Location &oper_loc, Function_Scope *scope, Math_Expr_FT **lhs, Math_Expr_FT **rhs) {
	char op = (char)oper;
	if(op == '|' || op == '&') {
		check_boolean_dimensionless(a, (*lhs)->source_loc, scope);
		check_boolean_dimensionless(b, (*rhs)->source_loc, scope);
	} else if (op == '<' || op == '>' || oper == Token_Type::geq || oper == Token_Type::leq || op == '=' || oper == Token_Type::neq || op == '+' || op == '-' || op == '%') {
		// TODO: If one of the sides resolves to a constant 0, that should always match (but that may involve pruning first).
		bool lhs_is_0 = false;
		if(!match_exact(&a, &b)) {
			s64 val = -1;
			lhs_is_0 = is_constant_dimensionless_integer(a, lhs, scope, &val) && val == 0;
			val = -1;
			bool rhs_is_0 = is_constant_dimensionless_integer(b, rhs, scope, &val) && val == 0;
			if(!lhs_is_0 && !rhs_is_0) {
				// NOTE: If either side is a constant 0, we allow the match.
				oper_loc.print_error_header();
				error_print("The units of the two arguments to ", name, " must be the same. The standard form of the units given are ", a.to_utf8(), " and ", b.to_utf8(), ".");
				fatal_error_trace(scope);
			}
		}
		if(op == '+' || op == '-' || op == '%') {
			// TODO: Should deg_c - deg_c = K ? Makes a bit more sense, and we often find ourselves having to do this conversion any way.
			// Just in case one of them was a 0, we should use the other one if that had a proper unit.
			if(lhs_is_0) result = b;
			else         result = a;
		}
		// Otherwise we don't set the result unit, so it remains dimensionless.
	} else if (op == '*') {
		result = multiply(a, b, 1);
	} else if (op == '/') {
		result = multiply(a, b, -1);
	} else if (op == '^') {
		// TODO: Ideally we should analyze rhs better, e.g. see if it could be pruned, or see if it could be identified as a rational.
		s64 val = -1;
		if(b.is_fully_dimensionless() && a.is_dimensionless()) {
			// Do nothing, the unit should remain dimensionless.
		} else if(is_constant_dimensionless_integer(b, rhs, scope, &val)) {
			pow(a, result, Rational<s16>((s16)val)); // Power of integer should not fail, so we don't have to check the return code.
		} else {
			(*lhs)->source_loc.print_error_header();
			error_print("In a power expression ^, to resolve the units, either both sides must be dimensionless, or the right hand side must be a dimensionless integer.");
			fatal_error_trace(scope);
		}
	} else
		fatal_error(Mobius_Error::internal, "Unhandled binary operator ", name, " in apply_binop_to_units.");
}

void
check_if_expr_units(Standardized_Unit &result, std::vector<Standardized_Unit> &units, std::vector<Math_Expr_FT *> &exprs, Function_Scope *scope) {
	// Check conditions:
	for(int idx = 1; idx < units.size(); idx+=2)
		check_boolean_dimensionless(units[idx], exprs[idx]->source_loc, scope);
	// Check values:
	Standardized_Unit *first_valid = nullptr;
	for(int idx = 0; idx < units.size(); idx+=2) {
		s64 val = -1;
		bool is_0 = is_constant_dimensionless_integer(units[idx], &exprs[idx], scope, &val) && val==0;
		if(is_0) continue; // We allow 0 to match against any other unit here.
		if(!first_valid) {
			first_valid = &units[idx];
			result = units[idx];
		} else {
			if(!match_exact(&units[idx], first_valid)) {
				exprs[idx]->source_loc.print_error_header();
				error_print("The unit of this expression with standard form ", units[idx].to_utf8(), " is not identical to ", first_valid->to_utf8(), ", which is the unit of previous possible values of this if expression.");
				fatal_error_trace(scope);
			}
		}
	}
}

Function_Resolve_Result
resolve_function_tree(Math_Expr_AST *ast, Function_Resolve_Data *data, Function_Scope *scope) {
	Function_Resolve_Result result = { nullptr, {}};
	
#define DEBUGGING_NOW 0
#if DEBUGGING_NOW
	warning_print("begin ", name(ast->type), "\n");
#endif
	
	Decl_Scope &decl_scope = *data->scope;
	
	auto app   = data->app;
	auto model = data->app->model;
	
	switch(ast->type) {
		
		case Math_Expr_Type::block : {
			auto new_block = new Math_Block_FT();
			Function_Scope new_scope;
			new_scope.parent = scope;
			new_scope.block = new_block;
			
			std::vector<Standardized_Unit> arg_units;
			resolve_arguments(new_block, ast, data, &new_scope, arg_units);
			
			// the value of a block is the value of the last expression in the block.
			Math_Expr_FT *last = new_block->exprs.back();
			if(last->value_type == Value_Type::none) {
				last->source_loc.print_error_header();
				error_print("The last statement in a block must evaluate to a value.\n");
				fatal_error_trace(scope);
			}
			new_block->value_type = last->value_type;
			result = {new_block, std::move(arg_units.back())};
		} break;
		
		case Math_Expr_Type::identifier_chain : {
			auto ident = static_cast<Identifier_Chain_AST *>(ast);
			auto new_ident = new Identifier_FT();
			result.fun = new_ident;
			
			int chain_size = ident->chain.size();
			
			bool isfun = is_inside_function(scope);
			bool found = false;
			
			std::string n1 = ident->chain[0].string_value;
			
			if(chain_size == 1) {
				if(!isfun && n1 == "no_override") {
					new_ident->variable_type = Variable_Type::no_override;
					new_ident->value_type = Value_Type::real;
					found = true;
					result.unit = data->expected_unit;
				} else
					found = find_local_variable(new_ident, result.unit, n1, scope);
			}
			
			auto reg = decl_scope[n1];
			if(!found && chain_size == 1 && reg && reg->id.reg_type == Reg_Type::constant) {
				delete new_ident; // A little stupid to do it that way, but oh well.
				auto const_decl = model->constants[reg->id];
				result.fun  = make_literal(const_decl->value);
				result.unit = model->units[const_decl->unit]->data.standard_form;
				found = true;
			}
			
			if(isfun && !found) {
				ident->chain[0].print_error_header();
				error_print("The name '", n1, "' is not the name of a function argument or a local variable. Note that parameters and state variables can not be accessed inside functions directly, but have to be passed as arguments.\n");
				fatal_error_trace(scope);
			}
			
			if(!found) {
				if(chain_size == 1) {
					if(!reg) {
						ident->chain[0].print_error_header();
						error_print("The name '", n1, "' is not the name of a local variable or entity declared or loaded in this scope.\n");
						fatal_error_trace(scope);
					}
					Entity_Id id = reg->id;
					
					if(id.reg_type == Reg_Type::parameter) {
						
						auto par = model->parameters[id];
						
						if(par->decl_type == Decl_Type::par_enum) {
							ident->chain[0].print_error_header();
							error_print("Enum parameters should be referenced directly. Instead use the syntax name.value.\n");
							fatal_error_trace(scope);
						} else if (par->decl_type == Decl_Type::par_datetime) {
							ident->chain[0].print_error_header();
							error_print("It is currently not supported to look up the value of date time parameters inside equations.\n");
							fatal_error_trace(scope);
						}
						new_ident->variable_type = Variable_Type::parameter;
						new_ident->parameter = id;
						new_ident->value_type = get_value_type(par->decl_type);
						if(is_valid(par->unit)) // For e.g. boolean it will not have a unit, which means dimensionless, but that is the default of the result, so we don't need to set it.
							result.unit = model->units[par->unit]->data.standard_form;
					} else if (id.reg_type == Reg_Type::component) {
						if(!is_located(data->in_loc)) {
							ident->chain[0].print_error_header();
							error_print("The name \"", n1, "\" can not properly be resolved since the location can not be inferred from the context.\n");
							fatal_error_trace(scope);
						}
						Var_Id var_id = try_to_locate_variable(data->in_loc, { id }, ident->chain, app, scope);
						set_identifier_location(data, result.unit, new_ident, var_id, ident->chain, scope);
					} else {
						ident->chain[0].print_error_header();
						error_print("The name \"", n1, "\" is not the name of a parameter or local variable.\n");
						fatal_error_trace(scope);
					}
				} else if(chain_size >= 2) {
					// This is either a time.xyz, a compartment.quantity_or_property, or an enum_par.enum_value
					std::string n2 = ident->chain[1].string_value;
					
					bool resolved = false;
					if(chain_size == 2) {
						if(n1 == "time") {
							if(false){}
							#define TIME_VALUE(name, bits) \
							else if(n2 == #name) new_ident->variable_type = Variable_Type::time_##name;
							#include "time_values.incl"
							#undef TIME_VALUE
							else {
								ident->chain[1].print_error_header();
								error_print("The time structure does not have a member \"", n2, "\".\n");
								fatal_error_trace(scope);
							}
							new_ident->value_type = Value_Type::integer;
							resolved = true;
							set_time_unit(result.unit, app, new_ident->variable_type);
						} else {
							auto reg = decl_scope[n1];
							if(!reg) {
								ident->chain[0].print_error_header();
								error_print("The name '", n1, "' is not the name of an entity declared or loaded in this scope.\n");
								fatal_error_trace(scope);
							}
							if(reg->id.reg_type == Reg_Type::parameter) {
								auto parameter = model->parameters[reg->id];
								
								if(parameter->decl_type != Decl_Type::par_enum) {
									ident->chain[0].print_error_header();
									error_print("The syntax name.value is only available for enum parameters. The parameter \"", n1, "\" is of type ", name(parameter->decl_type), ".\n");
									fatal_error_trace(scope);
								}
								s64 val = enum_int_value(parameter, n2);
								if(val < 0) {
									ident->chain[1].print_error_header();
									error_print("The name \"", n2, "\" was not registered as a possible value for the parameter \"", n1, "\".\n");
									fatal_error_trace(scope);
								}
								new_ident->variable_type = Variable_Type::parameter;
								new_ident->parameter = reg->id;
								new_ident->value_type = Value_Type::integer;
								auto ft = fixup_potentially_baked_value(app, new_ident, data->baked_parameters);
								result.fun = make_binop('=', ft, make_literal(val));
								resolved = true;
								// NOTE: In this case we don't set a unit, so it is dimensionless (which is what makes sense for truth values).
							}
						}
					}
					if(!resolved) {
						std::vector<Entity_Id> chain;
						for(int idx = 0; idx < chain_size; ++idx) {
							std::string str = ident->chain[idx].string_value;
							auto reg = decl_scope[str];
							if(!reg) {
								ident->chain[idx].print_error_header();
								error_print("The name \"", str, "\" is not the name of an entity declared in this scope.");
								fatal_error_trace(scope);
							}
							chain.push_back(reg->id);
						}
						Var_Id var_id = try_to_locate_variable(data->in_loc, chain, ident->chain, app, scope);
						set_identifier_location(data, result.unit, new_ident, var_id, ident->chain, scope);
					}
				} else {
					ident->chain[0].print_error_header();
					error_print("Too many identifiers in chain.\n");
					fatal_error_trace(scope);
				}
			}
			
			result.fun = fixup_potentially_baked_value(app, result.fun, data->baked_parameters);
		} break;
		
		case Math_Expr_Type::literal : {
			auto literal = static_cast<Literal_AST *>(ast);
			auto new_literal = new Literal_FT();
			
			new_literal->value_type = get_value_type(literal->value.type);
			new_literal->value      = get_parameter_value(&literal->value, literal->value.type);
			
			result.fun = new_literal;
			// NOTE: Unit not given so is dimensionless (but can instead be forced in a parent unit conversion).
		} break;
		
		case Math_Expr_Type::function_call : {
			auto fun = static_cast<Function_Call_AST *>(ast);
			
			std::string fun_name = fun->name.string_value;
			
			// First check for "special" calls that are not really function calls.
			if(fun_name == "last" || fun_name == "in_flux" || fun_name == "aggregate" || fun_name == "conc" || fun_name == "target") {
				auto new_fun = new Function_Call_FT(); // Hmm it is a bit annoying to have to do this only to delete it again.
				std::vector<Standardized_Unit> arg_units;
				resolve_arguments(new_fun, ast, data, scope, arg_units);
				if(new_fun->exprs.size() != 1) {
					fun->name.print_error_header();
					error_print("A ", fun_name, "() declaration only takes one argument.\n");
					fatal_error_trace(scope);
				}
				if(new_fun->exprs[0]->expr_type != Math_Expr_Type::identifier_chain) {
					new_fun->exprs[0]->source_loc.print_error_header();
					error_print("A ", fun_name, "() declaration can only be applied to a state variable or input series.\n");
					fatal_error_trace(scope);
				}
				auto var = static_cast<Identifier_FT *>(new_fun->exprs[0]);
				new_fun->exprs.clear();
				delete new_fun;
				bool can_series = (fun_name != "in_flux") && (fun_name != "conc");
				bool can_param  = (fun_name == "target");
				if(!(var->variable_type == Variable_Type::state_var || (can_series && var->variable_type == Variable_Type::series) || (can_param && var->variable_type == Variable_Type::parameter))) {
					var->source_loc.print_error_header();
					error_print("A ", fun_name, "() declaration can only be applied to a state variable");
					if(can_series) error_print(" or input series");
					if(can_param)  error_print(" or parameter");
					error_print(".\n");
					fatal_error_trace(scope);
				}
				if(fun_name == "last")
					var->flags = (Identifier_FT::Flags)(var->flags | Identifier_FT::Flags::last_result);
				else if(fun_name == "in_flux")
					var->flags = (Identifier_FT::Flags)(var->flags | Identifier_FT::Flags::in_flux);
				else if(fun_name == "aggregate")
					var->flags = (Identifier_FT::Flags)(var->flags | Identifier_FT::Flags::aggregate);
				else if(fun_name == "conc")
					var->flags = (Identifier_FT::Flags)(var->flags | Identifier_FT::Flags::conc);
				else if(fun_name == "target")
					var->flags = (Identifier_FT::Flags)(var->flags | Identifier_FT::Flags::target);
				
				result.fun = var;
				if(fun_name == "in_flux") {
					result.unit = multiply(arg_units[0], app->time_step_unit.standard_form, -1);
				} else if( fun_name == "conc") {
					auto conc_id = as<State_Var::Type::declared>(data->app->state_vars[var->state_var])->conc;
					result.unit = data->app->state_vars[conc_id]->unit.standard_form;
				} else
					result.unit = std::move(arg_units[0]);
			} else {
				// Otherwise it should have been registered as an entity.

				auto reg = decl_scope[fun_name];
				if(!reg || reg->id.reg_type != Reg_Type::function) {
					fun->name.print_error_header();
					error_print("The name \"", fun_name, "\" has not been declared as a function.\n");
					fatal_error_trace(scope);
				}
				auto fun_decl = model->functions[reg->id];
				auto fun_type = fun_decl->fun_type;
				
				//TODO: should be replaced with check in resolve_arguments
				if(fun->exprs.size() != fun_decl->args.size()) {
					fun->name.print_error_header();
					error_print("Wrong number of arguments to function \"", fun_name, "\". Expected ", fun_decl->args.size(), ", got ", fun->exprs.size(), ".\n");
					fatal_error_trace(scope);
				}
				
				if(fun_type == Function_Type::intrinsic) {
					auto new_fun = new Function_Call_FT();
					
					std::vector<Standardized_Unit> arg_units;
					resolve_arguments(new_fun, ast, data, scope, arg_units);
				
					new_fun->fun_type = fun_type;
					new_fun->fun_name = fun_name;
					fixup_intrinsic(new_fun, &fun->name);
					
					result.fun = new_fun;
					if(arg_units.size() == 1) {
						set_intrinsic_unit(result.unit, arg_units[0], fun_name, fun->source_loc, scope);
					} else if (arg_units.size() == 2) {
						// NOTE: The min and max functions behave like '+' when it comes to units
						apply_binop_to_units((Token_Type)'+', fun_name, result.unit, arg_units[0], arg_units[1], fun->source_loc, scope, &new_fun->exprs[0], &new_fun->exprs[1]);
					} else
						fatal_error(Mobius_Error::internal, "Unhandled number of arguments to intrinsic when unit checking");
				} else if(fun_type == Function_Type::decl) {
					if(is_inside_function(scope, fun_name)) {
						fun->name.print_error_header();
						error_print("The function \"", fun_name, "\" calls itself either directly or indirectly. This is not allowed.\n");
						fatal_error_trace(scope);
					}
					// Inline in the function call as a new block with the arguments as local vars.
					auto inlined_fun = new Math_Block_FT();
					inlined_fun->source_loc = fun->source_loc; //NOTE: do this to get correct diagnostics in fatal_error_trace()
					
					std::vector<Standardized_Unit> arg_units;
					resolve_arguments(inlined_fun, ast, data, scope, arg_units);
					
					inlined_fun->n_locals = inlined_fun->exprs.size();
					for(int argidx = 0; argidx < inlined_fun->exprs.size(); ++argidx) {
						auto arg = inlined_fun->exprs[argidx];
						auto inlined_arg = new Local_Var_FT();
						inlined_arg->exprs.push_back(arg);
						inlined_arg->name = fun_decl->args[argidx];
						inlined_arg->value_type = arg->value_type;
						inlined_fun->exprs[argidx] = inlined_arg;
						
						// NOTE: With the current implementation,
						// is_constant_dimensionless_integer could mute these, which is why we
						// do it like this. Should probably be fixed eventually.
						auto &argg = inlined_arg->exprs[0];
						auto loc = inlined_arg->exprs[0]->source_loc;
						if(is_valid(fun_decl->expected_units[argidx])) {
							auto &expect_unit = model->units[fun_decl->expected_units[argidx]]->data.standard_form;
							s64 val = -1;
							if(is_constant_dimensionless_integer(arg_units[argidx], &argg, scope, &val) && val == 0) continue;  // Constant 0 match against any unit
							if(!match_exact(&expect_unit, &arg_units[argidx])) {
								loc.print_error_header();
								error_print("The function declaration requires a unit (which on standard form is) ", expect_unit.to_utf8(), " for argument ", argidx, ", but we got ", arg_units[argidx].to_utf8(), ". See declaration of function here:\n");
								fun_decl->source_loc.print_error();
								fatal_error_trace(scope);
							}
						}
					}
					
					Function_Scope new_scope;
					new_scope.parent = scope;
					new_scope.block = inlined_fun;
					new_scope.function_name = fun_name;
					new_scope.local_var_units = std::move(arg_units);
					
					Function_Resolve_Data sub_data = *data;
					sub_data.scope = model->get_scope(fun_decl->code_scope);  // Resolve the function body in the scope of the library it was imported from (if relevant).
					
					auto res = resolve_function_tree(fun_decl->code, &sub_data, &new_scope);
					inlined_fun->exprs.push_back(res.fun);
					inlined_fun->value_type = inlined_fun->exprs.back()->value_type; // The value type is whatever the body of the function resolves to given these arguments.
					
					result.fun = inlined_fun;
					result.unit = std::move(res.unit);
				} else
					fatal_error(Mobius_Error::internal, "Unhandled function type.");
			}
		} break;
		
		case Math_Expr_Type::unary_operator : {
			auto unary = static_cast<Unary_Operator_AST *>(ast);
			auto new_unary = new Operator_FT(Math_Expr_Type::unary_operator);
			
			std::vector<Standardized_Unit> arg_units;
			resolve_arguments(new_unary, ast, data, scope, arg_units);
			
			new_unary->oper = unary->oper;
			
			if((char)unary->oper == '-') {
				if(new_unary->exprs[0]->value_type == Value_Type::boolean) {
					new_unary->exprs[0]->source_loc.print_error_header();
					error_print("Unary minus can not have an argument of type boolean.\n");
					fatal_error_trace(scope);
				}
				new_unary->value_type = new_unary->exprs[0]->value_type;
			} else if((char)unary->oper == '!') {
				new_unary->exprs[0] = make_cast(new_unary->exprs[0], Value_Type::boolean);
				new_unary->value_type = Value_Type::boolean;
			} else
				fatal_error(Mobius_Error::internal, "Unhandled unary operator type in resolve_function_tree().");
			
			if((char)new_unary->oper == '!')
				check_boolean_dimensionless(arg_units[0], unary->source_loc, scope);
			result.fun = new_unary;
			result.unit = std::move(arg_units[0]);
		} break;
		
		case Math_Expr_Type::binary_operator : {
			auto binary = static_cast<Binary_Operator_AST *>(ast);
			auto new_binary = new Operator_FT(Math_Expr_Type::binary_operator);
			
			std::vector<Standardized_Unit> arg_units;
			resolve_arguments(new_binary, ast, data, scope, arg_units);
			
			new_binary->oper = binary->oper;
			char op = (char)binary->oper;
			
			if(op == '|' || op == '&') {
				new_binary->exprs[0] = make_cast(new_binary->exprs[0], Value_Type::boolean);
				new_binary->exprs[1] = make_cast(new_binary->exprs[1], Value_Type::boolean);
				
				new_binary->value_type = Value_Type::boolean;
			} else if (op == '^') {
				//Note: we could implement pow for lhs of type int too, but llvm does not have an intrinsic for it, and there is unlikely to be any use case.
				new_binary->value_type = Value_Type::real;
				new_binary->exprs[0]   = make_cast(new_binary->exprs[0], Value_Type::real);
				if(new_binary->exprs[1]->value_type == Value_Type::boolean) new_binary->exprs[1] = make_cast(new_binary->exprs[1], Value_Type::integer);
			} else if (op == '%') {
				if(new_binary->exprs[0]->value_type == Value_Type::real || new_binary->exprs[1]->value_type == Value_Type::real) {
					binary->source_loc.print_error_header();
					error_print("Operator % can only take integer arguments.\n");
					fatal_error_trace(scope);
				}
				new_binary->exprs[0] = make_cast(new_binary->exprs[0], Value_Type::integer);
				new_binary->exprs[1] = make_cast(new_binary->exprs[1], Value_Type::integer);
				new_binary->value_type = Value_Type::integer;
			} else {
				make_casts_for_binary_expr(&new_binary->exprs[0], &new_binary->exprs[1]);
				
				if(op == '+' || op == '-' || op == '*' || op == '/')
					new_binary->value_type = new_binary->exprs[0]->value_type;
				else
					new_binary->value_type = Value_Type::boolean;
			}
			
			result.fun = new_binary;
			apply_binop_to_units(new_binary->oper, name(new_binary->oper), result.unit, arg_units[0], arg_units[1], binary->source_loc, scope, &new_binary->exprs[0], &new_binary->exprs[1]);
		} break;
		
		case Math_Expr_Type::if_chain : {
			auto ifexpr = static_cast<If_Expr_AST *>(ast);
			auto new_if = new Math_Expr_FT(Math_Expr_Type::if_chain);
			
			std::vector<Standardized_Unit> arg_units;
			resolve_arguments(new_if, ast, data, scope, arg_units);
			
			// Cast all possible result values up to the same type
			Value_Type value_type = new_if->exprs[0]->value_type;
			for(int idx = 0; idx < (int)new_if->exprs.size()-1; idx+=2) {
				new_if->exprs[idx+1] = make_cast(new_if->exprs[idx+1], Value_Type::boolean);
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
			
			result.fun = new_if;
			check_if_expr_units(result.unit, arg_units, new_if->exprs, scope);
		} break;
		
		case Math_Expr_Type::local_var : {
			
			auto local = static_cast<Local_Var_AST *>(ast);
			std::string local_name = local->name.string_value;
			
			for(auto loc : scope->block->exprs) {
				if(loc->expr_type == Math_Expr_Type::local_var) {
					auto loc2 = static_cast<Local_Var_FT *>(loc);
					
					if(loc2->name == local_name) {
						local->source_loc.print_error_header();
						error_print("Re-declaration of local variable \"", loc2->name, "\" in the same scope.\n");
						fatal_error_trace(scope);
					}
				}
			}
			
			auto new_local = new Local_Var_FT();
			
			std::vector<Standardized_Unit> arg_units;
			resolve_arguments(new_local, ast, data, scope, arg_units);
			
			new_local->name = local_name;
			new_local->value_type = new_local->exprs[0]->value_type;
			
			result.fun = new_local;
			result.unit = std::move(arg_units[0]);
		} break;
		
		case Math_Expr_Type::unit_convert : {
			auto conv = static_cast<Unit_Convert_AST *>(ast);
			auto new_binary = new Operator_FT(Math_Expr_Type::binary_operator);
			std::vector<Standardized_Unit> arg_units;
			resolve_arguments(new_binary, ast, data, scope, arg_units);
			
			Standardized_Unit to_unit;
			if(conv->auto_convert) {
				to_unit = data->expected_unit;
			} else {
				Unit_Data conv_unit;
				set_unit_data(conv_unit, conv->unit);
				to_unit = std::move(conv_unit.standard_form);
			}
			
			bool offset = false;
			double conversion_factor = 1.0;
			if(!conv->force) {
				offset = match_offset(&arg_units[0], &to_unit, &conversion_factor);
				if(!offset) {
					bool success = match(&arg_units[0], &to_unit, &conversion_factor);
					if(!success) {
						conv->source_loc.print_error_header();
						error_print("Unable to convert from unit with standard form ", arg_units[0].to_utf8(), " to ", to_unit.to_utf8(), ".");
						fatal_error_trace(scope);
					}
				}
			}
			if(conversion_factor == 1.0) {
				result.fun = new_binary->exprs[0];
				new_binary->exprs.clear();
				delete new_binary;
			} else {
				new_binary->exprs[0] = make_cast(new_binary->exprs[0], Value_Type::real);
				new_binary->exprs.push_back(make_literal(conversion_factor));
				new_binary->value_type = new_binary->exprs[0]->value_type;
				new_binary->oper = (Token_Type)(offset ? '+' : '*');
				result.fun = new_binary;
			}
			result.unit = std::move(to_unit);
		} break;
		
		default : {
			fatal_error(Mobius_Error::internal, "Unhandled math expr type in resolve_function_tree().");
		} break;
	}
	
	if(!result.fun)
		fatal_error(Mobius_Error::internal, "Result unassigned in resolve_function_tree().");
	
	result.fun->source_loc = ast->source_loc;
	
	if(result.fun->value_type == Value_Type::unresolved) {
		ast->source_loc.print_error_header();
		fatal_error("(internal error) did not resolve value type of expression.");
	}
	
#if DEBUGGING_NOW
	warning_print("end ", name(ast->type), "\n");
#endif

	return result;
}

Math_Expr_FT *
optimize_pow_int(Math_Expr_FT *lhs, s64 p) {
	// Note: case p == 0 is handled in another call. Also for p == 1, but we also use this for powers 1.5 etc. see below
	Math_Expr_FT *result = nullptr;
	if(p == 1)
		result = lhs;
	else if(p == -1)
		result = make_binop('/', make_literal(1.0), lhs);
	else if(p >= -2 && p <=3) {
		auto scope = new Math_Block_FT();
		result = scope;
		result->value_type = Value_Type::real;
		
		auto ref = add_local_var(scope, lhs);
	
		if(p == 2)
			result->exprs.push_back(make_binop('*', ref, ref));
		else if(p == 3)
			result->exprs.push_back(make_binop('*', make_binop('*', ref, ref), ref));
		else if(p == 4) {
			auto ref2 = add_local_var(scope, make_binop('*', ref, ref));
			result->exprs.push_back(make_binop('*', ref2, ref2));
		}
		else if(p == -2)
			result->exprs.push_back(make_binop('/', make_literal(1.0), make_binop('*', ref, ref)));
	} else
		result = make_binop('^', lhs, make_literal(p));  // Fallback on powi
	
	return result;
}

Math_Expr_FT *
maybe_optimize_pow(Operator_FT *binary, Math_Expr_FT *lhs, Literal_FT *rhs) {
	Math_Expr_FT *result = binary;
	if(rhs->value_type == Value_Type::real) {
		double val = rhs->value.val_real;
		double valmh = val-0.5;
		if(val == 0.5) {
			result = make_intrinsic_function_call(Value_Type::real, "sqrt", lhs);
		} else if (valmh == (double)(s64)valmh) {
			auto rt = make_intrinsic_function_call(Value_Type::real, "sqrt", lhs);
			auto rest = optimize_pow_int(copy(lhs), (s64)valmh);   // TODO: oops, lhs should instead be a local var. Will probably be caught by optimizer though.
			result = make_binop('*', rt, rest);
		} else if(val == (double)(s64)val) {
			result = optimize_pow_int(lhs, (s64)val);
		}
	} else if (rhs->value_type == Value_Type::integer)
		result = optimize_pow_int(lhs, rhs->value.val_integer);
	
	if(result != binary) {
		result->source_loc = binary->source_loc;
		binary->exprs.clear(); // to not invoke recursive destructor on lhs.
		delete rhs;
		delete binary;
	}
	
	return result;
}

Math_Expr_FT *
replace_iteration_index(Math_Expr_FT *expr, s32 block_id) {
	if(expr->expr_type == Math_Expr_Type::identifier_chain) {
		auto ident = static_cast<Identifier_FT *>(expr);
		if(ident->variable_type == Variable_Type::local && ident->local_var.scope_id == block_id) {
			//NOTE: the for loop block has only one local variable, the iteration index, so we only have to know that it points to that block.
			delete expr;
			return make_literal((s64)0);
		}
	}
	for(auto &arg : expr->exprs) arg = replace_iteration_index(arg, block_id);
	
	return expr;
}

Math_Expr_FT *
potentially_prune_local(Math_Expr_FT *expr, Function_Scope *scope) {
	auto ident = static_cast<Identifier_FT *>(expr);
	if(ident->variable_type == Variable_Type::local) {
		if(!scope)
			fatal_error(Mobius_Error::internal, "Something went wrong with the scope of an identifier when pruning a function tree.");
		Function_Scope *sc = scope;
		while(sc->block->unique_block_id != ident->local_var.scope_id) {
			sc = sc->parent;
			if(!sc)
				fatal_error(Mobius_Error::internal, "Something went wrong with the scope of an identifier when pruning a function tree.");
		}
		Math_Block_FT *block = sc->block;
		if(!block->is_for_loop) { // If the scope was a for loop, the identifier was pointing to the iteration index, and that can't be optimized away here.
			int index = 0;
			for(auto loc : block->exprs) {
				if(loc->expr_type == Math_Expr_Type::local_var) {
					if (index == ident->local_var.index) {
						loc->exprs[0] = prune_tree(loc->exprs[0], sc); // TODO: This is not very clean, and causes double work some times, but is needed if we want to use this from constant checking in the unit checking. Should probably instead make a separate constant checking thing rather than prune_tree!
						if(loc->exprs[0]->expr_type == Math_Expr_Type::literal) {
							auto literal        = new Literal_FT();
							auto loc2           = static_cast<Local_Var_FT *>(loc);
							auto loc_literal    = static_cast<Literal_FT *>(loc->exprs[0]);
							literal->value      = loc_literal->value;
							literal->value_type = loc_literal->value_type;
							literal->source_loc   = ident->source_loc;
							loc2->is_used       = false; //   note. we can't remove the local var itself since that would invalidate other local var references, but we could just ignore it in code generation later.
							delete expr;
							return literal;
						} else if (loc->exprs[0]->expr_type == Math_Expr_Type::identifier_chain)
							return potentially_prune_local(loc->exprs[0], sc);
					}
					++index;
				}
			}
		}
	}
	return expr;
}


Math_Expr_FT *
prune_tree(Math_Expr_FT *expr, Function_Scope *scope) {
	
	// Try to simplify the math expression if some components can be evaluated to constants at compile time.
	//    Some of this could be left to llvm, but it is beneficial make the job easier for llvm, and we do see improvements from some of these optimizations.
	
	Function_Scope *old_scope = scope;
	Function_Scope new_scope;
	if(expr->expr_type == Math_Expr_Type::block) {
		new_scope.parent = scope;
		new_scope.block = static_cast<Math_Block_FT *>(expr);
		scope = &new_scope;
	}
	
	for(auto &arg : expr->exprs)
		arg = prune_tree(arg, scope);
	
	switch(expr->expr_type) {
		case Math_Expr_Type::block : {
			auto block = static_cast<Math_Block_FT *>(expr);
			if(block->is_for_loop
				&& block->exprs[0]->expr_type == Math_Expr_Type::literal
				&& static_cast<Literal_FT *>(block->exprs[0])->value.val_integer == 1) {   // Iteration count of for loop is 1, so the loop only executes once.
				
				delete block->exprs[0];
				auto result = block->exprs[1];
				result = replace_iteration_index(result, block->unique_block_id); // Replace the iteration index with constant 0 in the body.
				result = prune_tree(result, scope);  // We could maybe prune more now that more stuff could be constant.
				
				block->exprs.clear();
				delete block;          //NOTE: don't move this up, because it would screw up the linking of scope data.
				return result;
			}
			if(!block->is_for_loop && block->exprs.size() == 1) {   // A block with a single statement can be replaced with that statement.
				auto result = block->exprs[0];
				block->exprs.clear();
				delete block;
				return result;
			}
		} break;
		
		case Math_Expr_Type::function_call : {
			auto fun = static_cast<Function_Call_FT *>(expr);
			if(fun->fun_type != Function_Type::intrinsic)           //TODO: implement for others.
				return expr;
			
			bool all_literal = true;
			for(auto arg : expr->exprs) {
				if(arg->expr_type != Math_Expr_Type::literal) { all_literal = false; break; }
			}
			if(all_literal) {
				auto literal = new Literal_FT();
				literal->source_loc = expr->source_loc;
				literal->value_type = expr->value_type;
				
				if(expr->exprs.size() == 1) {
					auto arg1 = static_cast<Literal_FT *>(expr->exprs[0]);
					literal->value = apply_intrinsic({arg1->value, arg1->value_type}, fun->fun_name);
					delete expr;
					return literal;
				} else if(expr->exprs.size() == 2) {
					auto arg1 = static_cast<Literal_FT *>(expr->exprs[0]);
					auto arg2 = static_cast<Literal_FT *>(expr->exprs[1]);
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
				auto unary = static_cast<Operator_FT *>(expr);
				auto arg = static_cast<Literal_FT *>(expr->exprs[0]);
				Parameter_Value val = apply_unary({arg->value, arg->value_type}, unary->oper);
				auto literal = new Literal_FT();
				literal->value = val;
				literal->value_type = expr->value_type;
				literal->source_loc = expr->source_loc;
				delete expr;
				return literal;
			}
		} break;
		
		case Math_Expr_Type::binary_operator : {
			// TODO: we could do more, like determine if the two branches are going to evaluate to the same (or opposite) value, and then use that in case of minus etc.
			Literal_FT *lhs = nullptr;
			Literal_FT *rhs = nullptr;
			auto binary = static_cast<Operator_FT *>(expr);
			if(expr->exprs[0]->expr_type == Math_Expr_Type::literal)
				lhs = static_cast<Literal_FT *>(expr->exprs[0]);
			if(expr->exprs[1]->expr_type == Math_Expr_Type::literal)
				rhs = static_cast<Literal_FT *>(expr->exprs[1]);
			
			Typed_Value result;
			result.type = Value_Type::unresolved;
			if(lhs && rhs) {
				// If both arguments are literals, just apply the operator directly.
				result = apply_binary({lhs->value, lhs->value_type}, {rhs->value, rhs->value_type}, binary->oper);
			} else {
				// If one argument is a literal, we can still in some cases determine the result in advance.
				if(lhs)
					result = check_binop_reduction(binary->source_loc, binary->oper, lhs->value, lhs->value_type, true);
				else if(rhs)
					result = check_binop_reduction(binary->source_loc, binary->oper, rhs->value, rhs->value_type, false);
			}
			
			if(result.type != Value_Type::unresolved) {
				if(result.type == Value_Type::none) { // note: this is used by the check_binop_reduction procedure to signal to keep the other operand rather than replace it with a literal
					Math_Expr_FT *res;
					if(lhs) {
						delete expr->exprs[0];
						res =  expr->exprs[1];
					} else {
						delete expr->exprs[1];
						res =  expr->exprs[0];
					}
					expr->exprs.clear(); // to not invoke recursive destructor
					delete expr;
					return res;
				} else {
					auto literal = new Literal_FT();
					literal->value = result;
					literal->value_type = expr->value_type;
					literal->source_loc = expr->source_loc;
					delete expr;
					return literal;
				}
			} else if((char)binary->oper == '^') {
				if(rhs)
					return maybe_optimize_pow(binary, expr->exprs[0], rhs);
				else if(lhs) {
					if(lhs->value.val_real == 2.0) {
						auto result = make_intrinsic_function_call(Value_Type::real, "pow2", make_cast(expr->exprs[1], Value_Type::real));
						expr->exprs.clear();
						delete lhs;
						delete expr;
						return result;
					} else if(lhs->value.val_real > 1e-16) {   // TODO: find a good epsilon here. We should check what the floating point error resulting from the replacement is for various epsilons.
						// if lhs is a literal > 0, replace    lhs^rhs with exp(ln(lhs)*rhs). Since the ln can be computed now, this function call is faster and easier for llvm to optimize.
						//    llvm does NOT do this reduction on its own, and it can have a huge impact on model speed.
						lhs->value.val_real = std::log(lhs->value.val_real);
						auto result = make_intrinsic_function_call(Value_Type::real, "exp", make_binop('*', lhs, make_cast(expr->exprs[1], Value_Type::real)));
						expr->exprs.clear();
						delete expr;
						return result;
					}
				}
			}
		} break;
	
		case Math_Expr_Type::if_chain : {
			
			if(expr->exprs.size() % 2 != 1)
				fatal_error(Mobius_Error::internal, "Got malformed if chain in prune_tree.");
			
			std::vector<Math_Expr_FT *> remains;
			bool found_true = false;
			for(int idx = 0; idx < (int)expr->exprs.size()-1; idx+=2) {
				bool is_false = false;
				bool is_true  = false;
				if(!found_true && expr->exprs[idx+1]->expr_type == Math_Expr_Type::literal) {
					auto literal = static_cast<Literal_FT *>(expr->exprs[idx+1]);
					if(literal->value.val_boolean) is_true  = true;
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
				auto old_literal = static_cast<Literal_FT*>(expr->exprs[0]);
				auto literal = new Literal_FT();
				literal->value = apply_cast({old_literal->value, old_literal->value_type}, expr->value_type);
				literal->value_type = expr->value_type;
				literal->source_loc = expr->source_loc;
				delete expr;
				return literal;
			}
		} break;
		
		case Math_Expr_Type::identifier_chain : {
			expr = potentially_prune_local(expr, scope);
		} break;
	}
	return expr;
}

void
register_dependencies(Math_Expr_FT *expr, Dependency_Set *depends) {
	for(auto arg : expr->exprs) register_dependencies(arg, depends);
	
	if(expr->expr_type == Math_Expr_Type::identifier_chain) {
		auto ident = static_cast<Identifier_FT *>(expr);
		if(ident->variable_type == Variable_Type::parameter)
			depends->on_parameter.insert(ident->parameter);
		else if(ident->variable_type == Variable_Type::state_var) {
			State_Var_Dependency dep {State_Var_Dependency::Type::none, ident->state_var};
			if(ident->flags & Identifier_FT::Flags::last_result)
				dep.type = (State_Var_Dependency::Type)(dep.type | State_Var_Dependency::Type::earlier_step);
			
			depends->on_state_var.insert(dep);
		}
		else if(ident->variable_type == Variable_Type::series)
			depends->on_series.insert(ident->series);
	}
}

template<typename Expr_Type> Math_Expr_FT *
copy_one(Math_Expr_FT *source) {
	if(!source)
		fatal_error(Mobius_Error::internal, "Somehow we got a nullptr node in a function tree in copy().");
	auto source_full = static_cast<Expr_Type *>(source);
	auto result = new Expr_Type();
	*result = *source_full;
	return result;
}

Math_Expr_FT *
copy(Math_Expr_FT *source) {
	if(!source)
		fatal_error(Mobius_Error::internal, "Received nullptr argument to copy() (function_tree.cpp).");
	
	Math_Expr_FT *result;
	switch(source->expr_type) {
		case Math_Expr_Type::block : {
			// Hmm this means that the unique block id is not going to be actually unique. It won't matter if we don't paste the same tree as a branch of itself though. Note that we can't just generate a new id without going through all references to it further down in the tree.
			result = copy_one<Math_Block_FT>(source);
		} break;
		
		case Math_Expr_Type::identifier_chain : {
			result = copy_one<Identifier_FT>(source);
		} break;
		
		case Math_Expr_Type::literal : {
			result = copy_one<Literal_FT>(source);
		} break;
		
		case Math_Expr_Type::function_call : {
			result = copy_one<Function_Call_FT>(source);
		} break;
		
		case Math_Expr_Type::unary_operator :
		case Math_Expr_Type::binary_operator : {
			result = copy_one<Operator_FT>(source);
		} break;
		
		case Math_Expr_Type::local_var : {
			result = copy_one<Local_Var_FT>(source);
		} break;
		
		case Math_Expr_Type::if_chain :
		case Math_Expr_Type::cast :
		case Math_Expr_Type::state_var_assignment :
		case Math_Expr_Type::derivative_assignment : {
			result = copy_one<Math_Expr_FT>(source);
		} break;
		
		default : {
			fatal_error(Mobius_Error::internal, "Unhandled math expr type in copy().");
		} break;
	};

	for(int idx = 0; idx < result->exprs.size(); ++idx) {
		result->exprs[idx] = copy(result->exprs[idx]);
	}
	
	return result;
}

void
print_tabs(int ntabs) { for(int i = 0; i < ntabs; ++i) warning_print('\t'); }

void
print_tree(Math_Expr_FT *expr, int ntabs) {
	print_tabs(ntabs);
	warning_print(name(expr->expr_type));
	if(expr->expr_type == Math_Expr_Type::literal) {
		auto literal = static_cast<Literal_FT *>(expr);
		if(literal->value_type == Value_Type::real)    warning_print(" ", literal->value.val_real);
		if(literal->value_type == Value_Type::integer) warning_print(" ", literal->value.val_integer);
		if(literal->value_type == Value_Type::boolean) warning_print(" ", literal->value.val_boolean ? "true": "false");
	} else if (expr->expr_type == Math_Expr_Type::binary_operator) {
		auto binop = static_cast<Operator_FT *>(expr);
		warning_print(" ", (char)binop->oper);
	}
	warning_print("\n");
	for(auto arg : expr->exprs)
		print_tree(arg, ntabs+1);
}

/*
ugh, this is obnoxious to write :(

void
print_tree(Math_Expr_FT *expr, int ntabs) {
	
	switch(expr->expr_type) {
		case Math_Expr_Type::block : {
			auto block = static_cast<Math_Block_FT *>(expr);
			//print_tabs(ntabs);
			if(block->is_for_loop) {
				warning_print("for(i = 0..");
				print_tree(expr->exprs[0]);
				warning_print(") ");
			}
			warning_print("{\n");
			if(block->is_for_loop)
				print_tree(expr->exprs[1], ntabs + 1);
			else
				for(auto arg : expr->exprs) print_tree(arg, ntabs + 1);
			warning_print("}\n");
		} break;
		
		case Math_Expr_Type::local_var : {
			//print_tabs(ntabs);
			warning_print("var := ");
			print_tree(expr->exprs[0]);
		}
		
		case Math_Expr_Type::identifier_chain :
	}
}
*/
