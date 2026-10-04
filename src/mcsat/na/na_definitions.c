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

#include "mcsat/na/na_definitions.h"
#include "mcsat/tracing.h"

#include <poly/assignment.h>
#include <poly/polynomial.h>
#include <poly/variable_list.h>
#include <poly/variable_order.h>

#include <assert.h>

void na_collect_definitions(na_plugin_t* na, int_hmap_t* defs) {
  assert(defs->nelems == 0);
  const mcsat_trail_t* trail = na->ctx->trail;
  assert(trail->to_repropagate.size == 0);
  const lp_assignment_t* m = na->lp_data.lp_assignment;
  // Scan the constraints of NA, not the trail, which is much longer on Boolean-heavy problems
  const ivector_t* cstr_vars = poly_constraint_db_get_constraints(na->constraint_db);
  uint32_t i;
  for (i = 0; i < cstr_vars->size; ++ i) {
    variable_t var = cstr_vars->data[i];
    // (P) A literal that NA propagated by evaluation is not a definition
    if (!trail_has_value(trail, var) || !variable_db_is_boolean(na->ctx->var_db, var) || !trail_get_boolean_value(trail, var)
        || trail_get_source_id(trail, var) == na->ctx->plugin_id) {
      continue;
    }
    const poly_constraint_t* cstr = poly_constraint_db_get(na->constraint_db, var);
    const lp_polynomial_t* e = poly_constraint_get_polynomial(cstr);
    if (poly_constraint_is_root_constraint(cstr) || poly_constraint_get_sign_condition(cstr) != LP_SGN_EQ_0
        || lp_polynomial_is_constant(e)) {
      continue;
    }
    lp_variable_t y = lp_polynomial_top_variable(e);
    // (P) Keep the definition of y that comes first on the trail
    int_hmap_pair_t* find = int_hmap_find(defs, y);
    if (lp_assignment_get_value(m, y)->type == LP_VALUE_NONE
        || (find != NULL && trail_get_index(trail, find->val) < trail_get_index(trail, var))) {
      continue;
    }
    assert(lp_polynomial_is_assigned(e, m));
    if (lp_polynomial_degree(e) == 1 && lp_polynomial_lc_is_constant(e) && lp_polynomial_sgn(e, m) == 0) {
      int_hmap_get(defs, y)->val = var;
    }
  }
}

void na_substitute_definitions(na_plugin_t* na, const int_hmap_t* defs, lp_variable_t x, lp_polynomial_t* p, ivector_t* defs_used) {
  assert(int_hmap_find(defs, x) == NULL);
  // Visit the defined variables y of p in decreasing order (the definition e of y replaces y by
  // variables below y). Substituting e gives p a total degree at most deg(p) + k * (deg(e) - 1),
  // where k = deg_y(p). When e is nonlinear and k >= 2, the substitution multiplies the degree, so
  // a chain of them grows it exponentially: (P) skip e when this bound exceeds 2 * deg(p0).
  const size_t max_degree = 2 * na_poly_total_degree(p);
  lp_variable_t bound = lp_variable_null;  // the last variable visited
  for (;;) {
    lp_variable_list_t vars;
    lp_variable_list_construct(&vars);
    lp_polynomial_get_variables(p, &vars);
    lp_variable_t y = lp_variable_null;
    variable_t y_def = variable_null;
    uint32_t i;
    for (i = 0; i < vars.list_size; ++ i) {
      int_hmap_pair_t* find = int_hmap_find(defs, vars.list[i]);
      if (vars.list[i] != x && find != NULL
          && (bound == lp_variable_null || lp_variable_order_cmp(na->lp_data.lp_var_order, vars.list[i], bound) < 0)
          && (y == lp_variable_null || lp_variable_order_cmp(na->lp_data.lp_var_order, vars.list[i], y) > 0)) {
        y = vars.list[i];
        y_def = find->val;
      }
    }
    lp_variable_list_destruct(&vars);
    if (y == lp_variable_null) {
      return;
    }
    bound = y;
    const lp_polynomial_t* e = poly_constraint_get_polynomial(poly_constraint_db_get(na->constraint_db, y_def));
    assert(lp_polynomial_top_variable(e) == y);
    if (!lp_polynomial_is_linear(e)) {
      size_t k = na_poly_degree(p, y);
      if (k > 1 && na_poly_total_degree(p) + k * (na_poly_total_degree(e) - 1) > max_degree) {
        continue;
      }
    }
    if (ctx_trace_enabled(na->ctx, "na::subst")) {
      ctx_trace_printf(na->ctx, "na_substitute_definitions(): p = ");
      lp_polynomial_print(p, ctx_trace_out(na->ctx));
      ctx_trace_printf(na->ctx, ", e = ");
      lp_polynomial_print(e, ctx_trace_out(na->ctx));
      ctx_trace_printf(na->ctx, "\n");
    }
    na_poly_substitute(p, y, e);
    for (i = 0; i < defs_used->size && defs_used->data[i] != y_def; ++ i) {}
    if (i == defs_used->size) {
      ivector_push(defs_used, y_def);
    }
  }
}

