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

#include "mcsat/tra/tra_conflict_strategies.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

#include "yices_types.h"
#include "api/yices_api_lock_free.h"
#include "mcsat/plugin.h"
#include "terms/term_manager.h"
#include "terms/terms.h"
#include "utils/int_hash_map.h"
#include "utils/int_hash_sets.h"
#include "utils/ptr_hash_map.h"

#include "mcsat/tra/tra_abstract_values.h"
#include "mcsat/tra/tra_conflict_utils.h"
#include "mcsat/tra/tra_consistency.h"
#include "mcsat/tra/tra_func_plugin.h"
#include "mcsat/tra/tra_plugin_internal.h"
#include "mcsat/tra/utils/tra_exact_values.h"
#include "mcsat/tra/utils/tra_term_explorer.h"
#include "mcsat/tra/utils/tra_tracing.h"

/* =========================
    Range conflict strategy
   ========================= */

bool tra_get_range_conflict(tra_plugin_t* tra, ivector_t* conflict_terms, ivector_t* conflict) {
  tra_trace_enter_function(tra, "get_range_conflict");

  // Start from the most recently pushed terms: compute_abstraction's recursion
  // means later entries tend to be the larger, outer terms.
  for (uint32_t k = conflict_terms->size; k > 0; k--) {
    term_t t = (term_t) conflict_terms->data[k - 1];
    if (is_boolean_term(tra->ctx->terms, t)) continue;

    ptr_hmap_pair_t* r = ptr_hmap_find(&tra->attribute_range, t);
    assert(r != NULL && r->val != NULL);

    term_t literal = tra_conflict_escape_literal(tra->ctx, tra->abstract_domain, t, (tra_itype_t) r->val, NULL);
    if (literal != NULL_TERM) {
      ivector_push(conflict, literal);
      if (tra_trace_enabled(tra, "tra::conflict_strategies")) {
        tra_trace_iprintf(tra, "[conflict: ");
        tra_trace_print_term(tra, literal);
        tra_trace_printf(tra, "]\n");
      }
      tra_trace_exit_function(tra, "get_range_conflict");
      return true;
    }
  }

  tra_trace_exit_function(tra, "get_range_conflict");

  return false;
}

/* ============================
    Constant conflict strategy
   ============================ */

bool tra_get_constant_term_conflict(tra_plugin_t* tra, ivector_t* conflict_terms, ivector_t* conflict) {
  tra_trace_enter_function(tra, "get_constant_term_conflict");

  for (uint32_t k = 0; k < conflict_terms->size; k++) {
    term_t ct = (term_t) conflict_terms->data[k];
    if (is_boolean_term(tra->ctx->terms, ct)) continue;
    int_hmap_pair_t* ct_constant = int_hmap_find(&tra->attribute_constant, ct);
    int_hmap_pair_t* ct_total_continuous = int_hmap_find(&tra->attribute_total_continuous, ct);
    assert(ct_constant != NULL && ct_total_continuous != NULL);
    if (!ct_constant->val || !ct_total_continuous->val) continue;

    tra_itype_t interval = tra_get_abstraction(tra, ct);
    if (interval == NULL) continue;

    term_t literal = tra_conflict_escape_literal(tra->ctx, tra->abstract_domain, ct, interval, NULL);
    if (literal != NULL_TERM) {
      ivector_push(conflict, literal);
      if (tra_trace_enabled(tra, "tra::conflict_strategies")) {
        tra_trace_iprintf(tra, "[conflict: ");
        tra_trace_print_term(tra, literal);
        tra_trace_printf(tra, "]\n");
      }
      tra_trace_exit_function(tra, "get_constant_term_conflict");
      return true;
    }
  }

  tra_trace_exit_function(tra, "get_constant_term_conflict");

  return false;
}

/* ===================================
    Function plugin conflict strategy
   =================================== */

