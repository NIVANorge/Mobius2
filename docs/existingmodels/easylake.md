---
layout: default
title: EasyLake
parent: Existing models
nav_order: 1
---

# EasyLake

EasyLake is a set of lake modules consisting of one physical module for water transport and temperature, and several biochemical modules that can be added on. This is meant as a module that should be part of a larger catchment model, providing transport and retention through any lakes that are part of the river system. It can also be used for studying water quality in the lake itself if something more detailed like [NIVAFjord](nivafjord.html) is not needed.

## EasyLake physical

The EasyLake model is a two-box lake model consisting of an upper epilimnion and a lower hypolimnion. The epilimnion thickness can vary, and the two compartments are mixed if their temperature becomes close to one another. Temperatures, evaporation and ice are computed using physically based processes. Lake bathymetry (cross-section area as a function of depth) can be parametrized to fit several different types of profiles.

![EasyLake](../img/EasyLake.png)
Figure: The water and energy balance in EasyLake. Discharge enters the lake through possibly multiple inlets $$Q_{in}$$, and exits through one outlet $$Q_{out}$$. Precipitation $$P$$ and evaporation $$E$$ also affect the water balance. The lake has a water level (from the max depth up to the surface) and an epilimnion thickness. Discharge is a function of how much the level is above a certain height. The energy balance in summer is governed by surface heat fluxes (shortwave, longwave, sensible and latent). When there is ice cover, ice thickness instead follows Stefan's law. The temperature curve is self-similar and is continuous between the variable epilimnion temperature and the constant bottom temperature. The epilimnion temperature is adjusted to match the heat balance for the entire lake. (This figure is a bit out of date and will be replaced, the bathymetry is differently parametrized now, and no longer uses the $$w$$ and $$L$$ parameters).

An earlier version of EasyLake was first deployed in

Norling, M. D., Clayer, F., Gundersen, C. B.: Levels of nitramines and nitrosamines in lake drinking water close to a CO2 capture plant: A modelling perspective, Env. Res. 212(D), [https://doi.org/10.1016/j.envres.2022.113581](https://doi.org/10.1016/j.envres.2022.113581), 2022

See the [mathematical description](autogen/easylake.html).

## EasyChem

EasyChem is a module for computing nutrient retention in the lake. It has microbial processes for organic carbon breakdown, and has phytoplankton growth. Nutrient retention depends on the phytoplankton C:N and C:P ratios. Phytoplankton growth is also limited by light availability and temperature.

See the [mathematical description](autogen/easylake.html#easychem).

## EasyChem particulate

This is a simple module that describes how particles settle to the lake sediments.

See the [mathematical description](autogen/easylake.html#easychem-particulate).

## EasyTox

This is a contaminant module for EasyLake meant for use in combination with [SimplyTox](simply.html#simplytox). The contaminants in each lake compartment are partitioned between truly dissolved form, dissolved organic carbon and particulate organic carbon.

There is also surface-air and surface-sediment exchange of contaminants that works similarly to the SimplyTox river compartment.

Mathematical description is forthcoming.

{% include lib/mathjax.html %}