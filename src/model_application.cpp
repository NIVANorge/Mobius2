
#include "model_application.h"

#include <map>
#include <string>



void
Model_Application::set_up_parameter_structure(std::unordered_map<Entity_Id, std::vector<Entity_Id>, Hash_Fun<Entity_Id>> *par_group_index_sets) {
	if(parameter_data.has_been_set_up)
		fatal_error(Mobius_Error::internal, "Tried to set up parameter structure twice.");
	if(!all_indexes_are_set())
		fatal_error(Mobius_Error::internal, "Tried to set up parameter structure before all index sets received indexes.");
	
	std::vector<Multi_Array_Structure<Entity_Id>> structure;
	
	std::map<std::vector<Entity_Id>, std::vector<Entity_Id>> par_by_index_sets;
	
	for(auto module : model->modules) {
		for(auto group_id : module->par_groups) {
			std::vector<Entity_Id> *index_sets;
			std::vector<Entity_Id> empty;
			bool found = false;
			if(par_group_index_sets) {
				auto find = par_group_index_sets->find(group_id);
				if(find != par_group_index_sets->end()) {
					found = true;
					index_sets = &find->second;
				}
			}
			if(!found) {
				auto comp_id = module->par_groups[group_id]->compartment;
				if(is_valid(comp_id)) {  // It is invalid for the "System" parameter group.
					auto compartment = module->compartments[comp_id];
					if(is_valid(compartment->global_id))
						compartment = model->modules[0]->compartments[compartment->global_id];
					index_sets = &compartment->index_sets;
				} else
					index_sets = &empty;
			}
		
			for(auto par : module->par_groups[group_id]->parameters)
				par_by_index_sets[*index_sets].push_back(par);
		}
	}
	
	for(auto pair : par_by_index_sets) {
		std::vector<Entity_Id> index_sets = pair.first;
		std::vector<Entity_Id> handles    = pair.second;
		Multi_Array_Structure<Entity_Id> array(std::move(index_sets), std::move(handles));
		structure.push_back(std::move(array));
	}
	
	parameter_data.set_up(std::move(structure));
	parameter_data.allocate();
	
	// Write default parameter values
	for(auto module : model->modules) {
		for(auto par : module->parameters) {
			Parameter_Value val = model->find_entity<Reg_Type::parameter>(par)->default_val;
			parameter_data.for_each(par, [&](std::vector<Index_T> *indexes, s64 offset) {
				*(parameter_data.get_value(offset)) = val;
			});
		}
	}
}

void
Model_Application::set_up_series_structure() {
	if(series_data.has_been_set_up)
		fatal_error(Mobius_Error::internal, "Tried to set up input structure twice.");
	if(!all_indexes_are_set())
		fatal_error(Mobius_Error::internal, "Tried to set up input structure before all index sets received indexes.");
	
	std::vector<Multi_Array_Structure<Var_Id>> structure;
	std::vector<Var_Id> handles;
		for(auto id : model->series) handles.push_back(id);
	
	Multi_Array_Structure<Var_Id> array({}, std::move(handles)); // TODO: index sets when we implement those.
	structure.push_back(std::move(array));
	
	series_data.set_up(std::move(structure));
}

void
Model_Application::set_up_neighbor_structure() {
	if(neighbor_data.has_been_set_up)
		fatal_error(Mobius_Error::internal, "Tried to set up neighbor structure twice.");
	if(!all_indexes_are_set())
		fatal_error(Mobius_Error::internal, "Tried to set up neighbor structure before all index sets received indexes.");
	
	std::vector<Multi_Array_Structure<Neighbor_T>> structure;
	for(auto neighbor_id : model->modules[0]->neighbors) {
		auto neighbor = model->modules[0]->neighbors[neighbor_id];
		
		if(neighbor->type != Neighbor_Structure_Type::directed_tree)
			fatal_error(Mobius_Error::internal, "Unsupported neighbor structure type in set_up_neighbor_structure()");
		
		Neighbor_T handle = { neighbor_id, 0 };  // For now we only support one info point per index, which will be what the index points at.
		std::vector<Neighbor_T> handles { handle };
		Multi_Array_Structure<Neighbor_T> array({neighbor->index_set}, std::move(handles));
		structure.push_back(array);
	}
	neighbor_data.set_up(std::move(structure));
	neighbor_data.allocate();
	
	for(int idx = 0; idx < neighbor_data.total_count; ++idx)
		neighbor_data.data[idx] = -1;                          // To signify that it doesn't point at anything.
};

//TODO: we could remove this one for now:
void 
Model_Application::set_indexes(Entity_Id index_set, Array<String_View> names) {
	index_counts[index_set.id] = {index_set, (s32)names.count};
	//TODO: actually store the names.
}

