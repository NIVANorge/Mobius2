---
layout: default
title: EasyLake
parent: Mathematical description
grand_parent: Existing models
nav_order: 1
---

# EasyLake

This is auto-generated documentation based on the model code in [models/easylake_simplycnp_model_new.txt](https://github.com/NIVANorge/Mobius2/blob/main/models/easylake_simplycnp_model_new.txt) .
Since the modules can be dynamically loaded with different arguments, this documentation does not necessarily reflect all use cases of the modules.

See the note on [notation](autogen.html#notation).

The file was generated at 2025-11-14 14:18:15.

---

## EasyLake

Version: 0.1.0

File: [modules/easylake/easylake.txt](https://github.com/NIVANorge/Mobius2/tree/main/models/modules/easylake/easylake.txt)

### Description

EasyLake is a simple lake model for use along with catchment models. It simulates residence time in the lake in an upper and lower compartment, and it has separate modules for nutrient retention and contaminants.

The physical part of the model simulates water balance and temperature.

The lake can also be equipped with an ice module (defined in the AirSea module).

The internal heat distribution is inspired by (but not as complex as) the [FLake model](http://www.flake.igb-berlin.de/).

We assume that the hypsograph of the lake follows a shape so that

$$
A(z) = A_0 \left(\frac{z}{z_{max}}\right)^{\theta+1}
$$

where $$A(z)$$ is the area of the horizontal cross-section of the lake at level $$z$$ measuring up from the maximal depth $$z_{max}$$, $$A_0$$ is the surface area at the zero level of the outlet, and $$\theta$$ is a user-defined parameter. For instance, with theta=1, the lake is approximately cone shaped.

We also assume that the discharge is linearly proportional to the water level at the outlet.

The lake is partitioned into an epilimnion and a hypolimnion. The epilimnion is assumed to have a temperature profile that does not depend on the depth. In the hypolimnion, the temperature profile follows

$$
T(z) = (T_e - T_b)\left(\frac{z}{z_{max}}\right)^2 + T_b,\; z < z_e
$$

Where $$T_e$$ is current epilimnion temperature and $$T_b$$ is bottom temperature (user-defined constant parameter), and $$z_e$$ is epilimnion thickness. The total heat of the lake is then proportional to

$$
\int_0^{z_{max}} A(z)T(z)\mathrm{d}z
$$

which has an exact solution, which one can back-solve for $$T_e$$ if the total heat of the lake is known.

The total heat is computed as an ordinary differential equation, and depends mostly on surface heat exchange (computed by the AirSea module).

The epilimnion is set to have a dynamic thickness. This is not simulated, but is instead determined by user-defined empirical parameters. The epilimnion and hypolimnion mix when the difference between epilimnion temperature and mean hypolimnion temperature is less than 0.4°C (typically in spring and autumn).

The thickness is set to have a given winter value, then is set to an initial thickness after spring mixing, after which it increases linearly.

Authors: François Clayer, Magnus D. Norling

### External symbols

| Name | Symbol | Type |
| ---- | ------ | ---- |
| Epilimnion | **epi** | compartment |
| Hypolimnion | **hyp** | compartment |
| Water | **water** | quantity |
| Heat energy | **heat** | quantity |
| Temperature | **temp** | property |
| Thickness | **th** | property |
| Surface area | **area** | property |
|  | **epi_target** | loc |

### Module functions

**shape_cross_section_A(A_0 : m², z_0 : m, z : m, theta)** = 

$$
\mathrm{A\_0}\cdot \left(\frac{\mathrm{z}}{\mathrm{z\_0}}\right)^{\mathrm{theta}+1}
$$

**shape_tip_V(A_0 : m², z_0 : m, z : m, theta)** = 

$$
\mathrm{A\_0}\cdot \mathrm{z}\cdot \frac{\left(\frac{\mathrm{z}}{\mathrm{z\_0}}\right)^{\mathrm{theta}+1}}{\mathrm{theta}+2}
$$

**shape_section_V(A_0 : m², z_0 : m, z1 : m, z2 : m, theta)** = 

$$
\mathrm{shape\_tip\_V}\left(\mathrm{A\_0},\, \mathrm{z\_0},\, \mathrm{z1},\, \mathrm{theta}\right)-\mathrm{shape\_tip\_V}\left(\mathrm{A\_0},\, \mathrm{z\_0},\, \mathrm{z2},\, \mathrm{theta}\right)
$$

**shape_tip_z(A_0 : m², z_0 : m, V : m³, theta)** = 

$$
\mathrm{zz\_0} = \left(\mathrm{z\_0}\Rightarrow 1\right) \\ \mathrm{f} = \left(\mathrm{V}\cdot \left(\mathrm{theta}+2\right)\cdot \frac{\mathrm{zz\_0}^{\mathrm{theta}+1}}{\mathrm{A\_0}}\Rightarrow 1\right) \\ \left(\mathrm{f}^{\frac{1}{\mathrm{theta}+2}}\Rightarrow \mathrm{m}\,\right)
$$

**hypo_temperature_integral(A_0 : m², T_e : °C, T_b : °C, z_e : m, z_0 : m, theta)** = 

$$
\left(2\cdot \mathrm{T\_b}+\left(\mathrm{theta}+2\right)\cdot \mathrm{T\_e}\right)\cdot \mathrm{A\_0}\cdot \mathrm{z\_e}\cdot \frac{\left(\frac{\mathrm{z\_e}}{\mathrm{z\_0}}\right)^{\mathrm{theta}+1}}{\mathrm{theta}^{2}+6\cdot \mathrm{theta}+8}
$$

### Parameters

| Name | Symbol | Unit |  Description |
| ---- | ------ | ---- |  ----------- |
| **Lake physical** | | | Distributes like: `epi` |
| Lake surface area | **A_surf** | m² |  |
| Water level at which outflow is 0 | **z_outflow** | m |  |
| Winter epilimnion thickness | **th_epi_w** | m |  |
| Spring epilimnion thickness | **th_epi_s** | m |  |
| Lake bathymetry factor | **theta** |  | The cross-section area of the lake at level z is A_0*(z/z_0)^(theta+1) |
| Epilimnion thickening rate | **dz_epi** | m day⁻¹ | How fast the thickness of the epilimnion changes during summer |
| Rating function linear component | **rate_l** | m² s⁻¹ |  |
| Initial epilimnion temperature | **t_epi** | °C |  |
| Bottom temperature | **t_bot** | °C |  |
| Wind mixing rate | **wnd_rate** | day⁻¹ |  |

### State variables

#### **Epilimnion volume**

Location: **epi.water**

Unit: m³

Initial value:

$$
\mathrm{shape\_section\_V}\left(\mathrm{A\_surf},\, \mathrm{z\_outflow},\, \mathrm{z\_outflow},\, \mathrm{z\_outflow}-\mathrm{th},\, \mathrm{theta}\right)
$$

#### **Hypolimnion volume**

Location: **hyp.water**

Unit: m³

Initial value:

$$
\mathrm{shape\_tip\_V}\left(\mathrm{A\_surf},\, \mathrm{z\_outflow},\, \mathrm{z\_outflow}-\mathrm{epi}.\mathrm{th},\, \mathrm{theta}\right)
$$

#### **Epilimnion thickness (relative to zero outflow)**

Location: **epi.th**

Unit: m

Value:

$$
\mathrm{doy} = \mathrm{time}.\mathrm{day\_of\_year} \\ \mathrm{spring\_mixing} = \mathrm{last}\left(\mathrm{ind}\right)\;\text{and}\;\left(\mathrm{last}\left(\mathrm{water}.\mathrm{temp}\right)>\mathrm{last}\left(\mathrm{hyp}.\mathrm{water}.\mathrm{temp}\right)\;\text{or}\;\mathrm{doy}<150 \mathrm{day}\,\right) \\ \mathrm{is\_winter} = \mathrm{last}\left(\mathrm{water}.\mathrm{temp}\right)<\mathrm{t\_bot}\;\text{and}\;\left(\mathrm{doy}<90 \mathrm{day}\,\;\text{or}\;\mathrm{doy}>275 \mathrm{day}\,\right) \\ \begin{cases}\mathrm{th\_epi\_s} & \text{if}\;\mathrm{spring\_mixing} \\ \mathrm{th\_epi\_w} & \text{if}\;\mathrm{is\_winter} \\ \mathrm{last}\left(\mathrm{th}\right)+\left(\mathrm{dz\_epi}\cdot \mathrm{time}.\mathrm{step\_length\_in\_seconds}\rightarrow \mathrm{m}\,\right) & \text{otherwise}\end{cases}
$$

Initial value:

$$
\mathrm{th\_epi\_w}
$$

#### **Lake water level**

Location: **epi.level**

Unit: m

Value:

$$
\mathrm{shape\_tip\_z}\left(\mathrm{A\_surf},\, \mathrm{z\_outflow},\, \mathrm{epi}.\mathrm{water}+\mathrm{hyp}.\mathrm{water},\, \mathrm{theta}\right)-\mathrm{z\_outflow}
$$

#### **Epilimnion surface area**

Location: **epi.area**

Unit: m²

Value:

$$
\mathrm{shape\_cross\_section\_A}\left(\mathrm{A\_surf},\, \mathrm{z\_outflow},\, \mathrm{level}+\mathrm{z\_outflow},\, \mathrm{theta}\right)
$$

#### **Hypolimnion surface area**

Location: **hyp.area**

Unit: m²

Value:

$$
\mathrm{shape\_cross\_section\_A}\left(\mathrm{A\_surf},\, \mathrm{z\_outflow},\, \mathrm{z\_outflow}-\mathrm{epi}.\mathrm{th},\, \mathrm{theta}\right)
$$

#### **Heat energy**

Location: **epi.water.heat**

Unit: J

Initial value:

$$
\href{stdlib.html#water-utils}{\mathrm{water\_temp\_to\_heat}}\left(\mathrm{water},\, \mathrm{t\_epi}\right)
$$

#### **Heat energy**

Location: **hyp.water.heat**

Unit: J

Initial value:

$$
\mathrm{t\_hyp} = \frac{\mathrm{hypo\_temperature\_integral}\left(\mathrm{A\_surf},\, \mathrm{t\_epi},\, \mathrm{t\_bot},\, \mathrm{z\_outflow}-\mathrm{epi}.\mathrm{th},\, \mathrm{z\_outflow},\, \mathrm{theta}\right)}{\mathrm{water}} \\ \href{stdlib.html#water-utils}{\mathrm{water\_temp\_to\_heat}}\left(\mathrm{water},\, \mathrm{t\_hyp}\right)
$$

#### **Epilimnion temperature**

Location: **epi.water.temp**

Unit: °C

Value:

$$
\href{stdlib.html#water-utils}{\mathrm{water\_heat\_to\_temp}}\left(\mathrm{water},\, \mathrm{water}.\mathrm{heat}\right)
$$

Initial value:

$$
\mathrm{t\_epi}
$$

#### **Hypolimnion temperature (mean)**

Location: **hyp.water.temp**

Unit: °C

Value:

$$
\href{stdlib.html#water-utils}{\mathrm{water\_heat\_to\_temp}}\left(\mathrm{water},\, \mathrm{water}.\mathrm{heat}\right)
$$

#### **Mixing indicator**

Location: **epi.ind**

Unit: 

Value:

$$
\left|\mathrm{water}.\mathrm{temp}-\mathrm{hyp}.\mathrm{water}.\mathrm{temp}\right|<0.4 \mathrm{°C}\,
$$

### Fluxes

#### **Metalimnion movement**

Source: epi.water

Target: hyp.water

Unit: m³ day⁻¹

Value:

$$
\mathrm{hyp\_vol\_last} = \mathrm{shape\_tip\_V}\left(\mathrm{A\_surf},\, \mathrm{z\_outflow},\, \mathrm{z\_outflow}-\mathrm{last}\left(\mathrm{th}\right),\, \mathrm{theta}\right) \\ \mathrm{hyp\_vol\_now} = \mathrm{shape\_tip\_V}\left(\mathrm{A\_surf},\, \mathrm{z\_outflow},\, \mathrm{z\_outflow}-\mathrm{th},\, \mathrm{theta}\right) \\ \left(\frac{\mathrm{hyp\_vol\_now}-\mathrm{hyp\_vol\_last}}{\mathrm{time}.\mathrm{step\_length\_in\_seconds}}\rightarrow \mathrm{m}^{3}\,\mathrm{day}^{-1}\,\right)
$$

#### **Lake outflow flux**

Source: epi.water

Target: epi_target

Unit: m³ s⁻¹

Value:

$$
\mathrm{max}\left(0,\, \mathrm{rate\_l}\cdot \mathrm{level}\right)
$$

#### **Layer heat transfer**

Source: epi.water.heat

Target: hyp.water.heat

Unit: J day⁻¹

Value:

$$
\mathrm{V} = \mathrm{water}+\mathrm{hyp}.\mathrm{water} \\ \mathrm{lake\_t} = \href{stdlib.html#water-utils}{\mathrm{water\_heat\_to\_temp}}\left(\mathrm{V},\, \mathrm{heat}+\mathrm{hyp}.\mathrm{water}.\mathrm{heat}\right) \\ \mathrm{hypo\_temp} = \mathrm{hypo\_temperature\_integral}\left(\mathrm{A\_surf},\, \mathrm{temp},\, \mathrm{t\_bot},\, \mathrm{z\_outflow}-\mathrm{epi}.\mathrm{th},\, \mathrm{z\_outflow},\, \mathrm{theta}\right) \\ \mathrm{T\_e\_should\_be} = \frac{\mathrm{lake\_t}\cdot \mathrm{V}-\mathrm{hypo\_temp}}{\mathrm{water}} \\ \mathrm{epi\_heat\_should\_be} = \href{stdlib.html#water-utils}{\mathrm{water\_temp\_to\_heat}}\left(\mathrm{water},\, \mathrm{T\_e\_should\_be}\right) \\ 5 \mathrm{day}^{-1}\,\cdot \left(\mathrm{heat}-\mathrm{epi\_heat\_should\_be}\right)
$$

#### **Mixing**

Source: epi.water

Target: hyp.water

Unit: m³ day⁻¹

This is a mixing flux (affects dissolved quantities only)

Value:

$$
\mathrm{A\_surf}\cdot \mathrm{epi}.\mathrm{th}\cdot \mathrm{epi}.\mathrm{ind}\cdot \mathrm{wnd\_rate}
$$

---

## Phytoplankton parameters Lake

Version: 0.0.0

File: [modules/plankton.txt](https://github.com/NIVANorge/Mobius2/tree/main/models/modules/plankton.txt)

### External symbols

| Name | Symbol | Type |
| ---- | ------ | ---- |
| Phytoplankton | **phyt** | quantity |
| Epilimnion | **basin** | compartment |

### Constants

| Name | Symbol | Unit | Value |
| ---- | ------ | ---- | ----- |
| Fraction of PAR in SW radiation | **f_par** |  | 0.45 |
| Phytoplankton oxicity threshold | **phyt_ox_th** | mg l⁻¹ | 2 |
| Phytoplankton increased death rate from anoxicity | **phyt_death_anox** |  | 10 |

### Parameters

| Name | Symbol | Unit |  Description |
| ---- | ------ | ---- |  ----------- |
| **Phytoplankton** | | | Distributes like: `phyt` |
| Chl-a of phyto at equilibrium | **phyt_eq_20** | mg l⁻¹ | Assuming 20°C and no nutrient or light limitations. |
| Q10 of Phyto equilibrium | **phyt_q10** |  | Adjustment of rate with 10°C change in temperature |
| Phytoplankton turnover rate | **phyt_turnover** | day⁻¹ |  |
| Optimal PAR intensity | **iopt** | W m⁻² |  |
| Half-saturation for nutrient uptake | **alpha** | mmol m⁻³ |  |
| Molar C/N ratio in Phytoplankton | **phyt_cn** |  | The default is the Redfield ratio |
| Molar C/P ratio in Phytoplankton | **phyt_cp** |  | The default is the Redfield ratio |
| Phytoplankton excretion rate | **excr** | day⁻¹ |  |
| Chl-a fraction | **chl_a_f** | % | How large a fraction of the phytoplankton mass is chlorophyll a |
| **Basin specific phytoplankton** | | | Distributes like: `epi` |
| Phytoplankton amenability | **phyt_a** |  | Adjustment factor to account for shallow lakes being better for plankton, after taking nutrients and light into consideration |

---

## Phytoplankton Lake

Version: 0.0.0

File: [modules/plankton.txt](https://github.com/NIVANorge/Mobius2/tree/main/models/modules/plankton.txt)

### Description

A phytoplankton module that is based on non-exponential growth. This is more stable and easier to calibrate, but can some times miss growth peaks.

Authors: Magnus Dahler Norling, François Clayer

### External symbols

| Name | Symbol | Type |
| ---- | ------ | ---- |
| Phytoplankton | **phyt** | quantity |
| Epilimnion | **layer** | compartment |
| Organic nitrogen | **on** | quantity |
| Water | **water** | quantity |
| Organic carbon | **oc** | quantity |
| Temperature | **temp** | property |
| Organic phosphorous | **op** | quantity |
| O₂ | **o2** | quantity |
| Inorganic nitrogen | **din** | quantity |
| Inorganic phosphorous | **phos** | quantity |
| Particles | **sed** | quantity |
| Chlorophyll-a | **chl_a** | property |
| Shortwave radiation | **sw** | property |

### State variables

#### **Layer phytoplankton**

Location: **layer.water.phyt**

Unit: kg

Conc. unit: mg l⁻¹

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
\mathrm{cmol} = \left(\mathrm{phyt\_cn}\cdot \frac{\mathrm{conc}\left(\mathrm{water}.\mathrm{din}\right)}{\mathrm{n\_mol\_mass}}\rightarrow \mathrm{mmol}\,\mathrm{m}^{-3}\,\right) \\ \frac{\mathrm{cmol}^{2}}{\left(\frac{\mathrm{alpha}}{\mathrm{phyt\_cn}}\right)^{2}+\mathrm{cmol}^{2}}
$$

#### **Phosphorus limitation**

Location: **layer.water.phyt.P_lim**

Unit: 

Value:

$$
\mathrm{cmol} = \left(\frac{\mathrm{conc}\left(\mathrm{water}.\mathrm{phos}\right)}{\mathrm{p\_mol\_mass}}\rightarrow \mathrm{mmol}\,\mathrm{m}^{-3}\,\right) \\ \frac{\mathrm{cmol}^{2}}{\left(\frac{\mathrm{alpha}}{\mathrm{phyt\_cp}}\right)^{2}+\mathrm{cmol}^{2}}
$$

#### **Phytoplankton equilibrium concentration**

Location: **layer.water.phyt.equi**

Unit: mg l⁻¹

Value:

$$
\mathrm{phyt\_eq} = \mathrm{phyt\_a}\cdot \frac{\href{stdlib.html#response}{\mathrm{q10\_adjust}}\left(\mathrm{phyt\_eq\_20},\, 20 \mathrm{°C}\,,\, \mathrm{temp},\, \mathrm{phyt\_q10}\right)}{\left(\mathrm{chl\_a\_f}\rightarrow 1\right)} \\ \mathrm{phyt\_eq}\cdot \mathrm{min}\left(\mathrm{light\_lim},\, \mathrm{min}\left(\mathrm{N\_lim},\, \mathrm{P\_lim}\right)\right)
$$

#### **Photosynthetic C fixation**

Location: **layer.water.phyt.fix**

Unit: kg day⁻¹

Value:

$$
\left(\mathrm{phyt\_turnover}\cdot \mathrm{equi}\cdot \mathrm{water}\rightarrow \mathrm{kg}\,\mathrm{day}^{-1}\,\right)
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
\mathrm{wd} = 0.5 \\ \mathrm{o2\_factor} = \href{stdlib.html#response}{\mathrm{s\_response}}\left(\mathrm{conc}\left(\mathrm{o2}\right),\, \left(1-\mathrm{wd}\right)\cdot \mathrm{phyt\_ox\_th},\, \left(1+\mathrm{wd}\right)\cdot \mathrm{phyt\_ox\_th},\, \mathrm{phyt\_death\_anox},\, 1\right) \\ \mathrm{phyt\_turnover}\cdot \mathrm{phyt}\cdot \mathrm{o2\_factor}
$$

#### **Layer O₂ photosynthesis**

Source: out

Target: layer.water.o2

Unit: kg day⁻¹

Value:

$$
\mathrm{aggregate}\left(\mathrm{phyt}.\mathrm{fix}\right)\cdot \frac{\mathrm{o2\_mol\_mass}}{\mathrm{c\_mol\_mass}}
$$

---

## EasyChem

Version: 0.0.6

File: [modules/easylake/easychem_new.txt](https://github.com/NIVANorge/Mobius2/tree/main/models/modules/easylake/easychem_new.txt)

### Description

This is a simple lake biogeochemical model for CNP and O₂ made to fit with EasyLake.

Includes microbial retention.

Many of the equations are inspired by [Selma](https://github.com/fabm-model/fabm/tree/master/src/models/selma), but with simplifications. 

Authors: François Clayer, Magnus D. Norling

### External symbols

| Name | Symbol | Type |
| ---- | ------ | ---- |
| Phytoplankton | **phyt** | quantity |
| Total organic carbon | **toc** | property |
| O₂ saturation concentration | **o2satconc** | property |
| Epilimnion | **epi** | compartment |
| Hypolimnion | **hyp** | compartment |
| Organic nitrogen | **on** | quantity |
| Water | **water** | quantity |
| Temperature | **temp** | property |
| Organic carbon | **oc** | quantity |
| O₂ | **o2** | quantity |
| Inorganic nitrogen | **din** | quantity |
| Total nitrogen | **tn** | property |
| Inorganic phosphorous | **phos** | quantity |
| Organic phosphorous | **op** | quantity |
| Particles | **sed** | quantity |
| Total dissolved phosphorous | **tdp** | property |
| Shortwave radiation | **sw** | property |
| Total phosphorous | **tp** | property |
| Chlorophyll-a | **chl_a** | property |
| Surface area | **area** | property |

### Parameters

| Name | Symbol | Unit |  Description |
| ---- | ------ | ---- |  ----------- |
| **Lake specific chemistry** | | | Distributes like: `epi` |
| Initial O₂ saturation | **init_O2** |  |  |
| Initial lake DOC concentration | **init_c** | mg l⁻¹ |  |
| Initial lake DIN concentration | **init_in** | mg l⁻¹ |  |
| Initial lake DIP concentration | **init_ip** | mg l⁻¹ |  |
| Direct lake DIN deposition | **din_dep** | kg ha⁻¹ year⁻¹ |  |
| **Oxygen** | | | Distributes like: `epi` |
| Piston velocity scaler for O₂ | **pvel_scaler** |  |  |
| Background sediment O₂ demand | **sod** | g m⁻² day⁻¹ |  |
| **Microbes** | | |  |
| Respiration rate | **K_OM** | year⁻¹ | Rate at 20°C |
| Respiration Q10 | **respQ10** |  | Adjustment of rate with 10°C change in temperature |
| Half-saturation concentration O₂ | **Km_o2** | mmol m⁻³ |  |
| P mineralization rel rate | **relrate_p** |  |  |
| N mineralization rel rate | **relrate_n** |  |  |
| Denitrification rate | **denit** | m⁻² year⁻¹ | Rate at 20°C |
| Denitrification Q10 | **denitQ10** |  | Adjustment of rate with 10°C change in temperature |
| **Lake specific denitrification** | | | Distributes like: `epi` |
| Lake specific denitrification | **denit_a** |  |  |

### State variables

#### **Epilimnion O₂**

Location: **epi.water.o2**

Unit: kg

Conc. unit: mg l⁻¹

Initial value (concentration):

$$
\left(\href{stdlib.html#sea-oxygen}{\mathrm{o2\_saturation\_concentration}}\left(\mathrm{temp},\, 0\right)\cdot \mathrm{init\_O2}\cdot \mathrm{o2\_mol\_mass}\rightarrow \mathrm{kg}\,\right)
$$

#### **Hypolimnion O₂**

Location: **hyp.water.o2**

Unit: kg

Conc. unit: mg l⁻¹

Initial value (concentration):

$$
\left(\href{stdlib.html#sea-oxygen}{\mathrm{o2\_saturation\_concentration}}\left(\mathrm{temp},\, 0\right)\cdot \mathrm{init\_O2}\cdot \mathrm{o2\_mol\_mass}\rightarrow \mathrm{kg}\,\right)
$$

#### **Epilimnion DOC**

Location: **epi.water.oc**

Unit: kg

Conc. unit: mg l⁻¹

Initial value (concentration):

$$
\mathrm{init\_c}
$$

#### **Hypolimnion DOC**

Location: **hyp.water.oc**

Unit: kg

Conc. unit: mg l⁻¹

Initial value (concentration):

$$
\mathrm{init\_c}
$$

#### **Epilimnion POC**

Location: **epi.water.sed.oc**

Unit: kg

#### **Hypolimnion POC**

Location: **hyp.water.sed.oc**

Unit: kg

#### **Epilimnion DIN**

Location: **epi.water.din**

Unit: kg

Conc. unit: mg l⁻¹

Initial value (concentration):

$$
\mathrm{init\_in}
$$

#### **Hypolimnion DIN**

Location: **hyp.water.din**

Unit: kg

Conc. unit: mg l⁻¹

Initial value (concentration):

$$
\mathrm{init\_in}
$$

#### **Epilimnion DON**

Location: **epi.water.on**

Unit: kg

Conc. unit: mg l⁻¹

Initial value (concentration):

$$
\frac{\mathrm{init\_c}}{\href{stdlib.html#chemistry}{\mathrm{cn\_molar\_to\_mass\_ratio}}\left(\mathrm{om\_cn}\right)}
$$

#### **Hypolimnion DON**

Location: **hyp.water.on**

Unit: kg

Conc. unit: mg l⁻¹

Initial value (concentration):

$$
\frac{\mathrm{init\_c}}{\href{stdlib.html#chemistry}{\mathrm{cn\_molar\_to\_mass\_ratio}}\left(\mathrm{om\_cn}\right)}
$$

#### **Epilimnion PON**

Location: **epi.water.sed.on**

Unit: kg

#### **Hypolimnion PON**

Location: **hyp.water.sed.on**

Unit: kg

#### **Epilimnion DIP**

Location: **epi.water.phos**

Unit: kg

Conc. unit: mg l⁻¹

Initial value (concentration):

$$
\mathrm{init\_ip}
$$

#### **Hypolimnion DIP**

Location: **hyp.water.phos**

Unit: kg

Conc. unit: mg l⁻¹

Initial value (concentration):

$$
\mathrm{init\_ip}
$$

#### **Epilimnion DOP**

Location: **epi.water.op**

Unit: kg

Conc. unit: mg l⁻¹

Initial value (concentration):

$$
\frac{\mathrm{init\_c}}{\href{stdlib.html#chemistry}{\mathrm{cp\_molar\_to\_mass\_ratio}}\left(\mathrm{om\_cp}\right)}
$$

#### **Hypolimnion DOP**

Location: **hyp.water.op**

Unit: kg

Conc. unit: mg l⁻¹

Initial value (concentration):

$$
\frac{\mathrm{init\_c}}{\href{stdlib.html#chemistry}{\mathrm{cp\_molar\_to\_mass\_ratio}}\left(\mathrm{om\_cp}\right)}
$$

#### **Epilimnion PIP**

Location: **epi.water.sed.phos**

Unit: kg

#### **Hypolimnion PIP**

Location: **hyp.water.sed.phos**

Unit: kg

#### **Epilimnion POP**

Location: **epi.water.sed.op**

Unit: kg

#### **Hypolimnion POP**

Location: **hyp.water.sed.op**

Unit: kg

#### **Hypolimnion Phytoplankton**

Location: **hyp.water.phyt**

Unit: kg

Conc. unit: mg l⁻¹

#### **O₂ saturation concentration**

Location: **epi.water.o2satconc**

Unit: mg l⁻¹

Value:

$$
\left(\href{stdlib.html#sea-oxygen}{\mathrm{o2\_saturation\_concentration}}\left(\mathrm{temp},\, 0\right)\cdot \mathrm{o2\_mol\_mass}\rightarrow \mathrm{mg}\,\mathrm{l}^{-1}\,\right)
$$

#### **Sediment O₂ consumption rate**

Location: **epi.o2rate**

Unit: kg m⁻² day⁻¹

Value:

$$
\mathrm{q10} = 2 \\ \mathrm{rate20} = \left(\mathrm{sod}\cdot \mathrm{min}\left(1,\, 1-\frac{0.05 \mathrm{mg}\,\mathrm{l}^{-1}\,-\mathrm{conc}\left(\mathrm{water}.\mathrm{o2}\right)}{0.05 \mathrm{mg}\,\mathrm{l}^{-1}\,}\right)\rightarrow \mathrm{kg}\,\mathrm{m}^{-2}\,\mathrm{day}^{-1}\,\right) \\ \href{stdlib.html#response}{\mathrm{q10\_adjust}}\left(\mathrm{rate20},\, 20 \mathrm{°C}\,,\, \mathrm{hyp}.\mathrm{water}.\mathrm{temp},\, \mathrm{q10}\right)
$$

#### **Bacterial mineralization (epi)**

Location: **epi.water.resp**

Unit: day⁻¹

This series is externally defined. It may be an input series.

#### **Bacterial mineralization (hypo)**

Location: **hyp.water.resp**

Unit: day⁻¹

This series is externally defined. It may be an input series.

#### **Epilimnion TDP**

Location: **epi.water.tdp**

Unit: mg l⁻¹

Value:

$$
\mathrm{conc}\left(\mathrm{phos}\right)+\mathrm{conc}\left(\mathrm{op}\right)
$$

#### **Epilimnion TP**

Location: **epi.water.tp**

Unit: mg l⁻¹

Value:

$$
\mathrm{tdp}+\mathrm{conc}\left(\mathrm{sed}\right)\cdot \left(\mathrm{conc}\left(\mathrm{sed}.\mathrm{phos}\right)+\mathrm{conc}\left(\mathrm{sed}.\mathrm{op}\right)\right)
$$

#### **Epilimnion TN**

Location: **epi.water.tn**

Unit: mg l⁻¹

Value:

$$
\mathrm{conc}\left(\mathrm{on}\right)+\mathrm{conc}\left(\mathrm{din}\right)+\mathrm{conc}\left(\mathrm{sed}\right)\cdot \mathrm{conc}\left(\mathrm{sed}.\mathrm{on}\right)
$$

#### **Epilimnion TOC**

Location: **epi.water.toc**

Unit: mg l⁻¹

Value:

$$
\mathrm{conc}\left(\mathrm{oc}\right)+\mathrm{conc}\left(\mathrm{sed}.\mathrm{oc}\right)\cdot \mathrm{conc}\left(\mathrm{sed}\right)+\mathrm{conc}\left(\mathrm{phyt}\right)
$$

#### **Epilimnion STS**

Location: **epi.water.sts**

Unit: mg l⁻¹

Value:

$$
\mathrm{conc}\left(\mathrm{epi}.\mathrm{water}.\mathrm{sed}\right)+\mathrm{conc}\left(\mathrm{phyt}\right)
$$

### Fluxes

#### **O₂ sediment consumption (epi)**

Source: epi.water.o2

Target: out

Unit: kg day⁻¹

Value:

$$
\left(\mathrm{epi}.\mathrm{o2rate}\cdot \left(\mathrm{area}-\mathrm{hyp}.\mathrm{area}\right)\rightarrow \mathrm{kg}\,\mathrm{day}^{-1}\,\right)
$$

#### **Lake N deposition**

Source: out

Target: epi.water.din

Unit: kg day⁻¹

Value:

$$
\left(\mathrm{area}\cdot \frac{\mathrm{din\_dep}}{365.25 \mathrm{day}\,\mathrm{year}^{-1}\,}\rightarrow \mathrm{kg}\,\mathrm{day}^{-1}\,\right)
$$

#### **DOC bacterial mineralization (hypo)**

Source: hyp.water.oc

Target: out

Unit: kg day⁻¹

Value:

$$
\mathrm{resp}\cdot \mathrm{oc}
$$

#### **DOC bacterial mineralization (epi)**

Source: epi.water.oc

Target: out

Unit: kg day⁻¹

Value:

$$
\mathrm{resp}\cdot \mathrm{oc}
$$

#### **O₂ bacterial consumption (hypo)**

Source: hyp.water.o2

Target: out

Unit: kg day⁻¹

Value:

$$
\mathrm{resp}\cdot \mathrm{oc}\cdot \frac{\mathrm{o2\_mol\_mass}}{\mathrm{c\_mol\_mass}}
$$

#### **O₂ bacterial consumption (epi)**

Source: epi.water.o2

Target: out

Unit: kg day⁻¹

Value:

$$
\mathrm{resp}\cdot \mathrm{oc}\cdot \frac{\mathrm{o2\_mol\_mass}}{\mathrm{c\_mol\_mass}}
$$

#### **DOP mineralization (epi)**

Source: epi.water.op

Target: epi.water.phos

Unit: kg day⁻¹

Value:

$$
\mathrm{resp}\cdot \mathrm{op}\cdot \mathrm{relrate\_p}
$$

#### **DOP mineralization (hyp)**

Source: hyp.water.op

Target: epi.water.phos

Unit: kg day⁻¹

Value:

$$
\mathrm{resp}\cdot \mathrm{op}\cdot \mathrm{relrate\_p}
$$

#### **POP mineralization (epi)**

Source: epi.water.sed.op

Target: epi.water.phos

Unit: kg day⁻¹

Value:

$$
\mathrm{resp}\cdot \mathrm{sed}.\mathrm{op}\cdot \mathrm{relrate\_p}
$$

#### **POP mineralization (hyp)**

Source: hyp.water.sed.op

Target: hyp.water.phos

Unit: kg day⁻¹

Value:

$$
\mathrm{resp}\cdot \mathrm{sed}.\mathrm{op}\cdot \mathrm{relrate\_p}
$$

#### **DON mineralization (hyp)**

Source: hyp.water.on

Target: hyp.water.din

Unit: kg day⁻¹

Value:

$$
\mathrm{resp}\cdot \mathrm{on}\cdot \mathrm{relrate\_n}
$$

#### **DON mineralization (epi)**

Source: epi.water.on

Target: epi.water.din

Unit: kg day⁻¹

Value:

$$
\mathrm{resp}\cdot \mathrm{on}\cdot \mathrm{relrate\_n}
$$

#### **DIN sediment denitrification (epi)**

Source: epi.water.din

Target: out

Unit: kg day⁻¹

Value:

$$
\mathrm{denit\_a}\cdot \mathrm{din}\cdot \href{stdlib.html#response}{\mathrm{q10\_adjust}}\left(\mathrm{denit},\, 20 \mathrm{°C}\,,\, \mathrm{temp},\, \mathrm{denitQ10}\right)\cdot \frac{\mathrm{area}-\mathrm{hyp}.\mathrm{area}}{365 \mathrm{day}\,\mathrm{year}^{-1}\,}
$$

#### **DIN sediment denitrification (hyp)**

Source: hyp.water.din

Target: out

Unit: kg day⁻¹

Value:

$$
\mathrm{denit\_a}\cdot \mathrm{din}\cdot \href{stdlib.html#response}{\mathrm{q10\_adjust}}\left(\mathrm{denit},\, 20 \mathrm{°C}\,,\, \mathrm{temp},\, \mathrm{denitQ10}\right)\cdot \frac{\mathrm{area}}{365 \mathrm{day}\,\mathrm{year}^{-1}\,}
$$

#### **Phytoplankton death (hyp)**

Source: hyp.water.phyt

Target: hyp.water.oc

Unit: kg day⁻¹

Value:

$$
\mathrm{wd} = 0.5 \\ \mathrm{o2\_factor} = \href{stdlib.html#response}{\mathrm{s\_response}}\left(\mathrm{conc}\left(\mathrm{o2}\right),\, \left(1-\mathrm{wd}\right)\cdot \mathrm{phyt\_ox\_th},\, \left(1+\mathrm{wd}\right)\cdot \mathrm{phyt\_ox\_th},\, \mathrm{phyt\_death\_anox},\, 1\right) \\ \mathrm{phyt\_turnover}\cdot \mathrm{phyt}\cdot \mathrm{o2\_factor}
$$

---

## EasyChem-Particulate

Version: 0.0.3

File: [modules/easylake/easychem_new.txt](https://github.com/NIVANorge/Mobius2/tree/main/models/modules/easylake/easychem_new.txt)

### Description

Particle transport and settling for EasyChem.

Authors: Magnus D. Norling

### External symbols

| Name | Symbol | Type |
| ---- | ------ | ---- |
| Atmosphere | **air** | compartment |
| Epilimnion | **epi** | compartment |
| Hypolimnion | **hyp** | compartment |
| Water | **water** | quantity |
| Particles | **sed** | quantity |
| Wind speed | **wind** | property |
| Surface area | **area** | property |
| Downstream | **downstream** | connection |
|  | **sett_target** | loc |

### Parameters

| Name | Symbol | Unit |  Description |
| ---- | ------ | ---- |  ----------- |
| **Particles** | | |  |
| Sediment settling velocity | **sett_vel** | m day⁻¹ | The net settling velocity after taking into account mixing whithin (but not between) each compartment. |
| Resuspension factor | **resusp_f** |  | Wind-proportional resuspension in epilimnion-sediment interface. |
| Sediment concentration of fast settling | **maxconc** | mg l⁻¹ | With suspended sediment concentrations in the river higher than this, it is assumed to contain larger grain sizes that settle faster |

### State variables

#### **Epilimnion suspended sediments**

Location: **epi.water.sed**

Unit: kg

Conc. unit: mg l⁻¹

#### **Hypolimnion suspended sediments**

Location: **hyp.water.sed**

Unit: kg

Conc. unit: mg l⁻¹

### Fluxes

#### **Epilimnion sediment settling**

Source: epi.water.sed

Target: sett_target

Unit: kg day⁻¹

Value:

$$
\left(\mathrm{max}\left(0 \mathrm{m}\,\mathrm{day}^{-1}\,,\, \mathrm{sett\_vel}-\left(\mathrm{air}.\mathrm{wind}\cdot \mathrm{resusp\_f}\rightarrow \mathrm{m}\,\mathrm{day}^{-1}\,\right)\right)\cdot \mathrm{conc}\left(\mathrm{sed}\right)\cdot \left(\mathrm{area}-\mathrm{hyp}.\mathrm{area}\right)\rightarrow \mathrm{kg}\,\mathrm{day}^{-1}\,\right)
$$

#### **Epilimnion-hypolimnion settling**

Source: epi.water.sed

Target: hyp.water.sed

Unit: kg day⁻¹

Value:

$$
\left(\mathrm{sett\_vel}\cdot \mathrm{conc}\left(\mathrm{sed}\right)\cdot \mathrm{hyp}.\mathrm{area}\rightarrow \mathrm{kg}\,\mathrm{day}^{-1}\,\right)
$$

#### **Hypolimnion sediment settling**

Source: hyp.water.sed

Target: sett_target

Unit: kg day⁻¹

Value:

$$
\left(\mathrm{sett\_vel}\cdot \mathrm{conc}\left(\mathrm{sed}\right)\cdot \mathrm{area}\rightarrow \mathrm{kg}\,\mathrm{day}^{-1}\,\right)
$$

#### **Epilimnion SS inlet settling**

Source: epi.water.sed

Target: sett_target

Unit: kg day⁻¹

Value:

$$
\mathrm{in\_s} = \mathrm{in\_flux}\left(\mathrm{downstream},\, \mathrm{epi}.\mathrm{water}.\mathrm{sed}\right) \\ \mathrm{in\_q} = \mathrm{in\_flux}\left(\mathrm{downstream},\, \mathrm{epi}.\mathrm{water}\right) \\ \mathrm{in\_conc} = \left(\href{stdlib.html#basic}{\mathrm{safe\_divide}}\left(\mathrm{in\_s},\, \mathrm{in\_q}\right)\rightarrow \mathrm{mg}\,\mathrm{l}^{-1}\,\right) \\ \mathrm{excess} = \mathrm{max}\left(0,\, \mathrm{in\_conc}-\mathrm{maxconc}\right) \\ \left(\mathrm{max}\left(0,\, \mathrm{in\_conc}-\mathrm{maxconc}\right)\cdot \mathrm{in\_q}\rightarrow \mathrm{kg}\,\mathrm{day}^{-1}\,\right)
$$



{% include lib/mathjax.html %}

