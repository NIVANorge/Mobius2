---
layout: default
title: Getting started
parent: Mobius2
nav_order: 0
---


# Getting started

## Download Mobius2 and run a model

The easiest way to run models in Mobius2 is to use the MobiView2 graphical user interface. You need to
	1. Get the Mobius2 repository from github.
	2. Get the MobiView2 program.
	
### 1. Get the Mobius2 repository from github.

The reason you want to get the Mobius2 repository is that it contains many existing models and data set examples that you can work with. You will not need to compile the Mobius2 framework itself.

Clone the repository [https://github.com/NIVANorge/Mobius2](https://github.com/NIVANorge/Mobius2). This can be done by using your favourite git client such as [Tortoise git](https://tortoisegit.org/) or [Github desktop](https://desktop.github.com/).

You can also download a zip archive of the repository from the front page, but then it is not as convenient to get the latest updates.

### 2. Get the MobiView2 program.

For now we only support MobiView2 on Windows. It is technically possible to compile it on Linux, so if you need that, please contact us.

Open ftp://mobiserver.niva.no/Mobius2 in a file explorer (not a web browser), then download the entire MobiView2 folder.

Edit MobiView2/config.txt so that the "Mobius2 base path" field contains the location where you put Mobius2, e.g. "C:/Data/Mobius2".

You can now run MobiView2.exe. Click the open icon in the top left and select e.g. "Mobius2/models/simplyq.txt", then "Mobius2/models/data/simplyq_simple.dat". If the model loaded correctly you can now run it by clicking the orange runner icon in the MobiView2 top bar. You can then select series to plot in the result and input series selections in the bottom right.

![MobiView2](/img/mobiview2.png)

Next, you can select a parameter group in the top left, which will allow you to edit parameter values in the top center. After changing some values, you can re-run the model and see 

## Further steps

What you want to do next depends on what you want to use Mobius2 for, so not all the options below may be relevant to you.

(to be written)
Get to know the existing models.
Learn more about MobiView2.
Learn the data format to set up models for other locations.
Use mobipy to script your model runs.
Build new models or modify existing ones using the Mobius2 language.
Involve yourself with feedback to the developer team, or become a developer.