bool
Model_Application::all_indexes_are_set() {
	for(auto count : index_counts) if(count.index == 0) return false;
	return true;
}

void
process_par_group_index_sets(Mobius_Model *model, Par_Group_Info *par_group, std::unordered_map<Entity_Id, std::vector<Entity_Id>, Hash_Fun<Entity_Id>> &par_group_index_sets, Token *module_name = nullptr) {
	auto global_module = model->modules[0];
	
	Module_Declaration *module = nullptr;
	if(module_name) {
		// TODO: make a better way to look up module by name
		for(auto mod : model->modules)
			if(mod->module_name == module_name->string_value) {
				module = mod;
				break;
			}
		if(!module)    //NOTE: we do errror handling on this on the second pass when we load the actual data.
			return;
	} else
		module = model->modules[0];
	
	auto par_group_id = module->par_groups.find_by_name(&par_group->name);
	if(!is_valid(par_group_id)) {
		// NOTE: we do error handling on this on the second pass.
		return;
	}
	
	if(par_group->index_sets.empty()) { // We have to signal it so that it is not filled with default index sets later
		par_group_index_sets[par_group_id] = {};
		return;
	}
	
	auto pgd          = module->par_groups[par_group_id];
	if(is_valid(pgd->compartment)) {  // It is invalid for the "System" par group
		auto compartment  = module->compartments[pgd->compartment];
		if(is_valid(compartment->global_id))
			compartment = global_module->compartments[compartment->global_id];
		
		for(String_View name : par_group->index_sets) {
			auto index_set_id = global_module->index_sets.find_by_name(name);
			if(!is_valid(index_set_id))
				fatal_error(Mobius_Error::internal, "We got an invalid index set for a parameter group from the data set.");
			
			if(std::find(compartment->index_sets.begin(), compartment->index_sets.end(), index_set_id) == compartment->index_sets.end()) {
				par_group->name.print_error_header();
				fatal_error("The par_group \"", par_group->name.string_value, "\" can not be indexed with the index set \"", name, "\" since the compartment \"", compartment->name, "\" is not distributed over that index set in the model \"", model->model_name, "\".");
			}
			
			par_group_index_sets[par_group_id].push_back(index_set_id);
		}
	}
}

void
process_parameters(Model_Application *app, Par_Group_Info *par_group_info, Module_Info *module_info = nullptr) {
	
	if(!app->parameter_data.has_been_set_up)
		fatal_error(Mobius_Error::internal, "We tried to process parameter data before the parameter structure was set up.");
	
	Mobius_Model *model = app->model;
	
	Module_Declaration *module = nullptr;
	if(module_info) {
		// TODO: make a better way to look up module by name
		for(auto mod : model->modules)
			if(mod->module_name == module_info->name.string_value) {
				module = mod;
				break;
			}
		if(!module) {
			module_info->name.print_warning_header();
			warning_print("The model \"", model->model_name, "\" does not contain a module named \"", module_info->name.string_value, "\". This data block will be ignored.\n\n");
			return;
		}
	} else
		module = model->modules[0];
	
	// TODO: Match module versions, and be lenient on errors if versions don't match.
	
	// TODO: oops module->module_name is not the right thing to use for the global module (unless we set that name to something sensible?).
	
	auto par_group_id = module->par_groups.find_by_name(&par_group_info->name);
	if(!is_valid(par_group_id)) {
		par_group_info->name.print_error_header();
		fatal_error("The module \"", module->module_name, "\" does not contain the parameter group \"", par_group_info->name.string_value, ".");
		//TODO: say what file the module was declared in?
	}
	//auto par_group = module->par_groups[par_group_id];
	
	for(auto &par : par_group_info->pars) {
		warning_print(par.name.string_value, "\n");
		auto par_id = module->parameters.find_by_name(&par.name);
		Entity_Registration<Reg_Type::parameter> *param;
		if(!is_valid(par_id) || (param = module->parameters[par_id])->par_group != par_group_id) {
			// NOTE: this also covers the case where par_id is invalid.
			par.name.print_error_header();
			fatal_error("The parameter group \"", par_group_info->name.string_value, "\" in the module \"", module->module_name, "\" does not contain a parameter named \"", par.name.string_value, "\".");
		}
		
		if(param->decl_type != par.type) {
			par.name.print_error_header();
			fatal_error("The parameter \"", par.name.string_value, "\" should be of type ", name(param->decl_type), ", not of type ", name(par.type), ".");
		}
		
		// TODO: Double check that we have a valid amount of values for the parameter.
		s64 expect_count = app->parameter_data.instance_count(par_id);
		if((par.type == Decl_Type::par_enum && expect_count != par.values_enum.size()) || (par.type != Decl_Type::par_enum && expect_count != par.values.size()))
			fatal_error(Mobius_Error::internal, "We got the wrong number of parameter values from the data set (or did not set up indexes for parameters correctly).");
		
		int idx = 0;
		app->parameter_data.for_each(par_id, [&idx, &app, &param, &par](auto idxs, s64 offset) {
			Parameter_Value value;
			if(par.type == Decl_Type::par_enum) {
				s64 idx2 = 0;
				bool found = false;
				for(; idx2 < param->enum_values.size(); ++idx2) {
					if(param->enum_values[idx2] == par.values_enum[idx]) {
						found = true;
						break;
					}
				}
				if(!found) {
					par.name.print_error_header();
					fatal_error("\"", par.values_enum[idx], "\" is not a valid value for the enum parameter \"", par.name.string_value, "\".");
				}
				value.val_integer = idx2;
			} else
				value = par.values[idx];
			++idx;
			*app->parameter_data.get_value(offset) = value;
		});
	}
}


