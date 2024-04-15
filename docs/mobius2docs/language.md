---
layout: default
title: Mobius2 language
parent: Mobius2
nav_order: 3
---

# The Mobius2 model building language

The Mobius2 language is a language for specifying models that can be run by the Mobius2 framework. Models are just-in-time compiled to machine code when they are loaded by one of the Mobius2 binaries (e.g. MobiView2 or mobipy).

The goal of the language is to be flexible enough to allow you to specify a large range of model structures while still requiring minimal programming and letting the framework take care of most of the heavy lifting.

Documentation is forthcoming. For now see [existing models](https://github.com/NIVANorge/Mobius2/tree/main/models) for inspiration.

Testing something:
```python
module("Simple-hydrology", version(1, 1, 0),
  soil : compartment
) {
  par_group("Hydrology soil", soil) {
    bfi : par_real("Baseflow index")
  }
}
```
