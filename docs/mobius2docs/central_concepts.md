---
layout: default
title: Central concepts
parent: Model language
nav_order: 0
---

# Central concepts

Mobius2 models are built around some central concepts like state variables, fluxes, parameters, etc. These are important both for understanding how existing models work and for builiding new models with the language.

This is an overview. More detailed documentation of how to create the below entities in the model language will be documented later.

## Names and identifiers

Most entities in the model can have identifiers and names.
- Identifiers (some times called symbols) are short strings that identify the given entity. An example is `groundwater`.
- Names are typically more descriptive, e.g. "Groundwater denitrification rate at 20 degrees Celsius".

## Modules

Modules are collections of parameters, state variables and other model components that are tightly related. Many modules can be composed to a larger model.

For instance one module could specify how water is transported out of the soil layer, another the groundwater and so on. Modules are formally independent of one another, but can be connected in the model by directing the outgoing fluxes from one module to a quantity in another. A single module can also be applied to different subsystems in the model (such as using the same module for air-sea heat fluxes or phytoplankton for a lake module and a fjord module).

Moreover, the modules do for the most part not need to know about how a system is distributed. So you could use the same soil water module for both a fully and a semi-distributed hydrology model.

One can also add biochemical modules that specify processes of dissolved quantities without these modules having to know anything about the water transport.

These are just examples, the framework is very general and flexible, and can be used to model a large range of systems not restricted to water quality.

## Index sets and distributions

In order to model multiple units of the same type of object (parameter, state variable, ..), one can distribute them over index sets. For instance you can have several river sections in your catchment, and you typically want to model them using the same equations, just with different parametrization per section.

An object can be distributed over multiple index sets, which means you get a copy per tuple of elements from these index sets. I.e. in a formal mathematical sense, it is distributed over the Cartesian product of the index sets. So for instance, if `soil` is distributed over `subcatchment` and `landscape_units` you get one copy of the soil compartment per pair of subcatchment and landscape unit.

## Parameters

Parameters are values that are held constant through each single model run. They are typically used to constrain a model to a given geographical location without changing the model itself. Parameters can either be "global" (not in the sense as for the entire world, but the entire modeled system), or be distributed, i.e. have different values per different copies of the subsystem (such as having different slope per river section or different nitrogen uptake rates per land type).

Parameters are always organized into parameter groups, and it is the group that determines the distribution of the parameters within it.

## Components and locations

We use the term *location* to refer to a sort of address of a state variable. A location is a '.'-separated chain of components, e.g. `soil.water.temp`. The types of component are
- **Compartment**. The first item (and only the first) component of a location is always a compartment. This typically represents a physical location, such as the soil layer, a lake, the heart of a fish, etc.
- **Property**. The last (and only the last) component of a location can be a property. These are used to address values that are computed from the state of the rest of the system (and can be referenced in other equations), but is not subject to mass balance.
- **Quantities**. These are used to form the location of state variables that are subject to automatic mass balance. See more about this below.

A compartment can be distributed so that you get multiple copies of the same compartment. This will usually also cause a distribution of associated state variables (the details are complicated, and will be documented separately). (You can also distributed a quantity component to get multiple types of the same quantity, for instance you can have several contaminants that are modeled with the same equations, but are parametrized differently).

## State variables

In Mobius2 we call all variables that can change over time for a State Variable (this does not include local variables that are declared in a single equation). State variables can represent the state of a part of the system, such as the amount of water in the soil column, or it can be a rate of change of another variable, such as a transport rate, or any other value that is used for computing other state variables or provide diagnostics of the system.

