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
 * tra_conflict_strategies.h
 *
 * The strategies with which the TRA plugin explains the conflicts found by the consistency
 * check. The notation is that of tra_conflict_utils.h. In the contracts below, conflict_terms
 * are the terms of a failed consistency check (tra->cache_conflicts), all of them analyzed,
 * and every strategy meets:
 *   Requires: conflict is empty.
 *   Ensures:  result <==> conflict is not empty;
 *             result ==> the literals of conflict are true in the trail and their
 *                        conjunction is unsatisfiable.
 */

#ifndef MCSAT_TRA_CONFLICT_STRATEGIES_H_
#define MCSAT_TRA_CONFLICT_STRATEGIES_H_

#include <stdbool.h>

#include "utils/int_vectors.h"

#include "mcsat/tra/tra_plugin_internal.h"

/**
 * Ensures:  result ==> conflict = [L], where L is the literal that tra_conflict_escape_literal
 *                      returns for t and the range of t (attribute_range), for some arithmetic
 *                      term t of conflict_terms;
 *           (P):  result if tra_conflict_escape_literal returns a literal for some such t.
 */
bool tra_get_range_conflict(tra_plugin_t* tra, ivector_t* conflict_terms, ivector_t* conflict);

/**
 * Ensures:  result ==> conflict = [L], where L is the literal that tra_conflict_escape_literal
 *                      returns for t and I(t), for some arithmetic term t of conflict_terms that
 *                      is constant and total-continuous (attribute_constant and
 *                      attribute_total_continuous);
 *           (P):  result if tra_conflict_escape_literal returns a literal for some such t.
 */
bool tra_get_constant_term_conflict(tra_plugin_t* tra, ivector_t* conflict_terms, ivector_t* conflict);

/**
 * Modifies: tra->term_wait_count and tra->last_conflict_plugin, which make the choices of t
 *           and of the function plugin fair across calls.
 * Ensures:  result ==> conflict is the conflict that a strategy of fp->profile, or
 *                      fp->default_conflict, returns for some function plugin fp and some
 *                      term t of conflict_terms;
 *           (P):  result if some term of conflict_terms and some function plugin meet the
 *                 hypothesis of the (P) clause of default_conflict.
 */
bool tra_get_function_plugin_conflict(tra_plugin_t* tra, ivector_t* conflict_terms, ivector_t* conflict);

/**
 * Let A range over the Boolean terms of conflict_terms, J = I(tra_atom_argument(A)), and
 * τ = tra_get_atom_truth(J, eq), where eq <==> A is an equality.
 * Let ℓ(x) be tra_conflict_value_literal(x) if x is not Boolean, and otherwise the literal x
 * or ¬x that is true in the trail.
 * A is refuted if J is defined and (τ, ℓ(A)) ∈ {(-1, A), (1, ¬A)}.
 * The leaves of A are the terms of V(A) that are problem variables (tra_plugin_internal.h),
 * real ites, divisions, or applications whose plugin fp has ¬fp->is_total_continuous.
 * A definition conflict is one of these sets, when its literals are true in the trail:
 *   {c, t ≠ a}, {¬c, t ≠ b} or {t ≠ a, t ≠ b}, for a real t = ite(c, a, b) ∈ V(A);
 *   {n ≠ 0, m ≠ n·t}, for t = m/n ∈ V(A).
 * Requires: A is a positive atom; every term of V(A) has a trail value and is not an abs.
 * Ensures:  result ==> conflict = C ∖ {true_term}, where C is
 *             - a definition conflict, if there is one;
 *             - otherwise, {ℓ(x) : x is a leaf of A} ∪ {ℓ(A)}, for a refuted A;
 *           (P):  result if some A is refuted.
 */
bool tra_get_fallback_conflict(tra_plugin_t* tra, ivector_t* conflict_terms, ivector_t* conflict);

#endif /* MCSAT_TRA_CONFLICT_STRATEGIES_H_ */
