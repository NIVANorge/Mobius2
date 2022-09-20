
#include "model_application.h"

#include <map>
#include <string>



void
Model_Application::set_up_parameter_structure(std::unordered_map<Entity_Id, std::vector<Entity_Id>, Hash_Fun<Entity_Id>> *par_group_index_sets) {
	if(parameter_structure.has_been_set_up)
		fatal_error(Mobius_Error::internal, "Tried to set up parameter structure twice.");
	if(!all_indexes_are_set())
		fatal_error(Mobius_Error::internal, "Tried to set up parameter structure before all index sets received indexes.");
	
	std::vector<Multi_Array_Structure<Entity_Id>> structure;
	
	std::map<std::vector<Entity_Id>, std::vector<Entity_Id>> par_by_index_sets;
	
	std::vector<Entity_Id> empty;
	for(auto module : model->modules) {
		for(auto group_id : module->par_groups) {
			std::vector<Entity_Id> *index_sets = &empty;
			
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
					comp_id = module->get_global(comp_id);
					auto compartment = model->find_entity<Reg_Type::compartment>(comp_id);
					index_sets = &compartment->index_sets;
				}
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
	
	parameter_structure.set_up(std::move(structure));
	data.parameters.allocate();
	
	// Write default parameter values
	for(auto module : model->modules) {
		for(auto par : module->parameters) {
			Parameter_Value val = model->find_entity<Reg_Type::parameter>(par)->default_val;
			parameter_structure.for_each(par, [&](auto indexes, s64 offset) {
				*(data.parameters.get_value(offset)) = val;
			});
		}
	}
}

template<s32 var_type> void
Model_Application::set_up_series_structure(Var_Registry<var_type> &reg, Storage_Structure<double, Var_Id> &data, Series_Metadata *metadata) {
	if(data.has_been_set_up)
		fatal_error(Mobius_Error::internal, "Tried to set up series structure twice.");
	if(!all_indexes_are_set())
		fatal_error(Mobius_Error::internal, "Tried to set up series structure before all index sets received indexes.");
	
	std::vector<Multi_Array_Structure<Var_Id>> structure;
	
	if(metadata) {
		std::map<std::vector<Entity_Id>, std::vector<Var_Id>> series_by_index_sets;
		
		std::vector<Entity_Id> empty;
		for(auto series_id : reg) {
			std::vector<Entity_Id> *index_sets = &empty;
			if(metadata) {
				auto *info = series_id.type == 1 ? &metadata->index_sets : &metadata->index_sets_additional;
				
				auto find = info->find(series_id);
				if(find != info->end())
					index_sets = &find->second;
			}
			series_by_index_sets[*index_sets].push_back(series_id);
		}
		
		for(auto pair : series_by_index_sets) {
			std::vector<Entity_Id> index_sets = pair.first;
			std::vector<Var_Id>    handles    = pair.second;
			Multi_Array_Structure<Var_Id> array(std::move(index_sets), std::move(handles));
			structure.push_back(std::move(array));
		}
	} else {
		std::vector<Var_Id> handles;
		for(auto series_id : reg)
			handles.push_back(series_id);
		Multi_Array_Structure<Var_Id> array({}, std::move(handles));
		structure.push_back(std::move(array));
	}
	
	data.set_up(std::move(structure));
}

void
Model_Application::allocate_series_data(s64 time_steps, Date_Time start_date) {
	// NOTE: They are by default cleared to 0
	data.series.allocate(time_steps, start_date);
	data.additional_series.allocate(time_steps, start_date);
	
	for(auto series_id : model->series) {
		if(!(model->series[series_id]->flags & State_Variable::Flags::f_clear_series_to_nan)) continue;
		
		series_structure.for_each(series_id, [time_steps, this](auto &indexes, s64 offset) {
			for(s64 step = 0; step < time_steps; ++step)
				*data.series.get_value(offset, step) = std::numeric_limits<double>::quiet_NaN();
		});
	}
	
	for(auto series_id : additional_series) {
		if(!(additional_series[series_id]->flags & State_Variable::Flags::f_clear_series_to_nan)) continue;
		
		additional_series_structure.for_each(series_id, [time_steps, this](auto &indexes, s64 offset) {
			for(s64 step = 0; step < time_steps; ++step)
				*data.additional_series.get_value(offset, step) = std::numeric_limits<double>::quiet_NaN();
		});
	}
}

void
Model_Application::set_up_neighbor_structure() {
	if(neighbor_structure.has_been_set_up)
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
	neighbor_structure.set_up(std::move(structure));
	data.neighbors.allocate();
	
	for(int idx = 0; idx < neighbor_structure.total_count; ++idx)
		data.neighbors.data[idx] = -1;                          // To signify that it doesn't point at anything (yet).
};

bool
Model_Application::all_indexes_are_set() {
	for(auto count : index_counts) if(count.index == 0) return false;
	return true;
}

void
process_par_group_index_sets(Mobius_Model *model, Par_Group_Info *par_group, std::unordered_map<Entity_Id, std::vector<Entity_Id>, Hash_Fun<Entity_Id>> &par_group_index_sets, const std::string &module_name = "") {
	auto global_module = model->modules[0];
	
	Module_Declaration *module = nullptr;
	if(module_name != "") {
		// TODO: make a better way to look up module by name
		for(auto mod : model->modules)
			if(mod->module_name == module_name.data()) {
				module = mod;
				break;
			}
		if(!module)    //NOTE: we do errror handling on this on the second pass when we load the actual data.
			return;
	} else
		module = model->modules[0];
	
	auto par_group_id = module->par_groups.find_by_name(par_group->name);
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
		auto comp_id = module->get_global(pgd->compartment);
		auto compartment  = model->find_entity<Reg_Type::compartment>(comp_id);
		
		for(String_View name : par_group->index_sets) {
			auto index_set_id = global_module->index_sets.find_by_name(name);
			if(!is_valid(index_set_id))
				fatal_error(Mobius_Error::internal, "We got an invalid index set for a parameter group from the data set.");
			
			if(std::find(compartment->index_sets.begin(), compartment->index_sets.end(), index_set_id) == compartment->index_sets.end()) {
				par_group->loc.print_error_header();
				fatal_error("The par_group \"", par_group->name, "\" can not be indexed with the index set \"", name, "\" since the compartment \"", compartment->name, "\" is not distributed over that index set in the model \"", model->model_name, "\".");
			}
			
			par_group_index_sets[par_group_id].push_back(index_set_id);
		}
	}
}

