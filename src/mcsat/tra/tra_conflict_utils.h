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
 * Utilities to construct simple conflicts. Shared by the conflict strategies of
 * the TRA plugin and by the function plugins.
 *
 * Notation for the contracts below, in addition to that of tra_func_plugin.h.
 *  - v(t) is the value of t in the trail, as computed by term_evaluate (tra_exact_values.h).
 *    It is undefined when term_evaluate fails.
 *  - The rationals in the literals below, except those of tra_conflict_value_literal, are read
 *    from mpfr values of at most 2^22 bits.
 *    The (P) clauses assume that this precision suffices, and that the abstract domain
 *    meets its (A) clauses.
 */

#ifndef MCSAT_TRA_CONFLICT_UTILS_H_
#define MCSAT_TRA_CONFLICT_UTILS_H_

#include <stdbool.h>

#include <gmp.h>
#include <mpfr.h>

#include "yices_types.h"
#include "mcsat/mcsat_types.h"
#include "utils/int_vectors.h"

#include "mcsat/tra/tra_abstract_values.h"
#include "mcsat/tra/tra_func_plugin.h"

/* The largest precision of the mpfr values from which the rationals of the literals are read. */
#define TRA_CONFLICT_PREC_LIMIT (1L << 22)

/**
 * Ensures:  result <==> x is finite and mpfr_get_q(q, x) is safe and within budget: the
 *           exponent of x is at most 2^22 in absolute value (see tra_conflict_utils.c).
 */
bool tra_mpfr_to_q_is_safe(const mpfr_t x);

/**
 * Requires: x_val = v(x).
 * Ensures:  lo < x_val < hi ==> the literals lo <= x and x <= hi, except those that are
 *                               trivially true, are appended to conflict;
 *           otherwise, conflict is unchanged.
 */
void tra_conflict_push_bounds(term_t x, const mcsat_value_t* x_val, const mpq_t lo, const mpq_t hi,
                              ivector_t* conflict);

/**
 * Let L be the literal t > bound if above, and t < bound otherwise.
 * Ensures:  result = L if t and bound have values on the trail, L is true there, and L is neither
 *           true_term nor false_term; result = NULL_TERM otherwise (e.g. if bound = NULL_TERM).
 */
term_t tra_conflict_cutting_literal(const plugin_context_t* ctx, term_t t, term_t bound, bool above);

/**
 * Requires: t_val is real-valued; lo and hi are finite.
 * Ensures:  result <==> t_val > hi;
 *           result ==> H = hi;
 *           !result ==> H = lo.
 */
bool tra_conflict_escape_endpoint(const mcsat_value_t* t_val, const mpfr_t lo, const mpfr_t hi, mpq_t H);

/**
 * Modifies: *below, when below != NULL.
 * Ensures:  result = NULL_TERM, or result is a literal that is true in the trail and
 *           implies t ∉ γ(X), with c rational:
 *             result is t < c or t <= c, if v(t) < γ(X),
 *             result is t > c or t >= c, if v(t) > γ(X);
 *           result != NULL_TERM and below != NULL ==> *below = (v(t) < γ(X));
 *           (P):  result != NULL_TERM if v(t) is defined and lib->trail_value_position(X, v(t))
 *                 is TV_BELOW or TV_ABOVE.
 */
term_t tra_conflict_escape_literal(const plugin_context_t* ctx, const tra_ilib_t* lib, term_t t,
                                   tra_itype_t X, bool* below);

/**
 * Requires: c is a Boolean term.
 * Ensures:  c has a trail value ==> result ∈ {c, ¬c} and result is true in the trail;
 *           c has no trail value ==> result = NULL_TERM.
 */
term_t tra_conflict_boolean_literal(const plugin_context_t* ctx, term_t c);

/**
 * Requires: x is an arithmetic term other than a constant, a sum or a product; v(x) is defined.
 * Ensures:  result is true in the trail, and result <==> x = v(x);
 *           result is x = c with c ∈ Q, or a root atom x = root_k(f) with f ∈ Z[x].
 */
term_t tra_conflict_value_literal(const plugin_context_t* ctx, term_t x);

/**
 * Requires: t is positive and not of function type; conflict is empty.
 * Let lib = fp->abstract_domain and, when fp owns t, x_1, ..., x_n be the arguments of t, I_i be
 * tra_get_argument_abstraction(fp->tra, t, i-1), and L be the literal that
 * tra_conflict_escape_literal returns for t and I(t).
 * Ensures:  result <==> conflict is not empty;
 *           result ==> fp owns t, conflict meets the contract of default_conflict, and
 *                      conflict = [l_1 <= x_1, x_1 <= u_1, ..., l_n <= x_n, x_n <= u_n, L]
 *                      with v(x_i) ∈ [l_i, u_i] ⊆ γ(I_i);
 *           (P):  result if the (P) clause of default_conflict requires a conflict for fp and t,
 *                 and lib->trail_value_position returns TV_UNKNOWN neither on (I(t), v(t))
 *                 nor on any (I_i, v(x_i)).
 */
bool tra_func_default_get_conflict(const tra_func_plugin_t* fp, term_t t, ivector_t* conflict);

/**
 * Requires: t is positive and not of function type; conflict is empty; dom(f) = R^n, and f
 *           is non-decreasing in every argument.
 * Ensures:  as tra_func_default_get_conflict, except that result ==>
 *             conflict = [l_1 <= x_1, ..., l_n <= x_n, L] with l_i ∈ γ(I_i) and l_i <= v(x_i),
 *                        if v(t) < γ(I(t)),
 *             conflict = [x_1 <= u_1, ..., x_n <= u_n, L] with u_i ∈ γ(I_i) and v(x_i) <= u_i,
 *                        if v(t) > γ(I(t)).
 */
bool tra_func_increasing_default_get_conflict(const tra_func_plugin_t* fp, term_t t, ivector_t* conflict);

#endif /* MCSAT_TRA_CONFLICT_UTILS_H_ */
