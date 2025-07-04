
#include <sstream>

#include "function_tree.h"
#include "emulate.h"
#include "units.h"
#include "model_application.h"
#include "resolve_identifier.h"

void
Math_Block_FT::set_id() {
	//TODO: if we ever want to parallellize code generation, we have to make a better system here:
	static s32 id_counter = 0;
	unique_block_id = id_counter++;
}

Math_Expr_FT *
make_cast(Math_Expr_FT *expr, Value_Type cast_to) {
	if(cast_to == expr->value_type) return expr;
	
	if(expr->value_type != Value_Type::real && expr->value_type != Value_Type::integer && expr->value_type != Value_Type::boolean) {
		expr->source_loc.print_error_header();
		fatal_error("This expression does not evaluate to a value.");
	}
	
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
make_literal(bool val_bool) {
	auto literal = new Literal_FT();
	literal->value_type = Value_Type::boolean;
	literal->value.val_boolean = val_bool;
	return literal;
}

Math_Expr_FT *
make_state_var_identifier(Var_Id state_var) {
	if(!is_valid(state_var))
		fatal_error(Mobius_Error::internal, "Tried to make an identifier to an invalid Var_Id.");
	auto ident = new Identifier_FT();
	ident->value_type    = Value_Type::real;
	ident->variable_type = Variable_Type::series;
	if(!(state_var.type == Var_Id::Type::state_var || state_var.type == Var_Id::Type::temp_var || state_var.type == Var_Id::Type::series))
		fatal_error(Mobius_Error::internal, "Tried to make an identifier to something that is not supported.");
	ident->var_id        = state_var;
	return ident;
}

Math_Expr_FT *
make_parameter_identifier(Mobius_Model *model, Entity_Id par_id) {
	auto ident = new Identifier_FT();
	ident->value_type    = get_value_type(model->parameters[par_id]->decl_type);
	ident->variable_type = Variable_Type::parameter;
	ident->par_id        = par_id;
	return ident;
}

Math_Expr_FT *
make_local_var_reference(s32 id, s32 scope_id, Value_Type value_type) {
	auto ident = new Identifier_FT();
	ident->value_type    = value_type;
	ident->variable_type = Variable_Type::local;
	ident->local_var.id = id;
	ident->local_var.scope_id = scope_id;
	return ident;
}

Math_Expr_FT *
add_local_var(Math_Block_FT *scope, Math_Expr_FT *val) {
	// NOTE: this should only be called on a block that is under construction.
	auto local = new Local_Var_FT();
	local->exprs.push_back(val);
	local->value_type = Value_Type::none;//val->value_type;
	local->is_used = true;
	s32 id = scope->n_locals;
	local->id = id;
	std::stringstream ss;
	ss << "_gen_" << scope->unique_block_id << '_' << id;
	local->name = ss.str();
	scope->exprs.push_back(local);
	++scope->n_locals;
	return make_local_var_reference(id, scope->unique_block_id, val->value_type);
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
	return (op == Token_Type::leq || op == Token_Type::geq || op == Token_Type::neq || c == '=' || c == '&' || c == '|' || c == '<' || c == '>' || c == '!');
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

Math_Expr_FT *
make_unary(char oper, Math_Expr_FT *arg) {
	auto unary = new Operator_FT(Math_Expr_Type::unary_operator);
	bool is_bool = is_boolean_operator((Token_Type)oper);
	if(is_bool)
		arg = make_cast(arg, Value_Type::boolean);
	unary->value_type = arg->value_type;
	unary->oper = (Token_Type)oper;
	unary->exprs.push_back(arg);
	return unary;
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
	auto if_expr = new Math_Expr_FT(Math_Expr_Type::if_chain);
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
	auto if_expr = make_simple_if(copy(res_ref), cond, make_literal((double)0.0));
	block->exprs.push_back(if_expr);
	return block;
}

Math_Expr_FT *
make_clamp(Math_Expr_FT *var, Math_Expr_FT *low, Math_Expr_FT *high) {
	/*
	Equivalent to
		v := var,
		l := low,
		h := high,
		l if v < l,
		h if v > h,
		v otherwise
	*/
	auto block = new Math_Block_FT();
	block->value_type = var->value_type;
	auto var_ref = add_local_var(block, var);
	auto low_ref = add_local_var(block, low);
	auto high_ref = add_local_var(block, high);
	auto cond1 = make_binop('<', var_ref, low_ref);
	auto cond2 = make_binop('>', copy(var_ref), high_ref);
	auto if_expr = new Math_Expr_FT(Math_Expr_Type::if_chain);
	if_expr->value_type = var->value_type;
	if_expr->exprs.push_back(copy(low_ref));
	if_expr->exprs.push_back(cond1);
	if_expr->exprs.push_back(copy(high_ref));
	if_expr->exprs.push_back(cond2);
	if_expr->exprs.push_back(copy(var_ref));
	block->exprs.push_back(if_expr);
	return block;
}

Math_Expr_FT *
make_no_op() {
	auto no_op = new Math_Expr_FT(Math_Expr_Type::no_op);
	no_op->value_type = Value_Type::none;
	return no_op;
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
				fun->value_type = Value_Type::ret_type; \
			} \
		}
	#include "intrinsics.incl"
	#undef MAKE_INTRINSIC1
	#undef MAKE_INTRINSIC2
	else {
		fatal_error(Mobius_Error::internal, "Unhandled intrinsic \"", name, "\" in fixup_intrinsic().");
	}
}

Tuple_FT *
find_tuple(Math_Expr_FT *tuple) {
	if(tuple->expr_type == Math_Expr_Type::block)
		return find_tuple(tuple->exprs.back());
	else if(tuple->expr_type == Math_Expr_Type::tuple)
		return static_cast<Tuple_FT *>(tuple);
	tuple->source_loc.print_error_header();
	fatal_error(Mobius_Error::internal, "Unable to find tuple referenced by value.");
	return nullptr;
}

void
resolve_add_expr(Math_Expr_FT *parent, Math_Expr_FT *child, Standardized_Unit &unit, Function_Scope *scope, std::vector<Standardized_Unit> &units) {
	
	// If we get an "unpack tuple" we desugar it to a local var holding the tuple and other local vars accessing the elements of that tuple.
	// TODO: Move this to a separate function to make it cleaner?
	if(child->expr_type == Math_Expr_Type::unpack_tuple) {
		// TODO: Assert the types are correct?
		auto unpack = static_cast<Unpack_Tuple_FT *>(child);
		auto tuple = find_tuple(unpack->exprs[0]);
		
		if(unpack->names.size() != tuple->exprs.size()) {
			unpack->source_loc.print_error_header();
			fatal_error("Incorrect number of elements in tuple unpacking. Expected ", tuple->exprs.size(), ".");
		}
		
		auto tuple_local = new Local_Var_FT();
		tuple_local->name = "_tuple_"; // This should only be relevant for debug printing. Maybe make a better name??
		tuple_local->source_loc = unpack->source_loc;
		tuple_local->value_type = Value_Type::none;//Value_Type::tuple;
		tuple_local->is_used = true; // Protect it from being removed.
		tuple_local->exprs.push_back(unpack->exprs[0]);
		
		Standardized_Unit no_unit = {};
		resolve_add_expr(parent, tuple_local, no_unit, scope, units);
		// The id of the tuple local should now have been resolved by the nested call to resolve_add_expr above.
		Local_Var_Id tuple_id = { scope->block->unique_block_id, tuple_local->id };
		
		for(int idx = 0; idx < tuple->exprs.size(); ++idx) {
			auto local = new Local_Var_FT();
			auto access = new Access_Tuple_Element_FT();
			
			access->source_loc = unpack->source_loc;
			access->value_type = tuple->exprs[idx]->value_type;
			access->element_index = idx;
			access->tuple_id = tuple_id;
			
			local->source_loc = unpack->source_loc;
			local->value_type = Value_Type::none;//access->value_type;
			local->name = unpack->names[idx];
			local->exprs.push_back(access);
			
			resolve_add_expr(parent, local, tuple->element_units[idx], scope, units);
		}
		
		unpack->exprs.clear(); // So that the tuple itself is not deleted recursively.
		delete unpack; // This one is not used in itself.
		
		return;
	}
	
	parent->exprs.push_back(child);
	if(child->expr_type == Math_Expr_Type::local_var) {
		auto local = static_cast<Local_Var_FT *>(child);
		local->id = scope->block->n_locals++;
		scope->local_var_units[local->id] = unit;
	}
	units.push_back(unit);
}