void na_plugin_add_auxvar_definition(na_plugin_t* na, variable_t constraint_var) {
  assert(poly_constraint_db_has(na->constraint_db, constraint_var));
  const poly_constraint_t* cstr = poly_constraint_db_get(na->constraint_db, constraint_var);
  if (!variable_db_is_boolean(na->ctx->var_db, constraint_var) || poly_constraint_is_root_constraint(cstr)
      || poly_constraint_get_sign_condition(cstr) != LP_SGN_EQ_0) {
    return;
  }
  const lp_polynomial_t* e = poly_constraint_get_polynomial(cstr);
  lp_variable_list_t vars;
  lp_variable_list_construct(&vars);
  lp_polynomial_get_variables(e, &vars);
  if (vars.list_size < 2) {
    lp_variable_list_destruct(&vars);
    return;
  }
  variable_t y = variable_null;
  term_t y_term = NULL_TERM;
  uint32_t i;
  for (i = 0; i < vars.list_size; ++ i) {
    term_t t = lp_data_get_term_from_lp_variable(&na->lp_data, vars.list[i]);
    variable_t v = variable_db_get_variable_if_exists(na->ctx->var_db, t);
    term_kind_t kind = term_kind(na->ctx->terms, t);
    bool auxvar = kind == ARITH_RDIV || (kind == UNINTERPRETED_TERM && term_name(na->ctx->terms, t) == NULL);
    if (auxvar && t > y_term && v != variable_null && int_hmap_find(&na->auxvar_definitions, v) == NULL
        && na_poly_degree(e, vars.list[i]) == 1) {
      y = v;
      y_term = t;
    }
  }
  lp_variable_list_destruct(&vars);
  if (y != variable_null) {
    int_hmap_add(&na->auxvar_definitions, y, constraint_var);
  }
}

bool na_plugin_auxvar_definition_is_not_unit(na_plugin_t* na, variable_t x) {
  assert(variable_db_is_real(na->ctx->var_db, x) || variable_db_is_int(na->ctx->var_db, x));
  const mcsat_trail_t* trail = na->ctx->trail;
  int_hmap_pair_t* find = int_hmap_find(&na->auxvar_definitions, x);
  if (find == NULL || !trail_has_value(trail, find->val) || !trail_get_boolean_value(trail, find->val)) {
    return false;
  }
  const lp_assignment_t* m = na->lp_data.lp_assignment;
  const poly_constraint_t* def = poly_constraint_db_get(na->constraint_db, find->val);
  // x occurs in def, and neither x nor the top variable of def has a value in m: so a variable
  // other than x has no value iff def is not unit
  assert(lp_assignment_get_value(m, lp_data_get_lp_variable_from_term(&na->lp_data, variable_db_get_term(na->ctx->var_db, x)))->type == LP_VALUE_NONE);
  assert(lp_assignment_get_value(m, poly_constraint_get_top_variable(def))->type == LP_VALUE_NONE);
  return !poly_constraint_is_unit(def, m);
}
