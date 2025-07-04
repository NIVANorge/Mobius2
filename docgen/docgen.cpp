
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iomanip>
#include "../src/model_declaration.h"


bool allow_logging = true;

const char *preamble = R""""(---
layout: default
title: _TITLE_
parent: Mathematical description
grand_parent: Existing models
nav_order: _NAVORDER_
---

# _TITLE_

This is auto-generated documentation based on the model code in [models/_MODELFILE_](https://github.com/NIVANorge/Mobius2/blob/main/models/_MODELFILE_) .
Since the modules can be dynamically loaded with different arguments, this documentation does not necessarily reflect all use cases of the modules.

See the note on [notation](autogen.html#notation).

The file was generated at _DATE_.

)"""";


const char *library_preamble = R""""(---
layout: default
title: Standard library
parent: Mathematical description
grand_parent: Existing models
nav_order: _NAVORDER_
---

# The Mobius2 standard library

This is auto-generated documentation based on the Mobius2 standard library in [Mobius2/stdlib](https://github.com/NIVANorge/Mobius2/blob/main/stdlib) .

The standard library provides common functions and constants for many models.

See the note on [notation](autogen.html#notation).

The file was generated at _DATE_.

)"""";


void
replace(std::string &str, const char *substring, const char *replacement) {
	size_t len = strlen(substring);
	size_t len2 = strlen(replacement);
	size_t index = 0;
	while (true) {
		index = str.find(substring, index);
		if (index == std::string::npos) break;
		str.replace(index, len, replacement);
		index += len2;
	}
}

std::string
unit_str(Mobius_Model *model, Entity_Id unit_id, bool utf8 = true) {
	if(is_valid(unit_id)) {
		auto unit = model->units[unit_id];
		if(utf8) {
			return unit->data.to_utf8();
		}
		return unit->data.to_latex();
	}
	return "";
}


int
precedence(Math_Expr_AST *expr) {
	if(expr->type == Math_Expr_Type::binary_operator) {
		auto binop = static_cast<Binary_Operator_AST *>(expr);
		return operator_precedence(binop->oper);
	}
	if(expr->type == Math_Expr_Type::unary_operator)
		return 50'000;
	if(expr->type == Math_Expr_Type::if_chain || expr->type == Math_Expr_Type::block)
		return 4500;
	return 1000'000;
}


// Oops, it is problematic that we don't store in the declaration if a Var_Location is from a 'loc'!
// In the docs we want to print that, not the resolved location...
// Although we could just go back to the AST?? - this is what we ended up doing it looks like.
std::string
loc_str(Decl_Scope *scope, Var_Location &loc) {
	std::stringstream ss;
	if(loc.type == Var_Location::Type::out)
		return "out";
	else if(loc.type == Var_Location::Type::connection)
		return "(connection)";   // TODO!
	else {
		bool begin = true;
		for(int i = 0; i < loc.n_components; ++i) {
			if(!begin) ss << '.';
			auto find = scope->identifiers.find(loc.components[i]);
			if(find == scope->identifiers.end())
				fatal_error(Mobius_Error::internal, "Unable to find identifier in the scope!");
			ss << find->second;
			begin = false;
		}
		return ss.str();
	}
}

struct
Print_Equation_Context {
	std::stringstream ss;
	std::string outer_unit;
	Mobius_Model *model;
	Decl_Scope *scope;
};

void
print_oper(Print_Equation_Context &context, Token_Type oper) {
	auto c = (char)oper;
	if(c == '*')
		context.ss << "\\cdot ";
	else if(c == '|')
		context.ss << "\\;\\text{or}\\;";
	else if(c == '&')
		context.ss << "\\;\\text{and}\\;";
	else if(c == '!')
		context.ss << "\\;\\text{not}\\;";
	else if(oper == Token_Type::leq)
		context.ss << "\\leq ";
	else if(oper == Token_Type::geq)
		context.ss << "\\geq ";
	else if(oper == Token_Type::neq)
		context.ss << "\\neq ";
	else
		context.ss << c;
}

