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

#include "mcsat/tra/functions/tra_pi.h"

#include <stdbool.h>
#include <stddef.h>

#include "yices_types.h"
#include "api/yices_api_lock_free.h"
#include "mcsat/mcsat_types.h"
#include "mcsat/plugin.h"
#include "utils/memalloc.h"

#include "mcsat/tra/tra_abstract_values.h"
#include "mcsat/tra/tra_conflict_utils.h"

// Enclosure of π in the lemmas on pi: (NUM - 1)/DEN < π < NUM/DEN, of width 1.9e-8
#define TRA_PI_LEMMA_NUM 165707065
#define TRA_PI_LEMMA_DEN 52746197

/* =============
    Term Notify
   ============= */

static void tra_pi_new_term_notify(tra_func_plugin_t* self, term_t t, trail_token_t* prop) {
  // Only called with t = pi, once per variable of pi. Lemmas (NUM - 1)/DEN <= pi <= NUM/DEN:
  // NA's placeholder for pi then starts close to π
  term_t lower_bound = _o_yices_rational32(TRA_PI_LEMMA_NUM - 1, TRA_PI_LEMMA_DEN);
  term_t upper_bound = _o_yices_rational32(TRA_PI_LEMMA_NUM, TRA_PI_LEMMA_DEN);
  term_t ineq1 = _o_yices_arith_geq_atom(t, lower_bound);
  term_t ineq2 = _o_yices_arith_geq_atom(upper_bound, t);
  prop->lemma(prop, ineq1);
  prop->lemma(prop, ineq2);
}

/* ==========
    Evaluate
   ========== */

static void tra_pi_eval(tra_func_plugin_t* self, tra_itype_t* args, tra_prec_t precision, tra_itype_t out) {
  self->abstract_domain->const_pi(out, precision);
}

/* ==========
    Destruct
   ========== */

static void tra_pi_destruct(tra_func_plugin_t* self) {
  safe_free(self);
}

/* ===========
    Allocator 
   =========== */

tra_func_plugin_t* tra_pi_plugin_allocator(const struct tra_plugin_s* tra){
  tra_func_plugin_t* plugin = (tra_func_plugin_t*) safe_malloc(sizeof(tra_func_plugin_t));
  plugin->name = "pi";
  plugin->arity = 0;
  plugin->is_total_continuous = true;

  plugin->tra = tra;

  plugin->dependencies = NULL;
  plugin->num_dependencies = 0;
  plugin->dependencies_plugins = NULL;
  plugin->add_dependency = tra_func_default_add_dependency;

  plugin->abstract_domain = NULL;
  plugin->add_abstract_domain = tra_func_default_add_abstract_domain;

  plugin->new_term_notify = tra_pi_new_term_notify;

  plugin->is_in_domain = tra_func_default_is_in_domain;

  plugin->let_na_decide = tra_func_default_let_na_decide;

  plugin->get_implicit_variables = tra_func_default_get_implicit_variables;
  
  plugin->hint_argument_value = tra_func_default_hint_argument_value;

  plugin->eval = tra_pi_eval;
  
  plugin->refine_precision = tra_func_default_refine_precision;

  // pi has no conflict strategy of its own
  plugin->strategies = NULL;
  plugin->num_strategies = 0;
  plugin->default_conflict = tra_func_default_get_conflict;

  plugin->destruct = tra_pi_destruct;

  return plugin;
}