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

Let $$o=\{o_i\}_{i\in I}$$ be the observed time series, and let $$m=\{m_i\}_{i\in I}$$ be the modelled time series. The set $$I$$ of comparison points is the set of all time steps inside the stat interval where both series have a valid value. For instance, the observed time series can have missing values, so the timesteps corresponding to the missing values will not be considered when evaluating goodness-of-fit. The stat interval is the entire model run interval unless something else is specified by the user. Let

$$
\overline{m} = \frac{1}{|I|}\sum_{i\in I}m_i
$$

denote the mean of a time series.

### Data points

The data points is the size of the set of comparison points $$I$$, denoted $$\|I\|$$.

### Mean error (bias)

The mean error is

$$
\overline{o - m} = \overline{o} -\overline{m} =\frac{1}{|I|} \sum_{i\in I} (o_i - m_i)
$$

For fluxes, the mean error is related to the discrepancy in mass balance.

### MAE

MAE is the mean absolute error

$$
\frac{1}{|I|}\sum_{i\in I}|o_i - m_i|,
$$

### RMSE

RMSE is the root mean square error

$$
\sqrt{\frac{1}{|I|}\sum_{i\in I}(o_i-m_i)^2}.
$$

### N-S

N-S is the Nash-Sutcliffe efficiency coefficient \[NashSutcliffe70\]

$$
1 - \frac{\sum_{i\in I}(o_i - m_i)^2}{\sum_{i\in I}(o_i-\overline{o})^2}.
$$

This coefficient takes values in $$(-\infty, 1]$$, where a value of 1 means a perfect fit, while a value of 0 or less means that the modeled series is a no better predictor than the mean of the observed series.

### log N-S

log N-S is the same as N-S, but where $$o_i$$ is replaced by $$\ln(o_i)$$ and $$m_i$$ by $$\ln(m_i)$$ for each $$i\in I$$. Here $$\ln$$ denotes the natural logarithm.

$$
1 - \frac{\sum_{i\in I}(\ln(o_i) - \ln(m_i))^2}{\sum_{i\in I}(\ln(o_i)-\overline{\ln(o)})^2}.
$$

This coefficient behaves similarly to N-S, but is less sensitive to errors on time steps where both series have large values.

### r2
$$r^2$$ is the coefficient of determination

$$
\left(\frac{\sum_{i\in I}(o_i-\overline{o})(m_i-\overline{m})}{\sqrt{\sum_{i\in I}(o_i-\overline{o})^2}\sqrt{\sum_{i\in I}(m_i-\overline{m})^2}}\right)^2.
$$

This coefficient takes values in $$[0, 1]$$.

** to be continued **

## Citations

\[Krause05\] P Krause, D. P. Boyle, and F. Bäse. *Comparison of different efficiency criteria for hdyrological model assessment*. Advances in Geosciences, 5, 89-97, [https://doi.org/10.5194/adgeo-5-89-2005](https://doi.org/10.5194/adgeo-5-89-2005), 2005.

\[NashSutcliffe70\] J. E. Nash and J. V. Sutcliffe. *River flow forcasting through conceptual models part I - A discussion of principles*. Journal of Hydrology, 10, 282-290, [https://doi.org/10.1016/0022-1694(70)90255-6](https://doi.org/10.1016/0022-1694(70)90255-6) 1970.


{% include lib/mathjax.html %}