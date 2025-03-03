---
layout: default
title: Getting started
nav_order: 1
---


# Getting started

## Download Mobius2 and run a model

The easiest way to run models in Mobius2 is to use the MobiView2 graphical user interface. You need to
1. Get the Mobius2 repository from github.
2. Get the MobiView2 program.
	
### 1. Get the Mobius2 repository from github.

The reason you want to get the Mobius2 repository is that it contains many existing models and data set examples that you can work with. You will not need to compile the Mobius2 framework itself.

Clone the repository [https://github.com/NIVANorge/Mobius2](https://github.com/NIVANorge/Mobius2). This can be done by using your favourite git client such as [Tortoise git](https://tortoisegit.org/) or [Github desktop](https://desktop.github.com/) (or [git command line](https://git-scm.com/book/en/v2/Git-Basics-Getting-a-Git-Repository)).

You can also download a zip archive of the repository from its github front page (click the green "Code" button, then "Download zip"), but then it is not as convenient to get the latest updates.

### 2. Get the MobiView2 program.

For now we only support MobiView2 on Windows. It is theoretically possible to compile it on Linux, so if you need that, please contact us. The python and Julia packages mobipy and mobius.jl can also be used to run the models on Linux. See below.

Open ftp://mobiserver.niva.no/Mobius2 in a file explorer (not a web browser), then download the entire MobiView2 folder by copying it over to somewhere on your machine.

In your local copy, edit MobiView2/config.txt so that the "Mobius2 base path" field contains the location where you put the Mobius2 repository, e.g.
```python
config("Mobius2 base path", "C:/Data/Mobius2/")
```

Try to run MobiView2.exe . If it doesn't open a window, you need to install the [Microsoft Visual Studio redistributables](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170). You should use the installer `vc_redist.x64.exe`.

You can now run MobiView2.exe. Click the ![Open](../img/toolbar/Open.png) open icon in the top left and select e.g. `"Mobius2/models/simplyq_model.txt"`, then `"Mobius2/models/example_data/simplyq_simple.dat"`. If the model loaded correctly you can now run it by clicking the ![Run](../img/toolbar/Run.png) runner icon in the MobiView2 toolbar. You can then select the series to plot in the result and input series selections in the bottom right.

If you chose `simplyq_model.txt`, you are running [SimplyQ](../existingmodels/simply.html#simplyq), which is a simple hydrology model for predicting river discharge.

![MobiView2](../img/mobiview_gettingstarted.png)

Next, you can select a parameter group in the top left, which will allow you to edit parameter values in the top center. After changing some values, you can re-run the model and see the changes to the model predictions in the plot view.

## Further steps

What you want to do next depends on what you want to use Mobius2 for, so not all the options below may be relevant to you.

### Get to know the existing models.

A good place to start is to get aquainted with the [existing models](../existingmodels/existingmodels.html) to see if they suit your modelling needs or to use them as an inspiration for your own models. Most of these come with example datasets in the repository. You open these models from `Mobius2/models`, and the example data files are in `Mobius2/models/data`.

### Learn the data format to set up models for other locations.

To set up a model for a new location you may need to edit the [data files](../datafiledocs/datafiles.html) in a text editor. It can be a good idea to use an existing example as a base instead of starting from scratch.

### Learn more about MobiView2.

MobiView2 contains many more features that can help you to quickly calibrate or autocalibrate your model, generate various types of plots, and run sensitivity analysis.

See [the full documentation](../mobiviewdocs/mobiview.html).

### Use mobipy to script your model runs.

The mobipy python package allows you to dynamically set parameter values and input series, and exctract result series from the model via python. This is useful if you want to script model runs to e.g.
- Run many different scenarios where you load data from some secondary source, without having to make many separate model data files.
- Script your own sensitivity analysis or autocalibration.
- Run the models in the backend of a web page with a custom web interface.

If you are on Windows, to be able to run mobipy you need to download mobipy/c_abi.dll from ftp://mobiserver.niva.no/Mobius2 and put it in your local Mobius2/mobipy folder. If you are on Linux, see the [Linux installation guide](../mobipydocs/linux_install.html).

See [the full documentation](../mobipydocs/mobipy.html).

### Build new models or modify existing ones using the Mobius2 language.

Mobius2 models are specified in the Mobius2 language. If you need to make modifications to existing modules, make new modules, or combine modules to new models you need to learn how to use this language. The language is designed to make it easy to use even if you don't know much programming, and it does most of the heavy lifting for you.

The [Guide](../mobius2docs/guide.html) is a good place to start.

### Involve yourself with feedback to the developer team, or become a developer.

Please use the [github issues tracker](https://github.com/NIVANorge/Mobius2/issues) to report bugs. You can also contact us at `magnus.norling@niva.no`