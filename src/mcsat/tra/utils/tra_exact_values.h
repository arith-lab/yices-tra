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

#ifndef MCSAT_TRA_UTIL_EXACT_VALUES_H_
#define MCSAT_TRA_UTIL_EXACT_VALUES_H_

#include <stdbool.h>

#include <gmp.h>
#include <mpfr.h>

#include "yices_types.h"
#include "mcsat/mcsat_types.h"

/*
 * Exact computations on trail values. In the contracts below, a real-valued mcsat value
 * (VALUE_RATIONAL or VALUE_LIBPOLY) stands for the real number it denotes, and 
 * sign(r) is -1, 0 or 1 depending on whether r < 0, r = 0 or r > 0.
 */

/**
 * Requires: v is real-valued.
 * Modifies: the isolating interval of an algebraic v may be refined (see lp_value_sgn).
 * Ensures:  result = sign(v).
 */
int tra_value_sign(const mcsat_value_t* v);

/**
 * Requires: v is real-valued.
 * Ensures:  result = sign(v - q).
 */
int tra_value_cmp_mpq(const mcsat_value_t* v, const mpq_t q);

/**
 * Requires: v is real-valued; f is not NaN; if f is finite, its binary exponent lies 
 *           in [mpfr_get_emin(), mpfr_get_emax()].
 * Ensures:  result = sign(v - f), where f = +oo gives -1 and f = -oo gives 1.
 * Cost:     linear in the binary exponent of f.
 */
int tra_value_cmp_mpfr(const mcsat_value_t* v, const mpfr_t f);

/**
 * Requires: v is real-valued.
 * Ensures:  result <==> |v - q| <= 2^-delta.
 */
bool tra_value_close_mpq(const mcsat_value_t* v, const mpq_t q, long delta);

/**
 * Requires: v is real-valued; if f is finite, its binary exponent lies in
 *           [mpfr_get_emin(), mpfr_get_emax()].
 * Ensures:  result <==> f is finite and |v - f| <= 2^-delta.
 * Cost:     linear in the binary exponent of f.
 */
bool tra_value_close_mpfr(const mcsat_value_t* v, const mpfr_t f, long delta);

/**
 * Requires: v is real-valued.
 * Ensures:  lo <= v <= hi;
 *           v rational ==> lo = v = hi;
 *           v irrational ==> (lo, hi) is the isolating interval of v.
 */
void tra_value_bounds_mpq(const mcsat_value_t* v, mpq_t lo, mpq_t hi);

/**
 * Requires: k >= 0. out may alias x.
 * Ensures:  out = floor(2^k x) / 2^k        if dir < 0,
 *           out = ceil(2^k x) / 2^k         if dir > 0,
 *           out = floor(2^k x + 1/2) / 2^k  if dir = 0.
 */
void tra_mpq_round_dyadic(mpq_t out, const mpq_t x, long k, int dir);

/**
 * Requires: v is real-valued; k >= 0.
 * Ensures:  2^k c is an integer, and |v - c| <= 2^-k.
 */
void tra_value_nearest_dyadic(const mcsat_value_t* v, long k, mpq_t c);

/**
 * Requires: v is real-valued; max_bits >= 0.
 * Let d = v - H if above, and d = H - v otherwise.
 * Ensures:  result = -1 or (result >= 0 and d > 2^-result);
 *           d <= 0 ==> result = -1;
 *           d > 2^-(max_bits+1) ==> result >= 0.
 */
long tra_value_separation(const mcsat_value_t* v, const mpq_t H, bool above, long max_bits);

/**
 * Requires: t is an arithmetic term; *value is unconstructed. The leaves of t are its
 *           subterms that are neither constants, sums nor products.
 * Ensures:  result <==> every leaf of t has a real-valued trail value;
 *           result ==> *value is constructed, of type VALUE_LIBPOLY, and equals t evaluated
 *                      at the trail values of its leaves (sums and products are evaluated
 *                      from their children, even when they have a trail value themselves);
 *           !result ==> *value is unconstructed.
 */
bool term_evaluate(const plugin_context_t* ctx, term_t t, mcsat_value_t* value);

/**
 * Requires: t1 and t2 are arithmetic terms.
 * Ensures:  result = sign(v1 - v2) if term_evaluate gives values v1, v2 to t1, t2;
 *           result = 0 otherwise.
 */
int tra_value_cmp_terms(const plugin_context_t* ctx, term_t t1, term_t t2);

#endif /* MCSAT_TRA_UTIL_EXACT_VALUES_H_ */
