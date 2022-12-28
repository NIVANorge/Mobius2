
#include "model_application.h"

#include <map>

void
Model_Application::set_up_parameter_structure(std::unordered_map<Entity_Id, std::vector<Entity_Id>, Hash_Fun<Entity_Id>> *par_group_index_sets) {
	if(parameter_structure.has_been_set_up)
		fatal_error(Mobius_Error::internal, "Tried to set up parameter structure twice.");
	if(!all_indexes_are_set())
		fatal_error(Mobius_Error::internal, "Tried to set up parameter structure before all index sets received indexes.");
	
	std::vector<Multi_Array_Structure<Entity_Id>> structure;
	std::map<std::vector<Entity_Id>, std::vector<Entity_Id>> par_by_index_sets;
	
	std::vector<Entity_Id> empty;
	
	for(auto group_id : model->par_groups) {
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
			auto comp_id = model->par_groups[group_id]->component;
			if(is_valid(comp_id)) {  // It is invalid for the "System" parameter group.
				auto comp = model->components[comp_id];
				index_sets = &comp->index_sets;
			}
		}
	
		for(auto par : model->par_groups[group_id]->parameters)
			par_by_index_sets[*index_sets].push_back(par);
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
	for(auto par_id : model->parameters) {
		Parameter_Value val = model->parameters[par_id]->default_val;
		parameter_structure.for_each(par_id, [&](auto indexes, s64 offset) {
			*(data.parameters.get_value(offset)) = val;
		});
	}
}

template<Var_Id::Type var_type> void
Model_Application::set_up_series_structure(Var_Registry<var_type> &reg, Storage_Structure<Var_Id> &data, Series_Metadata *metadata) {
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
				auto *info = series_id.type == Var_Id::Type::series ? &metadata->index_sets : &metadata->index_sets_additional;
				
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
	
	for(auto series_id : series) {
		if(!(series[series_id]->flags & State_Variable::Flags::clear_series_to_nan)) continue;
		
		series_structure.for_each(series_id, [time_steps, this](auto &indexes, s64 offset) {
			for(s64 step = 0; step < time_steps; ++step)
				*data.series.get_value(offset, step) = std::numeric_limits<double>::quiet_NaN();
		});
	}
	
	for(auto series_id : additional_series) {
		if(!(additional_series[series_id]->flags & State_Variable::Flags::clear_series_to_nan)) continue;
		
		additional_series_structure.for_each(series_id, [time_steps, this](auto &indexes, s64 offset) {
			for(s64 step = 0; step < time_steps; ++step)
				*data.additional_series.get_value(offset, step) = std::numeric_limits<double>::quiet_NaN();
		});
	}
}

Sub_Indexed_Component *
Model_Application::find_connection_component(Entity_Id conn_id, Entity_Id comp_id, bool make_error) {
	if(connection_components.size() < conn_id.id+1)
		fatal_error(Mobius_Error::internal, "Something went wrong with setting up connection components in time before they are used.");
	auto &components = connection_components[conn_id.id];
	auto find = std::find_if(components.begin(), components.end(), [comp_id](auto &comp)->bool { return comp_id == comp.id; });
	if(find == components.end()) {
		if(make_error)
			fatal_error(Mobius_Error::internal, "Something went wrong with setting up connection components in time before they are used.");
		return nullptr;
	}
	return &*find;
}

