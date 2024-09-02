
#include <sys/stat.h>
#include <algorithm>
#include <sstream>

#include "model_declaration.h"
#include "resolve_identifier.h"
#include "function_tree.h"
#include "grouped_topological_sort.h"


Registry_Base *
Mobius_Model::registry(Reg_Type reg_type) {
	switch(reg_type) {
		case Reg_Type::par_group :                return &par_groups;
		case Reg_Type::parameter :                return &parameters;
		case Reg_Type::unit :                     return &units;
		case Reg_Type::module_template :          return &module_templates;
		case Reg_Type::library :                  return &libraries;
		case Reg_Type::component :                return &components;
		case Reg_Type::var :                      return &vars;
		case Reg_Type::flux :                     return &fluxes;
		case Reg_Type::function :                 return &functions;
		case Reg_Type::index_set :                return &index_sets;
		case Reg_Type::solver :                   return &solvers;
		case Reg_Type::solver_function :          return &solver_functions;
		case Reg_Type::constant :                 return &constants;
		case Reg_Type::connection :               return &connections;
		case Reg_Type::module :                   return &modules;
		case Reg_Type::loc :                      return &locs;
		case Reg_Type::discrete_order :           return &discrete_orders;
		case Reg_Type::external_computation :     return &external_computations;
	}
	
	fatal_error(Mobius_Error::internal, "Unhandled entity type ", name(reg_type), " in registry().");
	return nullptr;
}

Decl_Scope *
Mobius_Model::get_scope(Entity_Id id) {
	if(!is_valid(id))
		return &top_scope;
	else if(id.reg_type == Reg_Type::library)
		return &libraries[id]->scope;
	else if(id.reg_type == Reg_Type::module)
		return &modules[id]->scope;
	else if(id.reg_type == Reg_Type::par_group)
		return &par_groups[id]->scope;
	fatal_error(Mobius_Error::internal, "Tried to look up the scope belonging to an id that is not a library, module or par_group.");
	return nullptr;
}

