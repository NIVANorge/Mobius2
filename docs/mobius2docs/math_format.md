---
layout: default
title: Math format
parent: Common declaration format
grand_parent: Model language
nav_order: 2
comment: "While we use python markup for code snippets, they are not actually python, it just creates convenient coloring for this format."
---

# The Mobius2 math format

Math expressions in Mobius2 are for the most part written using a purely functional style. This means that almost every expression evaluates to a value, and there are no side effects. (There are exceptions to this, but they are only used in rare occations).

Like the entire [Mobius2 declaration format](declaration_format.html), the math expressions are whitespace-agnostic, so use of newlines and tabulars are for human readability reasons only.

## Types and units

Every expresion has a type, which is either real, integer or boolean. Internally these are represented with 64 bit double-precision floating point numbers or integers respectively. Every state variable is stored as a real, but integers and booleans can be used as a part of the computation.

Mobius2 has automatic up-casting of integers and booleans to reals if they are an argument to an expression that requires a real. There is also down-casting to boolean in some cases.

Every expresson also has a [unit](units.html). Units can be transformed by the math expressions, for instance if you multiply two expressions, the resulting value has the unit that is the product of the units of the two factors.

## Expression components

### Block

A \<block\> is on the format

```python
{
	<local-var-declaration>,
	..
	<local-var-declaration>,
	<if-expression or primary-expression>
}
```

The block can have 0 or more local var declarations. It always ends with a final expression, and the value of the entire block is the value of this final expression.

Local variables are visible in the same scope and nested scopes, but not in outer scopes.

Every math expression in Mobius2 has one outer block which is the body of some declaration.

### Local var declaration

A \<local-var-declaration\> is of the format

```python
identifier := <primary-expression>,
```

The value of the right hand side primary expression is bound to the left hand side identifier below in the same block or any sub-scopes. You will normally not reassign the value of an already declared variable, but a local variable in a nested scope will shadow the one in an outer scope if it has the same identifier.

### Primary expression

A \<primary-expression\> is one of the following
- \<literal\>
- \<identifier\>
- \<block\>
- \<binary-operator\>
- \<unary-operator\>
- \<function-evaluation\>
- \<unit-conversion\>
- (\<primary-expression\>)

### If expression

An \<if-expression\> is something that evaluates to different values depending on some conditions. It is on the format
```python
<primary-expression> if <primary-expression>,
..
<primary-expression> if <primary-expression>,
<primary-expression> otherwise
```
There must be at least one line containing an `if` and one containing an `otherwise`.

The left \<primary-expression\> is a value, and the right one is a condition. The first value where the condition holds is the value of the entire \<if-expression\>. If no condition holds, the value is the one with the `otherwise`.

The units of all the values must be the same, and this is the unit of the \<if-expression\> itself.

### Literal

A \<literal\> is a number or value placed directly in the code. Examples

```python
5
0.32
false
2.8e12
```
These are formatted like in most other programming languages.

Literals are dimensionless, but can be given a unit by following them directly with a [unit declaration](units.html):

```python
2000[m 2]    # 2000 square meters
```

### Identifier

