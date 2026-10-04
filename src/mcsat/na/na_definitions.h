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

#ifndef NA_DEFINITIONS_H_
#define NA_DEFINITIONS_H_

#include "mcsat/na/na_plugin_internal.h"
#include "utils/int_hash_map.h"
#include "utils/int_vectors.h"

/*
 * Two independent notions; M is the assignment of NA.
 *
 * Definitions, for conflict explanations. A definition of y is a literal e = 0 such that:
 *  * it is true on the trail, and NA did not propagate it (for performance);
 *  * y is the top variable of e (lp_polynomial_top_variable), and e is assigned and zero in M;
 *  * e = c * y + e0 for a nonzero constant c.
 * The explanation substitutes y := -e0 / c into the core polynomials: a virtual substitution
 * restricted to such equations.
 *
 * Auxvar definitions, for decisions. An auxvar is a variable introduced by the solver: a division
 * or an unnamed uninterpreted term (such as a purification variable). Its auxvar definition is an
 * equation in which it occurs linearly, fixed when the equation is created. NA decides an auxvar
 * after the variables of its auxvar definition, which then force its value (for performance).
 */

/**
 * Requires: defs is empty; no trail pop is in progress (trail->to_repropagate is empty).
 * Modifies: defs.
 * Ensures:  the keys of defs are exactly the variables that have a definition; each one maps to
 *           its first definition on the trail.
 */
void na_collect_definitions(na_plugin_t* na, int_hmap_t* defs);

/**
 * Let p0 and U0 be p and defs_used on entry, and deg the total degree.
 * Requires: defs comes from na_collect_definitions; x is not a key of defs.
 * Modifies: p and defs_used.
 * Ensures:  p is p0 after the substitution of a set D of definitions of defs, where:
 *            * p = c * p0 for a nonzero constant c, wherever the equations of D hold;
 *            * defs_used is U0 followed by the definitions of D that are not in U0;
 *            * a variable of p is a key of defs only if its definition is nonlinear (for
 *              performance);
 *            * deg(p) <= 2 * deg(p0) + the sum of deg(e) - 1 over e in D (for performance).
 */
void na_substitute_definitions(na_plugin_t* na, const int_hmap_t* defs, lp_variable_t x, lp_polynomial_t* p, ivector_t* defs_used);

/**
 * Requires: constraint_var is in na->constraint_db.
 * Modifies: na->auxvar_definitions.
 * Ensures:  if the constraint of constraint_var is a Boolean equation e = 0 with at least two
 *           variables, it becomes the auxvar definition of the newest auxvar (largest term index)
 *           that occurs linearly in e and has none yet, if any.
 */
void na_plugin_add_auxvar_definition(na_plugin_t* na, variable_t constraint_var);

/**
 * Requires: x is a real or integer variable without a value in M.
 * Ensures:  result is true iff x has an auxvar definition that is true on the trail and has a
 *           variable other than x without a value in M.
 */
bool na_plugin_auxvar_definition_is_not_unit(na_plugin_t* na, variable_t x);

#endif /* NA_DEFINITIONS_H_ */
