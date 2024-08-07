---
layout: default
title: Mathematical description
parent: Existing models
nav_order: 4
has_children: true
---

# Mathematical description

Here is documentation for some of the existing modules we provide with Mobius2.

The mathematical description is auto-generated based on the model files.

It is useful to look at the [central model concepts](../../mobius2docs/central_concepts.html) to understand what we mean by certain concepts in the descriptions ("parameter", "state variable", "flux", etc.).

## Notation

Since the mathematical description is auto-generated from the model code, it follows some conventions that are more code-like than math-like:

### Blocks

If several equations are stacked on top of one another, the upper ones declare sub-expressions that are used below, while the bottom line is the value of the whole expression. For instance, in

$$
a = 15 \\
a\cdot 100 + a^2
$$

It is $$a\cdot 100$$ that is the value of the expression, while $$a = 15$$ declares a value used in that expression. This can sometimes be combined with an outer expression:

$$
\begin{pmatrix}
a = 15 \\
a\cdot 100 + a^2
\end{pmatrix} \cdot 50
$$

In this case, the value of the outer expression is $$(a\cdot 100 + a^2)\cdot 50$$.

### Unit conversions

If you see something of the form  $$(\mathrm{expression} \rightarrow \mathrm{unit})$$ means that the expression on the left is converted to the unit on the right, typically using a multiplier. For instance

$$
(Q \rightarrow \mathrm{m}^3\,\mathrm{s}^{-1})
$$

means that $$Q$$ is converted to the unit $$\mathrm{m}^3\,\mathrm{s}^{-1}$$. So if $$Q$$ initially has unit $$\mathrm{l}\,\mathrm{day}^{-1}$$, this resolves to

$$
Q\cdot \frac{1}{1000\cdot 86400}
$$

If you see a double right arrow $$(\mathrm{expression} \Rightarrow \mathrm{unit})$$ it means that the unit of the expression is discarded and replaced with the new unit, while the numerical value of the expression is kept. If you see $$(\mathrm{expression} \Rightarrow 1)$$, it means that the expression is put on dimensionless form (i.e. with unit 1).

### If-expressions

In if-expressions, a condition implicitly includes the exclusion of the conditions above it. For instance in the example

$$
a = \begin{cases}
1 & \text{if}\; b > 5 \\
2 & \text{if}\; b > 3 \\
3 & \text{otherwise}
\end{cases}
$$

the condition $$b > 3$$ in line 2 implies the exclusion of the above condition $$b > 5$$, so that the implied condition in line 2 is $$3 < b \leq 5$$.


{% include lib/mathjax.html %}