bool tra_get_function_plugin_conflict(tra_plugin_t* tra, ivector_t* conflict_terms, ivector_t* conflict) {
  tra_trace_enter_function(tra, "get_function_plugin_conflict");

  //We use wait counts to avoid picking the same term over and over again
  uint32_t best_idx = 0;
  int32_t best_wait_count = -1;
  for (uint32_t k = 0; k < conflict_terms->size; k++) {
    term_t t = (term_t) conflict_terms->data[k];
    if (is_boolean_term(tra->ctx->terms, t)) continue;
    int_hmap_pair_t* p = int_hmap_get(&tra->term_wait_count, t);
    p->val += 1;
    if (p->val > best_wait_count) {
      best_wait_count = p->val;
      best_idx = k;
    }
  }

  //We use a round-robin strategy to pick the function plugins. The atoms come first: a plugin
  //may relate the applications of its function occurring in one (e.g., by monotonicity).
  //Without a conflict, the pass over the atoms leaves the cursor unchanged (a full round).
  for (int atoms = 1; atoms >= 0; atoms--) {
    for (uint32_t fi = 0; fi < tra->num_tra_functions; fi++) {
      tra->last_conflict_plugin = (tra->last_conflict_plugin + 1) % tra->num_tra_functions;
      tra_func_plugin_t* fp = tra->tra_functions[tra->last_conflict_plugin];

      for (uint32_t j = 0; j < conflict_terms->size; j++) {
        term_t t = (term_t) conflict_terms->data[(best_idx + j) % conflict_terms->size];
        if (is_boolean_term(tra->ctx->terms, t) != atoms) continue;

        if (tra_trace_enabled(tra, "tra::conflict_strategies")) {
          tra_trace_iprintf(tra, "[%s asked %s ", fp->name,
                            atoms ? "for a conflict on atom" : "to refine abstraction for term");
          tra_trace_print_term(tra, t);
          tra_trace_printf(tra, "]\n");
        }

        if (!atoms) int_hmap_get(&tra->term_wait_count, t)->val = 0;
        // The strategies of the profile of fp, in order, then its default conflict (s = NULL)
        const tra_func_strategy_t* s = NULL;
        for (uint32_t k = 0; s == NULL && k < fp->profile.size; k++) {
          assert(fp->profile.order[k] < fp->num_strategies);
          const tra_func_strategy_t* sk = &fp->strategies[fp->profile.order[k]];
          if (sk->get_conflict(fp, t, conflict)) s = sk;
          assert((s != NULL) == (conflict->size > 0));
        }
        bool found = s != NULL || fp->default_conflict(fp, t, conflict);
        assert(found == (conflict->size > 0));
        if (found) {
          if (tra_trace_enabled(tra, "tra::conflict_strategies")) {
            tra_trace_iprintf(tra, "[%s: %s]\n", fp->name, s != NULL ? s->name : "default");
            tra_trace_iprintf(tra, "[conflict:");
            for (uint32_t l = 0; l < conflict->size; l++) {
              tra_trace_printf(tra, " ");
              tra_trace_print_term(tra, conflict->data[l]);
            }
            tra_trace_printf(tra, "]\n");
          }
          tra_trace_exit_function(tra, "get_function_plugin_conflict");
          return true;
        }
      }
    }
  }
  tra_trace_exit_function(tra, "get_function_plugin_conflict");
  return false;
}

/* ============================
    Fallback conflict strategy
   ============================ */

/*
 * Requires: t is a real ite or a division; conflict = [].
 * Ensures:  conflict = [], or conflict is a definition conflict for t (tra_get_fallback_conflict);
 *           (P):  conflict ≠ [] if there is a definition conflict for t.
 */
static void tra_definition_conflict(tra_plugin_t* tra, term_t t, ivector_t* conflict) {
  assert(conflict->size == 0);
  term_table_t* terms = tra->ctx->terms;
  // Not the global manager: it may rewrite t ≠ a into ¬c, which is not true in the trail
  term_manager_t* tm = tra->ctx->tm;

  switch (term_kind(terms, t)) {
    case ITE_TERM:
    case ITE_SPECIAL: {
      assert(!is_boolean_term(terms, t));
      composite_term_t* ite = ite_term_desc(terms, t);
      term_t c = ite->arg[0], a = ite->arg[1], b = ite->arg[2];
      term_t c_literal = tra_conflict_boolean_literal(tra->ctx, c);
      term_t branch = c_literal == c ? a : b;
      if (c_literal != NULL_TERM && tra_value_cmp_terms(tra->ctx, t, branch) != 0) {
        ivector_push(conflict, c_literal);
        ivector_push(conflict, opposite_term(mk_eq(tm, t, branch)));
      } else if (tra_value_cmp_terms(tra->ctx, t, a) != 0 && tra_value_cmp_terms(tra->ctx, t, b) != 0) {
        ivector_push(conflict, opposite_term(mk_eq(tm, t, a)));
        ivector_push(conflict, opposite_term(mk_eq(tm, t, b)));
      }
      break;
    }

    case ARITH_RDIV: {
      composite_term_t* rdiv = arith_rdiv_term_desc(terms, t);
      term_t m = rdiv->arg[0], n = rdiv->arg[1];
      if (tra_value_cmp_terms(tra->ctx, n, zero_term) == 0) break;
      term_t n_times_t = _o_yices_mul(n, t);
      if (tra_value_cmp_terms(tra->ctx, m, n_times_t) != 0) {
        ivector_push(conflict, opposite_term(mk_arith_term_eq0(tm, n)));
        ivector_push(conflict, opposite_term(mk_eq(tm, m, n_times_t)));
      }
      break;
    }

    default:
      assert(false);
  }
}

