
#ifndef MOBIUS_GRAPH_VISUALISATION_H
#define MOBIUS_GRAPH_VISUALISATION_H

#include <graphviz/cgraph.h>

struct Model_Application;

void build_flux_graph(Model_Application *app, Agraph_t *g, bool show_properties, bool show_flux_labels, bool show_short_names);

void build_distrib_connection_graph(Model_Application *app, Agraph_t *g, bool show_indexes, bool show_short_names);

// Something like this.. May need to have another intermediary editable object that is not Data_Set since it is maybe too rigid.
//void build_individual_connection_graph(Model_Application *app, Agraph_t *g, Data_Set *data_set, int conn_id);

// Could also have this for state var (model instruction) dependencies.

#endif // MOBIUS_GRAPH_VISUALISATION_H