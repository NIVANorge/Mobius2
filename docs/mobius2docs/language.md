---
layout: default
title: Model language
nav_order: 7
has_children: true
---

# The Mobius2 model building language

## Introduction

The Mobius2 language is a language for specifying models that can be run by the Mobius2 framework. Models are just-in-time compiled to machine code (using [LLVM](https://llvm.org/)) when they are loaded by one of the Mobius2 programs (e.g. MobiView2 or mobipy).

Some goals of the language are to
- be minimalistic, fast and easy to write.
- be flexible enough to allow you to specify a large range of model structures, including most structures used in hydrological and water quality models.
- let the framework take care of most of the heavy lifting, allowing the modeller to focus on mathematical specification.
- be self-documenting in the sense that model code reads like a model description.
- reduce the effort needed to get unit conversions correct.
- have models run as quickly as computationally possible.

This design allows you to quickly write new models and integrate and test new processes with existing models. The user interface MobiView2 allows you to visualize the model results and compute goodness-of-fit statistics during the entire design process and incorporate what you learn from this in a dynamic iterative way.

## Build your model with the Mobius2 language!

Whether you are a researcher who wants to develop models to handle new research questions or a student learning about hydrological or biogeochemical modelling, Mobius2 offers an easy way for you to focus on the specification of the models without having to do the nitty-gritty implementation details or having to use a full programming language.

The framework offers a simple language syntax to quickly get new models up and running, and offers a user-friendly graphical user interface and data format for all models written in the language.

Still, models run as fast as in any other compiled language, and can be complex enough to suit most research and management questions. Mobius models are already operational in many real-world projects.

Existing models and modules covering a large range of systems can be used as a basis for modification or inspiration.

The [Guide](guide.html) is a good place to start to learn the language.