void
Model_Application::build_from_data_set(Data_Set *data_set) {
	if(is_compiled)
		fatal_error(Mobius_Error::api_usage, "Tried to build model application after it was compiled.");
	if(this->data_set)
		fatal_error(Mobius_Error::api_usage, "Model application was provided more than one data sets.");
	this->data_set = data_set;
	
	auto global_module = model->modules[0];
	
	for(auto &index_set : data_set->index_sets) {
		auto id = global_module->index_sets.find_by_name(&index_set.name);
		if(!is_valid(id)) {
			index_set.name.print_error_header();
			fatal_error("\"", index_set.name.string_value, "\" has not been declared as an index set in the model \"", model->model_name, "\".");
		}
		index_counts[id.id] = { id, (s32)index_set.indexes.count() };
		// TODO: do we need to store the index names in the model app, or can we just keep the reference to the data_set ?
	}
	
	//TODO: not sure how to handle directed trees when it comes to discrete fluxes. Should we sort the indexes in order?
	
	for(auto &neighbor : data_set->neighbors) {
		auto neigh_id = global_module->neighbors.find_by_name(&neighbor.name);
		if(!is_valid(neigh_id)) {
			neighbor.name.print_error_header();
			fatal_error("\"", neighbor.name.string_value, "\" has not been declared as a neighbor structure in the model.");
		}
		// note: this one must be valid because we already checked it against the index sets in the data set, and the data set index sets were already checked against the model above.
		auto index_set = global_module->index_sets.find_by_name(neighbor.index_set);
		auto nbd = global_module->neighbors[neigh_id];
		if(nbd->index_set != index_set) {
			neighbor.name.print_error_header();
			fatal_error("The neighbor structure \"", neighbor.name.string_value, "\" was not attached to the index set \"", neighbor.index_set, "\" in the model \"", model->model_name, "\"");
		}
		if(nbd->type == Neighbor_Structure_Type::directed_tree) {
			if(neighbor.type != Neighbor_Info::Type::graph) {
				neighbor.name.print_error_header();
				fatal_error("Neighbor structures of type directed_tree can only be set up using graph data.");
			}
			if(!neighbor_data.has_been_set_up)
				set_up_neighbor_structure();
			if(neighbor.points_at.size() != index_counts[index_set.id].index)
				fatal_error(Mobius_Error::internal, "Somehow the neighbor data in a data set did not have a size matching the amount of indexes in the associated index set.");
			std::vector<Index_T> indexes = {{index_set, 0}};
			for(int idx = 0; idx < index_counts[index_set.id].index; ++idx) { // TODO: make ++ and < operators for Index_T instead!
				indexes[0].index = idx;
				int info_id = 0;  // directed trees only have one info point (id 0), which is the points_at information.
				s64 offset = neighbor_data.get_offset_alternate({neigh_id, info_id}, &indexes);
				*neighbor_data.get_value(offset) = (s64)neighbor.points_at[idx];
			}
		} else {
			fatal_error(Mobius_Error::internal, "Unsupported neighbor structure type in build_from_data_set().");
		}
	}
	
	std::unordered_map<Entity_Id, std::vector<Entity_Id>, Hash_Fun<Entity_Id>> par_group_index_sets;
	for(auto &par_group : data_set->global_module.par_groups)
		process_par_group_index_sets(model, &par_group, par_group_index_sets);
	for(auto &module : data_set->modules) {
		for(auto &par_group : module.par_groups)
			process_par_group_index_sets(model, &par_group, par_group_index_sets, &module.name);
	}
	warning_print("Set up par structure\n");
	set_up_parameter_structure(&par_group_index_sets);
	
	process_parameters(this, &data_set->global_pars);
	for(auto &par_group : data_set->global_module.par_groups) {
		process_parameters(this, &par_group);
	}
	for(auto &module : data_set->modules) {
		for(auto &par_group : module.par_groups)
			process_parameters(this, &par_group, &module);
	}
}



