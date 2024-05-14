---
layout: default
title: Declaration types
parent: Common declaration format
grand_parent: Model language
nav_order: 0
comment: "While we use python markup for code snippets, they are not actually python, it just creates convenient coloring for this format."
---

# The declaration types
{: .no_toc }

## Table of contents
{: .no_toc .text-delta }

1. TOC
{:toc}

---

## Introduction
{: .no_toc }

In this document lists the specification of each declaration type. See the description of the [common declaration format](declaration_format.html).

For a better understanding of how to build a practical model using the declarations, see the [guide](guide.html).

Here we specify how these declarations work in model, module, preamble and library files. [Declarations in data_set files function differently](../datafiledocs/new_project.html).

## Signatures
{: .no_toc }

In this document we will denote declaration signatures (i.e. what arguments they accept) using the following format

```python
decl_type(argname1:type1, argname2:type2, ..)
```

Here `decl_type` is the type of the current declaration, each `argname` is usually only something we use in this document when we refer to the argument in the text. You don't refer to it in the code. For instance, the signature `constant(name:quoted_string, u:unit, value:real)` means you can declare constants like

```python
euler : constant("Euler number", [], 2.71)
```

The `type` is one of the following
- A [token type](declaration_format.html#token-types), like `quoted_string`, `real`, `integer`, `boolean`. In this case, the argument must be a single literal value, e.g. `8`, `3.14`, `"Water"`, `true`.
- Another declaration type. In this case you must pass either the identifier of a declared entity of that type, or inline a declaration of that type.
- `location`. Here you must pass a [location](central_concepts.html#components-and-locations), e.g. `soil.water`. In some cases these can have bracketed restrictions (this will be documented separately). Alternatively, you can pass an entity of `loc` type.
- `any`. Any argument type, but limitations may be specified in the text.

If an argument is literally called `name:quoted_string`, it has the formal semantics of an [entity name](central_concepts.html/names-and-identifiers).

If an argument is specified as `argname:type...`, with trailing dots, it means you can pass multiple arguments of that type after one another

If an argument type is `(type1|type2|..)` it means it can have any of those types.

If several signatures are listed, they are different alternatives.

If you see **Bind to identifier: yes** it means that the declaration creates an entity that can be (optionally) bound to an identifier, e.g.

```python
soil : compartment("Soil")
```

In this example, the declaration of the compartment with name "Soil" is bound to the identifier `soil`, which now refers to this entity in the rest of this scope. For instance, `soil` can be passed as an argument to another declaration if that argument is of type `compartment`.

## model

Context: File top scope

Bind to identifier: no

Signature:

```python
model(name:quoted_string) { <declaration-body> }
```

Only a single model declaration can appear in each file.

This is the specification of a Mobius2 model. All parts of the model are created or loaded as a consequence of declarations in the body of the `model` declaration.

## module

Context: One of: file top scope, or model scope (called inlined module).

Bind to identifier: no

Signature:

```python
module(name:quoted_string, v:version) { <declaration-body> }
module(name:quoted_string, v:version, load_arguments:any...) { <declaration-body> } # (top scope only)
```

Multiple modules and preambles can be declared in the same file.

Load arguments can be provided if the module is *not* inline-declared. The load arguments are a list of identifiers that are passed to the module from the loading model, and must be specified as `identifier : type`. For instance, if the declaration is

```python
module("A module", version(0, 0, 1),
	a : compartment,
	q : quantity
)
```

the module must be loaded with load arguments of these types when it is [loaded in the model](#load). The identifiers `a` and `q` are visible in the module scope in the above example. For instance, this module can be loaded in model scope using

```python
# The names here are arbitrary
a : compartment("A")
q : quantity("Q")
load("the_module_file.txt", module("A module", a, q))
```

Internally, a `module` declaration creates a `module_template`. This template is then instantiated when it is loaded using a `load` declaration, and you can instantiate a `module` several times with different load arguments.

An inlined module declaration is automatically loaded (instantiated), and can only have one instance.

If a module has a `preamble` as a load argument, the declaration scope of the passed preamble is loaded into the module scope.

## preamble

Context: File top scope.

Bind to identifier: no

Signature:

```python
preamble(name:quoted_string, v:version, load_arguments:any...) { <declaration-body> }
```

Multiple modules and preambles can be declared in the same file.

A preamble is used to create a common place to declare certain entities like parameters and properties that are shared among several modules if you don't want to declare them in model scope directly.

A preamble functions like a module in that the preamble declaration is a template that can be instantiated several times (see [`load`](#load) ).

Example:

```python
# In the module/preamble file(s):

preamble("A preamble", version(0, 0, 1),
	s : compartment
) {
	par_group("Group", s) {
		p : par_real("The parameter", [], 1)
	}
}

module("A module", version(0, 0, 1),
	r : preamble
) {
	# The parameter 'p' and any other symbol declared in
	# the preamble is visible here since that preamble is
	# passed as the 'r' argument to this module in the model
	# load below.
}

# In the model file

model("A model") {
	s : compartment("S")
	load("the_module_file.txt",
		# Note that we can bind the preamble load to an identifier,
		# just not the preamble declaration above.
		r:preamble("A preamble", s),
		module("A module, r))
}
```

## library

Context: File top scope.

Bind to identifier: no

Signature:

```python
library(name:quoted_string) { <declaration-body> }
```

Multiple libraries can be declared in the same file.

A library is somewhere you can declare constants and functions that you want to reuse in many modules.

These can be loaded into another declaration scope using a `load` declaration.

Mobius2 also has a [standard library](../existingmodels/autogen/stdlib.html) with several reusable functions. These are loaded using e.g.

```python
load("stdlib/atmospheric.txt", library("Meteorology"))
```

Since the path starts with "stdlib", Mobius2 will look for it in the "Mobius2/stdlib" path.

## version

Context: File top scope (argument to module/preamble only).

Bind to identifier: no

Signature:

```python
version(major:integer, minor:integer, revision:integer)
```

The only purpose of the `version` is to document `module` and `preamble` versions.

## load

Context: model, module, preamble or library scope.

Bind to identifier: no

Signature

```python
load(file:quoted_string, <special>...)
```

A load can be used to load either a library or a module/preamble.

### Library loads

A library is loaded using the syntax

```python
load("some_library_path.txt", library("Library name"), ...)
```

You can load several libraries from the same file in one `load` declaration.

A library will be processed once if it is loaded at least one place (it can be loaded multiple places). Any identifier that is declared in the declaration scope of the library body will be visible in the scope where the `load` declaration is.

Loads do not cascade, so if a library loads another library, the identifiers declared in the second library are not visible to someone else who loads the first library (unless they also directly load the second library). Library loads are allowed to be circular (two libraries are allowed to load one another for instance).

### Module and preamble loads

Modules and preambles can only be loaded in the model scope (not inside another module for instance). 

For these you must also pass load arguments if there are any in the [declaration of the module](#module) or [preamble](#preamble) you want to load.

Moreover, you can create different instantiations of the same module or preamble by providing them with a separate load name. Here are some examples:

```python
# Here we pass the load arguments a, b, c to the module load.
load("some_module_path.txt",
	module("The module declared name", a, b, c))

# Here we load the module using a different name so that one could potentially
# load a separate instance of it.
load("some_module_path.txt",
	module("Another module declared name", "Module load name", e, f, g))
```

If a module is loaded twice using the same load name (or without a load name), only the first load that is processed counts, and subsequent ones are ignored. Right now, there is no error if two loads with coinciding names have disagreements about their load arguments, but we plan to introduce a check for that later.

If you load a preamble, you can bind it to an identifier that can be passed into module loads (if they have such a preamble load argument). For instance,

```python
s : compartment("S")
load("the_module_file.txt",
	r:preamble("A preamble", s),
	module("A module, r))
```

## extend

Context: `model` scope.

Bind to identifier: no

Signature:

```python
extend(model_file:quoted_string)
```

Optional notes:

```python
@exclude(exclusion:any...)
```

An `extend` takes all the declarations from another model and puts them in the main scope of the current model. This also works recursively (but you can't have circular extends).

This can for instance be used to build a water quality model on top of a hydrology model.

The `@exclude` note can be used to omit certain declarations in the extended model. For instance,

```python
extend("some_model.txt") @exclude(a : compartment, b : quantity)
```

will omit any declaration of a compartment with identifier `a` and quantity with identifier `b` in the extended model.

Since the extended model typically relies on all the entities it declares, this is mostly used if you extend two models that declare the same entites, to avoid conflict. It can also be used to e.g. replace what ODE `solver` is used.

## compartment, property, quantity

Context: `model` scope (all), or `module`/`preamble` scope (`property` only).

Bind to identifier: yes

Signature:

```python
compartment(name:quoted_string)
compartment(name:quoted_string, distribution:index_set...)
quantity(name:quoted_string)
quantity(name:quoted_string, distribution:index_set...)
property(name:quoted_string)
property(name:quoted_string) { <math-body> }
```

These entities are collectively known as "component".

A distribution can create one copy of all state variables with this component in their *location* (whether or not that happens in practice comes down to several factors - will be documented later). It can also be used to distribute [parameter groups](#par_group).

If a property has a math body, that is called the "default code" for that property. If you create a state variable (`var`) with that property as its last location component and the state variable itself doesn't have a math body, the default code will be used instead. Note that the default code will be resolved separately per state variable it is used for, using the state variable location as the [context location](math_format.html#the-context-location). However, the scope it is resolved in is still the scope it is declared in.

## par_group

Context: model, module or preamble.

Bind to identifier: no

Signature:

```python
par_group(name:quoted_string) { <declaration-body> }
par_group(name:quoted_string, distributes_like:(compartment|quantity)...) { <declaration-body> }
```

Optional notes:

```python
@index_with(distribution:index_set...)
```

A parameter group is created to embody a collection of parameters. It determines how the parameters within it can be distributed.

If a parameter group `distributes_like` one or more components, it will have a maximal distribution that is the union of the distributions of these components. Whether or not the group will have that full distribution in a particular model application depends on the [data set](../datafiledocs/new_project.html#parameter-groups).

The optional note `@index_with` can be used to set the maximal distribution directly, but this should only be used in rare instances where the default way to do it doesn't work.

## par_real, par_int, par_bool, par_enum

Context: par_group scope.

Bind to identifier: yes

Signature:

```python
par_real(name:quoted_string, u:unit, default:(real|integer))
par_real(name:quoted_string, u:unit, default:(real|integer), description:quoted_string)
par_real(name:quoted_string, u:unit, default:(real|integer), min:(real|integer), max:(real|integer))
par_real(name:quoted_string, u:unit, default:(real|integer), min:(real|integer), max:(real|integer), description:quoted_string)
par_int(name:quoted_string, u:unit, default:integer)
par_int(name:quoted_string, u:unit, default:integer, description:quoted_string)
par_int(name:quoted_string, u:unit, default:integer, min:integer, max:integer)
par_int(name:quoted_string, u:unit, default:integer, min:integer, max:integer, description:quoted_string)
par_bool(name:quoted_string, default:boolean)
par_bool(name:quoted_string, default:boolean, description:quoted_string)
par_enum(name:quoted_string, default:identifier) { <special> }
par_enum(name:quoted_string, default:identifier, description:quoted_string) { <special> }
```

These entities are collectively known as "parameter". Parameters are values that are held constant through each single model run, but unlike `constant`s, can be configured to have different values in e.g. data files or MobiView2. Parameters can also be distributed over index sets (this is determined by the `par_group` they are in).

- The `default` value is what you get for the parameter value if a specific value is not provided in the data set.
- The optional `min` and `max` values are guidelines to the user only, and the model will not complain if the user sets a value outside this bound. The bound is also the defaut bound for [sensitivity and optimization](../mobiviewdocs/sensitivity.html) setups.
- The optional `description` is displayed in MobiView2, and can be used to guide the user how to use this parameter.

The body of a `par_enum` must be a space-separated list of identifiers giving the set of values this parameter can have. This functions as the declaration of these identifiers. The `default` value must be one of these identifiers.

## constant

Context: model, module, preamble, library scopes.

Bind to identifier: yes

Signature:

```python
constant(name:quoted_string, u:unit, value:(real|integer))
constant(value:boolean)
```

A constant is a single value (can not be distributed) that can be referenced in math scope. It can be useful to put constants as named entities like this so that they don't appear as "magic value" literals directly in the code. This gives a form of documentation of what the constant is.

## function

Context: model, module, preamble, library scopes.

Bind to identifier: yes

Signature:

```python
function(<special>) { <math-body }
```

Functions in Mobius2 are meant to be used as simple code snippets that can be reused in many places.

The arguments to a function declaration declares the signature of how it can be used in a [function evaluation](math_format.html#function-evaluation). It is a comma-separated list of items, where an item is either
- A plain `identifier`
- Or an `identifier:unit` pair.
This identifier does not refer to an existing entity, instead it names that function argument inside the math body. If a unit is given, that argument is required to have that unit at every evaluation site.

For instance,

```python
very_fun : function(a, b) { a*b + 3 }
```

Can in a different math body be evaluated using the syntax

```python
very_fun(20, 10)   # Evaluates to 20*10 + 3 = 203
```

If the declaration is

```python
even_more_fun : function(a : [k m], b : [])
```

the first argument must have unit `[k m]` and the second be dimensionless wherever the function is evaluated.

Declared functions are always "inlined" at every evaluation site, meaning a separate copy of the function body is resolved and pasted in at that site. This means that you can't have recursive functions (functions that call themselves), but that may be implemented separately later.

The body of a `function` is limited in that it can not refer to parameters or state variables directly, instead you must pass their values in as arguments. You can however refer to constants.

## var

Context: module scope.

Signature:

```python
var(place:location, u:unit)
var(place:location, u:unit) { <math-body> }
var(place:location, u:unit, name:quoted_string)
var(place:location, u:unit, name:quoted_string) { <math-body> }
var(place:location, u:unit, conc_u:unit)
var(place:location, u:unit, conc_u:unit, name:quoted_string)
```

Optional notes:

```python
@initial { <math-body> }
@initial_conc { <math-body> }
@override { <math-body> }
@override_conc { <math-body> }
@no_store
@add_to_existing { <math-body> }
@show_conc(medium:location, secondary_conc_u:unit)
```

A `var` creates a [primary state variable](central_concepts.html#state-variables).

The main component of a primary variable is the rightmost component of its location. We say that the variable is a `property` if the main component is a `property` and a `quantity` if the main component is a `quantity`. The name of the state variable is the `name` argument if it exists, otherwise it is the name of the main component.

You can declare a state variable using the same `place` (location) multiple times across the model, but only one of these are allowed to have any code associated with it (including code from notes). If multiple declarations declare a state variable using the same location, they will only create one single state variable for that location, and all declarations must agree on the name and unit for it.

Only a property variable can be provided with a main math body (i.e. a math body following the declaration and not attached to a note). If a property variable does not have code, it can get it from a separate declaration of the same variable, or from the default code of the property component. If no code is provided at all, the property variable becomes an input series.

Only a dissolved quantity variable can have a concentration unit, and the concentration unit must be [convertible](units.html#conversion) to the ratio of the unit to the unit of what it is dissolved in.

### `@initial` and `@initial_conc`

An `@initial` has a math block that tells Mobius2 how to compute the value of this variable in the initial setup before the first model step. If a property does not have initial code, it is assumed that its initial value is computed using the main code.

For a dissolved quantity, `@initial_conc` can be used instead to give an initial concentration, and the initial mass is then computed by Mobius2 by multiplying the initial concentration with the initial mass of what it is dissolved in.

By default all quantities have initial value 0 unless `@initial` or `@initial_conc` is provided.

### `@override` and `@override_conc`

These can be used to set a value for a quantity directly instead of relying on mass balance, just as if it was a property. The `@override_conc` sets the concentration, and the mass is then computed from that (as with `@initial_conc` above).

These can be useful in some instances where you don't want to simulate mass balance in some part of the system.

If the math block of one of these resolves to `no_override` at compile time, the override is cancelled, an the mass balance is used after all. This allows you to make user-defined switches between mass balance and override, for instance as in "SimplyC":

```python
var(soil.water.oc, [k g, k m -2], [m g, l-1], "Soil water DOC")
	@initial_conc { basedoc }
	@override_conc {
		basedoc                                             if soildoc_type.const,
		basedoc*(1 + (kt1 + kt2*temp)*temp - kso4*air.so4)  if soildoc_type.equilibrium,
		no_override                                         otherwise
	}
```

### `@no_store`

The `@no_store` note can only be put on properties. This tells Mobius2 not to record the time series of this variable in memory. This means it can't be plotted in [MobiView2](../mobiviewdocs/mobiview.html) or extracted in [mobipy](../mobipydocs/mobipy.html), but it can save some memory. This is especially recommended for intermediate computations in compartments that are distributed over large index sets.

You can not use `@no_store` on a variable if you access the `last()` value of it in a math scope somewhere.

The `@no_store` note can be ignored if it is overridden by a configuration to MobiView2 or mobipy.

### `@add_to_existing`

This adds the math block of the `@adds_to_existing` note to the value of the declaration of the same state variable if it is declared somewhere else.

### `@show_conc`

This creates time series storage for a separate concentration variable. For instance if you are looking at `river.water.sed.oc`, i.e. the organic carbon in the suspended sediments in the river water (particulate organic carbon), then the main concentration variable that is generated for this is the concentration of `river.water.sed.oc` in `river.water.sed`, i.e. it is the mass fraction of organic carbon in the particles. If you want it to display (e.g in MobiView2) the concentration of `river.water.sed.oc` in `river.water` instead, you can create a note like

```python
@show_conc(river.water, [m g, l-1])
```

This will not change what you get when you reference `conc(river.water.sed.oc)` in math scope, so to access this value in math scope, you will still need to write `conc(river.water.sed.oc)*conc(river.water.sed)` to access this value.

## flux

Context: module scope.

Bind to identifier: yes.

Signature:

```python
flux(source:location, target:location, u:unit, name:quoted_string)
```

Optional notes:

```python
@no_carry
@no_carry { <special> }
@no_store
@specific { <math-body> }
@bidirectional
@mixing
```

The flux declaration creates a [flux state variable](central_concepts.html#fluxes).

The source and target locations can some times be restricted along connections. This will be documented separately.

The flux unit must be [convertible](units.html#conversion) to the unit of the source (if it is a variable, otherwise the unit of the target) divided by the sampling step unit of the model application. Note that this means that Mobius2 will multiply the magnitude of the flux with a unit conversion to make it match the model sampling step unit, so that the same model can be run at different time scales without changes to the model code.

If both the source and the target are variables and the target does not have the same unit as the source, a `unit_conversion` must be declared somewhere in the model between the source and the target.

If the source is distributed over index sets that the target is not distributed over (and the target is not a connection), the flux will be summed over those index sets before being added to the target, applying an `aggregation_weight` if one exists.

### `@no_carry`

By default, a transport flux will be generated if some quantity is dissolved both in the source and target (or only the source if the target is 'out').

If the flux has `@no_carry` without a body, no transport fluxes of any dissolved quantities will be generated for it.

If it has `@no_carry` with a body, the body is a space-separated list of quantity components that should not be transported. All other quantities are transported if they would normally be.

### `@no_store`

This works the same way as a `@no_store` for a [`var`](#var).

### `@specific`

This is used if the source or target has a specific restriction. Will be documented separately.

### `@bidirectional`

It is recommended to put `@bidirectional` on fluxes if you allow it to go in both directions and it can carry dissolved quantities. A flux goes in the opposite direction when its value is negative. In that case, it should be the concentrations of dissolved quantities in the target instead of the source that determines the transport fluxes of these quantities.

The only reason not to put `@bidirectional` on every flux is that it could make transport fluxes a little slower to compute since they have to check the direction of the flux each time.

Note that if you have a flux that goes negative and it is not declared as `@bidirectional`, there will not be any error, so you have to be careful about this yourself.

It will not work correctly to have a discrete flux (one where the target or source is not on a solver) go negative.

### `@mixing`

This declares that the flux goes in both directions with the same magnitude. It also has all the implications of `@bidirectional`. A `@mixing` flux will not change the amount of the source or the target, but it will mix any dissolved quantities that exist both in the source and target.

## loc

Context: model, module, preamble scope

Bind to identifer: yes

Signature:

```python
loc(location|parameter|constant|connection)
```

This allows you bind a location, parameter or constant to a new identifier. This identifier can be referenced in math. If it is a location, it can also be used as a location argument to another declaration.

If it is a location, it can also have connection restrictions in the same way the target of a flux can.

Creating `loc` declarations has a couple of use cases, one important one being to pass the target of a flux as a load argument to the module that declares this flux so that the module becomes independent of how discharges from it are connected up.

It can also be a way to pass some value to a module without that module having to know if the value is a state variable, parameter or constant.

## index_set

Context: model,

Bind to identifier: yes

Signature:

```python
index_set(name:quoted_string)
```

Optional notes:

```python
@sub(parent:index_set)
@union(index_set...)
```

This creates an index set that compartments and quantities (and thus par_groups and state variables) can be distributed over.

Sub-indexed and union index sets will be documented separately.

## connection

Context: model, module scopes.

Bind to identifier: yes.

Signature:

```python
connection(name:quoted_string)
```

Optional notes:

```python
@grid1d(comp:compartment, set:index_set)
@directed_graph() { <regex-body> }
@directed_graph(edge_set:index_set) { <regex-body> }
@no_cycles
```

This creates a connection that you can direct fluxes along, and some times do value lookups along.

You must provide either a `@grid1d` or `@directed_graph` note to specify the connection type.

### `@grid1d`

This arranges instances of the compartment `comp` next to one another along the index set `set` (in linear order of that index set). The compartment `comp` is required to have been declared as distributed over `set` (but not restricted to that index set).

The specifics of how you can use this will be documented separately

### `@directed_graph`

This arranges one or more indexed compartments along a directed graph. You can use it to path fluxes along networks of different compartments or different instances of these compartments.

If you want to allow a single node to have multiple outgoing arrows, you need to provide an edge index set for the connection. The `edge_set` must have been declared as sub-indexed to the index_set of the node(s) that can have multiple outgoing edges, or potentially to a union of these if they are different.

The regex body is currently not fully functional. It is supposed to describe how paths in the graph can look. For now just follow one of the below examples

```python
# Each maximal path is one or more instances of the compartment 'a'
@directed_graph { a+ }

# Each maximal path is one or more instances of the compartments 'a', 'b' or 'c'
@directed_graph { (a|b|c)+ }

# Each maximal path is one or more instances of the compartments 'a', 'b' or 'c',
# followed by an 'out' (the last arrow can point out of the model domain)
@directed_graph { (a|b|c)+ out }
```

More detailed documentation will follow later.

### `@no_cycles`

This can only be used if the connection is `@directed_graph`, and it disallows the data_set to specify cycles (circular paths) in the graph.

## solver

Context: model scope.

Bind to identifier: yes.

Signature:

```python
solver(name:quoted_string, f:solver_function, init_step:unit)
solver(name:quoted_string, f:solver_function, init_step:unit, rel_min:real)
solver(name:quoted_string, f:solver_function, init_step:par_real)
solver(name:quoted_string, f:solver_function, init_step:par_real, rel_min:par_real)
```

A solver is an ordinary differential equation (ODE) solver algorithm. You can tell Mobius2 to treat quantity primary variables as ODE variables if you `solve()` them using a solver (see below).

The `solver_function` is a separate entity type that you (for now) can't declare. Instead Mobius2 provides the following solver functions

| Name | Description |
| ---- | ----------- |
| `euler` | A solver using [Euler's method](https://en.wikipedia.org/wiki/Euler_method) with fixed step size (non-adaptive). This solver is mostly included for illustration since it is not that precise. |
| `inca_dascru` | A adaptive Runge-Kutta 4-5 solver based on \[Wambecq78\] and its implementation in the INCA models. This solver creates precise simulations of many systems. |

We plan to add more solver algorithms eventually.

The `init_step` is the time unit of the solver integration step, which is typically smaller than the sampling step of the model. The algorithm is more precise the smaller the integration step is, but it also causes it to run slower. If the solver is adaptive, it is allowed to dynamically adjust its step size to achieve higher precision. In that case, the `rel_min` gives the relative minimum size of the step it is allowed to adjust to. The minimum step will be `init_step*rel_min`.

If either `init_step` or `rel_min` are given as parameters, they can be adjusted by users of the model (`init_step` must have a unit that is convertible to the sampling step unit of the model, while `rel_min` must be dimensionless.

\[Wambecq78\] Wambecq, A.: Rational Runge–Kutta methods for solving systems of ordinary differential equations, Computing, 20, 333–342, [https://doi.org/10.1007/BF02252381](https://doi.org/10.1007/BF02252381), 1978. 

## solve

Context: model scope.

Bind to identifier: no.

Signature:

```python
solve(sol:solver, variable:location...)
```

Tell the framework to use the solver `sol` to solve one or more quantity primary variables given by their locations.

## aggregation_weight

Context: model scope.

Bind to identifier: no.

Signature:

```python
aggregation_weight(source:compartment, target:compartment) { <math-body> }
aggregation_weight(source:compartment, target:compartment, c:connection) { <math-body> }
```

If a flux goes from a compartment that is distributed over index sets that the target is not distributed over, it will be summed over those index sets before it is added to the target. If you provide an aggregation_weight between those compartments, the sum will be weighted with the expression in the math body of the aggregation_weight.

Remember that the weight is only applied to the value that is added to the target of the flux, not the value that is subtracted from the source.

An aggregation_weight (if it exists) is also applied when computing the result of the [`aggregate()` special directive](math_format.html#special-directives).

If a connection is specified on the aggregation weight, the weight will be applied to fluxes that go between those compartments along that connection only.


## unit_conversion

Context: model scope.

Bind to identifier: no.

Signature:

```python
unit_conversion(source:location, target:location) { <math-body> }
```

If a flux goes from a source that has a different unit than the target, you must provide a unit_conversion that shows how to convert the value.

The conversion factor will itself be automatically converted to give the right scale. For instance, if the source has unit `[k g, m-2]` and the target has unit `[k g]`, you can provide a conversion factor that has unit `[k m 2]`. The framework will then automatically scale it so that it has unit `[m 2]`.

## \[unit\], unit_of, compose_unit

Context: model, module, preamble, library, par_group scopes.

Bind to identifier: yes.

Signature:

```python
[<special>]
unit_of(par_real|par_int|constant|location)
compose_unit(u1:unit, u2:unit...)
```

Almost every value in Mobius2 must have a unit, and this is used by the framework to provide automatic unit conversions and do unit checks on expressions.

Regular unit declarations are on a [special format](units.html#the-unit-declaration-format).

The `unit_of` declaration refers to the unit of another entity like a parameter, constant or state variable.

The `compose_unit` declaration uses unit arithmetic to multiply several units together.

All of these declarations create an entity of type `unit`.

## discrete_order

Context: module scope.

Bind to identifier: no.

Signature:

```python
discrete_order { <special> }
```

The discrete order gives the order of evaluation of discrete fluxes. These are fluxes going from quantity primary variables that are not ODE (not on a solver). When a discrete flux is evaluated, it is directly (not using an integration step) subtracted from the source and added to the target, and so the order of evaluation can matter.

The body of a discrete_order declaration is a space-separated list of flux identifiers.

If two discrete fluxes are not given an order relation by a discrete_order, their order of evaluation could be arbitrary.

## external_computation

This is a feature that for now is only (and can only be) used by NIVAFjord to call into some C++ code. It may be expanded at a later point.