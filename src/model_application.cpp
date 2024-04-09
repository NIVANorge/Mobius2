
#include "model_application.h"

#include <map>
#include <cstdlib>
#include <sstream>

void
Index_Exprs::clean() {
	for(int idx = 0; idx < indexes.size(); ++idx) {
		delete indexes[idx];
		indexes[idx] = nullptr;
	}
}
	
void
Index_Exprs::copy(Index_Exprs &other) {
	clean();
	indexes = other.indexes;
	for(auto &idx : indexes)
		if(idx) idx = ::copy(idx);
}

// Hmm, this could also maybe be used elsewhere. Put it as utility in function_tree.h ?
Math_Expr_FT *
add_exprs(Math_Expr_FT *lhs, Math_Expr_FT *rhs) {
	if(!lhs) return rhs;
	return make_binop('+', lhs, rhs);
}

Math_Expr_FT *
Index_Exprs::get_index(Model_Application *app, Entity_Id index_set, Entity_Id *index_set_out) {
	
	Math_Expr_FT *result = nullptr;
	
	if(index_set_out)
		*index_set_out = index_set;
	if(indexes[index_set.id]) {
		result = ::copy(indexes[index_set.id]);
	} else {
		// Try to index a union index set (if a direct index is absent) by an index belonging to a member of that union.
		bool found = false;
		auto set = app->model->index_sets[index_set];
		for(auto ui_id : set->union_of) {
			if(indexes[ui_id.id]) {
				result = add_exprs(result, ::copy(indexes[ui_id.id]));
				if(index_set_out)
					*index_set_out = ui_id;
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

	return result;
}

void
Index_Exprs::set_index(Entity_Id index_set, Math_Expr_FT *index) {
	if(!index)
		fatal_error(Mobius_Error::internal, "It is not allowed to set a nullptr index on an Index_Exprs.");
	
	if(indexes[index_set.id]) delete indexes[index_set.id];
	indexes[index_set.id] = index;
}

Var_Id
Var_Registry::find_conc(Var_Id mass_id) {
	for(auto var_id : all_state_vars()) {
		auto var = (*this)[var_id];
		if(var->type != State_Var::Type::dissolved_conc) continue;
		if(as<State_Var::Type::dissolved_conc>(var)->conc_of == mass_id)
			return var_id;
	}
	return invalid_var;
}


void
prelim_compose(Model_Application *app, std::vector<std::string> &input_names);

Model_Application::Model_Application(Mobius_Model *model) :
	model(model), parameter_structure(this), series_structure(this), result_structure(this), temp_result_structure(this), connection_structure(this),
	additional_series_structure(this), index_counts_structure(this), data_set(nullptr), data(this), llvm_data(nullptr), index_data(model) {
	
	
	// NOTE: This is only because of how we implement Index_Set_Tuple. That could easily be amended if necessary.
	if(model->index_sets.count() > 64)
		fatal_error(Mobius_Error::internal, "There is an implementation restriction so that you can't currently have more than 64 index_sets in the same model.");
	
	time_step_size.unit       = Time_Step_Size::second;
	time_step_size.multiplier = 86400;
	time_step_unit.declared_form.push_back({0, 1, Compound_Unit::day});
	time_step_unit.set_standard_form();
	
	initialize_llvm();
	llvm_data = create_llvm_module();
}

Model_Application::Edit_Form
Model_Application::get_parameter_edit_form(Entity_Id par_id) {
	
	auto par_id_data = map_id(model, data_set, par_id);
	auto par = data_set->parameters[par_id_data];
	if(is_valid(par->from_pos))
		return Edit_Form::disabled;
	if(par->is_on_map_form)
		return Edit_Form::map;
	return Edit_Form::direct; 
}


void
Model_Application::set_up_parameter_structure(std::unordered_map<Entity_Id, std::vector<Entity_Id>, Hash_Fun<Entity_Id>> *par_group_index_sets) {
	if(parameter_structure.has_been_set_up)
		fatal_error(Mobius_Error::internal, "Tried to set up parameter structure twice.");
	if(!all_indexes_are_set())
		fatal_error(Mobius_Error::internal, "Tried to set up parameter structure before all index sets received indexes.");
	
	// TODO: Could probably just make this code work with Index_Set_Tuple altogether?
	
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
			// Hmm, this takes away the ability for the user to change the order around. Do we want that?
			auto group = model->par_groups[group_id];
			for(auto id : group->max_index_sets)
				index_sets.push_back(id);
		}
	
		for(auto par_id : model->get_scope(group_id)->by_type<Reg_Type::parameter>())
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

Var_Location
Model_Application::get_primary_location(Var_Id source, bool &is_conc) {
	Var_Location loc0;
	is_conc = false;
	auto var = vars[source];
	if(var->type == State_Var::Type::declared) {
		loc0 = var->loc1;
	} else if (var->type == State_Var::Type::dissolved_conc) {
		is_conc = true;
		auto var2 = vars[as<State_Var::Type::dissolved_conc>(var)->conc_of];
		loc0 = var2->loc1;
	} else
		fatal_error(Mobius_Error::internal, "Access of unhandled variable type in get_primary_location.");
	return loc0;
}

Var_Id
Model_Application::get_connection_target_variable(Var_Location &loc0, Entity_Id target_component, bool is_conc) {
	// TODO: May have to make this work with quantity connection components eventually, that is a bit more tricky.
	if(model->components[target_component]->decl_type != Decl_Type::compartment)
		fatal_error(Mobius_Error::internal, "For now, graph lookups are only supported over compartments.");
	
	auto loc = loc0;
	loc.components[0] = target_component;
	auto target = vars.id_of(loc);
	if(!is_valid(target)) return target;
	if(is_conc)
		target = vars.find_conc(target);
	return target;
}

Var_Id
Model_Application::get_connection_target_variable(Var_Id source, Entity_Id connection_id, Entity_Id target_component) {
	if(model->connections[connection_id]->type != Connection_Type::directed_graph)
		fatal_error(Mobius_Error::internal, "get_connection_target_variable should only be used for graph connections.");
	// TODO: Could also check that the target is valid for the given source and given the connection components?
	//  Could also check that the var is something that is valid to look up this way
	bool is_conc;
	auto loc0 = get_primary_location(source, is_conc);
	return get_connection_target_variable(loc0, target_component, is_conc);
}

Entity_Id
avoid_index_set_dependency(Model_Application *app, Var_Loc_Restriction restriction) {
	
	// TODO: For secondary restriction r2 also!
	
	Entity_Id avoid = invalid_entity_id;
	if(restriction.r1.type == Restriction::top || restriction.r1.type == Restriction::bottom)
		return app->model->connections[restriction.r1.connection_id]->node_index_set;
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
		
		if(connection->type == Connection_Type::directed_graph) {
			
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
				if(comp.is_edge_indexed)
					index_sets.push_back(connection->edge_index_set);
				
				Multi_Array_Structure<Connection_T> array(std::move(index_sets), std::move(handles));
				structure.push_back(array);
			}
		} else if (connection->type == Connection_Type::grid1d) {
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
			data.index_counts.data[offset] = index_data.get_index_count(indexes, index_set).index;
		});
	}
}