void
Model_Application::set_up_connection_structure() {
	if(connection_structure.has_been_set_up)
		fatal_error(Mobius_Error::internal, "Tried to set up connection structure twice.");
	if(!all_indexes_are_set())
		fatal_error(Mobius_Error::internal, "Tried to set up connection structure before all index sets received indexes.");
	
	std::vector<Multi_Array_Structure<Connection_T>> structure;
	for(auto connection_id : model->connections) {
		auto connection = model->connections[connection_id];
		
		if(connection->type == Connection_Type::directed_tree) {
			// TODO: This is a bit risky, we should actually check that connection_components is correctly set up;
			int max_index_sets = 0;
			for(auto &comp : connection_components[connection_id.id])
				max_index_sets = std::max(max_index_sets, (int)comp.index_sets.size());
			
			// TODO: As an optimization, we could figure out the maximal number of indexes that could be targeted by each given source instead of having this global max.
			for(auto &comp : connection_components[connection_id.id]) {
				// TODO: group handles from multiple source compartments by index tuples.
				Connection_T handle1 = { connection_id, comp.id, 0 };   // First info id for target compartment (indexed by the source compartment)
				std::vector<Connection_T> handles { handle1 };
				for(int id = 1; id <= max_index_sets; ++id) {
					Connection_T handle2 = { connection_id, comp.id, id };   // Info id for target index of number id.
					handles.push_back(handle2);
				}
				auto index_sets = comp.index_sets; // Copy vector
				Multi_Array_Structure<Connection_T> array(std::move(index_sets), std::move(handles));
				structure.push_back(array);
			}
		} else if (connection->type == Connection_Type::all_to_all) {
			// There is no connection data associated with this one.
		} else {
			fatal_error(Mobius_Error::internal, "Unsupported connection structure type in set_up_connection_structure()");
		}
	}
	connection_structure.set_up(std::move(structure));
	data.connections.allocate();
	
	for(int idx = 0; idx < connection_structure.total_count; ++idx)
		data.connections.data[idx] = -1;                          // To signify that it doesn't point at anything (yet).
};

bool
Model_Application::all_indexes_are_set() {
	for(auto count : index_counts) if(count.index == 0) return false;
	return true;
}

void
process_par_group_index_sets(Mobius_Model *model, Data_Set *data_set, Par_Group_Info *par_group, std::unordered_map<Entity_Id, std::vector<Entity_Id>, Hash_Fun<Entity_Id>> &par_group_index_sets, const std::string &module_name = "") {

	Entity_Id module_id = invalid_entity_id;
	if(!module_name.empty()) {
		module_id = model->modules.find_by_name(module_name);
		if(!is_valid(module_id)) //NOTE: we do error handling on missing modules on the second pass when we load the actual data.
			return;
	}
	
	Entity_Id group_id = model->par_groups.find_by_name(par_group->name);
	if(!model->get_scope(module_id)->has(group_id)) return;  // NOTE: we do error handling on this on the second pass.
	
	if(par_group->index_sets.empty()) { // We have to signal it so that it is not filled with default index sets later
		par_group_index_sets[group_id] = {};
		return;
	}
	
	auto pgd = model->par_groups[group_id];
	if(is_valid(pgd->component)) {  // It is invalid for the "System" par group
		auto comp  = model->components[pgd->component];
		
		auto &index_sets = par_group_index_sets[group_id];
		
		for(int idx = 0; idx < par_group->index_sets.size(); ++idx) {
			int index_set_idx = par_group->index_sets[idx];
			
			auto &name = data_set->index_sets[index_set_idx]->name;
			auto index_set_id = model->index_sets.find_by_name(name);
			if(!is_valid(index_set_id))
				fatal_error(Mobius_Error::internal, "We got an invalid index set for a parameter group from the data set.");
			
			if(std::find(comp->index_sets.begin(), comp->index_sets.end(), index_set_id) == comp->index_sets.end()) {
				par_group->loc.print_error_header();
				fatal_error("The par_group \"", par_group->name, "\" can not be indexed with the index set \"", name, "\" since the component \"", comp->name, "\" is not distributed over that index set in the model \"", model->model_name, "\".");
			}
			
			// The last two index sets are allowed to be the same, but no other duplicates are allowed.
			auto find = std::find(index_sets.begin(), index_sets.end(), index_set_id);
			if(find != index_sets.end()) {
				// If we are not currently processing the last index set or the duplicate is not the last one we inserted, there is an error
				if(idx != par_group->index_sets.size()-1 || (++find) != index_sets.end()) {
					par_group->loc.print_error_header();
					fatal_error("Only the two last index sets of a parameter group are allowed to be duplicate.");
				}
			}
			index_sets.push_back(index_set_id);
		}
	}
}

