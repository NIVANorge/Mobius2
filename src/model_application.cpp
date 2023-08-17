
#include "model_application.h"

#include <map>
#include <cstdlib>
#include <sstream>


Indexes::Indexes(Mobius_Model *model) {
	lookup_ordered = false;
	if(model)  // NOTE: We need the check model!=0 since some places we need to construct an Indexed_Par before the model is created.
		indexes.resize(model->index_sets.count(), invalid_index);
}
Indexes::Indexes() {
	lookup_ordered = true;
}

Indexes::Indexes(Index_T index) {
	lookup_ordered = true;
	add_index(index);
}

void
Indexes::clear() {
	if(lookup_ordered)
		indexes.clear();
	else {
		for(auto &index : indexes) index = invalid_index;
	}
	mat_col = invalid_index;
}

void
Indexes::set_index(Index_T index, bool overwrite) {
	if(!is_valid(index))
		fatal_error(Mobius_Error::internal, "Tried to set an invalid index on an Indexes");
	if(!lookup_ordered) {
		if(!overwrite && is_valid(indexes[index.index_set.id])) {
			if(is_valid(mat_col))
				fatal_error(Mobius_Error::internal, "Got duplicate matrix column index for an Indexes.");
			mat_col = index;
		} else
			indexes[index.index_set.id] = index;
	} else
		fatal_error(Mobius_Error::internal, "Using set_index on an Indexes that is lookup_ordered");
}

void
Indexes::add_index(Index_T index) {
	if(!is_valid(index))
		fatal_error(Mobius_Error::internal, "Tried to set an invalid index on an Indexes");
	if(lookup_ordered)
		indexes.push_back(index);
	else
		fatal_error(Mobius_Error::internal, "Using add_index on an Indexes that is not lookup_ordered");
}

void
Indexes::add_index(Entity_Id index_set, s32 idx) {
	add_index(Index_T { index_set, idx } );
}


void
Index_Exprs::clean() {
	for(int idx = 0; idx < indexes.size(); ++idx) {
		delete indexes[idx];
		indexes[idx] = nullptr;
	}
	delete mat_col;
	mat_col = nullptr;
	mat_index_set = invalid_entity_id;
}
	
void
Index_Exprs::copy(Index_Exprs &other) {
	clean();
	indexes = other.indexes;
	for(auto &idx : indexes) {
		if(idx) idx = ::copy(idx);
	}
	if(other.mat_col)
		mat_col = ::copy(other.mat_col);
	mat_index_set = other.mat_index_set;
}

// Hmm, this could also maybe be used elsewhere. Put it as utility in function_tree.h ?
Math_Expr_FT *
add_exprs(Math_Expr_FT *lhs, Math_Expr_FT *rhs) {
	if(!lhs) return rhs;
	return make_binop('+', lhs, rhs);
}

Math_Expr_FT *
Index_Exprs::get_index(Model_Application *app, Entity_Id index_set, bool matrix_column) {
	
	// TODO: How to handle matrix column for union index sets? Probably disallow it?
	
	Math_Expr_FT *result = nullptr;
	if(matrix_column && mat_col) {
		if(index_set != mat_index_set)
			fatal_error(Mobius_Error::internal, "Unexpected matrix column index set.");
		result = ::copy(mat_col);
	} else {
		if(indexes[index_set.id]) {
			result = ::copy(indexes[index_set.id]);
		} else {
			// Try to index a union index set (if a direct index is absent) by an index belonging to a member of that union.
			bool found = false;
			auto set = app->model->index_sets[index_set];
			for(auto ui_id : set->union_of) {
				if(indexes[ui_id.id]) {
					result = add_exprs(result, ::copy(indexes[ui_id.id]));
					found = true;
					break;
				} else
					result = add_exprs(result, app->get_index_count_code(ui_id, *this));
			}
			if(!found) {    // Do this since we want this to raise an error.
				delete result;
				result = nullptr;
			}
		}
	}
	return result;
}

void
Index_Exprs::set_index(Entity_Id index_set, Math_Expr_FT *index, bool matrix_column) {
	if(!index)
		fatal_error(Mobius_Error::internal, "It is not allowed to set a nullptr index on an Index_Exprs.");
	if(matrix_column) {
		if(mat_col) delete mat_col;
		mat_index_set = index_set;
		mat_col = index;
	} else {
		if(indexes[index_set.id]) delete indexes[index_set.id];
		indexes[index_set.id] = index;
	}
}

void
Index_Exprs::transpose_matrix(Entity_Id index_set) {
	if(!mat_col || mat_index_set != index_set) return;
	auto tmp = mat_col;
	mat_col = indexes[index_set.id];
	indexes[index_set.id] = tmp;
}


void
prelim_compose(Model_Application *app, std::vector<std::string> &input_names);

Model_Application::Model_Application(Mobius_Model *model) :
	model(model), parameter_structure(this), series_structure(this), result_structure(this), connection_structure(this),
	additional_series_structure(this), index_counts_structure(this), data_set(nullptr), data(this), llvm_data(nullptr)/*,
	state_vars(Var_Id::Type::state_var), series(Var_Id::Type::series), additional_series(Var_Id::Type::additional_series)*/ {
	
	index_counts.resize(model->index_sets.count());
	index_names_map.resize(model->index_sets.count());
	index_names.resize(model->index_sets.count());
	
	time_step_size.unit       = Time_Step_Size::second;
	time_step_size.multiplier = 86400;
	time_step_unit.declared_form.push_back({0, 1, Compound_Unit::day});
	time_step_unit.set_standard_form();
	
	initialize_llvm();
	llvm_data = create_llvm_module();
}