void
print_ident(Print_Equation_Context &context, String_View ident) {
	std::string res = ident;
	if(res == "pi")
		context.ss << "\\pi";
	else {
		replace(res, "_", "\\_");
		context.ss << "\\mathrm{" << res << "}";
	}
}

void
print_chain(Print_Equation_Context &context, std::vector<Token> &chain) {
	bool begin = true;
	for(auto &t : chain) {
		if(!begin) context.ss << '.';
		print_ident(context, t.string_value);
		begin = false;
	}
}

void
print_chain_alt(std::stringstream &ss, std::vector<Token> &chain) {
	// This one is for the non-equation side of it
	bool begin = true;
	for(auto &t : chain) {
		if(!begin) ss << '.';
		ss << t.string_value;
		begin = false;
	}
}

void
print_double_chain(std::stringstream &ss, std::vector<Token> &chain, std::vector<Token> &bracketed_chain) {
	// This one is for the non-equation side of it
	print_chain_alt(ss, chain);
	if(!bracketed_chain.empty()) {
		ss << "\[";
		print_chain_alt(ss, bracketed_chain);
		ss << "\]";
	}
}

void
print_unit_ast(Print_Equation_Context &context, Unit_Convert_AST *conv, bool print_identity) {
	if(conv->unit) {
		Unit_Data data;
		data.set_data(conv->unit);
		std::string str = data.to_latex();
		// TODO: Must be more robust. If it is another number, should insert a \cdot also.
		if(!print_identity && str=="1")
			return;
		context.ss << str;
	} else if (conv->by_identifier) {
		print_ident(context, conv->unit_identifier.string_value);
	} else if (conv->auto_convert) {
		context.ss << context.outer_unit;
	} else
		fatal_error(Mobius_Error::internal, "Unrecognized unit conversion type.");
}

std::string
format_double_tex(double d) {
	std::stringstream ss;
	ss << d;
	std::string str = ss.str();
	auto find = str.find("e");
	if(find == std::string::npos)
		return str;
	auto base = str.substr(0, find);
	
	bool is_minus_power = false;
	if(str.data()[find+1] == '-') {
		is_minus_power = true;
		++find;
	}
	if(str.data()[find+1] == '+')
		++find;
	std::string power = str.substr(find+1, str.size());
	// Remove leading zeros.
	power.erase(0, std::min(power.find_first_not_of('0'), power.size()-1)); 
	
	if(base == "1")
		return std::string("10^{") + (is_minus_power?"-":"") + power + "}";
	
	return base + "\\cdot 10^{" + power + "}";
}