Most state variables are available for plotting in [MobiView2](../mobiviewdocs/plots.html) or for extraction in [mobipy](../mobipydocs/mobipy#series). (Some are hidden by default - separate documentation will be writen about how to access the hidden ones).

State variables can either be declared or generated
- **Declared** state variables are explicitly declared in the source code of one of the modules.
- **Generated** state variables are constructed by the framework to automatically compute things like
	- Concentrations of dissolved variables
	- Transport fluxes of dissolved variables
	- Different types of accumulation variables, e.g. the rate of some quantity coming in along a given connection.
	
Declared state variables can either be primary variables or fluxes.

### Primary variables

Primary variables have a single *location*, e.g. `soil.water` or `river.water.oc`. This can be thought of as the address of the variable, and is used to refer to it in several contexts, including equation code.

We say that the type of the variable is the same as the type of the last component of the location, e.g. `soil.water` is a quantity if `water` is a quantity, and `soil.water.temp` is a property if `temp` is a property.

#### Properties

A property is any number that is computed for each time evaluation and whos value can be referenced in other equations. Properties are typically not *directly* dependent on the previous state of the system (though there are exceptions to that).

While a property can have more than two components in its location, this is mostly for convenient organization, and does not have the same semantics as the concept of a dissolved variable (see below).

Any property variable can be overridden with an input series from the [data set](../datafiledocs/datafiles.html). Some properties are without equations and must be provided as input series.

#### Quantities

A quantity is something that can be transported using fluxes, and whos mass balance is tracked by the Mobius2 framework.

Quantities are typically not directly computed using a single equation. Instead their state set with an initial value, and is updated by subtracting outgoing fluxes and adding incoming fluxes. This is most often done by using an ordinary differential equation integration algorithm.

The value of a quantity can in some instances be overridden with an equation, in which case all mass balance guarantees are absolved.

If the location of a quantity has more than two components, it is called a *dissolved* quantity. Unless otherwise specified, dissolved quantities are automatically transported along with what they are dissolved in. So if a flux transports `soil.water` somewhere, it will also transport the soil water dissolved organic carbon `soil.water.oc` proportionally to the latters concentration.

Note that the term "dissolved" is just a convenient way to name it, but this system could just as well be used to model `habitat.cattle.lice`, i.e. anything that is attached to and transported along with another quantity.

### Fluxes

We use the term "flux" in the loose sense of a quantity that is transported from somewhere to somewhere else. This is *not* restricted to the formal meaning of a transport per surface area often used in physics.

Every flux has two locations, one source and one target. These are either
- Valid locations identifying primary variable quantities.
- `out`. A flux source or target is `out` if it is outside of the model domain. This means that it is either a pure source or sink in the modeled system.
- A target can also be along a connection, which makes it more flexible how to connect it up. See more about connections below.

Sources and targets can also be restricted along a 1D grid, such as identifying one end of the grid, but we will not go into detail about that here.

## Connections

Connections can be used to direct fluxes between differently indexed copies of the same state variable, or some times to different types of target state variable depending on the indexes of the source state variable.

For instance, a connection can be used to describe how different river sections connect to one another, and also to some times let a river section connect to a lake instead of another river section.

There are two types of connections, directed graph and 1d grid .

### Directed graph

This is the most flexible connection type, where connections can be over any directed graph of indexed compartments. Directed graphs can be both 1-way and 2-way.

They can be used to model river connections, downhill connections of land patches, migration routes, horizontal connections of fjord basins etc.

(We also plan to let graph connections connect differently indexed quantities so that you could have degradation graphs).

### 1D grid

This imposes a 1-dimensional grid on a single compartment along a single index set. It can be seen as imposing a linear order on the index set.

It is convenient for specifying instances where a variable depends on the state of the system immediately above or below it.

We use this to model layered fjord basins, lakes and groundwater reservoirs.

1D grid connections can be used to simulate finite-difference partial differential equations. Note that this is not limited to 1-dimensional systems since you could distribute the compartment along two or more index sets and impose a connections along each of them to simulate higher dimensions.

## Units

Every value in the model (parameter, state variable, constant, ..) must have a unit. All model equations are subject to unit checks so that the unit of any expression is guaranteed to be correct if the unit of each part of the expression is correct. This will be documented separately.



