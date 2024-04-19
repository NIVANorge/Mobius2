
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iomanip>
#include "../src/model_declaration.h"


const char *preamble = R""""(---
layout: default
title: _TITLE_
parent: Autogenerated documentation
grand_parent: Existing models
nav_order: _NAVORDER_
---

# _TITLE_

This is auto-generated documentation based on the model code in [models/_MODELFILE_](https://github.com/NIVANorge/Mobius2/blob/main/models/_MODELFILE_) .
Since the modules can be dynamically loaded with different arguments, this does not necessarily reflect all use cases of the modules.

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
unit_str(Mobius_Model *model, Entity_Id unit_id, bool enclose = true) {
	if(is_valid(unit_id)) {
		auto unit = model->units[unit_id];
		if(enclose) {
			return "$$" + unit->data.to_latex() + "$$";
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
	return 1000'000;
}


// Oops, it is problematic that we don't store in the declaration if a Var_Location is from a 'loc'!
// In the docs we want to print that, not the resolved location...
// Although we could just go back to the AST??
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
};

void
print_oper(Print_Equation_Context &context, Token_Type oper) {
	auto c = (char)oper;
	if(c == '*')
		context.ss << "\\cdot ";
	else
		context.ss << c;
	// TODO: Probably have to handle many other types!
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

// TODO: Also have to make a precedence system like in the other print_expression.
// Could we unify code with that??
void
print_equation(Print_Equation_Context &context, Math_Expr_AST *ast, bool outer = false) {
	if(ast->type == Math_Expr_Type::block) {
		if(outer) {
			for(int i = 0; i < ast->exprs.size(); ++i) {
				print_equation(context, ast->exprs[i]);
				if(i != (int)ast->exprs.size()-1)
					context.ss << " \\\\ ";
			}
		} else {
			context.ss << "\\mathrm{expr}";
		}
	} else if (ast->type == Math_Expr_Type::binary_operator) {
		auto binop = static_cast<Binary_Operator_AST *>(ast);
		auto c = (char)binop->oper;
		if(c == '/') {
			context.ss << "\\frac{";
			print_equation(context, binop->exprs[0]);
			context.ss << "}{";
			print_equation(context, binop->exprs[1]);
			context.ss << "}";
		} else if(c == '^') {
			print_equation(context, binop->exprs[0]);
			context.ss << "^{";
			print_equation(context, binop->exprs[1]);
			context.ss << "}";
		} else {
			print_equation(context, binop->exprs[0]);
			print_oper(context, binop->oper);
			print_equation(context, binop->exprs[1]);
		}
	} else if (ast->type == Math_Expr_Type::unary_operator) {
		
		
		
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
		print_chain(context, ident->chain);
		if(!ident->bracketed_chain.empty()) {
			context.ss << "\\[";
			print_chain(context, ident->bracketed_chain);
			context.ss << "\\]";
		}
	} else if (ast->type == Math_Expr_Type::literal) {
		auto literal = static_cast<Literal_AST *>(ast);
		auto &v = literal->value;
		if(v.type == Token_Type::integer)
			context.ss << v.val_int;
		else if(v.type == Token_Type::real)
			context.ss << v.val_double;    // TODO: We want to format it better
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
			context.ss << "\\mathrm{log}_{10}\left(";
			print_equation(context, fun->exprs[0]);
			context.ss << "\right)";
		} else {
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
	std::stringstream ss;
	print_equation(context, code, true);
	return ss.str();
}

void
document_module(std::stringstream &ss, Mobius_Model *model, std::string &module_name) {
	auto module_id = model->deserialize(module_name, Reg_Type::module);
	auto module = model->modules[module_id];
	auto modtemplate = model->module_templates[module->template_id];
	
	ss << "## " << module->name << "\n\n";
	auto &ver = modtemplate->version;
	ss << "Version: " << ver.major << "." << ver.minor << "." << ver.revision << "\n\n";
	if(!modtemplate->doc_string.empty()) {
		// TODO: Ideally this should be put as a quote
		ss << "### Description" << "\n\n" << modtemplate->doc_string << "\n\n";
	}
	
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
	
	auto groups = module->scope.by_type<Reg_Type::par_group>();
	if(groups.size() > 0) {
		
		ss << "### Parameters\n\n";
		ss << "| Name | Symbol | Unit |  Description |\n";
		ss << "| ---- | ------ | ---- |  ----------- |\n";
		
		for(auto group_id : groups) {
			auto group = model->par_groups[group_id];
			ss << "| **" << group->name << "** | | | |\n";
			
			for(auto par_id : group->scope.by_type<Reg_Type::parameter>()) {
				auto par = model->parameters[par_id];
				//if(par->decl_type != Decl_Type::par_real) continue; //TODO!
				
				ss << "| " << par->name 
				   << " | " << model->get_symbol(par_id)
				   << " | " << unit_str(model, par->unit)
				   //<< " | " << par->default_val.val_real
				   //<< " | " << par->min_val.val_real
				   //<< " | " << par->max_val.val_real
				   << " | " << par->description   // TODO: Ideally we should check that there are no markdown characters in it..
				   << " |\n";
			}
		}
		ss << "\n";
	}
	
	auto vars = module->scope.by_type<Reg_Type::var>();
	if(vars.size() > 0) {
		ss << "### State variables\n\n";
		
		for(auto var_id : vars) {
			auto var = model->vars[var_id];
			
			std::string locstr = loc_str(&module->scope, var->var_location);
			
			if(var->var_name.empty()) {
				ss << "#### *" << model->components[var->var_location.last()]->name << "*\n\n";
			} else
				ss << "#### *" << var->var_name << "*\n\n";
			
			ss << "Location: **" << locstr << "**\n\n";
			ss << "Unit: " << unit_str(model, var->unit) << "\n\n";
			if(is_valid(var->conc_unit))
				ss << "Conc. unit: " << unit_str(model, var->conc_unit) << "\n\n";
			
			Print_Equation_Context context;
			context.outer_unit = unit_str(model, var->unit, false);
			
			if(var->code || var->override_code) {
				auto code = var->code ? var->code : var->override_code;
				ss << "Value:\n\n";
				ss << "$$\n" << equation_str(context, code) << "\n$$\n\n";
			}
			if(var->initial_code) {
				ss << "Initial value:\n\n";
				ss << "$$\n" << equation_str(context, var->initial_code) << "\n$$\n\n";
			}
			if(!var->code && !var->override_code && !var->initial_code && !var->adds_code_to_existing) {
				ss << "This series is externally defined. It may be an input series.\n\n";
			}
		}
	}
	
	auto fluxes = module->scope.by_type<Reg_Type::flux>();
	if(fluxes.size() > 0) {
		ss << "### Fluxes\n\n";
		
		for(auto flux_id : fluxes) {
			auto flux = model->fluxes[flux_id];
			
			ss << "#### *" << flux->name << "*\n\n";
			
			ss << "Source: (to be implemented)\n\n";
			ss << "Target: (to be implemented)\n\n";
			ss << "Unit: " << unit_str(model, flux->unit) << "\n\n";
			
			Print_Equation_Context context;
			context.outer_unit = unit_str(model, flux->unit, false);
			
			ss << "Value:\n\n";
			ss << "$$\n" << equation_str(context, flux->code) << "\n$$\n\n";
			
			// TODO: Other info such as no_carry etc.
		}
	}
}

void
generate_docs(int navorder, std::vector<const char *> &tuple) {
	
	std::stringstream infile;
	infile << "../models/" << tuple[1];
	std::string infilestr = infile.str();
	
	Mobius_Model *model = load_model(infilestr.c_str());
	
	std::stringstream ss;
	
	std::string head = preamble;
	replace(head, "_TITLE_", tuple[0]);
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
	replace(head, "_MODELFILE_", tuple[1]);
	
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


int
main() {
	
	// TODO: Read these from a file:
	std::vector<std::vector<const char *>> models = {
		{"SimplyQ", "simplyq_model.txt", "SimplyQ land", "SimplyQ river"},
		{"SimplyC", "simplyc_model.txt", "SimplyC land", "SimplyC river"},
	};
	
	for(int i = 0; i < models.size(); ++i)
		generate_docs(i, models[i]);
	
}