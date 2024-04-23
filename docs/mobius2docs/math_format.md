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

### If expression

An if-expression is something that evaluates to different values depending on some conditions. It is on the format
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


### Identifier

### Binary operator\

### Unary operator\

### Function evaluation\

### Unit conversion