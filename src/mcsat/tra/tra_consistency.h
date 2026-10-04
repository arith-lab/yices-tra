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

#ifndef MCSAT_TRA_CONSISTENCY_H_
#define MCSAT_TRA_CONSISTENCY_H_

#include "yices_types.h"

#include "mcsat/tra/tra_plugin_internal.h"

/* Verdict of a consistency check: how a trail value agrees with the abstraction. */
typedef enum {
  TRA_CC_CONSISTENT,    // exact agreement
  TRA_CC_INCONSISTENT,  // disagreement
  TRA_CC_DELTA,         // agreement only up to 2^-delta
  TRA_CC_UNKNOWN        // no verdict
} tra_cc_verdict_t;

/**
 * Requires: t is an analyzed ARITH_EQ_ATOM, ARITH_GE_ATOM or ARITH_BINEQ_ATOM, or its negation.
 * Ensures:  result is the argument s for which t tests the sign (s = t1 - t2 for t1 = t2).
 */
term_t tra_atom_argument(tra_plugin_t* tra, term_t t);

/**
 * Requires: t is an analyzed atom (ARITH_EQ_ATOM, ARITH_GE_ATOM or ARITH_BINEQ_ATOM) with a trail
 *           value.
 * Modifies: tra->cache_abstractions and tra->cache_conflicts.
 * Ensures:  if t is transcendental and every term of V(t) (see tra_get_term_variables) has
 *           a trail value, the trail value of t is checked against the abstraction of t:
 *             - if they agree only up to 2^-delta, delta mode is triggered at the current
 *               decision level;
 *             - if they disagree, or the check is undecided, the terms from which a conflict
 *               can be built are added to tra->cache_conflicts; if there is none and the
 *               check is undecided, a failure is reported.
 *           Otherwise, nothing happens.
 */
void tra_consistency_check(tra_plugin_t* tra, term_t t);

/**
 * Requires: t is an analyzed transcendental constant (attribute_transcendental, attribute_constant
 *           and attribute_total_continuous are 1), such as pi or (sin 3), and every term of V(t)
 *           has a trail value (as for tra_consistency_check: E(s) records the position of v(s)).
 * Let c be the value of t, and v(t) its trail value.
 * Modifies: E(s) for t and its subterms s.
 * Ensures:  result = TRA_CC_INCONSISTENT ==> v(t) != c, and tra_conflict_escape_literal returns
 *                                          a literal for t and I(t);
 *           result = TRA_CC_CONSISTENT   ==> v(t) = c;
 *           result = TRA_CC_DELTA        ==> |v(t) - c| <= 2^-delta.
 */
tra_cc_verdict_t tra_check_trail_value_transcendental_constant(tra_plugin_t* tra, term_t t);

#endif /* MCSAT_TRA_CONSISTENCY_H_ */