Module_Declaration *
does_module_exist(Mobius_Model *model, Module_Info *module_info) {
	Module_Declaration *module = nullptr;
	
	for(auto mod : model->modules)
		if(mod->module_name == module_info->name.data()) {
			module = mod;
			break;
		}
	return module;
}

void
process_parameters(Model_Application *app, Par_Group_Info *par_group_info, Module_Declaration *module = nullptr) {
	
	if(!app->parameter_structure.has_been_set_up)
		fatal_error(Mobius_Error::internal, "We tried to process parameter data before the parameter structure was set up.");
	
	Mobius_Model *model = app->model;
	
	if(!module)
		module = model->modules[0];
	
	// TODO: Match module versions, and be lenient on errors if versions don't match.
	
	// TODO: oops module->module_name is not the right thing to use for the global module (unless we set that name to something sensible?).
	
	auto par_group_id = module->par_groups.find_by_name(par_group_info->name);
	if(!is_valid(par_group_id)) {
		par_group_info->loc.print_error_header();
		fatal_error("The module \"", module->module_name, "\" does not contain the parameter group \"", par_group_info->name, ".");
		//TODO: say what file the module was declared in?
	}
	//auto par_group = module->par_groups[par_group_id];
	
	for(auto &par : par_group_info->pars) {
		//warning_print(par.name.string_value, "\n");
		auto par_id = module->parameters.find_by_name(par.name);
		Entity_Registration<Reg_Type::parameter> *param;
		if(!is_valid(par_id) || (param = module->parameters[par_id])->par_group != par_group_id) {
			// NOTE: this also covers the case where par_id is invalid.
			par.loc.print_error_header();
			fatal_error("The parameter group \"", par_group_info->name, "\" in the module \"", module->module_name, "\" does not contain a parameter named \"", par.name, "\".");
		}
		
		if(param->decl_type != par.type) {
			par.loc.print_error_header();
			fatal_error("The parameter \"", par.name, "\" should be of type ", name(param->decl_type), ", not of type ", name(par.type), ".");
		}
		
		// TODO: Double check that we have a valid amount of values for the parameter.
		s64 expect_count = app->parameter_structure.instance_count(par_id);
		if((par.type == Decl_Type::par_enum && expect_count != par.values_enum.size()) || (par.type != Decl_Type::par_enum && expect_count != par.values.size()))
			fatal_error(Mobius_Error::internal, "We got the wrong number of parameter values from the data set (or did not set up indexes for parameters correctly).");
		
		int idx = 0;
		app->parameter_structure.for_each(par_id, [&idx, &app, &param, &par](auto idxs, s64 offset) {
			Parameter_Value value;
			if(par.type == Decl_Type::par_enum) {
				s64 idx2 = 0;
				bool found = false;
				for(; idx2 < param->enum_values.size(); ++idx2) {
					if(param->enum_values[idx2] == par.values_enum[idx].data()) {
						found = true;
						break;
					}
				}
				if(!found) {
					par.loc.print_error_header();
					fatal_error("\"", par.values_enum[idx], "\" is not a valid value for the enum parameter \"", par.name, "\".");
				}
				value.val_integer = idx2;
			} else
				value = par.values[idx];
			++idx;
			*app->data.parameters.get_value(offset) = value;
		});
	}
}

