---
layout: default
title: EasyTox
parent: EasyLake
grand_parent: Existing models
nav_order: 0
---

# EasyTox

**This page is under construction.**

TODO: Link to math descriptions. Link to source code. Reference to papers.

EasyTox is a contaminant module for the EasyLake model, to be used in conjunction with the SimplyTox contaminant catchment model.

This page describes the version of the model that was built for the FuNitr project.

## Hydrology and water balance

![Water flow diagram](../img/easytox/waterflow.png)

Figure: A conceptual water balance diagram for the model. This is a simplified diagram. It is possible to connect several river sections and lakes in arbitrary branching structures. Land hydrology can be separated into sub-catchments and land use classes.

### SimplyQ (catchment hydrology)

In this version of the SimplyQ hydrology model (**REF**), all precipitation is received by a snow layer, but if there is no snow, it continues directly to the soil water.

The snow model itself is based on the HBV Nordic snow model (**REF**). All precipitation are assumed to fall as snow below a given air temperature threshold (typically close to 0°C), and melts above another threshold. The snow has a small capacity to hold melt water, and only melt water above this capacity is discharged to the soil compartment.

The soil and groundwater compartments are so-called "linear reservoirs". In a linear reservoir, there is a water threshold below which there is no runoff. In the soil this is called the field capacity. Excess water above this threshold becomes runoff in a rate that is linearly proportional to the amount of excess water and inversely proportional to a given time constant.

In the soil there is also evapotranspiration which removes water from the system. There can be quick flow directly to the river when rain+melt is high.

A so-called "baseflow index" determines what proportion of the soil runoff goes to the groundwater. The remaining proportion goes to the river.

The retention time in the river is computed using Manning's formula (**REF**), and some assumptions about the river slope and bank slope.

The lake runoff is computed using a rating curve that depends on the lake water level above the outlet level.

* The river retention time is typically short compared to the groundwater and lake, so processes in the latter two are usually more important for resulting contaminant concentrations.
* Even though the soil time constant is usually small, the field capacity can hold some amount of water, and so the effective soil retention time is higher than the soil water time constant. This also means that processes in the soil can be important.

### Lake physics

**TODO diagrams**

![Lake shape](../img/easytox/lake.png)
Figure: The subdivision and shape of a modeled lake.

TODO
- heat budget and ice, evaporation
- lake shape, division, mixing.

## Contaminants

Dissolved contaminants will follow the water transport paths in proportion to the amount of transported water and the contaminant concentration. The exception is that evapotranspiration $$Et$$ and lake evaporation $$Ev$$ don't bring with them any contaminants.

All deposition of the contaminants go directly to the soil water. There is also direct deposition to the lake surface.

Fate (TODO):
-partitioning (fugacity)
-biodegradation (where)
-photodegradation (lake)
-air-lake (and river) exchange.


{% include lib/mathjax.html %}