An \<identifier\> is either the identifier of a previously declared local variable, the identifier of an entity declared in the outer declaration scope (such as a parameter), a `.`-separated chain of identifiers forming a [location](central_concepts.html#components-and-locations), or a special value.

These have the units and types they are declared with. If a value is indexed over index sets, it will primarily be accessed using the same indexes as the current expression are evaluated with. (This causes expressions to propagate index set dependencies to one another and to put some restrictions on what can be accessed. This will be separately documented).

Note that the value you get when you access a parameter value in the model is the one provided in the data set (corresponding to the current index combination). The default value in the parameter declaration is just a helper for somebody who create a new data set.

When you access the value of a state variable or input series, you get the current value of that variable (this can some times force what order state variables have to be computed in. This will also be separately documented).

You can some times also access a value across a connection using a square bracket `[ .. ]`. This will be documented later.

Mobius2 also allows you to access some values that say something about the *model time* (not real time!) of the current evaluation of the expression

| Symbol | Unit | Comment |
| ------ | ---- | ----------- |
| `time.year` | `[year]` |  |
| `time.month` | `[month]` | Month of year. January=1 |
| `time.day_of_year` | `[day]` | Starts at 1 |
| `time.day_of_month` | `[day]` | Starts at 1 |
| `time.days_this_year` | [day, year-1] | 365 or 366 |
| `time.days_this_month` | [day, month-1] | |
| `time.step` | \* | The time step of the model. |
| `time.step_length_in_seconds` | `[s]` | |
| `time.fractional_step` | \* | If we are in an ODE solver, this is how far along the current time step we are. Always between 0 and 1. |

\* These have a unit equal to the time step unit of the current model application.

### Binary operator

A \<binary-operator\> is of the form

```python
<primary-expression><operator><primary-expression>
```

The precedence of an operator can determine association of the participating expressions if there are multiple operators. For instance, `a + b * c` is equivalent to `a + (b * c)` since `*` has higher precedence than `+`. You can use parentheses `( .. )` to force association, e.g. `(a + b)*c`.

| Symbol | Description | Precedence | Units |
| ------ | ----------- | ---------- | ----- |
| `|`    | logical or  | 1000       | \* (boolean) |
| `&`    | logical and | 2000       | \* (boolean) |
| `<`    | less than   | 3000       | \*\* (comparison) |
| `>`    | greater than   | 3000       | \*\* (comparison) |
| `<=`    | less than or equal  | 3000       | \*\* (comparison) |
| `>=`    | greater than or equal  | 3000       | \*\* (comparison) |
| `=`    | equal   | 3000       | \*\* (comparison) |
| `!=`    | not equal   | 3000       | \*\* (comparison) |
| `+`    | plus | 4000 | \*\*\* (aritmetic) |
| `-`    | minus | 4000 | \*\*\* (arithmetic) |
| `*`    | product | 5000 | \*\*\* (arithmetic) |
| `/`    | real division | 6000 | \*\*\* (arithmetic) |
| `//`    | integer division | 6000 | \*\*\* (arithmetic) |
| `%`    |  integer remainder | 6000 | \*\*\* (arithmetic) |
| `^`    |  exponentiation | 7000 | \*\*\*\* (exponentiation) |

\* The units of the arguments to a (boolean) operator must be dimensioness (the arguments are cast to boolean type). It produces a dimensionless boolean result.

\*\* The units of the arguments to a (comparison) operator must be of the same unit. It produces a dimensionless boolean result.

\*\*\* For the `+` and `-` operators the types of the two arguments must be the same, and the result has that unit. For `*`, `/` and `//`, unit arithmetic is applied. That is, the units of the arguments are themselves multiplied or divided with one another to produce the result unit.

\*\*\*\* With exponentiation, the unit of the left hand side is raised to the power of the right hand side if it is possible. It is possible if the left hand side is dimensionless or if it can be determined that the right hand side is a constant integer or rational value.

Usually if you get an error with units it means you forgot a unit conversion somewhere, but it can some times be problematic. For instance, if you are dealing with empirical formulas that come e.g. from regression fits, the unit of the expression may not make sense in terms of the units of the arguments. In this case we recommend that you force convert the units of the arguments to dimensionless, do the computation, then force convert the result back to the unit you need it to be in. Example:

```python
mean_barometric_pressure : function(elevation : [m]) {
	elev := elevation => [],                            # Force the unit of the argument to dimensionless
	(101.3 - (0.01152 - 0.544e-6*elev)*elev) => [k Pa]  # Force the unit of the result to kilo-Pascals.
}
```

### Unary operator

To be written

### Function evaluation

To be written

### Unit conversion

To be written