bool
Model_Application::all_indexes_are_set() {
	for(auto index_set : model->index_sets) {
		if(!index_data.are_all_indexes_set(index_set))
			return false;
	}
	return true;
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
	return make_literal((s64)index_data.get_max_count(index_set).index);
}


void
process_par_group_index_sets(Mobius_Model *model, Data_Set *data_set, Entity_Id par_group_data_id, 
	std::unordered_map<Entity_Id, std::vector<Entity_Id>, Hash_Fun<Entity_Id>> &par_group_index_sets) {
	
	auto par_group_data = data_set->par_groups[par_group_data_id];
	
	auto group_id = map_id(data_set, model, par_group_data_id);
	if(!is_valid(group_id)) return;  // NOTE: we do error handling on this on the second pass.
	
	if(par_group_data->index_sets.empty()) { // We have to signal it so that it is not filled with default index sets later
		par_group_index_sets[group_id] = {};
		return;
	}
	
	auto par_group = model->par_groups[group_id];
	if(par_group->max_index_sets.empty()) {
		if(!par_group_data->index_sets.empty()) {
			par_group_data->source_loc.print_error_header();
			fatal_error("The par_group \"", par_group->name, "\" can not be indexed with any index sets.");
		}
	} else {
		auto &index_sets = par_group_index_sets[group_id];
		
		for(int idx = 0; idx < par_group_data->index_sets.size(); ++idx) {
			auto index_set_data_id = par_group_data->index_sets[idx];
			
			auto &name = data_set->index_sets[index_set_data_id]->name;
			
			auto index_set_id = map_id(data_set, model, index_set_data_id);
			
			if(!is_valid(index_set_id)) {
				par_group->source_loc.print_error_header();
				fatal_error("The index set \"", name, "\" does not exist in the model.");
			}
			
			// TODO: Is this entirely correct now? I think we are not allowed to index with a union if also a union member is given??
			bool found = par_group->max_index_sets.has(index_set_id);
			if(!found) {
				par_group_data->source_loc.print_error_header();
				fatal_error("The par_group \"", par_group_data->name, "\" can not be indexed with the index set \"", name, "\" since none of its attached components are distributed over that index set in the model.");
			}
			
			// TODO: Should probably check this in the data_set already.
			auto find = std::find(index_sets.begin(), index_sets.end(), index_set_id);
			if(find != index_sets.end()) {
				par_group_data->source_loc.print_error_header();
				fatal_error("Parameter groups are not allowed to index over duplicate index sets.");
			}
			index_sets.push_back(index_set_id);
		}
	}
}


