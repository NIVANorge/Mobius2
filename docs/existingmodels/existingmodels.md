---
layout: default
title: Existing models
nav_order: 2
has_children: true
---

# Existing models

The Mobius2 framework comes with several existing modules and models that can be used directly or modified to accomodate new research questions. These include

- The [Simply family](simply.html). This is a family of catchment models building on the hydrology model SimplyQ. Modules for nutrients and contaminants exist.
- [EasyLake](easylake.html). This is a simple 2-box lake model that is intended for inclusion into a larger catcment model. It predicts residence time, temperature and ice in the lake, and has modules for nutrients retention and contaminants.
- [NIVAFjord](nivafjord.html). This is a 1-dimensional layered (multi-)basin model that can be used to model e.g. fjords, lagoons and lakes. It has biochemistry modules for nutrients, phytoplankton and sediments.

If desired, the above models can be coupled together into a full catchment-to-coast system, or you can use them separately to study smaller sub-systems.

The following model is stand-alone:
- [MAGIC](magic.html). This is a longer-timescale model for development of soil water chemistry (with a focus on acidity) in smaller catchments.

In addition to these, Mobius2 comes with modules for other processes that are convenient to include into larger models, for instance
- [Snow pack and melt](autogen/auxiliary.html#hbvsnow)
- [Evapotranspiration](autogen/auxiliary.html#degree-day-pet) (Penman-Monteith and Priestley-Taylor are also available, but not documented yet)
- [Atmospheric variables](autogen/auxiliary.html#atmospheric) (radiation, humidity...)
- [Air-sea heat exchange](autogen/auxiliary.html#airsea-lake) (including ice growth)

Other models include
- Air-sea exchange in presence of a partial cover with floating solar panels.
- EasyReservoir - a version of EasyLake for regulated reservoirs.

We expect to eventually port over other models from Mobius1 such as SedFlex and the plastic models.