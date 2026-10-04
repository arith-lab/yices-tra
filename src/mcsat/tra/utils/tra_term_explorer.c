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

#include "mcsat/tra/utils/tra_term_explorer.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "yices_types.h"
#include "api/yices_api_lock_free.h"
#include "mcsat/preprocessor.h"
#include "terms/term_explorer.h"
#include "terms/terms.h"
#include "utils/int_hash_sets.h"
#include "utils/int_vectors.h"

term_t tra_depurify_term_step(const preprocessor_t* preprocessor, term_t t) {
  term_table_t* terms = preprocessor->terms;

  // Fast path
  if (is_pos_term(t) && term_kind(terms, t) == UNINTERPRETED_TERM) {
    term_t def = preprocessor_purification_definition(preprocessor, t);
    return (def == NULL_TERM) ? t : def;
  }

  // The substitution is restricted to the purification variables that occur in t: the others
  // do not change the result. It is applied even when there is none, since it rebuilds t.
  ivector_t vars, defs, todo;
  int_hset_t visited;
  init_ivector(&vars, 0);
  init_ivector(&defs, 0);
  init_ivector(&todo, 0);
  init_int_hset(&visited, 0);
  ivector_push(&todo, t);
  while (todo.size > 0) {
    term_t u = ivector_pop2(&todo);
    if (!int_hset_add(&visited, u)) continue;
    if (is_pos_term(u) && term_kind(terms, u) == UNINTERPRETED_TERM) {
      term_t def = preprocessor_purification_definition(preprocessor, u);
      if (def != NULL_TERM) {
        ivector_push(&vars, u);
        ivector_push(&defs, def);
      }
      continue;
    }
    uint32_t n = term_num_children(terms, u);
    for (uint32_t i = 0; i < n; i++) {
      term_t c = term_ith_subterm(terms, u, i);
      if (c != NULL_TERM) ivector_push(&todo, c);
    }
  }

  term_t result = _o_yices_subst_term(vars.size, vars.data, defs.data, t);
  delete_int_hset(&visited);
  delete_ivector(&todo);
  delete_ivector(&defs);
  delete_ivector(&vars);
  return result;
}

term_t tra_depurify_term(const preprocessor_t* preprocessor, term_t t) {
  if (term_kind(preprocessor->terms, t) == ARITH_ROOT_ATOM) {
    return t;
  }

  // Apply substitutions until fixpoint
  term_t result = t, prev;
  do {
    prev = result;
    result = tra_depurify_term_step(preprocessor, result);
  } while (result != prev);

  return result;
}

