
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iomanip>
#include "../src/model_declaration.h"


const char *preamble = R""""(---
layout: default
title: _TITLE_
parent: Existing models
grand_parent: MobiView2
nav_order: _NAVORDER_
---

# _TITLE_

This is auto-generated documentation based on the model code in models/_MODELFILE_ .

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
		
		auto module_id = model->deserialize(module_name, Reg_Type::module);
		auto module = model->modules[module_id];
		auto modtemplate = model->module_templates[module->template_id];
		
		ss << "# " << module->name << "\n\n";
		if(!modtemplate->doc_string.empty()) {
			// TODO: Ideally this should be put as a quote
			ss << "## Docstring" << "\n\n" << modtemplate->doc_string << "\n\n";
		}
		ss << "## Parameters\n\n";
		
		for(auto group_id : module->scope.by_type<Reg_Type::par_group>()) {
			auto group = model->par_groups[group_id];
			ss << "| Name | Symbol | Unit | Default | Min | Max | Description |\n";
			ss << "| ---- | ------ | ---- | ------- | --- | --- | ----------- |\n";
			for(auto par_id : group->scope.by_type<Reg_Type::parameter>()) {
				auto par = model->parameters[par_id];
				if(par->decl_type != Decl_Type::par_real) continue; //TODO!
				
				ss << "| " << par->name 
				   << " | " << model->get_symbol(par_id)
				   << " | " << (is_valid(par->unit) ? model->units[par->unit]->data.to_utf8() : "")
				   << " | " << par->default_val.val_real
				   << " | " << par->min_val.val_real
				   << " | " << par->max_val.val_real
				   << " | " << par->description   // TODO: Ideally we should check that there are no markdown characters in it..
				   << " |\n";
			}
			ss << "\nParameter group: **" << group->name << "**\n\n";
		}
	}
	
	
	std::string result_file_name = tuple[0];
	std::transform(result_file_name.begin(), result_file_name.end(), result_file_name.begin(), [](unsigned char c){ return std::tolower(c); });
	std::stringstream out_path;
	out_path << "../docs/existingmodels/autogen/" << result_file_name << ".md";
	auto outpathstr = out_path.str();
	
	std::ofstream out;
	out.open(outpathstr.c_str());
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