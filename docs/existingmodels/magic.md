---
layout: default
title: MAGIC
parent: Existing models
nav_order: 3
---

# MAGIC

MAGIC is the Model of Acidification of Groundwater In Catchments.

MAGIC is used to fit historical trends of surface water response to acid deposition in order to predict future trends. For instance it can simulate changes due to recovery from acidification or changes in climate. It can also be used to simulate changes in soil chemistry due to changes in vegetation such as forest management and disruption.

This refers to the Mobius2 implementation. There are earlier implementations in FORTRAN by Bernard J. Cosby. The FORTRAN MAGIC v7 model was described in \[Cosby01\]. The FORTRAN model version numbers are not carried over into Mobius.

In the Mobius2 version the core ion balance model is mathematically identical to the one in MAGIC v7 and MAGIC v8, but in Mobius2 it is now possible for users to make their own additions and modifications to the mass balance transport and biogeochemical processes.

The latest paper describing the Mobius1 version is \[Norling24\]. Not all the features of the Mobius1 version (forest growth, advanced soil CNP processes) are available in Mobius2 yet.

The implementation of MAGIC in Mobius was funded by the CatchCaN project (The fate and future of carbon in forests), funded by the Technology Agency of the Czech Republic (TA CR) project number TO 01000220.

## Citations

\[Cosby01\] B. J. Cosby, R. C. Ferrier, A. Jenkins and R. F. Wright, 2001, Modelling the effects of acid deposition: refinements, adjustments and inclusion of nitrogen dynamics in the MAGIC model. Hydrol. Earth Syst. Sci, 5(3), 499-517 [https://doi.org/10.5194/hess-5-499-2001](https://doi.org/10.5194/hess-5-499-2001)

\[Norling24\] Norling, M.D., Kaste, Ø., Wright, R.F. *A biogeochemical model of acidification: MAGIC is alive and well*, Ecological Research, [https://doi.org/10.1111/1440-1703.12487](https://doi.org/10.1111/1440-1703.12487), 2024