void
process_parameters(Model_Application *app, Data_Set *data_set, Entity_Id par_group_data_id, std::vector<u8> &warned_module_already) {
	
	if(!app->parameter_structure.has_been_set_up)
		fatal_error(Mobius_Error::internal, "We tried to process parameter data before the parameter structure was set up.");
	
	Mobius_Model *model = app->model;
	
	auto par_group_data = data_set->par_groups[par_group_data_id];
	auto module_data_id = par_group_data->scope_id;
	auto module_id = invalid_entity_id;
	if(is_valid(module_data_id)) {
		module_id = map_id(data_set, model, module_data_id);
		
		if(!is_valid(module_id)) {
			if(!warned_module_already[module_data_id.id]) {
				auto module_data = data_set->modules[module_data_id];
				log_print("In ");
				module_data->source_loc.print_log_header();
				log_print("The model \"", model->model_name, "\" does not contain a module named \"", module_data->name, "\". This data block will be ignored.\n\n");
				warned_module_already[module_data_id.id] = true;
			}
			return;
		}
	}
	
	bool module_is_outdated = false;
	if(is_valid(module_id)) {
		auto mod  = model->modules[module_id];
		auto temp = model->module_templates[mod->template_id];
		auto mod_data = data_set->modules[par_group_data->scope_id];
		module_is_outdated = (mod_data->version < temp->version);
	}
	
	auto group_id = map_id(data_set, model, par_group_data_id);
	
	// TODO: Error messages should reference the name of the module template rather than the module maybe.
	// TODO: We should rethink what is an error and what is a warning here.
	if(!is_valid(group_id)) {
		log_print("In ");
		par_group_data->source_loc.print_log_header();
		if(is_valid(module_id))
			log_print("WARNING: The module \"", model->modules[module_id]->name, "\" does not contain the parameter group \"", par_group_data->name, "\". This data will be discarded if the data set is saved.\n\n");
			//TODO: say what file the module was declared in?
		else
			log_print("WARNING: The model does not contain the parameter group \"", par_group_data->name, "\". This data will be discarded if the data set is saved.\n\n");
		par_group_data->mark_for_deletion = true;
		return;
	}
	
	for(auto par_data_id : par_group_data->scope.by_type<Reg_Type::parameter>()) {
		
		auto par_data = data_set->parameters[par_data_id];
		if(par_data->mark_for_deletion) continue;
		
		auto par_id = map_id(data_set, model, par_data_id);
		
		if(!is_valid(par_id)) {
			if(module_is_outdated) {
				par_data->source_loc.print_log_header();
				log_print("The parameter group \"", par_group_data->name, "\" in the module \"", model->modules[module_id]->name, "\" does not contain a parameter named \"", par_data->name, "\". The version of the module in the model code is newer than the version in the data, so this may be due to a change in the model. If you save over this data file, the parameter will be removed from the data.\n");
				par_data->mark_for_deletion = true;
			} else {
				par_data->source_loc.print_error_header();
				fatal_error("The parameter group \"", par_group_data->name, "\" does not contain a parameter named \"", par_data->name, "\".");
			}
			continue;
		}
		
		auto par = model->parameters[par_id];
		
		if(par->decl_type != par_data->decl_type) {
			par_data->source_loc.print_error_header();
			fatal_error("The parameter \"", par_data->name, "\" should be of type ", name(par->decl_type), ", not of type ", name(par_data->decl_type), ".");
		}
		
		// NOTE: This should be tested for in the data set already, so this is just a safety tripwire.
		s64 expect_count = app->index_data.get_instance_count(app->parameter_structure.get_index_sets(par_id));
		if(expect_count != par_data->get_count()) {
			par_data->source_loc.print_error_header();
			fatal_error("Got ", par_data->get_count(), " values for this parameter, expected ", expect_count, ".");
		}
		
		int idx = 0;
		app->parameter_structure.for_each(par_id, [&idx, &app, &par, &par_data](auto idxs, s64 offset) {
			Parameter_Value value;
			if(par_data->decl_type == Decl_Type::par_enum) {
				s64 idx2 = 0;
				bool found = false;
				for(; idx2 < par->enum_values.size(); ++idx2) {
					if(par->enum_values[idx2] == par_data->values_enum[idx].data()) {
						found = true;
						break;
					}
				}
				if(!found) {
					par_data->source_loc.print_error_header();
					fatal_error("\"", par_data->values_enum[idx], "\" is not a valid value for the enum parameter \"", par_data->name, "\".");
				}
				value.val_integer = idx2;
			} else
				value = par_data->values[idx];
			++idx;
			*app->data.parameters.get_value(offset) = value;
		});
		
	}
}

