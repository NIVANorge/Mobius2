---
layout: default
title: Statistics
parent: Plots and statistics
grand_parent: MobiView2
nav_order: 0
---

# Series statistics

MobiView2 computes several statistics for the selected series. These are displayed in the time series info box. What series are displayed can be configured by clicking ![StatSettings](../img/toolbar/StatSettings.png) in the toolbar.

## The stat interval

You can select the stat interval below the time series info box. The interval consists of two dates, and only values between these dates (inclusive) will be considered in the statistics and goodness-of-fit computations.

**show example image**

## Goodness-of-fit

The goodness-of-fit statistics are displayed in the time series info box if you have exactly one result series and one input series selected, and the "Show GOF" box is checked.

During model calibration you can also see the changes in each statistic between runs. This is displayed to the right of the value, and is color coded to show if the change was good or bad. If the plot is in "compare baseline" mode, the changes in the GOF will be computed relative to the baseline, otherwise it is always relative to the previous run of the model.

Most of the goodness-of-fit statistics are implemented following \[Krause05\]. Further properties of the various statistics are discussed in that paper.

Let $o=\{o_i\}_{i\in I}$ be the observed time series, and let $m=\{m_i\}_{i\in I}$ be the modelled time series. The set $I$ of comparison points is the set of all time steps inside the stat interval (see Section \ref{sec:gofint} where both series have a valid value. For instance, the observed time series can have missing values, so the timesteps corresponding to the missing values will not be considered when evaluating goodness-of-fit. The stat interval is the entire model run interval unless something else is specified by the user. Let
$$
\overline{m} = \frac{1}{|I|}\sum_{i\in I}m_i
$$
denote the mean of a time series.


** to be continued **

Citations

\[Krause05\] P Krause, D. P. Boyle, and F. Bäse. *Comparison of different efficiency criteria for hdyrological model assessment*. Advances in Geosciences, 5, 89-97, [https://doi.org/10.5194/adgeo-5-89-2005](https://doi.org/10.5194/adgeo-5-89-2005), 2005
