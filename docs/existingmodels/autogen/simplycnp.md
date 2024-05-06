---
layout: default
title: SimplyCNP
parent: Mathematical description
grand_parent: Existing models
nav_order: 0
---

# SimplyCNP

This is auto-generated documentation based on the model code in [models/simplycnp_model.txt](https://github.com/NIVANorge/Mobius2/blob/main/models/simplycnp_model.txt) .
Since the modules can be dynamically loaded with different arguments, this documentation does not necessarily reflect all use cases of the modules.

See the note on [notation](autogen.html#notation).

The file was generated at 2024-05-06 13:40:56.

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
|  | **runoff_target** | loc |
| River | **river** | compartment |
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

---

## SimplyC land

Version: 1.0.1

File: [modules/simplyc.txt](https://github.com/NIVANorge/Mobius2/tree/main/models/modules/simplyc.txt)

### Description

This is a simple dissolved organic carbon (DOC) model that has as its main assumption that temperature and SO4 deposition are the strongest drivers for soil water DOC concentration.

The main purpose of the module is to predict DOC transport from land to river. The module does *not* keep track of the soil organic carbon pool as a whole, and so long-term changes in soil carbon availability are not taken into account, neither are effects from vegetation disturbance.

The user can configure the soil DOC concentration to either be constant, at a (temperature- and SO4-dependent) equilibrium, or always tending toward that equilibrium with a speed set by the `cdoc` parameter. In the latter case, influx of clean water (precipitation or snow melt) will dilute the soil water DOC concentration for a while before it again reaches equilibrium.

The ground water DOC concentration can be set to either be constant, equal to the average of the soil water DOC concentration, or follow mass balance (transport with recharge and runoff). In the latter case, the groundwater DOC decays with a user-set half life.

Authors: Magnus D. Norling, Leah A. Jackson-Blake

### External symbols

| Name | Symbol | Type |
| ---- | ------ | ---- |
| Atmosphere | **air** | compartment |
| Soil | **soil** | compartment |
| Groundwater | **gw** | compartment |
| Water | **water** | quantity |
| Organic carbon | **oc** | quantity |
| Temperature | **temp** | property |

### Parameters

| Name | Symbol | Unit |  Description |
| ---- | ------ | ---- |  ----------- |
| **DOC general** | | | |
| Soil temperature DOC creation linear coefficient | **kt1** | °C⁻¹ |  |
| Soil temperature DOC creation second-order coefficient | **kt2** | °C⁻² |  |
| Soil DOC linear SO4 dependence | **kso4** | l mg⁻¹ |  |
| Baseline soil DOC dissolution rate | **cdoc** | mg l⁻¹ day⁻¹ | Only used if the soil DOC computation type is dynamic. |
| Soil DOC computation type | **soildoc_type** |  |  |
| Groundwater DOC computation type | **gwdoc_type** |  |  |
| **DOC land** | | | |
| Baseline soil DOC concentration | **basedoc** | mg l⁻¹ | Soil water equilibrium DOC concentration when temperature is 0°C and there is no SO4. |
| **DOC deep soil** | | | |
| Groundwater DOC half-life | **gwdochl** | day | Half life of decay rate if groundwater DOC follows mass balance. |
| Groundwater DOC concentration | **gwdocconc** | mg l⁻¹ | Concentration if groundwater DOC is set to be constant. |

### State variables

#### **SO4 deposition**

Location: **air.so4**

Unit: mg l⁻¹

This series is externally defined. It may be an input series.

#### **Soil water DOC**

Location: **soil.water.oc**

Unit: kg km⁻²

Conc. unit: mg l⁻¹

Value:

$$
\begin{cases}\mathrm{basedoc} & \text{if}\;\mathrm{soildoc\_type}.\mathrm{const} \\ \mathrm{basedoc}\cdot \left(1+\left(\mathrm{kt1}+\mathrm{kt2}\cdot \mathrm{temp}\right)\cdot \mathrm{temp}-\mathrm{kso4}\cdot \mathrm{air}.\mathrm{so4}\right) & \text{if}\;\mathrm{soildoc\_type}.\mathrm{equilibrium} \\ \text{(mass balance)} & \text{otherwise}\end{cases}
$$

Initial value:

$$
\mathrm{basedoc}
$$

#### **Deep soil DOC**

Location: **gw.water.oc**

Unit: kg km⁻²

Conc. unit: mg l⁻¹

Value:

$$
\begin{cases}\mathrm{gwdocconc} & \text{if}\;\mathrm{gwdoc\_type}.\mathrm{const} \\ \mathrm{aggregate}\left(\mathrm{conc}\left(\mathrm{soil}.\mathrm{water}.\mathrm{oc}\right)\right) & \text{if}\;\mathrm{gwdoc\_type}.\mathrm{soil\_avg} \\ \text{(mass balance)} & \text{otherwise}\end{cases}
$$

Initial value:

$$
\begin{cases}\mathrm{gwdocconc} & \text{if}\;\mathrm{gwdoc\_type}.\mathrm{const}\;\text{or}\;\mathrm{gwdoc\_type}.\mathrm{mass\_bal} \\ \mathrm{aggregate}\left(\mathrm{conc}\left(\mathrm{soil}.\mathrm{water}.\mathrm{oc}\right)\right) & \text{otherwise}\end{cases}
$$

### Fluxes

#### **Soil DOC production**

Source: out

Target: soil.water.oc

Unit: kg km⁻² day⁻¹

Value:

$$
\mathrm{max}\left(0,\, \mathrm{water}\cdot \mathrm{cdoc}\cdot \left(1+\left(\mathrm{kt1}+\mathrm{kt2}\cdot \mathrm{temp}\right)\cdot \mathrm{temp}-\mathrm{kso4}\cdot \mathrm{air}.\mathrm{so4}\right)\right)
$$

#### **Soil DOC mineralization+resorption**

Source: soil.water.oc

Target: out

Unit: kg km⁻² day⁻¹

Value:

$$
\mathrm{oc}\cdot \frac{\mathrm{cdoc}}{\mathrm{basedoc}}
$$

#### **Deep soil DOC mineralization**

Source: gw.water.oc

Target: out

Unit: kg km⁻² day⁻¹

Value:

$$
\mathrm{rate} = \href{stdlib.html#response}{\mathrm{hl\_to\_rate}}\left(\mathrm{gwdochl}\right) \\ \mathrm{oc}\cdot \mathrm{rate}
$$

---

## SimplyC river

Version: 0.0.1

File: [modules/simplyc.txt](https://github.com/NIVANorge/Mobius2/tree/main/models/modules/simplyc.txt)

### Description

River processes for DOC.

Authors: Magnus D. Norling

### External symbols

| Name | Symbol | Type |
| ---- | ------ | ---- |
| River | **river** | compartment |
| Groundwater | **gw** | compartment |
| Water | **water** | quantity |
| Organic carbon | **oc** | quantity |
| Temperature | **temp** | property |

### Parameters

| Name | Symbol | Unit |  Description |
| ---- | ------ | ---- |  ----------- |
| **DOC river** | | | |
| River DOC loss rate at 20°C | **r_loss** | day⁻¹ |  |
| River DOC loss Q10 | **r_q10** |  |  |

### State variables

#### **River water DOC**

Location: **river.water.oc**

Unit: kg

Conc. unit: mg l⁻¹

Initial value:

$$
\mathrm{conc}\left(\mathrm{gw}.\mathrm{water}.\mathrm{oc}\right)
$$

### Fluxes

#### **River DOC loss**

Source: river.water.oc

Target: out

Unit: kg day⁻¹

Value:

$$
\mathrm{rate} = \href{stdlib.html#response}{\mathrm{q10\_adjust}}\left(\mathrm{r\_loss},\, 20 \mathrm{°C}\,,\, \mathrm{temp},\, \mathrm{r\_q10}\right) \\ \mathrm{oc}\cdot \mathrm{rate}
$$

---

## SimplyN

Version: 0.0.4

File: [modules/simplyn.txt](https://github.com/NIVANorge/Mobius2/tree/main/models/modules/simplyn.txt)

### Description

This is a simple dissolved inorganic nitrogen (DIN) model. The main assumption in the model is that there is a semi-constant input of DIN to the soil water from deposition and fixation, while loss (plant uptake + denitrification) is temperature-dependent. The latter two are bundled in one single process.

In addition fertilizer can be added at a single day per year. The fertilizer N is added as solid, and dissolves proportionally to the amount of new water (precipitation + snow melt) entering the soil.

Authors: Leah A. Jackson-Blake, Magnus D. Norling

### External symbols

| Name | Symbol | Type |
| ---- | ------ | ---- |
| Soil | **soil** | compartment |
| Groundwater | **gw** | compartment |
| River | **river** | compartment |
| Water | **water** | quantity |
| Inorganic nitrogen | **din** | quantity |
| Undissolved fertilizer nitrogen | **sn** | quantity |
| Temperature | **temp** | property |
| Total nitrogen | **tn** | property |

### Parameters

| Name | Symbol | Unit |  Description |
| ---- | ------ | ---- |  ----------- |
| **DIN universal params** | | | |
| Soilwater DIN uptake rate at 20°C | **din_immob_rate** | m day⁻¹ | (name is outdated, should be changed). This represents uptake and denitrification. |
| Soilwater DIN uptake rate response to 10°C change (Q10) | **din_immob_q10** |  |  |
| Groundwater DIN computation type | **gw_conc_type** |  |  |
| Groundwater DIN concentration | **gw_din_conc** | mg l⁻¹ | Only used if type is const |
| Reach denitrification rate at 20°C | **reach_denit_rate** | day⁻¹ |  |
| (Q10) Reach denitrification rate response to 10°C change in temperature | **reach_denit_q10** |  |  |
| **Soil DIN params varying by land use** | | | |
| Initial soilwater DIN concentration | **sw_din_init** | mg l⁻¹ |  |
| Net annual DIN input to soil | **net_annual_N_input** | kg ha⁻¹ year⁻¹ | (name outdated, should be changed) These are the gross DIN inputs to soil disregarding fertilizer inputs. Represents atmospheric deposition and fixation. |
| Fertilizer addition day | **fert_day** | day |  |
| Fertilizer N | **fert_n** | kg ha⁻¹ |  |
| Fertilizer DIN release | **fert_rel** | mm⁻¹ | Per mm of soil water input (giving less dissolution in dry years) |
| **River DIN** | | | |
| Reach effluent DIN inputs | **eff_din** | kg day⁻¹ |  |

### State variables

#### **Soil undissolved fertilizer N**

Location: **soil.sn**

Unit: kg km⁻²

#### **Soil water DIN**

Location: **soil.water.din**

Unit: kg km⁻²

Conc. unit: mg l⁻¹

Initial value:

$$
\mathrm{sw\_din\_init}
$$

#### **Groundwater DIN**

Location: **gw.water.din**

Unit: kg km⁻²

Conc. unit: mg l⁻¹

Value:

$$
\begin{cases}\mathrm{gw\_din\_conc} & \text{if}\;\mathrm{gw\_conc\_type}.\mathrm{const} \\ \mathrm{aggregate}\left(\mathrm{conc}\left(\mathrm{soil}.\mathrm{water}.\mathrm{din}\right)\right) & \text{if}\;\mathrm{gw\_conc\_type}.\mathrm{soil\_avg} \\ \text{(mass balance)} & \text{otherwise}\end{cases}
$$

Initial value:

$$
\mathrm{gw\_din\_conc}
$$

#### **River water DIN**

Location: **river.water.din**

Unit: kg

Conc. unit: mg l⁻¹

Initial value:

$$
\mathrm{conc}\left(\mathrm{gw}.\mathrm{water}.\mathrm{din}\right)
$$

#### **River TN**

Location: **river.water.tn**

Unit: mg l⁻¹

Value:

$$
\mathrm{conc}\left(\mathrm{din}\right)
$$

### Fluxes

#### **Fertilizer N addition**

Source: out

Target: soil.sn

Unit: kg km⁻² day⁻¹

Value:

$$
\begin{cases}\left(\mathrm{fert\_n}\cdot 1 \mathrm{day}^{-1}\,\rightarrow \mathrm{kg}\,\mathrm{km}^{-2}\,\mathrm{day}^{-1}\,\right) & \text{if}\;\mathrm{time}.\mathrm{day\_of\_year}=\mathrm{fert\_day} \\ 0 & \text{otherwise}\end{cases}
$$

#### **Fertilizer DIN release**

Source: soil.sn

Target: soil.water.din

Unit: kg km⁻² day⁻¹

Value:

$$
\left(\mathrm{soil}.\mathrm{sn}\cdot \mathrm{fert\_rel}\cdot \mathrm{in\_flux}\left(\mathrm{soil}.\mathrm{water}\right)\rightarrow \mathrm{kg}\,\mathrm{km}^{-2}\,\mathrm{day}^{-1}\,\right)
$$

#### **Non-agricultural soil water DIN addition**

Source: out

Target: soil.water.din

Unit: kg km⁻² day⁻¹

Value:

$$
\left(\frac{\mathrm{net\_annual\_N\_input}}{\mathrm{time}.\mathrm{days\_this\_year}}\rightarrow \mathrm{kg}\,\mathrm{km}^{-2}\,\mathrm{day}^{-1}\,\right)
$$

#### **Soil water DIN uptake**

Source: soil.water.din

Target: out

Unit: kg km⁻² day⁻¹

Value:

$$
\mathrm{rate} = \href{stdlib.html#response}{\mathrm{q10\_adjust}}\left(\mathrm{din\_immob\_rate},\, 20 \mathrm{°C}\,,\, \mathrm{temp},\, \mathrm{din\_immob\_q10}\right) \\ \left(\mathrm{conc}\left(\mathrm{din}\right)\cdot \mathrm{rate}\rightarrow \mathrm{kg}\,\mathrm{km}^{-2}\,\mathrm{day}^{-1}\,\right)
$$

#### **River effluent DIN**

Source: out

Target: river.water.din

Unit: kg day⁻¹

Value:

$$
\mathrm{eff\_din}
$$

#### **River DIN denitrification loss**

Source: river.water.din

Target: out

Unit: kg day⁻¹

Value:

$$
\mathrm{rate} = \href{stdlib.html#response}{\mathrm{q10\_adjust}}\left(\mathrm{reach\_denit\_rate},\, 20 \mathrm{°C}\,,\, \mathrm{temp},\, \mathrm{reach\_denit\_q10}\right) \\ \mathrm{din}\cdot \mathrm{rate}
$$

---

## SimplyP

Version: 0.6.0

File: [modules/simplyp.txt](https://github.com/NIVANorge/Mobius2/tree/main/models/modules/simplyp.txt)

### Description

SimplyP is a parsimonious phosphorus model. SimplyP models total dissolved phosphorous (TDP) in the soil solution using a equilibrium phosphate concentration at net zero sorption (EPC0) constant. The soil water TDP concentration tends to EPC0 with a speed dependent on a phosphorous sorption coefficient. The non-dissolved phosphorous is tracked as labile phosporous.

If dynamic EPC0 is turned on, the EPC0 will change slowly over time depending on the total amount of labile phosphorous.

For news, updates and references, see [the model's github home page](https://nivanorge.github.io/Mobius2/existingmodels/simply.html)

Technical implementation: The soil TDP mass is described by the ODE equation

$$
d(TDPs)/dt  = input - kf\cdot m\_soil\cdot (TDPs/water - epc0) - flow\cdot TDPs/water
$$

This equation is generally stiff (hence computationally difficult to solve). However, if we assume that flow (soil water flow) and water are approximately constant over the time step, we have an equation on the form

$$
d(TDPs)/dt  = (input + kf\cdot m\_soil\cdot epc0)  -  ((kf\cdot m\_soil + flow) / water)\cdot TDPs = a - b\cdot TDPs
$$

This has the exact solution

$$
TDPs(t) = a/b + (TDPs(0) - a/b) \cdot exp(-b\cdot t),
$$

where we can insert t=1 to integrate over the time step.
Solving it this way saves time by a factor of about 50-100, and has miniscule error compared to solving it with time-variable water and flow.
	
Now, the soil labile P mass is described by

$$
d(Plab)/dt  = kf\cdot m\_soil\cdot ((TDPs/water)-epc0)
$$

So

$$
Plab(1) = Plab(0) + \int_0^1 kf\cdot m\_soil\cdot ((TDPs(t)/water) - epc0) \mathrm{d}t
$$

Again, assuming constant water, the integral will be

$$
I = (kf\cdot m\_soil)\cdot ( (1/water)\cdot \int_0^1 TDPs(t)\mathrm{d}t - EPC0) \\
= (kf\cdot m\_soil)\cdot ( (1/water)(a/b + (TDPs(0)-a/b)\cdot (1/b)\cdot (1 - exp(-b)) ) - EPC0) \\
= (kf\cdot m\_soil)\cdot ( (1/(water\cdot b))(a + (TDPs(0) - a/b)(1 - exp(-b)) ) - EPC0)
$$

SimplyP was originally implemented in Python and published as

Jackson-Blake LA, Sample JE, Wade AJ, Helliwell RC, Skeffington RA. 2017. Are our dynamic water quality models too complex? A comparison of a new parsimonious phosphorus model, SimplyP, and INCA-P. Water Resources Research, 53, 5382–5399. [https://doi.org/10.1002/2016WR020132](https://doi.org/10.1002/2016WR020132)

New to version 0.6:
- The model has been ported to Mobius2. Everything is solved as one large coupled ODE system, so transport between land and river and between different river sections is more precise.

New to version 0.4:
- Landscape units are dynamic and user-specified instead of hardcoded.
- Sediment and hydrology equations are factored out into separate modules (SimplyQ, SimplySed)

New to version 0.3:
- More realistic hydrology.

For reference, here is [the original Python implementation of SimplyP](https://github.com/LeahJB/SimplyP), which is no longer being developed.

Authors: Leah A. Jackson-Blake, Magnus D. Norling

### External symbols

| Name | Symbol | Type |
| ---- | ------ | ---- |
| Soil | **soil** | compartment |
| Groundwater | **gw** | compartment |
| River | **river** | compartment |
| Water | **water** | quantity |
| Inorganic phosphorous | **phos** | quantity |
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
\begin{cases}\begin{pmatrix}\mathrm{q} = \left(\mathrm{last}\left(\mathrm{out\_flux}\left(\mathrm{soil}.\mathrm{water}\right)\right)\rightarrow \mathrm{mm}\,\mathrm{day}^{-1}\,\right) \\ \mathrm{days} = \left(\mathrm{time}.\mathrm{step\_length\_in\_seconds}\rightarrow \mathrm{day}\,\right) \\ \mathrm{pin} = \left(\mathrm{p\_input}\cdot \frac{\mathrm{days}}{\mathrm{time}.\mathrm{days\_this\_year}}\rightarrow \mathrm{kg}\,\mathrm{km}^{-2}\,\right) \\ \mathrm{m\_soil} = \left(\mathrm{m\_soil\_m2}\rightarrow \mathrm{kg}\,\mathrm{km}^{-2}\,\right) \\ \mathrm{a} = \mathrm{pin}+\mathrm{kf}\cdot \mathrm{m\_soil}\cdot \mathrm{epc0} \\ \mathrm{bV} = \mathrm{kf}\cdot \mathrm{m\_soil}+\mathrm{q}\cdot \mathrm{days} \\ \mathrm{b} = \frac{\mathrm{bV}}{\mathrm{last}\left(\mathrm{water}\right)} \\ \frac{\mathrm{a}}{\mathrm{b}}+\left(\mathrm{last}\left(\mathrm{water}.\mathrm{phos}\right)-\frac{\mathrm{a}}{\mathrm{b}}\right)\cdot e^{-\mathrm{b}}\end{pmatrix} & \text{if}\;\mathrm{dyn\_epc0} \\ \left(\mathrm{init\_epc0}\cdot \mathrm{water}\rightarrow \mathrm{kg}\,\mathrm{km}^{-2}\,\right) & \text{otherwise}\end{cases}
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
\begin{cases}\begin{pmatrix}\mathrm{q} = \left(\mathrm{last}\left(\mathrm{out\_flux}\left(\mathrm{soil}.\mathrm{water}\right)\right)\rightarrow \mathrm{mm}\,\mathrm{day}^{-1}\,\right) \\ \mathrm{days} = \left(\mathrm{time}.\mathrm{step\_length\_in\_seconds}\rightarrow \mathrm{day}\,\right) \\ \mathrm{pin} = \left(\mathrm{p\_input}\cdot \frac{\mathrm{days}}{\mathrm{time}.\mathrm{days\_this\_year}}\rightarrow \mathrm{kg}\,\mathrm{km}^{-2}\,\right) \\ \mathrm{m\_soil} = \left(\mathrm{m\_soil\_m2}\rightarrow \mathrm{kg}\,\mathrm{km}^{-2}\,\right) \\ \mathrm{a} = \mathrm{pin}+\mathrm{kf}\cdot \mathrm{m\_soil}\cdot \mathrm{epc0} \\ \mathrm{bV} = \mathrm{kf}\cdot \mathrm{m\_soil}+\mathrm{q}\cdot \mathrm{days} \\ \mathrm{b} = \frac{\mathrm{bV}}{\mathrm{last}\left(\mathrm{water}\right)} \\ \mathrm{sorp} = \mathrm{kf}\cdot \mathrm{m\_soil}\cdot \left(\frac{1}{\mathrm{bV}}\cdot \left(\mathrm{a}+\left(\mathrm{last}\left(\mathrm{water}.\mathrm{phos}\right)-\frac{\mathrm{a}}{\mathrm{b}}\right)\cdot \left(1-e^{-\mathrm{b}}\right)\right)-\mathrm{epc0}\right) \\ \mathrm{last}\left(\mathrm{plab}\right)+\mathrm{sorp}\end{pmatrix} & \text{if}\;\mathrm{dyn\_epc0} \\ \mathrm{last}\left(\mathrm{plab}\right) & \text{otherwise}\end{cases}
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

---

## SimplySed

Version: 0.6.0

File: [modules/simplysed.txt](https://github.com/NIVANorge/Mobius2/tree/main/models/modules/simplysed.txt)

### Description

This is a simple sediment transport module created as a part of SimplyP.

Erosion is computed as a product of a land erosion factor and a river erosion factor.

The land erosion factor depends on the land slope and the vegetation cover factor. The vegetation cover factor can either be be flat, or can have peaks in spring and autumn (with a user-determined proportion of the size of these peaks), representing plowing.

The erosion factor in the river follows a $$(aQ)^b$$ - type relationship, where Q is the total runoff from the catchment to the river.

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

Authors: Leah A. Jackson-Blake, Magnus D. Norling

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

**cover_shape(doy, doy_max, len, c_cov, shp_step, shp_tri, shp_smooth)** = 

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



{% include lib/mathjax.html %}

