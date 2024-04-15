---
layout: default
title: Model language
nav_order: 5
---

# The Mobius2 model building language

## Introduction

The Mobius2 language is a language for specifying models that can be run by the Mobius2 framework. Models are just-in-time compiled to machine code when they are loaded by one of the Mobius2 binaries (e.g. MobiView2 or mobipy).

The goal of the language is to be minimalistic and fast to write. It should flexible enough to allow you to specify a large range of model structures while still requiring minimal programming and letting the framework take care of most of the heavy lifting. This allows you to quickly write new models and test new processes. The integration with MobiView2 allows you to test the models during the entire design process and incorporate what you learn in a dynamic way.

If you are fluent in the language, it is so efficient to use that you can make modifications to the models to make them better fit each specific use case if desired.

This documentation is yet to be written. We apologize for the inconvenience. For now see [existing models](https://github.com/NIVANorge/Mobius2/tree/main/models) for inspiration.

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

## Build your model with the Mobius2 language

(to be written).