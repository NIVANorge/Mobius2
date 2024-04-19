---
layout: default
title: SimplyP
parent: Autogenerated documentation
grand_parent: Existing models
nav_order: 3
---

# SimplyP

This is auto-generated documentation based on the model code in [models/simplyp_model.txt](https://github.com/NIVANorge/Mobius2/blob/main/models/simplyp_model.txt) .
Since the modules can be dynamically loaded with different arguments, this documentation does not necessarily reflect all use cases of the modules.

The file was generated at 2024-04-19 12:47:35.

---

## SimplySed

Version: 0.6.0

### Description

This is a simple sediment transport module created as a part of SimplyP.

version 0.6:
* First Mobius2 version.
* Dynamic vegetation cover is computed a bit differently.

New to version 0.5.1:
* Updated parameter doc strings

New to version 0.5:
* Replaced Q - SS input relationship aQ^b with (aQ)^b. Reduces strong correlation/covariance of a and b params.
* Moved reach slope to be a reach parameter.
* Remove need for “Arable” land class.
* Can have dynamic erodibility for all land classes and % spring-sown crops.

### External symbols

| Name | Symbol | Type |
| ---- | ------ | ---- |
| Soil | **soil** | compartment |
| River | **river** | compartment |
| Water | **water** | quantity |
| Particles | **sed** | quantity |
| Erosion factor | **e_fact** | property |
| Catchment area | **a_catch** | par_real |

### Module functions

**cover_shape**(doy, doy_max, len, c_cov, shp_step, shp_tri, shp_smooth) = 

$$
\begin{cases}\href{stdlib.html#response}{\mathrm{step\_response}}\left(\mathrm{doy},\, \mathrm{doy\_max}-\frac{\mathrm{len}}{2},\, \mathrm{doy\_max}+\frac{\mathrm{len}}{2},\, \mathrm{c\_cov},\, 1,\, \mathrm{c\_cov}\right) & \text{if}\;\mathrm{shp\_step} \\ \href{stdlib.html#response}{\mathrm{wedge\_response}}\left(\mathrm{doy},\, \mathrm{doy\_max}-\frac{\mathrm{len}}{2},\, \mathrm{doy\_max},\, \mathrm{doy\_max}+\frac{\mathrm{len}}{2},\, \mathrm{c\_cov},\, 1,\, \mathrm{c\_cov}\right) & \text{if}\;\mathrm{shp\_tri} \\ \href{stdlib.html#response}{\mathrm{bump\_response}}\left(\mathrm{doy},\, \mathrm{doy\_max}-\frac{\mathrm{len}}{2},\, \mathrm{doy\_max},\, \mathrm{doy\_max}+\frac{\mathrm{len}}{2},\, \mathrm{c\_cov},\, 1,\, \mathrm{c\_cov}\right) & \text{if}\;\mathrm{shp\_smooth} \\ \mathrm{c\_cov} & \text{otherwise}\end{cases}
$$

### Parameters

| Name | Symbol | Unit |  Description |
| ---- | ------ | ---- |  ----------- |
| **Soil erodibility** | | | |
| Vegetation cover factor | **c_cov** |  | Vegetation cover factor, describing ratio between long-term erosion under the land use class, compared to under bare soil of the same soil type, slope, etc. Source from (R)USLE literature and area-weight as necessary to obtain a single value for the land class. |
| Day of year when soil erodibility is max for spring-grown crops | **doy_spring** | day |  |
| Day of year when soil erodibility is max for autumn-grown crops | **doy_autumn** | day |  |
| Proportion of spring-grown crops | **p_spring** |  |  |
| Reduction of load in sediment | **loadred** |  |  |
| Cover factor shape | **shp** |  |  |
| **Land slope** | | | |
| Mean slope of land | **land_slope** | ° |  |
| **River erosion** | | | |
| Erosion scaling factor | **ksed** | day mm⁻¹ |  |
| Erosion power factor | **psed** |  |  |

### State variables

#### **Variable reduction of load in sediments**

Location: **soil.loadred_var**

Unit: 

This series is externally defined. It may be an input series.

#### **Suspended sediments**

Location: **river.water.sed**

Unit: kg

Conc. unit: mg l⁻¹

#### **Time dependent vegetation cover factor**

Location: **soil.c_cover**

Unit: 

Value:

$$
\mathrm{E\_risk\_period} = 60 \mathrm{day}\, \\ \mathrm{spring} = \mathrm{cover\_shape}\left(\mathrm{time}.\mathrm{day\_of\_year},\, \mathrm{doy\_spring},\, \mathrm{E\_risk\_period},\, \mathrm{c\_cov},\, \mathrm{shp}.\mathrm{step},\, \mathrm{shp}.\mathrm{triangular},\, \mathrm{shp}.\mathrm{smooth}\right) \\ \mathrm{autumn} = \mathrm{cover\_shape}\left(\mathrm{time}.\mathrm{day\_of\_year},\, \mathrm{doy\_autumn},\, \mathrm{E\_risk\_period},\, \mathrm{c\_cov},\, \mathrm{shp}.\mathrm{step},\, \mathrm{shp}.\mathrm{triangular},\, \mathrm{shp}.\mathrm{smooth}\right) \\ \mathrm{spring}\cdot \mathrm{p\_spring}+\mathrm{autumn}\cdot \left(1-\mathrm{p\_spring}\right)
$$

#### **Erosion factor land**

Location: **soil.e_fact**

Unit: kg km⁻² day⁻¹

Value:

$$
\left(\mathrm{land\_slope}\cdot \mathrm{c\_cover}\cdot \left(1-\mathrm{loadred}\right)\cdot \left(1-\mathrm{loadred\_var}\right)\Rightarrow \mathrm{kg}\,\mathrm{km}^{-2}\,\mathrm{day}^{-1}\,\right)
$$

#### **Erosion factor river**

Location: **river.e_fact**

Unit: 

Value:

$$
\left(\mathrm{ksed}\cdot \frac{\mathrm{in\_flux}\left(\mathrm{water}\right)}{\mathrm{a\_catch}}\rightarrow 1\right)^{\mathrm{psed}}
$$

### Fluxes

#### **Sediment mobilization**

Source: out

Target: river.water.sed

Unit: kg day⁻¹

Value:

$$
\mathrm{a\_catch}\cdot \mathrm{aggregate}\left(\mathrm{soil}.\mathrm{e\_fact}\right)\cdot \mathrm{river}.\mathrm{e\_fact}
$$

---

## SimplyP

Version: 0.6.0

### Description

SimplyP is a parsimonious phosphorus model. It was originally implemented in Python and published as

[Jackson-Blake LA, Sample JE, Wade AJ, Helliwell RC, Skeffington RA. 2017. Are our dynamic water quality models too complex? A comparison of a new parsimonious phosphorus model, SimplyP, and INCA-P. Water Resources Research, 53, 5382–5399. doi:10.1002/2016WR020132](https://doi.org/10.1002/2016WR020132)

For news, updates and references, see [the model's github home page](https://nivanorge.github.io/Mobius2/existingmodels/simply.html)

New to version 0.6:
* The model has been ported to Mobius2. Everything is solved as one large coupled ODE system, so transport between land and river and between different river sections is more precise.

New to version 0.4:
* Landscape units are dynamic and user-specified instead of hardcoded.
* Sediment and hydrology equations are factored out into separate modules (SimplyQ, SimplySed)

New to version 0.3:
* More realistic hydrology.

For reference, here is [the original Python implementation of SimplyP](https://github.com/LeahJB/SimplyP), which is no longer being developed.

### External symbols

| Name | Symbol | Type |
| ---- | ------ | ---- |
| Soil | **soil** | compartment |
| Groundwater | **gw** | compartment |
| River | **river** | compartment |
| Water | **water** | quantity |
| Phosphorous | **phos** | quantity |
| Labile phosphorous | **plab** | quantity |
| Particles | **sed** | quantity |
| Erosion factor | **e_fact** | property |
| Total phosphorous | **tp** | property |
| Total dissolved phosphorous | **tdp** | property |
| Catchment area | **a_catch** | par_real |

### Parameters

| Name | Symbol | Unit |  Description |
| ---- | ------ | ---- |  ----------- |
| **P general** | | | |
| Dynamic EPC0, TDP and soil labile P | **dyn_epc0** |  |  |
| Soil mass per m2 | **m_soil_m2** | kg m⁻² |  |
| Phosphorous sorption coefficient | **kf** | l mg⁻¹ |  |
| Particulate P enrichment factor | **pp_enrich** |  |  |
| **Soil P** | | | |
| Initial soil TDP concentration and EPC0 | **init_epc0** | mg l⁻¹ |  |
| Initial total soil P content | **init_soil_p_conc** | mg kg⁻¹ |  |
| Inactive soil P content | **inactive_soil_p_conc** | mg kg⁻¹ |  |
| Net annual P input to soil | **p_input** | kg ha⁻¹ year⁻¹ |  |
| **Groundwater P** | | | |
| Groundwater TDP concentration | **gw_tdp** | mg l⁻¹ |  |
| **River P** | | | |
| Effluent TDP inputs | **eff_tdp** | kg day⁻¹ |  |

### State variables

#### **EPC0**

Location: **soil.epc0**

Unit: mg l⁻¹

Value:

$$
\mathrm{m\_soil} = \left(\mathrm{m\_soil\_m2}\rightarrow \mathrm{kg}\,\mathrm{km}^{-2}\,\right) \\ \begin{cases}\href{stdlib.html#basic}{\mathrm{safe\_divide}}\left(\mathrm{last}\left(\mathrm{plab}\right),\, \mathrm{kf}\cdot \mathrm{m\_soil}\right) & \text{if}\;\mathrm{dyn\_epc0} \\ \mathrm{init\_epc0} & \text{otherwise}\end{cases}
$$

Initial value:

$$
\mathrm{init\_epc0}
$$

#### **Soil DIP mass**

Location: **soil.water.phos**

Unit: kg km⁻²

Conc. unit: mg l⁻¹

Value:

$$
\begin{cases}\mathrm{expr} & \text{if}\;\mathrm{dyn\_epc0} \\ \left(\mathrm{init\_epc0}\cdot \mathrm{water}\rightarrow \mathrm{kg}\,\mathrm{km}^{-2}\,\right) & \text{otherwise}\end{cases}
$$

Initial value:

$$
\mathrm{init\_epc0}
$$

#### **Soil labile P mass**

Location: **soil.plab**

Unit: kg km⁻²

Value:

$$
\begin{cases}\mathrm{expr} & \text{if}\;\mathrm{dyn\_epc0} \\ \mathrm{last}\left(\mathrm{plab}\right) & \text{otherwise}\end{cases}
$$

Initial value:

$$
\left(\mathrm{init\_soil\_p\_conc}-\mathrm{inactive\_soil\_p\_conc}\right)\cdot \mathrm{m\_soil\_m2}
$$

#### **Labile P concentration**

Location: **soil.plab.plabconc**

Unit: mg kg⁻¹

Value:

$$
\frac{\mathrm{plab}}{\mathrm{m\_soil\_m2}}
$$

#### **Groundwater DIP**

Location: **gw.water.phos**

Unit: kg km⁻²

Conc. unit: mg l⁻¹

Value:

$$
\mathrm{gw\_tdp}
$$

Initial value:

$$
\mathrm{gw\_tdp}
$$

#### **River DIP**

Location: **river.water.phos**

Unit: kg

Conc. unit: mg l⁻¹

Initial value:

$$
\mathrm{gw\_tdp}
$$

#### **River PP**

Location: **river.water.sed.phos**

Unit: kg

#### **PP mobilization factor**

Location: **soil.plab.pp_fact**

Unit: kg km⁻² day⁻¹

Value:

$$
\left(\left(\mathrm{plabconc}+\mathrm{inactive\_soil\_p\_conc}\right)\cdot \mathrm{e\_fact}\cdot \mathrm{pp\_enrich}\rightarrow \mathrm{kg}\,\mathrm{km}^{-2}\,\mathrm{day}^{-1}\,\right)
$$

#### **River TP**

Location: **river.water.tp**

Unit: mg l⁻¹

Value:

$$
\mathrm{conc}\left(\mathrm{phos}\right)+\mathrm{conc}\left(\mathrm{sed}\right)\cdot \mathrm{conc}\left(\mathrm{sed}.\mathrm{phos}\right)
$$

#### **River TDP**

Location: **river.water.tdp**

Unit: mg l⁻¹

Value:

$$
\mathrm{conc}\left(\mathrm{phos}\right)
$$

### Fluxes

#### **River effluent DIP**

Source: out

Target: river.water.phos

Unit: kg day⁻¹

Value:

$$
\mathrm{eff\_tdp}
$$

#### **PP mobilization**

Source: out

Target: river.water.sed.phos

Unit: kg day⁻¹

Value:

$$
\mathrm{a\_catch}\cdot \mathrm{river}.\mathrm{e\_fact}\cdot \mathrm{aggregate}\left(\mathrm{soil}.\mathrm{plab}.\mathrm{pp\_fact}\right)
$$



{% include lib/mathjax.html %}
