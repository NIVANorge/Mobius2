---
layout: default
title: Common declaration format
parent: Model language
nav_order: 2
has_children: true
comment: "While we use python markup for code snippets, they are not actually python, it just creates convenient coloring for this format."
---

# The common declaration format

The common declaration format is the format followed by all Mobius2 model, module, library and data files.

Although this specification is important to have for reference, it can be easier to work through some examples and follow the [guide](guide.html) rather than reading this documentation.

Each Mobius2 file is a sequence of *declarations* that can again contain nested declarations in various ways.

A Mobius2 file must always be [UTF-8](https://en.wikipedia.org/wiki/UTF-8). Except inside string literals, every character must be from the ascii (one-byte) subset of UTF-8. We recommend editing them with a plain text editor such as notepad++.

Files in the Mobius2 declaration format are whitespace agnostic, that is any number of whitespace, tabulars or newlines count as a single whitespace for the parser. Use of newlines and tabulars are therefore entirely for human readability reasons only.

## File types

Mobius2 uses the following file types
- Model files. These contain a single model declaration in the outer scope and nothing more. They have a `.txt` suffix.
- Module/Library files. These contain one or more modules or libraries in the outer scope. They have a `.txt` suffix.
- Data files. These contain a single `data_set` declaration in the outer scope. These have a `.dat` suffix.

## Comments.

Anywhere you put a `#` symbol, the rest of the line is treated as a comment and will be disregarded by the framework. You can also create multi-line comments using `/*` and `*/`.

## Token types

Each Mobius2 file is parsed as a sequence of *tokens* that are then further interpreted as a sequence of declarations. A token is one of the following

| Name | Description | Examples |
| ---- | ----------- | -------- |
| identifier | A sequence of ascii letters `a-zA-Z` possibly also containing underscores `_`, and possibly also containing digits `0-9`, but not as the first character. | `soil`, `water`, `Q10`, `r_denit` |
| quoted_string | Text between qotation marks. If single quotation marks, it must be on a single line. If triple quotation marks, it could be on multiple lines. | "Soil water", "Denitrification rate" |
| integer | A whole number. (In math scope (see below), a leading minus sign is interpreted as a separate token.) | `8`, `-5`, `3762` |
| real | Something representing a real number, possibly including [E notation](https://en.wikipedia.org/wiki/Scientific_notation#E_notation). | `23.5`, `-4e2`, `0.45e-4`, `nan` |
| boolean | A truth value | `true`, `false` |
| Operator, delimiter or bracket | These are many different token types used to group statements or create mathematical expressions | `+`, `-`, `*`, `/`, `|`, `%`, `&`, `!`, `=`, `!=`, `>`, `<`, `>=`, `<=`, `(`, `)`, `{`, `}`, `[`, `]`, `?`, `->`, `<-`, `->>`, `=>`, `=>>`, `,`, `.` |
| Date | A date value, only available in data files | `1999-12-31`, `2024-05-07` |
| Time | A time value, only available in data files | `12:55:33`, `05:30:00` |

## Entities and identifiers

Most (but not all) declarations create a model entity. If a model entity is created you may be able to bind it to an identifier. You can then use that identifier to refer to the entity in other parts of the code.

An entity identifier is declared by writing it followed by a colon `:` and the rest of the declaration. Example:

```python
my_identifier : <declaration>
```

## The declaration type

The declaration type is chosen from one of several different [pre-defined keywords](declaration_types.html). It is the first part of the declaration apart from the optional identifier. For instance

```python
soil : compartment <rest of declaration>
```

where `compartment` is the declaration type and `soil` is the identifier.

## The declaration arguments

A declaration can require zero or more arguments (and can some times allow different types of arguments). These are enclosed in parantheses `( .. )`, and arguments are separated by commas `,`. If there are no arguments, the parantheses are optional.

There are several types of arguments, and the required types depends on the declaration type.

- **Literal value**. These can be numbers, boolean values or quoted strings, e.g. `8`, `3.14`, `"Water"`, `true`.
- **Entity**. This is either
	- The identifier of another entity (it must be declared somewhere in the same scope, but not necessarily before the current declaration).
	- An inlined declaration creating the entity. The inlined declaration follows the same format as the outer declaration.
- **Location**. This is the [*location*](central_concepts.html#components-and-locations) of a state variable, i.e. a `.`-separated list of component identifiers.

For example,

```python
soil : compartment("Soil")

par_group("Soil hydrology", soil) <rest of declaration>

par_group("River parameters", river : compartment("River")) <rest of declaration>
```

A string literal must be on a single line if it is enclosed in single quotation marks, or can be across multiple lines if it is enclosed in triple quotation marks

```python
"""
Hello, biochemists!
"""
```

## Unit declarations

Units are declared using a completely separate format. See the [unit documentation](units.html).

## The declaration body

Some declarations can have or require a declaration body. Declaration bodies are always enclosed in curly brackets `{ .. }`. There are two main types of declaration bodies: Declaration body and math body. The body type of a declaration depends on the declaration type.

### Declaration scope bodies

Declaration scope bodies contain a sequence of other declarations. They create a scope so that identifiers declared inside the scope are not visible outside (The exception are parameter groups that export their parameter symbols to the outer scope). However identifiers in the outside scope are visible in the inside one.

### Docstrings

A declaration scope body can also contain a single so-called docstring. This can be used by the model creator (or dataset creator) to provide the user with information about how to use this model component. The docstring is a free-floating (typically multiline) string literal. It can contain [Markdown](https://en.wikipedia.org/wiki/Markdown)-formatted text.

### Math scope bodies

A math body is a \<block\> described in the [Mobius2 math format](math_format.html). The math expression typically computes a single value. Math scope bodies can reference certain types of entities that are visible in the outer scope, but you can not declare entities there, only local variables. Unlike the declaration scope, the math scope also has a strict ordering of expressions.

### Special

Some declarations have a body type that is unique to its declaration type. These will be explained separately.

### Examples

```python
module("The superduper hydrology module", version(1, 0, 0),
	soil : compartment,
	groundwater : compartment,
	water : quantity
) {
"""
A description of the module. It does
* Compute A.
* Some other things
* etc.
"""

	par_group("Soil hydrology", soil) {
		tc : par_real("Soil water time constant", [day], 5)
		fc : par_real("Soil field capacity", [m m], 50)
	}
	
	var(soil.water, [m m], "Soil water")
	
	flux(soil.water.flow, groundwater.water, [m m, day-1], "Recharge") {
		min(soil.water - fc, 0)/tc
	}
}
```
In the above example, the `module` declaration has a body with its own scope containing one `par_group`, one `var` declaration and a `flux` declaration. The par_group creates its own inner scope, but exports the parameter symbols from within it to the module scope. The `flux` declaration has a math body that is able to reference entities in the module scope.

## Names

Many declarations have a quoted string as their first argument or last. This argument is called a *name*, and can be used to interact with the entity from a system external to the model code, e.g. in [MobiView2](../mobiviewdocs/mobiview.html), [mobipy](../mobipydocs/mobipy.html) or in the [data format](../datafiledocs/datafiles.html).

Names are also scoped in a similar way to identifiers (but parameter names are unlike parameter identifiers scoped inside their parameter group).

## Notes

Following the main body (if it has one), a declaration can have zero or more notes (depending on the declaration type). These can provide additional information about the declaration. A note is started with a `@`, followed by a keyword. The set of allowed note keywords is different per declaration type. After this follows potentially an argument list (formatted the same way as for the main declaration), and potentiallt a body for the note. What arguments and/or body is required depends both on the main declaration type and the note type.

Example:
```python
# The @initial note has no arguments, but has a math body.
var(soil.water, [m m], "Soil water") @initial { fc }
```

## Data blocks

In a data file only, a declaration can have a single data block (there is no separate data block per note).

A data block is enclosed in square brackets `[ .. ]`. A data block can either encode a list, a map, or a directed graph.

### Lists

A list is a space-separated list of single values. The single values are either numbers, identifiers or quoted strings.

Examples:

```python
[0 8 3.2]
["Arable" "Semi-natural"]
```

### Maps

A map always opens with a `!` after the opening bracket. It is then a space-separated list of `key : value` pairs. The key is always a single value (number, identifier or string), while the value is a single value or a nested data block (either list or map).

Examples:

```python
[!
	0 : 4
	5 : 10
]

[!
	"Breiangen" : [ 4 5 6 ]
	"Vestfjord" : [ 7 8 9 ]
]
```

### Directed graph

A directed graph only appears as the data block for a directed graph connection in the data set. For now, see the [connection data documentation](../datafiledocs/new_project.html#connections).