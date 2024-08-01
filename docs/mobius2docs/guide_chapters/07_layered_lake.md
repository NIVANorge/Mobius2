---
layout: default
title: 07 Simple layered lake
parent: Guide
grand_parent: Model language
nav_order: 6
comment: "While we use python markup for code snippets, they are not actually python, it just creates convenient coloring for this format."
---

# Simple layered lake

In this chapter we will set up the model complexity a notch and work with a 1-dimensional model of a lake. That it is 1-dimensional means that we will consider the state of the lake (temperature, various concentrations, etc.) to be the same across each horizontal layer for any given depth, but will vary as you change the depth (along the z axis). 1-dimensional models are often good approximations for smaller lakes.

We will base ourselves on a simplified version of the formulation of the MyLake model \[SalorantaAndersen07\]. In this first chapter we will just make a basin that with precipitation inputs and discharge outputs, but it will not be connected to a catchment that it receives river discharge from yet. The power of Mobius2's ability to couple different modules will be shown in the next chapter, where we will connect the lake to our existing catchment model.

We will not give as detailed a description of all the code in this chapter as we did in previous chapters, instead we will just highlight what is new.

**Chapter remains to be finished**




[Full code for chapter 07](https://github.com/NIVANorge/Mobius2/tree/main/guide/07).

## Citations

\[SalorantaAndersen07\] MyLake—A multi-year lake simulation model code suitable for uncertainty and sensitivity analysis simulations, Tuomo M. Saloranta and Tom Andersen 2007, Ecological Modelling 207(1), 45-60, [https://doi.org/10.1016/j.ecolmodel.2007.03.018](https://doi.org/10.1016/j.ecolmodel.2007.03.018)