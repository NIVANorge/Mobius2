---
layout: default
title: Advanced concepts
parent: Model language
nav_order: 3
---

# Advanced concepts
{: .no_toc }

This document is incomplete. The guide is prioritized for now.

## Table of contents
{: .no_toc .text-delta }

1. TOC
{:toc}

## Index set distributions

In a Mobius2 model, any compartment can be distributed over one or more index sets. This happens during declaration of the compartment in the model file.

```python
sc : compartment("Subcatchment")
ly : compartment("Landscape units")

air  : compartment("Atmosphere")
soil : compartment("Soil", sc, lu)
gw   : compartment("Groundwater, sc)
```

In the above example, there is only one global `air` compartment, while there is one `gw` box per subcatchment, and one `soil` box per pair of subcatchment and landscape unit (conceptually, each subcatchment is further-subdivided into landscape types).

When a compartment is distributed, any state variable that has that compartment in its [context location](math_format.html#the-context-location) can also be distributed over the given index sets. That means that the model can compute a separate value for that variable for each (tuple of) index(es) in those index set(s).

The framework will try to determine if the value will actually be different, and if it can determine that the value will be the same across one index set, that index set will not be included in the distribution of the state variable.

A state variable can pick up index set dependencies in a few different ways. The main one is from parameters. A parameter group can be distributed like one or more compartments. 

```python
par_group("Soil parameters", soil) {
	fc : par_real("Field capacity", [m m], 100, 0, 500)
	tc : par_real("Time constant", [day], 1, 1, 50)
}
```

In the above example, the parameter group "Soil parameters" is distributed like `soil`. This means that it can be distributed over any of the index sets that `soil` is distributed over. What exact index sets are used is [determined in the data set](../datafiledocs/new_project.html#parameter-groups). For instance in this example, the user could distribute the "Soil parameters" over nothing (having the same conditions in all locations), or over landscape units (so that soils in the same landscape type are the same across subcatchments) or over both landscape type and subcatchment.

Every parameter has the same distribution as its group. A state variable picks up the distributions of the parameters it accesses in its math expression (if it has one). This is because, if a parameter can be different for each index of an index set, any formula depending on that parameter must also be different across that index set. Input forcing series can similarly be distributed over index sets, and state variables that access an input forcing picks up these index sets.

Every state variable also picks up index set dependencies from all other state variables they depend on. This can include
* Access of the value of the other state variable in code.
* Quantities depend on any fluxes affecting them.
All index set dependencies are propagated down the dependency graph between state variables.

A state variable can only access parameters, input series and state variables from compartments that are distributed over a subset of the index sets of its own compartment. This rule is about the distribution of the compartment declarations, not the potentially smaller actual distributions of the state variables. This is because otherwise it would be ambiguous what index we are looking at in the excess index sets.

For instance, in the above examples, any state variable in `soil` can access any state variable in `gw` and `air`, but not the other way around.

There are some ways to get around this. For instance, you can access a variable from a higher distribution using [`aggregate`](math_format.html#special-directives). The aggregate sums the variable over the missing index sets, applying an [`aggregation_weight`](declaration_types.html#aggregation_weight) if one exists.

In the example above, if `gw.water` accesses the value of `soil.temp`, it will get a weighted sum of the latter over the landscape unit index set.

The aggregation is also automatically applied if a flux goes from a compartment with a higher number of index sets to a lower, for instance from `soil.water` to `gw.water`.

Another way to access specific indexes of an index set is to use location restrictions (below).

### Sub-indexed index sets

Some times you may want make one index set have a different content depending on the index of another index set. For instance, a lagoon model may have a different amount of layers per basin.

```python
basin : index_set("Basin")
layer : index_set("Layer") @sub(basin)
```

In this example, we say that the `layer` index set is *sub-indexed* to the `basin` index set, and that `basin` is the *parent* index set of `layer`. In the data set, you can now have a separate amount of layers per basin, declared using the [map format](declaration_format.html#maps), e.g.

```python
basin : index_set("Basin") [ "Inner fjord" "Outer fjord" ]

layer : index_set("Layer") [!
	"Inner fjord" : [ 20 ]
	"Outer fjord" : [ 35 ]
]
```

There are a few rules that must be followed to make this work. Any model entity that is distributed over a sub-indexed index set must also be indexed over the parent (otherwise you could not determine how many copies there should be). It is not allowed to have chains of sub-indexing, so a parent index set can't again be sub-indexed (the implementation complexity of this would be very high, and we haven't yet found a use case).

### Union index sets

Some times it can be useful to be able to distribute an entity over a union of other index sets (not a tuple of them). For instance, you may want a separate state for the atmosphere (temperature, precipitation, ..) over lakes and subcatchments, but these are separate index sets. You can then make an index set that is a union of these.

```python
sc : index_set("Subcatchment")
lk : index_set("Lake")
wb : index_set("Water bodies") @union(sc, lk)
```

A union can have two or more members. If you now distribute `air` over `wb`, you can e.g. provide a separate input series of `air.precip` for each subcatchment and lake. Even though `soil` does not distribute over `wb` directly, state variables in `soil` can still access `air.precip` because `soil` is distributed over `sc`, and `sc` is a part of the `wb` union.

For technical reasons,
* You can't create unions of other index sets that are themselves unions.
* A sub-indexed index set can't be a part of a union (but a parent index set can be).

### Distributing quantities

To not make the descriptions above too complicated up-front (and because it is less often used) we have omitted the fact that you can also distribute `quantity` declarations directly. This can be used to create several instances of the same type of substance, for instance different buoyancy classes of suspended particles, or different pollutants that use the same formulas (but are parametrized differently).

```python
sc : index_set("Subcatchment")
pt : index_set("Pollutant type")

river : compartment("Soil", sc)
pollutant : quantity("Pollutant", pt)
```

In this example, "river.water.pollutant" can index over the tuple of `sc` and `pt` (one amount of pollutant per subcatchment and pollutant type). The amendments to distribution rules are pretty straightforward. For instance,
* The allowed index set dependencies for a state variable includes the index sets coming from both its compartment and its quantities.
* A parameter group can index like a quantity or a combination of compartments and quantities.
* You can use `aggregate`, e.g. so that `river.water.tot_pollutant` can be computed using `aggregate(river.water.pollutant)`.

## Connections

Connections are used to link up different instances of the same compartment or multiple compartments, mainly for transport using fluxes.

Without using connections, if you have a `soil` compartment and a `gw` compartment, you can have a flux from `soil.water` to `gw.water` implying that this flux happens within each subcatchment, but not across different subcatchments.

On the other hand, if you want a flux (such as downstream discharge) between the river sections of two subcatchments you must use a `directed_graph` connection. There is also `grid1d` connections that position all the indexes of a given index set next to one another in a linear order.

If you have a connection, e.g. `downstream`, you can direct a flux to it, e.g. using

```python
flux(river.water, downstream, [m 3, s-1], "Reach flow flux") { ... }
```

A flux can transport a quantity along a connection if the compartment of the state variable is on the connection. For graphs, it is also required that there is a valid target (see below). The particular details on how to use connections depends on the type of connection. For implementation reasons, fluxes can only move quantities along connections if these quantities are on [ODE solvers](declaration_types.html#solver) (this may be changed for some specific instances at a later point if it is needed).

### Grid1D connections

In Mobius2, 1D grids are conceptualized so that the first index is called the `top` and the last the `bottom`, while `below` references the index that is 1 higher than the one you are currently looking at. This is just convenience terminology, you can use `grid1d` connections to model grids that are oriented any way you like.

A 1D grid always structures exactly one compartment over one index set. For instance, you can have

```python
li : index_set("Layer index")
layer : compartment("Layer", li)

vert : connection("Layer vertical") @grid1d(layer, li)
```

Note that the compartment could be indexed over multiple index sets, but the grid just structures one of these. So you could for instance have `layer` be distributed over `basin` and `li`, but the "Layer vertical" connection only structures the direction along the `li` index set.

A flux going along the grid1d connection transports a quantity from the current index to the one `below` (one higher). The flux will be set to 0 at the `bottom` (last) index since it doesn't have a target. You can move in the opposite direction using `@bidirectional` (see below).

#### Location restrictions for grids

If a compartment is structured over a grid, you can access specific parts of it using restrictions. For instance, any state variable or parameter that indexes like that compartment can be accessed using the `top`, `bottom`, `above` and `below` restrictions. For instance

```python
layer.water.temp[vert.top]       # The temperature of the top layer of water
conc(layer.water.oc)[vert.below] # The concentration of dissolved organic carbon one layer below
```

An access using `top` or `bottom` loses its dependency on the grid index set for the given connection (since index is inferred from the access). Inside a math expression, to check if you are at the `top` or `bottom` layer you can use `is_at`, e.g. `is_at[vert.top]`, which evaluates to a boolean.

A flux can have `top` and `bottom` restrictions in its source or target. For instance,

```python
flux(layer.water[vert.top], out, [m 3, s-1], "Lake runoff") { ... }
flux(out, layer.water[vert.top], [m 3, s-1], "Precipitation to lake surface") { ... }
```

In that case, the flux is only applied to the given specific index, and its *context location* counts as not being distributed over the grid index set. There is also the concept of a `specific` target for a flux.

```python
flux(river.water, layer.water[vert.specific], [m 3, s-1], "River discharge to lake") {
	# some value ...
} @specific {
	5
}
```

In the above example, the river discharges to layer 5 of the lake. Specific accesses are currently a bit limited and may be redesigned in the future. The `@specific` code block is always cast to an integer, and is clamped so that it evaluates to a valid index.

#### Flux aggregations for grids

The sum of fluxes that come to a quantity along a grid are summed up in the model, and can be accessed in code. For instance,

```python
in_flux(vert, layer.water)
```

sums up the amount of `water` that comes in along the `vert` connection. The sum includes fluxes coming in to location restrictions like `layer.water[vert.top]`

There is no `out_flux` variable that is accessible for grids in the current implementation. There is one implementation exception: A flux going out from a location restriction like `layer.water[vert.top]` are subtracted from the `in_flux`. This is for implementation simplicity only, but can be a bit confusing, and may be changed at a later point.

It should also be noted that if a flux is negative, it will still be added (hence causing a reduction) to the `in_flux` of its target even if it is declared as `@bidirectional`.

### Directed graph connections

In Mobius2, each node of a directed graph is an (indexed) instance of a compartment. Fluxes can be directed along edges of the graph. The nodes don't all have to be the same compartment, for instance nodes along the same graph can be both from `river` and `lake`. We will first cover the case where every node can have at most one outgoing edge.

In the data file, the graph is set up by declaring the edges between the nodes

```python
sc : index_set("Subcatchment") [ "Kråkstadelva" "Kure" ]
lk : index_set("Lake index") [ "Våg" "Storefjord" "Vanemfjord" ]

connection("Downstream") {
	r : compartment("River", sc)
	l : compartment("Lake", lk)
	
	directed_graph [
		r[ "Kråkstadelva" ] -> r[ "Kure" ] -> l[ "Storefjord" ] -> l[ "Vanemfjord" ] -> out
		l[ "Våg" ] -> r[ "Kure " ]
	]
}
```

Every arrow creates a directed edge between the two given nodes. The identifier `out` can also be used as a node, signifying that the fluxes directed along an edge to it go out of the modeled system, just like when `out` is explicitly the target of a flux.

It is not necessary always to index nodes by all their possible indexes. For instance, you may want to replicate the same "Downhill" flow graph between land types within each subcatchment as in the example below.

```python
sc : index_set("Subcatchment") [ "Upper" "Lower" ]
lu : index_set("Landscape unit") [ "Hilltop" "Hillside" "Riparian zone" ]

connection("Downhill") {
	s : compartment("Soil", lu)
	r : compartment("River")
	
	directed_graph [
		s[ "Hilltop" ] -> s[ "Hillside" ] -> s[ "Riparian zone" ] -> r[]
	]
}

connection("Downstream") {
	r : compartment("River", sc)
	
	directed_graph [
		r[ "Upper" ] -> r[ "Lower" ] -> out
	]
}
```

In the above example, even though soil and river are distributed over subcatchment in the model, they are not explicitly distributed so in the "Downhill" connection data, so this structure is repeated per subcatchment. It is also possible to have nodes with two or more indexes per node if needed. E.g. you could explicitly index the soil with both subcatchment and landscape unit if you want a different "Downhill" structure per subcatchment.

In the model, the connection is declared for instance as follows:

```python
downstream : connection("Downstream") @directed_graph {
	(river|lake)+ out
}
```

The body of the declaration is a so-called regular expression, but this was an experimental feature where most of it is currently disabled. What is important is that you list the compartments that can be nodes of the connection like `(a|b|c)` (`a`, `b`, and `c` being the compartments), possibly followed by an `out`.

To send a flux along the connection, simply write the identifier of the connection as the target of the flux (the source must be a location at a compartment that can be a node of the connection).

```python
flux(river.water, downstream, [m 3, s-1], "Reach flow flux") {
	# ...
}
```

If a connection is not allowed to have circular paths, it can be useful to put a `@no_cycles` note on the connection declaration. This makes the framework check and give an error if there are cycles in the graph data in the data file. It also makes it possible to allow some dependencies between state variables related to the connection that would otherwise not be possible. For instance a flux going along the connection is allowed to (directly or indirectly) depend on the `in_flux` (see below) of its own variable along the connection, which can be useful in some instances. If a connection is `@no_cycles`, then it is required (for implementation reasons) that if there is an edge between two nodes of the same compartment, an index of the first compartment can not appear after an index of the second compartment in the declaration of the index set.

It is also possible to have multiple outgoing edges per node. If you want this, you need to set up an index set to index the outgoing edges. This index set must be sub-indexed to the index set of the nodes. For instance,

```python
sp : index_set("Soil patch index")
se : index_set("Soil flow edge") @sub(sp)

soil : compartment("Soil", sp)

# NOTE: The edge index set is passed as an argument to the directed_graph:
downhill : connection("Downhill") @directed_graph(se) {
	sp+ out
}
```

(If you have multiple node types and each of them can have multiple outgoing edges, the edge index set must be sub-indexed to a union of the index sets of the different node types).

Now if you put a flux along the connection, it will also be indexed by the edge index set, so it is evaluated once per edge (and the sum over the edges is subtracted from the time derivative of the source state variable).

If you want a parameter (or any state variable) that is also indexed over the edge index set, you can create a compartment that is indexed by it, for instance

```python
flow_path : compartment("Soil flow path", sp, se)

# In a module :

par_group("Flow pathing", flow_path) {
	flow_frac : par_real("Path flow fraction", [], 0, 0, 1)
}

flux(soil.water, downhill, [m m, day-1], "Downhill flow") {
	flow * flow_frac # Assume 'soil.water.flow' is some other state variable
}
```

In the above example, the "Downhill flow" flux is partitioned according to the `flow_frac` parameter along each edge of `downhill`.

The data for the edge index set should not be given in the data set, it is instead auto-generated based on the connection data. For instance the data set may look like

```python

sp : index_set("Soil patch index") [ "A" "B" "C" "D" ]
se : index_set("Soil flow edge") @sub(sp)

connection("Downhill) {
	s : compartment("Soil", sp)
	
	directed_graph(se) [
		s[ "A" ] -> s[ "B" ] -> s[ "D" ]
		s[ "A" ] -> s[ "C" ]
	]
}

par_group("Flow pathing", sp, se) {
	
	par_real("Path flow fraction") [
		0.7 0.3
		1
	]
	
} 
```

Note that here you have two values of the flow fraction for edges starting in "A", one for "B", and none for "C" and "D", corresponding to the number of outgoing edges in the graph. The edges are ordered in the order of declaration in the graph ("A"->"B" is the first one, and "A"->"C" the second one starting in "A").

#### Location restrictions for graphs

The only location restriction available for graphs is `below`, and it can be used to access the value of a state variable or parameter that is at the far end of an outgoing edge starting in the context location of the code it is being used in. For instance:

```python
flux(soil.water, downhill, [m m, day-1], "Downhill flow") {
	flow * flow_frac    if water[below] < cap[below],
	0                   otherwise
}
```

The above example stops the the water flow from the current box if the below box is over capacity. This particular implementation is for illustration purposes only. In practice you want a smoother cutoff for the ODE solver to not have too much trouble with it.

You can use a `below` access even though you have multiple compartments in the connection, but it requires that the state variable has the same unit in all the nodes that can be reached from the current one as long as it exists there. If it doesn't exist in one of the nodes it is assumed to have a value of 0 in that node in this context.

#### Flux aggregations for graphs

The sum of fluxes that come to a quantity along a graph are summed up, and can be accessed in code. For instance,

```python
in_flux(downhill, soil.water)
```

sums up the amount of `water` that comes in along the `downhill` connection to the `soil.water` state variable. If (and only if) you have multiple outgoing edges for a graph (if it is given an edge index set), you can also access an `out_flux`, which sums up all outgoing fluxes along all edges from the current node.

It should also be noted that if a flux is negative, it will still be added (hence causing a reduction) to the `in_flux` of its target even if it is declared as `@bidirectional`. Similarly, it will be subtracted (causing an increase) from the `out_flux` of its source (if it exists).

### Aggregation weight along connections

Aggregation weights and unit conversions are also applied to connection fluxes. This also means that you can have an aggregation weight from a compartment to itself, for instance

```python

# In a module
var(soil.water, [m m], "Soil water")

flux(soil.water, downhill, [m m, day-1], "Downhill flow") { ... }

# In the model:

aggregation_weight(soil, soil) { area / area[below] }
```

In the above example, since the soil water is given per unit area, the inflow must be re-scaled according to the relative areas of two boxes. If you want the aggregation weight to only apply to fluxes along a certain connection, but not any other fluxes, you can pass the connection as a third argument to the aggregation weight declaration.

### Double location restrictions for flux targets

It is possible to have a flux that has a target with a restriction from two connections. This feature can be useful if you discharge along a river network that is represented as a directed graph and at the same time some components can be lakes that are also layered.

We are not entirely happy with the syntax for it as it is not entirely consistent with how the syntax for connection fluxes are in general, so this may change. It is also limited to the case where the second connection is of grid type. Example:

```python
flux(river.water, lake.water[downstream.below, vert.specific], [m 3, s-1], "River flow to lake") {
	flow
} @specific {
	river_discharge_layer
}
```

The flux is aggregated to the flux aggregations for the first connection only.

## Discrete fluxes

If a flux has a source and/or a target quantity that is not solved as an ODE equation, it is handled a little differently.

If the source is not ODE, then the value of the flux is subtracted from the state variable after the flux is computed. This means that the order of evaluation of different discrete fluxes from the same source matters. Similarly, if the target is not ODE, then the flux is added to the target, and this could happen before or after other fluxes are subtracted from the same quantity.

To force the order of evaluation of such fluxes, one can declare a `discrete_order`, which is a list of the identifiers of the fluxes. We refer to the example in our [snow module](https://github.com/NIVANorge/Mobius2/blob/main/models/modules/hbv_snow.txt).

You can not have a discrete flux in a context where it would create a transport flux of a dissolved quantity. This is because it would create a lot of difficulty when it comes to determine order of evaluation and concentrations for the transported quantities. It is possible to make this work, but it has not so far been deemed worth it since it causes a lot of added complexity in the framework for not much gain.

A model with discrete fluxes is not guaranteed to be time step size invariant. For instance, 

```python
flux(soil.water, out, [m m, day-1], "Soil water out") {
	c*water
}
```

In the above example, if `soil.water` is solved as a discrete equation (and no other fluxes affect it), the amount of water after one day step is

$$
w_0(1 - c)
$$

where $$w_0$$ is the initial amount of water. If the step size is changed to half a day, then the amount of water after one day (two steps) is

$$
w_0(1 - \frac{c}{2})^2 = w_0(1 - c + \frac{c^2}{4})
$$

On the other hand, if it is solved as an ODE, then the amount of water after one day is

$$
w_0 e^{-c}
$$

regardless of the step size used (at least up to solver accuracy).

## Order of evaluation and circular dependencies

The framework will figure out what order the model should evaluate each bit of math code so that e.g. one state variable `x` is computed after another `y` if `x` depends on the current-time value of `y`.

If there are circular dependencies (e.g. `x -> y -> x`), the framework will not be able to do this, and you will get an error that explains what the cycle is.

If you work with ODE equations this is usually much easier. This is because the values of the ODE equations are advanced "in parallel" using a solver algorithm. So if your equation depends on the value of an ODE equation, you usually don't need to worry about it. Most errors with circular dependencies also happen when you forgot to put a quantity state variable on a solver to make it an ODE.

{% include lib/mathjax.html %}