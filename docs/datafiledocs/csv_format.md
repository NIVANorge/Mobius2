---
layout: default
title: The csv format
parent: Data files
nav_order: 1
---

# The CSV format

The `.csv` format for Mobius2 time series inputs is a plain-text format. It is not strictly a "comma-separated" file, but the format is close enough to fall within the same general family.

Like the common declaration format, these files must be UTF-8. Moreover, these files are also whitespace-agnostic. That is, a string of space-like characters (tabs etc.) are seen as a single space.

## Date indexing or not date indexing.

There is a simple format where data rows are not date indexed. In that case the file starts with a single [date(-time)](datafiles.html#datetime-format) which is the common start date of all series in this file. The value rows are then assumed to proceed from the start date (inclusive) with the data set [sampling step](datafiles.html#the-sampling-step) frequency.

If you use date indexed value rows, there is no date at the top of the file, and it instead starts directly with the header.

## The header.

The header is a single row of space-separated input names, optionally with indexes and flags. In the simplest example, the header is a space-separated sequence of quoted names,

```csv
"Air temperature"  "Precipitation"
<the value block>
```

If a series is not "global", but is instead indexed over one or more [index sets](new_project.html#index-sets-and-distributions), you can provide it with an index tuple indicating index set:index pairs inside a square bracket as follows

```csv
"Fertilizer N addition"["Subcatchment":"Kråkstadelva" "Landscape units":"Arable"] <rest of header>
<the value block>
```

Note that all instances of the same input name must index over the same index sets (even if they are provided in different files).

If you want the same series to be applied to several index tuples, you can add more index tuples after the name

```csv
"Precipitation"["Subcatchment":"Kråkstadelva"]["Subcatchment":"Kure"] <rest of header>
<the value block>
```

You can also attach [flags](datafiles.html#series-flags) to a series by putting them in a separate square bracket

```csv
"NO3 deposition"["Landscape units":"Forest"][linear_interpolate [k g, ha-1, year-1]] <rest of header>
<the value block>
```

## The value block

If you are not date indexing, you provide one value per series per row. If you are date indexing you also start every row with a [date(-time)](datafiles.html#datetime-format).

Not date indexing:

```csv
2005-01-01
"Air temperature"  "Precipitation"
2.91	1.96
4.37	0.4
2.53	2.98
-0.57	4.72
```

Date indexing:

```csv
"Air temperature"  "Precipitation"
2005-01-01	2.91	1.96
2005-01-02	4.37	0.4
2005-01-03	2.53	2.98
2005-01-04	-0.57	4.72
```

