---
layout: default
title: SimplyC
parent: Existing models
grand_parent: MobiView2
nav_order: 1
---

# SimplyC

This is auto-generated documentation based on the model code in models/simplyc_model.txt .

The file was generated at 2024-04-17 17:28:36.

# SimplyC land

## Docstring

A simple DOC model.

## Parameters

| Name | Symbol | Unit | Default | Min | Max | Description |
| ---- | ------ | ---- | ------- | --- | --- | ----------- |
| Soil temperature DOC creation linear coefficient | kt1 | °C⁻¹ | 0 | 0 | 0.1 |  |
| Soil temperature DOC creation second-order coefficient | kt2 | °C⁻² | 0 | 0 | 0.1 |  |
| Soil DOC linear SO4 dependence | kso4 | l mg⁻¹ | 0 | 0 | 0.1 |  |
| Baseline soil DOC dissolution rate | cdoc | mg l⁻¹ day⁻¹ | 1 | 0 | 10 | Only used if the soil DOC computation type is dynamic. |

Parameter group: **DOC general**

| Name | Symbol | Unit | Default | Min | Max | Description |
| ---- | ------ | ---- | ------- | --- | --- | ----------- |
| Baseline soil DOC concentration | basedoc | mg l⁻¹ | 10 | 0 | 100 |  |

Parameter group: **DOC land**

| Name | Symbol | Unit | Default | Min | Max | Description |
| ---- | ------ | ---- | ------- | --- | --- | ----------- |
| Groundwater DOC half-life | gwdochl | day | 80 | 1 | 500 |  |
| Groundwater DOC concentration | gwdocconc | mg l⁻¹ | 3 | 0 | 20 |  |

Parameter group: **DOC deep soil**

# SimplyC river

## Parameters

| Name | Symbol | Unit | Default | Min | Max | Description |
| ---- | ------ | ---- | ------- | --- | --- | ----------- |
| River DOC loss rate at 20°C | r_loss | day⁻¹ | 0 | -inf | inf |  |
| River DOC loss Q10 | r_q10 |  | 1 | -inf | inf |  |

Parameter group: **DOC river**

