---
layout: default
title: Guide
parent: Model language
nav_order: 1
comment: "While we use python markup for code snippets, they are not actually python, it just creates convenient coloring for this format."
---

# The Guide

This document will guide you through building a model from the first steps to more advanced concepts.

It will explain the most important features of Mobius2, but will not cover all details. For that, see the [declaration format documentation](declaration_format.html) and related pages.

## The basic model

A model consists of a file with a `.txt` suffix that contains a single `model` declaration that can load one or more modules.

In the beginning you can inline a `module` declaration into the body of your model. Later you should learn to put modules in separate files so that they are reusable.

You must also declare at least one [state variable](central_concepts.html). For this you need at least one `compartment` and one `property` to form its location. Components are usually declared in the model scope, but properties can also be declared in the module scope.

We suggest you use [MobiView2](../mobiviewdocs/mobiview.html) to load and run your model.


```python
model("The first model") {
	
	# Declare the components
	soil : compartment("Soil")
	temp : property("Temperature")
	
	# This module declaration is inlined into the body of the model declaration.
	# Because of this identifiers in the model scope (soil, temp) are visible inside the module scope.
	module("A module", version(0, 0, 0)) {
		
		# Declare your first state variable. It has unit degrees Celsius.
		var(soil.temp, [deg_c], "Soil temperature") {
			20[deg_c]
		}
	}
}
```

To load this model you also need a `data_set`. This is put in a separate file with a `.dat` suffix. For now, it will only contain the start and end dates of the model run.

```python
data_set {
	par_group("System") {
		par_datetime("Start date")
		[ 2000-01-01 ]

		par_datetime("End date")
		[ 2000-12-31 ]
	}
}
```

Of course, this model is not that interesting yet as the state variable is constant.

## Parameters and forcings (input series)

A parameter is a value that is held constant through each model run, and can be used to e.g. tune the model to fit observed data, or to run scenarios. In Mobius2, parameters are always put inside parameter groups.

A forcing (input series) is another value that is not computed by the model, but unlike a parameter, varies over time. In Mobius2 any `property` state variable without a math expression is assumed to be an input series.

```python
model("The first model") {
	
	soil : compartment("Soil")
	temp : property("Temperature")
	growth_rate : property("Growth rate")
	
	module("A module", version(0, 0, 1)) {
		
		par_group("Growing parameters") {
			
			# A par_real is a parameter taking values in the real number line.
			# The third argument is the default value. This is not always the value that is used when the model is run, 
			# instead the value from the data_set is used
			br : par_real("Base growth rate", [k g, day-1], 1)
			
			# This parameter has a dimensionless unit:
			td : par_real("Growth rate temperature dependence", [], 2)
		}
		
		# This variable does not have a math expression, and since this is the only module, it can't be declared with one anywhere else either. It must therefore be a forcing.
		var(soil.temp, [deg_c], "Soil temperature")
		
		# We can refer to other values in the math expression for this variable:
		var(soil.growth_rate, [k g, day-1], "Plant growth rate") {
			br * td^((soil.temp - 20)/10)
		}
	}
}
```

Parameters and series are provided in the data_set. If a parameter is not provided, it is given its default value when it is loaded. If you then save the data_set again (from MobiView2), the parameter will be in it. Note how the structure of the data_set mimics the structure of the model.

```python
data_set {
	par_group("System") {
		par_datetime("Start date")
		[ 2000-01-01 ]

		par_datetime("End date")
		[ 2000-12-31 ]
	}
	
	module("A module", version(0, 0, 1)) {
		par_group("Growing parameters") {
			par_real("Base growth rate")
			[ 4 ]
			
			par_real("Growth rate temperature dependence")
			[ 2.1 ]
		}
	}
	
	# This instructs the model to load series data from the given .csv format. You can also use .xlsx files.
	series("data.csv")
}
```

The data.csv file:

```
"Soil temperature" [spline_interpolate]
2004-01-01	-5
2004-02-01	-4
2004-02-01	-1
2004-02-01	2
2004-02-01	7
2004-02-01	11
2004-02-01	14
2004-02-01	12
2004-02-01	9
2004-02-01	4
2004-02-01	1
2004-02-01	-3
```

Here Mobius2 is instructed to interpolate for missing values, but you could just provide a value per time step if you have that.

Currently we are running with a daily time step. That is configurable, but we will worry about that later.

Try to run the model in MobiView2, plot the "Plant growth rate" series, and see what happens if you edit a parameter value and run the model again.


## Quantities and solvers

## Dissolved and transported quantities

The rest of this document is yet to be written.