void
process_series_metadata(Model_Application *app, Data_Set *data_set, Entity_Id series_data_id, Series_Metadata *metadata) {
	
	auto model = app->model;
	
	auto series_data = data_set->series[series_data_id];
	
	for(auto &series : series_data->series) {
		if( (series.has_date_vector && series.dates.empty())   ||
			(!series.has_date_vector && series.time_steps==0) )   // Ignore empty data block.
			return;
		
		// If the data set does not provide a clamping interval for the series data, expand the interval to fit all the provided data.
		if(!data_set->series_interval_was_provided) {
			if(series.start_date < metadata->start_date)
				metadata->start_date = series.start_date;
			
			Date_Time end_date = series.end_date;
			if(!series.has_date_vector)
				end_date = advance(series.start_date, app->time_step_size, series.time_steps-1);
			
			if(end_date > metadata->end_date) metadata->end_date = end_date;
		}
		
		metadata->any_data_at_all = true;

		for(auto &header : series.header_data) {
			
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
			
			for(auto id : ids) {
				if(id.type == Var_Id::Type::state_var || id.type == Var_Id::Type::temp_var) {
					header.source_loc.print_error_header();
					fatal_error("The name \"", header.name, "\" refers to a variable that can't be provided as an input series.");
				}
			}
			
			if(header.indexes.empty()) continue;
			
			if(ids.begin()->type == Var_Id::Type::series) {
				if(metadata->index_sets.find(*ids.begin()) != metadata->index_sets.end()) continue; // NOTE: already set by another series data block.
			} else {
				if(metadata->index_sets_additional.find(*ids.begin()) != metadata->index_sets_additional.end()) continue;
			}
			
			for(auto &index : header.indexes[0].indexes) {
				// NOTE: just check the index sets of the first index tuple. We check for internal consistency between tuples when we process the data later
				// NOTE: the tuple should be structurally valid since we already tested it internally in the data set. We only have to check that it can index the components of the series location.
				auto idx_set = data_set->index_sets[index.index_set];
				Entity_Id index_set = model->top_scope.deserialize(idx_set->name, Reg_Type::index_set);
				if(!is_valid(index_set))
					fatal_error(Mobius_Error::internal, "Invalid index set for series in data set.");
				for(auto id : ids) {
					
					if(id.type == Var_Id::Type::series) {// Only perform the check for model inputs, not additional series.
						
						auto var0 = app->vars[id];
						//Index_Set_Tuple allowed = {};
						auto allowed = as<State_Var::Type::declared>(var0)->allowed_index_sets;
						/*
						if(var0->type == State_Var::Type::declared)
							allowed = as<State_Var::Type::declared>(var0)->allowed_index_sets;
						else if(var0->type == State_Var::Type::dissolved_conc) {
							auto var = as<State_Var::Type::dissolved_conc>(var0);
							auto allowed_diss = as<State_Var::Type::declared>(app->vars[var->conc_of])->allowed_index_sets;
							auto allowed_med  = as<State_Var::Type::declared>(app->vars[var->conc_in])->allowed_index_sets;
							allowed = allowed_diss;
							allowed.insert(allowed_med);
						} else
							fatal_error(Mobius_Error::internal, "Unsupported state var type for input series.");
						*/
						if(!allowed.has(index_set)) {
							header.source_loc.print_error_header();
							fatal_error("Can not set \"", idx_set->name, "\" as an index set dependency for the series \"", header.name, "\" since the relevant components are not distributed over that index set.");
						}
						
						metadata->index_sets[id].push_back(index_set);
					} else
						metadata->index_sets_additional[id].push_back(index_set);
				}
			}
		}
	}
}

void
process_series(Model_Application *app, Data_Set *data_set, Entity_Id series_data_id, Date_Time end_date);


