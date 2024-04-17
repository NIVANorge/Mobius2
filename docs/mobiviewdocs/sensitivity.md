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

## The common setup

Apart from the [simple parameter perturbation](simplesensitivity.html) setup, all the advanced forms of autocalibration and sensitivity analysis ([autocalibration](autocalibration.html), [MCMC](mcmc.html) and [variance-based sensitivity](variance.html)) share a common part of their setup:

(to be written).