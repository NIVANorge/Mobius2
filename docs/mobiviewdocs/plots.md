---
layout: default
title: Plots and statistics
parent: MobiView2
nav_order: 1
has_children: true
---

# Plots and statistics

You can use the plot to visualize inputs and results of the model. Time series can be selected in the result and model input trees to the left of the plot.
- You can select multiple time series at one time by ctrl-clicking (or shift-clicking) them.
- You can remove a selected time series by ctrl-clicking it. 
- If a time series varies over one or more index sets, you can select indexes from the lists below the plot. You can do multiselection (ctrl-click) here too.

If you want multiple plot axes displayed at the same time, you need to open the [additional plot window](additionalplots.html) by clicking ![Additional plots](../img/toolbar/ViewMorePlots.png) in the toolbar.

The time series info box will display statistics about the selected time series. It can display [goodness-of-fit statistics](statistics.html).

The plot will automatically update itself every time you run the model ![Run](../img/toolbar/Run.png) to reflect any changes in the result time series.

## Navigation

In many of the plot modes you can scroll and zoom the plot.

- Zoom the plot along the x-axis by using the scroll wheel on the mouse or click `ctrl plus` and `ctrl minus`.
- Scroll the plot along the x-axis by holding down the left button on your mouse and moving it. You can also use `ctrl left-arrow` and `ctrl right-arrow`.
- To zoom or scroll the y-axis you can right click the plot and remove the check on "Attach Y Axis".

## Plot options

The availability of the following options depend on the plot mode.

**Scatter sparse**. If this option is checked it will use a scatter plot to display series that have isolated points (typically input series of observations).

**Aggregation**. You can aggregate series over larger time intervals. The available aggregations are always larger than the sampling frequency of your current data set. You can also choose an aggregator function such as mean, sum, min, max, freq (the last one just counts the number of valid values per interval). If your aggregation interval is yearly, you can choose the pivot month in the dropdown list at the bottom.

**Transform**. You can choose transforms for the y axis.
- Regular Y axis. This applies no transform.
- Normalized. Every series is scaled independently so that it ranges between 0 and 1.
- Logarithmic. This appilies a logarithmic scale to the y axis.

## Plot modes

Plot modes can be chosen to visualize the selected series in different ways.

### Regular

### Stacked

### Stacked share

### Histogram

### Profile

### Profile2D

### Compare baseline

### Residuals

### Residuals histogram

### Quantile-Quantile

## Context menu options (edit or save plot)