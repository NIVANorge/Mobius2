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

## Table of contents
{: .no_toc .text-delta }

1. TOC
{:toc}

---

## Introduction
{: .no_toc }

In this document lists the specification of each declaration type. See the description of the [common declaration format](declaration_format.html).

For a better understanding of how to build a practical model using the declarations, see the [guide](guide.html).

Here we specify how these declarations work in model, module, preamble and library files. [Declarations in data_set files function differently](../datafiledocs/new_project.html).

**This document is currently incomplete**. New guide chapters are prioritized over finishing this.

## Signatures
{: .no_toc }

In this document we will denote declaration signatures (i.e. what arguments they accept) using the following format

```python
decl_type(argname1:type1, argname2:type2, ..)
```

Here `decl_type` is the type of the current declaration, each `argname` is usually only something we use in this document when we refer to the argument in the text. You don't refer to it in the code. For instance, the signature `constant(name:quoted_string, u:unit, value:real)` means you can declare constants like

```python
euler : constant("Euler number", [], 2.71)
```

If an argument is literally called `name:quoted_string`, it has the formal semantics of an [entity name](central_concepts.html/names-and-identifiers).

The `type` is one of the following
- A simple literal type, like `quoted_string`, `real`, `integer`, `boolean`. In this case, the argument must be a single literal value, e.g. `8`, `3.14`, `"Water"`, `true`.
- Another declaration type. In this case you must pass either the identifier of a declared entity of that type, or inline a declaration of that type.
- `location`. Here you must pass a location identifier, e.g. `soil.water`. In some cases these can have bracketed restrictions (this will be specified individually). Alternatively, you can pass the identifier to a `loc` entity.
- `any`. Any argument type, but limitations may be specified in the text.

If an argument is specified as `argname:type...`, it means you can pass multiple arguments of that type after one another

If an argument type is `(type1|type2|..)` it means it can have any of those types.

If several signatures are listed, they are different alternatives.

If you see **Bind to identifier: yes** it means that the declaration creates an identity that can be bound to an identifier, e.g. `some_identifier : decl(...)`.

## model

Context: File top scope

Bind to identifier: no

Signature:

```python
model(name:quoted_string) { <declaration-body> }
```

Only a single model declaration can appear in each file.

This is the specification of a Mobius2 model. All parts of the model are created or loaded as a consequence of declarations in the body of the `model` declaration.

## module

Context: One of
- model scope (called inlined module)
- File top scope.

Bind to identifier: no

Signatures:

```python
module(name:quoted_string, v:version) { <declaration-body> }
module(name:quoted_string, v:version, load_arguments:any...) { <declaration-body> } # (top scope only)
```

Multiple modules can be declared in the same file.

Load arguments can be provided if the module is *not* inline-declared. The load arguments are a list of identifiers that are passed to the module from the loading model. **(to be continued)**.

Formally, a `module` declaration internally creates a `module_template`. It is then instantiated when it is loaded using a `load` declaration, and you can instantiate a `module` several times with different load arguments.

An inlined module declaration is immediately loaded (instantiated), and can only have one instance.

## preamble

## library

## version

Context: module/preamble scope (argument only).

Bind to identifier: no

Signature:

```python
version(major:integer, minor:integer, revision:integer)
```

The only purpose of the `version` is to document `module` and `preamble` versions.

## load

## extend

## compartment, property, quantity

Context: `model` scope (all), or module/preamble scope (property only).

Bind to identifier: yes

Signature:

```python
compartment(name:quoted_string)
compartment(name:quoted_string, distribution:index_set...)
quantity(name:quoted_string)
quantity(name:quoted_string, distribution:index_set...)
property(name:quoted_string)
property(name:quoted_string) { <math-body> }
```

These entities are collectively known as "component".

