/*
 * This file is part of the YicesTRA Solver.
 * Copyright (C) 2026 IMDEA Software Institute.
 *
 * YicesTRA is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * YicesTRA is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with YicesTRA. If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * tra_arb_domain.h
 *
 * The ARB domain (see abstractions/tra_arb_domain.c). An element denotes either a rational
 * point {q}, or a ball [m - r, m + r] of the ARB library (R when r = +oo). It meets the (A)
 * clauses of tra_abstract_values.h, and its soundness clauses for the numbers that exact
 * elements abstract (see below). Its div also accepts b with 0 ∈ γ(b), as a component of a
 * product must (see tra_product_domain.h), and then gives γ(c) = R.
 *
 * Exact elements. An element may be exact: it then knows the number that it abstracts, as
 *
 *   rational + sqrt_coeff * sqrt(radicand) + pi_coeff * pi + exp_coeff * exp(exp_arg),
 *
 * with rational coefficients and exp_arg, and an integer radicand (arb_symbolic_t). Rationals,
 * const_pi, exp of a rational, and set_algebraic_lp (degree at most 2) give exact elements, and
 * the arithmetic combines them exactly. An exact element whose irrational coefficients are 0 is
 * the point {q}, e.g. pi - pi, 10 exp(1) - exp(1) 10, sin(pi/2), or an algebraic value whose
 * polynomial libpoly did not factor. Any other element is its ball, and the queries read the ball.
 *
 * Soundness of exact elements. The soundness clauses hold for the number that an exact element
 * abstracts, not for every point of the balls (pi - pi = {0} does not contain the difference of
 * two balls of pi). This suffices, as exact elements are created only for one number at a time
 * (a constant, or a trail value). The TRA plugin also evaluates terms at the trail values of pi
 * and of the applications of exp: the arithmetic uses only identities that hold for any value of
 * pi and of each exp(exp_arg), hence at these trail values too (provided that the applications
 * of exp at equal arguments have equal trail values, which the UF plugin keeps). The exception
 * is sin and cos at k pi/2, exact for the true pi only: when the trail value of such an
 * application differs, the application escapes its abstraction, and the sine plugin explains
 * the conflict (tra_sin_pi_taylor_conflict).
 */

#ifndef TRA_ARB_DOMAIN_H_
#define TRA_ARB_DOMAIN_H_

#include "mcsat/tra/tra_abstract_values.h"

/* The ARB domain: pass &tra_arb_ilib wherever a tra_ilib_t* is expected. */
extern tra_ilib_t tra_arb_ilib;

#endif /* TRA_ARB_DOMAIN_H_ */
