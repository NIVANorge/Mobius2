
#include <unordered_map>
#include <vector>
#include <sstream>
#include "../model_application.h"
#include "graph_visualisation.h"

struct
Node_Data {
	Agraph_t *subgraph;
	Agnode_t *node;
	int clusterid;
	std::vector<std::pair<Entity_Id, Var_Id>> ids;
};

Agnode_t *
make_empty(Agraph_t *g) {
	static int nodeid = 0;
	static char buf[32];
	
	sprintf(buf, "nonode_%d", nodeid++);
	Agnode_t *n = agnode(g, buf, 1);
	
	agsafeset(n, "label", "", "");
	agsafeset(n, "shape", "none", "");
	agsafeset(n, "width", "0", "");
	agsafeset(n, "height", "0", "");
	
	// TODO: This should not be in the node rank hierarchy when it tries to order things. Can that
	// be specified in the dot language?
	
	return n;
}

void
add_component_node(Agraph_t *g, std::unordered_map<Var_Location, Node_Data, Var_Location_Hash> &nodes, Model_Application *app, Var_Id var_id, Var_Location &loc) {
	static int clusterid = 0;
	static int nodeid = 0;
	static char buf[32];
	
	auto find = nodes.find(loc);
	if(find != nodes.end()) return;
	
	auto id = loc.last();
	auto comp = app->model->components[id];
	
	Var_Location above = loc;
	above.n_components--;
	Agraph_t *g_above;
	if(loc.n_components == 1)
		g_above = g;
	else {
		auto find = nodes.find(above);
		if(find == nodes.end()) {
			Var_Id above_id = app->vars.id_of(above);
			add_component_node(g, nodes, app, above_id, above);
		}
		g_above = nodes[above].subgraph;
	}
	
	if(comp->decl_type == Decl_Type::quantity || comp->decl_type == Decl_Type::compartment) {
		sprintf(buf, "cluster_%d", clusterid);
		
		Agraph_t *sub = agsubg(g_above, buf, 1);
		
		sprintf(buf, "node_%d", nodeid++);
	
		Agnode_t *n = make_empty(sub);
		nodes[loc] = Node_Data {sub, n, clusterid++, {}};
		
		agsafeset(sub, "style", "filled", "");
		if(loc.n_components == 1)
			agsafeset(sub, "fillcolor", "lightgrey", "");
		else
			agsafeset(sub, "fillcolor", "white", "");
		
		nodes[loc].ids.push_back({id, var_id});
	} else if(loc.n_components > 1)
		nodes[above].ids.push_back({id, var_id});
}

void
put_name(std::stringstream &ss, Mobius_Model *model, Entity_Id id, bool show_short_names) {
	Entity_Registration_Base *reg;
	if(id.reg_type == Reg_Type::component)
		reg = model->components[id];
	else
		reg = model->connections[id];
	
	if(show_short_names) {
		auto scope = model->get_scope(reg->scope_id);
		ss << (*scope)[id];
	} else {
		ss << reg->name;
	}
}

Agnode_t *
add_connection_node(Agraph_t *g, std::vector<Agnode_t *> &connection_nodes, Entity_Id conn_id, Model_Application *app, bool show_short_names) {
	if(connection_nodes[conn_id.id]) return connection_nodes[conn_id.id];
	
	static int nodeid = 0;
	static char buf[32];
	
	sprintf(buf, "connode_%d", nodeid++);
	Agnode_t *n = agnode(g, buf, 1);

	std::stringstream ss;
	ss << "<table cellborder=\"0\" border=\"0\">";
	ss << "<tr><td align=\"left\"><b>";
	put_name(ss, app->model, conn_id, show_short_names);
	ss << "</b></td></tr>";
	auto &components = app->connection_components[conn_id.id];
	for(auto comp : components) {
		ss << "<tr><td align=\"left\"><font point-size=\"8\">";
		put_name(ss, app->model, comp.id, show_short_names);
		ss << "</font></td></tr>";
	}
	ss << "</table>";
	
	auto str = ss.str();
	agsafeset(n, "label", agstrdup_html(g, str.data()), "");
	
	connection_nodes[conn_id.id] = n;
	return n;
}

