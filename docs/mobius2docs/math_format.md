---
layout: default
title: Math format
parent: Common declaration format
grand_parent: Model language
nav_order: 2
comment: "While we use python markup for code snippets, they are not actually python, it just creates convenient coloring for this format."
---

# The Mobius2 math format

Math expressions in Mobius2 are for the most part written using a purely "functional style". This means that almost every expression evaluates to a value, and there are no side effects. (There are exceptions to this, but they are only used in rare occations).

Like the entire [Mobius2 declaration format](declaration_format.html), the math expressions are whitespace-agnostic, so use of newlines and tabulars are for human readability reasons only.

## Types and units

Every expresion has a type, which is either real, integer or boolean. Internally these are represented with 64 bit double-precision floating point numbers or integers respectively. Every state variable is stored as a real, but integers and booleans can be used as a part of the computation.

Mobius2 has automatic up-casting of integers and booleans to reals if they are an argument to an expression that requires a real. There is also down-casting to boolean in some cases.

Every expresson also has a [unit](units.html). Units can be transformed by the math expressions, for instance if you multiply two expressions, the resulting value has the unit that is the product of the units of the two factors.

See the [note on unit errors](math_format.html#note-on-unit-errors) for some tips about how to deal with them.

## The context location

If the math expression is the body of a `var` or `flux` declaration, it has a context [*location*](central_concepts.html#components-and-locations).

For a `var` declaration, the context location is the single location of that state variable.

For a `flux` declaration, the context location is the source of the flux if it is not `out`, otherwise it is the target of the flux.

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
<identifier> := <primary-expression>,
```

The value of the right hand side is bound to the left hand side identifier, which can be referenced in any expression below it in the same block (including in nested blocks). You will normally not reassign the value of an already declared variable, but a local variable in a nested scope will shadow one in an outer scope if it has the same identifier.

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

The left \<primary-expression\> is a value, and the right one is a condition. The first value where the condition is `true` is the value of the entire \<if-expression\>. If no condition holds, the value is the one with the `otherwise`.

The units of all the values must be the same, and this is the unit of the \<if-expression\> itself.

The conditions must be dimensionless, and are cast to boolean if they are not already boolean.

### Literal

A \<literal\> is a number or value placed directly in the code. It is one of the [real, integer or boolean token types](declaration_format.html#token-types). Examples

```python
5
0.32
false
2.8e12
```

Literals are dimensionless by default, but can be given a unit by following them directly with a [unit declaration](units.html#the-unit-declaration-format):

```python
2000[m 2]    # 2000 square meters
```

### Identifier

An \<identifier\> is either the identifier of a local variable, the identifier of an entity declared in the outer declaration scope (such as a parameter or constant), a `.`-separated chain of identifiers forming a [location](central_concepts.html#components-and-locations), or a special value.

If you are in an expression with a *context location*, you can some times use shorthands for the location of a refereced state variable. For instance, if the context location is `river.water.oc`, and you try to access `temp`, if `temp` does not refer to a single value, Mobius2 will first look for `river.water.oc.temp`, then `river.water.temp` if the prior does not exist, and finally `river.temp`.

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
| `time.days_this_year` | `[day, year-1]` | 365 or 366 |
| `time.days_this_month` | `[day, month-1]` | |
| `time.step` | \* | The time step of the model. |
| `time.step_length_in_seconds` | `[s]` | |
| `time.fractional_step` | \* | If we are in an ODE solver, this is how far along the current time step we are. Always between 0 and 1. |

\* These have a unit equal to the time step unit of the current model application.

Other special identifiers:

| Symbol | Comment |
| ------ | ------- |
| `no_override` | This can be used to cancel an @override or @override_conc expression of a `var` declaration. The compiler must be able to resolve to either `no_override` or not `no_override` at compile time, meaning any `if` branches to these can only rely on constants or constant parameters. (to be documented) |
| `is_at` | This can be used to determine location in a grid1d connection. Will be documented later. |

Enum parameters: Enum parameters are accessed using `par_identifier.value_identifier`, where `value_identifier` is one of the declared possible values of this enum parameter. The expression evaluates to `true` if the parameter has the given value, and `false` otherwise.

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
| `+`    | plus | 4000 | \*\*\* (arithmetic) |
| `-`    | minus | 4000 | \*\*\* (arithmetic) |
| `*`    | product | 5000 | \*\*\* (arithmetic) |
| `/`    | real division | 6000 | \*\*\* (arithmetic) |
| `//`    | integer division | 6000 | \*\*\* (arithmetic) |
| `%`    |  integer remainder | 6000 | \*\*\* (arithmetic) |
| `^`    |  exponentiation | 7000 | \*\*\*\* (exponentiation) |

\* The units of the arguments to a (boolean) operator must be dimensioness (the arguments are cast to boolean type). It produces a dimensionless boolean result.

\*\* The units of the arguments to a (comparison) operator must be of the same unit. It produces a dimensionless boolean result.

\*\*\* For the `+` and `-` operators the units of the two arguments must be the same, and the result has that unit. For `*`, `/` and `//`, unit arithmetic is applied. That is, the units of the arguments are themselves multiplied or divided with one another to produce the result unit.

\*\*\*\* With exponentiation, the unit of the left hand side is raised to the power of the right hand side if it is possible. It is possible if the left hand side is dimensionless or if it can be determined at compile time that the right hand side is a constant integer or rational number.

### Unary operator

A \<unary-operator\> is of the form

```python
<operator><primary-expression>
```

There are only two unary operators

| Symbol | Description |
| ------ | ----------- |
| `-`    | minus |
| `!`    | not |

Minus preserves the unit of the argument. The not operator always produces a boolean value and must have a dimensionless argument. The argument is cast to boolean.

### Function evaluation

A \<function-evaluation\> is either a regular function evaluation or a special directive.

#### Regular function evaluation

These are of the form

```python
<function-identifier>(<primary-expression>, .., <primary-expression>)
```

The function has 0 or more arguments.

The function identifier identifies either a function declaration that is visible in the parent declaration scope, or an intrinsic function.

If it is a declared function, it can have requirements about the units of the arguments, and the result will have the unit of the expression of the body of the function declaration. 

Declared functions are inlined at the site they are evaluated. (This means that you can't have recursive declared functions for now, this may be implemented later).

The following intrinsic functions are visible in every function scope. They are implemented either using [LLVM intrinsics](https://llvm.org/docs/LangRef.html#intrinsic-functions) or [LLVM libc](https://libc.llvm.org/math/index.html).

| Signature  | Description | Units |
| ---------- | ----------- | ----- |
| `min(a, b)` | minimum value | a and b must have the same unit. Result has that same unit |
| `max(a, b)` | maximum value | Same as min |
| `copysign(a, b)` | magnitude of a with sign of b | Result has the unit of a |
| `sqrt(a)`  | square root | Result unit is the square root of the unit of a if possible |
| `cbrt(a)`  | cube root | Result unit is the cube root of the unit of a if possible |
| `abs(a)` | absolute value | Preserves unit |
| `floor(a)` | round down to closest integer | Preserves unit |
| `ceil(a)` | round up to closest integer | Preserves unit |
| `is_finite(a)` | `true` if a is finite and not `nan`, `false` otherwise | a has any unit, result is dimensionless |
| `exp(a)` | Euler number to the power of a | a must be dimensionless, result is dimensionless |
| `pow2(a)` | 2 to the power of a | a must be dimensionless, result is dimensionless  |
| `ln(a)` | natural logarithm | a must be dimensionless, result is dimensionless  |
| `log10(a)` | base-10 logarithm | a must be dimensionless, result is dimensionless  |
| `ln2(a)` | base-2 logarithm | a must be dimensionless, result is dimensionless  |
| `cos(a)` | cosine | a must be dimensionless, result is dimensionless  |
| `sin(a)` | sine | a must be dimensionless, result is dimensionless  |
| `tan(a)` | tangent | a must be dimensionless, result is dimensionless |
| `acos(a)` | inverse cosine | a must be dimensionless, result is dimensionless |
| `asin(a)` | inverse sine | a must be dimensionless, result is dimensionless |
| `atan(a)` | inverse tangent | a must be dimensionless, result is dimensionless |
| `cosh(a)` | hyperbolic cosine | a must be dimensionless, result is dimensionless |
| `sinh(a)` | hyperbolic sine | a must be dimensionless, result is dimensionless |
| `tanh(a)` | hyperbolic tangent | a must be dimensionless, result is dimensionless |

More intrinsics could be added if they are needed.

#### Special directives

Special directives allow you to reference a separate value related to a state variable `var`.

| Signature | Description | Unit |
| --------- | ----------- | ---- |
| `last(var)` | The previous time step value of the state variable `var` | Same as `var` |
| `in_flux(var)` | Sum of all fluxes that have `var` as a target excluding fluxes along connections | The unit of `var` divided by the model time step unit |
| `in_flux(con, var)` | Sum of all fluxes that have `var` as a target along the connection `con` | As above |
| `out_flux(var)` | Sum of all fluxes that have `var` as a source excluding fluxes along connections | As above |
| `out_flux(con, var)` | Sum of all fluxes that have `var` as a source along the connection `con` | As above |
| `conc(var)` | The concentration of `var`. Only available if `var` is *dissolved*. | Either the declared concentration unit of `var`, or (if none was declared) the unit of `var` divided by the unit of the quantity it is dissolved in. |
| `aggregate(var)` | This refers to a (possibly weighted) sum of this variable over index sets that the context location does not index over. |

If `var` is on a solver, `last(var)` will still reference the end-of-timestep value from the last model sampling step (not solver step).

You can use `aggregate(var)` if `var` indexes over a higher number of index sets than the context location of the code you are in. In that case, it will sum the `var` over the excess index sets, applying an `aggregation_weight` if one exists between the compartment of `var` and the compartment of the current context location.

### Unit conversion

There are four unit conversion operators with the following syntax

```python
<primary-expression> -> <unit-declaration>
<primary-expression> ->>
<primary-expression> => <unit-declaration>
<primary-expression> =>>
```
where a \<unit-declaration\> follows the [unit declaration format](units.html#the-unit-declaration-format).

| Operator | Description |
| -------- | ----------- |
| `->`     | Convert the lhs to the rhs unit by multiplying with a conversion factor if one exists (otherwise there is an error). The unit conversion factor exists if the units have the same SI dimensions when reduced to [standard form](units.html#the-standard-form). |
| `->>`    | Same as `->` but converts the lhs to the unit of the state variable declaration that the outer function body is attached to (if it is attached to one, otherwise this raises an error). |
| `=>`     | Discards the unit of the lhs and replaces it with the rhs unit, keeping the same underlying numerical value. |
| `=>>`    | Same as `=>`, but replaces the unit with the unit of the state variable of the outer function body similarly to `->>`. |

To do the unit conversion, Mobius2 will durinc compilation generate a multiplication with a conversion factor (if it exists, otherwise it reports an error). For instance,

```python
10[day] -> [s]
# translates to
10[day]*86400[s, day-1] # = 864000[s]
```

There is one exception, where converting between `[deg_c]` and `[K]` instead causes an addition or subtraction

```python
25[deg_c] -> [K]
# translates to
(25 + 273.15) => [K] # = 298.15[K]
```

If a unit conversion appears in conjunction with binary operators, the unit conversion acts as if it has precedence 4500. For instance

```python
a + b*c -> unit
# is equivalent to
a + ((b*c) -> unit)
```

## Note on unit errors

Usually if you get an error with units it means you forgot a unit conversion somewhere, but these errors can some times be problematic.

For instance, if you are dealing with empirical formulas that come e.g. from regression fits, the unit of the expression may not make sense in terms of the units of the arguments. In this case we recommend that you force convert the units of the arguments to dimensionless, do the computation, then force convert the result back to the unit you need it to be in. Example:

```python
mean_barometric_pressure : function(elevation : [m]) {
	elev := elevation => [],                            # Force the unit of the argument to dimensionless
	(101.3 - (0.01152 - 0.544e-6*elev)*elev) => [k Pa]  # Force the unit of the result to kilo-Pascals.
}
```

## Imperative constructs

If you need to, there are some imperative programming constructs you can use. We recommend that you only use this if you need to compute an iterative solution to something.

A \<block\> can be given an optional *iteration tag* by writing

```python
<identifier> : <block>
```

If you within that or a nested block reach an `iterate <identifier>` statement, the function evaluation skips back to the start of the block with that tag.

You can also update the value of a local variable using the syntax

```python
<identifier> <- <primary-expression>
```

where the identifier is that of the local variable. We recommend that you use this only when you need to because code is easier to read and the compiler can make better optimizations when variables are immutable.

For instance, the below expression uses [Newton's method](https://en.wikipedia.org/wiki/Newton's_method) for approximating the solution to `sin(x)+e^x = 0`

```python
solve_equation : function(eps, x0) {
	x   := x0,
	i:{
		xi := x - (sin(x)+exp(x))/(cos(x)+exp(x)),
		
		xi                    if abs(x - xi) < eps,
		{ x <- xi, iterate i} otherwise
	}
}
```