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

/* The state of the TRA plugin and its shared helpers. Function plugins must not include this file. */

#ifndef MCSAT_TRA_PLUGIN_INTERNAL_H_
#define MCSAT_TRA_PLUGIN_INTERNAL_H_

#include <stdbool.h>
#include <stdint.h>

#include "yices_types.h"
#include "mcsat/mcsat_types.h"
#include "mcsat/plugin.h"
#include "mcsat/utils/scope_holder.h"
#include "utils/int_hash_map.h"
#include "utils/int_vectors.h"
#include "utils/ptr_hash_map.h"

#include "mcsat/tra/tra_abstract_values.h"
#include "mcsat/tra/tra_func_plugin.h"
#include "mcsat/tra/tra_strategy_manager.h"

typedef struct na_plugin_s na_plugin_t;

/*
 * The notation is that of tra_func_plugin.h: V(t) is defined in tra_get_term_variables, I(t)
 * above tra_get_context, and v(t) in tra_conflict_utils.h. A problem variable is an
 * uninterpreted term that is neither a registered constant (such as pi) nor a purification
 * variable. Applications of uninterpreted functions other than the registered ones are not
 * supported: tra_analyze_term rejects them.
 */

typedef struct tra_plugin_s {

  /* The plugin interface */
  plugin_t plugin_interface;

  /* backend NA plugin */
  na_plugin_t* na_plugin;

  /* The plugin context. */
  plugin_context_t* ctx;

  /* Mask of ctx given to the NA plugin */
  plugin_context_t* na_ctx;

  /* ===============
      Configuration
     =============== */

  /* The abstract domain of every element below (see tra_config.h) */
  tra_ilib_t* abstract_domain;

  /* The function plugins, in the order of tra_registered_functions, and their number */
  tra_func_plugin_t** tra_functions; 
  uint32_t num_tra_functions;

  /* The strategy manager, which chooses the profiles of the function plugins (see tra_config.h) */
  tra_strategy_manager_t* strategy_manager;

  /* ================
      Trail tracking
     ================ */

  /* Invariant: propagate has processed the trail elements below last_known_trail_size. It is
   * saved by push and restored by pop (a pop can put propagations back above it, as NA assumes
   * for its own cursor). */
  uint32_t last_known_trail_size;
  scope_holder_t scope;

  /* The input terms: the subterms of the input assertions, and of the terms that the
   * purification variables met stand for (see tra_plugin_new_assertion_notify). The original
   * constraints are its ARITH_EQ_ATOM, ARITH_GE_ATOM and ARITH_BINEQ_ATOM elements.
   * Invariant: input_terms maps exactly the elements of input_terms_list to 1 or 2; the list has
   * no duplicates and holds positive terms only. 2 marks a term checked by
   * tra_plugin_new_assertion_notify: every term that the analysis reaches from it is analyzable.
   * push saves the size of the list, pop restores it; since input terms are added at the base
   * level, a backtrack never removes them. */
  int_hmap_t input_terms;
  ivector_t input_terms_list;

  /* Note: For delta mode, we just use:
   *  ctx->mcsat_options.bool_delta_mode   :: True iff delta mode is enabled
   *  ctx->mcsat_options.delta_precision    :: User-specified delta value (if delta mode enabled) 
   *  ctx->trigger_delta(ctx, j)           :: To triggered delta at the decision level j
  */

  /* =======================================================================================
      Term attributes: maps from the analyzed terms t (see tra_analyze_term), filled only
      by tra_analyze_term and never invalidated. Each map meets the invariant stated below,
      for the purification variables of t as they were when t was analyzed: after the pop of
      their scope, no original atom contains t (see tra_plugin_event_notify).
     ======================================================================================= */

  /* Invariant: t -> V(t), a closed int_hset_t (see int_hset_close). */
  ptr_hmap_t attribute_term_variables;

  /*
   * Invariant: x -> { t analyzed : x ∈ V(t) }, an int_hset_t that is NOT closed (see
   * tra_wake_dependents to iterate over it), and whose z_flag is false: the term 0 is never a
   * member, since term index 0 is reserved (terms.h). The key is the term x rather than its
   * variable, which may not exist yet when t is analyzed.
   */
  ptr_hmap_t attribute_variable_terms;

  /* Invariant: t -> 1 iff a registered function occurs in t, or (recursively) in the terms
   * that the purification variables of t stand for. */
  int_hmap_t attribute_transcendental; 

  /*
   * Invariant: t -> 1 only if t denotes a function of its problem variables that is defined
   * and continuous everywhere. It is 0 for Boolean terms, divisions and if-then-elses.
   */
  int_hmap_t attribute_total_continuous;

  /*
   * Invariant: t -> 1 iff no problem variable occurs in t, nor (recursively) in the terms
   * that the purification variables of t stand for; t then has a single value. For instance,
   * (sin (exp 16)) and (3/(sin 5)) are constant, and the latter is not total-continuous.
   */
  int_hmap_t attribute_constant; 

  /*
   * Invariant: t -> R(t), such that every value of t, for every assignment of its problem
   * variables, lies in γ(R(t)). For a Boolean term, true is 1 and false is -1.
   */
  ptr_hmap_t attribute_range; 

  /* ==================================
      Term caches (non-monotonic maps)
     ================================== */

  /*
   * Invariant: t -> E(t) (see tra_cache_abstraction_t), created by
   * tra_get_cached_abstraction_entry. Entries are invalidated on restarts, and when a term of
   * V(t) loses its trail value.
   */
  ptr_hmap_t cache_abstractions;

  /* ==================================
      Conflict triggering and analysis
     ================================== */

  /* The terms of the last failed consistency check, from which the conflict strategies start
   * (see tra_conflict_strategies.h). */
  ivector_t cache_conflicts;

  /* Invariant: t -> the number of calls of tra_get_function_plugin_conflict in which t was
   * a candidate without being tried (for fairness). */
  int_hmap_t term_wait_count;

  /* The index in tra_functions of the function plugin last asked by
   * tra_get_function_plugin_conflict (round robin); 0 initially. */
  uint32_t last_conflict_plugin;

} tra_plugin_t;