void
resolve_arguments(Math_Expr_FT *ft, Math_Expr_AST *ast, Function_Resolve_Data *data, Function_Scope *scope, std::vector<Standardized_Unit> &units) {
	//TODO allow error check on expected number of arguments
	for(auto arg : ast->exprs) {
		auto result = resolve_function_tree(arg, data, scope);
		
		resolve_add_expr(ft, result.fun, result.unit, scope, units);
	}
}

s32
find_tagged_scope(const std::string &iter_tag, Function_Scope *scope) {
	if(!scope) return -1;
	
	auto block = scope->block;
	if(block->iter_tag == iter_tag)
		return block->unique_block_id;
	if(scope->function_name.empty()) // Can't reference iteration scopes through function calls.
		return find_tagged_scope(iter_tag, scope->parent);
	return -1;
}

bool
find_local_variable(Identifier_FT *ident, Standardized_Unit &unit, const std::string &name, Function_Scope *scope, bool mark_as_reassignable = false) {
	if(!scope) return false;
	
	auto block = scope->block;
	if(!block->is_for_loop) {
		for(auto expr : block->exprs) {
			if(expr->expr_type == Math_Expr_Type::local_var) {
				auto local = static_cast<Local_Var_FT *>(expr);
				if(local->name == name) {
					ident->variable_type = Variable_Type::local;
					ident->local_var.id = local->id;
					ident->local_var.scope_id = block->unique_block_id;
					ident->value_type = local->exprs[0]->value_type;
					local->is_used = true;
					unit = scope->local_var_units[local->id];
					if(mark_as_reassignable) { // Arguably, in this case it should not be marked as used, but not doing it could complicate things if the assignment is a complicated expression.
						local->is_reassignable = true;
						//log_print("Marked as reassignable\n");
					}
					return true;
				}
			}
		}
	}
	if(scope->function_name.empty())  //NOTE: scopes should not "bleed through" function substitutions.
		return find_local_variable(ident, unit, name, scope->parent, mark_as_reassignable);
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

void
set_identifier_location(Function_Resolve_Data *data, Standardized_Unit &unit, Identifier_FT *ident, Var_Id var_id, std::vector<Token> &chain, Function_Scope *scope) {
	Source_Location sl = chain[0].source_loc;
	if(!is_valid(var_id)) {
		sl.print_error_header();
		error_print("The identifier\n");
		int idx = 0;
		for(Token &token : chain)
			error_print(token.string_value, idx++ == chain.size()-1 ? "" : ".");
		error_print("\ncan not be inferred as a valid state variable that has been created using a 'var' declaration. ");
		if(is_located(data->in_loc)) {
			error_print("It was being resolved in the following context: ");
			error_print_location(data->app->model, data->in_loc);
		} else
			error_print("The location can not be inferred from the context.");
		fatal_error_trace(scope);
	}
	ident->variable_type = Variable_Type::series;
	ident->var_id        = var_id;
	unit                 = data->app->vars[var_id]->unit.standard_form;
	ident->value_type    = Value_Type::real;
}

Math_Expr_FT *
fixup_potentially_baked_value(Model_Application *app, Math_Expr_FT *expr, std::vector<Entity_Id> *baked_parameters) {
	if(!baked_parameters || expr->expr_type != Math_Expr_Type::identifier) return expr;
	
	auto ident = static_cast<Identifier_FT *>(expr);
	if(ident->variable_type != Variable_Type::parameter) return expr; //TODO: Could maybe eventually bake others.
	auto par_id = ident->par_id;
	if(std::find(baked_parameters->begin(), baked_parameters->end(), par_id) == baked_parameters->end()) return expr;
	
	// NOTE: currently we only do this for single-instanced parameters.
	s64 offset = app->parameter_structure.get_offset_base(par_id);
	Parameter_Value val = *app->data.parameters.get_value(offset);
	
	auto literal = new Literal_FT();
	literal->value_type = ident->value_type;
	literal->source_loc   = ident->source_loc;
	literal->value      = val;
	
	//warning_print("Baking ", app->model->parameters[par_id]->name, "\n");
	
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
		
		case Variable_Type::time_fractional_step : {
			unit = {};
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
is_constant_zero(Math_Expr_FT *expr, Function_Scope *scope) {
	bool found;
	auto value = is_constant_rational(expr, scope, &found);
	
	return found && (value == Rational<s64>(0));
}

void
apply_binop_to_units(Token_Type oper, const std::string &name, Standardized_Unit &result, Standardized_Unit &a, Standardized_Unit &b, Source_Location &oper_loc, Function_Scope *scope, Math_Expr_FT *lhs, Math_Expr_FT *rhs) {
	char op = (char)oper;
	if(op == '|' || op == '&') {
		check_boolean_dimensionless(a, lhs->source_loc, scope);
		check_boolean_dimensionless(b, rhs->source_loc, scope);
	} else if (op == '<' || op == '>' || oper == Token_Type::geq || oper == Token_Type::leq || op == '=' || oper == Token_Type::neq || op == '+' || op == '-' || op == '%') {
		// TODO: If one of the sides resolves to a constant 0, that should always match (but that may involve pruning first).
		bool lhs_is_0 = false;
		if(!match_exact(&a, &b)) {
			lhs_is_0 = is_constant_zero(lhs, scope);
			bool rhs_is_0 = is_constant_zero(rhs, scope);
			if(!lhs_is_0 && !rhs_is_0) {
				// NOTE: If either side is a constant 0, we allow the match.
				oper_loc.print_error_header();
				error_print("The units of the two arguments to ", name, " must be the same. The standard form of the units given are ", a.to_utf8(), " and ", b.to_utf8(), ". Don't worry, this happens to everybody!");
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
		bool is_const;
		auto val = is_constant_rational(rhs, scope, &is_const);
		if(b.is_fully_dimensionless() && a.is_fully_dimensionless()) {
			// Do nothing, the unit should remain dimensionless.
		} else if(is_const) {
			bool success = pow(a, result, Rational<s16>((s16)val.nom, (s16)val.denom));  //TODO: Ooops, this could cause truncation!
			if(!success) {
				rhs->source_loc.print_error_header();
				error_print("Unable to raise the unit ", a.to_utf8(), " to the power ", val, " .");
			}
		} else {
			lhs->source_loc.print_error_header();
			error_print("In a power expression ^, to resolve the units, either both sides must be dimensionless, or the right hand side must be a dimensionless constant.");
			fatal_error_trace(scope);
		}
	} else
		fatal_error(Mobius_Error::internal, "Unhandled binary operator ", name, " in apply_binop_to_units.");
}

void
check_if_expr_units(Standardized_Unit &result, std::vector<Standardized_Unit> &units, std::vector<Math_Expr_FT *> &exprs, Function_Scope *scope) {
	// Check the units of an 'if' expression.
	
	// Check conditions:
	for(int idx = 1; idx < units.size(); idx+=2)
		check_boolean_dimensionless(units[idx], exprs[idx]->source_loc, scope);
	// Check values:
	Standardized_Unit *first_valid = nullptr;
	for(int idx = 0; idx < units.size(); idx+=2) {
		s64 val = -1;
		if(exprs[idx]->value_type == Value_Type::iterate) continue; // An 'iterate' doesn't evaluate to anything.
		bool is_0 = is_constant_zero(exprs[idx], scope);
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

void
arguments_must_be_values(Math_Expr_FT *expr, Function_Scope *scope) {
	for(auto arg : expr->exprs) {
		if(!is_value(arg->value_type)) {
			arg->source_loc.print_error_header();
			error_print("This expression argument must resolve to a value, not '", name(arg->value_type), "'.");
			fatal_error_trace(scope);
		}
	}
}

enum class Directive {
	none = 0,
	#define ENUM_VALUE(name) name,
	#include "special_directives.incl"
	#undef ENUM_VALUE
};

Directive
get_special_directive(String_View name) {
	Directive result = Directive::none;
	if(false) {}
	#define ENUM_VALUE(h) else if(name == #h) result = Directive::h;
	#include "special_directives.incl"
	#undef ENUM_VALUE
	
	return result;
}

void
resolve_special_directive(Function_Call_AST *ast, Directive directive, Function_Resolve_Data *data, Function_Scope *scope, Function_Resolve_Result &result) {
	
	auto fun_name = ast->name.string_value;
	
	if(data->simplified) {
		ast->source_loc.print_error_header();
		error_print("The expression '", fun_name, "' is not available in this context.");
		fatal_error_trace(scope);
	}
	auto new_fun = new Function_Call_FT(); // Hmm it is a bit annoying to have to do this only to delete it again.
	new_fun->source_loc = ast->source_loc;
	
	std::vector<Standardized_Unit> arg_units;
	resolve_arguments(new_fun, ast, data, scope, arg_units);
	
	int allowed_arg_count = 1;
	if(directive == Directive::in_flux || directive == Directive::out_flux)
		allowed_arg_count = 2;
	int arg_count = new_fun->exprs.size();
	if(arg_count == 0 || arg_count > allowed_arg_count) {
		ast->source_loc.print_error_header();
		error_print("A ", fun_name, "() declaration only takes one argument.\n");
		fatal_error_trace(scope);
	}
	int var_idx = arg_count == 1 ? 0 : 1;
	if(new_fun->exprs[var_idx]->expr_type != Math_Expr_Type::identifier) {
		new_fun->exprs[var_idx]->source_loc.print_error_header();
		error_print("A ", fun_name, "() declaration can only be applied to a state variable or input series.\n");
		fatal_error_trace(scope);
	}
	auto ident = static_cast<Identifier_FT *>(new_fun->exprs[var_idx]);
	bool can_series = (directive != Directive::in_flux) && (directive != Directive::out_flux) && (directive != Directive::conc);
	bool can_param  = (directive == Directive::aggregate);
	
	if(!(ident->is_computed_series() || (can_series && ident->is_input_series()) || (can_param && ident->variable_type == Variable_Type::parameter))) {
		ident->source_loc.print_error_header();
		error_print("A '", fun_name, "' declaration can only be applied to a state variable");
		if(can_series) error_print(" or input series");
		if(can_param)  error_print(" or parameter");
		error_print(".\n");
		fatal_error_trace(scope);
	}
	if(directive == Directive::last)
		ident->set_flag(Identifier_FT::last_result);
	else if(directive == Directive::in_flux)
		ident->set_flag(Identifier_FT::in_flux);
	else if(directive == Directive::out_flux)
		ident->set_flag(Identifier_FT::out_flux);
	else if(directive == Directive::aggregate)
		ident->set_flag(Identifier_FT::aggregate);
	else if(directive == Directive::result)
		ident->set_flag(Identifier_FT::result);
	else if(directive == Directive::conc) {
		//Do nothing, we can solve it directly.
	}
	
	if (directive == Directive::last) {
		if (!data->allow_last) {
			new_fun->source_loc.print_error_header();
			error_print("A 'last' is not allowed in this context.");
			fatal_error_trace(scope);
		}
		if (ident->var_id.type == Var_Id::Type::temp_var) {
			// TODO: We should check the var declaration, not the variable type, because otherwise the rule is not enforced if @no_store is overridden.
			new_fun->source_loc.print_error_header();
			error_print("Can't apply 'last' to this variable since it is @no_store.");
			fatal_error_trace(scope);
		}
	}
	
	if(directive == Directive::result && !data->allow_result) {
		new_fun->source_loc.print_error_header();
		error_print("A 'result' is now allowed in this context.");
		fatal_error_trace(scope);
	}
	
	if(directive == Directive::in_flux || directive == Directive::out_flux) {
		
		if(!data->allow_in_flux) {
			new_fun->source_loc.print_error_header();
			error_print("An 'in_flux' or 'out_flux' is not allowed in this context.");
			fatal_error_trace(scope);
		}
		
		if(arg_count == 2) {
			bool error = false;
			if(new_fun->exprs[0]->expr_type != Math_Expr_Type::identifier) error = true;
			else {
				auto ident2 = static_cast<Identifier_FT *>(new_fun->exprs[0]);
				if(ident2->variable_type != Variable_Type::connection) error = true;
				else {
					ident->other_connection = ident2->other_connection; 
					delete ident2;
				}
			}
			if(error) {
				new_fun->exprs[0]->source_loc.print_error_header();
				error_print("Expected a connection identifer.\n");
				fatal_error_trace(scope);
			}
		}
	}
	
	new_fun->exprs.clear();
	delete new_fun;
	
	result.fun = ident;
	if(directive == Directive::in_flux || directive == Directive::out_flux) {
		result.unit = multiply(arg_units[var_idx], data->app->time_step_unit.standard_form, -1);
	} else if(directive == Directive::conc) {
		auto var = as<State_Var::Type::declared>(data->app->vars[ident->var_id]);
		auto conc_id = var->conc;
		if(!is_valid(conc_id)) {
			ident->source_loc.print_error_header(Mobius_Error::model_building);
			error_print("This variable does not have a concentration.");
			fatal_error_trace(scope);
		}
		ident->var_id = conc_id;
		
		result.unit = data->app->vars[conc_id]->unit.standard_form;
	} else
		result.unit = std::move(arg_units[var_idx]);
}

void
resolve_tuple(Function_Call_AST *ast, Function_Resolve_Data *data, Function_Scope *scope, Function_Resolve_Result &result) {
	
	auto new_tuple = new Tuple_FT();
	new_tuple->source_loc = ast->source_loc;
	new_tuple->value_type = Value_Type::tuple;
	resolve_arguments(new_tuple, ast, data, scope, new_tuple->element_units);
	
	arguments_must_be_values(new_tuple, scope);
	
	result.fun = new_tuple;
}

void
resolve_function_call(Function_Call_AST *fun, Function_Resolve_Data *data, Function_Scope *scope, Function_Resolve_Result &result) {
	
	auto model = data->app->model;
	auto fun_name = fun->name.string_value;
	
	auto reg = (*data->scope)[fun_name];
	if(!reg || reg->id.reg_type != Reg_Type::function) {
		fun->name.print_error_header();
		error_print("The identifier '", fun_name, "' does not refer to a function.\n");
		fatal_error_trace(scope);
	}
	auto fun_decl = model->functions[reg->id];
	auto fun_type = fun_decl->fun_type;
	
	//TODO: should be replaced with check in resolve_arguments
	if(fun->exprs.size() != fun_decl->args.size()) {
		fun->name.print_error_header();
		error_print("Wrong number of arguments to function '", fun_name, "'. Expected ", fun_decl->args.size(), ", got ", fun->exprs.size(), ".\n");
		fatal_error_trace(scope);
	}
	
	if(fun_type == Function_Type::intrinsic) {
		// Represents a function that is directly implemented in llvm, either as an llvm intrinsic or cstdlib.
		auto new_fun = new Function_Call_FT();
		
		std::vector<Standardized_Unit> arg_units;
		resolve_arguments(new_fun, fun, data, scope, arg_units);
		
		arguments_must_be_values(new_fun, scope);
		
		new_fun->fun_type = fun_type;
		new_fun->fun_name = fun_name;
		fixup_intrinsic(new_fun, &fun->name);
		
		result.fun = new_fun;
		if(arg_units.size() == 1) {
			set_intrinsic_unit(result.unit, arg_units[0], fun_name, fun->source_loc, scope);
		} else if (arg_units.size() == 2) {
			if(fun_name == "min" || fun_name == "max" || fun_name == "uniform_real" || fun_name == "normal" || fun_name == "uniform_int") {
				// NOTE: The min and max functions behave like '+' when it comes to units
				apply_binop_to_units((Token_Type)'+', fun_name, result.unit, arg_units[0], arg_units[1], fun->source_loc, scope, new_fun->exprs[0], new_fun->exprs[1]);
			} else if(fun_name == "copysign") {
				result.unit = arg_units[0];
			} else
				fatal_error(Mobius_Error::internal, "Unimplemented unit checking for intrinsic ", fun_name, ".");
		} else
			fatal_error(Mobius_Error::internal, "Unhandled number of arguments to intrinsic when unit checking");
	} else if(fun_type == Function_Type::linked) {
		// A function that is implemented in C++ and linked into the code.
		
		auto new_fun = new Function_Call_FT();
		std::vector<Standardized_Unit> arg_units;
		resolve_arguments(new_fun, fun, data, scope, arg_units);
		
		new_fun->fun_type = fun_type;
		new_fun->fun_name = fun_name;
		// TODO: If we make anything else of this than just the "_test_fun_" function, the types and units must be provided in the declaration.
		new_fun->value_type = Value_Type::real;
		result.fun = new_fun;
		// result.unit remains dimensionless.
	} else if(fun_type == Function_Type::decl) {
		// A function that is implemented and compiled in the Mobius2 language.
		
		if(is_inside_function(scope, fun_name)) {
			fun->name.print_error_header();
			error_print("The function \"", fun_name, "\" calls itself either directly or indirectly. This is not allowed.\n");
			fatal_error_trace(scope);
		}
		// Inline in the function call as a new block with the arguments as local vars.
		auto inlined_fun = new Math_Block_FT();
		inlined_fun->source_loc = fun->source_loc; //NOTE: do this to get correct diagnostics in fatal_error_trace()
		
		std::vector<Standardized_Unit> arg_units;
		resolve_arguments(inlined_fun, fun, data, scope, arg_units);
		
		arguments_must_be_values(inlined_fun, scope);
		
		inlined_fun->n_locals = inlined_fun->exprs.size();
		
		Function_Scope new_scope;
		new_scope.parent = scope;
		new_scope.block = inlined_fun;
		new_scope.function_name = fun_name;
		
		for(int argidx = 0; argidx < inlined_fun->exprs.size(); ++argidx) {
			auto arg = inlined_fun->exprs[argidx];
			
			if(!is_value(arg->value_type)) {
				arg->source_loc.print_error_header();
				error_print("The arguments to a function must resolve to a value.");
				fatal_error_trace(scope);
			}
			
			auto inlined_arg = new Local_Var_FT();
			inlined_arg->exprs.push_back(arg);
			inlined_arg->name = fun_decl->args[argidx];
			inlined_arg->value_type = Value_Type::none;//arg->value_type;
			inlined_fun->exprs[argidx] = inlined_arg;
			inlined_arg->id = argidx;
			new_scope.local_var_units[argidx] = arg_units[argidx];
			
		
			auto &argg = inlined_arg->exprs[0];
			auto loc = inlined_arg->exprs[0]->source_loc;
			if(is_valid(fun_decl->expected_units[argidx])) {
				auto &expect_unit = model->units[fun_decl->expected_units[argidx]]->data.standard_form;
				if(is_constant_zero(argg, scope)) continue;  // Constant 0 match against any unit
				if(!match_exact(&expect_unit, &arg_units[argidx])) {
					loc.print_error_header();
					error_print("The function declaration requires a unit (which on standard form is) ", expect_unit.to_utf8(), " for argument ", argidx, ", but we got ", arg_units[argidx].to_utf8(), ". See declaration of function here:\n");
					fun_decl->source_loc.print_error();
					fatal_error_trace(scope);
				}
			}
		}
		
		Function_Resolve_Data sub_data = *data;
		sub_data.scope = model->get_scope(fun_decl->scope_id);  // Resolve the function body in the scope it was declared in.
		
		auto res = resolve_function_tree(fun_decl->code, &sub_data, &new_scope);
		inlined_fun->exprs.push_back(res.fun);
		inlined_fun->value_type = inlined_fun->exprs.back()->value_type; // The value type is whatever the body of the function resolves to given these arguments.
		
		result.fun = inlined_fun;
		result.unit = std::move(res.unit);
	} else
		fatal_error(Mobius_Error::internal, "Unhandled function type.");
}


void
resolve_identifier(Identifier_Chain_AST *ident, Function_Resolve_Data *data, Function_Scope *scope, Function_Resolve_Result &result) {
	
	auto app = data->app;
	auto model = app->model;
	
	auto new_ident = new Identifier_FT();
	result.fun = new_ident;
	
	int chain_size = ident->chain.size();
	
	bool isfun = is_inside_function(scope);
	
	std::string n1 = ident->chain[0].string_value;
	
	if(data->simplified) {
		// For when we use this to only compute simple formulas of given symbols.
		if(chain_size != 1) {
			ident->chain[0].print_error_header();
			fatal_error("Unable to resolve expressions with .'s in this context.");
		}
		bool found = false;
		for(int idx = 0; idx < data->simplified_syms.size(); ++idx) {
			if(data->simplified_syms[idx] == n1) {
				found = true;
				new_ident->exprs.push_back(make_literal((s64)idx));
				new_ident->value_type = Value_Type::real;
				new_ident->variable_type = Variable_Type::parameter; // Could maybe have a separate variable type for this.
				break;
			}
		}
		// TODO: Should we allow accessing global constants in this case?
		if(!found) {
			ident->chain[0].print_error_header();
			fatal_error("Unable to resolve symbol '", n1, "'.");
		}
		return;
	}
	
	if(chain_size == 1) {
		bool found = find_local_variable(new_ident, result.unit, n1, scope);
		if(found) return;
	}
	
	bool error = false;
	Location_Resolve resolve;
	resolve_full_location(model, resolve, ident->chain, ident->bracketed_chain, {}, data->scope, &error, data->connection);
	if(error) fatal_error_trace(scope);
	
	new_ident->restriction   = resolve.restriction;
	new_ident->variable_type = resolve.type;
	
	if (resolve.type == Variable_Type::constant) {
		delete new_ident; // A bit of a stupid way to do it, but then we don't have to construct it separately in all the other instances.
		auto const_decl = model->constants[resolve.val_id];
		if(const_decl->value_type == Value_Type::real)
			result.fun  = make_literal(const_decl->value.val_real);
		else if(const_decl->value_type == Value_Type::boolean)
			result.fun  = make_literal((bool)const_decl->value.val_boolean);
		else
			fatal_error(Mobius_Error::internal, "Unimplemented value type for constant.");
		if(is_valid(const_decl->unit))
			result.unit = model->units[const_decl->unit]->data.standard_form;
		return;
	}
	
	if(isfun) {
		ident->chain[0].print_error_header();
		error_print("The name '", n1, "' is not the name of a function argument or a local variable. Note that parameters and state variables can not be accessed inside functions directly, but have to be passed as arguments.\n");
		fatal_error_trace(scope);
	}
	
	if (resolve.type == Variable_Type::no_override) {
		
		if(!data->allow_no_override || isfun || data->simplified) {
			ident->source_loc.print_error_header();
			error_print("A 'no_override' is not allowed in this context.");
			fatal_error_trace(scope);
		}
		new_ident->value_type = Value_Type::real;
		result.unit = data->expected_unit;
		return;
		
	} else if (resolve.type == Variable_Type::is_at) {
		
		new_ident->variable_type = Variable_Type::is_at;
		new_ident->value_type = Value_Type::boolean;
		return;
		
	} else if (resolve.type == Variable_Type::parameter) {
		
		auto par = model->parameters[resolve.val_id];
				
		if(par->decl_type == Decl_Type::par_enum) {
			
			if(ident->chain.size() != 2) {
				ident->source_loc.print_error_header();
				error_print("Enum parameters must be referenced as par.value .");
			}
			auto n2 = ident->chain[1].string_value;
			s64 val = par->enum_int_value(n2);
			if(val < 0) {
				ident->chain[1].print_error_header();
				error_print("The name \"", n2, "\" was not registered as a possible value for the parameter \"", n1, "\".\n");
				fatal_error_trace(scope);
			}
			new_ident->variable_type = Variable_Type::parameter;
			new_ident->par_id = resolve.val_id;
			new_ident->value_type = Value_Type::integer;
			auto ft = fixup_potentially_baked_value(app, new_ident, data->baked_parameters);
			result.fun = make_binop('=', ft, make_literal(val));
			
		} else if (par->decl_type == Decl_Type::par_datetime) {
			ident->source_loc.print_error_header();
			error_print("It is currently not supported to reference datetime parameters.\n");
			fatal_error_trace(scope);
		} else {
			new_ident->variable_type = Variable_Type::parameter;
			new_ident->par_id = resolve.val_id;
			new_ident->value_type = get_value_type(par->decl_type);
			if(is_valid(par->unit)) // For e.g. boolean it will not have a unit, which means dimensionless, but that is the default of the result, so we don't need to set it.
				result.unit = model->units[par->unit]->data.standard_form;
			
			// TODO: If we do fixups for other values than parameters we have to remember to call it separately for those.
			result.fun = fixup_potentially_baked_value(app, new_ident, data->baked_parameters);
		}
		
		return;
		
	} else if (resolve.type == Variable_Type::connection) {
		
		new_ident->value_type = Value_Type::none;
		new_ident->other_connection = resolve.val_id;
		return;
		
	} else if (n1 == "time") {
		
		new_ident->value_type = Value_Type::integer;
		if(resolve.type == Variable_Type::time_fractional_step)
			new_ident->value_type = Value_Type::real;
			
		set_time_unit(result.unit, app, new_ident->variable_type);
		
		if(new_ident->variable_type == Variable_Type::time_step_length_in_seconds && app->time_step_size.unit == Time_Step_Size::second) {
			// If the step size unit is 'second', the step size is constant and can be inlined as a literal (if it is measured in months is is not).
			auto new_literal = new Literal_FT();
			new_literal->value_type = new_ident->value_type;
			new_literal->value.val_integer = app->time_step_size.multiplier;
			result.fun = new_literal;
			delete new_ident;
		}
		return;
		
	}
	
	if(resolve.type != Variable_Type::series)
		fatal_error(Mobius_Error::internal, "Unhandled variable type in resolution of identifier in function scope.");
	
	if(!is_located(resolve.loc)) {
		ident->source_loc.print_error_header();
		error_print("You can't reference 'out' as a value.");
		fatal_error_trace(scope);
	}
	
	Var_Id var_id = invalid_var;
	// If it looks well-formed (begins with a compartment), try it directly.
	if(model->components[resolve.loc.components[0]]->decl_type == Decl_Type::compartment) {
		var_id = app->vars.id_of(resolve.loc);
	}
	
	// Otherwise, try to locate it relatively to the context (if the context is set).
	if(!is_valid(var_id) && (data->in_loc.type == Var_Location::Type::located)) {
		Var_Location try_loc = data->in_loc;
		int insert_pos = try_loc.n_components;
		for(; insert_pos >= 0; --insert_pos) {
			int try_size = insert_pos + resolve.loc.n_components;
			if(try_size > max_var_loc_components)
				continue;
			
			for(int idx = 0; idx < resolve.loc.n_components; ++idx)
				try_loc.components[idx + insert_pos] = resolve.loc.components[idx];
			try_loc.n_components = try_size;
			
			var_id = app->vars.id_of(try_loc);
			if(is_valid(var_id))
				break;
		}
	}
	set_identifier_location(data, result.unit, new_ident, var_id, ident->chain, scope);
}

Function_Resolve_Result
resolve_function_tree(Math_Expr_AST *ast, Function_Resolve_Data *data, Function_Scope *scope) {
	
	// This takes a math expression AST and turns it into a more convenient internal representation (called function tree). It also
	//   - Resolves all identifiers and ties them to specific model Entity_Ids or Var_Ids (or local variable declarations).
	//   - Does type checking / resolution.
	//   - Does unit checking / resolution and implements unit conversions.
	//   - Inlines function calls if applicable.
	
	Function_Resolve_Result result = { nullptr, {}};
	
	Decl_Scope &decl_scope = *data->scope;
	
	auto app   = data->app;
	auto model = data->app->model;
	
	switch(ast->type) {
		
		case Math_Expr_Type::block : {
			auto block = static_cast<Math_Block_AST *>(ast);
			
			auto new_block = new Math_Block_FT();
			Function_Scope new_scope;
			new_scope.parent = scope;
			new_scope.block = new_block;
			
			if(is_valid(&block->iter_tag))
				new_block->iter_tag = block->iter_tag.string_value;
			
			std::vector<Standardized_Unit> arg_units;
			resolve_arguments(new_block, ast, data, &new_scope, arg_units);
			
			// The value of a block is the value of the last expression in the block.
			Math_Expr_FT *last = new_block->exprs.back();
			if(last->value_type == Value_Type::none) {
				last->source_loc.print_error_header();
				error_print("The last statement in a block must evaluate to a value.\n");
				fatal_error_trace(scope);
			}
			new_block->value_type = last->value_type;
			
			if(data->value_last_only) {
				for(int idx = 0; idx < (int)new_block->exprs.size()-1; ++idx) {
					auto expr = new_block->exprs[idx];
					if(expr->value_type != Value_Type::none) {
						expr->source_loc.print_error_header();
						error_print("This statement is not the last in its block, but it still resolves to a value. This is meaningless since that value would be discarded.");
						fatal_error_trace(scope);
					}
				}
			}
			
			// TODO: Better message. This also applies if the 'iterate' value type passes up through several blocks.
			for(int idx = 0; idx < (int)new_block->exprs.size()-1; ++idx) {
				if(new_block->exprs[idx]->value_type == Value_Type::iterate) {
					new_block->exprs[idx]->source_loc.print_error_header();
					error_print("An 'iterate' expression must be the last in a block.");
					fatal_error_trace(scope);
				}
			}
			
			result = {new_block, std::move(arg_units.back())};
		} break;
		
		case Math_Expr_Type::identifier : {
			auto ident = static_cast<Identifier_Chain_AST *>(ast);
			
			resolve_identifier(ident, data, scope, result);
			
			if(data->restrictive_lookups && result.fun->expr_type == Math_Expr_Type::identifier) {
				auto new_ident = static_cast<Identifier_FT *>(result.fun);
				if(new_ident->variable_type != Variable_Type::local && new_ident->variable_type != Variable_Type::parameter) {
					ident->source_loc.print_error_header();
					error_print("Only parameters and constants can be referenced in this context, not dynamic variables.");
					fatal_error_trace(scope);
				}
			}
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
			
			// First check for "special" calls that are not really function calls, like last(), in_flux(), etc.
			auto directive = get_special_directive(fun->name.string_value);
			if(directive != Directive::none)
				resolve_special_directive(fun, directive, data, scope, result);
			else if(fun->name.string_value == "tuple")
				resolve_tuple(fun, data, scope, result);
			else
				resolve_function_call(fun, data, scope, result);
			
		} break;
		
		case Math_Expr_Type::unary_operator : {
			auto unary = static_cast<Unary_Operator_AST *>(ast);
			auto new_unary = new Operator_FT(Math_Expr_Type::unary_operator);
			
			std::vector<Standardized_Unit> arg_units;
			resolve_arguments(new_unary, ast, data, scope, arg_units);
			
			arguments_must_be_values(new_unary, scope);
			
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
			
			arguments_must_be_values(new_binary, scope);
			
			new_binary->oper = binary->oper;
			char op = (char)binary->oper;
			
			if(op == '|' || op == '&') {
				new_binary->exprs[0] = make_cast(new_binary->exprs[0], Value_Type::boolean);
				new_binary->exprs[1] = make_cast(new_binary->exprs[1], Value_Type::boolean);
				new_binary->value_type = Value_Type::boolean;
			} else if (op == '/') {
				new_binary->exprs[0] = make_cast(new_binary->exprs[0], Value_Type::real);
				new_binary->exprs[1] = make_cast(new_binary->exprs[1], Value_Type::real);
				new_binary->value_type = Value_Type::real;
			} else if (op == '^') {
				//Note: we could implement pow for lhs of type int too, but llvm does not have an intrinsic for it, and there is unlikely to be any use case.
				new_binary->value_type = Value_Type::real;
				new_binary->exprs[0]   = make_cast(new_binary->exprs[0], Value_Type::real);
				if(new_binary->exprs[1]->value_type == Value_Type::boolean) new_binary->exprs[1] = make_cast(new_binary->exprs[1], Value_Type::integer);
			} else if (op == '%' || binary->oper == Token_Type::div_int) {
				if(new_binary->exprs[0]->value_type == Value_Type::real || new_binary->exprs[1]->value_type == Value_Type::real) {
					binary->source_loc.print_error_header();
					error_print("The operator ", name(binary->oper), " can only take integer arguments.\n");
					fatal_error_trace(scope);
				}
				if(binary->oper == Token_Type::div_int)
					new_binary->oper = (Token_Type)'/';  // It is more convenient to treat real and int division as the same operator type in later code.
				new_binary->exprs[0] = make_cast(new_binary->exprs[0], Value_Type::integer);
				new_binary->exprs[1] = make_cast(new_binary->exprs[1], Value_Type::integer);
				new_binary->value_type = Value_Type::integer;
			} else {
				make_casts_for_binary_expr(&new_binary->exprs[0], &new_binary->exprs[1]);
				
				if(op == '+' || op == '-' || op == '*')
					new_binary->value_type = new_binary->exprs[0]->value_type;
				else
					new_binary->value_type = Value_Type::boolean;
			}
			
			result.fun = new_binary;
			apply_binop_to_units(new_binary->oper, name(new_binary->oper), result.unit, arg_units[0], arg_units[1], binary->source_loc, scope, new_binary->exprs[0], new_binary->exprs[1]);
		} break;
		
		case Math_Expr_Type::if_chain : {
			auto ifexpr = static_cast<If_Expr_AST *>(ast);
			auto new_if = new Math_Expr_FT(Math_Expr_Type::if_chain);
			
			std::vector<Standardized_Unit> arg_units;
			resolve_arguments(new_if, ast, data, scope, arg_units);
			
			// Figure out what the 'highest' value type of the expression is.
			Value_Type value_type = new_if->exprs[0]->value_type;
			for(int idx = 0; idx < (int)new_if->exprs.size(); idx+=2) {
				auto new_type = new_if->exprs[idx]->value_type;
				if(value_type == Value_Type::iterate) value_type = new_type;
				
				if(new_type == Value_Type::real) value_type = Value_Type::real;
				else if(new_type == Value_Type::integer && value_type == Value_Type::boolean) value_type = Value_Type::integer;
				
				if(new_type == Value_Type::tuple) {
					new_if->exprs[idx]->source_loc.print_error_header();
					error_print("The values in an 'if' expression can't be tuples currently. Instead, create the elements using separate if expressions, then pack them into a tuple after.");
					fatal_error_trace(scope);
				}
			}
			
			if(value_type == Value_Type::iterate || value_type == Value_Type::none) {
				ifexpr->source_loc.print_error_header();
				error_print("At least one of the possible results of the 'if' expression must evaluate to a value.");
				fatal_error_trace(scope);
			}
			
			// Cast all possible result values up to the same type
			for(int idx = 0; idx < (int)new_if->exprs.size(); idx+=2) {
				if(idx+1 < new_if->exprs.size())
					new_if->exprs[idx+1] = make_cast(new_if->exprs[idx+1], Value_Type::boolean);
					
				auto val = new_if->exprs[idx];
				if(val->value_type != Value_Type::iterate)
					new_if->exprs[idx] = make_cast(val, value_type);
			}
			new_if->value_type = value_type;
			
			result.fun = new_if;
			check_if_expr_units(result.unit, arg_units, new_if->exprs, scope);
		} break;
		
		case Math_Expr_Type::local_var : {
			
			auto local = static_cast<Local_Var_AST *>(ast);
			std::string local_name = local->name.string_value;
			
			if(is_reserved(local_name)) {
				local->name.print_error_header();
				fatal_error("The identifier '", local_name, "' is reserved.");
			}
			
			if(!local->is_reassignment) { // It is a declaration
			
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
				
				arguments_must_be_values(new_local, scope);
				
				new_local->name = local_name;
				new_local->value_type = Value_Type::none;//new_local->exprs[0]->value_type;  // The assignment expression does itself not have a value.
				
				result.fun = new_local;
				result.unit = std::move(arg_units[0]);
			} else { // It is a reassignment, not a declaration
				
				// TODO: It is a bit unnecessary that we have to create an Identifier_FT for this. It is just because of code reuse, but maybe something could be factored out.
				Identifier_FT ident;
				Standardized_Unit old_unit;
				bool found = find_local_variable(&ident, old_unit, local_name, scope, true);
				if(!found) {
					local->source_loc.print_error_header();
					error_print("Can not re-assign a value to local variable '", local_name, "' because it has not been declared.");
					fatal_error_trace(scope);
				}
				
				auto new_assign = new Assignment_FT(ident.local_var);
				
				std::vector<Standardized_Unit> arg_units;
				resolve_arguments(new_assign, ast, data, scope, arg_units);
				
				arguments_must_be_values(new_assign, scope);
				
				// Make sure it keeps the value type it was declared with.
				new_assign->exprs[0] = make_cast(new_assign->exprs[0], ident.value_type);
				new_assign->value_type = Value_Type::none;//ident.value_type;
				
				if(!match_exact(&old_unit, &arg_units[0])) {
					local->source_loc.print_error_header();
					error_print("Re-assigning to local variable '", local_name, "' with a value that has a different unit to the one it was declared with. It was declared with ", old_unit.to_utf8(), " (standard form), but reassigned with ", arg_units[0].to_utf8(), " .");
					fatal_error_trace(scope);
				}
				
				result.fun  = new_assign;
				result.unit = std::move(arg_units[0]);
			}
		} break;
		
		case Math_Expr_Type::iterate : {
			auto iter = static_cast<Iterate_AST *>(ast);
			s32 scope_id = find_tagged_scope(iter->iter_tag.string_value, scope);
			if(scope_id < 0) {
				iter->iter_tag.print_error_header();
				error_print("This expression is not inside a scope that has been assigned the iterator '", iter->iter_tag.string_value, "'.");
				fatal_error_trace(scope);
			}
			
			auto new_iter = new Iterate_FT();
			new_iter->scope_id = scope_id;
			new_iter->value_type = Value_Type::iterate;
			
			result.fun = new_iter;
		} break;
		
		case Math_Expr_Type::unit_convert : {
			auto conv = static_cast<Unit_Convert_AST *>(ast);
			
			if(data->simplified) {
				conv->source_loc.print_error_header();
				error_print("Unit conversions are not available in this context.");
				fatal_error_trace(scope);
			}
			
			auto new_binary = new Operator_FT(Math_Expr_Type::binary_operator);
			std::vector<Standardized_Unit> arg_units;
			resolve_arguments(new_binary, ast, data, scope, arg_units);
			
			arguments_must_be_values(new_binary, scope);
			
			Standardized_Unit to_unit;
			if(conv->auto_convert) {
				to_unit = data->expected_unit;
			} else {
				if(conv->by_identifier) {
					auto reg = decl_scope[conv->unit_identifier.string_value];
					if(!reg || reg->id.reg_type != Reg_Type::unit) {
						conv->unit_identifier.print_error_header();
						error_print("The identifier '", conv->unit_identifier.string_value, "' does not refer to a unit.");
					}
					to_unit = model->units[reg->id]->data.standard_form;
				} else {
					Unit_Data conv_unit;
					conv_unit.set_data(conv->unit);
					to_unit = std::move(conv_unit.standard_form);
				}
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
		
		case Math_Expr_Type::unpack_tuple : {
			auto unpack = static_cast<Unpack_Tuple_AST *>(ast);
			auto new_unpack = new Unpack_Tuple_FT();
			new_unpack->value_type = Value_Type::none;
			
			std::vector<Standardized_Unit> arg_units;
			resolve_arguments(new_unpack, ast, data, scope, arg_units);
			
			if(new_unpack->exprs[0]->value_type != Value_Type::tuple) {
				ast->source_loc.print_error_header();
				fatal_error("Tried to unpack something that is not a tuple.");
			}
			
			for(auto &name : unpack->names)
				new_unpack->names.push_back(name.string_value);
			
			result.fun = new_unpack;
			
		} break;
		
		default : {
			fatal_error(Mobius_Error::internal, "Unhandled math expr type in resolve_function_tree().");
		} break;
	}
	
	if(!result.fun) {
		ast->source_loc.print_error_header(Mobius_Error::internal);
		fatal_error(Mobius_Error::internal, "Result unassigned in resolve_function_tree().");
	}
	if(result.fun->value_type == Value_Type::unresolved) {
		ast->source_loc.print_error_header(Mobius_Error::internal);
		fatal_error("Did not resolve value type of expression.");
	}
	
	result.fun->source_loc = ast->source_loc;

	return result;
}

void
register_dependencies(Math_Expr_FT *expr, std::set<Identifier_Data> *depends) {
	for(auto arg : expr->exprs)
		register_dependencies(arg, depends);
	
	if(expr->expr_type != Math_Expr_Type::identifier) return;
	auto ident = static_cast<Identifier_FT *>(expr);

	if(ident->variable_type == Variable_Type::parameter || ident->variable_type == Variable_Type::series || ident->variable_type == Variable_Type::is_at)
		depends->insert(*ident);
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
		
		case Math_Expr_Type::identifier : {
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
		
		case Math_Expr_Type::external_computation : {
			result = copy_one<External_Computation_FT>(source);
		} break;
		
		case Math_Expr_Type::state_var_assignment :
		case Math_Expr_Type::derivative_assignment :
		case Math_Expr_Type::local_var_assignment : {
			result = copy_one<Assignment_FT>(source);
		} break;
		
		case Math_Expr_Type::iterate : {
			result = copy_one<Iterate_FT>(source);
		} break;
		
		case Math_Expr_Type::if_chain :
		case Math_Expr_Type::cast :
		case Math_Expr_Type::no_op : {
			result = copy_one<Math_Expr_FT>(source);
		} break;
		
		case Math_Expr_Type::tuple : {
			result = copy_one<Tuple_FT>(source);
		} break;
		
		case Math_Expr_Type::access_tuple_element : {
			result = copy_one<Access_Tuple_Element_FT>(source);
		} break;
		
		default : {
			fatal_error(Mobius_Error::internal, "Unhandled math expr type in copy().");
		} break;
	};

	for(int idx = 0; idx < result->exprs.size(); ++idx)
		result->exprs[idx] = copy(result->exprs[idx]);
	
	return result;
}


void
print_tabs(int ntabs, std::ostream &os) { for(int i = 0; i < ntabs; ++i) os << '\t'; }

int
precedence(Math_Expr_FT *expr) {
	if(expr->expr_type == Math_Expr_Type::binary_operator) {
		auto binop = static_cast<Operator_FT *>(expr);
		return operator_precedence(binop->oper);
	}
	if(expr->expr_type == Math_Expr_Type::unary_operator)
		return 50'000;
	return 1000'000;
}

struct
Print_Tree_Context {
	Model_Application *app;
	std::ostream *os;
	//int iter_name_gen = 0;
};

struct
Print_Scope : Scope_Local_Vars<std::string, std::string> {
	int iter_name_gen = 0;
};

void
print_var_symbol(Print_Tree_Context *context, Var_Id var_id) {
	
	std::ostream &os = *context->os;
	auto app = context->app;
	auto model = app->model;
	
	auto var = app->vars[var_id];
	if(var->type == State_Var::Type::declared) {
		if(var->is_flux()) {
			auto flux_id = as<State_Var::Type::declared>(var)->decl_id;
			const auto &sym = model->get_symbol(flux_id);
			if(!sym.empty())
				os << model->get_symbol(flux_id);
			else
				os << '\"' << model->fluxes[flux_id]->name << '\"';
		} else {
			auto &loc = var->loc1;
			for(int idx = 0; idx < loc.n_components; ++idx) {
				os << model->get_symbol(loc.components[idx]);
				if(idx != loc.n_components-1) os << '.';
			}
		}
	} else if(var->type == State_Var::Type::regular_aggregate) {
		auto var2 = as<State_Var::Type::regular_aggregate>(var);
		os << "aggregate(";
		print_var_symbol(context, var2->agg_of);
		os << ")";
	} else if(var->type == State_Var::Type::dissolved_conc) {
		auto var2 = as<State_Var::Type::dissolved_conc>(var);
		os << "conc(";
		print_var_symbol(context, var2->conc_of);
		os << ", ";
		print_var_symbol(context, var2->conc_in);
		os << ")";
	} else {
		// TODO: Could do symbol-like printing for some of these too!
		os << var->name;
	}
}

void
print_tree_helper(Math_Expr_FT *expr, Print_Tree_Context *context, Print_Scope *scope, int block_tabs) {

	std::ostream &os = *context->os;
	auto app = context->app;
	
	auto model = app->model;
	
	bool always_bracket = false;
	
	if(!expr) {
		os << "\n___NULL___";
		return;
	}
	
	if(expr->visited)
		os << "\n___DUPLICATE___(" << name(expr->expr_type) << ")\n";
	expr->visited = true;
	
	switch(expr->expr_type) {
		case Math_Expr_Type::block : {
			auto block = static_cast<Math_Block_FT *>(expr);
			Print_Scope new_scope;
			new_scope.scope_id = block->unique_block_id;
			new_scope.scope_up = scope;
			new_scope.scope_value = block->iter_tag;
			int iter_id = 0;
			if(scope) iter_id = scope->iter_name_gen;
			new_scope.iter_name_gen = iter_id+1;
			
			if(block->is_for_loop) {
				std::stringstream ss;
				ss << "iter_" << iter_id;
				std::string iter_name = ss.str();
				new_scope.values[0] = iter_name;
				os << "for " << iter_name << " : 0..";
				print_tree_helper(block->exprs[0], context, &new_scope, block_tabs);
				os << "\n";
				print_tabs(block_tabs+1, os);
				print_tree_helper(block->exprs[1], context, &new_scope, block_tabs+1);
			} else {
				if(!block->iter_tag.empty())
					os << block->iter_tag << ':';
				os << "{\n";
				int idx = 0;
				for(auto exp : block->exprs) {
					if(exp->expr_type == Math_Expr_Type::local_var) {
						auto local = static_cast<Local_Var_FT *>(exp);
						new_scope.values[local->id] = local->name;
					}
					print_tabs(block_tabs+1, os);
					print_tree_helper(exp, context, &new_scope, block_tabs+1);
					os << "\n";
				}
				print_tabs(block_tabs, os);
				os << "}";
			}
		} break;
		
		case Math_Expr_Type::identifier : {
			auto ident = static_cast<Identifier_FT *>(expr);
			
			bool close = false;
			//TODO: name state_var, series and parameter
			if(ident->has_flag(Identifier_FT::last_result)) {  // Not sure if necessary to print other flags as they would have affected something else that is printed any way.
				close = true;
				os << "last(";
			}
			if(ident->variable_type == Variable_Type::local)
				os << find_local_var(scope, ident->local_var);
			else if(ident->variable_type == Variable_Type::parameter)
				os << "par[" << model->get_symbol(ident->par_id) << "]";
			else {
				os << name(ident->variable_type);
				if(ident->variable_type == Variable_Type::series) {
					os << "[";
					print_var_symbol(context, ident->var_id);
					os << "]";
				}
			}
			if(!expr->exprs.empty()) {
				os << '[';
				print_tree_helper(expr->exprs[0], context, scope, block_tabs);
				os << ']';
			}
			if(close) os << ')';
			
		} break;
		
		case Math_Expr_Type::literal : {
			auto literal = static_cast<Literal_FT *>(expr);
			if(literal->value_type == Value_Type::real)    os << literal->value.val_real;
			if(literal->value_type == Value_Type::integer) os << literal->value.val_integer;
			if(literal->value_type == Value_Type::boolean) os << (literal->value.val_boolean ? "true": "false");
		} break;
		
		case Math_Expr_Type::function_call : {
			auto fun = static_cast<Function_Call_FT *>(expr);
			os << fun->fun_name << '(';
			int idx = 0;
			for(auto exp : fun->exprs) {
				print_tree_helper(exp, context, scope, block_tabs);
				if(idx++ != fun->exprs.size()-1) os << ", ";
			}
			os << ')';
		} break;
		
		case Math_Expr_Type::unary_operator : {
			int prec = precedence(expr);
			int prec0 = precedence(expr->exprs[0]);
			auto unary = static_cast<Operator_FT *>(expr);
			os << name(unary->oper);
			if(prec0 < prec || always_bracket) os << '(';
			print_tree_helper(unary->exprs[0], context, scope, block_tabs);
			if(prec0 < prec || always_bracket) os << ')';
		} break;
		
		case Math_Expr_Type::binary_operator : {
			auto binop = static_cast<Operator_FT *>(expr);
			int prec = precedence(expr);
			int prec0 = precedence(expr->exprs[0]);
			int prec1 = precedence(expr->exprs[1]);
			if(prec0 < prec || always_bracket) os << '(';
			print_tree_helper(binop->exprs[0], context, scope, block_tabs);
			if(prec0 < prec || always_bracket) os << ')';
			os << ' ' << name(binop->oper) << ' ';
			if(prec1 < prec || always_bracket) os << '(';
			print_tree_helper(binop->exprs[1], context, scope, block_tabs);
			if(prec1 < prec || always_bracket) os << ')';
		} break;
		
		case Math_Expr_Type::if_chain : {
			os << "{\n";
			print_tabs(block_tabs+1, os);
			for(int idx = 0; idx < expr->exprs.size() / 2; ++idx) {
				print_tree_helper(expr->exprs[2*idx], context, scope, block_tabs+1);
				os << " if ";
				print_tree_helper(expr->exprs[2*idx+1], context, scope, block_tabs+1);
				os << ",\n";
				print_tabs(block_tabs+1, os);
			}
			print_tree_helper(expr->exprs[expr->exprs.size()-1], context, scope, block_tabs+1);
			os << " otherwise\n";
			print_tabs(block_tabs, os);
			os << '}';
		} break;
		
		case Math_Expr_Type::cast : {
			os << "cast(" << name(expr->value_type) << ", ";
			print_tree_helper(expr->exprs[0], context, scope, block_tabs);
			os << ')';
		} break;
		
		case Math_Expr_Type::local_var : {
			auto local = static_cast<Local_Var_FT *>(expr);
			if(!local->is_used) os << "(unused)";  // NOTE: if the tree has been pruned before printing, it is probably removed.
			os << local->name << " := ";
			print_tree_helper(expr->exprs[0], context, scope, block_tabs);
			os << ',';
		} break;
		
		case Math_Expr_Type::local_var_assignment : {
			auto assign = static_cast<Assignment_FT *>(expr);
			os << find_local_var(scope, assign->local_var);
			os << " <- ";
			print_tree_helper(expr->exprs[0], context, scope, block_tabs);
			os << ',';
		} break;
		
		case Math_Expr_Type::state_var_assignment : {
			// TODO: Want to be able to print the symbol of the variable here, but for that we need the var_id to be stored in the expression.
			auto assign = static_cast<Assignment_FT *>(expr);
			os << "state_var[";
			print_var_symbol(context, assign->var_id);
			os << "][";
			print_tree_helper(expr->exprs[0], context, scope, block_tabs);
			os << "] <- ";
			print_tree_helper(expr->exprs[1], context, scope, block_tabs);
		} break;
		
		case Math_Expr_Type::derivative_assignment : {
			auto assign = static_cast<Assignment_FT *>(expr);
			os << "ddt[";
			print_var_symbol(context, assign->var_id);
			os << "][";
			print_tree_helper(expr->exprs[0], context, scope, block_tabs);
			os << "] <- ";
			print_tree_helper(expr->exprs[1], context, scope, block_tabs);
		} break;
		
		case Math_Expr_Type::external_computation : {
			auto external = static_cast<External_Computation_FT *>(expr);
			os << "external_computation(\"" << external->function_name << "\", ";
			int idx = 0;
			for(auto arg : external->exprs) {
				print_tree_helper(arg, context, scope, block_tabs);
				if (idx++ != external->exprs.size()-1) os << ", ";
			}
			os << ")";
		} break;
		
		case Math_Expr_Type::iterate : {
			auto iter = static_cast<Iterate_FT *>(expr);
			//try {
				auto iter_scope = find_scope(scope, iter->scope_id);
				os << "iterate " << iter_scope->scope_value;
			//} catch(int) {
			//	os << "iterate " << "(missing)";
			//}
		} break;
		
		case Math_Expr_Type::tuple : {
			os << "tuple(";
			int idx = 0;
			for(auto arg : expr->exprs) {
				print_tree_helper(arg, context, scope, block_tabs);
				if (idx++ != expr->exprs.size()-1) os << ", ";
			}
			os << ")";
		} break;
		
		case Math_Expr_Type::access_tuple_element : {
			auto access = static_cast<Access_Tuple_Element_FT *>(expr);
			os << find_local_var(scope, access->tuple_id);
			os << "[" << access->element_index << "]";
		} break;
		
		case Math_Expr_Type::no_op : {
			os << "(no-op)";
		} break;
		
		default : {
			fatal_error("Unhandled math expr type in print_tree.");
		} break;
	}
}

void
print_tree(Model_Application *app, Math_Expr_FT *expr, std::ostream &os) {//, const std::string &function_name) {
	
	Print_Tree_Context context;
	context.app = app;
	context.os = &os;
	
	print_tree_helper(expr, &context, nullptr, 0);
}
