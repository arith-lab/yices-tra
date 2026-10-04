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

#include "mcsat/tra/tra_func_plugin.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <gmp.h>
#include <mpfr.h>
#include <poly/value.h>

#include "mcsat/value.h"

#include "mcsat/tra/tra_abstract_values.h"
#include "mcsat/tra/tra_conflict_utils.h"
#include "mcsat/tra/tra_plugin_internal.h"
#include "mcsat/tra/utils/tra_tracing.h"

bool tra_application_is_of_the_plugin(const tra_func_plugin_t* fp, term_t t) {
  return tra_get_function_plugin_of_term(fp->tra, t) == fp;
}

const plugin_context_t* tra_get_context(const tra_plugin_t* tra) {
  return tra->ctx;
}

tra_itype_t tra_get_abstraction(const tra_plugin_t* tra, term_t t) {
  ptr_hmap_pair_t* p = ptr_hmap_find(&tra->cache_abstractions, t);
  if (p == NULL || p->val == NULL) return NULL;

  // Entries outlive invalidation, so a stale one must not be handed out.
  tra_cache_abstraction_t* e = (tra_cache_abstraction_t*) p->val;
  return e->valid ? e->interval : NULL;
}

tra_itype_t tra_get_argument_abstraction(const tra_plugin_t* tra, term_t t, uint32_t i) {
  // Argument 0 of an application is the function symbol
  assert(term_kind(tra->ctx->terms, t) == APP_TERM && i + 1 < composite_term_arity(tra->ctx->terms, t));
  
  ptr_hmap_pair_t* p = ptr_hmap_find(&tra->cache_abstractions, t);
  if (p == NULL || p->val == NULL) return NULL;

  tra_cache_abstraction_t* e = (tra_cache_abstraction_t*) p->val;
  assert(i < e->n_args);
  return e->valid ? e->args[i] : NULL;
}

const int_hset_t* tra_get_term_variables(const tra_plugin_t* tra, term_t t) {
  ptr_hmap_pair_t* p = ptr_hmap_find(&tra->attribute_term_variables, t);
  return (p == NULL) ? NULL : (const int_hset_t*) p->val;
}

bool tra_func_approximate_value(const tra_func_plugin_t* fp, const mcsat_value_t* args, tra_prec_t precision,
                                mcsat_value_t* v) {
  tra_func_plugin_t* f = (tra_func_plugin_t*) fp; // the methods below do not modify f
  const tra_ilib_t* lib = fp->abstract_domain;
  uint32_t n = fp->arity;

  tra_itype_t arg_iv[n + 1];
  for (uint32_t i = 0; i < n; i++) arg_iv[i] = lib->alloc();
  tra_itype_t out = lib->alloc();

  // Balls around the arguments, then as small as f needs to reach the precision
  bool ok = true;
  for (uint32_t i = 0; ok && i < n; i++) ok = tra_interval_around_value(lib, &args[i], precision, arg_iv[i]);
  ok = ok && f->is_in_domain(f, arg_iv);
  if (ok) {
    tra_prec_t arg_precision = f->refine_precision(f, arg_iv, precision);
    for (uint32_t i = 0; i < n; i++) tra_interval_around_value(lib, &args[i], arg_precision, arg_iv[i]);
    f->eval(f, arg_iv, precision, out);
  }

  mpfr_t lo, hi;
  mpfr_inits(lo, hi, (mpfr_ptr) 0); // tra_interval_to_mpfr sets their precision
  ok = ok && lib->get_precision(out) >= precision && tra_interval_to_mpfr(lib, out, precision, lo, hi)
          && tra_mpfr_to_q_is_safe(lo) && tra_mpfr_to_q_is_safe(hi);
  if (ok) {
    // The midpoint of [lo, hi] ∋ f(args). Temporary solution: if tra_interval_to_mpfr stops at 2^20
    // bits, [lo, hi] is wider than γ(out), and the midpoint can be far from f(args) (e.g. by 2^105580
    // for exp(800000)). Refusing is worse: the model would then evaluate f as an uninterpreted
    // function, with value 0. The intended solution is that get-value reports the abstraction
    // instead of a value.
    mpq_t q, q_hi;
    mpq_inits(q, q_hi, NULL);
    mpfr_get_q(q, lo);
    mpfr_get_q(q_hi, hi);
    mpq_add(q, q, q_hi);
    mpq_div_2exp(q, q, 1);
    mcsat_value_construct_lp_value_direct(v, LP_VALUE_RATIONAL, q);
    mpq_clears(q, q_hi, NULL);
  }
  mpfr_clears(lo, hi, (mpfr_ptr) 0);

  for (uint32_t i = 0; i < n; i++) lib->free(arg_iv[i]);
  lib->free(out);
  return ok;
}

bool tra_func_set_strategy_profile(tra_func_plugin_t* fp, const uint32_t* order, uint32_t n) {
  if (n > fp->num_strategies) return false;
  for (uint32_t k = 0; k < n; k++) {
    if (order[k] >= fp->num_strategies) return false;
    for (uint32_t j = 0; j < k; j++) {
      if (order[j] == order[k]) return false;
    }
  }

  // Checked before any write, and moved, as order may point into fp->profile.order
  if (n > 0) memmove(fp->profile.order, order, n * sizeof(uint32_t));
  fp->profile.size = n;

  if (tra_trace_enabled(fp->tra, "tra::strategy_manager")) {
    tra_trace_iprintf(fp->tra, "[profile of %s:", fp->name);
    for (uint32_t k = 0; k < n; k++) tra_trace_printf(fp->tra, " %s", fp->strategies[fp->profile.order[k]].name);
    tra_trace_printf(fp->tra, "]\n");
  }
  return true;
}
