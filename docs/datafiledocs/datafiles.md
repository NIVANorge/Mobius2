---
layout: default
title: Data files
nav_order: 6
has_children: true
---

# The Mobius2 data formats

To run a Mobius2 model you need to provide it with site specific data for parameters, forcings (and sometimes more, like topological information).

A model combined with site-specific data is called a *model application*.

Each Mobius2 model application has a single main data file with a `.dat` suffix.

The main data file uses the [common declaration format](../mobius2docs/declaration_format.html), but you don't need to fully learn this format to use existing Mobius2 models. Instead we provide a [guide to set up a new project from an existing example file](new_project.html).

Input time series (forcing and comparison data) are not put directly in the main data file, instead the main data file says what other files series data is loaded from.

Time series can be provided either on a [`.csv`](csv_format.html) or [`.xlsx`](xlsx_format.html) format.

## Model inputs and comparison series

Model inputs are forcings that are used by the model for computations. These are declared as [variables](../mobius2docs/central_concepts.html#properties) in the module files, and show up under "Input data series" in [MobiView2](../mobiviewdocs/plotting.html).

You can put as many comparison time series you want into your data files using the same format as model series (name them anything that is not the name of a model series). These will be loaded and displayed as "Additional series", but not used in the model computation. This is useful for instance when you want to plot your model results against observations, compute [goodness-of-fit statistics](../mobiviewdocs/statistics.html), [autocalibrate](../mobiviewdocs/sensitivity.html) etc.

## The sampling step

The sampling step is both the main time step of the model run and the step size for input data (these could maybe be decoupled in the future). The sampling step is by default 1 day, but can be changed with a `time_step` declaration in the main data file, e.g.

```python
time_step([4, hr])  # 4 hour time step.
```

Each input series value is held constant over a single sampling step even if the model uses ODE solvers that evaluate at fractional steps, so you should set the sampling step to the frequency you want the input data to vary with.

If more than one series value is provided for the same series and time, only the last processed value will be used. This could happen if the series data has a higher frequency than the sampling step so that two time stamps are rounded to the same model sampling step. We may implement automatic aggregation in these instances later, but for now you have to do that yourself before you prepare the input file.

If some of the series have a lower frequency than the desired sampling step, you could use interpolation to fill the gaps (see below).

## Datetime format

Date(-time) values in data files (except some times in `.xlsx` files) are on one of the following formats:

```
YYYY-mm-dd
YYYY-mm-dd HH:MM:SS
```

Alone, `YYYY-mm-dd` is equivalent to `YYYY-mm-dd 00:00:00`.

## The series interval

The *series interval* is the largest interval that fits all the series data that is provided in the data set. When loaded into a model application, all series will for technical reasons be expanded to fill this entire window.

The model can only be run if the "Start date" and "End date" parameters of the model run fit within this interval.

A given data set can cut this interval down by specifying a `series_interval` in the main data file, e.g.

```python
series_interval(2000-01-01, 2020-01-01)
```

Series values outside this specified interval are then discarded. This can be useful to save memory if the source data is very large and you only want to run the model for shorter intervals.

## Missing values

If a model input series is not provided with data, it is filled as 0. If it is provided with some values and you don't interpolate (see below), missing values are also filled with 0. You should avoid this unless 0 is actually a legitimate value for this input. 

Missing values in comparison series are filled with nan (non-numbers). Missing values in observation series are ignored when e.g. computing goodness-of-fit stats, allowing you to only compare at the dates where you have observation data.

## Series indexing

A given input series can either have a single instance, or can be distributed over a tuple of one or more [index sets](../mobius2docs/central_concepts.html#index-sets-and-distributions). This can e.g. be used to provide separate meteorological data for different sub-areas of the model domain.

What index sets you are allowed to distribute a model input series over depends on the components forming the *location* of the series and what index sets they are distributed over in the model. Comparison series can be distributed over any combination of index sets that exist in the model.

The distribution must be the same for all instances of the same input name, even if they come from separate files. This means that each series of the same name must have indexes from the same tuple of index sets (though the indexes themselves could of course be different).

How to distribute a series over index sets is described in the pages for the individual series data formats.

## Series flags

Both series formats allow you to specify flags. A flag is either a [unit declaration](../mobius2docs/units.html#the-unit-declaration-format) or one of the following:

- **Interpolation flags** Interpolation flags tell Mobius2 to fill in missing values in the data. Values will be filled for every *sampling step*. The following interpolation flags are available
	- `step_interpolate`. Fills missing values with the last valid value before it.
	- `linear_interpolate`. Uses linear interpolation to fill missing values between valid values.
	- `spline_interpolate`. Uses [cubic spline interpolation](https://en.wikipedia.org/wiki/Spline_interpolation) to fill missing values between valid values. This is the smoothest option.
	- `inside`. If this flag is provided, any interpolation will only fill in values between missing values. If the flag is not present, interpolation will also expand the first and last valid values to fill the entire *series interval*.
- `repeat_yearly`. With this flag the first year of the time series is repeated yearly to fill the entire *series interval*.

To put multiple flags they can be listed after one another separated by spaces.

It is especially important to specify an interpolation method for every model forcing in the data set if you plan to run the model on a sampling step that is shorter than the sampling step in the data file, otherwise you will get missing values in the forcings.

