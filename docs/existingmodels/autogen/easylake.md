---
layout: default
title: EasyLake
parent: Mathematical description
grand_parent: Existing models
nav_order: 1
---

# EasyLake

This is auto-generated documentation based on the model code in [models/easylake_simplycnp_model.txt](https://github.com/NIVANorge/Mobius2/blob/main/models/easylake_simplycnp_model.txt) .
Since the modules can be dynamically loaded with different arguments, this documentation does not necessarily reflect all use cases of the modules.

See the note on [notation](autogen.html#notation).

The file was generated at 2024-05-06 13:40:56.

---

## EasyLake

Version: 0.1.0

File: [modules/easylake.txt](https://github.com/NIVANorge/Mobius2/tree/main/models/modules/easylake.txt)

### Description

EasyLake is a simple lake model for use along with catchment models. It simulates residence time in the lake in an upper and lower compartment, and it has separate modules for nutrient retention and contaminants.

The physical part of the model simulates water balance and temperature.

The lake can also be equipped with an ice module (defined in the AirSea module).

The internal heat distribution is inspired by (but not as complex as) the [FLake model](http://www.flake.igb-berlin.de/).

We assume that the hypsograph of the lake follows a shape so that

$$
A(z) = A_0 \left(\frac{z}{z_{max}}\right)^{\theta+1}
$$

where $$A(z)$$ is the area of the horizontal cross-section of the lake at level $$z$$ measuring up from the maximal depth $$z_{max}$$, $$A_0$$ is the surface area at the zero level of the outlet, and $$\theta$$ is a user-defined parameter.

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
| Ice formation temperature | **freeze_temp** | property |
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
\left(2\cdot \mathrm{T\_b}+\left(\mathrm{theta}+2\right)\cdot \mathrm{T\_e}\right)\cdot \mathrm{A\_0}\cdot \frac{\mathrm{z\_e}^{3}\cdot \left(\frac{\mathrm{z\_e}}{\mathrm{z\_0}}\right)^{\mathrm{theta}+1}}{\left(\mathrm{theta}^{2}+6\cdot \mathrm{theta}+8\right)\cdot \mathrm{z\_e}^{2}}
$$

### Parameters

| Name | Symbol | Unit |  Description |
| ---- | ------ | ---- |  ----------- |
| **Lake physical** | | | |
| Lake surface area | **A_surf** | m² |  |
| Water level at which outflow is 0 | **z_outflow** | m |  |
| Winter epilimnion thickness | **th_epi_w** | m |  |
| Spring epilimnion thickness | **th_epi_s** | m |  |
| Lake bathymetry factor | **theta** |  | The cross-section area of the lake at level z is A_0*(z/z_0)^theta |
| Epilimnion thickening rate | **dz_epi** | m day⁻¹ | How fast the thickness of the epilimnion changes during summer |
| Rating function linear component | **rate_l** | m² s⁻¹ |  |
| Initial epilimnion temperature | **t_epi** | °C |  |
| Bottom temperature | **t_bot** | °C |  |

### State variables

#### **Ice formation temperature**

Location: **epi.freeze_temp**

Unit: °C

Value:

$$
0 \mathrm{°C}\,
$$

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
\mathrm{A\_surf}\cdot \mathrm{epi}.\mathrm{th}\cdot \mathrm{epi}.\mathrm{ind}\cdot 0.1 \mathrm{day}^{-1}\,
$$

---

## AirSea Lake

Version: 0.1.1

File: [modules/airsea.txt](https://github.com/NIVANorge/Mobius2/tree/main/models/modules/airsea.txt)

### Description

Air-sea/lake heat fluxes are based off of Kondo 1975

The implementation used here is influenced by the implementation in [GOTM](https://github.com/gotm-model).

The ice module is influenced by the ice module in MyLake, and uses Stefan's law for ice accumulation.

Air-Sea bulk transfer coefficients in diabatic conditions, Junsei Kondo, 1975, Boundary-Layer Meteorology 9(1), 91-112 [https://doi.org/10.1007/BF00232256](https://doi.org/10.1007/BF00232256)

MyLake—A multi-year lake simulation model code suitable for uncertainty and sensitivity analysis simulations, Tuomo M. Saloranta and Tom Andersen 2007, Ecological Modelling 207(1), 45-60, [https://doi.org/10.1016/j.ecolmodel.2007.03.018](https://doi.org/10.1016/j.ecolmodel.2007.03.018)

Authors: Magnus D. Norling

### External symbols

| Name | Symbol | Type |
| ---- | ------ | ---- |
| Atmosphere | **air** | compartment |
| Cosine of the solar zenith angle | **cos_z** | property |
| Epilimnion | **surf** | compartment |
| Actual specific humidity | **a_hum** | property |
| Density | **rho** | property |
| Ice | **ice** | quantity |
| Heat energy | **heat** | quantity |
| Temperature | **temp** | property |
| Wind speed | **wind** | property |
| Precipitation | **precip** | property |
| Global radiation | **g_rad** | property |
| Pressure | **pressure** | property |
| Downwelling longwave radiation | **lwd** | property |
| Shortwave radiation | **sw** | property |
| Attenuation coefficient | **attn** | property |
| Ice formation temperature | **freeze_temp** | property |
| Ice indicator | **indicator** | property |
| Evaporation | **evap** | property |
|  | **A_surf** | loc |
|  | **top_water** | loc |
|  | **top_water_heat_sw** | loc |

### Constants

| Name | Symbol | Unit | Value |
| ---- | ------ | ---- | ----- |
| Ice density | **rho_ice** | kg m⁻³ | 917 |
| Latent heat of freezing | **l_freeze** | J kg⁻¹ | 333500 |
| Ice heat conduction coefficient | **lambda_ice** | W m⁻¹ K⁻¹ | 2.1 |
| Water albedo | **water_alb** |  | 0.045 |

### Parameters

| Name | Symbol | Unit |  Description |
| ---- | ------ | ---- |  ----------- |
| **Ice** | | | |
| Initial ice thickness | **init_ice** | m |  |
| Ice albedo | **ice_alb** |  |  |
| Ice attenuation coefficient | **ice_att_c** | m⁻¹ |  |
| Frazil threshold | **th_frazil** | m |  |

### State variables

#### **Wind speed**

Location: **air.wind**

Unit: m s⁻¹

This series is externally defined. It may be an input series.

#### **Stability**

Location: **surf.stab**

Unit: 

Value:

$$
\href{stdlib.html#air-sea}{\mathrm{surface\_stability}}\left(\mathrm{air}.\mathrm{wind},\, \mathrm{top\_water}.\mathrm{temp},\, \mathrm{air}.\mathrm{temp}\right)
$$

#### **Transfer coefficient for latent heat flux**

Location: **surf.ced**

Unit: 

Value:

$$
\href{stdlib.html#air-sea}{\mathrm{tc\_latent\_heat}}\left(\mathrm{air}.\mathrm{wind},\, \mathrm{stab}\right)
$$

#### **Transfer coefficent for sensible heat flux**

Location: **surf.chd**

Unit: 

Value:

$$
\href{stdlib.html#air-sea}{\mathrm{tc\_sensible\_heat}}\left(\mathrm{air}.\mathrm{wind},\, \mathrm{stab}\right)
$$

#### **Saturation specific humidity**

Location: **surf.s_hum**

Unit: 

Value:

$$
\mathrm{svap} = \href{stdlib.html#meteorology}{\mathrm{saturation\_vapor\_pressure}}\left(\mathrm{top\_water}.\mathrm{temp}\right) \\ \href{stdlib.html#meteorology}{\mathrm{specific\_humidity\_from\_pressure}}\left(\mathrm{air}.\mathrm{pressure},\, \mathrm{svap}\right)
$$

#### **Emitted longwave radiation**

Location: **surf.lwu**

Unit: W m⁻²

Value:

$$
\mathrm{emissivity} = 0.98 \\ \mathrm{emissivity}\cdot \href{stdlib.html#thermodynamics}{\mathrm{black\_body\_radiation}}\left(\left(\mathrm{top\_water}.\mathrm{temp}\rightarrow \mathrm{K}\,\right)\right)
$$

#### **Albedo**

Location: **surf.albedo**

Unit: 

Value:

$$
\begin{cases}\mathrm{ice\_alb} & \text{if}\;\mathrm{ice}.\mathrm{indicator} \\ \mathrm{water\_alb} & \text{otherwise}\end{cases}
$$

#### **Shortwave radiation**

Location: **surf.sw**

Unit: W m⁻²

Value:

$$
\left(1-\mathrm{albedo}\right)\cdot \left(1-\mathrm{ice}.\mathrm{attn}\right)\cdot \mathrm{air}.\mathrm{g\_rad}
$$

#### **Evaporation**

Location: **surf.evap**

Unit: mm day⁻¹

Value:

$$
\mathrm{rho\_ref} = 1025 \mathrm{kg}\,\mathrm{m}^{-3}\, \\ \left(\;\text{not}\;\mathrm{surf}.\mathrm{ice}.\mathrm{indicator}\cdot \frac{\mathrm{air}.\mathrm{rho}}{\mathrm{rho\_ref}}\cdot \mathrm{chd}\cdot \mathrm{air}.\mathrm{wind}\cdot \left(\mathrm{s\_hum}-\mathrm{air}.\mathrm{a\_hum}\right)\rightarrow \mathrm{mm}\,\mathrm{day}^{-1}\,\right)
$$

#### **Ice thickness**

Location: **surf.ice**

Unit: m

Initial value:

$$
\mathrm{init\_ice}
$$

#### **Freeze energy**

Location: **surf.ice.energy**

Unit: W m⁻²

Value:

$$
\mathrm{z\_surf} = 1 \mathrm{m}\, \\ \mathrm{K\_ice} = 200 \mathrm{W}\,\mathrm{m}^{-3}\,\mathrm{°C}^{-1}\, \\ \mathrm{e} = \left(\mathrm{freeze\_temp}-\mathrm{top\_water}.\mathrm{temp}\right)\cdot \mathrm{z\_surf}\cdot \mathrm{K\_ice} \\ \begin{cases}0 & \text{if}\;\mathrm{ice}<10^{-6} \mathrm{m}\,\;\text{and}\;\mathrm{e}<0 \\ \mathrm{e} & \text{otherwise}\end{cases}
$$

#### **Ice indicator**

Location: **surf.ice.indicator**

Unit: 

Value:

$$
\mathrm{ice}>\mathrm{th\_frazil}
$$

#### **Attenuation coefficient**

Location: **surf.ice.attn**

Unit: 

Value:

$$
\mathrm{cz} = \mathrm{max}\left(0.01,\, \href{stdlib.html#radiation}{\mathrm{refract}}\left(\mathrm{air}.\mathrm{cos\_z},\, \mathrm{refraction\_index\_ice}\right)\right) \\ \mathrm{th} = \frac{\mathrm{ice}}{\mathrm{cz}} \\ \begin{cases}0 & \text{if}\;\;\text{not}\;\mathrm{indicator} \\ 1-e^{-\mathrm{ice\_att\_c}\cdot \mathrm{th}} & \text{otherwise}\end{cases}
$$

#### **Ice surface temperature**

Location: **surf.ice.temp**

Unit: °C

Value:

$$
\mathrm{alpha} = \frac{1}{10 \mathrm{m}^{-1}\,\cdot \mathrm{ice}} \\ \begin{cases}\frac{\mathrm{alpha}\cdot \mathrm{freeze\_temp}+\mathrm{air}.\mathrm{temp}}{1+\mathrm{alpha}} & \text{if}\;\mathrm{indicator} \\ 0 & \text{otherwise}\end{cases}
$$

### Fluxes

#### **Net shortwave**

Source: out

Target: top_water_heat_sw

Unit: J day⁻¹

Value:

$$
\left(\mathrm{A\_surf}\cdot \mathrm{surf}.\mathrm{sw}\rightarrow \mathrm{J}\,\mathrm{day}^{-1}\,\right)
$$

#### **Net longwave**

Source: out

Target: top_water.heat

Unit: J day⁻¹

Value:

$$
\mathrm{net\_rad} = \left(1-\mathrm{surf}.\mathrm{albedo}\right)\cdot \mathrm{air}.\mathrm{lwd}-\mathrm{surf}.\mathrm{lwu} \\ \left(\;\text{not}\;\mathrm{surf}.\mathrm{ice}.\mathrm{indicator}\cdot \mathrm{A\_surf}\cdot \mathrm{net\_rad}\rightarrow \mathrm{J}\,\mathrm{day}^{-1}\,\right)
$$

#### **Freeze heating**

Source: out

Target: top_water.heat

Unit: J day⁻¹

Value:

$$
\left(\mathrm{A\_surf}\cdot \mathrm{surf}.\mathrm{ice}.\mathrm{energy}\rightarrow \mathrm{J}\,\mathrm{day}^{-1}\,\right)
$$

#### **Latent heat flux**

Source: out

Target: top_water.heat

Unit: J day⁻¹

Value:

$$
\mathrm{l\_vap} = \href{stdlib.html#meteorology}{\mathrm{latent\_heat\_of\_vaporization}}\left(\mathrm{top\_water}.\mathrm{temp}\right) \\ \left(\;\text{not}\;\mathrm{surf}.\mathrm{ice}.\mathrm{indicator}\cdot \mathrm{A\_surf}\cdot \mathrm{surf}.\mathrm{ced}\cdot \mathrm{l\_vap}\cdot \mathrm{air}.\mathrm{rho}\cdot \mathrm{air}.\mathrm{wind}\cdot \left(\mathrm{air}.\mathrm{a\_hum}-\mathrm{surf}.\mathrm{s\_hum}\right)\rightarrow \mathrm{J}\,\mathrm{day}^{-1}\,\right)
$$

#### **Sensible heat flux**

Source: out

Target: top_water.heat

Unit: J day⁻¹

Value:

$$
\left(\;\text{not}\;\mathrm{surf}.\mathrm{ice}.\mathrm{indicator}\cdot \mathrm{A\_surf}\cdot \mathrm{surf}.\mathrm{chd}\cdot \mathrm{C\_air}\cdot \mathrm{air}.\mathrm{rho}\cdot \mathrm{air}.\mathrm{wind}\cdot \left(\left(\mathrm{air}.\mathrm{temp}\rightarrow \mathrm{K}\,\right)-\left(\mathrm{top\_water}.\mathrm{temp}\rightarrow \mathrm{K}\,\right)\right)\rightarrow \mathrm{J}\,\mathrm{day}^{-1}\,\right)
$$

#### **Evaporation**

Source: top_water

Target: out

Unit: m³ day⁻¹

Value:

$$
\left(\mathrm{surf}.\mathrm{evap}\cdot \mathrm{A\_surf}\rightarrow \mathrm{m}^{3}\,\mathrm{day}^{-1}\,\right)
$$

#### **Precipitation to surface**

Source: out

Target: top_water

Unit: m³ day⁻¹

Value:

$$
\left(\mathrm{air}.\mathrm{precip}\cdot \mathrm{A\_surf}\rightarrow \mathrm{m}^{3}\,\mathrm{day}^{-1}\,\right)
$$

#### **Precipitation heat**

Source: out

Target: top_water.heat

Unit: J day⁻¹

Value:

$$
\mathrm{precip\_t} = \mathrm{max}\left(0 \mathrm{°C}\,,\, \mathrm{air}.\mathrm{temp}\right) \\ \mathrm{V} = \left(\mathrm{A\_surf}\cdot \mathrm{air}.\mathrm{precip}\rightarrow \mathrm{m}^{3}\,\mathrm{day}^{-1}\,\right) \\ \left(\href{stdlib.html#water-utils}{\mathrm{water\_temp\_to\_heat}}\left(\left(\mathrm{V}\Rightarrow \mathrm{m}^{3}\,\right),\, \mathrm{precip\_t}\right)\Rightarrow \mathrm{J}\,\mathrm{day}^{-1}\,\right)
$$

#### **Ice change**

Source: out

Target: surf.ice

Unit: m day⁻¹

Value:

$$
\left(\frac{\mathrm{energy}+\begin{cases}\mathrm{lambda\_ice}\cdot \frac{\left(\mathrm{freeze\_temp}\rightarrow \mathrm{K}\,\right)-\left(\mathrm{temp}\rightarrow \mathrm{K}\,\right)}{\mathrm{ice}} & \text{if}\;\mathrm{temp}\leq \mathrm{freeze\_temp}\;\text{and}\;\mathrm{indicator} \\ -\left(1-\mathrm{albedo}\right)\cdot \mathrm{air}.\mathrm{g\_rad}\cdot \mathrm{attn} & \text{if}\;\mathrm{indicator} \\ 0 & \text{otherwise}\end{cases}}{\mathrm{rho\_ice}\cdot \mathrm{l\_freeze}}\rightarrow \mathrm{m}\,\mathrm{day}^{-1}\,\right)+\left(\begin{cases}\mathrm{air}.\mathrm{precip} & \text{if}\;\mathrm{indicator}\;\text{and}\;\mathrm{air}.\mathrm{temp}\leq \mathrm{freeze\_temp} \\ 0 & \text{otherwise}\end{cases}\rightarrow \mathrm{m}\,\mathrm{day}^{-1}\,\right)
$$

---

## EasyChem

Version: 0.0.5

File: [modules/easychem.txt](https://github.com/NIVANorge/Mobius2/tree/main/models/modules/easychem.txt)

### Description

This is a simple lake biogeochemical model for CNP and O₂ made to fit with EasyLake.

More description to be written.

Many of the equations are inspired by [Selma](https://github.com/fabm-model/fabm/tree/master/src/models/selma), but with simplifications. 

Authors: François Clayer, Magnus D. Norling

### External symbols

| Name | Symbol | Type |
| ---- | ------ | ---- |
| Atmosphere | **air** | compartment |
| Total organic carbon | **toc** | property |
| Epilimnion | **epi** | compartment |
| Hypolimnion | **hyp** | compartment |
| Organic nitrogen | **on** | quantity |
| Water | **water** | quantity |
| Wind speed | **wind** | property |
| Temperature | **temp** | property |
| Organic carbon | **oc** | quantity |
| Phytoplankton | **phyt** | quantity |
| O₂ | **o2** | quantity |
| Ice | **ice** | quantity |
| Inorganic nitrogen | **din** | quantity |
| Total nitrogen | **tn** | property |
| Inorganic phosphorous | **phos** | quantity |
| Organic phosphorous | **op** | quantity |
| Particles | **sed** | quantity |
| Precipitation | **precip** | property |
| Total dissolved phosphorous | **tdp** | property |
| Shortwave radiation | **sw** | property |
| Ice indicator | **indicator** | property |
| Total phosphorous | **tp** | property |
| Surface area | **area** | property |

### Constants

| Name | Symbol | Unit | Value |
| ---- | ------ | ---- | ----- |
| Fraction of PAR in SW radiation | **f_par** |  | 0.45 |
| Phytoplankton increased death rate from anoxicity | **phyt_death_anox** |  | 10 |
| Anoxicity threshold | **anox_threshold** | mg l⁻¹ | 2 |

### Parameters

| Name | Symbol | Unit |  Description |
| ---- | ------ | ---- |  ----------- |
| **Lake specific chemistry** | | | |
| Initial O₂ saturation | **init_O2** |  |  |
| Initial lake DOC concentration | **init_c** | mg l⁻¹ |  |
| Initial lake DIN concentration | **init_in** | mg l⁻¹ |  |
| Initial lake DIP concentration | **init_ip** | mg l⁻¹ |  |
| Initial phytoplankton concentration | **init_phyt** | mg l⁻¹ |  |
| Direct lake DIN deposition | **din_dep** | kg ha⁻¹ year⁻¹ |  |
| **Oxygen** | | | |
| Piston velocity scaler for O₂ | **pvel_scaler** |  |  |
| Background sediment O₂ demand | **sod** | g m⁻² day⁻¹ |  |
| **Microbes** | | | |
| Respiration rate | **K_OM** | year⁻¹ |  |
| Respiration Q10 | **respQ10** |  | Adjustment of rate with 10°C change in temperature |
| Half-saturation concentration O₂ | **Km_o2** | mmol m⁻³ |  |
| P mineralization rel rate | **relrate_p** |  |  |
| N mineralization rel rate | **relrate_n** |  |  |
| **Phytoplankton** | | | |
| Chl-a of phyto at equilibrium | **phyt_eq_20** | mg l⁻¹ | Assuming 20°C and no nutrient or light limitations. |
| Q10 of Phyto equilibrium | **phyt_q10** |  |  |
| Phytoplankton turnover rate | **phyt_turnover** | day⁻¹ |  |
| Excretion rate | **excr** | day⁻¹ |  |
| Optimal PAR intensity | **iopt** | W m⁻² |  |
| Half-saturation for nutrient uptake | **halfsat** | mmol m⁻³ |  |
| Molar C/N ratio in Phytoplankton | **phyt_cn** |  |  |
| Molar C/P ratio in Phytoplankton | **phyt_cp** |  |  |
| Chl-a fraction | **chl_a_f** | % | How large a fraction of the phytoplankton mass is chlorophyll a |
| **Lake specific phytoplankton** | | | |
| Phytoplankton amenability | **phyt_a** |  | Adjustment factor to account for shallow lakes being better for plankton, after taking nutrients and light into consideration |

### State variables

#### **Epilimnion O₂**

Location: **epi.water.o2**

Unit: kg

Conc. unit: mg l⁻¹

Initial value:

$$
\left(\href{stdlib.html#sea-oxygen}{\mathrm{o2\_saturation}}\left(\mathrm{temp},\, 0\right)\cdot \mathrm{init\_O2}\cdot \mathrm{o2\_mol\_mass}\rightarrow \mathrm{kg}\,\right)
$$

#### **Hypolimnion O₂**

Location: **hyp.water.o2**

Unit: kg

Conc. unit: mg l⁻¹

Initial value:

$$
\left(\href{stdlib.html#sea-oxygen}{\mathrm{o2\_saturation}}\left(\mathrm{temp},\, 0\right)\cdot \mathrm{init\_O2}\cdot \mathrm{o2\_mol\_mass}\rightarrow \mathrm{kg}\,\right)
$$

#### **Epilimnion DOC**

Location: **epi.water.oc**

Unit: kg

Conc. unit: mg l⁻¹

Initial value:

$$
\mathrm{init\_c}
$$

#### **Hypolimnion DOC**

Location: **hyp.water.oc**

Unit: kg

Conc. unit: mg l⁻¹

Initial value:

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

Initial value:

$$
\mathrm{init\_in}
$$

#### **Hypolimnion DIN**

Location: **hyp.water.din**

Unit: kg

Conc. unit: mg l⁻¹

Initial value:

$$
\mathrm{init\_in}
$$

#### **Epilimnion DON**

Location: **epi.water.on**

Unit: kg

Conc. unit: mg l⁻¹

Initial value:

$$
\frac{\mathrm{init\_c}}{\href{stdlib.html#chemistry}{\mathrm{cn\_molar\_to\_mass\_ratio}}\left(\mathrm{om\_cn}\right)}
$$

#### **Hypolimnion DON**

Location: **hyp.water.on**

Unit: kg

Conc. unit: mg l⁻¹

Initial value:

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

Initial value:

$$
\mathrm{init\_ip}
$$

#### **Hypolimnion DIP**

Location: **hyp.water.phos**

Unit: kg

Conc. unit: mg l⁻¹

Initial value:

$$
\mathrm{init\_ip}
$$

#### **Epilimnion DOP**

Location: **epi.water.op**

Unit: kg

Conc. unit: mg l⁻¹

Initial value:

$$
\frac{\mathrm{init\_c}}{\href{stdlib.html#chemistry}{\mathrm{cp\_molar\_to\_mass\_ratio}}\left(\mathrm{om\_cp}\right)}
$$

#### **Hypolimnion DOP**

Location: **hyp.water.op**

Unit: kg

Conc. unit: mg l⁻¹

Initial value:

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

#### **Epilimnion Phytoplankton**

Location: **epi.water.phyt**

Unit: kg

Conc. unit: mg l⁻¹

Initial value:

$$
\mathrm{init\_phyt}
$$

#### **Hypolimnion Phytoplankton**

Location: **hyp.water.phyt**

Unit: kg

Conc. unit: mg l⁻¹

Initial value:

$$
\mathrm{init\_phyt}
$$

#### **O₂ piston velocity**

Location: **epi.water.p_vel**

Unit: cm hr⁻¹

Value:

$$
\mathrm{pvel\_scaler}\cdot \href{stdlib.html#sea-oxygen}{\mathrm{o2\_piston\_velocity}}\left(\mathrm{air}.\mathrm{wind},\, \mathrm{temp}\right)
$$

#### **O₂ saturation concentration**

Location: **epi.water.o2sat**

Unit: mg l⁻¹

Value:

$$
\left(\href{stdlib.html#sea-oxygen}{\mathrm{o2\_saturation}}\left(\mathrm{temp},\, 0\right)\cdot \mathrm{o2\_mol\_mass}\rightarrow \mathrm{mg}\,\mathrm{l}^{-1}\,\right)
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

#### **Chlorophyll-a**

Location: **epi.water.chl_a**

Unit: mg l⁻¹

Value:

$$
\left(\mathrm{conc}\left(\mathrm{phyt}\right)\cdot \mathrm{chl\_a\_f}\rightarrow \mathrm{mg}\,\mathrm{l}^{-1}\,\right)
$$

#### **Light limitation**

Location: **epi.water.phyt.light_lim**

Unit: 

Value:

$$
\mathrm{par\_sw} = \mathrm{sw}\cdot \left(1-\mathrm{f\_par}\right) \\ \mathrm{f} = \frac{\mathrm{par\_sw}}{\mathrm{max}\left(0.5\cdot \mathrm{par\_sw},\, \mathrm{iopt}\right)} \\ \mathrm{f}\cdot e^{1-\mathrm{f}}
$$

#### **Nitrogen limitation**

Location: **epi.water.phyt.N_lim**

Unit: 

Value:

$$
\mathrm{cmol} = \left(\frac{\mathrm{conc}\left(\mathrm{water}.\mathrm{din}\right)}{\mathrm{n\_mol\_mass}}\rightarrow \mathrm{mmol}\,\mathrm{m}^{-3}\,\right) \\ \frac{\mathrm{cmol}^{2}}{\left(\frac{\mathrm{halfsat}}{\mathrm{phyt\_cn}}\right)^{2}+\mathrm{cmol}^{2}}
$$

#### **Phosphorus limitation**

Location: **epi.water.phyt.P_lim**

Unit: 

Value:

$$
\mathrm{cmol} = \left(\frac{\mathrm{conc}\left(\mathrm{water}.\mathrm{phos}\right)}{\mathrm{p\_mol\_mass}}\rightarrow \mathrm{mmol}\,\mathrm{m}^{-3}\,\right) \\ \frac{\mathrm{cmol}^{2}}{\left(\frac{\mathrm{halfsat}}{\mathrm{phyt\_cp}}\right)^{2}+\mathrm{cmol}^{2}}
$$

#### **Phytoplankton equilibrium concentration**

Location: **epi.water.phyt.equi**

Unit: mg l⁻¹

Value:

$$
\mathrm{phyt\_eq} = \frac{\href{stdlib.html#response}{\mathrm{q10\_adjust}}\left(\mathrm{phyt\_eq\_20},\, 20 \mathrm{°C}\,,\, \mathrm{temp},\, \mathrm{phyt\_q10}\right)}{\left(\mathrm{chl\_a\_f}\rightarrow 1\right)} \\ \mathrm{phyt\_eq}\cdot \mathrm{phyt\_a}\cdot \mathrm{min}\left(\mathrm{light\_lim},\, \mathrm{min}\left(\mathrm{N\_lim},\, \mathrm{P\_lim}\right)\right)
$$

#### **Photosynthetic C fixation**

Location: **epi.water.phyt.fix**

Unit: kg day⁻¹

Value:

$$
\left(\mathrm{phyt\_turnover}\cdot \mathrm{epi}.\mathrm{water}.\mathrm{phyt}.\mathrm{equi}\cdot \mathrm{water}\rightarrow \mathrm{kg}\,\mathrm{day}^{-1}\,\right)
$$

#### **Phytoplankton death rate (epi)**

Location: **epi.water.phyt.death**

Unit: kg day⁻¹

This series is externally defined. It may be an input series.

#### **Phytoplankton death rate (hyp)**

Location: **hyp.water.phyt.death**

Unit: kg day⁻¹

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
\mathrm{tdp}+\mathrm{conc}\left(\mathrm{sed}\right)\cdot \left(\mathrm{conc}\left(\mathrm{sed}.\mathrm{phos}\right)+\mathrm{conc}\left(\mathrm{sed}.\mathrm{op}\right)\right)+\frac{\mathrm{conc}\left(\mathrm{phyt}\right)}{\href{stdlib.html#chemistry}{\mathrm{cp\_molar\_to\_mass\_ratio}}\left(\mathrm{phyt\_cp}\right)}
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

#### **Precipitation O₂**

Source: out

Target: epi.water.o2

Unit: kg day⁻¹

Value:

$$
\mathrm{conc} = \left(0.9\cdot \href{stdlib.html#sea-oxygen}{\mathrm{o2\_saturation}}\left(\mathrm{air}.\mathrm{temp},\, 0\right)\cdot \mathrm{o2\_mol\_mass}\rightarrow \mathrm{mg}\,\mathrm{l}^{-1}\,\right) \\ \left(\mathrm{air}.\mathrm{precip}\cdot \mathrm{area}\cdot \mathrm{conc}\rightarrow \mathrm{kg}\,\mathrm{day}^{-1}\,\right)
$$

#### **O₂ gas exchange at surface**

Source: out

Target: epi.water.o2

Unit: kg day⁻¹

Value:

$$
\left(\;\text{not}\;\mathrm{ice}.\mathrm{indicator}\cdot \mathrm{p\_vel}\cdot \left(\mathrm{o2sat}-\mathrm{conc}\left(\mathrm{o2}\right)\right)\cdot \mathrm{area}\rightarrow \mathrm{kg}\,\mathrm{day}^{-1}\,\right)
$$

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

#### **Epilimnion photosynthetic C fixation**

Source: out

Target: epi.water.phyt

Unit: kg day⁻¹

Value:

$$
\mathrm{fix}
$$

#### **Phytoplankton death DOC release (epi)**

Source: epi.water.phyt

Target: out

Unit: kg day⁻¹

Value:

$$
\mathrm{phyt}.\mathrm{death}
$$

#### **Phytoplankton death DOC release (hypo)**

Source: hyp.water.phyt

Target: out

Unit: kg day⁻¹

Value:

$$
\mathrm{phyt}.\mathrm{death}
$$

#### **Epilimnion O₂ photosynthesis**

Source: out

Target: epi.water.o2

Unit: kg day⁻¹

Value:

$$
\mathrm{phyt}.\mathrm{fix}\cdot \frac{\mathrm{o2\_mol\_mass}}{\mathrm{c\_mol\_mass}}
$$

#### **Phytoplankton N uptake**

Source: epi.water.din

Target: out

Unit: kg day⁻¹

Value:

$$
\frac{\mathrm{phyt}.\mathrm{fix}}{\href{stdlib.html#chemistry}{\mathrm{cn\_molar\_to\_mass\_ratio}}\left(\mathrm{phyt\_cn}\right)}
$$

#### **Phytoplankton P uptake**

Source: epi.water.phos

Target: out

Unit: kg day⁻¹

Value:

$$
\frac{\mathrm{phyt}.\mathrm{fix}}{\href{stdlib.html#chemistry}{\mathrm{cp\_molar\_to\_mass\_ratio}}\left(\mathrm{phyt\_cp}\right)}
$$

#### **Phytoplankton C excretion**

Source: epi.water.phyt

Target: epi.water.oc

Unit: kg day⁻¹

Value:

$$
\mathrm{phyt}\cdot \mathrm{excr}
$$

#### **Phytoplankton N excretion**

Source: epi.water.phyt

Target: epi.water.on

Unit: kg day⁻¹

Value:

$$
\mathrm{phyt}\cdot \frac{\mathrm{excr}}{\href{stdlib.html#chemistry}{\mathrm{cn\_molar\_to\_mass\_ratio}}\left(\mathrm{phyt\_cn}\right)}
$$

#### **Phytoplankton P excretion**

Source: epi.water.phyt

Target: epi.water.op

Unit: kg day⁻¹

Value:

$$
\mathrm{phyt}\cdot \frac{\mathrm{excr}}{\href{stdlib.html#chemistry}{\mathrm{cp\_molar\_to\_mass\_ratio}}\left(\mathrm{phyt\_cp}\right)}
$$

#### **Pytoplankton death P release (epi)**

Source: out

Target: epi.water.sed.op

Unit: kg day⁻¹

Value:

$$
\frac{\mathrm{phyt}.\mathrm{death}}{\href{stdlib.html#chemistry}{\mathrm{cp\_molar\_to\_mass\_ratio}}\left(\mathrm{phyt\_cp}\right)}
$$

#### **Pytoplankton death P release (hyp)**

Source: out

Target: hyp.water.sed.op

Unit: kg day⁻¹

Value:

$$
\frac{\mathrm{phyt}.\mathrm{death}}{\href{stdlib.html#chemistry}{\mathrm{cp\_molar\_to\_mass\_ratio}}\left(\mathrm{phyt\_cp}\right)}
$$

---

## EasyChem-Particulate

Version: 0.0.3

File: [modules/easychem.txt](https://github.com/NIVANorge/Mobius2/tree/main/models/modules/easychem.txt)

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
| **Particles** | | | |
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
\mathrm{in\_s} = \mathrm{in\_flux}\left(\mathrm{downstream},\, \mathrm{epi}.\mathrm{water}.\mathrm{sed}\right) \\ \mathrm{in\_q} = \mathrm{in\_flux}\left(\mathrm{downstream},\, \mathrm{epi}.\mathrm{water}\right) \\ \mathrm{in\_conc} = \left(\mathrm{safe\_divide}\left(\mathrm{in\_s},\, \mathrm{in\_q}\right)\rightarrow \mathrm{mg}\,\mathrm{l}^{-1}\,\right) \\ \mathrm{excess} = \mathrm{in\_conc}-\mathrm{maxconc} \\ \left(\href{stdlib.html#response}{\mathrm{s\_response}}\left(\mathrm{excess},\, 0,\, 5 \mathrm{mg}\,\mathrm{l}^{-1}\,,\, 0,\, \mathrm{excess}\cdot \mathrm{in\_q}\right)\rightarrow \mathrm{kg}\,\mathrm{day}^{-1}\,\right)
$$



{% include lib/mathjax.html %}