A distribution can create one copy of all state variables with this component in their *location* (whether or not that happens in practice comes down to several factors - will be documented later). It can also be used to distribute [parameter groups](#par_group).

If a property has a math body, that is called the "default code" for that property. If you create a state variable (`var`) with that property as its last location component and the state variable itself doesn't have a math body, the default code will be used instead. Note that the default code will be resolved separately per state variable it is used for, using the state variable location as the [context location](math_format.html#the-context-location). However, the scope it is resolved in is still the scope it is declared in.

## par_group

Context: model, module or preamble.

Bind to identifier: no

Signature:

```python
par_group(name:quoted_string) { <declaration-body> }
par_group(name:quoted_string, distributes_like:(compartment|quantity)...) { <declaration-body> }
```

Optional notes:

```python
@index_with(distribution:index_set...)
```

A parameter group is created to embody a collection of parameters. It determines how the parameters within it can be distributed.

If a parameter group `distributes_like` one or more components, it will have a maximal distribution that is the union of the distributions of these components. Whether or not the group will have that full distribution in a particular model application depends on the [data set](../datafiledocs/new_project.html#parameter-groups).

The optional note `@index_with` can be used to set the maximal distribution directly, but this should only be used in rare instances where the default way to do it doesn't work.

## par_real, par_int, par_bool, par_enum

Context: par_group scope.

Bind to identifier: yes

Signature:

```python
par_real(name:quoted_string, u:unit, default:(real|integer))
par_real(name:quoted_string, u:unit, default:(real|integer), description:quoted_string)
par_real(name:quoted_string, u:unit, default:(real|integer), min:(real|integer), max:(real|integer))
par_real(name:quoted_string, u:unit, default:(real|integer), min:(real|integer), max:(real|integer), description:quoted_string)
par_int(name:quoted_string, u:unit, default:integer)
par_int(name:quoted_string, u:unit, default:integer, description:quoted_string)
par_int(name:quoted_string, u:unit, default:integer, min:integer, max:integer)
par_int(name:quoted_string, u:unit, default:integer, min:integer, max:integer, description:quoted_string)
par_bool(name:quoted_string, default:boolean)
par_bool(name:quoted_string, default:boolean, description:quoted_string)
par_enum(name:quoted_string, default:identifier) { <special> }
par_enum(name:quoted_string, default:identifier, description:quoted_string) { <special> }
```

These entities are collectively known as "parameter". Parameters are values that are held constant through each single model run, but unlike `constant`s, can be configured to have different values in e.g. data files or MobiView2. Parameters can also be distributed over index sets (this is determined by the `par_group` they are in).

- The `default` value is what you get for the parameter value if a specific value is not provided in the data set.
- The optional `min` and `max` values are guidelines to the user only, and the model will not complain if the user sets a value outside this bound. The bound is also the defaut bound for [sensitivity and optimization](../mobiviewdocs/sensitivity.html) setups.
- The optional `description` is displayed in MobiView2, and can be used to guide the user how to use this parameter.

The body of a `par_enum` must be a space-separated list of identifiers giving the set of values this parameter can have. This functions as the declaration of these identifiers. The `default` value must be one of these identifiers.

## constant

Context: model, module, preamble, library scopes.

Bind to identifier: yes

Signature:

```python
constant(name:quoted_string, u:unit, value:(real|integer))
constant(value:boolean)
```

A constant is a single value (can not be distributed) that can be referenced in function scope. It can be useful to put constants as named entities like this so that they don't appear as "magic value" literals directly in the code. This gives a form of documentation of what the constant is.

## function

Context: model, module, preamble, library scopes.

Bind to identifier: yes

Signature:

```python
function(<special>) { <math-body }
```

Functions in Mobius2 are meant to be used as simple code snippets that can be reused in many places.

The arguments to a function declaration declares the signature of how it can be used in a [function evaluation](math_format.html#function-evaluation). It is a comma-separated list of items, where an item is either
- A plain `identifier`
- Or an `identifier:unit` pair.
This identifier does not refer to an existing entity, instead it names that function argument inside the math body. If a unit is given, that argument is required to have that unit at every evaluation site.

For instance,

```python
very_fun : function(a, b) { a*b + 3 }
```

Can in a different math body be evaluated using the syntax

```python
very_fun(20, 10)   # Evaluates to 20*10 + 3 = 203
```

Declared functions are always "inlined" at every evaluation site, meaning a separate copy of the function body is resolved and pasted in at that site. This means that you can't have recursive functions (functions that call themselves), but that may be implemented separately later.

The body of a `function` is limited in that it can not refer to parameters or state variables directly, instead you must pass their values in as arguments. You can however refer to constants.

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

Context: model, module, preamble, library, par_group.

Bind to identifier: yes

Signature:

```python
[<special>]
unit_of(par_real|par_int|constant|location)
compose_unit(u1:unit, u2:unit...)
```

Almost every value in Mobius2 must have a unit, and this is used by the framework to provide automatic unit conversions and do unit checks on expressions.

Regular unit declarations are on a [special format](units.html#the-unit-declaration-format).

The `unit_of` declaration refers to the unit of another entity like a parameter, constant or state variable.

The `compose_unit` declaration uses unit arithmetic to multiply several units together.

## discrete_order

## external_computation
