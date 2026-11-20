---
layout: default
title: MAGIC user guide
parent: MAGIC
grand_parent: Existing models
nav_order: 1
---

# MAGIC in Mobius2

This is a short user guide for people who have some background with using the MAGIC model \[Cosby01\], but are new to using it in Mobius2. If you need more help beyond what is covered here, contact us.

See the [background on the MAGIC model and its implementation in Mobius](magic.html).

## Installation

Install Mobius2 and MobiView2 (and maybe also mobipy) by following the [getting started guide](../gettingstarted/gettingstarted.html).

To test the MAGIC model, use MobiView2.exe to open `Mobius2/models/magic_model_simple.txt` and then `Mobius2/models/example_data/MAGIC/birkenes_test.dat`.

## Configure a setup to your site

Make a copy of the Birkenes setup (the `.dat` file and the `.xlsx` file). You can rename them, but if you rename the `.xlsx` file, you must make sure the corresponding `series()` field in the `.dat` file points at the right `.xlsx` file.

The example setup has one soil compartment connected to one surface water body (which could be a river or lake, that only depends on how you set its relative area and depth). You can make more complex structures with different soil, groundwater and surface water compartments connected in arbitrary ways, but we will not cover that in this document.

The parameters of the model are mostly the same as in MAGIC v7 & v8 \[Cosby01, Oulehle12\].
One difference is that you must set a “Runoff scale” per compartment, which is a number that converts Runoff per catchment area to Runoff per compartment area. In the simple setup, the best is to set it to 1 for the soil and 1/ra for the water body, where ra is the relative area of the water body.

It is also now possible to let the organic acid concentration vary with the SO4 concentration in each compartment.
The simplest Nitrogen module only has parameters named “Nitrification” and “Denitrification”, but these are really meant to be sums of all sources and sinks that affect Nitrogen like uptake, fixation etc. More advanced CNP modules are available, and it is easy to create new ones.

### Input series

When you have parametrized your setup, you also need to provide input series (forcings) in the `.xlsx` file.

The list of all possible input series you can provide is shown in the bottom right window of MobiView2 when you have the model open. If you click a name you will also see the unit the model wants it on in the legend of the plot view.

This list is a bit shorter than what was available in MAGIC v8, but this is just because all of them have not been needed yet. It is trivial to add more variable forcings, so contact us if needed.

In general, an input series will overwrite the similarly named parameter if it is provided, so for instance if you provide a Runoff time series, it will be used instead of the Runoff parameter on the dates when the time series is given.

For deposition, the “Total deposition” series will be used on dates it is provided, otherwise, it will use the “Wet deposition scale” series multiplied with the deposition parameter. If no Wet deposition scale series is provided, it will just use the parameter directly.

See the general (not MAGIC-specific) [specification of the input format](../datafiledocs/datafiles.html).

Note that Mobius2-MAGIC can currently only run on a monthly step. To run without seasonal variations, just prepare your input series so that they are constant within each year. This can be done using the `step_interpolate` flag as in the Birkenes example.

### Automation

You can use python to automate generation of model setups. This is useful if you have hundreds or thousands of sites to set up. We will not cover the details here, but you can contact us for example code.


## Calibrate your setup

You should follow the same procedures as in older versions of MAGIC. It is usually a good idea to follow the below steps, but this can vary depending on the use case

* Configure NH4 and NO3 – related processes to match modelled to observed concentrations in the water body.
* Configure Cl and SO4 likewise. This can involve adjusting the deposition (often what is measured at a nearby station or is modelled by a climate model does not account precisely for the mass balance of a given catchment).
* Calibrate the base cations. This involves adjusting base cation weathering, initial saturation and soil organic acid. It is also possible to use Aluminium as a calibration factor. The target is to get the right water body base cation concentrations and the right soil base saturation (and maybe soil water pH).
* Optionally you can also adjust the water body organic acid and/or aluminium to get the right water body pH. This doesn’t affect other processes that much, but it matters if you want to predict pH.

### Autocalibration

It is possible to use an auto-calibrator to do the calibration steps automatically. You can [set this up in MobiView2](../mobiviewdocs/autocalibration.html) by selecting what parameters and optimization targets to fit. You should do the above 4 steps separately. Remember that you can save the calibration setup for use later.

If you want to batch run the calibration over many model setups or otherwise make the process a bit more automated, you should instead script the calibration in python (or possibly Julia). This will require some coding experience.

See an example of [calibrating a single site](
https://github.com/NIVANorge/Mobius2/blob/main/example_notebooks/magic_autocalib.ipynb)

You can run similar code in a loop to calibrate multiple sites. Note that you may need to configure the script depending on how you want to calibrate your sites.


## References

\[Cosby01\] B. J. Cosby, R. C. Ferrier, A. Jenkins and R. F. Wright, 2001, Modelling the effects of acid deposition: refinements, adjustments and inclusion of nitrogen dynamics in the MAGIC model. Hydrol. Earth Syst. Sci, 5(3), 499-517 [https://doi.org/10.5194/hess-5-499-2001](https://doi.org/10.5194/hess-5-499-2001)

\[Oulehle12\] F. Oulehle, B.J. Cosby, R.F. Wright, J. Hruška, J. Kopáček, P. Krám, C.D. Evans, F. Moldan, 2012,
Modelling soil nitrogen: The MAGIC model with nitrogen retention linked to carbon turnover using decomposer dynamics,
Environmental Pollution, 165, 158-166, [https://doi.org/10.1016/j.envpol.2012.02.021](https://doi.org/10.1016/j.envpol.2012.02.021)

\[Norling24\] Norling, M.D., Kaste, Ø., Wright, R.F., 2024, *A biogeochemical model of acidification: MAGIC is alive and well*, Ecological Research, [https://doi.org/10.1111/1440-1703.12487](https://doi.org/10.1111/1440-1703.12487)