void
print_equation(Print_Equation_Context &context, Math_Expr_AST *ast, bool outer = false) {
	if(ast->type == Math_Expr_Type::block) {
		if(ast->exprs.size() == 1) {
			print_equation(context, ast->exprs[0]);
		} else {
			if(!outer)
				context.ss << "\\begin{pmatrix}";
			for(int i = 0; i < ast->exprs.size(); ++i) {
				print_equation(context, ast->exprs[i]);
				if(i != (int)ast->exprs.size()-1)
					context.ss << " \\\\ ";
			}
			if(!outer)
				context.ss << "\\end{pmatrix}";
		}
	} else if (ast->type == Math_Expr_Type::binary_operator) {
		auto binop = static_cast<Binary_Operator_AST *>(ast);
		auto c = (char)binop->oper;
		int prec = precedence(binop);
		int prec0 = precedence(binop->exprs[0]);
		int prec1 = precedence(binop->exprs[1]);
		
		if(c == '/') {
			context.ss << "\\frac{";
			print_equation(context, binop->exprs[0]);
			context.ss << "}{";
			print_equation(context, binop->exprs[1]);
			context.ss << "}";
		} else if(c == '^') {
			if(prec0 < prec)
				context.ss << "\\left(";
			print_equation(context, binop->exprs[0]);
			if(prec0 < prec)
				context.ss << "\\right)";
			context.ss << "^{";
			print_equation(context, binop->exprs[1]);
			context.ss << "}";
		} else {
			if(prec0 < prec)
				context.ss << "\\left(";
			print_equation(context, binop->exprs[0]);
			if(prec0 < prec)
				context.ss << "\\right)";
			
			print_oper(context, binop->oper);
			
			if(prec1 < prec)
				context.ss << "\\left(";
			print_equation(context, binop->exprs[1]);
			if(prec1 < prec)
				context.ss << "\\right)";
		}
	} else if (ast->type == Math_Expr_Type::unary_operator) {
		int prec = precedence(ast);
		int prec0 = precedence(ast->exprs[0]);
		auto unary = static_cast<Unary_Operator_AST *>(ast);
		print_oper(context, unary->oper);
		
		if(prec0 < prec)
			context.ss << "\\left(";
		print_equation(context, unary->exprs[0]);
		if(prec0 < prec)
			context.ss << "\\right)";
		
	} else if (ast->type == Math_Expr_Type::cast) {
		
		print_equation(context, ast->exprs[0], outer);
		
	} else if (ast->type == Math_Expr_Type::unit_convert) {
		
		// TODO: Should this also adhere to operator precedence?
		
		auto conv = static_cast<Unit_Convert_AST*>(ast);
		
		if(ast->exprs[0]->type == Math_Expr_Type::literal) {
			print_equation(context, ast->exprs[0]);
			context.ss << " ";
			print_unit_ast(context, conv, false);
		} else {
			context.ss << "\\left(";
			print_equation(context, ast->exprs[0]);
			if(conv->force)
				context.ss << "\\Rightarrow ";
			else
				context.ss << "\\rightarrow ";
			print_unit_ast(context, conv, true);
			context.ss << "\\right)";
		}
		
	} else if (ast->type == Math_Expr_Type::identifier) {
		auto ident = static_cast<Identifier_Chain_AST *>(ast);
		if(ident->chain.size() == 1 && ident->chain[0].string_value == "no_override") {
			context.ss << "\\text{(mass balance)}";
		} else {
			print_chain(context, ident->chain);
			if(!ident->bracketed_chain.empty()) {
				context.ss << "\\lbrack";
				print_chain(context, ident->bracketed_chain);
				context.ss << "\\rbrack";
			}
		}
	} else if (ast->type == Math_Expr_Type::literal) {
		auto literal = static_cast<Literal_AST *>(ast);
		auto &v = literal->value;
		if(v.type == Token_Type::integer)
			context.ss << v.val_int;
		else if(v.type == Token_Type::real)
			context.ss << format_double_tex(v.val_double);
		else if(v.type == Token_Type::boolean)
			context.ss << (v.val_bool ? "\\matrm{true}" : "\\mathrm{false}");
		else
			fatal_error(Mobius_Error::internal, "Unhandled literal type");
	} else if (ast->type == Math_Expr_Type::local_var) {
		auto local = static_cast<Local_Var_AST *>(ast);
		print_ident(context, local->name.string_value);
		context.ss << " = ";
		print_equation(context, ast->exprs[0]);
	} else if (ast->type == Math_Expr_Type::function_call) {
		auto fun = static_cast<Function_Call_AST *>(ast);
		if(fun->name.string_value == "exp") {
			context.ss << "e^{";
			print_equation(context, fun->exprs[0]);
			context.ss << "}";
		} else if(fun->name.string_value == "sqrt") {
			context.ss << "\\sqrt{";
			print_equation(context, fun->exprs[0]);
			context.ss << "}";
		} else if (fun->name.string_value == "cbrt") {
			context.ss << "\\sqrt[3]{";
			print_equation(context, fun->exprs[0]);
			context.ss << "}";
		} else if (fun->name.string_value == "log10") {
			context.ss << "\\mathrm{log}_{10}\\left(";
			print_equation(context, fun->exprs[0]);
			context.ss << "\\right)";
		} else if (fun->name.string_value == "abs") {
			context.ss << "\\left|";
			print_equation(context, fun->exprs[0]);
			context.ss << "\\right|";
		} else {
			std::string fun_name = fun->name.string_value;
			auto reg = (*context.scope)[fun_name];
			if(reg) {  // It doesn't find reg if it is a special directive.
				
				auto funreg = context.model->functions[reg->id];
				
				if(funreg->fun_type == Function_Type::decl && funreg->scope_id.reg_type == Reg_Type::library) {
					auto lib = context.model->libraries[funreg->scope_id];
					
					std::string libident = lib->name;
					std::transform(libident.begin(), libident.end(), libident.begin(), [](unsigned char c){ return std::tolower(c); });
					replace(libident, " ", "-");
					
					context.ss << "\\href{stdlib.html#" << libident << "}{";
					print_ident(context, fun->name.string_value);
					context.ss << "}";
				} else
					print_ident(context, fun->name.string_value);
			} else
				print_ident(context, fun->name.string_value);
			
			context.ss << "\\left(";
			bool first = true;
			for(auto expr : fun->exprs) {
				if(!first) context.ss << ",\\, ";
				print_equation(context, expr);
				first = false;
			}
			context.ss << "\\right)";
		}
	} else if (ast->type == Math_Expr_Type::if_chain) {
		context.ss << "\\begin{cases}";
		int n_cases = ((int)ast->exprs.size() - 1)/2;
		bool first = true;
		for(int i = 0; i < n_cases; ++i) {
			if(!first) context.ss << " \\\\ ";
			print_equation(context, ast->exprs[2*i]);
			context.ss << " & \\text{if}\\;";
			print_equation(context, ast->exprs[2*i + 1]);
			first = false;
		}
		context.ss << " \\\\ ";
		print_equation(context, ast->exprs.back());
		context.ss << " & \\text{otherwise}";
		context.ss << "\\end{cases}";
	} else {
		context.ss << "\\mathrm{expr}";
	}
}

