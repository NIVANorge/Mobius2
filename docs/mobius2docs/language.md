---
layout: default
title: Model language
nav_order: 6
has_children: true
---

# The Mobius2 model building language

## Introduction

The Mobius2 language is a language for specifying models that can be run by the Mobius2 framework. Models are just-in-time compiled to machine code (using [LLVM](https://llvm.org/)) when they are loaded by one of the Mobius2 binaries (e.g. MobiView2 or mobipy).

The goal of the language is to be minimalistic and fast to write. It should flexible enough to allow you to specify a large range of model structures while still requiring minimal programming and letting the framework take care of most of the heavy lifting. This allows you to quickly write new models and test new processes. The integration with MobiView2 allows you to test the models during the entire design process and incorporate what you learn in a dynamic way.

If you are fluent in the language, it is so efficient to use that you can make modifications to your models to fit each specific use case if desired.

This documentation is under construction. We apologize for the inconvenience. For now see [existing models](https://github.com/NIVANorge/Mobius2/tree/main/models) for inspiration.

## Build your model with the Mobius2 language!

Whether you are a researcher who want to develop models to handle new research questions or a student learning about biogeochemical modelling, Mobius2 offers an easy way to focus on the mathematical specification of the models without doing the nitty-gritty implementation details or having to use a full programming language.

The framework offers an easy language syntax to quickly get new models up and running, and offers a user-friendly graphical user interface and data format for all models written in the language.

Still, models run as fast as in any other language, and can be complex enough to suit most research or management questions. Mobius models are operational in many projects.

Existing models and modules can be used as a starting point.