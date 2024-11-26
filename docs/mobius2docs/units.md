---
layout: default
title: Units
parent: Common declaration format
grand_parent: Model language
nav_order: 1
comment: "While we use python markup for code snippets, they are not actually python, it just creates convenient coloring for this format."
---

# Units

Units in Mobius2 are roughly [SI-based](https://en.wikipedia.org/wiki/International_System_of_Units), and are constructed from the following base units

| Name | Symbol |
| ---- | ------ |
| Meter | `m` |
| Second | `s` |
| Gram | `g` |
| Mole | `mol` |
| Equivalent (charge) | `eq` |
| Degrees Celsius | `deg_c` |
| Degrees | `deg` |
| Kelvin | `K` |
| Ampere | `A` |

Note that it differs from SI in that we use gram as the base instead of kilogram.

The following derived units are also available (and can be expressed in terms of the base units).

| Name | Symbol | In base units |
| ---- | ------ | ------------- |
| Liter | `l` | `10⁻³ m³` |
| Hectare | `ha` | `10⁴ m²` |
| Pascal | `Pa` | `10³ g m⁻¹ s⁻²` |
| Newton | `N` | `10³ g m s⁻²` |
| Joule | `J` | `10³ g m² s⁻²` |
| Watt | `W` | `10³ g m² s⁻³` |
| Bar | `bar` | `10⁸ g m⁻¹ s⁻²` |
| Percent | `perc` | `10⁻²` |
| Volt | `V` | `10³ g m² s⁻³ A⁻¹` |
| Ohm | `ohm` | `10³ g m² s⁻³ A⁻²` |
| Ton (metric) | `ton` | `10⁶ g` |

(More derived units could be added if they are needed).

There are also the `min`, `hr`, `day`, `week`, `month` and `year` time units. Note that you can't automatically convert between `month` or `year` and any of the smaller time units since that would depend on what month or year is in question. Instead look at using values from the [`time`](math_format.html#identifier) structure. 

The following SI prefixes are available

| Name | Symbol | Power of 10 |
| ---- | ------ | ----- |
| Yotta | `Y` | 24 |
| Zetta | `Z` | 21 |
| Exa | `E` | 18 |
| Peta | `P` | 15 |
| Tera | `T` | 12 |
| Giga | `G` | 9 |
| Mega | `M` | 6 |
| Kilo | `k` | 3 |
| Hecto | `h` | 2 |
| Deca | `da` | 1 |
| Desi | `d` | -1 |
| Centi | `c` | -2 |
| Milli | `m` | -3 |
| Micro | `u` or `mu` | -6 |
| Nano | `n` | -9 |
| Pico | `p` | -12 |
| Femto | `f` | -15 |
| Atto | `a` | -18 |
| Zepto | `z` | -21 |
| Yocto | `y` | -24 |

## The unit declaration format

In model code, units are declared inside square brackets `[ .. ]` that contain a comma-separated list of parts. Each part is either a

- Scaling factor. This can only appear as the first part, and can be used if you can't express the scaling using SI prefixes. The scaling factor is an integer or a rational number.
- A unit part (the components to the part must usually be separated by spaces).
	- This starts with an optional SI prefix.
	- It is then followed by the symbol of a base or derived unit.
	- Next it is followed by an optional power. The power is integer or rational, and applies to both the unit and the SI prefix.

Examples:

```python
[m m]         # Millimetres
[m m, day -1] # Millimetres per day
[2, day]      # 2 days
[s, m -1/3]   # Seconds per cube root meter
[]            # Dimensionless
```

## The standard form

Mobius2 internally converts all units down to a standard form which is a single scaling factor followed by the unit expressed in base units only. This form is often what you see reported in error messages.

For instance, the standard form of `[k W, m m]` (kilowatt-millimeter) is `10³ g m³ s⁻³`.

## Conversion

Two units can be converted to one another if they have the same *dimension*, that is if everything in their standard forms except for the scaling factor is the same (the base units and their powers). If two units can be converted to one another, the conversion factor is the ratio of the two scaling factor in one way or another.