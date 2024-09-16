---
layout: default
title: Simply
parent: Existing models
nav_order: 0
---

![Simply](../img/SimplyLogo.png)

The simply models are parsimonious hydrology, sediment, nutrient and contaminant models. The models are dynamic, and are spatially semi-distributed, i.e. there is the ability to include differences in hydrology, sediment and other processes between land use types and sub-catchments.

Key philosophies behind Simply development are:

1. Process representation should be as simple as possible, only including those processes that appear to dominate the catchment-scale response, whilst maintaining sufficient complexity for the model to be useful in hypothesis and scenario testing.
2. Process representation should be simple enough to allow parameter values to be constrained using available data. This involves keeping the number of parameters requiring calibration to a minimum, and aiming for as many parameters as possible to be in principle measurable, so their values can be based on observed data (either gathered from the study site or literature-based). We aim for it to be possible to include all uncertain parameters in uncertainty analysis.

Examples of potential model uses include:

1. Interpolating sparse monitoring data, to provide more ecologically-relevant estimates of in-stream phosphorus concentrations, or more accurate estimates of loads delivered downstream to lakes or estuaries.
2. Hypothesis testing and highlighting knowledge and data gaps. This in turn could be used to help design monitoring strategies and experimental needs, and prioritise areas for future model development.
3. Exploring the potential response of the system to future environmental change (e.g. climate, land use and management), including potential storm and low-flow dynamics.
Providing evidence to support decision-making, e.g. to help set water quality and load reduction goals, and advise on means of achieving those goals.

The Simply models originate from \[JacksonBlake17\], where the first version of SimplyP (including what was later named SimplyQ and SimplySed) was implemented as a [python program](https://github.com/LeahJB/SimplyP).

An early version of SimplyC was developed in \[Norling21\].

Some of the models also have [Mobius1 versions](https://github.com/NIVANorge/Mobius/tree/master/Applications/SimplyP).

Note that although all the Simply models are constructed with SimplyQ in mind as the hydrology model, they are in principle independent of the hydrology, and could be combined with other hydrology modules.

## SimplyQ

SimplyQ is a simple hydrology module. It is formulated as a two-box [linear reservoir model](https://en.wikipedia.org/wiki/Runoff_model_(reservoir)). The upper box is a soil box, while the lower one is groundwater. Both have a retention volume, below which there is no outflow. The baseflow index is a parameter that says how much of the soil runoff becomes groundwater recharge. There is also a quick flow component that discharges soil water directly to the stream when water input to the box (precipitation + snow melt) is high. Evapotranspiration is subtracted from the soil box, and is limited if the soil is dry.

The river component of SimplyQ transports the water along a branched river network.

See the [mathematical description](autogen/simplycnp.html#simplyq)

## SimplyCNP

SimplyCNP is the combination of SimplyQ with the C, N and P modules described below. It also includes smaller modules for organic N and P.

A publication is forthcoming.

## SimplyC

SimplyC is a dissolved organic carbon (DOC). The concentration of DOC in the soil water is set to have an equilibrium that depends on temperature and sulfate deposition. It can be configured either to always stay at this equilibrium or to tend towards it at a given rate (in the latter case, incoming clean water will dilute the concentration for a while after infiltration).

In the groundwater, the DOC concentration is either constant, set to the soil water average, or follows mass balance (with the recharge and discharge), with a constant decay rate.

In the river, the DOC can be given a [Q10](https://en.wikipedia.org/wiki/Q10_(temperature_coefficient))-governed decay rate.

See the [mathematical description](autogen/simplycnp.html#simplyc-land)

## SimplyN

SimplyN is a dissolved inorganic nitrate (DIN) module. It allows for atmospheric deposition and fixation inputs, fertilizer inputs and uptake+denitrification removal. There is also a Q10-based denitrification loss process in the river.

See the [mathematical description](autogen/simplycnp.html#simplyn)

## SimplySed

SimplySed is a suspended sediment mobilization and transport module. Land erosion depends on land slope, and can depend on a dynamic vegetation cover factor. Erosion also depends on the flow of water from land to the river.

See the [mathematical description](autogen/simplycnp.html#simplysed)

## SimplyP

SimplyP is a module for total dissolved phosphourous (TDP) and particulate phosphorous (PP). TDP in the soil solution is modeled using an *equilibrium phosphate concentration at net zero sorption* (EPC0) - type process, where the EPC0 can change over time. This model was first published in \[JacksonBlake17\].

See the [mathematical description](autogen/simplycnp.html#simplyp)

## SimplyTox

SimplyTox is a set of contaminant modules. They form a [Level IV multimedia fugacity model](https://en.wikipedia.org/wiki/Multimedia_fugacity_model) for soil, groundwater, river and lake. The process formulations are based on the ones for the INCA-Contaminants model \[Nizzetto15\], but slightly simplified.

Because of the fugacity approach, SimplyTox is suitable for modelling a large range of substances including [persistent organic pollutants](https://en.wikipedia.org/wiki/Persistent_organic_pollutant), or more water-soluble contaminants.

In all compartments, contaminants are partitioned between a truly dissolved phase, absorption to dissolved organic carbon and to solid organic carbon. In the soil there are two solid organic carbon boxes consisting of fast-accessible and slowly-accessible carbon. In the river, there is partitioning with both suspended particulate organic carbon and with the sediment layer. There is also air-water exchange both in the soil and the river surface.

A version of the model for deep groundwater with multiple layered reservoirs is also available.

Mathematical description is forthcoming

## Auxiliary

The Simply models can be combined with various snow, soil temperature and potential evapotranspiration models, examples of which are found in the repository.

## Citations

\[JacksonBlake17\] Jackson-Blake L. A., Sample J. E., Wade A. J., Helliwell R. C., Skeffington R. A. *Are our dynamic water quality models too complex? A comparison of a new parsimonious phosphorus model, SimplyP, and INCA-P*. Water Resources Research, 53, 5382–5399, [https://doi.org/10.1002/2016WR020132](https://doi.org/10.1002/2016WR020132), 2017

\[Norling21\] Norling, M. D., Jackson-Blake, L. A., Calidonio, J.-L. G., and Sample, J. E.: *Rapid development of fast and flexible environmental models: the Mobius framework v1.0*, Geosci. Model Dev., 14, 1885–1897, [https://doi.org/10.5194/gmd-14-1885-2021](https://doi.org/10.5194/gmd-14-1885-2021), 2021.

\[Nizzetto15\] Nizzetto L., Butterfield D., Futter M., Lin Y., Allan I., Larssen T. *Assessment of contaminant fate in catchments using a novel integrated hydrobiogeochemical-multimedia fate model*. Sci. Total Environ. 544, 553-563, [https://doi.org/10.1016/j.scitotenv.2015.11.087](https://doi.org/10.1016/j.scitotenv.2015.11.087) 2015

{% include lib/mathjax.html %}