bool tra_get_fallback_conflict(tra_plugin_t* tra, ivector_t* conflict_terms, ivector_t* conflict) {
  assert(conflict->size == 0);
  tra_trace_enter_function(tra, "get_fallback_conflict");
  term_table_t* terms = tra->ctx->terms;

  // First, a definition conflict
  for (uint32_t k = 0; k < conflict_terms->size && conflict->size == 0; k++) {
    term_t atom = conflict_terms->data[k];
    if (!is_boolean_term(terms, atom)) continue;
    const int_hset_t* vars = tra_get_term_variables(tra, atom);
    assert(vars != NULL); // atom is analyzed
    for (uint32_t i = 0; i < vars->nelems && conflict->size == 0; i++) {
      term_t t = vars->data[i];
      term_kind_t kind = term_kind(terms, t);
      // To allow abs ∈ V, add its definition to tra_definition_conflict
      assert(kind != ARITH_ABS);
      bool real_ite = (kind == ITE_TERM || kind == ITE_SPECIAL) && !is_boolean_term(terms, t);
      if (real_ite || kind == ARITH_RDIV) tra_definition_conflict(tra, t, conflict);
    }
  }

  // Otherwise, the first refuted atom, with its leaves
  for (uint32_t k = 0; k < conflict_terms->size && conflict->size == 0; k++) {
    term_t atom = conflict_terms->data[k];
    if (!is_boolean_term(terms, atom)) continue;
    assert(is_pos_term(atom));
    term_kind_t kind = term_kind(terms, atom);
    assert(kind == ARITH_EQ_ATOM || kind == ARITH_GE_ATOM || kind == ARITH_BINEQ_ATOM);
    tra_itype_t J = tra_get_abstraction(tra, tra_atom_argument(tra, atom));
    if (J == NULL) continue;
    term_t L = tra_conflict_boolean_literal(tra->ctx, atom);
    assert(L != NULL_TERM); // atom ∈ V(atom)
    int truth = tra_get_atom_truth(tra->abstract_domain, J, kind != ARITH_GE_ATOM);
    if (truth == 0 || (truth > 0) == (L == atom)) continue;

    const int_hset_t* vars = tra_get_term_variables(tra, atom);
    for (uint32_t i = 0; i < vars->nelems; i++) {
      term_t t = vars->data[i];
      term_kind_t t_kind = term_kind(terms, t);
      // Skip the non-leaves: I(t) is not built around v(t) (for applications, by is_in_domain)
      tra_func_plugin_t* fp = t_kind == APP_TERM || t_kind == UNINTERPRETED_TERM ? tra_get_function_plugin_of_term(tra, t) : NULL;
      if (is_boolean_term(terms, t) && t_kind != UNINTERPRETED_TERM) continue;
      if (t_kind == UNINTERPRETED_TERM && (fp != NULL || tra_depurify_term_step(tra->ctx->preprocessor, t) != t)) continue;
      if (t_kind == APP_TERM && fp->is_total_continuous) continue;
      term_t l = is_boolean_term(terms, t) ? tra_conflict_boolean_literal(tra->ctx, t) : tra_conflict_value_literal(tra->ctx, t);
      assert(l != NULL_TERM); // t ∈ V(atom)
      ivector_push(conflict, l);
    }
    ivector_push(conflict, L);
  }

  // Remove true_term: ctx->tm may simplify a literal to it
  uint32_t n = 0;
  for (uint32_t i = 0; i < conflict->size; i++) {
    assert(conflict->data[i] != false_term);
    if (conflict->data[i] != true_term) conflict->data[n++] = conflict->data[i];
  }
  ivector_shrink(conflict, n);

  if (conflict->size > 0 && tra_trace_enabled(tra, "tra::conflict_strategies")) {
    tra_trace_iprintf(tra, "[conflict:");
    for (uint32_t i = 0; i < conflict->size; i++) {
      tra_trace_printf(tra, " ");
      tra_trace_print_term(tra, conflict->data[i]);
    }
    tra_trace_printf(tra, "]\n");
  }

  tra_trace_exit_function(tra, "get_fallback_conflict");
  return conflict->size > 0;
}