void
Model_Application::set_up_parameter_structure(std::unordered_map<Entity_Id, std::vector<Entity_Id>, Hash_Fun<Entity_Id>> *par_group_index_sets) {
	if(parameter_structure.has_been_set_up)
		fatal_error(Mobius_Error::internal, "Tried to set up parameter structure twice.");
	if(!all_indexes_are_set())
		fatal_error(Mobius_Error::internal, "Tried to set up parameter structure before all index sets received indexes.");
	
	std::vector<Multi_Array_Structure<Entity_Id>> structure;
	std::map<std::vector<Entity_Id>, std::vector<Entity_Id>> par_by_index_sets;
	
	for(auto group_id : model->par_groups) {
		std::vector<Entity_Id> index_sets;
		
		bool found = false;
		if(par_group_index_sets) {
			auto find = par_group_index_sets->find(group_id);
			if(find != par_group_index_sets->end()) {
				found = true;
				index_sets = find->second;
			}
		}
		if(!found) {
			auto group = model->par_groups[group_id];
			for(auto comp_id : group->components) {
				auto comp = model->components[comp_id];
				index_sets.insert(index_sets.end(), comp->index_sets.begin(), comp->index_sets.end());
			}
		}
	
		//for(auto par : model->par_groups[group_id]->parameters)
		for(auto par_id : model->by_scope<Reg_Type::parameter>(group_id))
			par_by_index_sets[index_sets].push_back(par_id);
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

void
Model_Application::set_up_series_structure(Var_Id::Type type, Series_Metadata *metadata) {
	
	auto &data = get_storage_structure(type);
	
	if(data.has_been_set_up)
		fatal_error(Mobius_Error::internal, "Tried to set up series structure twice.");
	if(!all_indexes_are_set())
		fatal_error(Mobius_Error::internal, "Tried to set up series structure before all index sets received indexes.");
	
	std::vector<Multi_Array_Structure<Var_Id>> structure;
	
	if(metadata) {
		std::map<std::vector<Entity_Id>, std::vector<Var_Id>> series_by_index_sets;
		
		std::vector<Entity_Id> empty;
		for(auto series_id : vars.all(type)) {
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
		for(auto series_id : vars.all(type))
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
	
	// TODO: This could be factored into a function that is called twice.
	for(auto series_id : vars.all_series()) {
		if(!(vars[series_id]->has_flag(State_Var::clear_series_to_nan))) continue;
		
		series_structure.for_each(series_id, [time_steps, this](auto &indexes, s64 offset) {
			for(s64 step = 0; step < time_steps; ++step)
				*data.series.get_value(offset, step) = std::numeric_limits<double>::quiet_NaN();
		});
	}
	
	for(auto series_id : vars.all_additional_series()) {
		if(!(vars[series_id]->has_flag(State_Var::clear_series_to_nan))) continue;
		
		additional_series_structure.for_each(series_id, [time_steps, this](auto &indexes, s64 offset) {
			for(s64 step = 0; step < time_steps; ++step)
				*data.additional_series.get_value(offset, step) = std::numeric_limits<double>::quiet_NaN();
		});
	}
}

Sub_Indexed_Component *
Model_Application::find_connection_component(Entity_Id conn_id, Entity_Id comp_id, bool make_error) {
	
	auto &components = connection_components[conn_id].components;
	auto find = std::find_if(components.begin(), components.end(), [comp_id](auto &comp)->bool { return comp_id == comp.id; });
	if(find == components.end()) {
		if(make_error)
			fatal_error(Mobius_Error::internal, "Something went wrong with setting up connection components in time before they are used.");
		return nullptr;
	}
	return &*find;
}

Entity_Id
Model_Application::get_single_connection_index_set(Entity_Id conn_id) {
	auto conn = model->connections[conn_id];
	if(conn->type != Connection_Type::all_to_all && conn->type != Connection_Type::grid1d)
		fatal_error(Mobius_Error::internal, "Misuse of get_single_connection_index_set().");
	return connection_components[conn_id].components[0].index_sets[0];
}

Entity_Id
avoid_index_set_dependency(Model_Application *app, Var_Loc_Restriction restriction) {
	
	// TODO: For secondary restriction r2 also!
	
	Entity_Id avoid = invalid_entity_id;
	if(restriction.r1.type == Restriction::top || restriction.r1.type == Restriction::bottom)
		return app->get_single_connection_index_set(restriction.r1.connection_id);
	return avoid;
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
		
		if(connection->type == Connection_Type::directed_tree || connection->type == Connection_Type::directed_graph) {
			// TODO: This is a bit risky, we should actually check that connection_components is correctly set up.
			
			for(auto &comp : connection_components[connection_id].components) {
				
				if(comp.total_as_source == 0) continue; // No out-going arrows from this source, so we don't need to pack info about them later.
				
				// TODO: group handles from multiple source compartments by index tuples.
				Connection_T handle1 = { connection_id, comp.id, 0 };   // First info id for target compartment (indexed by the source compartment)
				std::vector<Connection_T> handles { handle1 };
				for(int id = 1; id <= comp.max_target_indexes; ++id) {
					Connection_T handle2 = { connection_id, comp.id, id };   // Info id for target index number id.
					handles.push_back(handle2);
				}
		
				auto index_sets = comp.index_sets; // Copy vector
				if(connection->type == Connection_Type::directed_graph)
					index_sets.push_back(comp.edge_index_set);
				
				Multi_Array_Structure<Connection_T> array(std::move(index_sets), std::move(handles));
				structure.push_back(array);
			}
		} else if (connection->type == Connection_Type::all_to_all || connection->type == Connection_Type::grid1d) {
			// There is no connection data associated with these.
		} else {
			fatal_error(Mobius_Error::internal, "Unsupported connection structure type in set_up_connection_structure()");
		}
	}
	connection_structure.set_up(std::move(structure));
	data.connections.allocate();
	
	for(int idx = 0; idx < connection_structure.total_count; ++idx)
		data.connections.data[idx] = -2;    // To signify that it doesn't point at anything (yet).
};

void
Model_Application::set_up_index_count_structure() {
	std::vector<Multi_Array_Structure<Entity_Id>> structure;
	for(auto index_set : model->index_sets) {
		auto sub_indexed_to = model->index_sets[index_set]->sub_indexed_to;
		if(is_valid(sub_indexed_to))
			structure.push_back(Multi_Array_Structure<Entity_Id>( {sub_indexed_to}, {index_set} ));
		else
			structure.push_back(Multi_Array_Structure<Entity_Id>( {}, {index_set} ));
	}
	index_counts_structure.set_up(std::move(structure));
	data.index_counts.allocate();
	
	for(auto index_set : model->index_sets) {
		int idx = 0;
		index_counts_structure.for_each(index_set, [this, index_set, &idx](Indexes &indexes, s64 offset) {
			data.index_counts.data[offset] = index_counts[index_set.id][idx++].index;
		});
	}
}

bool
Model_Application::all_indexes_are_set() {
	for(auto count : index_counts) if(count.empty()) return false;
	return true;
}

void
process_par_group_index_sets(Mobius_Model *model, Data_Set *data_set, Par_Group_Info *par_group, std::unordered_map<Entity_Id, std::vector<Entity_Id>, Hash_Fun<Entity_Id>> &par_group_index_sets, const std::string &module_name = "") {

	Entity_Id module_id = invalid_entity_id;
	if(!module_name.empty()) {
		module_id = model->model_decl_scope.deserialize(module_name, Reg_Type::module);
		if(!is_valid(module_id)) //NOTE: we do error handling on missing modules on the second pass when we load the actual data.
			return;
	}
	
	auto module_scope = model->get_scope(module_id);
	
	Entity_Id group_id = module_scope->deserialize(par_group->name, Reg_Type::par_group);
	if(!is_valid(group_id)) return;  // NOTE: we do error handling on this on the second pass.
	
	if(par_group->index_sets.empty()) { // We have to signal it so that it is not filled with default index sets later
		par_group_index_sets[group_id] = {};
		return;
	}
	
	auto pgd = model->par_groups[group_id];
	if(pgd->components.empty()) {
		if(!par_group->index_sets.empty()) {
			par_group->source_loc.print_error_header();
			fatal_error("The par_group \"", par_group->name, "\" can not be indexed with any index sets since it is not tied to a component.");
		}
	} else {
		auto &index_sets = par_group_index_sets[group_id];
		
		for(int idx = 0; idx < par_group->index_sets.size(); ++idx) {
			int index_set_idx = par_group->index_sets[idx];
			
			auto &name = data_set->index_sets[index_set_idx]->name;
			auto index_set_id = model->model_decl_scope.deserialize(name, Reg_Type::index_set);
			if(!is_valid(index_set_id)) {
				par_group->source_loc.print_error_header();
				fatal_error("The index set \"", name, "\" does not exist in the model.");
			}
			
			bool found = false;
			for(auto comp_id : pgd->components) {
				auto comp = model->components[comp_id];
				if(std::find(comp->index_sets.begin(), comp->index_sets.end(), index_set_id) != comp->index_sets.end()) {
					found = true;
					break;
				}
			}
			if(!found) {
				par_group->source_loc.print_error_header();
				fatal_error("The par_group \"", par_group->name, "\" can not be indexed with the index set \"", name, "\" since none of its attached components are distributed over that index set in the model \"", model->model_name, "\".");
			}
			
			// The last two index sets are allowed to be the same, but no other duplicates are allowed.
			auto find = std::find(index_sets.begin(), index_sets.end(), index_set_id);
			if(find != index_sets.end()) {
				// If we are not currently processing the last index set or the duplicate is not the last one we inserted, there is an error
				if(idx != par_group->index_sets.size()-1 || (++find) != index_sets.end()) {
					par_group->source_loc.print_error_header();
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
	
	auto module_scope = model->get_scope(module_id);
	
	auto group_id = module_scope->deserialize(par_group_info->name, Reg_Type::par_group);
	
	// TODO: Error messages should reference the name of the module template rather than the module maybe.
	
	if(!is_valid(group_id)) {
		par_group_info->source_loc.print_error_header();
		if(is_valid(module_id))
			fatal_error("The module \"", model->modules[module_id]->name, "\" does not contain the parameter group \"", par_group_info->name, "\".");
			//TODO: say what file the module was declared in?
		else
			fatal_error("The model does not contain the parameter group \"", par_group_info->name, "\".");
	}
	
	bool module_is_outdated = false;
	if(module_info && is_valid(module_id)) {
		auto mod  = model->modules[module_id];
		auto temp = model->module_templates[mod->template_id];
		module_is_outdated = (module_info->version < temp->version);
	}
	
	auto group_scope = model->get_scope(group_id);
	for(auto &par : par_group_info->pars) {
		
		if(par.mark_for_deletion) continue;
		
		auto par_id = group_scope->deserialize(par.name, Reg_Type::parameter);
		
		if(!is_valid(par_id)) {
			if(module_is_outdated) {
				par.source_loc.print_log_header();
				log_print("The parameter group \"", par_group_info->name, "\" in the module \"", model->modules[module_id]->name, "\" does not contain a parameter named \"", par.name, "\". The version of the module in the model code is newer than the version in the data, so this may be due to a change in the model. If you save over this data file, the parameter will be removed from the data.\n");
				par.mark_for_deletion = true;
			} else {
				par.source_loc.print_error_header();
				fatal_error("The parameter group \"", par_group_info->name, "\" does not contain a parameter named \"", par.name, "\".");
			}
			continue;
		}
		
		auto param = model->parameters[par_id];
		
		if(param->decl_type != par.type) {
			par.source_loc.print_error_header();
			fatal_error("The parameter \"", par.name, "\" should be of type ", name(param->decl_type), ", not of type ", name(par.type), ".");
		}
		
		// NOTE: This should be tested for somewhere else already, so this is just a safety
		// tripwire.
		s64 expect_count = app->active_instance_count(app->parameter_structure.get_index_sets(par_id));
		if(expect_count != par.get_count())
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
					par.source_loc.print_error_header();
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
process_series_metadata(Model_Application *app, Data_Set *data_set, Series_Set_Info *series, Series_Metadata *metadata) {
	
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
		
		check_allowed_serial_name(header.name, header.source_loc);
		
		// NOTE: several time series could have been given the same name.
		std::set<Var_Id> ids = app->vars.find_by_name(header.name);
		
		if(ids.size() > 1) {
			header.source_loc.print_log_header();
			log_print("The name \"", header.name, "\" is not unique, and identifies multiple series.\n");
		} else if(ids.empty()) {
			//This series is not recognized as a model input, so it is an "additional series"
			// See if it was already registered.
			
			auto var_id = app->vars.register_var<State_Var::Type::declared>(invalid_var_location, header.name, Var_Id::Type::additional_series);
			ids.insert(var_id);
			auto var = app->vars[var_id];
			var->set_flag(State_Var::clear_series_to_nan);
			var->unit = header.unit;
			
			//TODO check that the units of multiple instances of the same series cohere. Or should the rule be that only the first instance declares the unit? In that case we should still give an error if later cases declares a unit. Or should we be even more fancy and allow for automatic unit conversions?
		}
		
		if(header.indexes.empty()) continue;
		
		if(ids.begin()->type == Var_Id::Type::series) {
			if(metadata->index_sets.find(*ids.begin()) != metadata->index_sets.end()) continue; // NOTE: already set by another series data block.
		} else {
			if(metadata->index_sets_additional.find(*ids.begin()) != metadata->index_sets_additional.end()) continue;
		}
		
		for(auto &index : header.indexes[0]) {  // NOTE: just check the index sets of the first index tuple. We check for internal consistency between tuples somewhere else.
			// NOTE: this should be valid since we already tested it internally in the data set.
			auto idx_set = data_set->index_sets[index.first];
			Entity_Id index_set = model->model_decl_scope.deserialize(idx_set->name, Reg_Type::index_set);
			if(!is_valid(index_set))
				fatal_error(Mobius_Error::internal, "Invalid index set for series in data set.");
			for(auto id : ids) {
				
				if(id.type == Var_Id::Type::series) {// Only perform the check for model inputs, not additional series.
					auto comp_id     = app->vars[id]->loc1.first();
					auto comp        = model->components[comp_id];

					if(std::find(comp->index_sets.begin(), comp->index_sets.end(), index_set) == comp->index_sets.end()) {
						header.source_loc.print_error_header();
						fatal_error("Can not set \"", idx_set->name, "\" as an index set dependency for the series \"", header.name, "\" since the compartment \"", comp->name, "\" is not distributed over that index set.");
					}
					
					metadata->index_sets[id].push_back(index_set);
				} else
					metadata->index_sets_additional[id].push_back(index_set);
			}
		}
	}
}

void
process_series(Model_Application *app, Data_Set *data_set, Series_Set_Info *series_info, Date_Time end_date);

void
Model_Application::set_indexes(Entity_Id index_set, std::vector<std::string> &indexes, Index_T parent_idx) {
	auto sub_indexed_to = model->index_sets[index_set]->sub_indexed_to;
	if(is_valid(sub_indexed_to)) {
		log_print("Names for sub-indexed index sets are not yet supported. Replacing with numerical indexes.");
		set_index_count(index_set, (int)indexes.size(), parent_idx);
	} else {
		index_counts[index_set.id] = { Index_T {index_set, (s32)indexes.size()} };
		index_names[index_set.id].resize(indexes.size());
		s32 idx = 0;
		for(auto index : indexes) {
			index_names_map[index_set.id][index] = Index_T {index_set, idx};
			index_names[index_set.id][idx] = index;
			++idx;
		}
	}
}

void
Model_Application::set_index_count(Entity_Id index_set, int count, Index_T parent_idx) {
	
	index_names[index_set.id].clear();
	auto sub_indexed_to = model->index_sets[index_set]->sub_indexed_to;
	if(is_valid(sub_indexed_to)) {
		if(!is_valid(parent_idx)) { // If we did not provide an index for the parent index set of this one (if it exists), we set the same index count to all instances of the parent.
			auto par_count = get_max_index_count(sub_indexed_to);
			for(Index_T par_idx = { sub_indexed_to, 0 }; par_idx < par_count; ++par_idx)
				set_index_count(index_set, count, par_idx);
			return;
		}
		if(parent_idx.index_set != sub_indexed_to)
			fatal_error(Mobius_Error::internal, "Mis-matched parent index in set_indexes.");
		auto &counts = index_counts[index_set.id];
		s64 parent_count = get_max_index_count(sub_indexed_to).index;
		if(!parent_count)
			fatal_error(Mobius_Error::internal, "Tried to set indexes of an index set that was sub-indexed before setting the indexes of the parent.");
		counts.resize(parent_count, {index_set, 0});
		counts[parent_idx.index].index = count;
	} else {
		index_counts[index_set.id] = { Index_T {index_set, (s32)count} };
	}
}

/*
Index_T
Model_Application::map_down_from_union(Index_T index) {
	Index_T result = index;
	auto set = model->index_sets[index.index_set];
	if(set->union_of.empty())
		return result;
	for(auto ui_id : set->union_of) {
		auto count = get_max_index_count(ui_id);   // NOTE: Only ok since we don't support sub-indexed unions.
		if(count.index < result.index)
			result.index -= count.index;
		else {
			result.index_set = ui_id;
			break;
		}
	}
	return result;
}

Index_T
Model_Application::map_up_to_union(Index_T index, Entity_Id union_id) {
	auto set = model->index_sets[union_id];
	bool found = false;
	Index_T result = index;
	for(auto ui_id : set->union_of) {
		if(ui_id == index.index_set) {
			found = true;
			result.index_set = union_id;
			break;
		}
		result.index += get_max_index_count(ui_id).index; // NOTE: Only ok since we don't support sub-indexed unions.
	}
	if(!found)
		fatal_error(Mobius_Error::internal, "Mis-use of map_up function.");
	return result;
}
*/

Index_T
Model_Application::get_max_index_count(Entity_Id index_set) {
	auto &count = index_counts[index_set.id];
	if(count.empty()) return Index_T {index_set, 0};
	Index_T max = count[0];
	for(auto &c : count) max = std::max(max, c);
	return max;
}

Index_T
Model_Application::get_index_count(Entity_Id index_set, Indexes &indexes) {
	auto set = model->index_sets[index_set];
	if(is_valid(set->sub_indexed_to)) {
		if(indexes.lookup_ordered) {
			for(auto &index : indexes.indexes) {
				if(index.index_set == set->sub_indexed_to)
					return index_counts[index_set.id][index.index];
			}
		} else {
			return index_counts[index_set.id][indexes.indexes[set->sub_indexed_to.id].index];
		}
	}
	return index_counts[index_set.id][0];
}

Math_Expr_FT *
Model_Application::get_index_count_code(Entity_Id index_set, Index_Exprs &indexes) {
	
	// If the index count could depend on the state of another index set, we have to look it up dynamically
	if(is_valid(model->index_sets[index_set]->sub_indexed_to)) {
		auto offset = index_counts_structure.get_offset_code(index_set, indexes);
		auto ident = new Identifier_FT();
		ident->value_type = Value_Type::integer;
		ident->variable_type = Variable_Type::index_count;
		ident->exprs.push_back(offset);
		return ident;
	}
	// Otherwise we can just return the constant.
	return make_literal((s64)get_max_index_count(index_set).index);
}

s64
Model_Application::active_instance_count(const std::vector<Entity_Id> &index_sets) {
	s64 count = 1;
	if(index_sets.empty()) return count;
	
	for(int idx = 0; idx < index_sets.size(); ++idx) {
		
		auto index_set = model->index_sets[index_sets[idx]];
		if(is_valid(index_set->sub_indexed_to)) {
			// NOTE: For now removing the correctness check of the parent index set appearing first here since it should be sufficiently checked elsewhere
			s64 sum = 0;
			for(Index_T &c : index_counts[index_sets[idx].id]) sum += c.index;
			count *= sum;
			count /= get_max_index_count(index_set->sub_indexed_to).index; // NOTE: This is overcounted in the sum.
		} else
			count *= get_max_index_count(index_sets[idx]).index;
	}
	return count;
}

bool
Model_Application::is_in_bounds(Indexes &indexes) {
	for(auto &index : indexes.indexes) {
		if(is_valid(index)) {
			auto count = get_index_count(index.index_set, indexes);
			if(index >= count) { // NOTE: We should NOT check for index.index < 0, since that just means that this index is not set right now.
				//log_print("Failed for set ", model->index_sets[index.index_set]->name, " because index is ", index.index, " and count is ", count.index, "\n");
				return false;
			}
		}
	}
	return true;
}

Index_T
Model_Application::get_index(Entity_Id index_set, const std::string &name) {
	
	auto &map = index_names_map[index_set.id];
	Index_T result = invalid_index;
	
	if(!map.empty()) {
		auto find = map.find(name);
		if(find != map.end())
			return find->second;
	} else {
		// TODO: Should be made more robust
		result.index_set = index_set;
		result.index = atoi(name.data());
	}
	return result;
}

std::string
Model_Application::get_index_name(Index_T index, bool *is_quotable) {
	if(!is_valid(index) || index.index < 0 || index.index > get_max_index_count(index.index_set).index) // TODO : it is ok now since we only allow named index sets for indexes that are not sub-indexed.
		fatal_error(Mobius_Error::internal, "Index out of bounds in get_index_name");
	
	if(index_names[index.index_set.id].empty()) {
		char buf[16];
		itoa(index.index, buf, 10);
		if(is_quotable) *is_quotable = false;
		return buf;
	} else {
		if(is_quotable) *is_quotable = true;
		return index_names[index.index_set.id][index.index];
	}
}

void
put_index_name_with_edge_naming(Model_Application *app, Index_T &index, Indexes &indexes, std::vector<std::string> &names_out, int pos, bool quote) {
	
	auto model = app->model;
	bool is_quotable = false;
	
	auto index_set = model->index_sets[index.index_set];
	Entity_Id conn_id = index_set->is_edge_of_connection;
	if(!is_valid(conn_id) || model->connections[conn_id]->type != Connection_Type::directed_graph) {
		names_out[pos] = app->get_index_name(index, &is_quotable);
	} else {
		Index_T parent_idx = invalid_index;
		if(is_valid(index_set->sub_indexed_to)) {
			for(Index_T &idx2 : indexes.indexes) {
				if(idx2.index_set == index_set->sub_indexed_to)
					parent_idx = idx2;
			}
		}
		
		// If this index set is the edge index set of a graph connection, we generate a name for the index by the target of the edge (arrow).
		//TODO: Can we make a faster way to find the arrow
		auto &arrows = app->connection_components[conn_id].arrows;
		for(auto arr : arrows) {
			auto source_idx = invalid_index;
			if(arr.source_indexes.indexes.size() == 1)
				source_idx = arr.source_indexes.indexes[0];
			else if(arr.source_indexes.indexes.size() > 1)
				fatal_error(Mobius_Error::internal, "Graph arrows with multiple source indexes not supported by get_index_name.");
			
			if(arr.edge_index == index && source_idx == parent_idx) {
				if(is_valid(arr.target_id)) {
					if(arr.target_indexes.indexes.empty()) {
						names_out[pos] = model->components[arr.target_id]->name;
						is_quotable = true;
					} else if (arr.target_indexes.indexes.size() == 1) {
						names_out[pos] = app->get_index_name(arr.target_indexes.indexes[0], &is_quotable);
					} else
						fatal_error(Mobius_Error::internal, "Graph arrows with multiple target indexes not supported by get_index_name.");
				} else {
					is_quotable = false;
					names_out[pos] = "out";
				}
			}
		}
	}
	
	if(quote && is_quotable) {
		names_out[pos] = "\"" + names_out[pos] + "\"";
	}
}

void
Model_Application::get_index_names_with_edge_naming(Indexes &indexes, std::vector<std::string> &names_out, bool quote) {
	
	int count = indexes.indexes.size();
	if(is_valid(indexes.mat_col)) ++count;
	names_out.resize(count);
	
	int pos = 0;
	for(auto &index : indexes.indexes) {
		if(!is_valid(index)) { pos++; continue; }
		put_index_name_with_edge_naming(this, index, indexes, names_out, pos, quote);
		++pos;
	}
	if(is_valid(indexes.mat_col))
		put_index_name_with_edge_naming(this, indexes.mat_col, indexes, names_out, count-1, quote);
}

std::string
Model_Application::get_possibly_quoted_index_name(Index_T index) {
	bool is_quotable;
	std::string result = get_index_name(index, &is_quotable);
	if(is_quotable)
		result = "\"" + result + "\"";
	return result;
}

void
add_connection_component(Model_Application *app, Data_Set *data_set, Component_Info *comp, Entity_Id connection_id, Entity_Id component_id, bool single_index_only) {
	
	// TODO: In general we should be more nuanced with what source location we print for errors in this procedure.
	
	auto model = app->model;
	auto component = model->components[component_id];
	
	if(component->decl_type != Decl_Type::compartment) {
		comp->source_loc.print_error_header();
		fatal_error("The name \"", comp->name, "\" does not refer to a compartment. This connection type only supports compartments, not quantities.");
	}
	
	auto cnd = model->connections[connection_id];
	
	auto find_decl = std::find_if(cnd->components.begin(), cnd->components.end(), [=](auto &pair){ return (pair.first == component_id); });
	if(find_decl == cnd->components.end()) {
		comp->source_loc.print_error_header();
		error_print("The connection \"", cnd->name,"\" is not allowed for the component \"", component->name, "\". See declaration of the connection:\n");
		cnd->source_loc.print_error();
		mobius_error_exit();
	}
	Entity_Id edge_index_set = find_decl->second;
	
	if(comp->edge_index_set >= 0) {
		if(cnd->type == Connection_Type::directed_tree) {
			comp->source_loc.print_error_header();
			fatal_error("The components of a 'directed_tree' should not be provided with an edge index set in the data set.");
		}
		auto idx_set_info = data_set->index_sets[comp->edge_index_set];
		auto proposed_edge_index_set = model->model_decl_scope.deserialize(idx_set_info->name, Reg_Type::index_set);
		if(!is_valid(proposed_edge_index_set)) {
			idx_set_info->source_loc.print_error_header();
			fatal_error("The index set \"", idx_set_info->name, "\" is not found in the model.");
		}
		if(is_valid(proposed_edge_index_set) && (proposed_edge_index_set != edge_index_set)) {
			idx_set_info->source_loc.print_error_header();
			fatal_error("The edge index set of this component in the data set does not match the edge index set given in the model.");
		}
	} else if (cnd->type == Connection_Type::directed_graph) {
		comp->source_loc.print_error_header();
		fatal_error("The components of a 'directed_graph' connection must be provided with an edge index set in the data set.");
	}
	
	
	if(single_index_only && comp->index_sets.size() != 1) {
		comp->source_loc.print_error_header();
		fatal_error("This connection type only supports connections on components that are indexed by a single index set");
	}
	std::vector<Entity_Id> index_sets;
	for(int idx_set_id : comp->index_sets) {
		auto idx_set_info = data_set->index_sets[idx_set_id];
		auto index_set = model->model_decl_scope.deserialize(idx_set_info->name, Reg_Type::index_set);
		if(!is_valid(index_set)) {
			idx_set_info->source_loc.print_error_header();
			fatal_error("The index set \"", idx_set_info->name, " does not exist in the model.");  // Actually, this has probably been checked somewhere else already.
		}
		if(std::find(component->index_sets.begin(), component->index_sets.end(), index_set) == component->index_sets.end()) {
			comp->source_loc.print_error_header();
			fatal_error("The index sets indexing a component in a connection relation must also index that component in the model. The index set \"", idx_set_info->name, "\" does not index the component \"", component->name, "\" in the model");
		}
		index_sets.push_back(index_set);
	}
	
	if(is_valid(edge_index_set) && !index_sets.empty()) {
		if(model->index_sets[edge_index_set]->sub_indexed_to != index_sets[0]) {
			comp->source_loc.print_error_header();
			error_print("The edge index set for this component is not declared as sub-indexed to the index set of the component. See declaration of the edge index set:\n");
			model->index_sets[edge_index_set]->source_loc.print_error();
			mobius_error_exit();
		}
	}
	
	auto &components = app->connection_components[connection_id].components;
	auto find = std::find_if(components.begin(), components.end(), [component_id](const Sub_Indexed_Component &comp) -> bool { return comp.id == component_id; });
	if(find != components.end()) {
		if(index_sets != find->index_sets) { // This should no longer be necessary when we make component declarations local to the connection data in the data set.
			comp->source_loc.print_error_header();
			fatal_error("The component \"", app->model->components[component_id]->name, "\" appears twice in the same connection relation, but with different index set dependencies.");
		}
	} else {
		Sub_Indexed_Component comp;
		comp.id = component_id;
		comp.index_sets = index_sets;
		comp.edge_index_set = edge_index_set;
		components.push_back(std::move(comp));
	}
}

void
set_up_simple_connection_components(Model_Application *app, Entity_Id conn_id) {
	
	// This is for all_to_all and grid1d where there is only one component.
	
	auto conn = app->model->connections[conn_id];
	
	if(conn->components.size() != 1 || !is_valid(conn->components[0].second))
		fatal_error(Mobius_Error::internal, "Somehow model declaration did not set up component data correctly for a grid1d or all_to_all.");
	
	auto &component = conn->components[0];
	
	auto comp_type = app->model->components[component.first]->decl_type;
	if(conn->type != Connection_Type::all_to_all && comp_type != Decl_Type::compartment || comp_type == Decl_Type::property) {
		conn->source_loc.print_error_header();
		fatal_error("This connection can't have nodes of this component type.");
	}
	
	Sub_Indexed_Component comp;
	comp.id = component.first;
	comp.index_sets.push_back(component.second);
	app->connection_components[conn_id].components.push_back(comp);
}

void
pre_process_connection_data(Model_Application *app, Connection_Info &connection, Data_Set *data_set, const std::string &module_name = "") {
	
	auto model = app->model;
	
	Entity_Id module_id = invalid_entity_id;
	if(!module_name.empty())
		module_id = model->deserialize(module_name, Reg_Type::module);
	
	auto scope = model->get_scope(module_id);
	
	auto conn_id = scope->deserialize(connection.name, Reg_Type::connection);
	
	if(!is_valid(conn_id)) {
		connection.source_loc.print_error_header();
		fatal_error("The connection structure \"", connection.name, "\" has not been declared in the model.");
	}

	auto cnd = model->connections[conn_id];
	
	if(cnd->type == Connection_Type::all_to_all || cnd->type == Connection_Type::grid1d) {
		connection.source_loc.print_error_header();
		fatal_error("No connection data should be provided for connections of type 'all_to_all' or 'grid1d'");
	}
	
	bool single_index_only = false;
	// TODO: Should also allow 0 index sets for directed_graph, not exactly one.
	// TODO: Should make sure the edge index set is sub-indexed to the component index set for this one.
	if(cnd->type == Connection_Type::directed_graph)
		single_index_only = true;
	
	for(auto &comp : connection.components) {
		Entity_Id comp_id = model->model_decl_scope.deserialize(comp.name, Reg_Type::component);
		if(!is_valid(comp_id)) {
			comp.source_loc.print_error_header();
			fatal_error("The component \"", comp.name, "\" has not been declared in the model.");
			//comp.source_loc.print_log_header();
			//log_print("The component \"", comp.name, "\" has not been declared in the model.\n");
			//continue;
		}
		add_connection_component(app, data_set, &comp, conn_id, comp_id, single_index_only);
	}
	
	if(cnd->type != Connection_Type::directed_tree && cnd->type != Connection_Type::directed_graph) 
		fatal_error(Mobius_Error::internal, "Unsupported connection structure type in pre_process_connection_data().");
	
	// NOTE: We allow empty info for this connection type, in which case the data type is 'none'.
	if(connection.type != Connection_Info::Type::graph && connection.type != Connection_Info::Type::none) {
		connection.source_loc.print_error_header();
		fatal_error("Connection structures of type directed_tree or directed_graph can only be set up using graph data.");
	}
	
	for(auto &arr : connection.arrows) {
		
		Connection_Arrow arrow;
		
		auto comp_source = connection.components[arr.first.id];
		arrow.source_id = model->model_decl_scope.deserialize(comp_source->name, Reg_Type::component);
	
		if(arr.second.id >= 0) {
			auto comp_target = connection.components[arr.second.id];
			arrow.target_id = model->model_decl_scope.deserialize(comp_target->name, Reg_Type::component);
		}
		
		// Note: can happen if we are running with a subset of the larger model the dataset is set up for, and the subset doesn't have these compoents.
		if( !is_valid(arrow.source_id) || (!is_valid(arrow.target_id) && arr.second.id >= 0) ) continue;
		
		// Store useful information that allows us to prune away un-needed operations later.
		auto source = app->find_connection_component(conn_id, arrow.source_id);
		if(is_valid(arrow.target_id))
			source->can_be_located_source = true;
		source->total_as_source++;
		
		// TODO: Maybe asser the two vectors are the same size (would mean bug in Data_Set code if they are not).
		int idx = 0;
		for(auto index_set : source->index_sets) {
			arrow.source_indexes.add_index(Index_T { index_set, (s16)arr.first.indexes[idx] });
			++idx;
		}
		
		source->possible_targets.insert(arrow.target_id);
		
		if(is_valid(arrow.target_id)) {
			auto target = app->find_connection_component(conn_id, arrow.target_id);
			target->possible_sources.insert(arrow.source_id);
			source->max_target_indexes = std::max((int)target->index_sets.size(), source->max_target_indexes);
		
			
		
			// TODO: Maybe asser the two vectors are the same size.
			int idx = 0;
			for(auto index_set : target->index_sets) {
				arrow.target_indexes.add_index(Index_T { index_set, (s16)arr.second.indexes[idx] });
				++idx;
			}
		}
		
		app->connection_components[conn_id].arrows.push_back(std::move(arrow));
	}
	
	if(cnd->type == Connection_Type::directed_graph) {
		// Set up the index set of the edge index.
		
		auto &comps = app->connection_components[conn_id];
		
		Storage_Structure<Entity_Id> components_structure(app);
		make_connection_component_indexing_structure(app, &components_structure, conn_id);
		std::vector<int> n_edges(components_structure.total_count);
		
		for(auto &arrow : comps.arrows) {
			s64 offset = components_structure.get_offset(arrow.source_id, arrow.source_indexes);
			s16 edge_idx = n_edges[offset]++;
			Entity_Id edge_index_set = app->find_connection_component(conn_id, arrow.source_id)->edge_index_set;
			if(!is_valid(edge_index_set))
				fatal_error(Mobius_Error::internal, "Got a directed_graph connection that has a component without an edge index set.");
			arrow.edge_index = Index_T { edge_index_set, edge_idx };
		}
		
		for(auto &comp : comps.components) {
			if(comp.index_sets.size() != 1)   //TODO: Also allow zero index sets.
				fatal_error(Mobius_Error::internal, "Somehow got not exactly 1 index set for graph component.");
			Entity_Id index_set = comp.index_sets[0];
			
			//TODO: This may be broken if we allow sub-indexing of sub-indexing.
			// Also may be broken unless we *require* the edge index set to be sub-indexed (but we should do that).
			Indexes indexes(Index_T {index_set, 0});
			for(; indexes.indexes[0] < app->get_max_index_count(index_set); ++indexes.indexes[0]) {
				s64 offset = components_structure.get_offset(comp.id, indexes);
				app->set_index_count(comp.edge_index_set, n_edges[offset], indexes.indexes[0]);
			}
		}
	}
	
	match_regex(app, conn_id, connection.source_loc);
}


void
process_connection_data(Model_Application *app, Entity_Id conn_id) {
	
	auto model = app->model;
	
	auto cnd = model->connections[conn_id];
		
	if(cnd->type == Connection_Type::all_to_all || cnd->type == Connection_Type::grid1d)
		return;  // Nothing else to do for these.
		
	if(cnd->type != Connection_Type::directed_tree && cnd->type != Connection_Type::directed_graph)
		fatal_error(Mobius_Error::internal, "Unsupported connection structure type in process_connection_data().");

	auto &comps = app->connection_components[conn_id];

	for(auto &arr : comps.arrows) {
		
		Sub_Indexed_Component *component = app->find_connection_component(conn_id, arr.source_id);
		
		auto &index_sets = component->index_sets;
		Indexes indexes = arr.source_indexes;
		if(cnd->type == Connection_Type::directed_graph)
			indexes.add_index(arr.edge_index);
		
		s64 offset = app->connection_structure.get_offset({conn_id, arr.source_id, 0}, indexes);
		s32 id_code = (is_valid(arr.target_id) ? (s32)arr.target_id.id : -1);
		*app->data.connections.get_value(offset) = id_code;
		
		for(int idx = 0; idx < arr.target_indexes.indexes.size(); ++idx) {
			int id = idx+1;
			s64 offset = app->connection_structure.get_offset({conn_id, arr.source_id, id}, indexes);
			*app->data.connections.get_value(offset) = (s32)arr.target_indexes.indexes[idx].index;
		}
	}

}

void
process_index_set_data(Model_Application *app, Data_Set *data_set, Index_Set_Info &index_set) {
	
	// TODO: Should test that what we get in the data set is not an edge index set (since these need to be generated).
		// Also should not save edge index sets (elsewhere).
	
	auto model = app->model;
	auto id = model->model_decl_scope.deserialize(index_set.name, Reg_Type::index_set);
	//auto id = model->index_sets.find_by_name(index_set.name);
	if(!is_valid(id)) {
		// TODO: Should be just a warning here instead, but then we have to follow up and make it properly handle declarations of series data that is indexed over this index set.
		index_set.source_loc.print_error_header();
		fatal_error("\"", index_set.name, "\" has not been declared as an index set in the model \"", model->model_name, "\".");
		//continue;
	}
	if(!model->index_sets[id]->union_of.empty()) {
		// Check that the unions match
		std::vector<int> union_of = index_set.union_of;
		bool error = false;
		for(auto ui_id : model->index_sets[id]->union_of) {
			int ui_id_dataset = data_set->index_sets.find_idx(model->index_sets[ui_id]->name);
			auto find = std::find(union_of.begin(), union_of.end(), ui_id_dataset);
			if(find == union_of.end()) {
				error = true;
				break;
			}
			*find = -10;
		}
		if(!error) {
			for(int ui_id_dataset : union_of) {
				if(ui_id_dataset != -10) {
					error = true;
					break;
				}
			}
		}
		if(error) {
			index_set.source_loc.print_error_header();
			fatal_error("This index set is declared as a union in the model, but is not the same union in the data set.");
		}
		
		// For union index sets, the index data is not separately processed here.
		return;
	} else if (!index_set.union_of.empty()) {
		index_set.source_loc.print_error_header();
		fatal_error("This index set is not declared as a union in the model, but is in the data set");
	}
	
	Entity_Id sub_indexed_to = invalid_entity_id;
	if(index_set.sub_indexed_to >= 0)
		sub_indexed_to = model->model_decl_scope.deserialize(data_set->index_sets[index_set.sub_indexed_to]->name, Reg_Type::index_set);
	if(model->index_sets[id]->sub_indexed_to != sub_indexed_to) {
		index_set.source_loc.print_error_header();
		fatal_error("The parent index set of this index set does not match between the model and the data set.");
	}
	for(int idx = 0; idx < index_set.indexes.size(); ++idx) {
		auto &idxs = index_set.indexes[idx];
		Index_T parent_idx = { sub_indexed_to, idx };
		if(idxs.type == Sub_Indexing_Info::Type::named) {
			std::vector<std::string> names;
			names.reserve(idxs.indexes.count());
			for(auto &index : idxs.indexes)
				names.push_back(index.name);
			app->set_indexes(id, names, parent_idx);
		} else if (idxs.type == Sub_Indexing_Info::Type::numeric1) {
			app->set_index_count(id, idxs.n_dim1, parent_idx);
		} else
			fatal_error(Mobius_Error::internal, "Unhandled index set info type in build_from_data_set().");
	}
}

void
Model_Application::build_from_data_set(Data_Set *data_set) {
		
	
	if(is_compiled)
		fatal_error(Mobius_Error::api_usage, "Tried to build model application after it was compiled.");
	if(this->data_set)
		fatal_error(Mobius_Error::api_usage, "Model application was provided with more than one data set.");
	this->data_set = data_set;
	
	
	std::vector<std::string> input_names;
	for(auto &series : data_set->series) {
		for(auto &header : series.header_data)
			input_names.push_back(header.name);
	}
	
	
	for(auto &index_set : data_set->index_sets)
		process_index_set_data(this, data_set, index_set);
	
	
	connection_components.initialize(model);
	
	for(auto conn_id : model->connections) {
		auto type = model->connections[conn_id]->type;
		if(type == Connection_Type::all_to_all || type == Connection_Type::grid1d)
			set_up_simple_connection_components(this, conn_id);
	}
	
	for(auto &connection : data_set->global_module.connections)
		pre_process_connection_data(this, connection, data_set);
	for(auto &module : data_set->modules) {
		for(auto &connection : module.connections)
			pre_process_connection_data(this, connection, data_set, module.name);
	}
	
	for(auto conn_id : model->connections) {
		auto &components = connection_components[conn_id].components;
		if(components.empty()) {
			fatal_error(Mobius_Error::model_building, "Did not get compartment data for the connection \"", model->connections[conn_id]->name, "\" in the data set.");
			//TODO: Should maybe just print a warning and auto-generate the data instead of having an error. This is more convenient when creating a new project.
		}
	}
	
	prelim_compose(this, input_names);

	if(data_set->time_step_was_provided) {
		time_step_unit = data_set->time_step_unit;
		bool success;
		time_step_size = time_step_unit.to_time_step(success);
		if(!success) {
			data_set->unit_source_loc.print_error_header();
			fatal_error("This is not a valid time step unit.");
		}
	}
	
	// Generate at least one index for index sets that were not provided in the data set.
	for(auto index_set : model->index_sets) {
		auto set = model->index_sets[index_set];
		// NOTE: Edge index sets are generated in a different way. Moreover, union index sets are handled differently (below).
		if(get_max_index_count(index_set).index == 0 && !is_valid(set->is_edge_of_connection) && set->union_of.empty())
			set_index_count(index_set, 1);
	}
	// Set up counts for union index sets. Note that these are not allowed to be sub-indexed currently, neither are their unionees.
	for(auto index_set : model->index_sets) {
		auto set = model->index_sets[index_set];
		if(set->union_of.empty()) continue;
		if(get_max_index_count(index_set).index != 0)
			fatal_error(Mobius_Error::internal, "An index count was explicitly set for a union index set.");
		int numeric = -1;
		for(auto ui_id : set->union_of) {
			int numeric_new = -1;
			numeric_new = index_names[ui_id.id].empty();
			if(numeric >= 0 && numeric_new != numeric) {
				fatal_error(Mobius_Error::model_building, "The index set \"", set->name, "\" is a union of index sets where the data is a mix of numeric and non-numeric index names. This is not allowed.");
				// TODO: We could maybe look into the data_set to give a couple of source locations.
			}
			numeric = numeric_new;
		}
		if(numeric) {
			s32 count = 0;
			for(auto ui_id : set->union_of)
				count += get_max_index_count(ui_id).index;
			set_index_count(index_set, count);
		} else {
			std::vector<std::string> union_names;
			for(auto ui_id : set->union_of) {
				// TODO: We must check that they are not overlapping in names.
				union_names.insert(union_names.end(), index_names[ui_id.id].begin(), index_names[ui_id.id].end());
			}
			set_indexes(index_set, union_names);
		}
	}
	
	if(!index_counts_structure.has_been_set_up)
		set_up_index_count_structure();
	
	if(!connection_structure.has_been_set_up) // Hmm, can this ever have been set up already?
		set_up_connection_structure();
	
	for(auto &conn_id : model->connections)
		process_connection_data(this, conn_id);
	
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
		Entity_Id module_id = model->model_decl_scope.deserialize(module.name, Reg_Type::module);
		if(!is_valid(module_id)) {
			log_print("In ");
			module.source_loc.print_log_header();
			log_print("The model \"", model->model_name, "\" does not contain a module named \"", module.name, "\". This data block will be ignored.\n\n");
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
			process_series_metadata(this, data_set, &series, &metadata);
		
		set_up_series_structure(Var_Id::Type::series,            &metadata);
		set_up_series_structure(Var_Id::Type::additional_series, &metadata);
		
		s64 time_steps = 0;
		if(metadata.any_data_at_all)
			time_steps = steps_between(metadata.start_date, metadata.end_date, time_step_size) + 1; // NOTE: if start_date == end_date we still want there to be 1 data point (dates are inclusive)
		else if(vars.count(Var_Id::Type::series) != 0) {
			//TODO: use the model run start and end date.
			// Or better yet, we could just bake in series default values as literals in the code. in this case.
		}
		//warning_print("Input dates: ", metadata.start_date.to_string());
		//warning_print(" ", metadata.end_date.to_string(), " ", time_steps, "\n");
	
		allocate_series_data(time_steps, metadata.start_date);
		
		for(auto &series : data_set->series)
			process_series(this, data_set, &series, metadata.end_date);
	} else {
		set_up_series_structure(Var_Id::Type::series,            nullptr);
		set_up_series_structure(Var_Id::Type::additional_series, nullptr);
	}
	
	//warning_print("Model application set up with data.\n");
}

void
Model_Application::save_to_data_set() {
	//TODO: We should probably just generate a data set in this case.
	if(!data_set)
		fatal_error(Mobius_Error::api_usage, "Tried to save model application to data set, but no data set was attached to the model application.");
	
	// NOTE : This should only write parameter values. All other editing should go directly to the data set, and one should then reload the model with the data set.
	// 		The exeption is if we generated an index for a data set that was not in the model.

	for(Entity_Id index_set_id : model->index_sets) {
		auto index_set = model->index_sets[index_set_id];
		auto index_set_info = data_set->index_sets.find(index_set->name);
		// TODO: Maybe do a sanity check that the data set contains the same indexes as the model application?
		
		if(index_set_info) continue;  // We are only interested in creating new index sets that were missing in the data set.
		
		// TODO: Hmm, this should only be skipped if it is a directed_graph?
		if(is_valid(index_set->is_edge_of_connection)) continue; // These are handled differently.
		
		// TODO: We should have some api on the data set for this.
		index_set_info = data_set->index_sets.create(index_set->name, {});
		if(!is_valid(index_set->sub_indexed_to)) {
			index_set_info->indexes.resize(1);
			index_set_info->indexes[0].type = Sub_Indexing_Info::Type::numeric1;
			index_set_info->indexes[0].n_dim1 = get_max_index_count(index_set_id).index; // Should be 1 unless we change the code somewhere else.
		} else {
			index_set_info->sub_indexed_to = data_set->index_sets.find_idx(model->index_sets[index_set->sub_indexed_to]->name);
			auto parent_count = get_max_index_count(index_set->sub_indexed_to);
			index_set_info->indexes.resize(parent_count.index);
			for(Index_T parent_idx = {index_set->sub_indexed_to, 0}; parent_idx < parent_count; ++parent_idx) {
				Indexes indexes(parent_idx);
				index_set_info->indexes[parent_idx.index].type = Sub_Indexing_Info::Type::numeric1;
				index_set_info->indexes[parent_idx.index].n_dim1 = get_index_count(index_set_id, indexes).index;
			}
		}
	}
	
	// Hmm, this is a bit cumbersome
	for(int idx = -1; idx < model->modules.count(); ++idx) {
		Entity_Id module_id = invalid_entity_id;
		if(idx >= 0) module_id = { Reg_Type::module, (s16)idx };
		
		Module_Info *module_info = nullptr;
		if(idx < 0)
			module_info = &data_set->global_module;
		else {
			auto module = model->modules[module_id];
			std::string &name = module->name;
			module_info = data_set->modules.find(name);
		
			if(!module_info)
				module_info = data_set->modules.create(name, {});
			
			module_info->version = model->module_templates[module->template_id]->version;
		}
		
		for(auto group_id : model->by_scope<Reg_Type::par_group>(module_id)) {
			auto par_group = model->par_groups[group_id];
			
			Par_Group_Info *par_group_info = module_info->par_groups.find(par_group->name);
			if(!par_group_info)
				par_group_info = module_info->par_groups.create(par_group->name, {});
			
			par_group_info->index_sets.clear();
			
			// TODO: not sure if we should have an error for an empty par group.
			bool index_sets_resolved = false;
			
			for(auto par_id : model->by_scope<Reg_Type::parameter>(group_id)) {
				
				if(!index_sets_resolved) {
					auto &index_sets = parameter_structure.get_index_sets(par_id);
				
					for(auto index_set_id : index_sets) {
						auto index_set = model->index_sets[index_set_id];
						
						int	index_set_idx = data_set->index_sets.find_idx(index_set->name);
						
						if(index_set_idx < 0) {
							par_group_info->error = true;
							log_print("WARNING: Tried to set an index set dependency \"", index_set->name, "\" for the parameter group \"", par_group->name, "\" in the data set, but the index set was not in the data set. This will cause this parameter group to not be saved.\n");
							break;
						}
						
						par_group_info->index_sets.push_back(index_set_idx);
					}
					if(par_group_info->error) break;
					
					index_sets_resolved = true;
				}
				
				auto par = model->parameters[par_id];
				
				auto par_info = par_group_info->pars.find(par->name);
				if(!par_info)
					par_info = par_group_info->pars.create(par->name, {});
				
				par_info->type = par->decl_type;
				par_info->values.clear();
				par_info->values_enum.clear();
				par_info->mark_for_deletion = false;
				
				parameter_structure.for_each(par_id, [&,this](Indexes &idxs, s64 offset) {
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

void
make_connection_component_indexing_structure(Model_Application *app, Storage_Structure<Entity_Id> *components_structure, Entity_Id connection_id) {
	
	auto &comps = app->connection_components[connection_id].components;
	
	// TODO: Could also group them by the index sets.
	std::vector<Multi_Array_Structure<Entity_Id>> structure;
	for(auto &comp : comps) {
		std::vector<Entity_Id> index_sets = comp.index_sets;
		structure.push_back(std::move(Multi_Array_Structure<Entity_Id>(std::move(index_sets), {comp.id})));
	}
	
	components_structure->set_up(std::move(structure));
}

inline size_t
round_up(int align, size_t size) {
	int rem = size % align;
	if(rem == 0) return size;
	return size + (align - rem);
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
		//auto sz2 = round_up(data_alignment, sz);
		//data = (Val_T *) _aligned_malloc(sz2, data_alignment);  // should be replaced with std::aligned_alloc(data_alignment, sz2) when that is available.
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
	auto id = app->model->model_decl_scope["start_date"]->id;
	auto offset = parameters.structure->get_offset_base(id);                    // NOTE: it should not be possible to index this over an index set any way.
	return parameters.get_value(offset)->val_datetime;
}

Date_Time
Model_Data::get_end_date_parameter() {
	auto id = app->model->model_decl_scope["end_date"]->id;
	auto offset = parameters.structure->get_offset_base(id);                    // NOTE: it should not be possible to index this over an index set any way.
	return parameters.get_value(offset)->val_datetime;
}

Model_Data::Model_Data(Model_Application *app) :
	app(app), parameters(&app->parameter_structure), series(&app->series_structure),
	results(&app->result_structure, 1), connections(&app->connection_structure),
	additional_series(&app->additional_series_structure), index_counts(&app->index_counts_structure) {
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
	cpy->index_counts.refer_to(&this->index_counts);
	return cpy;
}

inline void
serialize_loc(Mobius_Model *model, std::stringstream &ss, const Var_Location &loc) {
	for(int idx = 0; idx < loc.n_components; ++idx) {
		ss << model->serialize(loc.components[idx]);
		if(idx != loc.n_components-1) ss << ':';
	}
}

std::string
Model_Application::serialize(Var_Id id) { 
	
	auto var = vars[id];
	if(id.type != Var_Id::Type::additional_series) {
		std::stringstream ss;
		ss << ':';
		if(var->type == State_Var::Type::declared) {
			auto var2 = as<State_Var::Type::declared>(var);
			if(var2->is_flux())
				ss << model->serialize(var2->decl_id);
			else
				serialize_loc(model, ss, var2->loc1);
		} else if(var->type == State_Var::Type::dissolved_conc) {
			auto var2 = as<State_Var::Type::dissolved_conc>(var);
			auto var_mass = vars[var2->conc_of];
			ss << "dissolved_conc@";
			serialize_loc(model, ss, var_mass->loc1);
		} else if(var->type == State_Var::Type::dissolved_flux) {
			auto var2 = as<State_Var::Type::dissolved_flux>(var);
			auto var_conc = as<State_Var::Type::dissolved_conc>(vars[var2->conc]);
			auto var_mass = vars[var_conc->conc_of];
			State_Var *var_flux = var;
			while(var_flux->type != State_Var::Type::declared)   // NOTE: It could be a chain of dissolvedes.
				var_flux = vars[as<State_Var::Type::dissolved_flux>(var_flux)->flux_of_medium];
			ss << "dissolved_flux@";
			ss << model->serialize(as<State_Var::Type::declared>(var_flux)->decl_id) << '@';
			serialize_loc(model, ss, var_mass->loc1);
		} else if(var->type == State_Var::Type::regular_aggregate) {
			auto var2 = as<State_Var::Type::regular_aggregate>(var);
			ss << "regular_aggregate@";
			ss << model->serialize(var2->agg_to_compartment) << '@';
			ss << serialize(var2->agg_of);
		} else if(var->type == State_Var::Type::in_flux_aggregate) {
			auto var2 = as<State_Var::Type::in_flux_aggregate>(var);
			auto var_to = vars[var2->in_flux_to];
			ss << "in_flux_aggregate@";
			serialize_loc(model, ss, var_to->loc1);
		} else if(var->type == State_Var::Type::connection_aggregate) {
			auto var2 = as<State_Var::Type::connection_aggregate>(var);
			auto agg_for = vars[var2->agg_for];
			ss << "connection_aggregate@";
			ss << (var2->is_source ? "source@" : "target@");
			ss << model->serialize(var2->connection) << '@';
			serialize_loc(model, ss, agg_for->loc1);
		} else if(var->type == State_Var::Type::external_computation) {
			ss << "external_computation@" << var->name;
		} else
			fatal_error(Mobius_Error::internal, "Unsupported State_Var::Type in serialize()");
		return ss.str();
	} else
		return var->name;
	fatal_error(Mobius_Error::internal, "Unimplemented possibility in Model_Application::serialize().");
	return "";
}
	
Var_Id
Model_Application::deserialize(const std::string &name) {

	if(name.data()[0] == ':') {
		auto find = serial_to_id.find(name);
		if(find == serial_to_id.end()) return invalid_var;
		return find->second;
	} 
	const auto &ids = vars.find_by_name(name);
	if(!ids.empty())
		return *ids.begin();
		
	return invalid_var;
}

