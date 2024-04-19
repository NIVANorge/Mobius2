---
layout: default
title: SimplyC
parent: Autogenerated documentation
grand_parent: Existing models
nav_order: 1
---

# SimplyC

This is auto-generated documentation based on the model code in [models/simplyc_model.txt](https://github.com/NIVANorge/Mobius2/blob/main/models/simplyc_model.txt) .
Since the modules can be dynamically loaded with different arguments, this documentation does not necessarily reflect all use cases of the modules.

The file was generated at 2024-04-19 12:47:35.

---

## SimplyC land

Version: 1.0.1

### Description

A simple DOC model.

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
| Baseline soil DOC concentration | **basedoc** | mg l⁻¹ |  |
| **DOC deep soil** | | | |
| Groundwater DOC half-life | **gwdochl** | day |  |
| Groundwater DOC concentration | **gwdocconc** | mg l⁻¹ |  |

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
\begin{cases}\mathrm{basedoc} & \text{if}\;\mathrm{soildoc\_type}.\mathrm{const} \\ \mathrm{basedoc}\cdot \left(1+\left(\mathrm{kt1}+\mathrm{kt2}\cdot \mathrm{temp}\right)\cdot \mathrm{temp}-\mathrm{kso4}\cdot \mathrm{air}.\mathrm{so4}\right) & \text{if}\;\mathrm{soildoc\_type}.\mathrm{equilibrium} \\ \mathrm{no\_override} & \text{otherwise}\end{cases}
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
\begin{cases}\mathrm{gwdocconc} & \text{if}\;\mathrm{gwdoc\_type}.\mathrm{const} \\ \mathrm{aggregate}\left(\mathrm{conc}\left(\mathrm{soil}.\mathrm{water}.\mathrm{oc}\right)\right) & \text{if}\;\mathrm{gwdoc\_type}.\mathrm{soil\_avg} \\ \mathrm{no\_override} & \text{otherwise}\end{cases}
$$

Initial value:

$$
\begin{cases}\mathrm{gwdocconc} & \text{if}\;\mathrm{gwdoc\_type}.\mathrm{const}\;\text{or}\;\mathrm{gwdoc\_type}.\mathrm{half\_life} \\ \mathrm{aggregate}\left(\mathrm{conc}\left(\mathrm{soil}.\mathrm{water}.\mathrm{oc}\right)\right) & \text{otherwise}\end{cases}
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

### External symbols

| Name | Symbol | Type |
| ---- | ------ | ---- |
| River | **river** | compartment |
| Groundwater | **gw** | compartment |
| Water | **water** | quantity |
| Temperature | **temp** | property |
| Organic carbon | **oc** | quantity |

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



{% include lib/mathjax.html %}