void
register_intrinsics(Mobius_Model *model) {
	auto global = &model->global_scope;
	
	#define MAKE_INTRINSIC1(name, emul, llvm, ret_type, type1) \
		{ \
			auto fun_id = model->functions.create_internal(global, #name, #name, Decl_Type::function); \
			auto fun = model->functions[fun_id]; \
			fun->fun_type = Function_Type::intrinsic; \
			fun->args = {"a"}; \
			fun->has_been_processed = true; \
		}
	#define MAKE_INTRINSIC2(name, emul, ret_type, type1, type2) \
		{ \
			auto fun_id = model->functions.create_internal(global, #name, #name, Decl_Type::function); \
			auto fun = model->functions[fun_id]; \
			fun->fun_type = Function_Type::intrinsic; \
			fun->args = {"a", "b"}; \
			fun->has_been_processed = true; \
		}
	#include "intrinsics.incl"
	#undef MAKE_INTRINSIC1
	#undef MAKE_INTRINSIC2
	
	//auto test_fun = model->functions.create_internal(global, "_test_fun_", "_test_fun_", Decl_Type::function);
	//model->functions[test_fun]->fun_type = Function_Type::linked;
	//model->functions[test_fun]->args = {"a"};
	
	auto dimless_id = model->units.create_internal(nullptr, "na", "dimensionless", Decl_Type::unit); // NOTE: this is not exported to the scope. Just have it as a unit for pi.
	auto pi_id = model->constants.create_internal(global, "pi", "π", Decl_Type::constant);
	model->constants[pi_id]->value.val_real = 3.14159265358979323846;
	model->constants[pi_id]->value_type = Value_Type::real;
	model->constants[pi_id]->unit = dimless_id;
	
	auto mod_scope = &model->top_scope;
	
	auto system_id = model->par_groups.create_internal(mod_scope, "system", "System", Decl_Type::par_group);
	auto system = model->par_groups[system_id];
	system->scope.parent_id = system_id;
	
	auto start_id  = model->parameters.create_internal(&system->scope, "start_date", "Start date", Decl_Type::par_datetime);
	auto end_id    = model->parameters.create_internal(&system->scope, "end_date", "End date", Decl_Type::par_datetime);
	mod_scope->import(system->scope);
	// Just so that they are registered as being referenced and we don't get the warning about unreferenced parameters.
	//  TODO: We could instead have a flag on internally created things and just not give warnings for them.
	system->scope["start_date"];
	system->scope["end_date"];
	
	auto start  = model->parameters[start_id];
	auto end    = model->parameters[end_id];
	
	Date_Time default_start(1970, 1, 1);
	Date_Time default_end(1970, 1, 16);
	Date_Time min_date(1000, 1, 1);
	Date_Time max_date(3000, 1, 1);
	
	start->default_val.val_datetime = default_start;
	start->description = "The start date is inclusive";
	start->min_val.val_datetime = min_date;
	start->max_val.val_datetime = max_date;
	
	end->default_val.val_datetime = default_end;
	end->description   = "The end date is inclusive";
	end->min_val.val_datetime = min_date;
	end->max_val.val_datetime = max_date;
	
	auto euler_id = model->solver_functions.create_internal(mod_scope, "euler", "Euler", Decl_Type::solver_function);
	auto dascru_id = model->solver_functions.create_internal(mod_scope, "inca_dascru", "INCADascru", Decl_Type::solver_function);
	
	model->solver_functions[euler_id]->solver_fun = &euler_solver;
	model->solver_functions[dascru_id]->solver_fun = &inca_dascru;
}

Entity_Id
load_top_decl_from_file(Mobius_Model *model, Source_Location from, String_View path, String_View relative_to, const std::string &decl_name, Decl_Type type) {
	
	// NOTE: This one is only for modules, preambles and libraries.
	
	bool is_preamble = (type == Decl_Type::preamble);
	bool is_module_like = is_preamble || (type == Decl_Type::module);
	
	// TODO: Should really check if the model-relative path exists before changing the relative path.
	std::string models_path = model->config.mobius_base_path + "models/"; // Note: since relative_to is a string view, this one must exist in the outer scope.
	if(!model->config.mobius_base_path.empty()) {

		if(is_module_like && bottom_directory_is(path, "modules"))
			relative_to = models_path;
		else if(type == Decl_Type::library && bottom_directory_is(path, "stdlib"))
			relative_to = model->config.mobius_base_path;
	}
	
	std::string normalized_path;
	String_View file_data = model->file_handler.load_file(path, from, relative_to, &normalized_path);
	
	bool already_parsed_file = false;
	Entity_Id result = invalid_entity_id;
	auto find_file = model->parsed_decls.find(normalized_path);
	
	if(find_file != model->parsed_decls.end()) {
		already_parsed_file = true;
		auto find_decl = find_file->second.find(decl_name);
		if(find_decl != find_file->second.end())
			result = find_decl->second;
	}

	if(!already_parsed_file) {
		Token_Stream stream(path, file_data);
			
		while(true) {
			if(stream.peek_token().type == Token_Type::eof) break;
			Decl_AST *decl = parse_decl(&stream);
			
			// TODO: Could we find a way to not have to do these matches both here and inside process_module_declaration ?
			if(is_module_like) {
				match_declaration(decl,
					{
						{Token_Type::quoted_string, Decl_Type::version},
						{Token_Type::quoted_string, Decl_Type::version, {true}}
					}, false);
			} else if (decl->type == Decl_Type::library) {
				match_declaration(decl, {{Token_Type::quoted_string}}, false);
			} else {
				decl->source_loc.print_error_header();
				fatal_error("Module files should only have modules, preambles or libraries in the top scope. This is a '", name(decl->type), "'.");
			}
			
			std::string found_name = single_arg(decl, 0)->string_value;
			Entity_Id id = invalid_entity_id;
			
			if(decl->type == Decl_Type::library) {
				id = model->libraries.register_decl(&model->top_scope, decl);
				auto lib = model->libraries[id];
				lib->scope.parent_id = id;
				lib->normalized_path = normalized_path;
				lib->name = found_name;
			} else if (is_module_like) {
				id = model->module_templates.register_decl(&model->top_scope, decl);
				auto mod = model->module_templates[id];
				mod->normalized_path = normalized_path;
			}
			model->parsed_decls[normalized_path][found_name] = id;
			if(decl_name == found_name) {
				result = id;
				if(decl->type != type) {
					decl->source_loc.print_error_header(Mobius_Error::parsing);
					fatal_error("The declaration \"", decl_name, "\" is of type '", name(decl->type), "', but was loaded as a '", name(type), "'.");
				}
			}
		}
	}
	
	if(!is_valid(result))
		fatal_error(Mobius_Error::parsing, "Could not find the '", name(type), "' \"", decl_name, "\" in the file \"", path, "\".");
	
	return result;
}

// TODO: We could acutally move some functionality into these, but it is not crucial..
void
Library_Registration::process_declaration(Catalog *catalog) {
	fatal_error(Mobius_Error::internal, "Registration<Reg_Type::library>::process_declaration should not be called.");
}

void
Module_Template_Registration::process_declaration(Catalog *catalog) {
	fatal_error(Mobius_Error::internal, "Registration<Reg_Type::module_template>::process_declaration should not be called.");
}

void
Module_Registration::process_declaration(Catalog *catalog) {
	fatal_error(Mobius_Error::internal, "Registration<Reg_Type::module>::process_declaration should not be called.");
}

void
Unit_Registration::process_declaration(Catalog *catalog) {
	
	auto model = static_cast<Mobius_Model *>(catalog);
	auto scope = catalog->get_scope(scope_id);
	
	if(decl->type == Decl_Type::unit)
		data.set_data(decl);
	else if(decl->type == Decl_Type::compose_unit) {
		match_declaration(decl, {{Decl_Type::unit, {Decl_Type::unit, true}}});
		for(auto arg : decl->args)
			composed_of.push_back(scope->resolve_argument(Reg_Type::unit, arg));
	} else if(decl->type == Decl_Type::unit_of) {
		
		int which = match_declaration(decl, {
			{Decl_Type::par_real},
			{Decl_Type::par_int},
			{Decl_Type::constant},
			{Arg_Pattern::loc}
		});
		if(which == 0 || which == 1)
			unit_of = scope->resolve_argument(Reg_Type::parameter, decl->args[0]);
		else if(which == 2)
			unit_of = scope->resolve_argument(Reg_Type::constant, decl->args[1]);
		else {
			// TODO: This is a bit hacky.
			Loc_Registration reg;
			resolve_loc_decl_argument(model, scope, decl->args[0], reg);
			unit_of = reg.val_id;
			unit_of_loc = reg.loc;
		}
		
	} else
		fatal_error(Mobius_Error::internal, "Unimplemented unit declaration type");
	
	// TODO: We also want 'unit_of', but it is very tricky because it can't be resolved until after state vars are resolved, but when we resolve state vars we already want the units to be available... Although we could just iterate over 'var' declarations and match the var_location. Then model_composition will check consistency of those any way.
	
	has_been_processed = true;
}

void
Component_Registration::process_declaration(Catalog *catalog) {
	// NOTE: the Decl_Type is either compartment, property or quantity.
	
	//TODO: If we want to allow units on this declaration directly, we have to check for mismatches between decls in different modules.
	// For now it is safer to just have it on the 'var', but we could go over this later and see if we could make it work.
	int which = match_declaration(decl,
		{
			{Token_Type::quoted_string},
			{Token_Type::quoted_string, {Decl_Type::index_set, true}},
		});
	
	set_serial_name(catalog, this);
	
	if(decl->body) {
		if(decl->type != Decl_Type::property) {
			decl->body->opens_at.print_error_header();
			fatal_error("Only properties can have default code, not quantities or compartments.");
		}
		auto fun = static_cast<Function_Body_AST *>(decl->body);
		default_code = fun->block;
	}
	
	auto scope = catalog->get_scope(scope_id);
	
	if(decl->args.size() > 1) {
		if(decl->type == Decl_Type::property) {
			decl->args[1]->source_loc().print_error_header();
			fatal_error("A 'property' declaration should not be directly distributed over index sets.");
		}
		for(int idx = 1; idx < decl->args.size(); ++idx) {
			auto id = scope->resolve_argument(Reg_Type::index_set, decl->args[idx]);
			index_sets.push_back(id);
		}
	}
	
	has_been_processed = true;
}

void
Parameter_Registration::process_declaration(Catalog *catalog) {

	Token_Type token_type = get_token_type(decl->type);
	
	int which;
	if(decl->type == Decl_Type::par_real || decl->type == Decl_Type::par_int) {
		which = match_declaration(decl,
		{
			{Token_Type::quoted_string, Decl_Type::unit, token_type},                                                      // 0
			{Token_Type::quoted_string, Decl_Type::unit, token_type, Token_Type::quoted_string},                           // 1
			{Token_Type::quoted_string, Decl_Type::unit, token_type, token_type, token_type},                              // 2
			{Token_Type::quoted_string, Decl_Type::unit, token_type, token_type, token_type, Token_Type::quoted_string},   // 3
		});
	} else if (decl->type == Decl_Type::par_bool || decl->type == Decl_Type::par_enum) {            // note: min, max values for boolean parameters are redundant.
		which = match_declaration(decl,
		{
			{Token_Type::quoted_string, token_type},                                                      // 0
			{Token_Type::quoted_string, token_type, Token_Type::quoted_string},                           // 1
		}, true, -1);
	} else
		fatal_error(Mobius_Error::internal, "Got an unrecognized type in parameter declaration processing.");

	set_serial_name(catalog, this);
	auto scope = catalog->get_scope(scope_id);
	
	int mt0 = 2;
	if(token_type == Token_Type::boolean) mt0--;
	if(decl->type != Decl_Type::par_enum)
		default_val                        = get_parameter_value(single_arg(decl, mt0), token_type);
	
	if(decl->type == Decl_Type::par_real) {
		unit                               = scope->resolve_argument(Reg_Type::unit, decl->args[1]);
		if(which == 2 || which == 3) {
			min_val                        = get_parameter_value(single_arg(decl, 3), Token_Type::real);
			max_val                        = get_parameter_value(single_arg(decl, 4), Token_Type::real);
		} else {
			min_val.val_real               = -std::numeric_limits<double>::infinity();
			max_val.val_real               =  std::numeric_limits<double>::infinity();
		}
	} else if (decl->type == Decl_Type::par_int) {
		unit                               = scope->resolve_argument(Reg_Type::unit, decl->args[1]);
		if(which == 2 || which == 3) {
			min_val                        = get_parameter_value(single_arg(decl, 3), Token_Type::integer);
			max_val                        = get_parameter_value(single_arg(decl, 4), Token_Type::integer);
		} else {
			min_val.val_integer            = std::numeric_limits<s64>::lowest();
			max_val.val_integer            = std::numeric_limits<s64>::max();
		}
	} else if (decl->type == Decl_Type::par_bool) {
		min_val.val_boolean                = false;
		max_val.val_boolean                = true;
	}
	
	int mt1 = 3;
	if(decl->type == Decl_Type::par_bool || decl->type == Decl_Type::par_enum) mt1--;
	if(which == 1)
		description         = single_arg(decl, mt1)->string_value;
	else if(which == 3)
		description         = single_arg(decl, 5)->string_value;
	
	if(decl->type == Decl_Type::par_enum) {
		auto body = static_cast<Function_Body_AST *>(decl->body);   // Re-purposing function body for a simple list... TODO: We should maybe have a separate body type for that
		for(auto expr : body->block->exprs) {
			if(expr->type != Math_Expr_Type::identifier) {
				expr->source_loc.print_error_header();
				fatal_error("Expected a list of identifiers only.");
			}
			auto ident = static_cast<Identifier_Chain_AST *>(expr);
			if(ident->chain.size() != 1) {
				expr->source_loc.print_error_header();
				fatal_error("Expected a list of identifiers only.");
			}
			enum_values.push_back(ident->chain[0].string_value);
		}
		std::string default_val_name = single_arg(decl, 1)->string_value;
		s64 default_val_int = enum_int_value(default_val_name);
		if(default_val_int < 0) {
			single_arg(decl, 1)->print_error_header();
			fatal_error("The given default value '", default_val_name, "' does not appear on the list of possible values for this enum parameter.");
		}
		default_val.val_integer = default_val_int;
		min_val.val_integer = 0;
		max_val.val_integer = (s64)enum_values.size() - 1;
	}
	
	has_been_processed = true;
}

void
Par_Group_Registration::process_declaration(Catalog *catalog) {
	int which = match_declaration(decl,
	{
		{Token_Type::quoted_string},
		{Token_Type::quoted_string, {Decl_Type::compartment, true}},
	}, false, -1, true);
	
	auto parent_scope = catalog->get_scope(scope_id);
	
	// TODO: Should everything in the parent_scope also be imported to the scope ? Only really necessary for unit, but would be nice.
	
	set_serial_name(catalog, this);
	scope.parent_id = id;
	
	// TODO: Don't allow duplicate components or index sets.
	
	if(which >= 1) {
		for(int idx = 1; idx < decl->args.size(); ++idx)
			components.push_back(parent_scope->resolve_argument(Reg_Type::component, decl->args[idx]));
	}
	
	for(auto note : decl->notes) {
		auto str = note->decl.string_value;
		if(str == "index_with") {
			
			match_declaration_base(note, {{{Decl_Type::index_set, true}}}, 0);
			
			for(auto arg : note->args)
				direct_index_sets.push_back(parent_scope->resolve_argument(Reg_Type::index_set, arg));
			
		} else {
			note->decl.print_error_header();
			fatal_error("Unrecognized note type '", str, "' for par_group declaration.");
		}
	}
	
	auto body = static_cast<Decl_Body_AST *>(decl->body);

	// NOTE: Have to allow Decl_Type::unit since they are often inline declared in parameter declarations.
	for(Decl_AST *child : body->child_decls)
		catalog->register_decls_recursive(&scope, child, {Decl_Type::par_real, Decl_Type::par_int, Decl_Type::par_bool, Decl_Type::par_enum, Decl_Type::unit, Decl_Type::compose_unit, Decl_Type::unit_of});
	
	for(auto id : scope.all_ids) {
		catalog->find_entity(id)->process_declaration(catalog);
	}
	
	// All the parameters are visible in the module or model scope.
	parent_scope->import(scope, &decl->source_loc);
	
	//TODO: process doc string(s) in the par group?
	
	has_been_processed = true;
}

void
Function_Registration::process_declaration(Catalog *catalog) {
	
	match_declaration(decl,
		{
			{},
			{{Decl_Type::unit, true}},
		}, true, -1);
	
	auto scope = catalog->get_scope(scope_id);
	auto model = static_cast<Mobius_Model *>(catalog);
	
	for(auto arg : decl->args) {
		if(!arg->decl && arg->chain.size() != 1) {
			decl->source_loc.print_error_header();
			fatal_error("The arguments to a function declaration should be just single identifiers with or without units.");
		}
		if(arg->decl) {
			// TODO: It is a bit annoying to not use resolve_argument here, but we don't want it to be associated to the identifier in the scope.
			// We could also just store the unit data in the vector instead of having separate
			// unit entities...
			// The issue here is that we reuse the same syntax as a decl, but it isn't actually
			// one.
			auto unit_id = model->units.create_internal(scope, "", "", Decl_Type::unit);
			auto unit = model->units[unit_id];
			unit->source_loc = arg->source_loc();
			unit->data.set_data(arg->decl);
			unit->has_been_processed = true;
			if(!arg->decl->identifier.string_value.count) {
				arg->decl->source_loc.print_error_header();
				fatal_error("All arguments to a function must be named.");
			}
			args.push_back(arg->decl->identifier.string_value);
			expected_units.push_back(unit_id);
		} else {
			args.push_back(arg->chain[0].string_value);
			expected_units.push_back(invalid_entity_id);
		}
	}
	
	for(int idx = 1; idx < args.size(); ++idx) {
		for(int idx2 = 0; idx2 < idx; ++idx2) {
			if(args[idx] == args[idx2]) {
				decl->args[idx]->chain[0].print_error_header();
				fatal_error("Duplicate argument name '", args[idx], "' in function declaration.");
			}
		}
	}
	
	auto body = static_cast<Function_Body_AST *>(decl->body);
	code = body->block;
	fun_type = Function_Type::decl;
	
	has_been_processed = true;
}

void
Constant_Registration::process_declaration(Catalog *catalog) {
	
	int which = match_declaration(decl, 
		{
			{Token_Type::quoted_string, Decl_Type::unit, Token_Type::real},
			{Token_Type::boolean}
		});
	
	if(which == 0) {
		set_serial_name(catalog, this);
		auto scope = catalog->get_scope(scope_id);
		
		unit = scope->resolve_argument(Reg_Type::unit, decl->args[1]);
		value.val_real = single_arg(decl, 2)->double_value();
		value_type = Value_Type::real;
	} else {
		value.val_boolean = single_arg(decl, 0)->val_bool;
		value_type = Value_Type::boolean;
	}
	
	has_been_processed = true;
}

void
Var_Registration::process_declaration(Catalog *catalog) {

	int which = match_declaration(decl,
		{
			{Arg_Pattern::loc, Decl_Type::unit},
			{Arg_Pattern::loc, Decl_Type::unit, Decl_Type::unit},
			{Arg_Pattern::loc, Decl_Type::unit, Token_Type::quoted_string},
			{Arg_Pattern::loc, Decl_Type::unit, Decl_Type::unit, Token_Type::quoted_string},
		},
		false, true, true);
	
	// NOTE: We don't set a serial name because this name is allowed to clash with other names, and serialization works differently for vars.
	int name_idx = which;
	Token *var_name_token = nullptr;
	if(which == 2 || which == 3)
		var_name_token = single_arg(decl, name_idx);
	
	if(var_name_token) {
		check_allowed_serial_name(var_name_token->string_value, var_name_token->source_loc);
		var_name = var_name_token->string_value;
	}
	
	auto model = static_cast<Mobius_Model *>(catalog);
	auto scope = catalog->get_scope(scope_id);

	resolve_simple_loc_argument(model, scope, decl->args[0], var_location);
	
	unit = scope->resolve_argument(Reg_Type::unit, decl->args[1]);
	
	if(which == 1 || which == 3)
		conc_unit = scope->resolve_argument(Reg_Type::unit, decl->args[2]);

	if(decl->body)
		code = static_cast<Function_Body_AST *>(decl->body)->block;
	
	for(auto note : decl->notes) {
		auto str = note->decl.string_value;
		if(str == "initial" || str == "initial_conc") {
			
			match_declaration_base(note, {{}}, -1);
			
			if(initial_code) {
				note->decl.print_error_header();
				fatal_error("Declaration var more than one 'initial' or 'initial_conc' block.");
			}
			initial_code = static_cast<Function_Body_AST *>(note->body)->block;
			initial_is_conc = (str == "initial_conc");
		} else if(str == "override" || str == "override_conc") {
			
			match_declaration_base(note, {{}}, -1);
			
			if(override_code) {
				note->decl.print_error_header();
				fatal_error("Declaration has more than one 'override' or 'override_conc' block.");
			}
			override_code = static_cast<Function_Body_AST *>(note->body)->block;
			override_is_conc = (str == "override_conc");
			
		} else if(str == "no_store") {
			
			match_declaration_base(note, {{}}, 0);
			
			store_series = false;
			
		} else if(str == "show_conc") {
			
			match_declaration_base(note, {{Arg_Pattern::loc, Decl_Type::unit}}, 0);
			
			resolve_simple_loc_argument(model, scope, note->args[0], additional_conc_medium);
			additional_conc_unit = scope->resolve_argument(Reg_Type::unit, note->args[1]);
			
		} else if(str == "add_to_existing") {
			
			match_declaration_base(note, {{}}, -1);
			adds_code_to_existing = static_cast<Function_Body_AST *>(note->body)->block;
		} else if(str == "clear_nan") {
			
			match_declaration_base(note, {{}}, 0);
			clear_nan = true;
		} else {
			note->decl.print_error_header();
			fatal_error("Urecognized note '", str, "' for var declaration.");
		}
	}
	
	if(adds_code_to_existing && (code || override_code || initial_code)) {
		source_loc.print_error_header();
		fatal_error("An 'add_to_existing' block can't be combined with other function blocks for a single 'var' declaration.");
	}
	
	if(override_code && code) {
		decl->body->opens_at.print_error_header();
		fatal_error("A variable can't have both a main and an @override block.");
	}
	
	if((is_valid(conc_unit) || initial_is_conc || override_is_conc || is_located(additional_conc_medium)) && !var_location.is_dissolved()) {
		source_loc.print_error_header();
		fatal_error("Concentration variables can only be created for dissolved quantities.");
		// TODO: This doesn't check that it is a quantity, not a property, which we can't yet. Do we actually do that check in model composition?
	}
	
	has_been_processed = true;
}

void
Flux_Registration::process_declaration(Catalog *catalog) {
	
	match_declaration(decl,
		{
			{Arg_Pattern::loc, Arg_Pattern::loc, Decl_Type::unit, Token_Type::quoted_string},
		}, true, -1, true);
	
	set_serial_name(catalog, this, 3);
	
	auto model = static_cast<Mobius_Model *>(catalog);
	auto scope = catalog->get_scope(scope_id);
	
	unit = scope->resolve_argument(Reg_Type::unit, decl->args[2]);
	//resolve_argument<Reg_Type::unit>(model, scope, decl, 2);
	
	resolve_flux_loc_argument(model, scope, decl->args[0], source);
	resolve_flux_loc_argument(model, scope, decl->args[1], target);
	
	if(source == target && is_located(source)) {
		decl->source_loc.print_error_header();
		fatal_error("The source and the target of a flux can't be the same.");
	}
	
	if(source.r1.type == Restriction::specific) {
		decl->source_loc.print_error_header();
		fatal_error("For now we only allow the target of a flux to have the 'specific' restriction");
	}
	bool has_specific_target = (target.r1.type == Restriction::specific) || (target.r2.type == Restriction::specific);
	bool has_specific_code = false;
	
	if(decl->body)
		code = static_cast<Function_Body_AST *>(decl->body)->block;
	
	for(auto note : decl->notes) {
		auto str = note->decl.string_value;
		if(str == "no_carry") {
			match_declaration_base(note, {{}}, 1);
		
			if(!is_located(source)) {
				note->decl.print_error_header();
				fatal_error("A 'no_carry' block only makes sense if the source of the flux is not 'out'.");
			}
			if(note->body)
				no_carry_ast = static_cast<Function_Body_AST *>(note->body)->block;
			else
				no_carry_by_default = true;
			
		} else if(str == "no_store") {
			match_declaration_base(note, {{}}, 0);
			
			store_series = false;
			
		} else if(str == "specific") {
			match_declaration_base(note, {{}}, -1);
			
			has_specific_code = true;
			specific_target_ast = static_cast<Function_Body_AST *>(note->body)->block;
			
		} else if(str == "bidirectional" || str == "mixing") {
			match_declaration_base(note, {{}}, 0);
			
			if(bidirectional || mixing) {
				note->decl.print_error_header();
				fatal_error("A flux can't both be 'bidirectional' and 'mixing'");
			}
			
			if(str == "bidirectional")
				bidirectional = true;
			else
				mixing = true;
			
			if(!is_valid(target.r1.connection_id) && !is_located(target) || !is_located(source)) {
				note->decl.print_error_header();
				fatal_error("A flux going to or from 'out' can't be '", str, "'.");
			}
		} else {
			note->decl.print_error_header();
			fatal_error("Unrecognized note '", str, "' for a flux declaration.");
		}
	}
	
	if(has_specific_target != has_specific_code) {
		decl->source_loc.print_error_header();
		fatal_error("A flux must either have both a 'specific' restriction in its target and a '@specific' code block, or have none of these.");
	}
	
	has_been_processed = true;
}

void
Discrete_Order_Registration::process_declaration(Catalog *catalog) {
	match_declaration(decl, {{ }}, false, -1);
	
	auto model = static_cast<Mobius_Model *>(catalog);
	auto scope = catalog->get_scope(scope_id);
	
	auto body = static_cast<Function_Body_AST *>(decl->body);
	bool success = true;
	for(auto expr : body->block->exprs) {
		if(expr->type != Math_Expr_Type::identifier) {
			success = false;
			break;
		}
		auto ident = static_cast<Identifier_Chain_AST *>(expr);
		if(ident->chain.size() != 1 || !ident->bracketed_chain.empty()) {
			success = false;
			break;
		}
		auto flux_id = scope->expect(Reg_Type::flux, &ident->chain[0]);
		fluxes.push_back(flux_id);
		
		auto flux = model->fluxes[flux_id];
		if(is_valid(flux->discrete_order)) {
			ident->chain[0].source_loc.print_error_header();
			error_print("The flux '", ident->chain[0].string_value, "' was already put in another discrete order declared here:\n");
			model->discrete_orders[flux->discrete_order]->source_loc.print_error();
			mobius_error_exit();
		}
		flux->discrete_order = id;
	}
	if(!success) {
		body->opens_at.print_error_header();
		fatal_error("A 'discrete_order' body should just contain a list of flux identifiers.");
	}
	
	has_been_processed = true;
}

void
External_Computation_Registration::process_declaration(Catalog *catalog) {
	// NOTE: Since we disambiguate entities on their name right now, we can't let the name and function_name be the same in case you want to reuse the same function many times in the same scope.
	
	// TODO: Should maybe make it possible to only have the initial block and not the main block.
	
	int which = match_declaration(decl,
		{
			{ Token_Type::quoted_string, Token_Type::quoted_string },
			{ Token_Type::quoted_string, Token_Type::quoted_string, Decl_Type::compartment },
		}, false, -1, true);
	
	set_serial_name(catalog, this);
	auto scope = catalog->get_scope(scope_id);
	
	function_name = single_arg(decl, 1)->string_value;
	if(which == 1)
		component = scope->resolve_argument(Reg_Type::component, decl->args[2]);
	
	auto body = static_cast<Function_Body_AST *>(decl->body);
	code = body->block;
	
	for(auto note : decl->notes) {
		if(note->decl.string_value == "allow_connection") {
			match_declaration_base(note, {{Decl_Type::compartment, Decl_Type::connection}}, 0);
			connection_component = scope->resolve_argument(Reg_Type::component, note->args[0]);
			connection           = scope->resolve_argument(Reg_Type::connection, note->args[1]);
		} else if (note->decl.string_value == "initial") {
			match_declaration_base(note, {{Token_Type::quoted_string}}, -1);
			init_function_name = single_arg(note, 0)->string_value;
			init_code = static_cast<Function_Body_AST *>(note->body)->block;
		} else {
			note->decl.print_error_header();
			fatal_error("Unrecognized note type '", note->decl.string_value, "' for 'external_computation'.");
		}
	}
	
	has_been_processed = true;
}

void
recursive_gather_connection_component_options(Mobius_Model *model, Decl_Scope *scope, Entity_Id connection_id, Math_Expr_AST *expr) {
	if(expr->type == Math_Expr_Type::regex_identifier) {
		
		auto ident = static_cast<Regex_Identifier_AST *>(expr);
		if(ident->ident.string_value == "out" || ident->wildcard)
			return;
		
		auto connection = model->connections[connection_id];
		auto component_id = scope->expect(Reg_Type::component, &ident->ident);

		if(std::find(connection->components.begin(), connection->components.end(), component_id) == connection->components.end())
			connection->components.push_back(component_id);
	} else {
		for(auto child : expr->exprs)
			recursive_gather_connection_component_options(model, scope, connection_id, child);
	}
}

void
Connection_Registration::process_declaration(Catalog *catalog) {

	match_declaration(decl,
	{
		{Token_Type::quoted_string},
	}, true, 0, true);
	
	set_serial_name(catalog, this);
	auto scope = catalog->get_scope(scope_id);
	auto model = static_cast<Mobius_Model *>(catalog);
	
	for(auto note : decl->notes) {
		if(note->decl.string_value == "grid1d") {
			
			if(type != Connection_Type::unrecognized) {
				note->decl.print_error_header();
				fatal_error("This connection received a type twice.");
			}
			
			match_declaration_base(note, {{Decl_Type::compartment, Decl_Type::index_set}}, 0);
			
			type = Connection_Type::grid1d;
			node_index_set = scope->resolve_argument(Reg_Type::index_set, note->args[1]);
			components.push_back(scope->resolve_argument(Reg_Type::component, note->args[0]));
			
		} else if(note->decl.string_value == "directed_graph") {
			
			if(type != Connection_Type::unrecognized) {
				note->decl.print_error_header();
				fatal_error("This connection received a type twice.");
			}
			
			int which = match_declaration_base(note, {{}, {Decl_Type::index_set}}, -1);
			
			type = Connection_Type::directed_graph;
			if(which == 1)
				edge_index_set = scope->resolve_argument(Reg_Type::index_set, note->args[0]);
			
			regex = static_cast<Regex_Body_AST *>(note->body)->expr;
			
			recursive_gather_connection_component_options(model, scope, id, regex);
			
			if(components.empty()) {
				regex->source_loc.print_error_header();
				fatal_error("At least one component must be involved in a connection.");
			}
			
		} else if(note->decl.string_value == "no_cycles") {
			
			match_declaration_base(note, {{}}, 0);
			
			no_cycles = true;
			
		} else {
			note->decl.print_error_header();
			fatal_error("Unrecognized connection structure type '", note->decl.string_value, "'.");
		}
	}
	
	if(no_cycles && type != Connection_Type::directed_graph) {
		source_loc.print_error_header();
		fatal_error("A 'no_cycles' note only makes sense for a 'directed_graph'.");
	}
	
	if(type == Connection_Type::unrecognized) {
		source_loc.print_error_header();
		fatal_error("This connection did not receive a type.");
	}
	
	has_been_processed = true;
}

void
Loc_Registration::process_declaration(Catalog *catalog) {

	match_declaration(decl, {{Arg_Pattern::loc}});
	
	auto scope = catalog->get_scope(scope_id);
	auto model = static_cast<Mobius_Model *>(catalog);
	
	resolve_loc_decl_argument(model, scope, decl->args[0], *this);
	
	has_been_processed = true;
}

void
process_load_library_declaration(Mobius_Model *model, Decl_AST *decl, Entity_Id to_scope, String_View load_decl_path);

void
load_library(Mobius_Model *model, Entity_Id to_scope, String_View rel_path, String_View load_decl_path, std::string &decl_name, Source_Location load_loc) {
	
	Entity_Id lib_id = load_top_decl_from_file(model, load_loc, rel_path, load_decl_path, decl_name, Decl_Type::library);

	auto lib = model->libraries[lib_id];
	
	if(!lib->has_been_processed && !lib->is_being_processed) {
		
		// @ CATALOG_REFACTOR : This inner part could be put in Registration<Reg_Type::library>::process_declaration maybe, but it is a bit awkward due to pointer invalidation.
		
		// To not go into an infinite loop if we have a recursive load, we have to mark this and then skip future loads of it in the same recursive call.
		lib->is_being_processed = true;
		
		match_declaration(lib->decl, {{Token_Type::quoted_string}}, false, -1);
		
		auto body = static_cast<Decl_Body_AST *>(lib->decl->body);
		
		if(body->doc_string.string_value.count)
			lib->doc_string = body->doc_string.string_value;
		
		lib->scope.import(model->global_scope);
		
		// It is important to process the contents of the library before we do subsequent loads, otherwise some contents may not be loaded if we short-circuit due to a recursive load.
		//   Note that the code of the functions is processed at a much later state, so we don't need the symbols in the function bodies to be available yet.
		for(Decl_AST *child : body->child_decls) {
			if(child->type == Decl_Type::load) continue; // processed below.
			
			model->register_decls_recursive(&lib->scope, child, { Decl_Type::constant, Decl_Type::function, Decl_Type::unit, Decl_Type::compose_unit, Decl_Type::unit_of });
		}
		
		// Check recursively for library loads into the current library.
		for(Decl_AST *child : body->child_decls) {
			if(child->type == Decl_Type::load)
				process_load_library_declaration(model, child, lib_id, lib->normalized_path);
		}
		
		lib = model->libraries[lib_id]; // NOTE: Have to re-fetch it because the pointer may have been invalidated by registering more libraries.
		
		for(auto id : lib->scope.all_ids)
			model->find_entity(id)->process_declaration(model);
		
		lib->has_been_processed = true;
	}
	
	model->get_scope(to_scope)->import(lib->scope, &load_loc);
}

void
process_load_library_declaration(Mobius_Model *model, Decl_AST *decl, Entity_Id to_scope, String_View load_decl_path) {
	int which = match_declaration(decl,
		{
			{Token_Type::quoted_string, { Decl_Type::library, true } },
			{{Decl_Type::library, true}},
		}, false);
	String_View file_name;
	String_View relative_to;
	if(which == 0) {
		file_name = single_arg(decl, 0)->string_value;
		relative_to = load_decl_path;
	} else {
		file_name = load_decl_path;   // Load another library from the same file.
		relative_to = "";
	}
	
	int offset = (which == 0 ? 1 : 0);  // If the library names start at argument 0 or 1 of the load decl.
	
	for(int idx = offset; idx < decl->args.size(); ++idx) {
		auto lib_load_decl = decl->args[idx]->decl;
		match_declaration(lib_load_decl, {{Token_Type::quoted_string}}, false, false);
		std::string library_name = single_arg(lib_load_decl, 0)->string_value;

		load_library(model, to_scope, file_name, relative_to, library_name, lib_load_decl->source_loc);
	}
}

Entity_Id
process_module_load(Mobius_Model *model, Token *load_name, Entity_Id template_id, Source_Location &load_loc, std::vector<Entity_Id> &load_args, bool inline_declared, String_View identifier) {
	
	auto mod_temp = model->module_templates[template_id];
	bool is_preamble = (mod_temp->decl_type == Decl_Type::preamble);
	
	// TODO: It is a bit superfluous to process the name and version of the module template every time it is specialized.
	//   Could do it in load_top_decl_from_file?
	
	bool allow_identifier = (inline_declared && is_preamble); // Inline declared preambles are allowed to have an identifier.
	auto decl = mod_temp->decl;
	match_declaration(decl,
		{
			{Token_Type::quoted_string, Decl_Type::version},
			{Token_Type::quoted_string, Decl_Type::version, {true}}
		}, allow_identifier, -1);
	
	auto version_decl = decl->args[1]->decl;

	match_declaration(version_decl, {{Token_Type::integer, Token_Type::integer, Token_Type::integer}}, false);
	
	mod_temp->name                 = single_arg(decl, 0)->string_value;
	mod_temp->version.major        = single_arg(version_decl, 0)->val_int;
	mod_temp->version.minor        = single_arg(version_decl, 1)->val_int;
	mod_temp->version.revision     = single_arg(version_decl, 2)->val_int;
	mod_temp->was_inline_declared  = inline_declared;
	
	auto body = static_cast<Decl_Body_AST *>(decl->body);
	
	if(body->doc_string.string_value.count)
		mod_temp->doc_string = body->doc_string.string_value;
	
	// Create a module specialization of the module template:
	
	auto spec_name_token = single_arg(decl, 0);
	if(load_name)
		spec_name_token = load_name;
	
	std::string spec_name = spec_name_token->string_value;
	
	auto model_scope = &model->top_scope;

	auto module_id = model_scope->deserialize(spec_name, Reg_Type::module);
	
	if(is_valid(module_id)) return module_id; // It has been specialized with this name already, so just return the one that was already created.
	
	module_id = model->modules.create_internal(model_scope, identifier, spec_name, mod_temp->decl_type);
	
	auto module = model->modules[module_id];
	module->source_loc = mod_temp->source_loc;
	
	if(load_name)
		module->full_name = mod_temp->name + " (" + module->name + ")";
	else
		module->full_name = module->name;
	
	module->scope.parent_id = module_id;
	module->template_id = template_id;
	module->scope.import(model->global_scope);
	
	if(inline_declared)
		module->scope.import(*model_scope, &load_loc, true);  // The 'true' is to signify that we also import parameters (that would otherwise be a double import since they come from a par_group scope)
	
	if(inline_declared && decl->args.size() > 2) {
		decl->source_loc.print_error_header();
		fatal_error("Inlined module declarations should not have load arguments.\n");
	}
	
	int required_args = decl->args.size() - 2;
	if(load_args.size() != required_args) {
		load_loc.print_error_header();
		error_print("The module \"", mod_temp->name, "\" requires ", required_args, " load arguments. ", ((required_args > load_args.size()) ? "Only " : "As many as "), load_args.size(), " were passed. See declaration at\n");
		decl->source_loc.print_error();
		mobius_error_exit();
	}
	
	for(int idx = 0; idx < required_args; ++idx) {
		
		Entity_Id load_id = load_args[idx];
		
		auto arg = decl->args[idx + 2];
		if(!arg->decl || !is_valid(&arg->decl->identifier)) {
			arg->chain[0].print_error_header();
			fatal_error("Load arguments to a module must be of the form  identifier : decl_type.");
		}
		match_declaration(arg->decl, {{}}, true, false); // TODO: Not sure if we should allow passing a name to enforce name match.
		
		bool arg_is_preamble = arg->decl->type == Decl_Type::preamble;
		
		if(is_preamble && arg_is_preamble) {
			arg->source_loc().print_error_header();
			fatal_error("A preamble can't load another preamble.");
		}
		
		std::string identifier = arg->decl->identifier.string_value;
		auto *entity = model->find_entity(load_id);
		if(arg->decl->type != entity->decl_type) {
			load_loc.print_error_header();
			error_print("Load argument ", idx, " to the module ", mod_temp->name, " should have type '", name(arg->decl->type), "'. A '", name(entity->decl_type), "' was passed instead. See declaration at\n");
			decl->source_loc.print_error();
			mobius_error_exit();
		}
		
		if(!arg_is_preamble) {
			// For a regular argument, associate the identifier with the id of the passed argument inside this scope.
			auto reg = module->scope.add_local(identifier, arg->decl->source_loc, load_id, false);
			reg->is_load_arg = true;
		} else {
			// TODO: What do we do with the identifier of the load argument in this case?
			
			// If it is a handle to a preamble, instead load the scope of the preamble into this scope.
			auto preamble = model->modules[load_id];
			module->scope.import(preamble->scope, &arg->decl->source_loc, true);
			// The 'true' signifies that we allow imports of parameters (they are not local to the preamble scope since they are in a nested par_group scope.
			// TODO: It is a bit problematic that this will also import parameters that were passed as load arguments to the preamble, which we don't really want.
		}
	}
	
	const std::set<Decl_Type> allowed_types = is_preamble ? std::set<Decl_Type> {
		// Allowed in preambles.
		Decl_Type::property,
		Decl_Type::par_group,
		Decl_Type::unit,
		Decl_Type::compose_unit,
		Decl_Type::unit_of,
		Decl_Type::function,
		Decl_Type::constant,
	} : std::set<Decl_Type> {
		// Allowed in regular modules.
		Decl_Type::property,
		Decl_Type::connection,
		Decl_Type::loc,
		Decl_Type::par_group,
		Decl_Type::constant, 
		Decl_Type::function, 
		Decl_Type::unit,
		Decl_Type::compose_unit,
		Decl_Type::unit_of,
		Decl_Type::flux,
		Decl_Type::var,
		Decl_Type::discrete_order,
		Decl_Type::external_computation,
	};
	
	for(Decl_AST *child : body->child_decls) {
		
		if(child->type == Decl_Type::load)
			process_load_library_declaration(model, child, module_id, mod_temp->normalized_path);
		else 
			model->register_decls_recursive(&module->scope, child, allowed_types);
	}
	
	// Note: The declarations are processed in a specific order so that later ones can access the data of earlier ones.
	//   For instance, when processing a var, it is very convenient that all components that can go into a var location (property, connection...) are already processed.
	
	for(auto id : module->scope.all_ids) {
		if(id.reg_type == Reg_Type::component || id.reg_type == Reg_Type::connection || id.reg_type == Reg_Type::unit || id.reg_type == Reg_Type::par_group) {
			model->find_entity(id)->process_declaration(model);
		}
	}
	
	for(auto id : module->scope.by_type(Reg_Type::loc)) {
		model->find_entity(id)->process_declaration(model);
	}
	
	for(auto id : module->scope.all_ids) {
		auto entity = model->find_entity(id);
		if(!entity->has_been_processed)
			entity->process_declaration(model);
	}
	
	module->has_been_processed = true;
	
	return module_id;
}

void
Index_Set_Registration::process_main_decl(Catalog *catalog) {
	
	// Factored out to be reused in Data_Set
	
	set_serial_name(catalog, this);
	auto scope = catalog->get_scope(scope_id);
	
	for(auto note : decl->notes) {
		auto str = note->decl.string_value;
		if(str == "sub") {
			match_declaration_base(note, {{Decl_Type::index_set}}, 0);
		
			auto subto_id = scope->resolve_argument(Reg_Type::index_set, note->args[0]);
			auto subto = catalog->index_sets[subto_id];
			
			// TODO: A bit hacky, but hard to resolve otherwise.
			//   we *could* scan all the index sets first, then figure out which ones are going to be parents, and register those first, but it is tricky because we would have
			//   to factor that into the recursive registration and make that code ugly.
			if(subto_id.id > id.id) {
				note->decl.print_error_header();
				fatal_error("For technical reasons, an index set must be declared after an index sets it is sub-indexed to.");
			}
			
			sub_indexed_to = subto_id;
			if(std::find(subto->union_of.begin(), subto->union_of.end(), id) != subto->union_of.end()) {
				decl->source_loc.print_error_header();
				fatal_error("An index set can not be sub-indexed to something that is a union of it.");
			}
		} else if(str == "union") {
			match_declaration_base(note, {{Decl_Type::index_set, {Decl_Type::index_set, true}}}, 0);
			
			for(int argidx = 0; argidx < note->args.size(); ++argidx) {
				
				auto union_set = scope->resolve_argument(Reg_Type::index_set, note->args[argidx]);
				if(union_set == id || std::find(union_of.begin(), union_of.end(), union_set) != union_of.end()) {
					decl->args[argidx]->source_loc().print_error_header();
					fatal_error("Any index set can only appear once in a union.");
				}
				union_of.push_back(union_set);
			}
		} else {
			note->decl.print_error_header();
			fatal_error("Unrecognized note '", str, "' for 'index_set' declaration.");
		}
	}
	
	has_been_processed = true;
}

void
Index_Set_Registration::process_declaration(Catalog *catalog) {
	
	match_declaration(decl, {{Token_Type::quoted_string}}, true, 0, true);
	
	process_main_decl(catalog);
}

void
Solver_Function_Registration::process_declaration(Catalog *catalog) {

	// TODO: Make it possible to load solver functions dynamically from dlls.
	fatal_error(Mobius_Error::internal, "Unimplemented: Solver_Function_Registration::process_declaration");
}

void
Solver_Registration::process_declaration(Catalog *catalog) {
	
	int which = match_declaration(decl, {
		{Token_Type::quoted_string, Decl_Type::solver_function, Decl_Type::unit},
		{Token_Type::quoted_string, Decl_Type::solver_function, Decl_Type::unit, Token_Type::real},
		{Token_Type::quoted_string, Decl_Type::solver_function, Decl_Type::par_real},
		{Token_Type::quoted_string, Decl_Type::solver_function, Decl_Type::par_real, Decl_Type::par_real},
	});
	
	set_serial_name(catalog, this);
	auto scope = catalog->get_scope(scope_id);
	auto model = static_cast<Mobius_Model *>(catalog);
	
	solver_fun = scope->resolve_argument(Reg_Type::solver_function, decl->args[1]);
	hmin = 0.01;
	
	if(which == 0 || which == 1) {
		h_unit = scope->resolve_argument(Reg_Type::unit, decl->args[2]);
		if(which == 1)
			hmin = single_arg(decl, 3)->double_value();
	} else {
		h_par = scope->resolve_argument(Reg_Type::parameter, decl->args[2]);
		if(which == 3) {
			hmin_par = scope->resolve_argument(Reg_Type::parameter, decl->args[3]);
			auto unit = model->parameters[hmin_par]->unit;
			if(is_valid(unit) && !model->units[unit]->data.standard_form.is_fully_dimensionless()) {
				decl->args[3]->source_loc().print_error_header();
				fatal_error("The unit of a parameter giving the relative minimal step size of a solver must be dimensionless.");
			}
		}
		if(model->parameters[h_par]->decl_type != Decl_Type::par_real || (is_valid(hmin_par) && model->parameters[hmin_par]->decl_type != Decl_Type::par_real)) {
			decl->source_loc.print_error_header();
			fatal_error("Solvers can only be parametrized with parameters of type 'par_real'.");
		}
	}
	
	has_been_processed = true;
}

void
process_solve_declaration(Mobius_Model *model, Decl_Scope *scope, Decl_AST *decl) {

	match_declaration(decl, {{Decl_Type::solver, {Arg_Pattern::loc, true}}}, false);
	
	auto solver_id = scope->resolve_argument(Reg_Type::solver, decl->args[0]);
	auto solver    = model->solvers[solver_id];
	
	for(int idx = 1; idx < decl->args.size(); ++idx) {
		Var_Location loc;
		resolve_simple_loc_argument(model, scope, decl->args[idx], loc);
	
		solver->locs.push_back({loc, decl->args[idx]->source_loc()});
	}
}

void
process_aggregation_weight_declaration(Mobius_Model *model, Decl_Scope *scope, Decl_AST *decl) {
	int which = match_declaration(decl,
		{
			{Decl_Type::compartment, Decl_Type::compartment},
			{Decl_Type::compartment, Decl_Type::compartment, Decl_Type::connection}
		}, false, -1);
	
	auto from_comp = scope->resolve_argument(Reg_Type::component, decl->args[0]);
	auto to_comp   = scope->resolve_argument(Reg_Type::component, decl->args[1]);
	Entity_Id connection = invalid_entity_id;
	if(which == 1)
		connection = scope->resolve_argument(Reg_Type::connection, decl->args[2]);
	
	if(model->components[from_comp]->decl_type != Decl_Type::compartment || model->components[to_comp]->decl_type != Decl_Type::compartment) {
		decl->source_loc.print_error_header(Mobius_Error::model_building);
		fatal_error("Aggregations can only be declared between compartments");
	}
	
	//TODO: some guard against overlapping / contradictory declarations.
	auto function = static_cast<Function_Body_AST *>(decl->body);
	model->components[from_comp]->aggregations.push_back({to_comp, connection, function->block, scope->parent_id});
}

void
process_unit_conversion_declaration(Mobius_Model *model, Decl_Scope *scope, Decl_AST *decl) {
	match_declaration(decl, {{Arg_Pattern::loc, Arg_Pattern::loc}}, false, -1);
	
	Flux_Unit_Conversion_Data data = {};

	resolve_simple_loc_argument(model, scope, decl->args[0], data.source);
	resolve_simple_loc_argument(model, scope, decl->args[1], data.target);
	
	data.code = static_cast<Function_Body_AST *>(decl->body)->block;
	data.scope_id = scope->parent_id;
	
	//TODO: some guard against overlapping / contradictory declarations.
	if(data.source == data.target) {
		decl->source_loc.print_error_header();
		fatal_error("The source and target of a 'unit_conversion' should not be the same.");
	}
	
	model->components[data.source.first()]->unit_convs.push_back(data);
}

struct
Model_Extension {
	std::string normalized_path;
	Decl_AST *decl;
	int load_order;
	int depth;
	std::set<std::pair<Decl_Type, std::string>> excludes;
};

bool
should_exclude_decl(Model_Extension &extend, Decl_AST *decl) {
	
	for(auto &exclude : extend.excludes) {
		if(
			(exclude.first == decl->type) &&
			(exclude.second.empty() || (decl->identifier.string_value == exclude.second.c_str()) ) 
		)
			return true;
	}
	
	return false;
}

bool
load_model_extensions(File_Data_Handler *handler, Decl_AST *from_decl,
	std::unordered_set<std::string> &loaded_paths, std::vector<Model_Extension> &loaded_decls, String_View rel_path, int *load_order, Model_Extension *from_extension) {
	
	auto body = static_cast<Decl_Body_AST *>(from_decl->body);
	
	for(Decl_AST *child : body->child_decls) {
		if(child->type != Decl_Type::extend) continue;
		
		match_declaration(child, {{Token_Type::quoted_string}}, false, 0, true);
		String_View extend_file_name = single_arg(child, 0)->string_value;
		
		// TODO: It is a bit unnecessary to load it if we are going to delete it. Could instead compute the normalized_path first.
		std::string normalized_path;
		auto extend_model = read_catalog_ast_from_file(Decl_Type::model, handler, extend_file_name, rel_path, &normalized_path);
		
		if(loaded_paths.find(normalized_path) != loaded_paths.end()) {
			begin_error(Mobius_Error::parsing);
			error_print("There is circularity in the model extensions:\n", extend_file_name, "\n");
			delete extend_model;
			return false;
		}
		
		match_declaration(extend_model, {{Token_Type::quoted_string}}, false, -1);
		
		Model_Extension new_extension = {};
		new_extension.normalized_path = normalized_path;
		new_extension.decl = extend_model;
		new_extension.load_order = *load_order;
		if(from_extension) {
			new_extension.depth = from_extension->depth + 1;
			new_extension.excludes = from_extension->excludes; // Include recursively excluded loads from above.
		}
		
		for(auto note : child->notes) {
			if(note->decl.string_value != "exclude") {
				note->decl.print_error_header();
				fatal_error("Unrecognized note type '", note->decl.string_value, "' for 'extend' declaration.");
			}
			match_declaration_base(note, {{true}}, 0);
			for(auto arg : note->args) {
				if(!arg->decl) {
					arg->source_loc().print_error_header();
					fatal_error("Expected only arguments of declaration type to an 'exclude' note.");
				}
				match_declaration(arg->decl, {{}}, true, 0);
				std::string identifier = "";
				if(is_valid(&arg->decl->identifier))
					identifier = arg->decl->identifier.string_value;
				new_extension.excludes.emplace(arg->decl->type, identifier);
			}
		}
		
		// NOTE: this is for tracking circularity. We have to reset it for each branch we go down in the extension tree (but we allow multiple branches to extend the same model)
		auto loaded_paths_sub = loaded_paths;
		loaded_paths_sub.insert(normalized_path);
		
		// Make sure to only load it once.
		bool found = false;
		for(auto &extend : loaded_decls) {
			if(extend.normalized_path == normalized_path) {
				found = true;
				break;
			}
		}
		if(found)
			delete extend_model;
		else {
			loaded_decls.push_back(new_extension);
			
			(*load_order)++;
			// Load extensions of extensions.
			bool success = load_model_extensions(handler, extend_model, loaded_paths_sub, loaded_decls, normalized_path, load_order, &new_extension);
			if(!success) {
				error_print(extend_file_name, "\n");
				return false;
			}
		}
	}
	
	return true;
}

bool mobius_developer_mode = false;   // NOTE: This is a global that is accessible in the entire mobius project, see mobius_common.h

Mobius_Config
load_config(String_View file_name) {
	
	Mobius_Config config;
	
	File_Data_Handler files;
	
	auto file_data = files.load_file(file_name);
	Token_Stream stream(file_name, file_data);
	while(true) {
		Token token = stream.peek_token();
		if(token.type == Token_Type::eof) break;
		Decl_AST *decl = parse_decl(&stream);
		if(decl->type != Decl_Type::config) {
			decl->source_loc.print_error_header();
			fatal_error("Unexpected declaration type '", name(decl->type), "' in a config file.");
		}
		match_declaration(decl, {{Token_Type::quoted_string, {false}}}, false);
		auto item = single_arg(decl, 0)->string_value;
		if(item == "Mobius2 base path") {
			match_declaration(decl, {{Token_Type::quoted_string, Token_Type::quoted_string}}, false);
			
			config.mobius_base_path = single_arg(decl, 1)->string_value;
			struct stat info;
			if(stat(config.mobius_base_path.data(), &info) != 0 || !(info.st_mode & S_IFDIR)) {
				single_arg(decl, 0)->print_error_header();
				fatal_error("The path \"", config.mobius_base_path, "\" is not a valid directory path.");
			}
		} else if(item == "Developer mode") {
			match_declaration(decl, {{Token_Type::quoted_string, Token_Type::boolean}}, false);
			
			config.developer_mode = single_arg(decl, 1)->val_bool;
		} else if(item == "Just store all the series") {
			match_declaration(decl, {{Token_Type::quoted_string, Token_Type::boolean}}, false);
			
			config.store_all_series = single_arg(decl, 1)->val_bool;
		} else if(item == "Store transport fluxes") {
			match_declaration(decl, {{Token_Type::quoted_string, Token_Type::boolean}}, false);
			
			config.store_transport_fluxes = single_arg(decl, 1)->val_bool;
		} else {
			decl->source_loc.print_error_header();
			fatal_error("Unknown config option \"", item, "\".");
		}
		delete decl;
	}
	
	if(config.developer_mode)
		log_print(Log_Mode::dev, file_name, ": Configured to developer mode.\n");
	
	return std::move(config);
}

struct
Module_Load {
	Decl_AST    *module_spec;
	bool         is_inline;
	String_View  file_name;
	std::string  loaded_from;
	
	Module_Load(Decl_AST *module_spec, bool is_inline, String_View file_name, const std::string &loaded_from)
		: module_spec(module_spec), is_inline(is_inline), file_name(file_name), loaded_from(loaded_from) {}
};

void
pre_register_module_loads(Catalog *catalog, Decl_Scope *scope, Decl_AST *load_decl, 
	std::vector<Module_Load> &module_loads, const std::string &loaded_from) {
	
	// NOTE: This one actually only checks inline decls, not ones you pass as identifiers.
	//   This is why we don't for instance have par_real here since it can only be declared inside a par_group.
	const std::set<Decl_Type> allowed_load_arg_types = {
		Decl_Type::property,
		Decl_Type::compartment,
		Decl_Type::quantity,
		Decl_Type::loc,
		Decl_Type::connection,
		Decl_Type::unit,
		Decl_Type::compose_unit,
		Decl_Type::unit_of,
		Decl_Type::constant
	};
	
	String_View file_name = single_arg(load_decl, 0)->string_value;
	
	for(int idx = 1; idx < load_decl->args.size(); ++idx) {
		auto module_load_decl = load_decl->args[idx]->decl;
		if(!module_load_decl) {
			load_decl->args[idx]->source_loc().print_error_header();
			fatal_error("This is not a valid module load declaration.");
		}
		
		module_loads.emplace_back(module_load_decl, false, file_name, loaded_from); // false signifies it is not an inline decl but a load.
		
		// NOTE: We can't call register_decls_recursive on module_load_decl since we only want it to process the arguments.
		for(auto arg : module_load_decl->args) {
			if(arg->decl)
				catalog->register_decls_recursive(scope, arg->decl, allowed_load_arg_types);
		}
	}
}

void
basic_checks_and_finalization(Mobius_Model *model) {
	
	// Find out what unit each unit_of unit refers to.
	for(auto unit_id : model->units) {
		auto unit = model->units[unit_id];
		if(unit->decl && unit->decl->type == Decl_Type::unit_of) {
			if(is_valid(unit->unit_of)) {
				if(unit->unit_of.reg_type == Reg_Type::parameter)
					unit->refers_to = model->parameters[unit->unit_of]->unit;
				else if(unit->unit_of.reg_type == Reg_Type::constant)
					unit->refers_to = model->constants[unit->unit_of]->unit;
			} else {
				// NOTE: We can't use final resolved units yet from the model composition, but we also can't wait for that stage since that stage needs the units to have been resolved already. Hopefully this doesn't cause problems.
				// Hmm, maybe we should have some kind of lookup hash table for Var_Location -> Entity_Id also then?
				for(auto var_id : model->vars) {
					auto var = model->vars[var_id];
					if(var->var_location == unit->unit_of_loc) {
						unit->refers_to = var->unit;
						break;
					}
				}
			}
		}
	}
	
	// We have to resolve compose_unit and unit_of units in the correct order (if they depend on one another) so we have to sort them first.
	struct
	Unit_Sorting_Predicate {
		Mobius_Model *model;
		inline bool participates(Entity_Id node) {
			auto unit = model->units[node];
			return unit->decl && (unit->decl->type == Decl_Type::compose_unit || unit->decl->type == Decl_Type::unit_of);
		}
		// A bit annoying that we have to return a vector copy..
		inline std::vector<Entity_Id> edges(Entity_Id node) {
			auto unit = model->units[node];
			if(unit->decl->type == Decl_Type::compose_unit)
				return unit->composed_of;
			return { unit->refers_to };
		}
	} unit_sorting_predicate = { model };
	
	std::vector<Entity_Id> sorted_units;
	topological_sort<Unit_Sorting_Predicate, Entity_Id>(unit_sorting_predicate, sorted_units, Entity_Id {Reg_Type::unit, (s16)model->units.count()}, [model](const std::vector<Entity_Id> &cycle) {
		model->units[cycle[0]]->source_loc.print_error_header();
		fatal_error("There is a circular 'compose_unit' or 'unit_of' reference involving the above declaration.");
	}, Entity_Id { Reg_Type::unit, 0} );
	
	for(auto unit_id : sorted_units) {
		auto unit = model->units[unit_id];

		if(unit->decl->type == Decl_Type::compose_unit) { // unit->decl  is nullptr if this is an intrinsic.
			for(auto other_id : unit->composed_of)
				unit->data = multiply(unit->data, model->units[other_id]->data);
		} else if (unit->decl->type == Decl_Type::unit_of) {
			if(is_valid(unit->refers_to))
				unit->data = model->units[unit->refers_to]->data;
		}
	}
	
	for(auto conn_id : model->connections) {
		auto conn = model->connections[conn_id];
		if(conn->type == Connection_Type::grid1d) {
			auto index_set = conn->node_index_set;
			if(!model->index_sets[index_set]->union_of.empty()) {
				conn->source_loc.print_error_header(Mobius_Error::model_building);
				fatal_error("A 'grid1d' connection can not be placed over a union index set.");
			}
		}
	}
	
	for(auto index_set_id : model->index_sets) {
		auto index_set = model->index_sets[index_set_id];
		if(is_valid(index_set->sub_indexed_to)) {
			auto super = model->index_sets[index_set->sub_indexed_to];
			if(is_valid(super->sub_indexed_to)) {
				index_set->source_loc.print_error_header(Mobius_Error::model_building);
				fatal_error("We currently don't support sub-indexing an index set to another index set that is again sub-indexed.");
			}
			if(!index_set->union_of.empty()) {
				index_set->source_loc.print_error_header(Mobius_Error::model_building);
				fatal_error("A union index set should not be sub-indexed to anything.");
			}
		}
		for(auto ui_id : index_set->union_of) {
						
			auto ui = model->index_sets[ui_id];
			if(is_valid(ui->sub_indexed_to)) {
				// TODO: We could maybe support it if the union and all the union members are sub-indexed to the same thing.
				index_set->source_loc.print_error_header(Mobius_Error::model_building);
				fatal_error("We currently don't support union index sets of sub-indexed index sets.");
			}
			if(!ui->union_of.empty()) {
				// Although we could maybe just 'flatten' the union. That would have to be done in model_declaration post-processing.
				index_set->source_loc.print_error_header(Mobius_Error::model_building);
				fatal_error("We currently don't support unions of unions.");
			}
		}
	}
	
	for(auto group_id : model->par_groups) {
		auto group = model->par_groups[group_id];
		for(auto comp_id : group->components) {
			if(model->components[comp_id]->decl_type == Decl_Type::property) {
				group->source_loc.print_error_header();
				fatal_error("A 'group' can not be attached to a 'property', only to a 'compartment' or 'quantity'.");
			}
		}
		
		// Compile the maximal tuple of index sets for the par group.
		for(auto comp_id : group->components) {
			for(auto index_set : model->components[comp_id]->index_sets)
				insert_dependency(model, group->max_index_sets, index_set);
		}
		for(auto index_set : group->direct_index_sets)
			insert_dependency(model, group->max_index_sets, index_set);
	}
}

void
process_module_load_outer(Mobius_Model *model, Module_Load &load) {
	
	auto scope = &model->top_scope;
	auto module_spec = load.module_spec;
		
	if(load.is_inline) {
		// Inline module sub-scope inside the model declaration.
		auto template_id = model->module_templates.register_decl(scope, module_spec);
		auto mod_temp = model->module_templates[template_id];
		mod_temp->decl = module_spec;
		mod_temp->normalized_path = load.loaded_from;
		auto load_loc = module_spec->source_loc;
		
		std::vector<Entity_Id> load_args; // Inline modules don't have load arguments, so this should be left empty.
		process_module_load(model, nullptr, template_id, load_loc, load_args, true, module_spec->identifier.string_value);
	} else {
		
		bool allow_identifier = (module_spec->type == Decl_Type::preamble);
		
		int which = match_declaration(module_spec, 
			{
				{Token_Type::quoted_string, Token_Type::quoted_string},
				{Token_Type::quoted_string, Token_Type::quoted_string, {true}},
				{Token_Type::quoted_string},
				{Token_Type::quoted_string, {true}}
			}, allow_identifier, false);
		
		auto load_loc = module_spec->source_loc;
		auto module_name = single_arg(module_spec, 0)->string_value;
		Token *load_name = nullptr;
		if(which <= 1)
			load_name = single_arg(module_spec, 1);
		
		auto template_id = load_top_decl_from_file(model, load_loc, load.file_name, load.loaded_from, module_name, module_spec->type);
		
		std::vector<Entity_Id> load_args;
		int args_start_at = 1;
		if(which == 0 || which == 1)
			args_start_at = 2;
		// TODO: This one gives an undecipherable error if this is an argument that can't be resolved in this way
		// (e.g. it is a literal)
		for(int argidx = args_start_at; argidx < module_spec->args.size(); ++argidx)
			// Reg_Type::unrecognized means 'any' in this case. Note that we already screened for allowed types earlier.
			load_args.push_back(scope->resolve_argument(Reg_Type::unrecognized, module_spec->args[argidx]));
		
		auto module_id = process_module_load(model, load_name, template_id, load_loc, load_args, false, module_spec->identifier.string_value);
	}
}

Mobius_Model *
load_model(String_View file_name, Mobius_Config *config) {
	
	Mobius_Model *model = new Mobius_Model();
	
	if(config)
		model->config = *config;
	else 
		model->config = load_config();
	
	mobius_developer_mode = model->config.developer_mode;
	
	auto decl = read_catalog_ast_from_file(Decl_Type::model, &model->file_handler, file_name);
	match_declaration(decl, {{Token_Type::quoted_string}}, false, -1);
	model->main_decl  = decl;
	model->model_name = single_arg(decl, 0)->string_value;
	model->path       = file_name;
	
	auto body = static_cast<Decl_Body_AST *>(decl->body);
	
	if(body->doc_string.string_value.count)
		model->doc_string = body->doc_string.string_value;
	
	register_intrinsics(model);

	std::unordered_set<std::string> loaded_files = { file_name };
	std::vector<Model_Extension> extend_models = { { file_name, decl, 0, 0} };
	int order = 1;
	bool success = load_model_extensions(&model->file_handler, decl, loaded_files, extend_models, file_name, &order, nullptr);
	if(!success)
		mobius_error_exit();
	
	std::sort(extend_models.begin(), extend_models.end(), [](const Model_Extension &extend1, const Model_Extension &extend2) -> bool {
		if(extend1.depth == extend2.depth) return extend1.load_order < extend2.load_order;
		return extend1.depth > extend2.depth;
	});

	auto scope = &model->top_scope;
	
	// Declarations allowed in the model top scope.
	// Not including declarations that don't create an entity registration (solve, ...) . These are handled separately.
	const std::set<Decl_Type> allowed_model_decls = {
		Decl_Type::property,
		Decl_Type::compartment,
		Decl_Type::quantity,
		Decl_Type::loc,
		Decl_Type::connection,
		Decl_Type::constant,
		Decl_Type::par_group,
		Decl_Type::unit,
		Decl_Type::function,
		Decl_Type::index_set,
		Decl_Type::solver,
	};
	
	std::vector<Decl_AST *>  special_decls;
	std::vector<Module_Load> module_loads;
	
	for(auto &extend : extend_models) {
		auto ast = extend.decl;
		auto body = static_cast<Decl_Body_AST *>(ast->body);
		
		for(Decl_AST *child : body->child_decls) {
			
			if(should_exclude_decl(extend, child)) continue;
			
			if(child->type == Decl_Type::load) {
				
				int which = match_declaration(child, 
					{
						{Token_Type::quoted_string, {Decl_Type::module, true}},
						{Token_Type::quoted_string, {Decl_Type::library, true}},
					}, false);
				
				if(which == 0) {
					// Register inlined load arguments only. The actual load is processed later.
					pre_register_module_loads(model, scope, child, module_loads, extend.normalized_path);
				} else
					process_load_library_declaration(model, child, scope->parent_id, file_name);
			} else if (child->type == Decl_Type::extend) {
				// Do nothing. This was handled already.
			} else if (child->type == Decl_Type::module || child->type == Decl_Type::preamble) {
				// Inline module declaration. These are handled later.
				module_loads.emplace_back(child, true, "", file_name);  // true signifies it is an inline decl.
				
			} else if (child->type == Decl_Type::solve || child->type == Decl_Type::unit_conversion || child->type == Decl_Type::aggregation_weight) {
				// These don't create registrations on their own.. They will be processed after other declarations below.
				special_decls.push_back(child);
				// We still may have to handle inline decls in arguments.
				for(auto arg : child->args) {
					if(arg->decl)
						model->register_decls_recursive(scope, arg->decl, allowed_model_decls);
				}
			} else {
				model->register_decls_recursive(scope, child, allowed_model_decls);
			}
		}
	}
	
	// NOTE: The declarations are processed in this order because the processing of some types can depend on the data of other ones.
	for(auto id : scope->by_type(Reg_Type::index_set))
		model->index_sets[id]->process_declaration(model);
	
	for(auto id : scope->all_ids) {
		if(id.reg_type == Reg_Type::component || id.reg_type == Reg_Type::connection || id.reg_type == Reg_Type::par_group || id.reg_type == Reg_Type::constant || id.reg_type == Reg_Type::unit || id.reg_type == Reg_Type::function) {
			auto entity = model->find_entity(id);
			if(!entity->has_been_processed) // Note: happens if it was internally created.
				entity->process_declaration(model);
		}
	}
	
	for(auto id : scope->all_ids) {
		if(id.reg_type == Reg_Type::loc || id.reg_type == Reg_Type::solver)
			model->find_entity(id)->process_declaration(model);
	}
	
	// TODO: Instead do this for all the registries at the end (make a Catalog function for reuse in Data_Set). Then we will also catch missed processing inside modules and libraries.
	for(auto id : scope->all_ids) {
		if(!model->find_entity(id)->has_been_processed)
			fatal_error(Mobius_Error::internal, "Failed to process declaration of type ", name(id.reg_type), ".");
	}
	
	// Special decls that don't cause registrations but instead modify other ones.
	for(auto decl : special_decls) {
		if      (decl->type == Decl_Type::solve)
			process_solve_declaration(model, scope, decl);
		else if (decl->type == Decl_Type::aggregation_weight)
			process_aggregation_weight_declaration(model, scope, decl);
		else if (decl->type == Decl_Type::unit_conversion)
			process_unit_conversion_declaration(model, scope, decl);
	}
	
	// Finally, load modules. Load preambles first since they could be referenced by other modules.
	// TODO: This splits up the sorting of modules we did earlier though....
		// Maybe that system should be rewritten to take into account what references what instead? Probably too much effort..
	for(auto &load : module_loads) {
		if(load.module_spec->type != Decl_Type::preamble) continue;
		process_module_load_outer(model, load);
	}
	for(auto &load : module_loads) {
		if(load.module_spec->type != Decl_Type::module) continue;
		process_module_load_outer(model, load);
	}
	
	basic_checks_and_finalization(model);
	
	return model;
}

Var_Location
remove_dissolved(const Var_Location &loc) {
	Var_Location result = loc;
	if(!loc.is_dissolved())
		fatal_error(Mobius_Error::internal, "Tried to find a variable location above one that is not dissolved in anything.");
	result.n_components--;
	return result;
}

Var_Location
add_dissolved(const Var_Location &loc, Entity_Id quantity) {
	Var_Location result = loc;
	if(loc.n_components == max_var_loc_components)
		fatal_error(Mobius_Error::internal, "Tried to find a variable location with a dissolved chain that is too long.");
	result.n_components++;
	result.components[result.n_components-1] = quantity;
	return result;
}

void
check_valid_distribution(Mobius_Model *model, std::vector<Entity_Id> &index_sets, Source_Location &err_loc) {
	
	// TODO: This overlaps with functionality in index_data.h. Do we need both?
	
	int idx = 0;
	for(auto id : index_sets) {
		auto set = model->index_sets[id];
		if(is_valid(set->sub_indexed_to)) {
			bool found = (std::find(index_sets.begin(), index_sets.begin()+idx, set->sub_indexed_to) != index_sets.begin()+idx);
			auto parent_set = model->index_sets[set->sub_indexed_to];
			if(!found && !parent_set->union_of.empty()) {
				for(auto ui_id : parent_set->union_of) {
					found = (std::find(index_sets.begin(), index_sets.begin()+idx, ui_id) != index_sets.begin()+idx);
					if(found) break;
				}
			}
			if(!found) {
				err_loc.print_error_header();
				error_print("The index set \"", set->name, "\" is sub-indexed to another index set \"", model->index_sets[set->sub_indexed_to]->name, "\", but the parent index set (or a union member of it) does not precede it in this distribution. See the declaration of the index sets here:");
				set->source_loc.print_error();
				mobius_error_exit();
			}
		}
		if(!set->union_of.empty()) {
			for(auto ui_id : set->union_of) {
				if(std::find(index_sets.begin(), index_sets.end(), ui_id) != index_sets.end()) {
					err_loc.print_error_header();
					error_print("The index set \"", set->name, "\" is a union consisting among others of the index set \"", model->index_sets[ui_id]->name, "\", but both appear in the same distribution. See the declaration of the index sets here:");
					set->source_loc.print_error();
					mobius_error_exit();
				}
			}
		}
		++idx;
	}
}

void
Decl_Scope::check_for_unreferenced_things(Catalog *catalog) {
	std::unordered_map<Entity_Id, bool, Entity_Id_Hash> lib_was_used;
	for(auto &pair : visible_entities) {
		auto &reg = pair.second;
		
		if(reg.is_load_arg && !reg.was_referenced) {
			log_print(Log_Mode::dev, "Warning: In ");
			reg.source_loc.print_log_header(Log_Mode::dev);
			log_print(Log_Mode::dev, "The module argument '", reg.identifier, "' was never referenced.\n");
		}
		
		if (!reg.is_load_arg && !reg.external && !reg.was_referenced && reg.id.reg_type == Reg_Type::parameter) {
			log_print(Log_Mode::dev, "Warning: In ");
			reg.source_loc.print_log_header(Log_Mode::dev);
			log_print(Log_Mode::dev, "The parameter '", reg.identifier, "' was never referenced.\n");
		}
		
		if(reg.external) {
			auto entity = catalog->find_entity(reg.id);
			if(entity->scope_id.reg_type == Reg_Type::library) {
				if(reg.was_referenced)
					lib_was_used[entity->scope_id] = true;
				else {
					auto find = lib_was_used.find(entity->scope_id);
					if(find == lib_was_used.end())
						lib_was_used[entity->scope_id] = false;
				}
			}
		}
	}
	
	// NOTE: Skipping check of library loading library. Otherwise, if a library A loads library B but the parts of A that reference B are not used in the current model, the warning below will fire for A loading B. This is because unused library code is not fully processed. This skip can be removed if we find a better solution.
	if(parent_id.reg_type == Reg_Type::library) return;
	
	std::string scope_type;
	std::string scope_name;
	if(parent_id.reg_type == Reg_Type::module) {
		scope_type = "module";
		scope_name = catalog->find_entity(parent_id)->name;
	} else if(parent_id.reg_type == Reg_Type::library) {
		scope_type = "library";
		scope_name = catalog->find_entity(parent_id)->name;
	} else {
		scope_type = "model";
		scope_name = catalog->model_name;
	}
	
	for(auto &pair : lib_was_used) {
		if(!pair.second) {
			// TODO: How would we find the load source location of the library in this module? That is probably not stored anywhere right now.
			// TODO:  we could put that in the Scope_Entity source_loc (?)
			log_print(Log_Mode::dev, "Warning: The ", scope_type, " \"", scope_name, "\" loads the library \"", catalog->find_entity(pair.first)->name, "\", but doesn't use any of it.\n");
		}
	}
}

// NOTE: would like to just have an ostream& operator<< on the Var_Location, but it needs to reference the scope to get the names..
void
error_print_location(Mobius_Model *model, const Specific_Var_Location &loc) {
	for(int idx = 0; idx < loc.n_components; ++idx)
		error_print(model->get_symbol(loc.components[idx]), idx == loc.n_components-1 ? "" : ".");
	// TODO: This should also print the bracket (restriction)
}

void
log_print_location(Mobius_Model *model, const Specific_Var_Location &loc) {
	for(int idx = 0; idx < loc.n_components; ++idx)
		log_print(model->get_symbol(loc.components[idx]), idx == loc.n_components-1 ? "" : ".");
	// TODO: This should also print the bracket (restriction)
}

void
Mobius_Model::free_asts() {
	// NOTE: All other ASTs, eg. function code, are sub-trees of one of the ones that we delete below, so we should not delete them separately (though we could maybe null them).
	
	delete main_decl;
	main_decl = nullptr;
	
	// NOTE: For inline-declared modules, their AST is a part of the model AST and should not be deleted separately.
	for(auto id : module_templates) {
		auto temp = module_templates[id];
		if(temp->was_inline_declared) continue;
		delete temp->decl;
		temp->decl = nullptr;
	}
	for(auto id : libraries) {
		delete libraries[id]->decl;
		libraries[id]->decl = nullptr;
	}
}

void
insert_dependency(Mobius_Model *model, Index_Set_Tuple &index_sets, Entity_Id index_set) {
	
	if(index_sets.has(index_set)) return;
	
	// Replace an existing index set with a union member if applicable
	// TODO: Is there a more efficient way to do this: ? It should be very rare also.
	Index_Set_Tuple remove;
	for(auto existing : index_sets) {
		auto set = model->index_sets[existing];
		if(!set->union_of.empty()) {
			auto find = std::find(set->union_of.begin(), set->union_of.end(), index_set);
			if(find != set->union_of.end()) {
				// TODO:    It may be better to give an error in this case rather than allowing the overwrite. We'll see how it works out.
				remove.insert(existing);
			}
		}
	}
	index_sets.remove(remove);
	index_sets.insert(index_set);
}