---
layout: default
title: SimplyQ
parent: Existing models
grand_parent: MobiView2
nav_order: 0
---

# SimplyQ

This is auto-generated documentation based on the model code in models/simplyq_model.txt .

The file was generated at 2024-04-17 17:28:36.

# SimplyQ land

## Docstring

This is an adaption of a hydrology module originally implemented in Python as a part of the model SimplyP, which was published as

[Jackson-Blake LA, Sample JE, Wade AJ, Helliwell RC, Skeffington RA. 2017. Are our dynamic water quality models too complex? A comparison of a new parsimonious phosphorus model, SimplyP, and INCA-P. Water Resources Research, 53, 5382–5399. doi:10.1002/2016WR020132](https://doi.org/10.1002/2016WR020132)

New to version 0.5 :
	- New implementation in the Mobius2 framework.

## Parameters

| Name | Symbol | Unit | Default | Min | Max | Description |
| ---- | ------ | ---- | ------- | --- | --- | ----------- |
| Baseflow index | bfi |  | 0.6 | 0 | 1 |  |
| Quick flow inflection point | qqinfl | mm day⁻¹ | 160 | 20 | 2000 |  |

Parameter group: **Hydrology general**

| Name | Symbol | Unit | Default | Min | Max | Description |
| ---- | ------ | ---- | ------- | --- | --- | ----------- |
| Field capacity | fc | mm | 120 | 0 | 1000 |  |
| Soil water time constant | tc_s | day | 2 | 1 | 40 |  |

Parameter group: **Hydrology land**

| Name | Symbol | Unit | Default | Min | Max | Description |
| ---- | ------ | ---- | ------- | --- | --- | ----------- |
| Groundwater time constant | tc_g | day | 30 | 1 | 400 |  |
| Groundwater retention volume | gw_ret | mm | 0 | 1 | 10000 |  |

Parameter group: **Groundwater**

# SimplyQ river

## Docstring

The river part of the SimplyQ module.

## Parameters

| Name | Symbol | Unit | Default | Min | Max | Description |
| ---- | ------ | ---- | ------- | --- | --- | ----------- |
| Reach slope | slope |  | 0.014 | 1e-05 | 3 |  |
| Reach length | len | m | 10000 | 0 | 1e+07 |  |
| Manning's roughness coefficient | c_mann | s m⁻¹′³ | 0.04 | 0.012 | 0.1 | Default of 0.04 is for clean winding natural channels. See e.g. Chow 1959 for a table of values for other channel types |
| Initial reach flow | init_flow | m³ s⁻¹ | 1 | 0 | 100 |  |

Parameter group: **Reach parameters**

