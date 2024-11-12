---
layout: default
title: Auxiliary
parent: Mathematical description
grand_parent: Existing models
nav_order: 3
---

# Auxiliary

This is auto-generated documentation based on the model code in [models/easylake_simplycnp_model.txt](https://github.com/NIVANorge/Mobius2/blob/main/models/easylake_simplycnp_model.txt) .
Since the modules can be dynamically loaded with different arguments, this documentation does not necessarily reflect all use cases of the modules.

See the note on [notation](autogen.html#notation).

The file was generated at 2024-11-12 12:57:15.

---

## Degree-day PET

Version: 1.0.0

File: [modules/pet.txt](https://github.com/NIVANorge/Mobius2/tree/main/models/modules/pet.txt)

### Description

This is a very simple potential evapotranspiration model that uses a linear relationship between PET and air temperature.

Authors: Magnus D. Norling

### External symbols

| Name | Symbol | Type |
| ---- | ------ | ---- |
| Atmosphere | **air** | compartment |
| Soil | **soil** | compartment |
| Potential evapotranspiration | **pet** | property |
| Temperature | **temp** | property |

### Parameters

| Name | Symbol | Unit |  Description |
| ---- | ------ | ---- |  ----------- |
| **Potential evapotranspiration** | | | Distributes like: `soil` |
| Degree-day factor for evapotranspiration | **ddf_pet** | mm °C⁻¹ day⁻¹ |  |
| Minimal temperature for evapotranspiration | **pet_min_t** | °C |  |

### State variables

#### **Potential evapotranspiration**

Location: **soil.pet**

Unit: mm day⁻¹

Value:

$$
\mathrm{max}\left(0,\, \mathrm{ddf\_pet}\cdot \left(\mathrm{air}.\mathrm{temp}-\mathrm{pet\_min\_t}\right)\right)
$$

---

## HBVSnow

Version: 1.0.0

File: [modules/hbv_snow.txt](https://github.com/NIVANorge/Mobius2/tree/main/models/modules/hbv_snow.txt)

### Description

This is an adaption of the snow module from HBV-Nordic (Sælthun 1995)

[NVE home page](https://www.nve.no/vann-og-vassdrag/vannets-kretsloep/analysemetoder-og-modeller/hbv-modellen/)

[Model description](https://publikasjoner.nve.no/publication/1996/publication1996_07.pdf)

Authors: Magnus D. Norling

### External symbols

| Name | Symbol | Type |
| ---- | ------ | ---- |
| Atmosphere | **air** | compartment |
| Snow | **snow** | quantity |
| Snow layer | **snow_box** | compartment |
| Water | **water** | quantity |
| Temperature | **temp** | property |
| Precipitation | **precip** | property |
|  | **water_target** | loc |

### Parameters

| Name | Symbol | Unit |  Description |
| ---- | ------ | ---- |  ----------- |
| **Snow** | | | Distributes like: `snow_box` |
| Degree-day factor for snowmelt | **ddf_melt** | mm °C⁻¹ day⁻¹ | Linear correlation between rate of snow melt and air temperature |
| Temperature at which precip falls as snow | **t_snow** | °C |  |
| Temperature at which snow melts | **t_melt** | °C |  |
| Refreeze efficiency | **refr_eff** |  | Speed of refreeze of liquid water in snow relative to speed of melt at the opposite temperature differential |
| Liquid water fraction | **snow_liq** |  | How many mm liquid water one mm of snow (water equivalents) can hold before the water is released |
| Initial snow depth (water equivalents) | **init_snow** | mm |  |

### State variables

#### **Air temperature**

Location: **air.temp**

Unit: °C

This series is externally defined. It may be an input series.

#### **Precipitation**

Location: **air.precip**

Unit: mm day⁻¹

This series is externally defined. It may be an input series.

#### **Snow depth**

Location: **snow_box.snow**

Unit: mm

Initial value:

$$
\mathrm{init\_snow}
$$

#### **Snow water**

Location: **snow_box.water**

Unit: mm

### Fluxes

#### **Precipitation falling as snow**

Source: out

Target: snow_box.snow

Unit: mm day⁻¹

Value:

$$
\mathrm{air}.\mathrm{precip}\cdot \left(\mathrm{air}.\mathrm{temp}\leq \mathrm{t\_snow}\right)
$$

#### **Precipitation falling as rain**

Source: out

Target: snow_box.water

Unit: mm day⁻¹

Value:

$$
\mathrm{air}.\mathrm{precip}\cdot \left(\mathrm{air}.\mathrm{temp}>\mathrm{t\_snow}\right)
$$

#### **Melt**

Source: snow_box.snow

Target: snow_box.water

Unit: mm day⁻¹

Value:

$$
\mathrm{max}\left(0,\, \mathrm{ddf\_melt}\cdot \left(\mathrm{air}.\mathrm{temp}-\mathrm{t\_melt}\right)\right)
$$

#### **Refreeze**

Source: snow_box.water

Target: snow_box.snow

Unit: mm day⁻¹

Value:

$$
\mathrm{max}\left(0,\, \mathrm{refr\_eff}\cdot \mathrm{ddf\_melt}\cdot \left(\mathrm{t\_melt}-\mathrm{air}.\mathrm{temp}\right)\right)
$$

#### **Melt runoff**

Source: snow_box.water

Target: water_target

Unit: mm day⁻¹

Value:

$$
\mathrm{max}\left(0,\, \mathrm{water}-\mathrm{snow}\cdot \mathrm{snow\_liq}\right)\cdot 1 \mathrm{day}^{-1}\,
$$

---

## Simply soil temperature

Version: 0.1.0

File: [modules/simplysoiltemp.txt](https://github.com/NIVANorge/Mobius2/tree/main/models/modules/simplysoiltemp.txt)

### Description

This is a simplification of the soil temperature model developed for the INCA models.

Rankinen K. T. Karvonen and D. Butterfield (2004), A simple model for predicting soil temperature in snow covered and seasonally frozen soil; Model description and testing, Hydrol. Earth Syst. Sci., 8, 706-716, [https://doi.org/10.5194/hess-8-706-2004](https://doi.org/10.5194/hess-8-706-2004)

Soil temperature is computed at a depth of 15 cm.

Authors: Leah A. Jackson-Blake, Magnus D. Norling

### External symbols

| Name | Symbol | Type |
| ---- | ------ | ---- |
| Atmosphere | **air** | compartment |
| Snow | **snow** | quantity |
| Soil | **soil** | compartment |
| Snow layer | **snow_box** | compartment |
| Temperature | **temp** | property |

### Constants

| Name | Symbol | Unit | Value |
| ---- | ------ | ---- | ----- |
| Soil temperature depth | **st_depth** | cm | 15 |

### Parameters

| Name | Symbol | Unit |  Description |
| ---- | ------ | ---- |  ----------- |
| **Soil temperature general** | | |  |
| Snow depth / soil temperature factor | **depthst** | cm⁻¹ | Per cm snow water equivalents |
| Initial soil temperature | **init_st** | °C |  |
| **Soil temperature land** | | | Distributes like: `soil` |
| Soil thermal conductivity / specific heat capacity | **stc** | m² Ms⁻¹ |  |

### State variables

#### **COUP temperature**

Location: **soil.coup**

Unit: °C

Value:

$$
\mathrm{last}\left(\mathrm{coup}\right)+\left(\frac{\mathrm{stc}}{\left(2\cdot \mathrm{st\_depth}\right)^{2}}\cdot \left(\mathrm{air}.\mathrm{temp}-\mathrm{last}\left(\mathrm{coup}\right)\right)\cdot \mathrm{time}.\mathrm{step\_length\_in\_seconds}\rightarrow \mathrm{°C}\,\right)
$$

Initial value:

$$
\mathrm{init\_st}
$$

#### **Soil temperature**

Location: **soil.temp**

Unit: °C

Value:

$$
\mathrm{coup}\cdot e^{\left(\mathrm{depthst}\cdot \mathrm{snow\_box}.\mathrm{snow}\rightarrow 1\right)}
$$

Initial value:

$$
\mathrm{init\_st}
$$

---

## RiverTemperature

Version: 0.1.1

File: [modules/rivertemp.txt](https://github.com/NIVANorge/Mobius2/tree/main/models/modules/rivertemp.txt)

### Description

A simple model for river water temperature. Water from the catchment is assumed to have the same temperature as the soil. Then heat is exchanged with the atmosphere with a constant rate relative to the temperature difference.

Authors: Magnus D. Norling

### External symbols

| Name | Symbol | Type |
| ---- | ------ | ---- |
| Atmosphere | **air** | compartment |
| Soil | **soil** | compartment |
| River | **river** | compartment |
| Water | **water** | quantity |
| Heat energy | **heat** | quantity |
| Temperature | **temp** | property |

### Constants

| Name | Symbol | Unit | Value |
| ---- | ------ | ---- | ----- |
| Minimum river temperature | **min_t** | °C | 0.4 |

### Parameters

| Name | Symbol | Unit |  Description |
| ---- | ------ | ---- |  ----------- |
| **River temperature** | | | Distributes like: `river` |
| Air-river heat exchange coefficient | **coeff** | day⁻¹ |  |

### State variables

#### **River thermal energy**

Location: **river.water.heat**

Unit: J

Initial value:

$$
\href{stdlib.html#water-utils}{\mathrm{water\_temp\_to\_heat}}\left(\mathrm{water},\, \mathrm{max}\left(\mathrm{air}.\mathrm{temp},\, \mathrm{min\_t}\right)\right)
$$

#### **River temperature**

Location: **river.water.temp**

Unit: °C

Value:

$$
\href{stdlib.html#water-utils}{\mathrm{water\_heat\_to\_temp}}\left(\mathrm{water},\, \mathrm{water}.\mathrm{heat}\right)
$$

### Fluxes

#### **River heat from land**

Source: out

Target: river.water.heat

Unit: J day⁻¹

Value:

$$
\mathrm{V} = \left(\mathrm{in\_flux}\left(\mathrm{water}\right)\rightarrow \mathrm{m}^{3}\,\mathrm{day}^{-1}\,\right) \\ \mathrm{t} = \mathrm{max}\left(\mathrm{min\_t},\, \mathrm{aggregate}\left(\mathrm{soil}.\mathrm{temp}\right)\right) \\ \left(\href{stdlib.html#water-utils}{\mathrm{water\_temp\_to\_heat}}\left(\left(\mathrm{V}\Rightarrow \mathrm{m}^{3}\,\right),\, \mathrm{t}\right)\Rightarrow \mathrm{J}\,\mathrm{day}^{-1}\,\right)
$$

#### **River sensible heat flux**

Source: out

Target: river.water.heat

Unit: J day⁻¹

Value:

$$
\left(\href{stdlib.html#water-utils}{\mathrm{water\_temp\_to\_heat}}\left(\mathrm{water},\, \mathrm{max}\left(\mathrm{air}.\mathrm{temp},\, \mathrm{min\_t}\right)\right)-\mathrm{heat}\right)\cdot \mathrm{coeff}
$$

---

## Atmospheric

Version: 0.1.0

File: [modules/atmospheric.txt](https://github.com/NIVANorge/Mobius2/tree/main/models/modules/atmospheric.txt)

### Description

Simple module for some common atmospheric attributes for use e.g. with evapotranspiration or air-sea heat exchange modules.

The user must provide either "Daily average global radiation" or "Cloud cover" as an input series, but one of these can be computed from the other if not both are available.

Authors: Magnus D. Norling

### External symbols

| Name | Symbol | Type |
| ---- | ------ | ---- |
| Atmosphere | **air** | compartment |
| Cosine of the solar zenith angle | **cos_z** | property |
| Actual vapour pressure | **a_vap** | property |
| Actual specific humidity | **a_hum** | property |
| Temperature | **temp** | property |
| Wind speed | **wind** | property |
| Global radiation | **g_rad** | property |
| Saturation vapour pressure | **s_vap** | property |
| Pressure | **pressure** | property |
| Density | **dens** | property |
| Downwelling longwave radiation | **lwd** | property |

### Parameters

| Name | Symbol | Unit |  Description |
| ---- | ------ | ---- |  ----------- |
| **Location** | | | Distributes like: `air` |
| Latitude | **latitude** | ° | Only used if Daily average radiation is not provided as a series |
| Longitude | **longitude** | ° | Only used if sub-daily precision is used. |
| GMT zone | **time_zone** | hr | Only used if sub-daily precision is used. |
| Elevation | **elev** | m | Only used if Daily average radiation is not provided as a series |
| **Radiation** | | |  |
| Cloud absorption scaling factor | **crs** |  | Used if 'Daily average global radiation' is not provided as a series. Scaling factor for shortwave absorption by clouds |
| Cloud absorption power factor | **crp** |  | Used if 'Daily average global radiation' is not provided as a series. Power factor for shortwave absorption by clouds |
| Use sub-daily precision | **subdaily** |  | Compute hourly averages of solar radiation. Only works correctly if the model sampling step is [hr] or lower. |

### State variables

#### **Air temperature**

Location: **air.temp**

Unit: °C

This series is externally defined. It may be an input series.

#### **Relative humidity**

Location: **air.r_hum**

Unit: %

This series is externally defined. It may be an input series.

#### **Wind speed**

Location: **air.wind**

Unit: m s⁻¹

This series is externally defined. It may be an input series.

#### **Daily average global radiation on a clear sky day**

Location: **air.g_rad_cloud_free**

Unit: W m⁻²

Value:

$$
\mathrm{ext} = \href{stdlib.html#radiation}{\mathrm{daily\_average\_extraterrestrial\_radiation}}\left(\mathrm{latitude},\, \mathrm{time}.\mathrm{day\_of\_year}\right) \\ \href{stdlib.html#radiation}{\mathrm{clear\_sky\_shortwave}}\left(\mathrm{ext},\, \mathrm{elev}\right)
$$

#### **Daily average global radiation**

Location: **air.g_rad_day**

Unit: W m⁻²

Value:

$$
\left(1-\mathrm{crs}\cdot \mathrm{cloud}^{\mathrm{crp}}\right)\cdot \mathrm{g\_rad\_cloud\_free}
$$

#### **Global radiation**

Location: **air.g_rad**

Unit: W m⁻²

Value:

$$
\begin{cases}\begin{pmatrix}\mathrm{hour} = \mathrm{floor}\left(\left(\mathrm{time}.\mathrm{second\_of\_day}+\mathrm{time}.\mathrm{fractional\_step}\cdot \mathrm{time}.\mathrm{step\_length\_in\_seconds}\rightarrow \mathrm{hr}\,\right)\right) \\ \mathrm{hour\_a} = \href{stdlib.html#radiation}{\mathrm{hour\_angle}}\left(\mathrm{time}.\mathrm{day\_of\_year},\, \mathrm{time\_zone},\, \mathrm{hour},\, \mathrm{longitude}\right) \\ \href{stdlib.html#radiation}{\mathrm{hourly\_average\_radiation}}\left(\mathrm{g\_rad\_day},\, \mathrm{time}.\mathrm{day\_of\_year},\, \mathrm{latitude},\, \mathrm{hour\_a}\right)\end{pmatrix} & \text{if}\;\mathrm{subdaily} \\ \mathrm{g\_rad\_day} & \text{otherwise}\end{cases}
$$

#### **Cosine of the solar zenith angle**

Location: **air.cos_z**

Unit: 

Value:

$$
\mathrm{hour\_a} = \begin{pmatrix}\mathrm{hour} = \left(\mathrm{time}.\mathrm{second\_of\_day}+\mathrm{time}.\mathrm{fractional\_step}\cdot \mathrm{time}.\mathrm{step\_length\_in\_seconds}\rightarrow \mathrm{hr}\,\right) \\ \begin{cases}\href{stdlib.html#radiation}{\mathrm{hour\_angle}}\left(\mathrm{time}.\mathrm{day\_of\_year},\, \mathrm{time\_zone},\, \mathrm{hour},\, \mathrm{longitude}\right) & \text{if}\;\mathrm{subdaily} \\ 0 & \text{otherwise}\end{cases}\end{pmatrix} \\ \href{stdlib.html#radiation}{\mathrm{cos\_zenith\_angle}}\left(\mathrm{hour\_a},\, \mathrm{time}.\mathrm{day\_of\_year},\, \mathrm{latitude}\right)
$$

#### **Cloud cover**

Location: **air.cloud**

Unit: 

Value:

$$
\mathrm{sw\_ratio} = \frac{\mathrm{g\_rad\_day}}{\mathrm{g\_rad\_cloud\_free}} \\ \mathrm{cc} = 1-\mathrm{sw\_ratio} \\ \href{stdlib.html#basic}{\mathrm{clamp}}\left(\mathrm{cc},\, 0,\, 1\right)
$$

#### **Air pressure**

Location: **air.pressure**

Unit: hPa

Value:

$$
\left(\href{stdlib.html#meteorology}{\mathrm{mean\_barometric\_pressure}}\left(\mathrm{elev}\right)\rightarrow \mathrm{hPa}\,\right)
$$

#### **Saturation vapour pressure**

Location: **air.s_vap**

Unit: hPa

Value:

$$
\href{stdlib.html#meteorology}{\mathrm{saturation\_vapor\_pressure}}\left(\mathrm{temp}\right)
$$

#### **Actual vapour pressure**

Location: **air.a_vap**

Unit: hPa

Value:

$$
\left(\mathrm{r\_hum}\rightarrow 1\right)\cdot \mathrm{s\_vap}
$$

#### **Actual specific humidity**

Location: **air.a_hum**

Unit: 

Value:

$$
\href{stdlib.html#meteorology}{\mathrm{specific\_humidity\_from\_pressure}}\left(\mathrm{pressure},\, \mathrm{a\_vap}\right)
$$

#### **Air density**

Location: **air.dens**

Unit: kg m⁻³

Value:

$$
\href{stdlib.html#meteorology}{\mathrm{air\_density}}\left(\mathrm{temp},\, \mathrm{pressure},\, \mathrm{a\_vap}\right)
$$

#### **Downwelling longwave radiation**

Location: **air.lwd**

Unit: W m⁻²

Value:

$$
\href{stdlib.html#radiation}{\mathrm{downwelling\_longwave}}\left(\mathrm{temp},\, \mathrm{a\_vap},\, \mathrm{cloud}\right)
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
| Wind speed | **wind** | property |
| Temperature | **temp** | property |
| Precipitation | **precip** | property |
| Global radiation | **g_rad** | property |
| Pressure | **pressure** | property |
| Downwelling longwave radiation | **lwd** | property |
| Shortwave radiation | **sw** | property |
| Attenuation coefficient | **attn** | property |
| Ice indicator | **indicator** | property |
| Evaporation | **evap** | property |
|  | **freeze_temp** | loc |
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
| **Ice** | | | Distributes like: `epi` |
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

#### **Transfer coefficient for sensible heat flux**

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
\mathrm{z\_surf} = 1 \mathrm{m}\, \\ \mathrm{K\_ice} = 200 \mathrm{W}\,\mathrm{m}^{-3}\,\mathrm{°C}^{-1}\, \\ \mathrm{e} = \left(\mathrm{freeze\_temp}-\mathrm{top\_water}.\mathrm{temp}\right)\cdot \mathrm{z\_surf}\cdot \mathrm{K\_ice} \\ \begin{cases}0 \mathrm{W}\,\mathrm{m}^{-2}\, & \text{if}\;\mathrm{ice}<10^{-6} \mathrm{m}\,\;\text{and}\;\mathrm{e}<0 \\ \mathrm{e} & \text{otherwise}\end{cases}
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
\mathrm{alpha} = \frac{1}{10 \mathrm{m}^{-1}\,\cdot \mathrm{ice}} \\ \begin{cases}\frac{\mathrm{alpha}\cdot \mathrm{freeze\_temp}+\mathrm{air}.\mathrm{temp}}{1+\mathrm{alpha}} & \text{if}\;\mathrm{indicator} \\ 0 \mathrm{°C}\, & \text{otherwise}\end{cases}
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

## AirSeaGas Lake

Version: 0.0.0

File: [modules/airsea_gas.txt](https://github.com/NIVANorge/Mobius2/tree/main/models/modules/airsea_gas.txt)

### Description

Air-sea gas exchange module (O₂, CO₂, CH₄)

Authors: François Clayer, Magnus Dahler Norling

### External symbols

| Name | Symbol | Type |
| ---- | ------ | ---- |
| Atmosphere | **air** | compartment |
| CO₂ | **co2** | quantity |
| O₂ saturation concentration | **o2satconc** | property |
| Epilimnion | **basin** | compartment |
| O₂ | **o2** | quantity |
| CH₄ | **ch4** | quantity |
| Wind speed | **wind** | property |
| Temperature | **temp** | property |
| Precipitation | **precip** | property |
| Pressure | **pressure** | property |
|  | **ice_ind** | loc |
|  | **top_water** | loc |
|  | **A_surf** | loc |
|  | **compute_dic** | loc |

### State variables

#### **O₂ piston velocity**

Location: **basin.p_vel**

Unit: cm hr⁻¹

Value:

$$
\href{stdlib.html#sea-oxygen}{\mathrm{o2\_piston\_velocity}}\left(\mathrm{air}.\mathrm{wind},\, \mathrm{top\_water}.\mathrm{temp}\right)
$$

### Fluxes

#### **Precipitation O₂**

Source: out

Target: top_water.o2

Unit: kg day⁻¹

Value:

$$
\mathrm{precip\_saturation} = 0.9 \\ \mathrm{cnc} = \mathrm{precip\_saturation}\cdot \href{stdlib.html#sea-oxygen}{\mathrm{o2\_saturation\_concentration}}\left(\mathrm{air}.\mathrm{temp},\, 0\right)\cdot \mathrm{o2\_mol\_mass} \\ \left(\mathrm{air}.\mathrm{precip}\cdot \mathrm{A\_surf}\cdot \mathrm{cnc}\rightarrow \mathrm{kg}\,\mathrm{day}^{-1}\,\right)
$$

#### **O₂ gas exchange at surface**

Source: top_water.o2

Target: out

Unit: kg day⁻¹

Value:

$$
\left(\;\text{not}\;\mathrm{ice\_ind}\cdot \mathrm{basin}.\mathrm{p\_vel}\cdot \left(\mathrm{conc}\left(\mathrm{top\_water}.\mathrm{o2}\right)-\mathrm{top\_water}.\mathrm{o2satconc}\right)\cdot \mathrm{A\_surf}\rightarrow \mathrm{kg}\,\mathrm{day}^{-1}\,\right)
$$

---

## Simple river TOC

Version: 0.1.1

File: [modules/simplyc.txt](https://github.com/NIVANorge/Mobius2/tree/main/models/modules/simplyc.txt)

### Description

River particulate organic carbon.

Authors: Magnus D. Norling

### External symbols

| Name | Symbol | Type |
| ---- | ------ | ---- |
| Total organic carbon | **toc** | property |
| River | **river** | compartment |
| Water | **water** | quantity |
| Organic carbon | **oc** | quantity |
| Particles | **sed** | quantity |

### Parameters

| Name | Symbol | Unit |  Description |
| ---- | ------ | ---- |  ----------- |
| **Particulate carbon** | | |  |
| Particle OC mass fraction | **sed_oc** | g kg⁻¹ |  |

### State variables

#### **River POC**

Location: **river.water.sed.oc**

Unit: kg

#### **River TOC**

Location: **river.water.toc**

Unit: mg l⁻¹

Value:

$$
\mathrm{conc}\left(\mathrm{oc}\right)+\mathrm{conc}\left(\mathrm{sed}.\mathrm{oc}\right)\cdot \mathrm{conc}\left(\mathrm{sed}\right)
$$

### Fluxes

#### **River POC mobilization**

Source: out

Target: river.water.sed.oc

Unit: kg day⁻¹

Value:

$$
\left(\mathrm{sed\_oc}\cdot \mathrm{in\_flux}\left(\mathrm{river}.\mathrm{water}.\mathrm{sed}\right)\rightarrow \mathrm{kg}\,\mathrm{day}^{-1}\,\right)
$$

---

## Organic CNP land

Version: 0.0.0

File: [modules/organic_np.txt](https://github.com/NIVANorge/Mobius2/tree/main/models/modules/organic_np.txt)

### Parameters

| Name | Symbol | Unit |  Description |
| ---- | ------ | ---- |  ----------- |
| **Organic nutrient ratios land** | | |  |
| OM molar C/N ratio | **om_cn** |  | The default value is the Redfield ratio |
| OM molar C/P ratio | **om_cp** |  | The default value is the Redfield ratio |

---

## Simple organic NP

Version: 0.0.0

File: [modules/organic_np.txt](https://github.com/NIVANorge/Mobius2/tree/main/models/modules/organic_np.txt)

### External symbols

| Name | Symbol | Type |
| ---- | ------ | ---- |
| River | **river** | compartment |
| Organic nitrogen | **on** | quantity |
| Water | **water** | quantity |
| Organic carbon | **oc** | quantity |
| Particles | **sed** | quantity |
| Organic phosphorous | **op** | quantity |
| Total nitrogen | **tn** | property |
| Total phosphorous | **tp** | property |
| Total dissolved phosphorous | **tdp** | property |

### State variables

#### **River DOP**

Location: **river.water.op**

Unit: kg

Conc. unit: mg l⁻¹

#### **River POP**

Location: **river.water.sed.op**

Unit: kg

#### **River DON**

Location: **river.water.on**

Unit: kg

Conc. unit: mg l⁻¹

#### **River PON**

Location: **river.water.sed.on**

Unit: kg

#### **Total phosphorous**

Location: **river.water.tp**

Unit: mg l⁻¹

#### **Total dissolved phosphorous**

Location: **river.water.tdp**

Unit: mg l⁻¹

#### **Total nitrogen**

Location: **river.water.tn**

Unit: mg l⁻¹

### Fluxes

#### **DON mobilization**

Source: out

Target: river.water.on

Unit: kg day⁻¹

Value:

$$
\left(\frac{\mathrm{in\_flux}\left(\mathrm{oc}\right)}{\href{stdlib.html#chemistry}{\mathrm{cn\_molar\_to\_mass\_ratio}}\left(\mathrm{om\_cn}\right)}\rightarrow \mathrm{kg}\,\mathrm{day}^{-1}\,\right)
$$

#### **PON mobilization**

Source: out

Target: river.water.sed.on

Unit: kg day⁻¹

Value:

$$
\left(\frac{\mathrm{in\_flux}\left(\mathrm{sed}.\mathrm{oc}\right)}{\href{stdlib.html#chemistry}{\mathrm{cn\_molar\_to\_mass\_ratio}}\left(\mathrm{om\_cn}\right)}\rightarrow \mathrm{kg}\,\mathrm{day}^{-1}\,\right)
$$

#### **DOP mobilization**

Source: out

Target: river.water.op

Unit: kg day⁻¹

Value:

$$
\left(\frac{\mathrm{in\_flux}\left(\mathrm{oc}\right)}{\href{stdlib.html#chemistry}{\mathrm{cn\_molar\_to\_mass\_ratio}}\left(\mathrm{om\_cp}\right)}\rightarrow \mathrm{kg}\,\mathrm{day}^{-1}\,\right)
$$

#### **POP mobilization**

Source: out

Target: river.water.sed.op

Unit: kg day⁻¹

Value:

$$
\left(\frac{\mathrm{in\_flux}\left(\mathrm{sed}.\mathrm{oc}\right)}{\href{stdlib.html#chemistry}{\mathrm{cp\_molar\_to\_mass\_ratio}}\left(\mathrm{om\_cp}\right)}\rightarrow \mathrm{kg}\,\mathrm{day}^{-1}\,\right)
$$



{% include lib/mathjax.html %}