/*
 * The entry E(t) of cache_abstractions. Entries are never freed, only invalidated (so, in
 * incremental use, their number grows with the analyzed terms). Invariant, when valid holds:
 *  - interval = I(t), computed from the current trail values of the terms of V(t);
 *  - if t is an application, args[0], ..., args[n_args-1] are the elements I_1, ..., I_n
 *    from which interval was computed (see the guarantee above tra_get_context);
 *  - trail_trusted holds if, for some subterm s of t, interval was computed from the trail
 *    value of s rather than from its arguments, while their abstractions did not prove s
 *    undefined (e.g. s = x/y, with 0 in the abstraction of y but y not exactly 0);
 *  - trail_position is the position of v(t) relative to interval (TV_INSIDE when v(t) is
 *    undefined).
 */
typedef struct {
  tra_itype_t interval;
  tra_itype_t* args;
  uint32_t n_args;                   // the number of arguments of t, 0 if t is not an application
  bool trail_trusted;
  tra_tv_position_t trail_position;
  bool valid;
} tra_cache_abstraction_t;


/**
 * Requires: t is positive and not of function type.
 * Ensures:  result is the function plugin that owns t, or NULL if no plugin owns t;
 *           result != NULL ==> t is a constant (result->arity = 0) or an application of
 *           result->arity arguments.
 */
tra_func_plugin_t* tra_get_function_plugin_of_term(const tra_plugin_t* tra, term_t t);

/**
 * Ensures:  result = E(t), the entry of t in cache_abstractions; if t had no entry, a new
 *           one is created, which is not valid.
 */
tra_cache_abstraction_t* tra_get_cached_abstraction_entry(tra_plugin_t* tra, term_t t);

#endif /* MCSAT_TRA_PLUGIN_INTERNAL_H_ */