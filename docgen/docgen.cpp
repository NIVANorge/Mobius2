
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
unit_str(Mobius_Model *model, Entity_Id unit_id) {
	return (is_valid(unit_id) ? model->units[unit_id]->data.to_utf8() : "");
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

void
print_oper(std::stringstream &ss, Token_Type oper) {
	auto c = (char)oper;
	if(c == '*')
		ss << "\\cdot";
	else
		ss << c;
	// TODO: Probably have to handle many other types!
}

void
print_ident(std::stringstream &ss, String_View ident) {
	std::string res = ident;
	replace(res, "_", "\\_");
	ss << "\\mathrm{" << res << "}";
}

void
print_chain(std::stringstream &ss, std::vector<Token> &chain) {
	bool begin = true;
	for(auto &t : chain) {
		if(!begin) ss << '.';
		print_ident(ss, t.string_value);
		begin = false;
	}
}

// TODO: Also have to make a precedence system like in the other print_expression.
// Could we unify code with that??
void
print_equation(std::stringstream &ss, Mobius_Model *model, Decl_Scope *scope, Math_Expr_AST *ast, bool outer = false) {
	if(ast->type == Math_Expr_Type::block) {
		if(outer) {
			for(int i = 0; i < ast->exprs.size(); ++i) {
				print_equation(ss, model, scope, ast->exprs[i]);
				if(i != (int)ast->exprs.size()-1)
					ss << " \\\\";
			}
		} else {
			ss << "\\mathrm{expr}";
		}
	} else if (ast->type == Math_Expr_Type::binary_operator) {
		auto binop = static_cast<Binary_Operator_AST *>(ast);
		auto c = (char)binop->oper;
		if(c == '/') {
			ss << "\\frac{";
			print_equation(ss, model, scope, binop->exprs[0]);
			ss << "}{";
			print_equation(ss, model, scope, binop->exprs[1]);
			ss << "}";
		} else if(c == '^') {
			print_equation(ss, model, scope, binop->exprs[0]);
			ss << "^{";
			print_equation(ss, model, scope, binop->exprs[1]);
			ss << "}";
		} else {
			print_equation(ss, model, scope, binop->exprs[0]);
			print_oper(ss, binop->oper);
			print_equation(ss, model, scope, binop->exprs[1]);
		}
	} else if (ast->type == Math_Expr_Type::cast) {
		print_equation(ss, model, scope, ast->exprs[0], outer);
	} else if (ast->type == Math_Expr_Type::unit_convert) {
		
		// TODO: Should this also adhere to operator precedence?
		
		auto conv = static_cast<Unit_Convert_AST*>(ast);
		ss << "\\left(";
		print_equation(ss, model, scope, ast->exprs[0]);
		if(conv->force)
			ss << "\\Rightarrow";
		else
			ss << "\\rightarrow";
		ss << "\\mathrm{some\\_unit}"; //TODO: Need latex formatting of units.
		ss << "\\right)";
		
	} else if (ast->type == Math_Expr_Type::identifier) {
		auto ident = static_cast<Identifier_Chain_AST *>(ast);
		print_chain(ss, ident->chain);
		if(!ident->bracketed_chain.empty()) {
			ss << "\\[";
			print_chain(ss, ident->bracketed_chain);
			ss << "\\]";
		}
	} else if (ast->type == Math_Expr_Type::literal) {
		auto literal = static_cast<Literal_AST *>(ast);
		auto &v = literal->value;
		if(v.type == Token_Type::integer)
			ss << v.val_int;
		else if(v.type == Token_Type::real)
			ss << v.val_double;    // TODO: We want to format it better
		else if(v.type == Token_Type::boolean)
			ss << (v.val_bool ? "\\matrm{true}" : "\\mathrm{false}");
		else
			fatal_error(Mobius_Error::internal, "Unhandled literal type");
	} else if (ast->type == Math_Expr_Type::local_var) {
		auto local = static_cast<Local_Var_AST *>(ast);
		print_ident(ss, local->name.string_value);
		ss << " = ";
		print_equation(ss, model, scope, ast->exprs[0]);
	} else if (ast->type == Math_Expr_Type::function_call) {
		auto fun = static_cast<Function_Call_AST *>(ast);
		if(fun->name.string_value == "exp") {
			ss << "e^{";
			print_equation(ss, model, scope, fun->exprs[0]);
			ss << "}";
		} else {
			print_ident(ss, fun->name.string_value);
			ss << "\\left(";
			bool first = true;
			for(auto expr : fun->exprs) {
				if(!first) ss << ", ";
				print_equation(ss, model, scope, expr);
				first = false;
			}
			ss << "\\right)";
		}
	} else if (ast->type == Math_Expr_Type::if_chain) {
		ss << "\\begin{cases}";
		int n_cases = ((int)ast->exprs.size() - 1)/2;
		bool first = true;
		for(int i = 0; i < n_cases; ++i) {
			if(!first) ss << " \\\\ ";
			print_equation(ss, model, scope, ast->exprs[2*i]);
			ss << " & \\text{ if }";
			print_equation(ss, model, scope, ast->exprs[2*i + 1]);
			first = false;
		}
		ss << " \\\\ ";
		print_equation(ss, model, scope, ast->exprs.back());
		ss << " & \\text{otherwise}";
		ss << "\\end{cases}";
	} else {
		ss << "\\mathrm{expr}";
	}
}

std::string
equation_str(Mobius_Model *model, Decl_Scope *scope, Math_Expr_AST *code) {
	std::stringstream ss;
	print_equation(ss, model, scope, code, true);
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
		ss << "### Docstring" << "\n\n" << modtemplate->doc_string << "\n\n";
	}
	
	ss << "### External symbols\n\n";
	ss << "| Name | Symbol | Type |\n";
	ss << "| ---- | ------ | ---- |\n";
	for(auto &pair : module->scope.visible_entities) {
		auto &symbol = pair.first;
		auto &record = pair.second;
		auto entity = model->find_entity(record.id);
		
		if(!record.is_load_arg) continue;
		
		ss << "| " << entity->name << " | " << symbol << " | " << name(entity->decl_type) << " |\n";
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
			
			ss << "#### " << var->var_name << "\n\n";
			
			ss << "| Location | Unit |";
			if(is_valid(var->conc_unit))
				ss << " Conc. unit |";
			ss << "\n";
			ss << "| -------- | ---- |";
			if(is_valid(var->conc_unit))
				ss << " ---- |";
			ss << "\n";
			
			ss << "| " << locstr
			   << " | " << unit_str(model, var->unit);
			if(is_valid(var->conc_unit))
				ss << " | " << unit_str(model, var->conc_unit);
			ss << " |\n\n";
			
			if(var->code || var->override_code) {
				auto code = var->code ? var->code : var->override_code;
				ss << "Value:\n\n";
				ss << "$$\n" << equation_str(model, &module->scope, code) << "\n$$\n\n";
			}
			if(var->initial_code) {
				ss << "Initial value:\n\n";
				ss << "$$\n" << equation_str(model, &module->scope, var->initial_code) << "\n$$\n\n";
			}
			
			// TODO: Adds to existing
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