void
process_parameters(Model_Application *app, Par_Group_Info *par_group_info, Module_Info *module_info = nullptr, Entity_Id module_id = invalid_entity_id) {
	
	if(!app->parameter_structure.has_been_set_up)
		fatal_error(Mobius_Error::internal, "We tried to process parameter data before the parameter structure was set up.");
	
	Mobius_Model *model = app->model;
	
	auto group_id = model->par_groups.find_by_name(par_group_info->name);
	if(!model->get_scope(module_id)->has(group_id)) {
		par_group_info->loc.print_error_header();
		// TODO: oops module->module_name is not the right thing to use for the global module (unless we set that name to something sensible?).
		if(is_valid(module_id))
			fatal_error("The module \"", model->modules[module_id]->name, "\" does not contain the parameter group \"", par_group_info->name, "\".");
			//TODO: say what file the module was declared in?
		else
			fatal_error("The model does not contain the parameter group \"", par_group_info->name, "\".");
	}
	
	bool module_is_outdated = module_info && is_valid(module_id) && (module_info->version < model->modules[module_id]->version);
	
	for(auto &par : par_group_info->pars) {
		auto par_id = model->parameters.find_by_name(par.name);
		Entity_Registration<Reg_Type::parameter> *param;
		if(!is_valid(par_id) || (param = model->parameters[par_id])->par_group != group_id) {
			if(module_is_outdated) {
				warning_print("The parameter group \"", par_group_info->name, "\" in the module \"", model->modules[module_id]->name, "\" does not contain a parameter named \"", par.name, "\". The version of the module in the model code is newer than the version in the data, so this may be due to a change in the model. If you save over this data file, the parameter will be removed.\n");
			}
			par.loc.print_error_header();
			fatal_error("The parameter group \"", par_group_info->name, "\" does not contain a parameter named \"", par.name, "\".");
		}
		
		if(param->decl_type != par.type) {
			par.loc.print_error_header();
			fatal_error("The parameter \"", par.name, "\" should be of type ", name(param->decl_type), ", not of type ", name(par.type), ".");
		}
		
		// TODO: just print a warning and fill in default values (or trim off values) ?
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
		std::set<Var_Id> ids = app->series[header.name];
		
		if(ids.empty()) {
			//This series is not recognized as a model input, so it is an "additional series"
			State_Variable var;
			var.name = header.name;
			var.flags = State_Variable::Flags::clear_series_to_nan;
			var.unit = header.unit;
			
			Var_Id var_id = app->additional_series.register_var(var, invalid_var_location);
			ids.insert(var_id);
		}
		//TODO check that the units of multiple instances of the same series cohere. Or should the rule be that only the first instance declares the unit? In that case we should still give an error if later cases declares a unit. Or should we be even more fancy and allow for automatic unit conversions (when we implement that)?
		
		if(header.indexes.empty()) continue;
		
		if(metadata->index_sets.find(*ids.begin()) != metadata->index_sets.end()) continue; // NOTE: already set by another series data block.
		
		for(auto &index : header.indexes[0]) {  // NOTE: just check the index sets of the first index tuple. We check for internal consistency between tuples somewhere else.
			// NOTE: this should be valid since we already tested it internally in the data set.
			Entity_Id index_set = model->index_sets.find_by_name(index.first);
			if(!is_valid(index_set))
				fatal_error(Mobius_Error::internal, "Invalid index set for series in data set.");
			for(auto id : ids) {
				
				if(id.type == Var_Id::Type::series) {// Only perform the check for model inputs, not additional series.
					auto comp_id     = app->series[id]->loc1.first();
					auto comp        = model->components[comp_id];

					if(std::find(comp->index_sets.begin(), comp->index_sets.end(), index_set) == comp->index_sets.end()) {
						header.loc.print_error_header();
						fatal_error("Can not set \"", index.first, "\" as an index set dependency for the series \"", header.name, "\" since the compartment \"", comp->name, "\" is not distributed over that index set.");
					}
				}
				if(id.type == Var_Id::Type::series)
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
Model_Application::set_indexes(Entity_Id index_set, std::vector<std::string> &indexes) {
	index_counts[index_set.id] = Index_T {index_set, (s32)indexes.size()};
	index_names[index_set.id].resize(indexes.size());
	s32 idx = 0;
	for(auto index : indexes) {
		index_names_map[index_set.id][index] = Index_T {index_set, idx};
		index_names[index_set.id][idx] = index;
		++idx;
	}
}

Index_T
Model_Application::get_index(Entity_Id index_set, const std::string &name) {
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
add_connection_component(Model_Application *app, Data_Set *data_set, Component_Info *comp, Entity_Id connection_id, Entity_Id component_id, bool single_index_only, bool compartment_only, Source_Location loc) {
	
	// TODO: In general we should be more nuanced with what source location we print for errors in this procedure.
	
	auto model = app->model;
	auto component = model->components[component_id];
	
	if(!is_valid(component_id) || (compartment_only && component->decl_type != Decl_Type::compartment) || (comp->decl_type != component->decl_type)) {
		comp->loc.print_error_header();
		if(compartment_only)
			fatal_error("The name \"", comp->name, "\" does not refer to a compartment that was declared in this model. This connection type only supports compartments, not quantities.");
		else
			fatal_error("The name \"", comp->name, "\" does not refer to a ", name(comp->decl_type), " that was declared in this model.");
	}
	
	auto cnd = model->connections[connection_id];
	
	// TODO: Could also print the location of the declaration of the connection.
	if(std::find(cnd->components.begin(), cnd->components.end(), component_id) == cnd->components.end()) {
		loc.print_error_header();
		fatal_error("The connection \"", cnd->name,"\" is not allowed for the component \"", component->name, "\".");
	}
	
	if(single_index_only && comp->index_sets.size() != 1) {
		loc.print_error_header();
		fatal_error("This connection type only supports connections on components that are indexed by a single index set");
	}
	std::vector<Entity_Id> index_sets;
	for(int idx_set_id : comp->index_sets) {
		auto idx_set_info = data_set->index_sets[idx_set_id];
		auto index_set = model->index_sets.find_by_name(idx_set_info->name);
		if(!is_valid(index_set)) {
			idx_set_info->loc.print_error_header();
			fatal_error("The index set \"", idx_set_info->name, " does not exist in the model.");  // Actually, this has probably been checked somewhere else already.
		}
		if(std::find(component->index_sets.begin(), component->index_sets.end(), index_set) == component->index_sets.end()) {
			loc.print_error_header();
			fatal_error("The index sets indexing a component in a connection relation must also index that component in the model. The index set \"", idx_set_info->name, "\" does not index the component \"", component->name, "\" in the model");
		}
		index_sets.push_back(index_set);
	}
	
	auto &components = app->connection_components[connection_id.id];
	auto find = std::find_if(components.begin(), components.end(), [component_id](const Sub_Indexed_Component &comp) -> bool { return comp.id == component_id; });
	if(find != components.end()) {
		if(index_sets != find->index_sets) { // This should no longer be necessary when we make component declarations local to the connection data in the data set.
			loc.print_error_header();
			fatal_error("The component \"", app->model->components[component_id]->name, "\" appears twice in the same connection relation, but with different index set dependencies.");
		}
	} else {
		Sub_Indexed_Component comp;
		comp.id = component_id;
		comp.index_sets = index_sets;
		components.push_back(std::move(comp));
	}
}

void
pre_process_connection_data(Model_Application *app, Connection_Info &connection, Data_Set *data_set) {
	
	auto model = app->model;
	
	auto conn_id = model->connections.find_by_name(connection.name);
	if(!is_valid(conn_id)) {
		connection.loc.print_error_header();
		fatal_error("The connection structure \"", connection.name, "\" has not been declared in the model.");
	}

	auto cnd = model->connections[conn_id];
	
	bool single_index_only = false;
	bool compartment_only = true;
	if(cnd->type == Connection_Type::all_to_all) {
		single_index_only = true;
		compartment_only = false;
	}
	for(auto &comp : connection.components) {
		Entity_Id comp_id = model->components.find_by_name(comp.name);
		add_connection_component(app, data_set, &comp, conn_id, comp_id, single_index_only, compartment_only, connection.loc);
	}
	
	if(cnd->type == Connection_Type::directed_tree) {
		// NOTE: We allow empty info for this connection type, in which case the data type is 'none'.
		if(connection.type != Connection_Info::Type::graph && connection.type != Connection_Info::Type::none) {
			connection.loc.print_error_header();
			fatal_error("Connection structures of type directed_tree can only be set up using graph data.");
		}
		
		/*  //TODO: Use the arrows to store useful info about what components can be targets etc.
		for(auto &arr : connection.arrows) {
			auto comp = connection.components[arr.first.id];
			Entity_Id source_comp_id = model->components.find_by_name(comp->name);
			
			auto comp_target = connection.components[arr.second.id];
			Entity_Id target_comp_id = model->components.find_by_name(comp_target->name);
		}
		*/
		// TODO: In the end we will have to check that the connection structure matches the regex, but this is going to be complicated.
		// TODO: Also have to check that it is actually a tree.
		
	} else if (cnd->type == Connection_Type::all_to_all) {
		if(connection.type != Connection_Info::Type::single_component) {
			connection.loc.print_error_header();
			fatal_error("Connections of type all_to_all should have exactly a single component identifier in their data.");
		}
	} else
		fatal_error(Mobius_Error::internal, "Unsupported connection structure type in build_from_data_set().");
}

void
process_connection_data(Model_Application *app, Connection_Info &connection, Data_Set *data_set) {
	
	auto model = app->model;
	
	auto conn_id = model->connections.find_by_name(connection.name);
	auto cnd = model->connections[conn_id];
			
	if(cnd->type == Connection_Type::directed_tree) {

		for(auto &arr : connection.arrows) {
			auto comp = connection.components[arr.first.id];
			Entity_Id source_comp_id = model->components.find_by_name(comp->name);
			
			auto comp_target = connection.components[arr.second.id];
			Entity_Id target_comp_id = model->components.find_by_name(comp_target->name);
			
			auto *find = app->find_connection_component(conn_id, source_comp_id);
		
			auto &index_sets = find->index_sets;
			std::vector<Index_T> indexes;
			for(int idx = 0; idx < index_sets.size(); ++idx) {
				Index_T index = {index_sets[idx], arr.first.indexes[idx]};
				indexes.push_back(index);
			}
			s64 offset = app->connection_structure.get_offset_alternate({conn_id, source_comp_id, 0}, indexes);
			*app->data.connections.get_value(offset) = target_comp_id.id;
			
			for(int idx = 0; idx < arr.second.indexes.size(); ++idx) {
				int id = idx+1;
				s64 offset = app->connection_structure.get_offset_alternate({conn_id, source_comp_id, id}, indexes);
				*app->data.connections.get_value(offset) = (s64)arr.second.indexes[idx];
			}
		}
		
	} else if (cnd->type == Connection_Type::all_to_all) {
		// No data to set up for this one.
	} else
		fatal_error(Mobius_Error::internal, "Unsupported connection structure type in build_from_data_set().");
}


void
Model_Application::build_from_data_set(Data_Set *data_set) {
	if(is_compiled)
		fatal_error(Mobius_Error::api_usage, "Tried to build model application after it was compiled.");
	if(this->data_set)
		fatal_error(Mobius_Error::api_usage, "Model application was provided more than one data sets.");
	this->data_set = data_set;
	
	for(auto &index_set : data_set->index_sets) {
		auto id = model->index_sets.find_by_name(index_set.name);
		if(!is_valid(id)) {
			index_set.loc.print_error_header();
			fatal_error("\"", index_set.name, "\" has not been declared as an index set in the model \"", model->model_name, "\".");
		}
		std::vector<std::string> names;
		names.reserve(index_set.indexes.count());
		for(auto &index : index_set.indexes)
			names.push_back(index.name);
		set_indexes(id, names);
	}
	
	std::vector<std::string> gen_index = { "(generated)" };
	for(auto index_set : model->index_sets) {
		if(index_counts[index_set.id].index == 0)
			set_indexes(index_set, gen_index);
	}
	
	connection_components.resize(model->connections.count());
	for(auto &connection : data_set->connections)
		pre_process_connection_data(this, connection, data_set);
	if(!connection_structure.has_been_set_up)
		set_up_connection_structure();
	
	for(auto &connection : data_set->connections)
		process_connection_data(this, connection, data_set);
	
	std::unordered_map<Entity_Id, std::vector<Entity_Id>, Hash_Fun<Entity_Id>> par_group_index_sets;
	for(auto &par_group : data_set->global_module.par_groups)
		process_par_group_index_sets(model, data_set, &par_group, par_group_index_sets);
	for(auto &module : data_set->modules) {
		for(auto &par_group : module.par_groups)
			process_par_group_index_sets(model, data_set, &par_group, par_group_index_sets, module.name);
	}
	
	set_up_parameter_structure(&par_group_index_sets);
	
	for(auto &par_group : data_set->global_module.par_groups)
		process_parameters(this, &par_group);
	for(auto &module : data_set->modules) {
		Entity_Id module_id = model->modules.find_by_name(module.name);
		if(!is_valid(module_id)) {
			module.loc.print_warning_header();
			warning_print("The model \"", model->model_name, "\" does not contain a module named \"", module.name, "\". This data block will be ignored.\n\n");
			continue;
		}
		for(auto &par_group : module.par_groups)
			process_parameters(this, &par_group, &module, module_id);
	}
	
	if(!data_set->series.empty()) {
		Series_Metadata metadata;
		metadata.start_date.seconds_since_epoch = std::numeric_limits<s64>::max();
		metadata.end_date.seconds_since_epoch   = std::numeric_limits<s64>::min();
		
		for(auto &series : data_set->series)
			process_series_metadata(this, &series, &metadata);
		
		set_up_series_structure(series,            series_structure,            &metadata);
		set_up_series_structure(additional_series, additional_series_structure, &metadata);
		
		s64 time_steps = 0;
		if(metadata.any_data_at_all)
			time_steps = steps_between(metadata.start_date, metadata.end_date, time_step_size) + 1; // NOTE: if start_date == end_date we still want there to be 1 data point (dates are inclusive)
		else if(series.count() != 0) {
			//TODO: use the model run start and end date.
			// Or better yet, we could just bake in series default values as literals in the code. in this case.
		}
		warning_print("Input dates: ", metadata.start_date.to_string());
		warning_print(" ", metadata.end_date.to_string(), " ", time_steps, "\n");
	
		allocate_series_data(time_steps, metadata.start_date);
		
		for(auto &series : data_set->series)
			process_series(this, &series, metadata.end_date);
	} else {
		set_up_series_structure(series,            series_structure,            nullptr);
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

	for(Entity_Id index_set_id : model->index_sets) {
		auto index_set = model->index_sets[index_set_id];
		auto index_set_info = data_set->index_sets.find(index_set->name);
		if(!index_set_info)
			index_set_info = data_set->index_sets.create(index_set->name, {});
		index_set_info->indexes.clear();
		for(s32 idx = 0; idx < index_counts[index_set_id.id].index; ++idx)
			index_set_info->indexes.create(index_names[index_set_id.id][idx], {});
	}
	
	// Hmm, this is a bit cumbersome
	for(int idx = -1; idx < model->modules.count(); ++idx) {
		Entity_Id module_id = invalid_entity_id;
		if(idx >= 0) module_id = { Reg_Type::module, idx };
		
		if(is_valid(module_id) && !model->modules[module_id]->has_been_processed)  // NOTE: This means that it was in a file that was loaded, but not actually included into the model.
			continue;
		
		Module_Info *module_info = nullptr;
		if(idx < 0)
			module_info = &data_set->global_module;
		else {
			std::string &name = model->modules[module_id]->name;
			module_info = data_set->modules.find(name);
		
			if(!module_info)
				module_info = data_set->modules.create(name, {});
			
			module_info->version = model->modules[module_id]->version;
		}
		
		for(auto group_id : model->by_scope<Reg_Type::par_group>(module_id)) {
			auto par_group = model->par_groups[group_id];
			
			Par_Group_Info *par_group_info = module_info->par_groups.find(par_group->name);
			if(!par_group_info)
				par_group_info = module_info->par_groups.create(par_group->name, {});
			
			par_group_info->index_sets.clear();
			if(par_group->parameters.size() > 0) { // TODO: not sure if empty should just be an error.
				auto id0 = par_group->parameters[0];
				auto &index_sets = parameter_structure.get_index_sets(id0);
				
				for(auto index_set_id : index_sets) {
					auto index_set = model->index_sets[index_set_id];
					auto index_set_idx = data_set->index_sets.find_idx(index_set->name);
					if(index_set_idx < 0)
						fatal_error(Mobius_Error::internal, "Tried to set an index set for a parameter group in a data set, but the index set was not in the data set.");
					par_group_info->index_sets.push_back(index_set_idx);
				}
			}
			
			for(auto par_id : par_group->parameters) {
				auto par = model->parameters[par_id];
				
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

template<typename Val_T, typename Handle_T> void 
Data_Storage<Val_T, Handle_T>::allocate(s64 time_steps, Date_Time start_date) {
	if(!structure->has_been_set_up)
		fatal_error(Mobius_Error::internal, "Tried to allocate data before structure was set up.");
	this->start_date = start_date;
	if(this->time_steps != time_steps || !is_owning) {
		free_data();
		this->time_steps = time_steps;
		size_t sz = alloc_size();
		data = (Val_T *) malloc(sz);
		is_owning = true;
	} 
	size_t sz = alloc_size();
	memset(data, 0, sz);
}

template<typename Val_T, typename Handle_T> void
Data_Storage<Val_T, Handle_T>::refer_to(Data_Storage<Val_T, Handle_T> *source) {
	if(structure != source->structure)
		fatal_error(Mobius_Error::internal, "Tried to make a data storage refer to another one that belongs to a different storage structure.");
	free_data();
	data = source->data;
	time_steps = source->time_steps;
	start_date = source->start_date;
	is_owning = false;
}

template<typename Val_T, typename Handle_T> void
Data_Storage<Val_T, Handle_T>::copy_from(Data_Storage<Val_T, Handle_T> *source, bool size_only) {
	if(structure != source->structure)
		fatal_error(Mobius_Error::internal, "Tried to make a data storage copy from another one that belongs to a different storage structure.");
	free_data();
	if(source->time_steps > 0) {
		allocate(source->time_steps, source->start_date);
		if(!size_only)
			memcpy(data, source->data, alloc_size());
	} else {
		start_date = source->start_date;
	}
}

Date_Time
Model_Data::get_start_date_parameter() {
	auto id = app->model->parameters.find_by_name("Start date");
	auto offset = parameters.structure->get_offset_base(id);                    // NOTE: it should not be possible to index this over an index set any way.
	return parameters.get_value(offset)->val_datetime;
}

Date_Time
Model_Data::get_end_date_parameter() {
	auto id = app->model->parameters.find_by_name("End date");
	auto offset = parameters.structure->get_offset_base(id);                    // NOTE: it should not be possible to index this over an index set any way.
	return parameters.get_value(offset)->val_datetime;
}

Model_Data::Model_Data(Model_Application *app) :
	app(app), parameters(&app->parameter_structure), series(&app->series_structure),
	results(&app->result_structure, 1), connections(&app->connection_structure),
	additional_series(&app->additional_series_structure) {
}

// TODO: this should take flags on what to copy and what to keep a reference of!
Model_Data *
Model_Data::copy(bool copy_results) {
	Model_Data *cpy = new Model_Data(app);
	
	cpy->parameters.copy_from(&this->parameters);
	if(copy_results)
		cpy->results.copy_from(&this->results);
	cpy->series.refer_to(&this->series);
	cpy->additional_series.refer_to(&this->additional_series);
	cpy->connections.refer_to(&this->connections);
	return cpy;
}
	


