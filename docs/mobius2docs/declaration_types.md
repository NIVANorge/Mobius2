---
layout: default
title: Declaration types
parent: Common declaration format
grand_parent: Model language
nav_order: 0
comment: "While we use python markup for code snippets, they are not actually python, it just creates convenient coloring for this format."
---

# The declaration types
{: .no_toc }

In this document lists the specification of each declaration type. For a better understanding of how to use them together, see the [guide](guide.html).

Here we specify how these declarations work in model, module, preamble and library files. [Declarations in data_set files function differently](../datafiledocs/new_project.html).

**This document is currently incomplete**. New guide chapters are prioritized.

## Signatures
{: .no_toc }

(will explain the signatures here)

## Table of contents
{: .no_toc .text-delta }

1. TOC
{:toc}

---

## model

Context: The only declaration in the top scope of a file.

Signature:

```python
model(name:quoted-string) { <declaration-body> }
```

This is the specification of a Mobius2 model. It has a declaration body that creates many of the model entities and loads the modules that the model is composed of.

## module

Context: One of
- *inlined*, e.g. declared directly in the body of a `model`
- In the top scope of a file. Multiple modules can be declared in the same file.

Signatures:

```python
module(name:quoted-string, v:version) { <declaration-body> }
module(name:quoted-string, v:version, load-arguments...) { <declaration-body> }
```

Load arguments can not be provided if the module is inline-declared. The load arguments are a list of identifiers that are passed to the module from the loading model. (to be continued).

## preamble

## library

## version

Context: `module` scope (currently only as the version argument to a module).

Signature:

```python
version(major:integer, minor:integer, revision:integer)
```

## load

## extend

## compartment, property, quantity

## par_group

## par_real, par_int, par_bool, par_enum

## constant

## function

## var

## flux

## loc

## index_set

## connection

## solver

## solve

## aggregation_weight

## unit_conversion

## \[unit\], unit_of, compose_unit

## discrete_order

## external_computation
