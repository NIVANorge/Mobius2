---
layout: default
title: Sensitivity and optimization
parent: MobiView2
nav_order: 2
has_children: true
---

# Sensitivity and optimization

MobiView2 includes four different modes of doing optimization, sensitivity and uncertainty analysis, each of which can be configured in many different ways.

- [Simple sensitivity](simplesensitivity.html) (single-parameter perturbation) lets you visually explore how a single result series is affected by perturbing a single parameter.
- [Autocalibration](autocalibration.html) can be used to find a set of parameter values that optimizes a given [goodness-of-fit statistic](statistics.html#goodness-of-fit).
- [MCMC](mcmc.html), or Markov Chain Monte Carlo, is an advanced form of Bayesian sensitivity and uncertainty analysis. It finds the posterior likelihood distribution of the set of parameter values, allowing you both to see how certain/uncertain a given parameter set is (given a likelihood structure of the observed data) and how correlated pairs of parameters are.
- [Variance-based sensitivity](variance.html) allows you to explore how much the variance of a chosen result statistic is affected by the variation of each selected parameter, giving you a sense of what parameters it is the most sensitive to.

If you need a more complex setup than the one you can make in MobiView2, we recommend that you use [mobipy](../mobipydocs/mobipy.html) to script it yourself instead.

## The common setup

Apart from the [simple parameter perturbation](simplesensitivity.html) setup, all the advanced forms of autocalibration and sensitivity analysis ([autocalibration](autocalibration.html), [MCMC](mcmc.html) and [variance-based sensitivity](variance.html)) share a common part of their setup:

![Common setup](../img/mobiview/optimsetup.png)

In the toolbar of the setup window you can click buttons to

- [Save](../img/toolbar/Save.png) Save the current setup for future use.
- [Load](../img/toolbar/Open.png) Load it again. Note that you must make sure that you are using the same data file for the model setup, this is not stored in the setup file.

### The parameter space

If you select a parameter in the main window, you can click ![Add parameter](../img/toolbar/Add.png) "Add parameter" in the setup to add it to the list. You can set the min and max values that you want to constrain this parameter with. Note that all values in the range have to be valid values that don't make the model crash. For the optimizer it helps if you can narrow down the range as much as possible before running it.

You can click ![Add group(../img/toolbar/AddGroup.png) "Add group" to add all the parameters that are visible in the main parameter view.

If an index set was set as locked ![Locked](../img/toolbar/Lock.png) in the main window at the time a parameter was added, all values across the indexes of that index set will be treated as a single parameter, and all of them will be given the same value every time the model is run.

### Symbols and expressions
You can set a "Symbol" per parameter. These must all be different. This is used both for reference in expressions and as a shorthand name in some statistic reports (the latter is only relevant for MCMC and variance-based sensitivity).

If a parameter is given an "Expression", it is no longer treated as a part of the parameter space that should be directly sampled by the algorithm you are running (so the min-max range is no longer relevant). Instead it will at each sample time be given the value of the expression. The Expression can refer to the Symbol of other parameters. The expression syntax uses the same math notation as the [Mobius2 model language](../mobius2docs/language.html). Example: `1.0 - 0.5*bfi`, where `bfi` is the Symbol of another parameter.

This can also be used to force two parameters to have the same value by making one have an Expression that just contains the Symbol of the other.

### Targets

To add a target statistic, select one result series and up to one comparison input series in the main window (similarly to how you [select them for plotting](plots.html)), then click ![Add parameter](../img/toolbar/Add.png) "Add target". You can select the target statistic from a dropdown list of statistics. The available statistic will depend on what kind of run you want (optimizer, MCMC or variance-based sensitivity). The total statistic that is considered in each model run is the weighted sum of the statistics for the individual targets, where the Weight per target is user-defined.

Statistics are computed inside the given Start and End interval (inclusive), which can be set per statistic. The "Error param(s)" is only relevant for the [MCMC](mcmc.html) setup, and is explained there.

### Timeout

The individual run timeout allows you to set a timeout on each individual run of the model in milliseconds. This can be useful if the model sometimes crashes on certain combinations of parameters, but it is hard to exclude them using the parameter intervals. If the model run times out, the calibration statistic is set to $\infty$ or $-\infty$ depending on whether the target statistic is being minimalized or maximized respectively. If you set the timeout negative, there is no timeout.