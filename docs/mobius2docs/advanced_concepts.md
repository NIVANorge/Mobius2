---
layout: default
title: Advanced concepts
parent: Model language
nav_order: 3
---

# Advanced concepts

This document is incomplete. The guide is prioritized for now.

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

An access using `top` or `bottom` loses its dependency on the grid index set for the given connection (since index is inferred from the access).

A flux can have `top` and `bottom` restrictions in its source or target. For instance,

```python
flux(layer.water[vert.top], out, [m 3, s-1], "Lake runoff") { ... }
flux(out, layer.water[vert.top], [m 3, s-1], "Precipitation to lake surface") { ... }
```

In that case, the flux is only applied to the given specific index, and its *context location* counts as not being distributed over the grid index set.

** incomplete (is at, specific) **

#### Flux aggregations for grids

### Directed graph connections

#### Location restrictions for graphs

#### Flux aggregations for graphs

#### Aggregation weight along connections

### `@bidirectional` and `@mixing`

## Order of evaluation

## Discrete fluxes