std::string
equation_str(Print_Equation_Context &context, Math_Expr_AST *code) {
	context.ss.str("");
	print_equation(context, code, true);
	return context.ss.str();
}


void
print_function_definition(Mobius_Model *model, Decl_Scope *scope, Entity_Id fun_id, std::stringstream &ss) {
	
	auto fun = model->functions[fun_id];
	ss << "**" << model->get_symbol(fun_id) << "(";
	bool first = true;
	for(int i = 0; i < fun->args.size(); ++i) {
		if(!first) ss << ", ";
		ss << fun->args[i];
		if(is_valid(fun->expected_units[i])) {
			auto unit = model->units[fun->expected_units[i]];
			if(!unit->data.standard_form.is_fully_dimensionless())
				ss << " : " << unit_str(model, fun->expected_units[i]);
		}
		first = false;
	}
	ss << ")** = \n\n";
	
	Print_Equation_Context context;
	context.model = model;
	context.scope = scope;
	
	ss << "$$\n";

	ss << equation_str(context, fun->code);
	
	ss << "\n$$\n\n";
}

void
print_constants(std::stringstream &ss, Mobius_Model *model, Decl_Scope *scope) {
	auto constants = scope->by_type(Reg_Type::constant);
	if(constants.size() > 0) {
		
		ss << "### Constants\n\n";
		ss << "| Name | Symbol | Unit | Value |\n";
		ss << "| ---- | ------ | ---- | ----- |\n";
		
		for(auto const_id : constants) {
			auto con = model->constants[const_id];
			ss << "| " << con->name << " | **" << model->get_symbol(const_id) << "** | " << unit_str(model, con->unit) << " | ";
			if(con->value_type == Value_Type::real) {
				ss << con->value.val_real;
			} else if (con->value_type == Value_Type::boolean) {
				ss << con->value.val_boolean;
			} else
				fatal_error(Mobius_Error::internal, "Unsupported constant value type.");
			ss << " |\n";
		
		}
		ss << "\n";
	}
}


