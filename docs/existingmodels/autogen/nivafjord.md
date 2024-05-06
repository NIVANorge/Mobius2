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

The file was generated at 2024-05-06 13:32:10.

---

## NIVAFjord dimensions

Version: 0.0.0

File: [modules/nivafjord/basin.txt](https://github.com/NIVANorge/Mobius2/tree/main/models/modules/nivafjord/basin.txt)

### External symbols

| Name | Symbol | Type |
| ---- | ------ | ---- |
| Fjord layer | **layer** | compartment |
| All basins | **all_basins** | index_set |
| Layer index | **layer_idx** | index_set |

### Parameters

| Name | Symbol | Unit |  Description |
| ---- | ------ | ---- |  ----------- |
| **Layer thickness** | | | |
| Stable layer thickness | **dz0** | m |  |
| **Layer area** | | | |
| Layer area | **A** | m² |  |

---

## NIVAFjord basin

Version: 0.0.3

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
|  | **sw_sed** | loc |
| Depth | **z** | property |
| Density | **rho** | property |
| Basin | **basin** | compartment |
| Fjord layer | **layer** | compartment |
| Water | **water** | quantity |
| Salinity | **salinity** | property |
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
| Fjord vertical | **vert** | connection |
| Shortwave vertical | **sw_vert** | connection |

### Parameters

| Name | Symbol | Unit |  Description |
| ---- | ------ | ---- |  ----------- |
| **Mixing parameters** | | | |
| Brunt-Väisälä frequency reference | **N0** | s⁻¹ |  |
| Minimum B-V frequency | **Nmin** | s⁻¹ |  |
| Mixing non-linear coefficient | **alpha** |  |  |
| Surface additional mixing energy | **Es0** | m² s⁻³ |  |
| Halving depth of additional mixing energy | **zshalf** | m |  |
| Diminishing rate of additional mixing energy | **hsfact** | m |  |
| **Layer specific mixing** | | | |
| Mixing factor reference | **K0** | m² s⁻¹ | Mixing factor when the B-V frequency is equal to the reference |
| **Initial layer physical** | | | |
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
\mathrm{z}\lbrack\mathrm{vert}.\mathrm{above}\rbrack+\mathrm{dz}\lbrack\mathrm{vert}.\mathrm{above}\rbrack
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

Initial value:

$$
\mathrm{salinity}\cdot 0.001\cdot \mathrm{rho\_water}
$$

#### **Layer thermal energy**

Location: **layer.water.heat**

Unit: J

Initial value:

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

#### **Total wind mixing energy**

Location: **basin.emix**

Unit: J

Value:

$$
\mathrm{A}\lbrack\mathrm{vert}.\mathrm{top}\rbrack\cdot \sqrt{\frac{\mathrm{stress}^{3}}{\mathrm{layer}.\mathrm{water}.\mathrm{rho}\lbrack\mathrm{vert}.\mathrm{top}\rbrack}}\cdot \mathrm{time}.\mathrm{step\_length\_in\_seconds}
$$

#### **Sum V above**

Location: **layer.water.sumV**

Unit: m³

Value:

$$
\mathrm{sumV}\lbrack\mathrm{vert}.\mathrm{above}\rbrack+\mathrm{water}\lbrack\mathrm{vert}.\mathrm{above}\rbrack
$$

#### **Potential energy needed for wind mixing**

Location: **layer.water.potmix**

Unit: J

Value:

$$
\mathrm{max}\left(0,\, \mathrm{grav}\cdot \mathrm{ddens}\cdot \mathrm{sumV}\cdot \frac{\mathrm{water}}{\mathrm{sumV}+\mathrm{water}}\cdot \frac{\mathrm{z}+0.5\cdot \mathrm{dz}}{2}\right)
$$

#### **Wind mixing energy**

Location: **layer.water.emix**

Unit: J

Value:

$$
\mathrm{rem} = \mathrm{max}\left(0,\, \mathrm{basin}.\mathrm{emix}-\mathrm{summix}\right) \\ \mathrm{min}\left(\mathrm{rem},\, \mathrm{potmix}\right)
$$

#### **Sum used wind mixing energy**

Location: **layer.water.summix**

Unit: J

Value:

$$
\mathrm{emix}\lbrack\mathrm{vert}.\mathrm{above}\rbrack+\mathrm{summix}\lbrack\mathrm{vert}.\mathrm{above}\rbrack
$$

#### **Wind mixing**

Location: **layer.water.v_w**

Unit: m s⁻¹

Value:

$$
\mathrm{rem} = \mathrm{max}\left(0,\, \mathrm{basin}.\mathrm{emix}-\mathrm{summix}\lbrack\mathrm{vert}.\mathrm{below}\rbrack\right) \\ \mathrm{mixspeed} = \frac{1 \mathrm{m}\,}{\mathrm{time}.\mathrm{step\_length\_in\_seconds}} \\ \begin{cases}0 \mathrm{m}\,\mathrm{s}^{-1}\, & \text{if}\;\mathrm{is\_at}\lbrack\mathrm{vert}.\mathrm{bottom}\rbrack \\ \left(\mathrm{mixspeed}\rightarrow \mathrm{m}\,\mathrm{s}^{-1}\,\right) & \text{if}\;\mathrm{rem}>10^{30} \mathrm{J}\,\;\text{and}\;\mathrm{potmix}\lbrack\mathrm{vert}.\mathrm{below}\rbrack<10^{30} \mathrm{J}\, \\ \left(\mathrm{mixspeed}\cdot \href{stdlib.html#basic}{\mathrm{safe\_divide}}\left(\mathrm{emix}\lbrack\mathrm{vert}.\mathrm{below}\rbrack,\, \mathrm{potmix}\lbrack\mathrm{vert}.\mathrm{below}\rbrack\right)\rightarrow \mathrm{m}\,\mathrm{s}^{-1}\,\right) & \text{otherwise}\end{cases}
$$

#### **Tide wave mixing**

Location: **layer.water.v_t**

Unit: m s⁻¹

Value:

$$
\mathrm{dz\_} = 0.5\cdot \left(\mathrm{dz}+\mathrm{dz}\lbrack\mathrm{vert}.\mathrm{below}\rbrack\right) \\ \href{stdlib.html#basic}{\mathrm{safe\_divide}}\left(\mathrm{K0},\, \mathrm{dz\_}\cdot \left(\frac{\mathrm{Nfreq}}{\mathrm{N0}}\right)^{\mathrm{alpha}}\right)
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
\left(\mathrm{in\_flux}\left(\mathrm{sw\_vert},\, \mathrm{layer}.\mathrm{water}.\mathrm{heat}\right)\rightarrow \mathrm{J}\,\mathrm{day}^{-1}\,\right)\cdot \left(1-\mathrm{attn}\right)\cdot \frac{\mathrm{A}\lbrack\mathrm{vert}.\mathrm{below}\rbrack}{\mathrm{A}}
$$

#### **Shortwave to sediments**

Source: layer.water.heat

Target: sw_sed

Unit: J day⁻¹

Value:

$$
\left(\mathrm{in\_flux}\left(\mathrm{sw\_vert},\, \mathrm{layer}.\mathrm{water}.\mathrm{heat}\right)\rightarrow \mathrm{J}\,\mathrm{day}^{-1}\,\right)\cdot \left(1-\mathrm{attn}\right)\cdot \frac{\mathrm{A}-\mathrm{A}\lbrack\mathrm{vert}.\mathrm{below}\rbrack}{\mathrm{A}}
$$



{% include lib/mathjax.html %}

