---
layout: default
title: 01 First model
parent: Guide
grand_parent: Model language
nav_order: 0
comment: "While we use python markup for code snippets, they are not actually python, it just creates convenient coloring for this format."
---

# 01. The first model

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

[Full code for chapter 01](https://github.com/NIVANorge/Mobius2/tree/main/guide/01).