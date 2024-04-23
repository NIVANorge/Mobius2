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
| Degrees Celsius | `deg_c` |
| Degrees | `deg` |
| Kelvin | `K` |
| Ampere | `A` |

The following compound units are also available (and are constructed from the base units).

| Name | Symbol |
| ---- | ------ |
| Liter | `l` |
| Hectare | `ha` |
| Pascal | `Pa` |
| Newton | `N` |
| Joule | `J` |
| Watt | `W` |
| Bar | `bar` |
| Percent | `perc` |
| Volt | `V` |
| Ohm | `ohm` |

(More compound units could be added if they are needed).

There are also the `min`, `hr`, `day`, `week`, `month` and `year` time units. Note that you can't automatically convert `month` or `year` to any of the smaller time units since that would depend on what month or year is in question. Instead look at using values from the `time` structure.

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
| Micro | `u`, `mu` | -6 |
| Nano | `n` | -9 |
| Pico | `p` | -12 |
| Femto | `f` | -15 |
| Atto | `a` | -18 |
| Zepto | `z` | -21 |
| Yocto | `y` | -24 |

## The unit declaration format

In model code, units are declared inside square brackets `[ .. ]`, and are a `,`-separated list of parts. Each part is either a

- Scaling factor. This can only appear as the first part, and can be used if you can't express the scaling using SI prefixes. The scaling factor is an integer or a rational number.
- A unit part.
	- This starts with an optional SI prefix.
	- It is then followed by the symbol of a base or compound unit.
	- Next it is followed by an optional power. The power is integer or rational, and applies to both the unit and the SI prefix.

Examples:

```python
[m m]         # Millimetres
[m m, day -1] # Millimetres per day-1
[2, day]      # 2 days
[s, m -1/3]   # Seconds per cube root meter
[]            # Dimensionless
```

## The standard form

Mobius2 converts all units down to a standard form which is a scaling factor followed by the unit expressed in base units only. This form is often what you see reported in error messages.

The standard form also allows Mobius2 to do automatic computations of conversion factors between units and to check that units of different expressions match.