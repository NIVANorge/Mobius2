---
layout: default
title: EasyLake
parent: Existing models
nav_order: 1
---

# EasyLake

EasyLake is a set of lake modules consisting of one physical module for water transport and temperature, and several biochemical modules that can be added on. This is meant as a module that should be part of a larger catchment model, providing transport and retention through any lakes that are part of the river system. It can also be used for studying water quality in the lake itself if something more detailed like [NIVAFjord](nivafjord.html) is not needed.

## EasyLake physical

![EasyLake](../img/EasyLake.png)
(this figure is a bit out of date and will be replaced, the bathymetry is differently parametrized now).

The EasyLake model is a two-box lake model consisting of an upper epilimnion and a lower hypolimnion. The epilimnion thickness can vary, and the two compartments are mixed if their temperature becomes close to one another. Temperatures, evaporation and ice are computed using physically based processes. Lake bathymetry (cross-section area as a function of depth) can be parametrized to fit several different types of profiles.

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