void
add_connection_component(Model_Application *app, Data_Set *data_set, Component_Data *component_data, Entity_Id connection_id, Entity_Id component_id) {
	
	// TODO: In general we should be more nuanced with what source location we print for errors in this procedure.
	
	auto model = app->model;
	auto component = model->components[component_id];
	
	if(component->decl_type != Decl_Type::compartment) {
		component_data->source_loc.print_error_header();
		fatal_error("The name \"", component_data->name, "\" does not refer to a compartment. This connection type only supports compartments, not quantities.");
	}
	
	auto connection = model->connections[connection_id];
	
	auto find = std::find(connection->components.begin(), connection->components.end(), component_id);
	if(find == connection->components.end()) {
		component_data->source_loc.print_error_header();
		error_print("The connection \"", connection->name,"\" is not allowed for the component \"", component->name, "\". See declaration of the connection:\n");
		connection->source_loc.print_error();
		mobius_error_exit();
	}
	
	std::vector<Entity_Id> index_sets;
	for(auto set_id : component_data->index_sets) {
		auto idx_set_info = data_set->index_sets[set_id];
		auto index_set = model->top_scope.deserialize(idx_set_info->name, Reg_Type::index_set);
		if(!is_valid(index_set)) {
			idx_set_info->source_loc.print_error_header();
			fatal_error("The index set \"", idx_set_info->name, " does not exist in the model.");  // Actually, this has probably been checked somewhere else already.
		}
		if(std::find(component->index_sets.begin(), component->index_sets.end(), index_set) == component->index_sets.end()) {
			component_data->source_loc.print_error_header();
			fatal_error("The index sets indexing a component in a connection relation must also index that component in the model. The index set \"", idx_set_info->name, "\" does not index the component \"", component->name, "\" in the model");
		}
		index_sets.push_back(index_set);
	}
	
	auto &components = app->connection_components[connection_id].components;
	auto find2 = std::find_if(components.begin(), components.end(), [component_id](const Sub_Indexed_Component &comp) -> bool { return comp.id == component_id; });
	if(find2 != components.end()) {
		if(index_sets != find2->index_sets) { // This should no longer be necessary when we make component declarations local to the connection data in the data set.
			component_data->source_loc.print_error_header();
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
set_up_simple_connection_components(Model_Application *app, Entity_Id conn_id) {
	
	// TODO: May not need this now that we store the node index set in the connection registration.
	
	// This is for grid1d where there is only one component.
	
	auto conn = app->model->connections[conn_id];
	
	if(conn->components.size() != 1 || !is_valid(conn->components[0]) || !is_valid(conn->node_index_set))
		fatal_error(Mobius_Error::internal, "Somehow model declaration did not set up component data correctly for a grid1d.");
	
	auto component_id = conn->components[0];
	
	auto comp_type = app->model->components[component_id]->decl_type;
	if(comp_type != Decl_Type::compartment || comp_type == Decl_Type::property) {
		conn->source_loc.print_error_header();
		fatal_error("This connection can't have nodes of this component type.");
	}
	
	Sub_Indexed_Component comp;
	comp.id = component_id;
	comp.index_sets.push_back(conn->node_index_set);
	app->connection_components[conn_id].components.push_back(comp);
}

void
pre_process_connection_data(Model_Application *app, Data_Set *data_set, Entity_Id conn_data_id) {
	
	auto model = app->model;
	
	auto connection_data = data_set->connections[conn_data_id];
	
	Entity_Id module_id = invalid_entity_id;
	if(is_valid(connection_data->scope_id))
		module_id = map_id(data_set, model, connection_data->scope_id);
	
	auto scope = model->get_scope(module_id);
	
	auto conn_id = map_id(data_set, model, conn_data_id);
	
	if(!is_valid(conn_id)) {
		connection_data->source_loc.print_error_header();
		fatal_error("The connection structure \"", connection_data->name, "\" has not been declared in the model.");
	}

	auto connection = model->connections[conn_id];
	
	if(connection->type != Connection_Type::directed_graph) {
		connection_data->source_loc.print_error_header();
		error_print("Connection data should only be provided for connections of type 'directed_graph'. In the model it is declared as another type. See declaration here:");
		connection->source_loc.print_error();
		mobius_error_exit();
	}
	
	bool error = false;
	if(is_valid(connection_data->edge_index_set)) {
		
		auto edge_id = map_id(data_set, model, connection_data->edge_index_set);
		if(!is_valid(edge_id) || edge_id != connection->edge_index_set) {
			connection_data->source_loc.print_error_header();
			fatal_error("The edge index set of this connection don't match between the model and the data set.");
		}
	} else if (is_valid(connection->edge_index_set)) {
		connection_data->source_loc.print_error_header();
		fatal_error("This connection was given an edge index set in the model, but not in the data set."); 
	}
	
	for(auto &comp_data_id : connection_data->scope.by_type<Reg_Type::component>()) {
		
		auto comp_data = data_set->components[comp_data_id];
		// Can't do this since they are scoped differently in the model and the data_set
		//auto comp_id = map_id(data_set, model, comp_data_id);
		auto comp_id = model->top_scope.deserialize(comp_data->name, Reg_Type::component);
		if(!is_valid(comp_id)) {
			comp_data->source_loc.print_error_header();
			fatal_error("The component \"", comp_data->name, "\" has not been declared in the model.");
		}
		
		add_connection_component(app, data_set, comp_data, conn_id, comp_id);
	}
	
	// NOTE: We allow empty info for this connection type, in which case the data type is 'none'.
	if(connection_data->type != Connection_Data::Type::directed_graph && connection_data->type != Connection_Data::Type::none) {
		connection_data->source_loc.print_error_header();
		fatal_error("Connection structures of type directed_tree or directed_graph can only be set up using graph data.");
	}
	
	for(auto &arr : connection_data->arrows) {
		
		Connection_Arrow arrow;
		
		//arrow.source_id = map_id(data_set, model, arr.first.id);
		arrow.source_id = model->top_scope.deserialize(data_set->components[arr.first.id]->name, Reg_Type::component);
		if(is_valid(arr.second.id))
			arrow.target_id = model->top_scope.deserialize(data_set->components[arr.second.id]->name, Reg_Type::component);
			//arrow.target_id = map_id(data_set, model, arr.second.id);
		
		// Note: can happen if we are running with a subset of the larger model the dataset is set up for, and the subset doesn't have these components.
		if( !is_valid(arrow.source_id) || (!is_valid(arrow.target_id) && is_valid(arr.second.id)) ) continue;
		
		// Store useful information that allows us to prune away un-needed operations later.
		auto source = app->find_connection_component(conn_id, arrow.source_id);
		if(is_valid(arrow.target_id))
			source->can_be_located_source = true;
		
		// TODO: Maybe assert the two vectors are the same size (would mean bug in Data_Set code if they are not).
		
		// TODO: Make a function to translate from Indexes_D to Indexes instead (also to be used in
		// process_input_data and maybe other places.
		int idx = 0;
		for(auto index_set : source->index_sets) {
			arrow.source_indexes.add_index(Index_T { index_set, (s16)arr.first.indexes.indexes[idx].index });
			++idx;
		}
		
		source->possible_targets.insert(arrow.target_id);
		
		if(is_valid(arrow.target_id)) {
			auto target = app->find_connection_component(conn_id, arrow.target_id);
			target->possible_sources.insert(arrow.source_id);
			source->max_target_indexes = std::max((int)target->index_sets.size(), source->max_target_indexes);
		
			// TODO: Maybe assert the two vectors are the same size.
			
			// TODO: See same comment as above.
			int idx = 0;
			for(auto index_set : target->index_sets) {
				arrow.target_indexes.add_index(Index_T { index_set, (s16)arr.second.indexes.indexes[idx].index });
				++idx;
			}
		}
		
		app->connection_components[conn_id].arrows.push_back(std::move(arrow));
	}
	
	// Set up the index set of the edge index (if applicable)
	auto &comps = app->connection_components[conn_id];
	
	
	for(auto &comp : comps.components) {
		if(comp.index_sets.size() == 1 && is_valid(connection->edge_index_set))  // TODO: Also make it work for 0 (non-indexed nodes) eventually
			comp.is_edge_indexed = app->index_data.can_be_sub_indexed_to(comp.index_sets[0], connection->edge_index_set);
	}
	
	Storage_Structure<Entity_Id> components_structure(app);
	make_connection_component_indexing_structure(app, &components_structure, conn_id);
	std::vector<int> n_edges(components_structure.total_count);
	
	for(auto &arrow : comps.arrows) {
		s64 offset   = components_structure.get_offset(arrow.source_id, arrow.source_indexes);
		s16 edge_idx = n_edges[offset]++;
		
		auto find_source = app->find_connection_component(conn_id, arrow.source_id);
		
		if(find_source->is_edge_indexed)
			arrow.edge_index = Index_T { connection->edge_index_set, edge_idx };
	}
	
	for(auto &comp : comps.components) {
		
		components_structure.for_each(comp.id, [&](Indexes &indexes, s64 offset) {
			if(n_edges[offset] > 0)
				comp.total_as_source++;
			comp.max_outgoing_per_node = std::max(comp.max_outgoing_per_node, n_edges[offset]);
		});

		if(!comp.is_edge_indexed && comp.max_outgoing_per_node > 1) {
			connection->source_loc.print_error_header(Mobius_Error::model_building);
			fatal_error("A component of type \"", model->components[comp.id]->name, "\" can have more than one outgoing edge in this connection, but its edges can't be indexed by the edge index set (or an edge index set was not given).");
		}
		// NOTE: We still have to allow it to be edge indexed even if max outgoing <= 1 since that is dependent on the data, not on the model.
	}
	
	match_regex(app, conn_id, connection_data->source_loc);
}

void
process_connection_data(Model_Application *app, Entity_Id conn_id) {
	
	auto model = app->model;
	
	auto connection = model->connections[conn_id];
		
	if(connection->type == Connection_Type::grid1d)
		return;  // Nothing else to do for these.
		
	if(connection->type != Connection_Type::directed_graph)
		fatal_error(Mobius_Error::internal, "Unsupported connection structure type in process_connection_data().");

	auto &comps = app->connection_components[conn_id];

	for(auto &arr : comps.arrows) {
		
		Sub_Indexed_Component *component = app->find_connection_component(conn_id, arr.source_id);
		
		auto &index_sets = component->index_sets;
		Indexes indexes = arr.source_indexes;
		if(component->is_edge_indexed)
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
Model_Application::build_from_data_set(Data_Set *data_set) {
		
	
	if(is_compiled)
		fatal_error(Mobius_Error::api_usage, "Tried to build model application after it was compiled.");
	if(this->data_set)
		fatal_error(Mobius_Error::api_usage, "Model application was provided with more than one data set.");
	this->data_set = data_set;
	
	
	std::vector<std::string> input_names;
	for(auto series_id : data_set->series) {
		auto series_data = data_set->series[series_id];
		for(auto &series_set : series_data->series) {
			for(auto &header : series_set.header_data)
				input_names.push_back(header.name);
		}
	}
	
	// Generate at least one index for index sets that were not provided in the data set.
	for(auto index_set_id : model->index_sets) {
		auto index_set = model->index_sets[index_set_id];
		auto data_id = map_id(model, data_set, index_set_id);
		if(!is_valid(data_id)) {
			std::string sub_indexed_to = is_valid(index_set->sub_indexed_to) ? model->serialize(index_set->sub_indexed_to) : "";
			std::vector<std::string> union_of;
			for(auto ui_id : index_set->union_of)
				union_of.push_back(model->serialize(ui_id));
			
			// TODO: Also do something if it is an edge index set!
			data_set->generate_index_data(index_set->name, sub_indexed_to, union_of);
		}
	}
	
	for(auto index_set_data_id : data_set->index_sets)
		data_set->index_data.transfer_data(this->index_data, index_set_data_id);
	
	connection_components.initialize(model);
	
	// TODO: The only reason we still do this seems to be that it makes looking up aggregation weights easier when registering connection aggregates, but there is no reason we should need it.
	for(auto conn_id : model->connections) {
		auto type = model->connections[conn_id]->type;
		if(type == Connection_Type::grid1d)
			set_up_simple_connection_components(this, conn_id);
	}
	
	for(auto &conn_id : data_set->connections)
		pre_process_connection_data(this, data_set, conn_id);
	
	for(auto conn_id : model->connections) {
		if(model->connections[conn_id]->type != Connection_Type::directed_graph)
			continue;
		
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

	if(!index_counts_structure.has_been_set_up)
		set_up_index_count_structure();
	
	if(!connection_structure.has_been_set_up) // Hmm, can this ever have been set up already?
		set_up_connection_structure();
	
	for(auto &conn_id : model->connections)
		process_connection_data(this, conn_id);
	
	
	std::unordered_map<Entity_Id, std::vector<Entity_Id>, Hash_Fun<Entity_Id>> par_group_index_sets;
	for(auto par_group_data_id : data_set->par_groups)
		process_par_group_index_sets(model, data_set, par_group_data_id, par_group_index_sets);
	
	set_up_parameter_structure(&par_group_index_sets);
	
	std::vector<u8> warned_module_already(data_set->modules.count());
	for(auto par_group_data_id : data_set->par_groups)
		process_parameters(this, data_set, par_group_data_id, warned_module_already);
	
	if(data_set->series.count() > 0) {
		Series_Metadata metadata;
		metadata.start_date.seconds_since_epoch = std::numeric_limits<s64>::max();
		metadata.end_date.seconds_since_epoch   = std::numeric_limits<s64>::min();
		
		if(data_set->series_interval_was_provided) {
			metadata.start_date = data_set->series_begin;
			metadata.end_date = data_set->series_end;
		}
		
		for(auto series_id : data_set->series)
			process_series_metadata(this, data_set, series_id, &metadata);
		
		set_up_series_structure(Var_Id::Type::series,            &metadata);
		set_up_series_structure(Var_Id::Type::additional_series, &metadata);
		
		s64 time_steps = 0;
		if(metadata.any_data_at_all)
			time_steps = steps_between(metadata.start_date, metadata.end_date, time_step_size) + 1; // NOTE: if start_date == end_date we still want there to be 1 data point (dates are inclusive)
		else if(vars.count(Var_Id::Type::series) != 0) {
			//TODO: use the model run start and end date.
			// Or better yet, we could just bake in series default values as literals in the code. in this case.
		}
	
		allocate_series_data(time_steps, metadata.start_date);
		
		for(auto series_id : data_set->series)
			process_series(this, data_set, series_id, metadata.end_date);
		
	} else {
		set_up_series_structure(Var_Id::Type::series,            nullptr);
		set_up_series_structure(Var_Id::Type::additional_series, nullptr);
	}

}

void
Model_Application::save_to_data_set() {
	
	//TODO: We should probably just generate a data set in the case of a missing one.
	if(!data_set)
		fatal_error(Mobius_Error::api_usage, "Tried to save model application to data set, but no data set was attached to the model application.");
	
	// NOTE: This only saves parameter data. Other structural data should be edited in the data_set, not in the model_application
	//   (not implemented yet).
	
	for(auto module_id : model->modules) {
		auto module = model->modules[module_id];
		auto module_data_id = map_id(model, data_set, module_id);
		if(!is_valid(module_data_id))
			module_data_id = data_set->modules.create_internal(&data_set->top_scope, "", module->name, module->decl_type);
		auto module_data = data_set->modules[module_data_id];
		module_data->scope.parent_id = module_data_id; // TODO: Easy to forget and has created many bugs. Would be nice to somehow have this automatic.
		module_data->version = model->module_templates[module->template_id]->version;
	}
	
	for(auto par_group_id : model->par_groups) {
		auto par_group = model->par_groups[par_group_id]; 
		
		auto par_group_data_id = map_id(model, data_set, par_group_id);
		if(!is_valid(par_group_data_id)) {
			
			Decl_Scope *scope = &data_set->top_scope;
			if(is_valid(par_group->scope_id)) {
				// Note: this one can only be a module, so it must exist since we created nonexisting modules earlier.
				auto scope_id_data = map_id(model, data_set, par_group->scope_id);
				scope = data_set->get_scope(scope_id_data);
			}
			par_group_data_id = data_set->par_groups.create_internal(scope, "", par_group->name, Decl_Type::par_group);
			data_set->par_groups[par_group_data_id]->scope.parent_id = par_group_data_id;
		}
		auto par_group_data = data_set->par_groups[par_group_data_id];
		par_group_data->index_sets.clear();
		
		bool index_sets_resolved = false;
		par_group_data->error = false;
		for(auto par_id : par_group->scope.by_type<Reg_Type::parameter>()) {
			if(!index_sets_resolved) {
				auto &index_sets = parameter_structure.get_index_sets(par_id);
				
				for(auto index_set_id : index_sets) {
					auto index_set_id_data = map_id(model, data_set, index_set_id);
					if(!is_valid(index_set_id_data)) {
						par_group_data->error = true;
						log_print("WARNING: Tried to set an index set dependency \"", model->index_sets[index_set_id]->name, "\" for the parameter group \"", par_group->name, "\" in the data set, but the index set was not in the data set. This will cause this parameter group to not be saved.\n");
						break;
					}
					par_group_data->index_sets.push_back(index_set_id_data);
				}
				if(par_group_data->error) break;
				index_sets_resolved = true;
			}
			
			auto par = model->parameters[par_id];
			
			auto par_id_data = map_id(model, data_set, par_id);
			if(!is_valid(par_id_data)) {
				//log_print("Failed to deserialize ", model->serialize(par_id), ".\n");
				par_id_data = data_set->parameters.create_internal(&par_group_data->scope, "", par->name, par->decl_type);
			}
			
			auto par_data = data_set->parameters[par_id_data];
			
			par_data->values.clear();
			par_data->values_enum.clear();
			par_data->mark_for_deletion = false;
			
			parameter_structure.for_each(par_id, [&,this](Indexes &idxs, s64 offset) {
				if(par_data->decl_type == Decl_Type::par_enum) {
					s64 ival = data.parameters.get_value(offset)->val_integer;
					par_data->values_enum.push_back(par->enum_values[ival]);
				} else
					par_data->values.push_back(*data.parameters.get_value(offset));
			});
		}
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
		if(sz > 0) {
			data = (Val_T *) malloc(sz);
			//auto sz2 = round_up(data_alignment, sz);
			//data = (Val_T *) _aligned_malloc(sz2, data_alignment);  // should be replaced with std::aligned_alloc(data_alignment, sz2) when that is available.
			if(!data)
				fatal_error(Mobius_Error::internal, "Failed to allocated data (", sz, " bytes).");
		} else
			data = nullptr;
		is_owning = true;
	}
	size_t sz = alloc_size();
	memset(data, 0, sz);
}

template<typename Val_T, typename Handle_T> void 
Data_Storage<Val_T, Handle_T>::free_data() {
	//if(data && is_owning) _aligned_free(data);
	if(data && is_owning) free(data); 
	data = nullptr;
	time_steps = 0;
	is_owning = false;
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
	auto id = app->model->top_scope["start_date"]->id;
	auto offset = parameters.structure->get_offset_base(id);                    // NOTE: it should not be possible to index this over an index set any way.
	return parameters.get_value(offset)->val_datetime;
}

Date_Time
Model_Data::get_end_date_parameter() {
	auto id = app->model->top_scope["end_date"]->id;
	auto offset = parameters.structure->get_offset_base(id);                    // NOTE: it should not be possible to index this over an index set any way.
	return parameters.get_value(offset)->val_datetime;
}

Model_Data::Model_Data(Model_Application *app) :
	app(app), parameters(&app->parameter_structure), series(&app->series_structure),
	results(&app->result_structure, 1), temp_results(&app->temp_result_structure), connections(&app->connection_structure),
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

Var_Id
Model_Application::find_base_flux(Var_Id dissolved_flux_id) {
	Var_Id result_id = dissolved_flux_id;
	State_Var *var_flux = vars[dissolved_flux_id];
	// NOTE: It could be a chain of dissolvedes.
	do {
		//result_id = as<State_Var::Type::dissolved_flux>(var_flux)->flux_of_medium;
		// Have to do it this way because intermediate ones could have been disabled, and that
		// triggers an error in 'as'.
		result_id = static_cast<State_Var_Sub<State_Var::Type::dissolved_flux> *>(var_flux)->flux_of_medium;
		var_flux = vars[result_id];
	} while(var_flux->type != State_Var::Type::declared);
	return result_id;
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
			auto var_medium = vars[var2->conc_in];
			ss << "dissolved_conc@";
			serialize_loc(model, ss, var_mass->loc1);
			ss << '@';
			serialize_loc(model, ss, var_medium->loc1);
		} else if(var->type == State_Var::Type::dissolved_flux) {
			auto var2 = as<State_Var::Type::dissolved_flux>(var);
			//auto var_conc = as<State_Var::Type::dissolved_conc>(vars[var2->conc])
			State_Var *var_flux = vars[find_base_flux(id)];
			ss << "dissolved_flux@";
			ss << model->serialize(as<State_Var::Type::declared>(var_flux)->decl_id) << '@';
			serialize_loc(model, ss, var2->loc1);
		} else if(var->type == State_Var::Type::regular_aggregate) {
			auto var2 = as<State_Var::Type::regular_aggregate>(var);
			ss << "regular_aggregate@";
			ss << model->serialize(var2->agg_to_compartment) << '@';
			ss << serialize(var2->agg_of);
		} else if(var->type == State_Var::Type::parameter_aggregate) {
			auto var2 = as<State_Var::Type::parameter_aggregate>(var);
			ss << "parameter_aggregate@";
			ss << model->serialize(var2->agg_to_compartment) << '@';
			ss << model->serialize(var2->agg_of);
		} else if(var->type == State_Var::Type::in_flux_aggregate) {
			auto var2 = as<State_Var::Type::in_flux_aggregate>(var);
			auto var_to = vars[var2->in_flux_to];
			ss << var2->is_out ? "out_flux@" : "in_flux@";
			serialize_loc(model, ss, var_to->loc1);
		} else if(var->type == State_Var::Type::connection_aggregate) {
			auto var2 = as<State_Var::Type::connection_aggregate>(var);
			auto agg_for = vars[var2->agg_for];
			ss << var2->is_out ? "out_flux@" : "in_flux@";
			ss << model->serialize(var2->connection) << '@';
			serialize_loc(model, ss, agg_for->loc1);
		} else if(var->type == State_Var::Type::external_computation) {
			ss << "external_computation@" << var->name;
		} else if(var->type == State_Var::Type::step_resolution) {
			auto var2 = as<State_Var::Type::step_resolution>(var);
			ss << "step_resolution@" << model->solvers[var2->solver_id]->name;
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
	
	Var_Id result = invalid_var;
	if(name.empty()) return result;
	if(name.data()[0] == ':') {
		auto find = serial_to_id.find(name);
		if(find == serial_to_id.end()) return invalid_var;
		result = find->second;
	} else {
		const auto &ids = vars.find_by_name(name);
		if(!ids.empty())
			result = *ids.begin();
	}
	if(is_valid(result)) {
		if(!vars[result]->is_valid())
			result = invalid_var;
	}
	return result;
}