void
document_module(std::stringstream &ss, Mobius_Model *model, std::string &module_name) {
	
	std::cout << "\tmodule \"" << module_name << "\"\n";
	
	auto module_id = model->deserialize(module_name, Reg_Type::module);
	auto module = model->modules[module_id];
	auto modtemplate = model->module_templates[module->template_id];
	
	ss << "## " << module->name << "\n\n";
	auto &ver = modtemplate->version;
	ss << "Version: " << ver.major << "." << ver.minor << "." << ver.revision << "\n\n";
	
	// Hmm, this is a bit volatile since it assumes the module was loaded as "modules/...". Should instead try to locate it?
	std::string file = modtemplate->source_loc.filename;
	ss << "File: [" << file << "](https://github.com/NIVANorge/Mobius2/tree/main/models/" << file << ")\n\n";
	
	if(!modtemplate->doc_string.empty()) {
		// TODO: Ideally this should be put as a quote
		ss << "### Description" << "\n\n" << modtemplate->doc_string << "\n\n";
	}
	
	int count_vis = 0;
	for(auto &pair : module->scope.visible_entities) {
		auto &record = pair.second;
		if(!record.is_load_arg) continue;
		++count_vis;
	}
	
	if(count_vis > 0) {
		ss << "### External symbols\n\n";
		ss << "| Name | Symbol | Type |\n";
		ss << "| ---- | ------ | ---- |\n";
		for(auto &pair : module->scope.visible_entities) {
			auto &symbol = pair.first;
			auto &record = pair.second;
			auto entity = model->find_entity(record.id);
			
			if(!record.is_load_arg) continue;
			
			ss << "| " << entity->name << " | **" << symbol << "** | " << name(entity->decl_type) << " |\n";
		}
		ss << "\n";
	}
	
	print_constants(ss, model, &module->scope);
	
	auto funs = module->scope.by_type(Reg_Type::function);
	if(funs.size() > 0) {
		ss << "### Module functions\n\n";
		
		for(auto fun_id : funs) {
			print_function_definition(model, &module->scope, fun_id, ss);
		}
	}
	
	auto groups = module->scope.by_type(Reg_Type::par_group);
	if(groups.size() > 0) {
		
		ss << "### Parameters\n\n";
		ss << "| Name | Symbol | Unit |  Description |\n";
		ss << "| ---- | ------ | ---- |  ----------- |\n";
		
		for(auto group_id : groups) {
			auto group = model->par_groups[group_id];
			std::string possible_distr;
			if(!group->components.empty()) {
				possible_distr = "Distributes like: ";
				bool first = true;
				for(auto id : group->components) {
					if(!first) possible_distr += ", ";
					possible_distr += "`" + model->get_symbol(id) + "`";
					first = false;
				}
			}
			ss << "| **" << group->name << "** | | | " << possible_distr << " |\n";
			
			for(auto par_id : group->scope.by_type(Reg_Type::parameter)) {
				auto par = model->parameters[par_id];
				
				std::string unit;
				if(par->decl_type == Decl_Type::par_enum) {
					unit = "Possible values: ";
					bool first = true;
					for(auto &val : par->enum_values) {
						if(!first) unit += ", ";
						unit += "`" + val + "`";
						first = false;
					}
				} else {
					unit = unit_str(model, par->unit);
				}
				
				ss << "| " << par->name 
				   << " | **" << model->get_symbol(par_id)
				   << "** | " << unit
				   //<< " | " << par->default_val.val_real
				   //<< " | " << par->min_val.val_real
				   //<< " | " << par->max_val.val_real
				   << " | " << par->description   // TODO: Ideally we should check that there are no markdown characters in it..
				   << " |\n";
			}
		}
		ss << "\n";
	}
	
	auto vars = module->scope.by_type(Reg_Type::var);
	if(vars.size() > 0) {
		ss << "### State variables\n\n";
		
		for(auto var_id : vars) {
			auto var = model->vars[var_id];
			
			std::string locstr = loc_str(&module->scope, var->var_location);
			
			if(var->var_name.empty()) {
				ss << "#### **" << model->components[var->var_location.last()]->name << "**\n\n";
			} else
				ss << "#### **" << var->var_name << "**\n\n";
			
			ss << "Location: **" << locstr << "**\n\n";
			ss << "Unit: " << unit_str(model, var->unit) << "\n\n";
			if(is_valid(var->conc_unit))
				ss << "Conc. unit: " << unit_str(model, var->conc_unit) << "\n\n";
			
			Print_Equation_Context context;
			context.model = model;
			context.scope = &module->scope;
			context.outer_unit = unit_str(model, var->unit, false);
			
			if(var->code || var->override_code) {
				auto code = var->code ? var->code : var->override_code;
				ss << "Value";
				if(var->override_code && var->override_is_conc)
					ss << " (concentration)";
				ss << ":\n\n";
				ss << "$$\n" << equation_str(context, code) << "\n$$\n\n";
			}
		
			if(var->initial_code) {
				ss << "Initial value";
				if(var->initial_is_conc)
					ss << " (concentration)";
				ss << ":\n\n";
				ss << "$$\n" << equation_str(context, var->initial_code) << "\n$$\n\n";
			}
			if(!var->code && !var->override_code && !var->initial_code && !var->adds_code_to_existing) {
				if(model->components[var->var_location.last()]->decl_type == Decl_Type::property)
					ss << "This series is externally defined. It may be an input series.\n\n";
			}
		}
	}
	
	auto fluxes = module->scope.by_type(Reg_Type::flux);
	if(fluxes.size() > 0) {
		ss << "### Fluxes\n\n";
		
		for(auto flux_id : fluxes) {
			auto flux = model->fluxes[flux_id];
			
			ss << "#### **" << flux->name << "**\n\n";
			
			ss << "Source: ";
			auto source = flux->decl->args[0];
			print_double_chain(ss, source->chain, source->bracketed_chain);
			ss << "\n\n";
			ss << "Target: ";
			auto target = flux->decl->args[1];
			print_double_chain(ss, target->chain, target->bracketed_chain);
			ss << "\n\n";
			ss << "Unit: " << unit_str(model, flux->unit) << "\n\n";
			if(flux->mixing)
				ss << "This is a mixing flux (affects dissolved quantities only)\n\n";
			
			Print_Equation_Context context;
			context.model = model;
			context.scope = &module->scope;
			context.outer_unit = unit_str(model, flux->unit, false);
			
			ss << "Value:\n\n";
			ss << "$$\n" << equation_str(context, flux->code) << "\n$$\n\n";
			
			// TODO: Other info such as no_carry etc.
		}
	}
}