void
add_flux_edge(Agraph_t *g, std::unordered_map<Var_Location, Node_Data, Var_Location_Hash> &nodes, 
	std::vector<Agnode_t *> &connection_nodes, Model_Application *app, State_Var *var, bool show_flux_labels, bool show_short_names) {
		
	static int edgeid = 0;
	static char buf[32];
	
	sprintf(buf, "edge_%d", edgeid++);
	std::string *name = nullptr;
	
	Var_Location *loc1 = nullptr;
	Var_Location *loc2 = nullptr;
	Entity_Id conn_id = invalid_entity_id;
	
	bool is_agg = false;
	if(var->type == State_Var::Type::declared) {
		if(var->has_flag(State_Var::has_aggregate)) return;
		if(is_located(var->loc1)) loc1 = &var->loc1;
		if(is_located(var->loc2)) loc2 = &var->loc2;
		conn_id = var->loc2.connection_id;
		name = &var->name;
	} else if (var->type == State_Var::Type::regular_aggregate) {
		auto var2 = as<State_Var::Type::regular_aggregate>(var);
		auto agg_var = app->vars[var2->agg_of];
		if(agg_var->type != State_Var::Type::declared) return;
		loc1 = &agg_var->loc1;
		loc2 = &var->loc2;
		name = &agg_var->name;
		is_agg = true;
	} else
		return;
	
	Agnode_t *n1, *n2;
	if(loc1)
		n1 = nodes[*loc1].node;
	else
		n1 = make_empty(g);
	
	if(loc2)
		n2 = nodes[*loc2].node;
	else if(is_valid(conn_id))
		n2 = add_connection_node(g, connection_nodes, conn_id, app, show_short_names);
	else
		n2 = make_empty(g);
	
	Agedge_t *e = agedge(g, n1, n2, buf, 1);
	if(show_flux_labels) {
		agsafeset(e, "label", (char *)name->data(), "");
		agsafeset(e, "fontsize", "12", "");
	}
	
	if(loc1) {
		sprintf(buf, "cluster_%d", nodes[*loc1].clusterid);
		agsafeset(e, "ltail", buf, "");
	}
	if(loc2) {
		sprintf(buf, "cluster_%d", nodes[*loc2].clusterid);
		agsafeset(e, "lhead", buf, "");
	}
	if(is_agg)
		//agsafeset(e, "arrowhead", "open", "");
		//agsafeset(e, "color", "black:invis:black", "");
		agsafeset(e, "penwidth", "2", "");
}


void
build_flux_graph(Model_Application *app, Agraph_t *g, bool show_properties, bool show_flux_labels, bool show_short_names) {
	
	agsafeset(g, "compound", "true", "");
	
	std::unordered_map<Var_Location, Node_Data, Var_Location_Hash> nodes;
	std::vector<Agnode_t *> connection_nodes(app->model->connections.count(), nullptr);
	
	for(auto var_id : app->vars.all_state_vars()) {
		auto var = app->vars[var_id];
		if(var->type != State_Var::Type::declared) continue;
		auto var2 = as<State_Var::Type::declared>(var);
		if(var2->decl_type != Decl_Type::quantity && var2->decl_type != Decl_Type::property) continue;
		add_component_node(g, nodes, app, var_id, var->loc1);
	}
	
	for(auto var_id : app->vars.all_series()) {
		auto var = app->vars[var_id];
		add_component_node(g, nodes, app, var_id, var->loc1);
	}
	
	for(auto &pair : nodes) {
		
		std::stringstream ss;
		auto &data = pair.second;
	
		if(show_properties) {
			ss << "<table cellborder=\"0\" border=\"0\">";
			int row = 0;
			for(auto id : data.ids) {
				bool is_input = (id.second.type == Var_Id::Type::series);
				ss << "<tr><td align=\"left\">";
				if(row == 0) ss << "<b>";
				else ss << "<font point-size=\"8\">";
				if(is_input) ss << "<i>";
				put_name(ss, app->model, id.first, show_short_names);
				if(is_input) ss << "</i>";
				if(row != 0) ss << "</font>";
				else ss << "</b>";
				ss << "</td></tr>";
				++row;
			}
			ss << "</table>";
			
		} else
			put_name(ss, app->model, data.ids[0].first, show_short_names); 
		
		auto str = ss.str();
		agsafeset(data.subgraph, "label", agstrdup_html(g, str.data()), "");
	}
	
	//TODO: show_short_names should probably also affect fluxes, but it is
	//non-ideal since fluxes seldom have short names attached to them.
	
	for(auto var_id : app->vars.all_fluxes()) {
		auto var = app->vars[var_id];
		add_flux_edge(g, nodes, connection_nodes, app, var, show_flux_labels, show_short_names);
	}
}



struct Node_Data2 {
	Agraph_t *subgraph = nullptr;
	Agraph_t *instance_subg = nullptr;
	
	// Ugh, will be annoying with indexes here...
	std::vector<std::unique_ptr<Node_Data2>> subnodes;
	
	Node_Data2(int size) : subnodes(size) {};
	//~Node_Data2() {}
};

