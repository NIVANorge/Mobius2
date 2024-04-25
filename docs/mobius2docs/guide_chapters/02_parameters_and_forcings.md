---
layout: default
title: 02 Parameters and forcings
parent: Guide
grand_parent: Model language
nav_order: 1
comment: "While we use python markup for code snippets, they are not actually python, it just creates convenient coloring for this format."
---

# 02. Parameters and forcings (input series)

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
			# The third argument is the default value. This is not always the
			# value that is used when the model is run, instead the value from 
			# the data_set is used
			br : par_real("Base growth rate", [k g, day-1], 1)
			
			# This parameter has a dimensionless unit:
			td : par_real("Growth rate temperature dependence", [], 2)
		}
		
		# This variable does not have a math expression, and since this 
		# is the only module, it can't be declared with one anywhere else either.
		# It must therefore be a forcing.
		var(soil.temp, [deg_c], "Soil temperature")
		
		# We can refer to other values in the math expression for this variable:
		var(soil.growth_rate, [k g, day-1], "Plant growth rate") {
			br * td^((soil.temp - 20[deg_c])/10[deg_c])
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
	
	# This instructs the model to load series data from the 
	# given .csv format. You can also use .xlsx files.
	series("data.csv")
}
```

The data.csv file:

```
"Soil temperature" [spline_interpolate]
2000-01-01	-5
2000-02-01	-4
2000-03-01	-1
2000-04-01	2
2000-05-01	7
2000-06-01	11
2000-07-01	14
2000-08-01	12
2000-09-01	9
2000-10-01	4
2000-11-01	1
2000-12-01	-3
2001-01-01	-5
```

Here Mobius2 is instructed to interpolate for missing values, but you could just provide a value per time step if you have that.

Currently we are running with a daily time step. That is configurable, but we will worry about that later.

Try to run the model in MobiView2, plot the "Plant growth rate" series, and see what happens if you edit a parameter value and run the model again.

Try also to experiment with changing the equations or adding more complexity to the model.

Rembember that if you add a new parameter, or parameter group, you don't need to manually add these to the `data_set`. Mobius2 will auto-generate parameter values for these for you.

[Full code for chapter 02](https://github.com/NIVANorge/Mobius2/tree/main/guide/02).