---
layout: default
title: Set up a new project
parent: Data files
nav_order: 0
comment: "While we use python markup for code snippets, they are not actually python, it just creates convenient coloring for this format."
---

# Set up a new project from an existing example

This document will guide you through how to take an existing data `.dat` file for a model and set it up for a new location. Many example data files are available in the [Mobius2 repository](https://github.com/NIVANorge/Mobius2/tree/main/models/data).

## Gathering data for your location

We will not teach you how to gather data since the available sources and formats will depend on what country you are setting up the model in. Instead, this document will only help you to put the data on a format that is recognizable by Mobius2.

If however you happen to be a NIVA employee modelling a location in Norway, we suggest you use the [catchment processing workflows](https://nivanorge.github.io/catchment_processing_workflows/).

## The data set file

Each *model application* has a single `.dat` main data file. This is a plain-text file using the [common declaration format](../mobius2docs/declaration_format.html). Don't worry too much about the details of the declaration format. To edit a data file you only need to edit some bits of it, which we will explain how to do.

This file contains a single `data_set` declaration, with all the other declarations inside it.

It can be convenient to put a single docstring inside your data set declaration to document where you got the data from and how you processed it. The docstring is saved if you edit the file in MobiView2, while all other comments are lost. E.g.,

```python
data_set {
"""
Meteorological data was obtained from ..., and so on.
Parameters are calibrated to minimize ...
Data set created by ... for the ... project, 2024-05-01
"""

	# Everything else goes here
}
```

## Index sets and distributions

The first thing you will do is to edit the [index sets](../mobius2docs/central_concepts.html#index-sets-and-distributions) to match your use case. This can for instance mean to divide your catchment into river sections and sub-catchments, selected lakes and land use types (depending entirely on what model you are running).

Index sets are usually given data on [list form](../mobius2docs/declaration_format.html#data-blocks). For instance,

```python
sc : index_set("Subcatchment") [ "Kråkstadelva" "Kure" "Vaaler" ]
lk : index_set("Lake index") [ "Våg" "Vanem" "Store" ]
```

You can also supply the index set with a single number of indexes, in which case the indexes are anonymously named `0 1 2 ..` .

```python
layer : index_set("Layers") [ 35 ]
```

The index set declaration only says what the indexes in the set are. Information about *connections* between them or *parameter* data attached to them is separately declared (see below).

There are also the concepts of *sub-indexed* and *union* index sets as well as position maps, but this is more rare and will be documented later.

## Time series data

Time series data is provided in separate [`.csv`](csv_format.html) or [`.xlsx`](xlsx_format.html) files, but they are loaded from the main dataset file using a `series` declaration, e.g.

```python
series("my_data.xlsx")
series("more_data.csv")
```

The path of the series files must be given absolutely or relatively to the path of the main data file. You can load as many series files as you want. See also the [data files page](datafiles.html) for some more details about related concepts.

## Connections

If your model has a connection of `directed_graph` type, it will be specified in the data set. This type of connection is often used to model e.g. connections between river sections and lakes, or downhill flow paths. We start with an example since it is easier to explain when there is something to refer to.

```python
sc : index_set("Subcatchment") [ "Kråkstadelva" "Kure" "Vaaler" ]
lk : index_set("Lake index") [ "Våg" "Vanem" "Store" ]

connection("Downstream") {
	r : compartment("River", sc)
	l : compartment("Lake", lk)
	
	directed_graph [
		r["Kråkstadelva"] -> r["Kure"]
		l["Våg"] -> r["Kure"] -> r["Vaaler"] -> l["Vanem"] -> l["Store"]
	]
}
```

The first part of the graph data consists of declaring the node types and what they index over, in this case `r` ("River") indexing over `sc` ("Subcatchment") and `l` ("Lake") indexing over `lk` ("Lake index"). You should not change this part since it must match objects in the model declaration.

What you edit is the `directed_graph` block. You must make the node indexes match whatever you put in the index sets. Whenever you put an arrow `->` between two node instances it creates a directed edge (arrow, path) of the current connection between these (and in the "Downstream" example you can assume that discharge will be directed along that edge).

## Parameter groups

Every parameter is scoped inside a parameter group, which is some times scoped inside a module. This matches exactly how they are declared in the model and module files.

A parameter group can be distributed over 0 or more index sets. All parameters in the group are indexed over the same index sets as the group.

The index set distribution is limited by the distribution of the attached component(s) in the model declaration. For instance, if a parameter group is declared to be attached to `soil` in the model, the index sets for this parameter group will be limited to the ones `soil` is distributed over. If you delete a parameter group declaration from the data set, ![Load](../img/toolbar/Open.png) load the file in MobiView2, and then ![Save](../img/toolbar/Save.png) save it again, the parameter group will be given its maximal index set distribution.

You can reduce the amount of index sets in the data file if you want to make the model setup more semi-distributed. For instance you could decide that some parameters that could index over both "Subcatchment" and "Landscape units" should index over the latter only. Example:

```python
par_group("Soil hydrology", sc, lu) {
	#...
}

# Alternatively, change it to be "semi-distributed" where the soil hydrology 
# only depends on the land use type, not the subcatchment.
par_group("Soil hydrology", lu) {
	#...
}
```

## Parameter data

One way to edit the parameter data is to delete every parameter declaration and instead edit the values in [MobiView2](../mobiviewdocs/parameters.html) (the parameters are given default values if they are not originally in the data file). 

If you want to edit them by hand, you have to provide one value for each tuple of indexes in the index sets of the distribution, in the same order they are given in the `index_set` declaration. The rightmost index set in the distribution is considered to be the innermost one. Example:

```python
sc : index_set("Subcatchment") ["Coull" "Aboyne"]
lu : index_set("Landscape units")
	["Arable" "Semi-natural" "Improved grasslands"]

module("A hydrology module", version(0, 0, 1)) {
	
	# One index set
	par_group("Soil hydrology", lu) {
		par_real("Soil water time constant")
		[ 2 7 3 ] # One value per landscape unit
	}

	# Alternatively, two index sets
	par_group("Soil hydrology", sc, lu) {
		par_real("Soil water time constant") [
			2    7   3    # One row per subcatchment, one column per landscape unit
			2.5  6   4.1
		]
	}
}
```

Remember that the format is whitespace-agnostic, so the example above is just a convenient way to format it for visual readability.

If there are 3 or more dimensions, you just repeat the inner set of value blocks for each index of any "outer" index sets.

There is also the concept of a map form for parameter data, but this is more rare and will be documented later.

## Quick select

A `quick_select` is used to make it more convenient to [select result series in MobiView2](../mobiviewdocs/plots.html) that you tend to look at more often than others. A `quick_select` declaration is given a user-defined name argument and a data block on the [map format](../mobius2docs/declaration_format.html#maps) (see example below).

```python
quick_select("River vars") [!
	"Q" : [ "Reach flow flux" "Obs Q" ]
	"DIN" : [ "concentration(River water DIN, Reach water volume)" "Obs DIN" ]
	"DON" : [ "concentration(River DON, Reach water volume)" "Obs DON" ]
	"TN" : [ "River TN" "Obs TN" ]
	"TOC" : [ "River TOC concentration" "Obs TOC" ]
	"SS" : [ "concentration(Suspended sediments, Reach water volume)" "Obs SS" ]
]
```

The key to each map entry is a user-defined name, which is the name that appears for you to select in MobiView2. The value is a list of strings, each string is the name or serial name of a time series (state variable or additional time series).

You can have multiple `quick_select` declarations in each data set.