void
process_series_metadata(Model_Application *app, Series_Set_Info *series, Series_Metadata *metadata) {
	
	auto model = app->model;
	
	if( (series->has_date_vector && series->dates.empty())   ||
		(!series->has_date_vector && series->time_steps==0) )   // Ignore empty data block.
		return;
	
	if(series->start_date < metadata->start_date) metadata->start_date = series->start_date;
	
	Date_Time end_date = series->end_date;
	if(!series->has_date_vector)
		end_date = advance(series->start_date, app->time_step_size, series->time_steps-1);
	
	if(end_date > metadata->end_date) metadata->end_date = end_date;
	
	metadata->any_data_at_all = true;

	for(auto &header : series->header_data) {
		// NOTE: several time series could have been given the same name.
		std::set<Var_Id> ids = model->series[header.name];
		
		if(ids.empty()) {
			//This series is not recognized as a model input, so it is an "additional series"
			
			State_Variable var;
			var.name = app->alloc.copy_string_view(header.name.data());
			var.flags = State_Variable::Flags::f_clear_series_to_nan;
			
			Var_Id var_id = app->additional_series.register_var(var, invalid_var_location);
			ids.insert(var_id);
		}
		
		if(header.indexes.empty()) continue;
		
		if(metadata->index_sets.find(*ids.begin()) != metadata->index_sets.end()) continue; // NOTE: already set by another series data block.
		
		for(auto &index : header.indexes[0]) {  // NOTE: just check the index sets of the first index tuple. We check for internal consistency between tuples somewhere else.
			// NOTE: this should be valid since we already tested it internally in the data set.
			Entity_Id index_set = model->modules[0]->index_sets.find_by_name(index.first);
			if(!is_valid(index_set))
				fatal_error(Mobius_Error::internal, "Invalid index set for series in data set.");
			for(auto id : ids) {
				if(id.type == 1) {// Only perform the check for model inputs, not additional series.
					auto comp_id     = model->series[id]->loc1.compartment;
					auto compartment = model->modules[0]->compartments[comp_id];

					if(std::find(compartment->index_sets.begin(), compartment->index_sets.end(), index_set) == compartment->index_sets.end()) {
						header.loc.print_error_header();
						fatal_error("Can not set \"", index.first, "\" as an index set dependency for the series \"", header.name, "\" since the compartment \"", compartment->name, "\" is not distributed over that index set.");	
					}
				}
				if(id.type == 1)
					metadata->index_sets[id].push_back(index_set);
				else
					metadata->index_sets_additional[id].push_back(index_set);
			}
		}
	}
}


void
process_series(Model_Application *app, Series_Set_Info *series_info, Date_Time end_date);

void
Model_Application::set_indexes(Entity_Id index_set, std::vector<String_View> &indexes) {
	index_counts[index_set.id] = Index_T {index_set, (s32)indexes.size()};
	index_names[index_set.id].resize(indexes.size());
	s32 idx = 0;
	for(auto index : indexes) {
		String_View new_name = alloc.copy_string_view(index);
		index_names_map[index_set.id][new_name] = Index_T {index_set, idx};
		index_names[index_set.id][idx] = new_name;
		++idx;
	}
	
}