Agraph_t *
add_index_sets(Agraph_t *g, Agraph_t *sub, Model_Application *app, std::vector<std::unique_ptr<Node_Data2>> &nodes, std::vector<Entity_Id> &index_sets, int level = 0) {
	static char buf[32];
	static int clusterid = 0;
	static int edgeid = 0;
	
	if(index_sets.empty()) return g;
	
	auto model = app->model;
	
	auto index_set = index_sets[level];
	auto &node = nodes[index_set.id];
	if(!node) {
		node.reset(new Node_Data2(model->index_sets.count()));
		
		std::stringstream ss;
		int cluster1 = clusterid++;
		int cluster2 = clusterid++;
		auto set = model->index_sets[index_set];
		
		ss << "<table cellborder='0' border='0' style='rounded'>";
		ss << "<tr><td><b>" << set->name << "</b></td></tr>";
		if(is_valid(set->sub_indexed_to)) {
			ss << "<tr><td>(variable index count)</td></tr>";
		} else {
			auto count = app->get_max_index_count(index_set).index;
			if(count > 5)
				ss << "<tr><td>" << count << " instances</td></tr>";
			else {
				for(int idx = 0; idx < count; ++idx) {
					Index_T index = {index_set, idx};
					ss << "<tr><td>" << app->get_index_name(index) << "</td></tr>";
				}
			}
		}
		ss << "</table>";
		auto str1 = ss.str();
		ss.str("");
		
		sprintf(buf, "cluster_%d", cluster1);
		node->subgraph = agsubg(sub, buf, 1);
		//agsafeset(node->subgraph, "shape", "plaintext", "");
		agsafeset(node->subgraph, "style", "rounded", "");
		agsafeset(node->subgraph, "label", agstrdup_html(g, str1.data()), "");
		agsafeset(node->subgraph, "bgcolor", "white", "");
		agsafeset(node->subgraph, "penwidth", "1", "");
		
		
		sprintf(buf, "cluster_%d", cluster2);
		node->instance_subg = agsubg(sub, buf, 1);
		agsafeset(node->instance_subg, "bgcolor", "lightgrey", "");
		agsafeset(node->instance_subg, "penwidth", "2", "");
		
		ss << set->name << " instance";
		auto str = ss.str();
		agsafeset(node->instance_subg, "label", str.data(), "");
		
		Agnode_t *a = make_empty(node->subgraph);
		Agnode_t *b = make_empty(node->instance_subg);
		sprintf(buf, "instanceedg_%d", edgeid++);
		Agedge_t *e = agedge(sub, a, b, buf, 1);
		
		agsafeset(e, "dir", "both", "");
		agsafeset(e, "arrowhead", "dot", "");
		agsafeset(e, "arrowtail", "dot", "");
		//agsafeset(e, "label", "instance", "");
		sprintf(buf, "cluster_%d", cluster1);
		agsafeset(e, "ltail", buf, "");
		sprintf(buf, "cluster_%d", cluster2);
		agsafeset(e, "lhead", buf, "");
	}
	
	if(level == index_sets.size()-1) return node->instance_subg;
	
	return add_index_sets(g, node->instance_subg, app, node->subnodes, index_sets, level+1);
}


void
build_distrib_connection_graph(Model_Application *app, Agraph_t *g, bool show_short_names) {
	
	// TODO: Implement show short names
	// TODO: Add show_connections (maybe)
	
	static char buf[32];
	static int nodeid = 0;
	static int edgeid = 0;
	
	auto model = app->model;
	
	agsafeset(g, "compound", "true", "");
	
	std::vector<std::unique_ptr<Node_Data2>> base_nodes(model->index_sets.count());
	
	std::vector<int> used_compartment(model->components.count(), 0);
	for(auto var_id : app->vars.all_state_vars()) {
		auto var = app->vars[var_id];
		if(var->type != State_Var::Type::declared) continue;
		if(is_located(var->loc1))
			used_compartment[var->loc1.first().id] = 1;
	}
	for(auto var_id : app->vars.all_series()) {
		auto var = app->vars[var_id];
		if(is_located(var->loc1))
			used_compartment[var->loc1.first().id] = 1;
	}
	
	std::vector<Agnode_t *> component_instances(model->components.count(), nullptr);
	for(auto comp_id : model->components) {
		
		if(!used_compartment[comp_id.id]) continue;
		auto comp = model->components[comp_id];
		if(comp->decl_type != Decl_Type::compartment) continue; // NOTE: For now only distribution of compartments. Could also do quantities later?
		
		auto subg = add_index_sets(g, g, app, base_nodes, comp->index_sets);
		sprintf(buf, "comp_%d", nodeid++);
		Agnode_t *n = agnode(subg, buf, 1);
		agsafeset(n, "label", (char *)comp->name.data(), "");
		agsafeset(n, "shape", "rectangle", "");
		agsafeset(n, "style", "filled", "");
		agsafeset(n, "fillcolor", "white", "");
		agsafeset(n, "penwidth", "1", "");
		
		component_instances[comp_id.id] = n;
	}
	
	for(auto conn_id : model->connections) {
		auto &comps = app->connection_components[conn_id.id];
		
		auto &name = model->connections[conn_id]->name;
		
		for(auto &comp : comps) {
			// Note: this draws only the connections that are in the dataset, not all that are theoretically possible. Change that?
			Agnode_t *n2 = component_instances[comp.id.id];
			for(auto source_id : comp.possible_sources) {
				Agnode_t *n1 = component_instances[source_id.id];
				if(!n1 || !n2) continue;
				
				sprintf(buf, "conn_%d", edgeid++);
				Agedge_t *e = agedge(g, n1, n2, buf, 1);
				agsafeset(e, "label", name.data(), "");
			}
		}
	}
}