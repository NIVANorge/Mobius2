---
layout: default
title: NIVAFjord
parent: Mathematical description
grand_parent: Existing models
nav_order: 2
---

# NIVAFjord

This is auto-generated documentation based on the model code in [models/nivafjord_simplycnp_model.txt](https://github.com/NIVANorge/Mobius2/blob/main/models/nivafjord_simplycnp_model.txt) .
Since the modules can be dynamically loaded with different arguments, this documentation does not necessarily reflect all use cases of the modules.

See the note on [notation](autogen.html#notation).

The file was generated at 2025-11-14 14:18:15.

---

## NIVAFjord dimensions

Version: 0.0.0

File: [modules/nivafjord/basin.txt](https://github.com/NIVANorge/Mobius2/tree/main/models/modules/nivafjord/basin.txt)

### External symbols

| Name | Symbol | Type |
| ---- | ------ | ---- |
| Fjord layer | **layer** | compartment |
| All layers | **all_layer** | compartment |

### Parameters

| Name | Symbol | Unit |  Description |
| ---- | ------ | ---- |  ----------- |
| **Layer thickness** | | | Distributes like: `all_layer` |
| Stable layer thickness | **dz0** | m |  |
| **Layer area** | | | Distributes like: `layer` |
| Layer area | **A** | m² |  |

---

## NIVAFjord basin

Version: 0.0.4

File: [modules/nivafjord/basin.txt](https://github.com/NIVANorge/Mobius2/tree/main/models/modules/nivafjord/basin.txt)

### Description

This is the physics component for a single basin of the NIVAFjord model.

The model is loosely based on the model [Eutrofimodell for Indre Oslofjord (Norwegian only)](https://niva.brage.unit.no/niva-xmlui/handle/11250/207887).

Each basin is divided into a set of (assumed horizontally mixed) layers of potentially varying thickness.

Turbulent vertical mixing is computed assuming constant turbulent energy (per layer), which implies a fixed dependency of the mixing rate on the [Brunt-Väisälä frequency](https://en.wikipedia.org/wiki/Brunt%E2%80%93V%C3%A4is%C3%A4l%C3%A4_frequency) proportional to

$$
r_0(N/N_0)^\alpha
$$

where $$N$$ is the Brunt-Väisälä frequency, $$N_0$$ a reference frequency, $$r_0$$ the mixing rate at the reference frequency and $$\alpha \sim 1.4$$ an exponential dependency. The reference rate $$r_0$$ can be set separately per layer.

Wind-driven mixing and mixing from ship traffic is added on top.

The basin model is also similar to MyLake.

MyLake—A multi-year lake simulation model code suitable for uncertainty and sensitivity analysis simulations, Tuomo M. Saloranta and Tom Andersen 2007, Ecological Modelling 207(1), 45-60, [https://doi.org/10.1016/j.ecolmodel.2007.03.018](https://doi.org/10.1016/j.ecolmodel.2007.03.018)

Authors: Magnus D. Norling

### External symbols

| Name | Symbol | Type |
| ---- | ------ | ---- |
| Atmosphere | **air** | compartment |
| Cosine of the solar zenith angle | **cos_z** | property |
|  | **sw_sed** | loc |
| Depth | **z** | property |
| Density | **rho** | property |
| Basin | **basin** | compartment |
| Fjord layer | **layer** | compartment |
| Salinity | **salinity** | property |
| Water | **water** | quantity |
| Ice formation temperature | **freeze_temp** | property |
| Salt | **salt** | quantity |
| Heat energy | **heat** | quantity |
| Wind speed | **wind** | property |
| Temperature | **temp** | property |
| Pressure | **pressure** | property |
| Global radiation | **g_rad** | property |
| Attenuation | **attn** | property |
| Thickness | **dz** | property |
| Sea level | **h** | property |
| Area | **area** | property |
| Shortwave radiation | **sw** | property |
|  | **ice_ind** | loc |
| Fjord vertical | **vert** | connection |
| Shortwave vertical | **sw_vert** | connection |

### Constants

| Name | Symbol | Unit | Value |
| ---- | ------ | ---- | ----- |
| Sediment albedo | **sed_alb** |  | 0.3 |

### Parameters

| Name | Symbol | Unit |  Description |
| ---- | ------ | ---- |  ----------- |
| **Mixing parameters** | | | Distributes like: `basin` |
| Brunt-Väisälä frequency reference | **N0** | s⁻¹ |  |
| Minimum B-V frequency | **Nmin** | s⁻¹ |  |
| Mixing non-linear coefficient | **alpha** |  |  |
| Mixing factor reduction under ice | **r_ice** |  | Mostly relevant for lakes, where it should be around 0.12 |
| Wind turbulent energy transfer coefficient | **m0** |  | Between 0.2 and 0.3 in open ocean, higher in confined areas |
| Surface additional mixing energy | **Es0** | m² s⁻³ | From boat activity |
| Halving depth of additional mixing energy | **zshalf** | m |  |
| Diminishing rate of additional mixing energy | **hsfact** | m |  |
| **Layer specific mixing** | | | Distributes like: `layer` |
| Mixing factor reference | **K0** | m² s⁻¹ | Mixing factor when the B-V frequency is equal to the reference |
| **Initial layer physical** | | | Distributes like: `layer` |
| Initial layer temperature | **init_t** | °C |  |
| Initial layer salinity | **init_s** |  |  |

### State variables

#### **Air temperature**

Location: **air.temp**

Unit: °C

This series is externally defined. It may be an input series.

#### **Wind speed**

Location: **air.wind**

Unit: m s⁻¹

This series is externally defined. It may be an input series.

#### **Global radiation**

Location: **air.g_rad**

Unit: W m⁻²

This series is externally defined. It may be an input series.

#### **Ice formation temperature**

Location: **basin.freeze_temp**

Unit: °C

Value:

$$
\href{stdlib.html#seawater}{\mathrm{ice\_formation\_temperature}}\left(\mathrm{layer}.\mathrm{water}.\mathrm{salinity}\lbrack\mathrm{vert}.\mathrm{top}\rbrack\right)
$$

#### **Layer thickness**

Location: **layer.dz**

Unit: m

Value:

$$
\mathrm{Aavg} = 0.5\cdot \left(\mathrm{A}+\mathrm{A}\lbrack\mathrm{vert}.\mathrm{below}\rbrack\right) \\ \frac{\mathrm{water}}{\mathrm{Aavg}}
$$

Initial value:

$$
\mathrm{dz0}
$$

#### **Depth (to bottom of layer)**

Location: **layer.z**

Unit: m

Value:

$$
\mathrm{z}\lbrack\mathrm{vert}.\mathrm{above}\rbrack+\mathrm{dz}
$$

#### **Basin sea level**

Location: **basin.h**

Unit: m

Value:

$$
\mathrm{aggregate}\left(\mathrm{layer}.\mathrm{dz}\right)-\mathrm{aggregate}\left(\mathrm{dz0}\right)
$$

#### **Basin area**

Location: **basin.area**

Unit: m²

Value:

$$
\mathrm{A}\lbrack\mathrm{vert}.\mathrm{top}\rbrack
$$

#### **Layer water**

Location: **layer.water**

Unit: m³

Initial value:

$$
\mathrm{Aavg} = 0.5\cdot \left(\mathrm{A}+\mathrm{A}\lbrack\mathrm{vert}.\mathrm{below}\rbrack\right) \\ \mathrm{Aavg}\cdot \mathrm{dz0}
$$

#### **Layer salt**

Location: **layer.water.salt**

Unit: kg

Conc. unit: kg m⁻³

Initial value (concentration):

$$
\mathrm{salinity}\cdot 0.001\cdot \mathrm{rho\_water}
$$

#### **Layer thermal energy**

Location: **layer.water.heat**

Unit: J

Initial value (concentration):

$$
\mathrm{C\_water}\cdot \left(\mathrm{temp}\rightarrow \mathrm{K}\,\right)\cdot \mathrm{rho\_water}
$$

#### **Layer temperature**

Location: **layer.water.temp**

Unit: °C

Value:

$$
\left(\frac{\mathrm{heat}}{\mathrm{water}\cdot \mathrm{C\_water}\cdot \mathrm{rho\_water}}\rightarrow \mathrm{°C}\,\right)
$$

Initial value:

$$
\mathrm{init\_t}
$$

#### **Layer salinity**

Location: **layer.water.salinity**

Unit: 

Value:

$$
1000\cdot \frac{\mathrm{conc}\left(\mathrm{salt}\right)}{\mathrm{rho\_water}}
$$

Initial value:

$$
\mathrm{init\_s}
$$

#### **Potential density**

Location: **layer.water.rho**

Unit: kg m⁻³

Value:

$$
\href{stdlib.html#seawater}{\mathrm{seawater\_pot\_dens}}\left(\mathrm{temp},\, \mathrm{salinity}\right)
$$

#### **Pressure**

Location: **layer.water.pressure**

Unit: Pa

Value:

$$
\mathrm{pressure}\lbrack\mathrm{vert}.\mathrm{above}\rbrack+\mathrm{rho}\cdot \mathrm{grav}\cdot \mathrm{dz}
$$

#### **d(dens)**

Location: **layer.water.ddens**

Unit: kg m⁻³

Value:

$$
\mathrm{rho}\lbrack\mathrm{vert}.\mathrm{below}\rbrack-\mathrm{rho}
$$

#### **Brunt-Väisälä frequency**

Location: **layer.water.Nfreq**

Unit: s⁻¹

Value:

$$
\mathrm{mdz} = 0.5\cdot \left(\mathrm{dz}+\mathrm{dz}\lbrack\mathrm{vert}.\mathrm{below}\rbrack\right) \\ \mathrm{N2} = \mathrm{grav}\cdot \frac{\mathrm{ddens}}{\mathrm{mdz}\cdot \mathrm{rho}} \\ \sqrt{\mathrm{max}\left(\mathrm{Nmin}^{2},\, \mathrm{N2}\right)}
$$

#### **Wind stress**

Location: **basin.stress**

Unit: N m⁻²

Value:

$$
\mathrm{u} = \mathrm{air}.\mathrm{wind} \\ \mathrm{c\_stress} = 0.001\cdot \left(0.8+0.9\cdot \frac{\mathrm{u}^{8}}{\mathrm{u}^{8}+10^{8} \mathrm{m}^{8}\,\mathrm{s}^{-8}\,}\right) \\ \mathrm{air}.\mathrm{rho}\cdot \mathrm{c\_stress}\cdot \mathrm{u}^{2}
$$

#### **Total wind mixing work**

Location: **basin.emix**

Unit: W m⁻²

Value:

$$
\mathrm{r} = \mathrm{layer}.\mathrm{water}.\mathrm{rho}\lbrack\mathrm{vert}.\mathrm{top}\rbrack \\ \;\text{not}\;\mathrm{ice\_ind}\cdot \mathrm{m0}\cdot \sqrt{\frac{\mathrm{stress}^{3}}{\mathrm{r}}}
$$

#### **Wind mixing**

Location: **layer.water.v_w**

Unit: m s⁻¹

Value:

$$
\mathrm{rem\_emix} = \mathrm{max}\left(0,\, \mathrm{basin}.\mathrm{emix}-\mathrm{summix}\right) \\ \mathrm{f} = \frac{\mathrm{grav}\cdot \left(\mathrm{rho}\lbrack\mathrm{vert}.\mathrm{below}\rbrack-\mathrm{rho}\right)\cdot \left(\mathrm{dz}\lbrack\mathrm{vert}.\mathrm{below}\rbrack+\mathrm{dz}\right)}{2} \\ \mathrm{v} = \frac{\mathrm{rem\_emix}}{\mathrm{f}} \\ \mathrm{max\_v} = \left(10 \mathrm{m}\,\mathrm{day}^{-1}\,\rightarrow \mathrm{m}\,\mathrm{s}^{-1}\,\right) \\ \mathrm{v2} = \begin{cases}\mathrm{v} & \text{if}\;\mathrm{is\_finite}\left(\mathrm{v}\right)\;\text{and}\;\mathrm{f}>0 \\ \mathrm{max\_v} & \text{if}\;\mathrm{rem\_emix}>0 \\ 0 \mathrm{m}\,\mathrm{s}^{-1}\, & \text{otherwise}\end{cases} \\ \mathrm{max}\left(0 \mathrm{m}\,\mathrm{s}^{-1}\,,\, \mathrm{min}\left(\mathrm{v2},\, \mathrm{max\_v}\right)\right)
$$

#### **Used wind mixing work in layer**

Location: **layer.water.emix**

Unit: W m⁻²

Value:

$$
\mathrm{f} = \frac{\mathrm{grav}\cdot \left(\mathrm{rho}\lbrack\mathrm{vert}.\mathrm{below}\rbrack-\mathrm{rho}\right)\cdot \left(\mathrm{dz}\lbrack\mathrm{vert}.\mathrm{below}\rbrack+\mathrm{dz}\right)}{2} \\ \begin{cases}\mathrm{v\_w}\cdot \mathrm{f} & \text{if}\;\mathrm{f}>0 \\ 0 & \text{otherwise}\end{cases}
$$

#### **Sum used wind mixing energy**

Location: **layer.water.summix**

Unit: W m⁻²

Value:

$$
\mathrm{emix}\lbrack\mathrm{vert}.\mathrm{above}\rbrack+\mathrm{summix}\lbrack\mathrm{vert}.\mathrm{above}\rbrack
$$

#### **Turbulent mixing**

Location: **layer.water.v_t**

Unit: m s⁻¹

Value:

$$
\mathrm{K} = \mathrm{K0}\cdot \left(\;\text{not}\;\mathrm{ice\_ind}+\mathrm{r\_ice}\cdot \mathrm{ice\_ind}\right) \\ \mathrm{dz\_} = 0.5\cdot \left(\mathrm{dz}+\mathrm{dz}\lbrack\mathrm{vert}.\mathrm{below}\rbrack\right) \\ \href{stdlib.html#basic}{\mathrm{safe\_divide}}\left(\mathrm{K},\, \mathrm{dz\_}\cdot \left(\frac{\mathrm{Nfreq}}{\mathrm{N0}}\right)^{\mathrm{alpha}}\right)
$$

#### **Additional mixing**

Location: **layer.water.v_s**

Unit: m s⁻¹

Value:

$$
\mathrm{dz\_} = 0.5\cdot \left(\mathrm{dz}+\mathrm{dz}\lbrack\mathrm{vert}.\mathrm{below}\rbrack\right) \\ \mathrm{eta} = e^{\frac{-\left(\mathrm{z}-\mathrm{zshalf}\right)}{\mathrm{hsfact}}} \\ \mathrm{Es} = \mathrm{Es0}\cdot \frac{\mathrm{eta}}{1+\mathrm{eta}} \\ \frac{\mathrm{Es}}{\mathrm{dz\_}\cdot \mathrm{Nfreq}^{2}}
$$

#### **Layer shortwave radiation**

Location: **layer.water.sw**

Unit: W m⁻²

Value:

$$
\left(\frac{\mathrm{in\_flux}\left(\mathrm{sw\_vert},\, \mathrm{layer}.\mathrm{water}.\mathrm{heat}\right)}{\mathrm{A}}\rightarrow \mathrm{W}\,\mathrm{m}^{-2}\,\right)
$$

#### **Attenuation fraction**

Location: **layer.water.att**

Unit: 

Value:

$$
\mathrm{cz} = \mathrm{max}\left(0.01,\, \href{stdlib.html#radiation}{\mathrm{refract}}\left(\mathrm{air}.\mathrm{cos\_z},\, \mathrm{refraction\_index\_water}\right)\right) \\ \mathrm{th} = \frac{\mathrm{dz}}{\mathrm{cz}} \\ 1-e^{-\mathrm{attn}\cdot \mathrm{th}}
$$

### Fluxes

#### **Layer mixing down**

Source: layer.water

Target: vert

Unit: m³ day⁻¹

This is a mixing flux (affects dissolved quantities only)

Value:

$$
\left(\left(\mathrm{v\_t}+\mathrm{v\_s}+\mathrm{v\_w}\right)\cdot \mathrm{A}\lbrack\mathrm{vert}.\mathrm{below}\rbrack\rightarrow \mathrm{m}^{3}\,\mathrm{day}^{-1}\,\right)
$$

#### **Shortwave shine-through**

Source: layer.water.heat

Target: sw_vert

Unit: J day⁻¹

Value:

$$
\left(\mathrm{in\_flux}\left(\mathrm{sw\_vert},\, \mathrm{layer}.\mathrm{water}.\mathrm{heat}\right)\rightarrow \mathrm{J}\,\mathrm{day}^{-1}\,\right)\cdot \left(1-\mathrm{att}\right)\cdot \frac{\mathrm{A}\lbrack\mathrm{vert}.\mathrm{below}\rbrack}{\mathrm{A}}
$$

#### **Shortwave to sediments**

Source: layer.water.heat

Target: sw_sed

Unit: J day⁻¹

Value:

$$
\left(\mathrm{in\_flux}\left(\mathrm{sw\_vert},\, \mathrm{layer}.\mathrm{water}.\mathrm{heat}\right)\rightarrow \mathrm{J}\,\mathrm{day}^{-1}\,\right)\cdot \left(1-\mathrm{att}\right)\cdot \left(1-\mathrm{sed\_alb}\right)\cdot \frac{\mathrm{A}-\mathrm{A}\lbrack\mathrm{vert}.\mathrm{below}\rbrack}{\mathrm{A}}
$$

---

## NIVAFjord chemistry rates

Version: 0.0.1

File: [modules/nivafjord/fjordchem.txt](https://github.com/NIVANorge/Mobius2/tree/main/models/modules/nivafjord/fjordchem.txt)

### Parameters

| Name | Symbol | Unit |  Description |
| ---- | ------ | ---- |  ----------- |
| **Oxygen** | | |  |
| Bubble formation coefficient | **bubble** | day⁻¹ |  |
| Oxicity threshold | **ox_th** | mg l⁻¹ |  |
| **Microbes** | | |  |
| Decomposition Q10 | **decompQ10** |  | Adjustment of rate with 10°C change in temperature |
| Maximal decomposition rate at 20°C | **maxdecomp_20** | day⁻¹ |  |
| Maximal denitrification rate at 20°C | **maxdenitr_20** | day⁻¹ |  |
| Molar O₂/C ratio in respiration | **O2toC** |  |  |
| N mineralization and nitrification rate modifier | **n_min** |  |  |
| P mineralization modifier | **p_min** |  |  |
| NO3 threshold for denitrification | **n_denit_th** | mg l⁻¹ |  |
| POM dissolution rate | **diss_r** | day⁻¹ |  |
| **P desorption / adsorption** | | |  |
| PO4 sorption coefficient | **k_sorp** | l mg⁻¹ |  |
| PO4 sorption rate | **r_sorp** | day⁻¹ |  |

---

## NIVAFjord chemistry

Version: 0.0.5

File: [modules/nivafjord/fjordchem.txt](https://github.com/NIVANorge/Mobius2/tree/main/models/modules/nivafjord/fjordchem.txt)

### Description

NIVAFjord module for O₂, dissolved nutrients and particles as well as microbial processes.

Authors: Magnus Dahler Norling, François Clayer

### External symbols

| Name | Symbol | Type |
| ---- | ------ | ---- |
| Atmosphere | **air** | compartment |
| CO₂ | **co2** | quantity |
| Cosine of the solar zenith angle | **cos_z** | property |
| Fjord phytoplankton | **phyt** | quantity |
| Depth | **z** | property |
| O₂ saturation concentration | **o2satconc** | property |
| Fjord layer | **layer** | compartment |
| CH₄ | **ch4** | quantity |
| Fjord sediment | **sediment** | compartment |
| Organic nitrogen | **on** | quantity |
| Water | **water** | quantity |
| O₂ | **o2** | quantity |
| Temperature | **temp** | property |
| Organic carbon | **oc** | quantity |
| Nitrate | **din** | quantity |
| Total nitrogen | **tn** | property |
| Phosphate | **phos** | quantity |
| Organic phosphorous | **op** | quantity |
| Sediments | **sed** | quantity |
| Total phosphorous | **tp** | property |
| Salinity | **salin** | property |
| Thickness | **dz** | property |
| Attenuation | **attn** | property |
| Area | **area** | property |
| Shortwave radiation | **sw** | property |
| O₂ saturation | **o2sat** | property |
| Fjord vertical | **vert** | connection |
| Compute DIC (CO₂, CH₄) | **compute_dic** | par_bool |

### Parameters

| Name | Symbol | Unit |  Description |
| ---- | ------ | ---- |  ----------- |
| **Initial chem** | | | Distributes like: `layer` |
| Initial O₂ saturation | **init_O2** |  |  |
| Initial DOC concentration | **init_DOC** | mg l⁻¹ |  |
| Initial DOM molar C/N rate | **init_CN** |  |  |
| Initial DOM molar C/P rate | **init_CP** |  |  |
| Initial NO3 concentration | **init_NO3** | mg l⁻¹ |  |
| Initial PO4 concentration | **init_PO4** | mg l⁻¹ |  |
| Initial suspended sediment concentration | **init_ss** | mg l⁻¹ |  |
| Initial particle organic carbon content | **init_foc** | g kg⁻¹ |  |
| **Light** | | |  |
| Diffuse attenuation coefficent (clear water) | **attn0** | m⁻¹ |  |
| Shading factor | **shade_f** | m⁻¹ l mg⁻¹ | Shading provided by suspended particles and DOM |
| **Suspended particles** | | |  |
| Settling velocity | **sett_vel** | m day⁻¹ |  |

### State variables

#### **Layer O₂**

Location: **layer.water.o2**

Unit: kg

Conc. unit: mg l⁻¹

Initial value (concentration):

$$
\left(\href{stdlib.html#sea-oxygen}{\mathrm{o2\_saturation\_concentration}}\left(\mathrm{temp},\, \mathrm{salin}\right)\cdot \mathrm{init\_O2}\cdot \mathrm{o2\_mol\_mass}\rightarrow \mathrm{kg}\,\right)
$$

#### **Layer DOC**

Location: **layer.water.oc**

Unit: kg

Conc. unit: mg l⁻¹

Initial value (concentration):

$$
\mathrm{init\_DOC}
$$

#### **Layer CO₂**

Location: **layer.water.co2**

Unit: kg

Conc. unit: mg l⁻¹

#### **Layer CH₄**

Location: **layer.water.ch4**

Unit: kg

Conc. unit: mg l⁻¹

#### **Layer DIN**

Location: **layer.water.din**

Unit: kg

Conc. unit: mg l⁻¹

Initial value (concentration):

$$
\mathrm{init\_NO3}
$$

#### **Layer DON**

Location: **layer.water.on**

Unit: kg

Conc. unit: mg l⁻¹

Initial value (concentration):

$$
\frac{\mathrm{init\_DOC}}{\href{stdlib.html#chemistry}{\mathrm{cn\_molar\_to\_mass\_ratio}}\left(\mathrm{init\_CN}\right)}
$$

#### **Layer DIP**

Location: **layer.water.phos**

Unit: kg

Conc. unit: mg l⁻¹

Initial value (concentration):

$$
\mathrm{init\_PO4}
$$

#### **Layer DOP**

Location: **layer.water.op**

Unit: kg

Conc. unit: mg l⁻¹

Initial value (concentration):

$$
\frac{\mathrm{init\_DOC}}{\href{stdlib.html#chemistry}{\mathrm{cp\_molar\_to\_mass\_ratio}}\left(\mathrm{init\_CP}\right)}
$$

#### **Layer suspended sediments**

Location: **layer.water.sed**

Unit: kg

Conc. unit: mg l⁻¹

Initial value (concentration):

$$
\mathrm{init\_ss}
$$

#### **Layer POC**

Location: **layer.water.sed.oc**

Unit: kg

Conc. unit: g kg⁻¹

Initial value (concentration):

$$
\mathrm{init\_foc}
$$

#### **Layer PIP**

Location: **layer.water.sed.phos**

Unit: kg

Conc. unit: g kg⁻¹

#### **Layer POP**

Location: **layer.water.sed.op**

Unit: kg

Conc. unit: g kg⁻¹

#### **Layer PON**

Location: **layer.water.sed.on**

Unit: kg

Conc. unit: g kg⁻¹

#### **Layer TN**

Location: **layer.water.tn**

Unit: mg l⁻¹

Value:

$$
\mathrm{conc}\left(\mathrm{din}\right)+\mathrm{conc}\left(\mathrm{on}\right)+\left(\mathrm{conc}\left(\mathrm{sed}\right)\cdot \mathrm{conc}\left(\mathrm{sed}.\mathrm{on}\right)\rightarrow \mathrm{mg}\,\mathrm{l}^{-1}\,\right)
$$

#### **Layer TP**

Location: **layer.water.tp**

Unit: mg l⁻¹

Value:

$$
\mathrm{conc}\left(\mathrm{phos}\right)+\mathrm{conc}\left(\mathrm{op}\right)+\left(\mathrm{conc}\left(\mathrm{sed}\right)\cdot \left(\mathrm{conc}\left(\mathrm{sed}.\mathrm{op}\right)+\mathrm{conc}\left(\mathrm{sed}.\mathrm{phos}\right)\right)\rightarrow \mathrm{mg}\,\mathrm{l}^{-1}\,\right)
$$

#### **O₂ saturation concentration**

Location: **layer.water.o2satconc**

Unit: mg l⁻¹

Value:

$$
\left(\href{stdlib.html#sea-oxygen}{\mathrm{o2\_saturation\_concentration}}\left(\mathrm{temp},\, \mathrm{salin}\right)\cdot \mathrm{o2\_mol\_mass}\rightarrow \mathrm{mg}\,\mathrm{l}^{-1}\,\right)
$$

#### **O₂ saturation**

Location: **layer.water.o2sat**

Unit: 

Value:

$$
\frac{\mathrm{conc}\left(\mathrm{layer}.\mathrm{water}.\mathrm{o2}\right)}{\mathrm{layer}.\mathrm{water}.\mathrm{o2satconc}}
$$

#### **Layer attenuation constant**

Location: **layer.water.attn**

Unit: m⁻¹

Value:

$$
\mathrm{attn0}+\left(\mathrm{conc}\left(\mathrm{sed}\right)+\mathrm{aggregate}\left(\mathrm{conc}\left(\mathrm{phyt}\right)\right)+\mathrm{conc}\left(\mathrm{oc}\right)\right)\cdot \mathrm{shade\_f}
$$

#### **Oxicity factor**

Location: **layer.water.ox_fac**

Unit: 

Value:

$$
\mathrm{wd} = 1 \\ \href{stdlib.html#response}{\mathrm{s\_response}}\left(\mathrm{conc}\left(\mathrm{o2}\right),\, \left(1-\mathrm{wd}\right)\cdot \mathrm{ox\_th},\, \left(1+\mathrm{wd}\right)\cdot \mathrm{ox\_th},\, 0,\, 1\right)
$$

#### **Temperature factor**

Location: **layer.water.temp_fac**

Unit: 

Value:

$$
\href{stdlib.html#response}{\mathrm{q10\_adjust}}\left(1,\, 20 \mathrm{°C}\,,\, \mathrm{temp},\, \mathrm{decompQ10}\right)
$$

#### **OM mineralization rate**

Location: **layer.water.min_rat**

Unit: day⁻¹

Value:

$$
\mathrm{ox\_fac}\cdot \mathrm{temp\_fac}\cdot \mathrm{maxdecomp\_20}
$$

#### **Denitrification rate**

Location: **layer.water.denit_rat**

Unit: day⁻¹

Value:

$$
\mathrm{wd} = 0.5 \\ \mathrm{n\_fac} = \href{stdlib.html#response}{\mathrm{s\_response}}\left(\frac{\mathrm{conc}\left(\mathrm{din}\right)}{\mathrm{n\_denit\_th}},\, 1-\mathrm{wd},\, 1+\mathrm{wd},\, 0,\, 1\right) \\ \left(1-\mathrm{ox\_fac}\right)\cdot \mathrm{temp\_fac}\cdot \mathrm{maxdenitr\_20}\cdot \mathrm{n\_fac}
$$

### Fluxes

#### **O₂ bubble formation**

Source: layer.water.o2

Target: out

Unit: kg day⁻¹

Value:

$$
\mathrm{pressure\_corr} = 1+0.476 \mathrm{m}^{-1}\,\cdot \mathrm{z}\lbrack\mathrm{vert}.\mathrm{above}\rbrack \\ \mathrm{satconc} = \mathrm{o2satconc}\cdot \mathrm{pressure\_corr} \\ \left(\mathrm{max}\left(0,\, \mathrm{bubble}\cdot \left(\mathrm{conc}\left(\mathrm{o2}\right)-\mathrm{satconc}\right)\cdot \mathrm{water}\right)\rightarrow \mathrm{kg}\,\mathrm{day}^{-1}\,\right)
$$

#### **Layer particle settling**

Source: layer.water.sed

Target: vert

Unit: kg day⁻¹

Value:

$$
\left(\mathrm{sett\_vel}\cdot \mathrm{conc}\left(\mathrm{sed}\right)\cdot \mathrm{A}\lbrack\mathrm{vert}.\mathrm{below}\rbrack\rightarrow \mathrm{kg}\,\mathrm{day}^{-1}\,\right)
$$

#### **Layer particle settling to bottom**

Source: layer.water.sed

Target: sediment.sed

Unit: kg day⁻¹

Value:

$$
\left(\mathrm{sett\_vel}\cdot \mathrm{conc}\left(\mathrm{sed}\right)\cdot \mathrm{sediment}.\mathrm{area}\rightarrow \mathrm{kg}\,\mathrm{day}^{-1}\,\right)
$$

#### **Layer POC dissolution**

Source: layer.water.sed.oc

Target: layer.water.oc

Unit: kg day⁻¹

Value:

$$
\mathrm{diss\_r}\cdot \mathrm{sed}.\mathrm{oc}
$$

#### **Layer PON dissolution**

Source: layer.water.sed.on

Target: layer.water.on

Unit: kg day⁻¹

Value:

$$
\mathrm{diss\_r}\cdot \mathrm{sed}.\mathrm{on}
$$

#### **DOC mineralization**

Source: layer.water.oc

Target: out

Unit: kg day⁻¹

Value:

$$
\mathrm{oc}\cdot \mathrm{min\_rat}
$$

#### **POC mineralization**

Source: layer.water.sed.oc

Target: out

Unit: kg day⁻¹

Value:

$$
\mathrm{sed}.\mathrm{oc}\cdot \mathrm{min\_rat}
$$

#### **DON mineralization and nitrification**

Source: layer.water.on

Target: layer.water.din

Unit: kg day⁻¹

Value:

$$
\mathrm{on}\cdot \mathrm{min\_rat}\cdot \mathrm{n\_min}
$$

#### **PON mineralization and nitrification**

Source: layer.water.sed.on

Target: layer.water.din

Unit: kg day⁻¹

Value:

$$
\mathrm{sed}.\mathrm{on}\cdot \mathrm{min\_rat}\cdot \mathrm{n\_min}
$$

#### **DOP mineralization**

Source: layer.water.op

Target: layer.water.phos

Unit: kg day⁻¹

Value:

$$
\mathrm{op}\cdot \mathrm{temp\_fac}\cdot \mathrm{maxdecomp\_20}\cdot \mathrm{p\_min}
$$

#### **POP mineralization**

Source: layer.water.sed.op

Target: layer.water.phos

Unit: kg day⁻¹

Value:

$$
\mathrm{sed}.\mathrm{op}\cdot \mathrm{temp\_fac}\cdot \mathrm{maxdecomp\_20}\cdot \mathrm{p\_min}
$$

#### **O₂ microbial consumption**

Source: layer.water.o2

Target: out

Unit: kg day⁻¹

Value:

$$
\left(\mathrm{oc}+\mathrm{sed}.\mathrm{oc}\right)\cdot \mathrm{min\_rat}\cdot \frac{\mathrm{o2\_mol\_mass}}{\mathrm{c\_mol\_mass}}\cdot \mathrm{O2toC}+\left(\mathrm{on}+\mathrm{sed}.\mathrm{on}\right)\cdot \mathrm{min\_rat}\cdot \mathrm{n\_min}\cdot \frac{\mathrm{o2\_mol\_mass}}{\mathrm{n\_mol\_mass}}\cdot 1.5
$$

#### **Microbial CO2 production**

Source: out

Target: layer.water.co2

Unit: kg day⁻¹

Value:

$$
\left(\mathrm{oc}+\mathrm{sed}.\mathrm{oc}\right)\cdot \left(\mathrm{min\_rat}+\mathrm{denit\_rat}\right)
$$

#### **DOC mineralization by denitrification**

Source: layer.water.oc

Target: out

Unit: kg day⁻¹

Value:

$$
\mathrm{water}.\mathrm{oc}\cdot \mathrm{denit\_rat}
$$

#### **POC mineralization by denitrification**

Source: layer.water.sed.oc

Target: out

Unit: kg day⁻¹

Value:

$$
\mathrm{water}.\mathrm{sed}.\mathrm{oc}\cdot \mathrm{denit\_rat}
$$

#### **NO3 denitrification consumption**

Source: layer.water.din

Target: out

Unit: kg day⁻¹

Value:

$$
\mathrm{bo2} = \left(\mathrm{water}.\mathrm{oc}+\mathrm{water}.\mathrm{sed}.\mathrm{oc}\right)\cdot \mathrm{denit\_rat}\cdot \mathrm{O2toC}\cdot \frac{\mathrm{o2\_mol\_mass}}{\mathrm{c\_mol\_mass}} \\ \mathrm{bo2}\cdot \frac{1}{3}\cdot \frac{\mathrm{n\_mol\_mass}}{\mathrm{o2\_mol\_mass}}
$$

---

## NIVAFjord sediments

Version: 0.0.4

File: [modules/nivafjord/sediment.txt](https://github.com/NIVANorge/Mobius2/tree/main/models/modules/nivafjord/sediment.txt)

### External symbols

| Name | Symbol | Type |
| ---- | ------ | ---- |
| CO₂ | **co2** | quantity |
| Depth | **z** | property |
| Fjord layer | **layer** | compartment |
| CH₄ | **ch4** | quantity |
| Fjord sediment | **sediment** | compartment |
| Organic nitrogen | **on** | quantity |
| Water | **water** | quantity |
| O₂ | **o2** | quantity |
| Salinity | **salin** | property |
| Sediments | **sed** | quantity |
| Organic carbon | **oc** | quantity |
| Temperature | **temp** | property |
| Nitrate | **din** | quantity |
| Phosphate | **phos** | quantity |
| Organic phosphorous | **op** | quantity |
| Heat energy | **heat** | quantity |
| Area | **area** | property |
| Thickness | **dz** | property |
| Fjord vertical | **vert** | connection |
| Compute DIC (CO₂, CH₄) | **compute_dic** | par_bool |

### Constants

| Name | Symbol | Unit | Value |
| ---- | ------ | ---- | ----- |
| Sediment density | **sed_rho** | kg m⁻³ | 2600 |
| Sediment specific heat capacity | **sed_C** | J kg⁻¹ K⁻¹ | 4000 |
| Sediment heat conductivity coefficient | **sed_k** | W m⁻¹ K⁻¹ | 2.2 |

### Parameters

| Name | Symbol | Unit |  Description |
| ---- | ------ | ---- |  ----------- |
| **Sediments** | | |  |
| Active sediment thickness | **th** | m |  |
| Sediment porosity | **porosity** |  |  |
| Resuspension fraction | **resus** |  | Fraction of settling material that is resuspended |
| **Biochemistry** | | |  |
| Sediment relative microbial rate | **micr_rel** |  | Rate of sediment microbial processes relative to water column rate |
| Sediment CH4 production scaler | **ch4_production_scaler** |  | sediment CH4 production relative to sediment CO2 production |
| Bubble CH4 formation coefficient | **bubble_ch4** | day⁻¹ |  |
| **Initial chem** | | | Distributes like: `sediment` |
| Initial sediment C fraction | **init_c** | g kg⁻¹ |  |
| Initial sediment IP fraction | **init_p** | g kg⁻¹ |  |
| Initial sediment C/N molar fraction | **init_cn** |  |  |
| Initial sediment (organic) C/P molar fraction | **init_cp** |  |  |
| Sediment pore water DOC concentration | **sed_doc** | mg l⁻¹ |  |
| **Sediment temperature** | | |  |
| Thickness of thermally active sediment layer | **dzh** | m |  |
| Deep sediment temperature | **T_bot** | °C |  |

### State variables

#### **Active sediment thickness**

Location: **sediment.dz**

Unit: m

Value:

$$
\mathrm{th}
$$

#### **Sediment thermal energy**

Location: **sediment.heat**

Unit: J

Initial value:

$$
\left(\mathrm{temp}\rightarrow \mathrm{K}\,\right)\cdot \mathrm{sed\_C}\cdot \mathrm{area}\cdot \mathrm{dzh}\cdot \mathrm{sed\_rho}
$$

#### **Sediment surface temperature**

Location: **sediment.temp**

Unit: °C

Value:

$$
\mathrm{V} = \mathrm{area}\cdot \mathrm{dzh} \\ \mathrm{T\_avg} = \left(\frac{\mathrm{heat}}{\mathrm{sed\_C}\cdot \mathrm{sed\_rho}\cdot \mathrm{area}\cdot \mathrm{dzh}}\rightarrow \mathrm{°C}\,\right) \\ 2\cdot \mathrm{T\_avg}-\mathrm{T\_bot}
$$

Initial value:

$$
\mathrm{T\_bot}
$$

#### **Sediment area**

Location: **sediment.area**

Unit: m²

Value:

$$
\mathrm{A}-\mathrm{A}\lbrack\mathrm{vert}.\mathrm{below}\rbrack
$$

#### **Sediment mass**

Location: **sediment.sed**

Unit: kg

Initial value:

$$
\mathrm{area}\cdot \mathrm{th}\cdot \mathrm{sed\_rho}\cdot \left(1-\mathrm{porosity}\right)
$$

#### **Sediment organic carbon**

Location: **sediment.sed.oc**

Unit: kg

Conc. unit: g kg⁻¹

Initial value (concentration):

$$
\mathrm{init\_c}
$$

#### **Sediment CH4**

Location: **sediment.sed.ch4**

Unit: kg

Conc. unit: g kg⁻¹

#### **Sediment organic N**

Location: **sediment.sed.on**

Unit: kg

Conc. unit: g kg⁻¹

Initial value (concentration):

$$
\frac{\mathrm{init\_c}}{\href{stdlib.html#chemistry}{\mathrm{cn\_molar\_to\_mass\_ratio}}\left(\mathrm{init\_cn}\right)}
$$

#### **Sediment IP**

Location: **sediment.sed.phos**

Unit: kg

Conc. unit: g kg⁻¹

Initial value (concentration):

$$
\mathrm{init\_p}
$$

#### **Sediment OP**

Location: **sediment.sed.op**

Unit: kg

Conc. unit: g kg⁻¹

Initial value (concentration):

$$
\frac{\mathrm{init\_c}}{\href{stdlib.html#chemistry}{\mathrm{cp\_molar\_to\_mass\_ratio}}\left(\mathrm{init\_cp}\right)}
$$

#### **Sediment pore water**

Location: **sediment.water**

Unit: m³

Value:

$$
\mathrm{area}\cdot \mathrm{th}\cdot \mathrm{porosity}
$$

#### **Sediment pore water DOC**

Location: **sediment.water.oc**

Unit: kg

Conc. unit: mg l⁻¹

Value (concentration):

$$
\mathrm{sed\_doc}
$$

#### **CH4 saturation concentration**

Location: **sediment.sed.ch4satconc**

Unit: g kg⁻¹

Value:

$$
\mathrm{T} = \left(\mathrm{temp}\Rightarrow 1\right) \\ \mathrm{S} = \left(\mathrm{layer}.\mathrm{water}.\mathrm{salin}\Rightarrow 1\right) \\ \mathrm{ch4\_mol\_mass}\cdot \left(0.00247 \mathrm{mol}\,\mathrm{kg}^{-1}\,-4.033\cdot 10^{5} \mathrm{mol}\,\mathrm{kg}^{-1}\,\cdot \mathrm{T}\right)\cdot \left(1-\left(0.2844-0.001775\cdot \mathrm{T}\right)\cdot \frac{\mathrm{S}}{58.44}\right)\cdot \left(1+44.6 \mathrm{m}^{-1}\,\cdot \frac{\mathrm{layer}.\mathrm{z}\lbrack\mathrm{vert}.\mathrm{above}\rbrack}{490}\right)
$$

### Fluxes

#### **Water-sediment heat conduction**

Source: layer.water.heat

Target: sediment.heat

Unit: J day⁻¹

Value:

$$
\mathrm{dz\_} = 0.5 \mathrm{m}\, \\ \left(\mathrm{sediment}.\mathrm{area}\cdot \left(\left(\mathrm{layer}.\mathrm{water}.\mathrm{temp}\rightarrow \mathrm{K}\,\right)-\left(\mathrm{sediment}.\mathrm{temp}\rightarrow \mathrm{K}\,\right)\right)\cdot \frac{\mathrm{sed\_k}}{\mathrm{dz\_}}\rightarrow \mathrm{J}\,\mathrm{day}^{-1}\,\right)
$$

#### **Sediment resuspension**

Source: sediment.sed

Target: layer.water.sed

Unit: kg day⁻¹

Value:

$$
\left(\mathrm{in\_flux}\left(\mathrm{sed}\right)\cdot \mathrm{resus}\rightarrow \mathrm{kg}\,\mathrm{day}^{-1}\,\right)
$$

#### **Sediment burial**

Source: sediment.sed

Target: out

Unit: kg day⁻¹

Value:

$$
\left(\mathrm{in\_flux}\left(\mathrm{sed}\right)\cdot \left(1-\mathrm{resus}\right)\rightarrow \mathrm{kg}\,\mathrm{day}^{-1}\,\right)
$$

#### **POC mineralization**

Source: sediment.sed.oc

Target: out

Unit: kg day⁻¹

Value:

$$
\mathrm{sed}.\mathrm{oc}\cdot \mathrm{layer}.\mathrm{water}.\mathrm{min\_rat}\cdot \mathrm{micr\_rel}
$$

#### **PON mineralization and nitrification**

Source: sediment.sed.on

Target: layer.water.din

Unit: kg day⁻¹

Value:

$$
\mathrm{sed}.\mathrm{on}\cdot \mathrm{layer}.\mathrm{water}.\mathrm{min\_rat}\cdot \mathrm{micr\_rel}\cdot \mathrm{n\_min}
$$

#### **POP mineralization**

Source: sediment.sed.op

Target: sediment.sed.phos

Unit: kg day⁻¹

Value:

$$
\mathrm{sed}.\mathrm{op}\cdot \mathrm{layer}.\mathrm{water}.\mathrm{temp\_fac}\cdot \mathrm{maxdecomp\_20}\cdot \mathrm{p\_min}
$$

#### **O₂ sediment microbial consumption**

Source: layer.water.o2

Target: out

Unit: kg day⁻¹

Value:

$$
\mathrm{mr} = \mathrm{layer}.\mathrm{water}.\mathrm{min\_rat}\cdot \mathrm{micr\_rel} \\ \mathrm{sediment}.\mathrm{sed}.\mathrm{oc}\cdot \mathrm{mr}\cdot \frac{\mathrm{o2\_mol\_mass}}{\mathrm{c\_mol\_mass}}\cdot \mathrm{O2toC}+\mathrm{sediment}.\mathrm{sed}.\mathrm{on}\cdot \mathrm{mr}\cdot \mathrm{n\_min}\cdot \frac{\mathrm{o2\_mol\_mass}}{\mathrm{n\_mol\_mass}}\cdot 1.5
$$

#### **CO₂ sediment microbial production**

Source: out

Target: layer.water.co2

Unit: kg day⁻¹

Value:

$$
\mathrm{mr} = \mathrm{layer}.\mathrm{water}.\mathrm{min\_rat}\cdot \mathrm{micr\_rel} \\ \mathrm{dr} = \mathrm{layer}.\mathrm{water}.\mathrm{denit\_rat}\cdot \mathrm{micr\_rel} \\ \mathrm{sediment}.\mathrm{sed}.\mathrm{oc}\cdot \left(\mathrm{mr}+\mathrm{dr}\right)
$$

#### **CH4 sediment microbial production**

Source: out

Target: sediment.sed.ch4

Unit: kg day⁻¹

Value:

$$
\mathrm{mr} = \mathrm{layer}.\mathrm{water}.\mathrm{min\_rat}\cdot \mathrm{micr\_rel} \\ \mathrm{sediment}.\mathrm{sed}.\mathrm{oc}\cdot \mathrm{mr}\cdot \mathrm{ch4\_production\_scaler}
$$

#### **CH4 sediment bubbling**

Source: sediment.sed.ch4

Target: out

Unit: kg day⁻¹

Value:

$$
\left(\mathrm{max}\left(0,\, \mathrm{bubble\_ch4}\cdot \left(\mathrm{conc}\left(\mathrm{ch4}\right)-\mathrm{ch4satconc}\right)\cdot \mathrm{sed}\right)\rightarrow \mathrm{kg}\,\mathrm{day}^{-1}\,\right)
$$

#### **CH4 sediment diffusion release**

Source: sediment.sed.ch4

Target: layer.water.ch4

Unit: kg day⁻¹

Value:

$$
\mathrm{diff\_coeff} = \left(4.18\cdot 10^{11} \mathrm{m}^{2}\,\mathrm{s}^{-1}\,\mathrm{°C}^{-1}\,\cdot \mathrm{sediment}.\mathrm{temp}+8.06\cdot 10^{10} \mathrm{m}^{2}\,\mathrm{s}^{-1}\,\rightarrow \mathrm{m}^{2}\,\mathrm{day}^{-1}\,\right) \\ \left(\mathrm{diff\_coeff}\cdot \mathrm{area}\cdot \frac{\mathrm{conc}\left(\mathrm{sed}.\mathrm{ch4}\right)\cdot \mathrm{rho\_water}\cdot \mathrm{porosity}-\mathrm{conc}\left(\mathrm{layer}.\mathrm{water}.\mathrm{ch4}\right)}{0.25 \mathrm{m}\,}\rightarrow \mathrm{kg}\,\mathrm{day}^{-1}\,\right)
$$

#### **POC mineralization by denitrification**

Source: sediment.sed.oc

Target: out

Unit: kg day⁻¹

Value:

$$
\mathrm{sediment}.\mathrm{sed}.\mathrm{oc}\cdot \mathrm{layer}.\mathrm{water}.\mathrm{denit\_rat}
$$

#### **NO3 sediment denitrification consumption**

Source: layer.water.din

Target: out

Unit: kg day⁻¹

Value:

$$
\mathrm{bo2} = \mathrm{sediment}.\mathrm{sed}.\mathrm{oc}\cdot \mathrm{layer}.\mathrm{water}.\mathrm{denit\_rat}\cdot \mathrm{O2toC}\cdot \frac{\mathrm{o2\_mol\_mass}}{\mathrm{c\_mol\_mass}} \\ \mathrm{bo2}\cdot \frac{1}{3}\cdot \frac{\mathrm{n\_mol\_mass}}{\mathrm{o2\_mol\_mass}}
$$

#### **PO4 adsorption / desorption to sediment layer**

Source: layer.water.phos

Target: sediment.sed.phos

Unit: kg day⁻¹

Value:

$$
\mathrm{epc0} = \left(\frac{\mathrm{conc}\left(\mathrm{sediment}.\mathrm{sed}.\mathrm{phos}\right)}{\mathrm{k\_sorp}}\rightarrow \mathrm{mg}\,\mathrm{l}^{-1}\,\right) \\ \mathrm{epc0\_} = \mathrm{min}\left(\mathrm{epc0},\, 0 \mathrm{mg}\,\mathrm{l}^{-1}\,\right) \\ \mathrm{r\_sorp}\cdot \left(\left(\mathrm{ox\_fac}\cdot \left(\mathrm{conc}\left(\mathrm{water}.\mathrm{phos}\right)-\mathrm{epc0\_}\right)\cdot \mathrm{water}\rightarrow \mathrm{kg}\,\right)-\left(1-\mathrm{ox\_fac}\right)\cdot \mathrm{sediment}.\mathrm{sed}.\mathrm{phos}\right)
$$

---

## Phytoplankton parameters Fjord 2

Version: 0.0.1

File: [modules/plankton.txt](https://github.com/NIVANorge/Mobius2/tree/main/models/modules/plankton.txt)

### External symbols

| Name | Symbol | Type |
| ---- | ------ | ---- |
| Fjord phytoplankton | **phyt** | quantity |
| Basin | **basin** | compartment |

### Constants

| Name | Symbol | Unit | Value |
| ---- | ------ | ---- | ----- |
| Fraction of PAR in SW radiation | **f_par** |  | 0.45 |
| Phytoplankton oxicity threshold | **phyt_ox_th** | mg l⁻¹ | 2 |
| Phytoplankton increased death rate from anoxicity | **phyt_death_anox** |  | 10 |

### Parameters

| Name | Symbol | Unit |  Description |
| ---- | ------ | ---- |  ----------- |
| **Phytoplankton** | | | Distributes like: `fjord_phyt` |
| Q10 of Phyto growth | **phyt_q10** |  | Adjustment of rate with 10°C change in temperature |
| Phyto max growth rate | **phyt_grow** | day⁻¹ | At 20°C |
| Phyto death rate | **phyt_death** | day⁻¹ |  |
| Optimal PAR intensity | **iopt** | W m⁻² |  |
| Half-saturation for nutrient uptake | **alpha** |  |  |
| Molar C/N ratio in Phytoplankton | **phyt_cn** |  | The default is the Redfield ratio |
| Molar C/P ratio in Phytoplankton | **phyt_cp** |  | The default is the Redfield ratio |
| Phytoplankton excretion rate | **excr** | day⁻¹ |  |
| Chl-a fraction | **chl_a_f** | % | How large a fraction of the phytoplankton mass is chlorophyll a |

---

## Phytoplankton Fjord 2

Version: 0.0.0

File: [modules/plankton.txt](https://github.com/NIVANorge/Mobius2/tree/main/models/modules/plankton.txt)

### Description

A phytoplankton module that is based on exponential growth. Inspired by OxyDep.

Authors: Magnus Dahler Norling

### External symbols

| Name | Symbol | Type |
| ---- | ------ | ---- |
| Fjord phytoplankton | **phyt** | quantity |
| Fjord layer | **layer** | compartment |
| Organic nitrogen | **on** | quantity |
| Water | **water** | quantity |
| Temperature | **temp** | property |
| Organic carbon | **oc** | quantity |
| Organic phosphorous | **op** | quantity |
| O₂ | **o2** | quantity |
| Nitrate | **din** | quantity |
| Phosphate | **phos** | quantity |
| Sediments | **sed** | quantity |
| Chlorophyll-a | **chl_a** | property |
| Shortwave radiation | **sw** | property |

### Constants

| Name | Symbol | Unit | Value |
| ---- | ------ | ---- | ----- |
| Min phyto saturation | **minphyt** | mg l⁻¹ | 0.01 |

### Parameters

| Name | Symbol | Unit |  Description |
| ---- | ------ | ---- |  ----------- |
| **Initial phytoplankton** | | | Distributes like: `layer`, `fjord_phyt` |
| Initial chl-a concentration | **init_chla** | µg l⁻¹ |  |

### State variables

#### **Layer phytoplankton**

Location: **layer.water.phyt**

Unit: kg

Conc. unit: mg l⁻¹

Initial value (concentration):

$$
\left(\frac{\mathrm{init\_chla}}{\mathrm{chl\_a\_f}}\rightarrow \mathrm{kg}\,\right)
$$

#### **Phytoplankton C**

Location: **layer.water.phyt.oc**

Unit: kg

Value (concentration):

$$
1
$$

#### **Phytoplankton N**

Location: **layer.water.phyt.on**

Unit: kg

Value (concentration):

$$
\frac{1}{\href{stdlib.html#chemistry}{\mathrm{cn\_molar\_to\_mass\_ratio}}\left(\mathrm{phyt\_cn}\right)}
$$

#### **Phytoplankton P**

Location: **layer.water.phyt.op**

Unit: kg

Value (concentration):

$$
\frac{1}{\href{stdlib.html#chemistry}{\mathrm{cp\_molar\_to\_mass\_ratio}}\left(\mathrm{phyt\_cp}\right)}
$$

#### **Light limitation**

Location: **layer.water.phyt.light_lim**

Unit: 

Value:

$$
\mathrm{par\_sw} = \mathrm{sw}\cdot \mathrm{f\_par} \\ \mathrm{f} = \frac{\mathrm{par\_sw}}{\mathrm{max}\left(0.5\cdot \mathrm{par\_sw},\, \mathrm{iopt}\right)} \\ \mathrm{f}\cdot e^{1-\mathrm{f}}
$$

#### **Nitrogen limitation**

Location: **layer.water.phyt.N_lim**

Unit: 

Value:

$$
\mathrm{p} = \mathrm{min}\left(\mathrm{conc}\left(\mathrm{phyt}\right),\, \mathrm{minphyt}\right)\cdot \mathrm{water} \\ \mathrm{phyt\_mol\_n} = \left(\frac{\mathrm{p}}{\mathrm{phyt\_cn}\cdot \mathrm{c\_mol\_mass}}\rightarrow \mathrm{mol}\,\right) \\ \mathrm{nut\_mol\_n} = \left(\frac{\mathrm{water}.\mathrm{din}}{\mathrm{n\_mol\_mass}}\rightarrow \mathrm{mol}\,\right) \\ \mathrm{ratio2} = \left(\frac{\mathrm{nut\_mol\_n}}{\mathrm{phyt\_mol\_n}}\right)^{2} \\ \frac{\mathrm{ratio2}}{\mathrm{ratio2}+\mathrm{alpha}}
$$

#### **Phosphorus limitation**

Location: **layer.water.phyt.P_lim**

Unit: 

Value:

$$
\mathrm{p} = \mathrm{min}\left(\mathrm{conc}\left(\mathrm{phyt}\right),\, \mathrm{minphyt}\right)\cdot \mathrm{water} \\ \mathrm{phyt\_mol\_p} = \left(\frac{\mathrm{p}}{\mathrm{phyt\_cp}\cdot \mathrm{c\_mol\_mass}}\rightarrow \mathrm{mol}\,\right) \\ \mathrm{nut\_mol\_p} = \left(\frac{\mathrm{water}.\mathrm{phos}}{\mathrm{p\_mol\_mass}}\rightarrow \mathrm{mol}\,\right) \\ \mathrm{ratio2} = \left(\frac{\mathrm{nut\_mol\_p}}{\mathrm{phyt\_mol\_p}}\right)^{2} \\ \frac{\mathrm{ratio2}}{\mathrm{ratio2}+\mathrm{alpha}}
$$

#### **Photosynthetic C fixation**

Location: **layer.water.phyt.fix**

Unit: kg day⁻¹

Value:

$$
\mathrm{lim} = \mathrm{min}\left(\mathrm{light\_lim},\, \mathrm{min}\left(\mathrm{N\_lim},\, \mathrm{P\_lim}\right)\right) \\ \mathrm{maxrate} = \href{stdlib.html#response}{\mathrm{q10\_adjust}}\left(\mathrm{phyt\_grow},\, 20 \mathrm{°C}\,,\, \mathrm{temp},\, \mathrm{phyt\_q10}\right) \\ \left(\mathrm{maxrate}\cdot \mathrm{lim}\cdot \mathrm{phyt}\rightarrow \mathrm{kg}\,\mathrm{day}^{-1}\,\right)
$$

#### **Phyto chl-a**

Location: **layer.water.phyt.chl_a**

Unit: mg l⁻¹

Value:

$$
\left(\mathrm{conc}\left(\mathrm{phyt}\right)\cdot \mathrm{chl\_a\_f}\rightarrow \mathrm{mg}\,\mathrm{l}^{-1}\,\right)
$$

#### **Layer chl-a**

Location: **layer.water.chl_a**

Unit: mg l⁻¹

Value:

$$
\mathrm{aggregate}\left(\mathrm{phyt}.\mathrm{chl\_a}\right)
$$

### Fluxes

#### **Phytoplankton growth**

Source: out

Target: layer.water.phyt

Unit: kg day⁻¹

Value:

$$
\mathrm{fix}
$$

#### **Phytoplankton growth N uptake**

Source: layer.water.phyt.on

Target: layer.water.din

Unit: kg day⁻¹

Value:

$$
\frac{-\mathrm{phyt}.\mathrm{fix}}{\href{stdlib.html#chemistry}{\mathrm{cn\_molar\_to\_mass\_ratio}}\left(\mathrm{phyt\_cn}\right)}
$$

#### **Phytoplankton growth P uptake**

Source: layer.water.phyt.op

Target: layer.water.phos

Unit: kg day⁻¹

Value:

$$
\frac{-\mathrm{phyt}.\mathrm{fix}}{\href{stdlib.html#chemistry}{\mathrm{cp\_molar\_to\_mass\_ratio}}\left(\mathrm{phyt\_cp}\right)}
$$

#### **Phytoplankton C excretion**

Source: layer.water.phyt

Target: layer.water.oc

Unit: kg day⁻¹

Value:

$$
\mathrm{excr}\cdot \mathrm{phyt}
$$

#### **Phytoplankton N excretion**

Source: layer.water.phyt.on

Target: layer.water.on

Unit: kg day⁻¹

Value:

$$
\mathrm{excr}\cdot \mathrm{phyt}.\mathrm{on}
$$

#### **Phytoplankton P excretion**

Source: layer.water.phyt.op

Target: layer.water.op

Unit: kg day⁻¹

Value:

$$
\mathrm{excr}\cdot \mathrm{phyt}.\mathrm{op}
$$

#### **Phytoplankton death**

Source: layer.water.phyt

Target: layer.water.sed

Unit: kg day⁻¹

Value:

$$
\mathrm{wd} = 0.5 \\ \mathrm{o2\_factor} = \href{stdlib.html#response}{\mathrm{s\_response}}\left(\mathrm{conc}\left(\mathrm{o2}\right),\, \left(1-\mathrm{wd}\right)\cdot \mathrm{phyt\_ox\_th},\, \left(1+\mathrm{wd}\right)\cdot \mathrm{phyt\_ox\_th},\, \mathrm{phyt\_death\_anox},\, 1\right) \\ \mathrm{phyt\_death}\cdot \mathrm{phyt}\cdot \mathrm{o2\_factor}
$$

#### **Layer O₂ photosynthesis**

Source: out

Target: layer.water.o2

Unit: kg day⁻¹

Value:

$$
\mathrm{aggregate}\left(\mathrm{phyt}.\mathrm{fix}\right)\cdot \frac{\mathrm{o2\_mol\_mass}}{\mathrm{c\_mol\_mass}}
$$



{% include lib/mathjax.html %}

