# Mobius2

See the main site at
https://nivanorge.github.io/Mobius2/

## Introduction
Mobius2 is a modelling framework for coupling systems of different biochemical components to produce models with biochemical processes and mass balance-based transport networks.

The framework can be used to model any system ordinary differential equations or discrete step equations, but has special support for mass balance of transported substances, with automatic transport of dissolved substances.

Different modules for water transport (for instance soils, rivers, lakes) can be coupled without them knowing about one another, and biochemical components can be specified independently of the transport modules.

Mobius2 also provides special support for transport along directed graphs and grids. This can be used to model for instance branched river networks, downhill drainage, hydraulic systems, layered lakes, fjords and lagoons, and more.

Any model created in Mobius2 can be explored in the graphical user interface MobiView and interacted with through python in the mobipy package, without the model creator having to write any binding code between the model and various user interfaces.

**insert nice plots**

The old version of Mobius, Mobius 1, can be found here:
https://github.com/NIVANorge/Mobius
