
#ifndef MOBIUS_GRAPH_VISUALISATION_H
#define MOBIUS_GRAPH_VISUALISATION_H

#include <graphviz/cgraph.h>

struct Model_Application;

void build_model_graph(Model_Application *app, Agraph_t *g, bool show_properties, bool show_flux_labels, bool show_short_names);


#endif // MOBIUS_GRAPH_VISUALISATION_h