void
generate_docs(int navorder, std::vector<std::string> &tuple) {
	
	std::cout << "Documenting " << tuple[0] << std::endl;
	
	std::stringstream infile;
	infile << "../models/" << tuple[1];
	std::string infilestr = infile.str();
	
	Mobius_Model *model = load_model(infilestr.c_str());
	
	std::stringstream ss;
	
	std::string head = preamble;
	replace(head, "_TITLE_", tuple[0].c_str());
	auto navorderstr = std::to_string(navorder);
	replace(head, "_NAVORDER_", navorderstr.c_str());
	{
		auto t = std::time(nullptr);
		auto tm = *std::localtime(&t);
		std::stringstream date;
		date << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
		std::string datestr = date.str();
		replace(head, "_DATE_", datestr.c_str());
	}
	replace(head, "_MODELFILE_", tuple[1].c_str());
	
	ss << head;
	
	for(int i = 2; i < tuple.size(); ++i) {
		std::string module_name = tuple[i];
		ss << "---\n\n";
		document_module(ss, model, module_name);
	}
	
	ss << "\n\n{% include lib/mathjax.html %}\n\n";
	
	std::string result_file_name = tuple[0];
	std::transform(result_file_name.begin(), result_file_name.end(), result_file_name.begin(), [](unsigned char c){ return std::tolower(c); });
	std::stringstream out_path;
	out_path << "../docs/existingmodels/autogen/" << result_file_name << ".md";
	auto outpathstr = out_path.str();
	
	std::ofstream out;
	out.open(outpathstr.c_str(), std::ios::binary);
	out << ss.str();
	out.close();
}