Index_T
Model_Application::get_index(Entity_Id index_set, String_View name) {
	auto &map = index_names_map[index_set.id];
	Index_T result;
	result.index_set = invalid_entity_id;
	result.index = -1;  //TODO: Should we throw an error here instead?
	auto find = map.find(name);
	if(find != map.end())
		return find->second;
	return result;
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
		auto id = global_module->index_sets.find_by_name(index_set.name);
		if(!is_valid(id)) {
			index_set.loc.print_error_header();
			fatal_error("\"", index_set.name, "\" has not been declared as an index set in the model \"", model->model_name, "\".");
		}
		//index_counts[id.id] = { id, (s32)index_set.indexes.count() };
		std::vector<String_View> names;
		names.reserve(index_set.indexes.count());
		for(auto &index : index_set.indexes)
			names.push_back(index.name);
		set_indexes(id, names);
	}
	
	//TODO: not sure how to handle directed trees when it comes to discrete fluxes. Should we sort the indexes in order?
	
	for(auto &neighbor : data_set->neighbors) {
		auto neigh_id = global_module->neighbors.find_by_name(neighbor.name);
		if(!is_valid(neigh_id)) {
			neighbor.loc.print_error_header();
			fatal_error("\"", neighbor.name, "\" has not been declared as a neighbor structure in the model.");
		}
		// note: this one must be valid because we already checked it against the index sets in the data set, and the data set index sets were already checked against the model above.
		auto index_set = global_module->index_sets.find_by_name(neighbor.index_set);
		auto nbd = global_module->neighbors[neigh_id];
		if(nbd->index_set != index_set) {
			neighbor.loc.print_error_header();
			fatal_error("The neighbor structure \"", neighbor.name, "\" was not attached to the index set \"", neighbor.index_set, "\" in the model \"", model->model_name, "\"");
		}
		if(nbd->type == Neighbor_Structure_Type::directed_tree) {
			if(neighbor.type != Neighbor_Info::Type::graph) {
				neighbor.loc.print_error_header();
				fatal_error("Neighbor structures of type directed_tree can only be set up using graph data.");
			}
			if(!neighbor_structure.has_been_set_up)
				set_up_neighbor_structure();
			if(neighbor.points_at.size() != index_counts[index_set.id].index)
				fatal_error(Mobius_Error::internal, "Somehow the neighbor data in a data set did not have a size matching the amount of indexes in the associated index set.");
			std::vector<Index_T> indexes = {{index_set, 0}};
			for(int idx = 0; idx < index_counts[index_set.id].index; ++idx) { // TODO: make ++ and < operators for Index_T instead!
				indexes[0].index = idx;
				int info_id = 0;  // directed trees only have one info point (id 0), which is the points_at information.
				s64 offset = neighbor_structure.get_offset_alternate({neigh_id, info_id}, indexes);
				*data.neighbors.get_value(offset) = (s64)neighbor.points_at[idx];
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
			process_par_group_index_sets(model, &par_group, par_group_index_sets, module.name);
	}
	
	set_up_parameter_structure(&par_group_index_sets);
	
	for(auto &par_group : data_set->global_module.par_groups) {
		process_parameters(this, &par_group);
	}
	for(auto &module : data_set->modules) {
		Module_Declaration *mod = does_module_exist(model, &module);
		if(!mod) {
			module.loc.print_warning_header();
			warning_print("The model \"", model->model_name, "\" does not contain a module named \"", module.name, "\". This data block will be ignored.\n\n");
			continue;
		}
		for(auto &par_group : module.par_groups)
			process_parameters(this, &par_group, mod);
	}
	
	if(!data_set->series.empty()) {
		Series_Metadata metadata;
		metadata.start_date.seconds_since_epoch = std::numeric_limits<s64>::max();
		metadata.end_date.seconds_since_epoch   = std::numeric_limits<s64>::min();
		
		for(auto &series : data_set->series) {
			process_series_metadata(this, &series, &metadata);
		}
		
		set_up_series_structure(model->series, series_structure, &metadata);
		set_up_series_structure(additional_series, additional_series_structure, &metadata);
		
		s64 time_steps = 0;
		if(metadata.any_data_at_all) {
			time_steps = steps_between(metadata.start_date, metadata.end_date, time_step_size) + 1; // NOTE: if start_date == end_date we still want there to be 1 data point (dates are inclusive)
		}
		else if(model->series.count() != 0) {
			//TODO: use the model run start and end date.
			// Or better yet, we could just bake in series data as literals in the code. in this case.
		}
		warning_print("Input dates: ", metadata.start_date.to_string());
		warning_print(" ", metadata.end_date.to_string(), " ", time_steps, "\n");
	
		allocate_series_data(time_steps, metadata.start_date);
		
		for(auto &series : data_set->series)
			process_series(this, &series, metadata.end_date);
	} else {
		set_up_series_structure(model->series, series_structure, nullptr);
		set_up_series_structure(additional_series, additional_series_structure, nullptr);
	}
	
	warning_print("Model application set up with data.\n");
}

void
Model_Application::save_to_data_set() {
	//TODO: We should probably just generate a data set in this case.
	if(!data_set)
		fatal_error(Mobius_Error::api_usage, "Tried to save model application to data set, but no data set was attached to the model application.");
	
	
	// NOTE : For now we just write back the parameter values and index sets. Eventually we should write the entire structure when necessary.
	
	auto global = model->modules[0];
	
	for(auto index_set_id : global->index_sets) {
		auto index_set = global->index_sets[index_set_id];
		auto index_set_info = data_set->index_sets.find(index_set->name);
		if(!index_set_info)
			index_set_info = data_set->index_sets.create(index_set->name, {});
		index_set_info->indexes.clear();
		for(s32 idx = 0; idx < index_counts[index_set_id.id].index; ++idx) {
			index_set_info->indexes.create(index_names[index_set_id.id][idx], {});
		}
	}
	
	s16 module_id = 0;
	for(auto module : model->modules) {
		Module_Info *module_info = nullptr;
		if(module_id == 0)
			module_info = &data_set->global_module;
		else
			module_info = data_set->modules.find(module->module_name);
		if(!module_info)
			module_info = data_set->modules.create(module->module_name, {});
		
		module_info->version = module->version;
		
		for(auto par_group_id : module->par_groups) {
			auto par_group = module->par_groups[par_group_id];
			
			Par_Group_Info *par_group_info = module_info->par_groups.find(par_group->name);
			if(!par_group_info)
				par_group_info = module_info->par_groups.create(par_group->name, {});
			
			par_group_info->index_sets.clear();
			if(par_group->parameters.size() > 0) { // NOTE: not sure if empty should just be an error.
				auto id0 = par_group->parameters[0];
				auto &index_sets = parameter_structure.get_index_sets(id0);
				
				for(auto index_set_id : index_sets) {
					auto index_set = model->find_entity<Reg_Type::index_set>(index_set_id);
					par_group_info->index_sets.push_back(index_set->name);
				}
			}
			
			for(auto par_id : par_group->parameters) {
				auto par = module->parameters[par_id];
				
				auto par_info = par_group_info->pars.find(par->name);
				if(!par_info)
					par_info = par_group_info->pars.create(par->name, {});
				
				par_info->type = par->decl_type;
				par_info->values.clear();
				par_info->values_enum.clear();
				
				parameter_structure.for_each(par_id, [&,this](std::vector<Index_T> &idxs, s64 offset) {
					if(par_info->type == Decl_Type::par_enum) {
						s64 ival = data.parameters.get_value(offset)->val_integer;
						par_info->values_enum.push_back(par->enum_values[ival]);
					} else {
						par_info->values.push_back(*data.parameters.get_value(offset));
					}
				});
			}
		}
		
		++module_id;
	}
}


Date_Time
Model_Data::get_start_date_parameter() {
	auto id = app->model->modules[0]->parameters.find_by_name("Start date");
	auto offset = parameters.structure->get_offset_base(id);                    // NOTE: it should not be possible to index this over an index set any way.
	return parameters.get_value(offset)->val_datetime;
}

Date_Time
Model_Data::get_end_date_parameter() {
	auto id = app->model->modules[0]->parameters.find_by_name("End date");
	auto offset = parameters.structure->get_offset_base(id);                    // NOTE: it should not be possible to index this over an index set any way.
	return parameters.get_value(offset)->val_datetime;
}

Model_Data::Model_Data(Model_Application *app) :
	app(app), parameters(&app->parameter_structure), series(&app->series_structure),
	results(&app->result_structure, 1), neighbors(&app->neighbor_structure),
	additional_series(&app->additional_series_structure) {
}

// TODO: this should take flags on what to copy and what to keep a reference of!
Model_Data *
Model_Data::copy() {
	Model_Data *cpy = new Model_Data(app);
	
	cpy->parameters.copy_from(&this->parameters);
	cpy->series.refer_to(&this->series);
	cpy->additional_series.refer_to(&this->additional_series);
	cpy->neighbors.refer_to(&this->neighbors);
	// NOTE: result_structure should not copy or refer (at least for default) since the point is that there will be a model run to generate new data for this one.
	return cpy;
}
	


