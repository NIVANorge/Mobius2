---
layout: default
title: SimplyQ
parent: Mathematical description
grand_parent: Existing models
nav_order: 0
---

# SimplyQ

This is auto-generated documentation based on the model code in [models/simplyq_model.txt](https://github.com/NIVANorge/Mobius2/blob/main/models/simplyq_model.txt) .
Since the modules can be dynamically loaded with different arguments, this documentation does not necessarily reflect all use cases of the modules.

See the note on [notation](autogen.html#notation).

The file was generated at 2024-04-29 12:11:16.

---

## SimplyQ land

Version: 0.5.0

File: [modules/simplyq.txt](https://github.com/NIVANorge/Mobius2/tree/main/models/modules/simplyq.txt)

### Description

This is an adaption of a hydrology module originally implemented in Python as a part of the model SimplyP, which was published as

Jackson-Blake LA, Sample JE, Wade AJ, Helliwell RC, Skeffington RA. 2017. Are our dynamic water quality models too complex? A comparison of a new parsimonious phosphorus model, SimplyP, and INCA-P. Water Resources Research, 53, 5382–5399. [doi:10.1002/2016WR020132](https://doi.org/10.1002/2016WR020132)

New to version 0.5 :
- New implementation in the Mobius2 framework.

Authors: James E. Sample, Leah A. Jackson-Blake, Magnus D. Norling

### External symbols

| Name | Symbol | Type |
| ---- | ------ | ---- |
| Soil | **soil** | compartment |
| Groundwater | **gw** | compartment |
| River | **river** | compartment |
|  | **runoff_target** | loc |
| Water | **water** | quantity |
| Flow | **flow** | property |
| Potential evapotranspiration | **pet** | property |
| Catchment area | **a_catch** | par_real |
|  | **gw_target** | loc |

### Parameters

| Name | Symbol | Unit |  Description |
| ---- | ------ | ---- |  ----------- |
| **Hydrology general** | | | |
| Baseflow index | **bfi** |  |  |
| Quick flow inflection point | **qqinfl** | mm day⁻¹ |  |
| **Hydrology land** | | | |
| Field capacity | **fc** | mm |  |
| Soil water time constant | **tc_s** | day |  |
| **Groundwater** | | | |
| Groundwater time constant | **tc_g** | day |  |
| Groundwater retention volume | **gw_ret** | mm |  |

### State variables

#### **Soil water volume**

Location: **soil.water**

Unit: mm

Initial value:

$$
\mathrm{fc}
$$

#### **Groundwater volume**

Location: **gw.water**

Unit: mm

Initial value:

$$
\mathrm{gw\_ret}+\left(\mathrm{tc\_g}\cdot \frac{\mathrm{river}.\mathrm{water}.\mathrm{flow}}{\mathrm{a\_catch}}\rightarrow \mathrm{mm}\,\right)
$$

#### **Soil water flow**

Location: **soil.water.flow**

Unit: mm day⁻¹

Value:

$$
\mathrm{rate} = \frac{\mathrm{water}-\mathrm{fc}}{\mathrm{tc\_s}} \\ \href{stdlib.html#response}{\mathrm{s\_response}}\left(\mathrm{water},\, \mathrm{fc},\, 1.01\cdot \mathrm{fc},\, 0,\, \mathrm{rate}\right)
$$

### Fluxes

#### **Quick flow**

Source: soil.water

Target: river.water

Unit: mm day⁻¹

Value:

$$
\mathrm{drylim} = 0.9 \\ \mathrm{q\_in} = \left(\mathrm{in\_flux}\left(\mathrm{water}\right)\rightarrow \mathrm{mm}\,\mathrm{day}^{-1}\,\right) \\ \mathrm{q\_in}\cdot \href{stdlib.html#response}{\mathrm{s\_response}}\left(\mathrm{water},\, \mathrm{drylim}\cdot \mathrm{fc},\, \mathrm{fc},\, 0,\, 1\right)\cdot \mathrm{atan}\left(\frac{\mathrm{q\_in}}{\mathrm{qqinfl}}\right)\cdot \frac{2}{\pi}
$$

#### **Evapotranspiration**

Source: soil.water

Target: out

Unit: mm day⁻¹

Value:

$$
\href{stdlib.html#response}{\mathrm{s\_response}}\left(\mathrm{water},\, 0.5\cdot \mathrm{fc},\, \mathrm{fc},\, 0,\, \mathrm{pet}\right)
$$

#### **Soil runoff**

Source: soil.water

Target: runoff_target

Unit: mm day⁻¹

Value:

$$
\mathrm{flow}\cdot \left(1-\mathrm{bfi}\right)
$$

#### **Recharge**

Source: soil.water

Target: gw.water

Unit: mm day⁻¹

Value:

$$
\mathrm{flow}\cdot \mathrm{bfi}
$$

#### **Groundwater runoff**

Source: gw.water

Target: gw_target

Unit: mm day⁻¹

Value:

$$
\frac{\mathrm{max}\left(0,\, \mathrm{water}-\mathrm{gw\_ret}\right)}{\mathrm{tc\_g}}
$$

---

## SimplyQ river

Version: 0.5.0

File: [modules/simplyq.txt](https://github.com/NIVANorge/Mobius2/tree/main/models/modules/simplyq.txt)

### Description

The river part of SimplyQ.

Authors: Leah A. Jackson-Blake, Magnus D. Norling

### External symbols

| Name | Symbol | Type |
| ---- | ------ | ---- |
| River | **river** | compartment |
| Water | **water** | quantity |
| Flow | **flow** | property |
|  | **river_target** | loc |

### Parameters

| Name | Symbol | Unit |  Description |
| ---- | ------ | ---- |  ----------- |
| **Reach parameters** | | | |
| Reach slope | **slope** |  | Roughly the altitude difference between the uppermost and lowermost points divided by the length |
| Reach length | **len** | m |  |
| Manning's roughness coefficient | **c_mann** | s m⁻¹′³ | Default of 0.04 is for clean winding natural channels. See e.g. Chow 1959 for a table of values for other channel types |
| Initial reach flow | **init_flow** | m³ s⁻¹ |  |

### State variables

#### **Reach water volume**

Location: **river.water**

Unit: m³

Initial value:

$$
\mathrm{q} = \left(\mathrm{init\_flow}\Rightarrow 1\right) \\ \mathrm{depth} = 0.349 \mathrm{m}\,\cdot \mathrm{q}^{0.34} \\ \mathrm{width} = 2.71 \mathrm{m}\,\cdot \mathrm{q}^{0.557} \\ \mathrm{width}\cdot \mathrm{depth}\cdot \mathrm{len}
$$

#### **Reach flow**

Location: **river.water.flow**

Unit: m³ s⁻¹

Value:

$$
0.28 \mathrm{m}^{3}\,\mathrm{s}^{-1}\,\cdot \left(\mathrm{water}\cdot \frac{\sqrt{\mathrm{slope}}}{\mathrm{len}\cdot \mathrm{c\_mann}}\Rightarrow 1\right)^{1.5}
$$

Initial value:

$$
\mathrm{init\_flow}
$$

### Fluxes

#### **Reach flow flux**

Source: river.water

Target: river_target

Unit: m³ s⁻¹

Value:

$$
\left(\mathrm{flow}\rightarrow \mathrm{m}^{3}\,\mathrm{s}^{-1}\,\right)
$$



{% include lib/mathjax.html %}