void
document_library(std::stringstream &ss, Mobius_Model *model, Entity_Id lib_id) {
	
	auto lib = model->libraries[lib_id];
	
	std::cout << "\tlibrary \"" << lib->name << "\"\n";
	
	ss << "## " << lib->name << "\n\n";
	
	// TODO: This is a bit error prone, should just use some util to extract filename from path.
	std::string filename = lib->source_loc.filename;
	if(filename.find("stdlib/") != 0)
		filename = "stdlib/" + filename;
	ss << "File: " << "[" << filename << "](https://github.com/NIVANorge/Mobius2/tree/main/" << filename << ")\n\n";
	
	ss << "### Description\n\n";
	ss << lib->doc_string << "\n\n";
	
	print_constants(ss, model, &lib->scope);
	
	auto funs = lib->scope.by_type(Reg_Type::function);
	if(funs.size() > 0) {
		ss << "### Library functions\n\n";
		
		for(auto fun_id : funs) {
			print_function_definition(model, &lib->scope, fun_id, ss);
		}
	}
}

void
generate_lib_docs(int navorder, const char *filename) {
	
	std::cout << "Documenting libraries" << std::endl;
	
	Mobius_Model *model = load_model(filename);
	
	std::stringstream ss;
	
	std::string head = library_preamble;
	auto navorderstr = std::to_string(navorder);
	replace(head, "_NAVORDER_", navorderstr.c_str());
	{
		auto t = std::time(nullptr);
		auto tm = *std::localtime(&t);
		std::stringstream date;
		date << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
		std::string datestr = date.str();
		replace(head, "_DATE_", datestr.c_str());
	}
	
	ss << head;
	
	for(auto lib_id : model->libraries) {
		ss << "---\n\n";
		document_library(ss, model, lib_id);
	}
	
	ss << "\n\n{% include lib/mathjax.html %}\n\n";
	
	
	std::ofstream out;
	out.open("../docs/existingmodels/autogen/stdlib.md", std::ios::binary);
	out << ss.str();
	out.close();
}


int
main() {
	
	std::vector<std::vector<std::string>> models;
	
	const char *filename = "module_list.txt";
	auto file_data = read_entire_file(filename);
	Token_Stream stream(filename, file_data);
	
	std::vector<std::string> models_line;
	while(true) {
		
		auto name = stream.expect_quoted_string();
		
		models_line.push_back(std::string(name));
		
		auto token = stream.read_token();
		if((char)token.type == ';') {
			models.push_back(models_line);
			models_line.clear();
		} else if((char)token.type != ',') {
			token.print_error_header();
			fatal_error("Expected comma or semicolon.");
		}
		
		auto peek = stream.peek_token();
		if(peek.type == Token_Type::eof) break;
	}
	
	
	for(int i = 0; i < models.size(); ++i)
		generate_docs(i, models[i]);
	
	generate_lib_docs((int)models.size(), "all_the_libraries.txt");
}