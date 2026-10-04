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

#include "mcsat/tra/tra_plugin.h"

#include <assert.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "yices_types.h"
#include "api/yices_api_lock_free.h"
#include "context/context_types.h"
#include "io/tracer.h"
#include "mcsat/gc.h"
#include "mcsat/mcsat_types.h"
#include "mcsat/na/feasible_set_db.h"
#include "mcsat/na/na_plugin.h"
#include "mcsat/na/na_plugin_internal.h"
#include "mcsat/plugin.h"
#include "mcsat/preprocessor.h"
#include "mcsat/tracing.h"
#include "mcsat/trail.h"
#include "mcsat/utils/int_mset.h"
#include "mcsat/value.h"
#include "mcsat/variable_db.h"
#include "model/model_eval.h"
#include "model/models.h"
#include "terms/term_explorer.h"
#include "terms/terms.h"
#include "terms/types.h"
#include "utils/int_hash_map.h"
#include "utils/int_hash_sets.h"
#include "utils/int_vectors.h"
#include "utils/memalloc.h"
#include "utils/ptr_hash_map.h"

#include "mcsat/tra/tra_abstract_values.h"
#include "mcsat/tra/tra_analyze_term.h"
#include "mcsat/tra/tra_config.h"
#include "mcsat/tra/tra_conflict_strategies.h"
#include "mcsat/tra/tra_consistency.h"
#include "mcsat/tra/tra_func_plugin.h"
#include "mcsat/tra/tra_plugin_internal.h"
#include "mcsat/tra/utils/tra_term_explorer.h"
#include "mcsat/tra/utils/tra_tracing.h"


/* ========================
    Symbols (tra_plugin.h)
   ======================== */

uint32_t tra_num_symbols(void) {
  return NUM_TRA_FUNCS;
}

const mcsat_symbol_t* tra_symbol(uint32_t i) {
  assert(i < NUM_TRA_FUNCS);
  return &tra_registered_functions[i].symbol;
}

/* ===================================
    Decisions of the function plugins
   =================================== */

/* True if the function plugin owning t, if any, lets NA decide t.
 * Otherwise the plugin names a term to decide instead. The plugin
 * is then asked to provide a value for this term, which it can 
 * avoid giving.
 */
static bool tra_let_na_decide(tra_plugin_t* tra, term_t t) {
  tra_func_plugin_t* fp = tra_get_function_plugin_of_term(tra, t);
  term_t instead = (fp == NULL) ? NULL_TERM : fp->let_na_decide(fp, t);
  if (instead == NULL_TERM) return true;

  variable_t y = variable_db_get_variable_if_exists(tra->ctx->var_db, instead);
  assert(y != variable_null);
  mcsat_value_t value;
  if (fp->hint_argument_value(fp, instead, &value)) {
    tra->ctx->hint_value(tra->ctx, y, &value);
    mcsat_value_destruct(&value);
  }
  tra->ctx->hint_next_decision(tra->ctx, y);
  return false;
}

/* ===================================
    Context masking for the NA plugin
   =================================== */

// We need to mask contexts to intercept calls of the NA plugin

typedef struct {
  plugin_context_t ctx; 
  plugin_context_t* original;
  tra_plugin_t* tra;          // For tracing
} masked_context_t;

static void free_masked_ctx(plugin_context_t* possible_mask, plugin_context_t* original) {
  // Iteratively free a chain of masks until we get to the original token (not freed).
  if (possible_mask != original) {
    masked_context_t* mt = (masked_context_t*) possible_mask;
    free_masked_ctx(mt->original, original);
    safe_free(mt);
  }
}

// Context mask methods

static void na_ctx_mask_request_term_notification_by_kind(plugin_context_t* self, term_kind_t kind, bool is_internal) {
  // Does nothing
  masked_context_t* mt = (masked_context_t*) self;
  tra_trace_iprintf_if(mt->tra, "tra::na_ctx",
                       "[ctx->request_term_notification_by_kind called: kind=%s, is_internal=%s]\n",
                       kind_to_string(kind), is_internal ? "true" : "false");
  mt->original->request_term_notification_by_kind(mt->original, kind, is_internal);
}

static void na_ctx_mask_request_term_notification_by_type(plugin_context_t* self, type_kind_t type) {
  // Does nothing
  masked_context_t* mt = (masked_context_t*) self;
  tra_trace_iprintf_if(mt->tra, "tra::na_ctx",
                       "[ctx->request_term_notification_by_type called: type=%s]\n", type_to_string(type));
  mt->original->request_term_notification_by_type(mt->original, type);
}

static void na_ctx_mask_request_restart(plugin_context_t* self) {
  // Forwards the request; fails in debug mode, since NA never requests a restart
  masked_context_t* mt = (masked_context_t*) self;
  tra_trace_iprintf_if(mt->tra, "tra::na_ctx", "[ctx->request_restart called]\n");
  
  assert(false);
  mt->original->request_restart(mt->original);
}

static void na_ctx_mask_report_failure(plugin_context_t* self) {
  masked_context_t* mt = (masked_context_t*) self;
  tra_trace_iprintf_if(mt->tra, "tra::na_ctx", "[ctx->report_failure called]\n");
  mt->original->report_failure(mt->original);
}

static void na_ctx_mask_request_gc(plugin_context_t* self) {
  // Does nothing
  masked_context_t* mt = (masked_context_t*) self;
  tra_trace_iprintf_if(mt->tra, "tra::na_ctx", "[ctx->request_gc called]\n");
  mt->original->request_gc(mt->original);
}

static void na_ctx_mask_request_decision_calls(plugin_context_t* self, type_kind_t type) {
  // Does nothing
  masked_context_t* mt = (masked_context_t*) self;
  tra_trace_iprintf_if(mt->tra, "tra::na_ctx", "[ctx->request_decision_calls called: type=%s]\n", type_to_string(type));
  mt->original->request_decision_calls(mt->original, type);
}

static void na_ctx_mask_bump_variable(plugin_context_t* self, variable_t x) {
  // Does nothing
  masked_context_t* mt = (masked_context_t*) self;
  if (tra_trace_enabled(mt->tra, "tra::na_ctx")) {
    tra_trace_iprintf(mt->tra, "[ctx->bump_variable called: ");
    tra_trace_print_term(mt->tra, variable_db_get_term(mt->tra->ctx->var_db, x));
    tra_trace_printf(mt->tra, "]\n");
  }
  mt->original->bump_variable(mt->original, x);
}

static void na_ctx_mask_bump_variable_n(plugin_context_t* self, variable_t x, uint32_t n) {
  // Does nothing
  masked_context_t* mt = (masked_context_t*) self;
  if (tra_trace_enabled(mt->tra, "tra::na_ctx")) {
    tra_trace_iprintf(mt->tra, "[ctx->bump_variable_n called: n=%u, ", n);
    tra_trace_print_term(mt->tra, variable_db_get_term(mt->tra->ctx->var_db, x));
    tra_trace_printf(mt->tra, "]\n");
  }
  mt->original->bump_variable_n(mt->original, x, n);
}

static int na_ctx_mask_cmp_variables(plugin_context_t* self, variable_t x, variable_t y) {
  // Does nothing
  masked_context_t* mt = (masked_context_t*) self;
  int result = mt->original->cmp_variables(mt->original, x, y);
  if (tra_trace_enabled(mt->tra, "tra::na_ctx")) {
    tra_trace_iprintf(mt->tra, "[ctx->cmp_variables called: ");
    tra_trace_print_term(mt->tra, variable_db_get_term(mt->tra->ctx->var_db, x));
    tra_trace_printf(mt->tra, " vs ");
    tra_trace_print_term(mt->tra, variable_db_get_term(mt->tra->ctx->var_db, y));
    tra_trace_printf(mt->tra, " : %d]\n", result);
  }
  return result;
}

static void na_ctx_mask_request_top_decision(plugin_context_t* self, variable_t x) {
  // Does nothing
  masked_context_t* mt = (masked_context_t*) self;
  if (tra_trace_enabled(mt->tra, "tra::na_ctx")) {
    tra_trace_iprintf(mt->tra, "[ctx->request_top_decision called: ");
    tra_trace_print_term(mt->tra, variable_db_get_term(mt->tra->ctx->var_db, x));
    tra_trace_printf(mt->tra, "]\n");
  }
  mt->original->request_top_decision(mt->original, x);
}

static void na_ctx_mask_hint_next_decision(plugin_context_t* self, variable_t x) {
  masked_context_t* mt = (masked_context_t*) self;
  term_t x_term = variable_db_get_term(mt->tra->ctx->var_db, x);
  if (tra_trace_enabled(mt->tra, "tra::na_ctx")) {
    tra_trace_iprintf(mt->tra, "[ctx->hint_next_decision called: ");
    tra_trace_print_term(mt->tra, x_term);
    tra_trace_printf(mt->tra, "]\n");
  }

  // Check whether the function plugin owning x_term likes the hint
  if (!tra_let_na_decide(mt->tra, x_term)) {
    tra_trace_iprintf_if(mt->tra, "tra::na_ctx", "[hint replaced: the function plugin did not let NA decide]\n");

    return;
  }

  mt->original->hint_next_decision(mt->original, x);
}

static void na_ctx_mask_hint_value(plugin_context_t* self, variable_t x, const mcsat_value_t* val) {
  // Does nothing
  masked_context_t* mt = (masked_context_t*) self;
  if (tra_trace_enabled(mt->tra, "tra::na_ctx")) {
    tra_trace_iprintf(mt->tra, "[ctx->hint_value called]\n");
    tra_trace_print_variable_assignment(mt->tra, x, val);
  }
  mt->original->hint_value(mt->original, x, val);
}

static void na_ctx_mask_trigger_delta(plugin_context_t* self, uint32_t level) {
  // Does nothing
  masked_context_t* mt = (masked_context_t*) self;
  tra_trace_iprintf_if(mt->tra, "tra::na_ctx", "[ctx->trigger_delta called: level=%u]\n", level);
  mt->original->trigger_delta(mt->original, level);
}

static void na_ctx_mask_register_term(plugin_context_t* self, term_t t) {
  // Does nothing
  masked_context_t* mt = (masked_context_t*) self;
  if (tra_trace_enabled(mt->tra, "tra::na_ctx")) {
    tra_trace_iprintf(mt->tra, "[ctx->register_term called: ");
    tra_trace_print_term(mt->tra, t);
    tra_trace_printf(mt->tra, "]\n");
  }
  mt->original->register_term(mt->original, t);
}

static bool na_ctx_mask_type_is_equality_sensitive(plugin_context_t* self, type_t tau) {
  // Does nothing
  masked_context_t* mt = (masked_context_t*) self;
  bool result = mt->original->type_is_equality_sensitive(mt->original, tau);
  tra_trace_iprintf_if(mt->tra, "tra::na_ctx",
                       "[ctx->type_is_equality_sensitive called: %s]\n", result ? "true" : "false");
  return result;
}

static uint32_t na_ctx_mask_equality_sensitivity_generation(plugin_context_t* self) {
  // Does nothing
  masked_context_t* mt = (masked_context_t*) self;
  uint32_t result = mt->original->equality_sensitivity_generation(mt->original);
  tra_trace_iprintf_if(mt->tra, "tra::na_ctx", "[ctx->equality_sensitivity_generation called: %u]\n", result);
  return result;
}

static bool na_ctx_mask_equality_sensitivity_is_frozen(plugin_context_t* self) {
  // Does nothing
  masked_context_t* mt = (masked_context_t*) self;
  bool result = mt->original->equality_sensitivity_is_frozen(mt->original);
  tra_trace_iprintf_if(mt->tra, "tra::na_ctx",
                       "[ctx->equality_sensitivity_is_frozen called: %s]\n", result ? "true" : "false");
  return result;
}

// Create context mask

static masked_context_t* create_na_ctx_mask(tra_plugin_t* tra, plugin_context_t* original) {
  masked_context_t* mt = safe_malloc(sizeof(masked_context_t));
  mt->original = original;
  mt->tra = tra;

  // The data of the context is copied from the original
  mt->ctx = *original;

  // Set up wrapper function pointers. Every callback of plugin_context_t must be wrapped:
  // the original callbacks expect their own context, not the mask, as self.
  mt->ctx.request_term_notification_by_kind = na_ctx_mask_request_term_notification_by_kind;
  mt->ctx.request_term_notification_by_type = na_ctx_mask_request_term_notification_by_type;
  mt->ctx.request_restart = na_ctx_mask_request_restart;
  mt->ctx.report_failure = na_ctx_mask_report_failure;
  mt->ctx.request_gc = na_ctx_mask_request_gc;
  mt->ctx.request_decision_calls = na_ctx_mask_request_decision_calls;
  mt->ctx.bump_variable = na_ctx_mask_bump_variable;
  mt->ctx.bump_variable_n = na_ctx_mask_bump_variable_n;
  mt->ctx.cmp_variables = na_ctx_mask_cmp_variables;
  mt->ctx.request_top_decision = na_ctx_mask_request_top_decision;
  mt->ctx.hint_next_decision = na_ctx_mask_hint_next_decision;
  mt->ctx.hint_value = na_ctx_mask_hint_value;
  mt->ctx.trigger_delta = na_ctx_mask_trigger_delta;
  mt->ctx.register_term = na_ctx_mask_register_term;
  mt->ctx.type_is_equality_sensitive = na_ctx_mask_type_is_equality_sensitive;
  mt->ctx.equality_sensitivity_generation = na_ctx_mask_equality_sensitivity_generation;
  mt->ctx.equality_sensitivity_is_frozen = na_ctx_mask_equality_sensitivity_is_frozen;

  return mt;
}

/* ===================================
    Trail token masking for NA plugin
   =================================== */

// Similar to context masking, but for trail_token_t
// At the moment we only implement a printer mask
typedef struct {
  trail_token_t token;          
  trail_token_t* original;      
  tra_plugin_t* tra;        // For tracing
} masked_trail_token_t;

static void free_masked_token(trail_token_t* possible_mask, trail_token_t* original) {
  //Iteratively free a chain of masks until we get to the original token (not freed).
  if (possible_mask != original) {
    masked_trail_token_t* mt = (masked_trail_token_t*) possible_mask;
    free_masked_token(mt->original, original);
    safe_free(mt);
  }
}

// Printer token mask methods

static bool printer_add(trail_token_t* token, variable_t x, const mcsat_value_t* value) {
  masked_trail_token_t* mt = (masked_trail_token_t*) token;
  tra_trace_iprintf(mt->tra, "[token->add called]\n");
  tra_trace_print_variable_assignment(mt->tra, x, value);
  return mt->original->add(mt->original, x, value);
}

static bool printer_add_at_level(trail_token_t* token, variable_t x, const mcsat_value_t* value, uint32_t level) {
  masked_trail_token_t* mt = (masked_trail_token_t*) token;
  tra_trace_iprintf(mt->tra, "[token->add_at_level called: level=%u]\n", level);
  tra_trace_print_variable_assignment(mt->tra, x, value);
  return mt->original->add_at_level(mt->original, x, value, level);
}

static void printer_conflict(trail_token_t* token) {
  masked_trail_token_t* mt = (masked_trail_token_t*) token;
  tra_trace_iprintf(mt->tra, "[token->conflict called]\n");
  mt->original->conflict(mt->original);
}

static void printer_lemma(trail_token_t* token, term_t lemma) {
  masked_trail_token_t* mt = (masked_trail_token_t*) token;
  tra_trace_iprintf(mt->tra, "[token->lemma called]\n");
  tra_trace_iprintf(mt->tra, "[lemma ");
  tra_trace_print_term(mt->tra, lemma);
  tra_trace_printf(mt->tra, "]\n");
  mt->original->lemma(mt->original, lemma);
}

static void printer_definition_lemma(trail_token_t* token, term_t lemma, term_t x) {
  masked_trail_token_t* mt = (masked_trail_token_t*) token;
  tra_trace_iprintf(mt->tra, "[token->definition_lemma called]\n");
  tra_trace_iprintf(mt->tra, "[lemma ");
  tra_trace_print_term(mt->tra, lemma);
  tra_trace_printf(mt->tra, " for ");
  tra_trace_print_term(mt->tra, x);
  tra_trace_printf(mt->tra, "]\n");
  mt->original->definition_lemma(mt->original, lemma, x);
}

// Create printer token mask

static masked_trail_token_t* create_printer_token(tra_plugin_t* tra, trail_token_t* original) {
  masked_trail_token_t* mt = safe_malloc(sizeof(masked_trail_token_t));
  mt->original = original;
  mt->tra = tra;
  
  // Set up wrapper function pointers
  mt->token.add = printer_add;
  mt->token.add_at_level = printer_add_at_level;
  mt->token.conflict = printer_conflict;
  mt->token.lemma = printer_lemma;
  mt->token.definition_lemma = printer_definition_lemma;
  
  return mt;
}

/* ========================
    Construct and destruct
   ======================== */

static void tra_plugin_construct(plugin_t* plugin, plugin_context_t* ctx) {
  tra_plugin_t* tra = (tra_plugin_t*) plugin;

  // OPTIONS NOT AVAILABLE IN CONSTRUCTOR, THEY ARE SETUP LATER
  // (In particular, the tracer is not currently available)

  tra->ctx = ctx;

  tra->last_known_trail_size = 0;
  scope_holder_construct(&tra->scope);
  init_int_hmap(&tra->input_terms, 0);
  init_ivector(&tra->input_terms_list, 0);

  // Specifies abstract domain to be used (see tra_config.h)
  tra->abstract_domain = tra_abstract_domain_configure();

  init_ptr_hmap(&tra->attribute_term_variables, 0);
  init_ptr_hmap(&tra->attribute_variable_terms, 0);
  init_int_hmap(&tra->attribute_transcendental, 0);
  init_int_hmap(&tra->attribute_total_continuous, 0);
  init_int_hmap(&tra->attribute_constant, 0);
  init_ptr_hmap(&tra->attribute_range, 0);

  init_ptr_hmap(&tra->cache_abstractions, 0);
  init_ivector(&tra->cache_conflicts, 0);
  init_int_hmap(&tra->term_wait_count, 0);
  tra->last_conflict_plugin = 0;

  // The NA plugin is only provided the masked context
  // so that the TRA plugin can intercept callbacks
  tra->na_ctx = (plugin_context_t*) create_na_ctx_mask(tra, ctx);

  tra->na_plugin = (na_plugin_t*) na_plugin_allocator();
  tra->na_plugin->plugin_interface.construct((plugin_t*) tra->na_plugin, tra->na_ctx);

  // Register supported transcendental functions (as in tra_config.h)
  tra->num_tra_functions = NUM_TRA_FUNCS;
  tra->tra_functions = (tra_func_plugin_t**) safe_malloc(tra->num_tra_functions * sizeof(tra_func_plugin_t*));

  for (uint32_t i = 0; i < tra->num_tra_functions; i++) {
    tra->tra_functions[i] = tra_registered_functions[i].tra_func_allocator(tra);

    assert(strcmp(tra->tra_functions[i]->name, tra_registered_functions[i].symbol.name) == 0);
    assert(tra->tra_functions[i]->arity == (int) tra_registered_functions[i].symbol.arity);
    // The names are pairwise distinct: the owner of a term is found by name
    for (uint32_t k = 0; k < i; k++) assert(strcmp(tra->tra_functions[k]->name, tra->tra_functions[i]->name) != 0);
  }

  for (uint32_t i = 0; i < tra->num_tra_functions; i++) {

    // Register dependencies with other functions
    for(int j = 0; j < tra->tra_functions[i]->num_dependencies; j++) {
      const char* dep_name = tra->tra_functions[i]->dependencies[j];
      uint32_t k = 0;
      while (k < tra->num_tra_functions && (k == i || strcmp(tra->tra_functions[k]->name, dep_name) != 0)) k++;
      assert(k < tra->num_tra_functions); // every dependency is a registered function
      if (k < tra->num_tra_functions) tra->tra_functions[i]->add_dependency(tra->tra_functions[i], tra->tra_functions[k]);
    }

    // Register abstract domain
    tra->tra_functions[i]->add_abstract_domain(tra->tra_functions[i], tra->abstract_domain);

    // Empty profile, with room for every strategy (freed in tra_plugin_destruct)
    tra_func_profile_t* profile = &tra->tra_functions[i]->profile;
    profile->order = (uint32_t*) safe_malloc(tra->tra_functions[i]->num_strategies * sizeof(uint32_t));
    profile->size = 0;
  }

  // The strategy manager (see tra_config.h)
  tra->strategy_manager = tra_strategy_manager_allocator(tra, tra->tra_functions, tra->num_tra_functions);
}

static void tra_plugin_destruct(plugin_t* plugin) {
  tra_plugin_t* tra = (tra_plugin_t*) plugin;

  // Destruct NA plugin
  if (tra->na_plugin != NULL) {
      tra->na_plugin->plugin_interface.destruct((plugin_t*) tra->na_plugin);
      free(tra->na_plugin);
  }

  // Free the context mask given to the NA plugin 
  // (tra->ctx itself is not ours to free)
  free_masked_ctx(tra->na_ctx, tra->ctx);
  tra->na_ctx = tra->ctx;

  // Destruct the strategy manager, before the function plugins that it reads
  tra->strategy_manager->destruct(tra->strategy_manager);

  // Destruct the function plugins, and their profiles
  for (uint32_t i = 0; i < tra->num_tra_functions; i++) {
    safe_free(tra->tra_functions[i]->profile.order);
    tra->tra_functions[i]->destruct(tra->tra_functions[i]);
  }
  safe_free(tra->tra_functions);

  // Free attribute_term_variables
  ptr_hmap_pair_t* tv_pair = ptr_hmap_first_record(&tra->attribute_term_variables);
  while (tv_pair != NULL) {
    if (tv_pair->val != NULL) {
      delete_int_hset((int_hset_t*) tv_pair->val);
      safe_free(tv_pair->val);
    }
    tv_pair = ptr_hmap_next_record(&tra->attribute_term_variables, tv_pair);
  }
  delete_ptr_hmap(&tra->attribute_term_variables);

  // Free attribute_variable_terms 
  ptr_hmap_pair_t* vt_pair = ptr_hmap_first_record(&tra->attribute_variable_terms);
  while (vt_pair != NULL) {
    if (vt_pair->val != NULL) {
      delete_int_hset((int_hset_t*) vt_pair->val);
      safe_free(vt_pair->val);
    }
    vt_pair = ptr_hmap_next_record(&tra->attribute_variable_terms, vt_pair);
  }
  delete_ptr_hmap(&tra->attribute_variable_terms);

  // Free attribute_transcendental
  delete_int_hmap(&tra->attribute_transcendental);

  // Free attribute_total_continuous
  delete_int_hmap(&tra->attribute_total_continuous);

  // Free attribute_constant
  delete_int_hmap(&tra->attribute_constant);

  // Free attribute_range
  ptr_hmap_pair_t* range_pair = ptr_hmap_first_record(&tra->attribute_range);
  while (range_pair != NULL) {
    if (range_pair->val != NULL) {
      tra->abstract_domain->free((tra_itype_t) range_pair->val);
    }
    range_pair = ptr_hmap_next_record(&tra->attribute_range, range_pair);
  }
  delete_ptr_hmap(&tra->attribute_range);

  // Free cache_abstractions
  ptr_hmap_pair_t* itv_pair = ptr_hmap_first_record(&tra->cache_abstractions);
  while (itv_pair != NULL) {
    if (itv_pair->val != NULL) {
      tra_cache_abstraction_t* e = (tra_cache_abstraction_t*) itv_pair->val;
      tra->abstract_domain->free(e->interval);
      for (uint32_t i = 0; i < e->n_args; i++) tra->abstract_domain->free(e->args[i]);
      safe_free(e->args);
      safe_free(e);
    }
    itv_pair = ptr_hmap_next_record(&tra->cache_abstractions, itv_pair);
  }
  delete_ptr_hmap(&tra->cache_abstractions);

  // Free cache_conflicts
  delete_ivector(&tra->cache_conflicts);

  // Free term_wait_count
  delete_int_hmap(&tra->term_wait_count);

  scope_holder_destruct(&tra->scope);
  delete_int_hmap(&tra->input_terms);
  delete_ivector(&tra->input_terms_list);
}

/* ===============
    Notifications
   =============== */

/*
 * The token given to function plugins in new_term_notify (only lemma is available, see the
 * contract of new_term_notify in tra_func_plugin.h): a lemma about t becomes a definition lemma of
 * t, which MCSAT keeps as long as t has a variable. A plain lemma is removed with the scope where it
 * is asserted, and dropped if the solver is inconsistent when it is flushed, while t is notified
 * only once per variable.
 */
typedef struct {
  trail_token_t token;
  trail_token_t* original;
  term_t t;
} definition_trail_token_t;

static void definition_token_lemma(trail_token_t* token, term_t lemma) {
  definition_trail_token_t* dt = (definition_trail_token_t*) token;
  dt->original->definition_lemma(dt->original, lemma, dt->t);
}

static void tra_plugin_new_term_notify(plugin_t* plugin, term_t t, trail_token_t* prop) {
  tra_plugin_t* tra = (tra_plugin_t*) plugin;
  trail_token_t* original_prop = prop;

  if (tra_trace_enabled(tra, "tra::notify")) {
    tra_trace_enter_function(tra, "notify");
    tra_trace_print_term_infos(tra, t);
    prop = (trail_token_t*) create_printer_token(tra, prop);
  }

  tra->na_plugin->plugin_interface.new_term_notify((plugin_t*) tra->na_plugin, t, prop);

  free_masked_token(prop, original_prop);
  prop = original_prop;

  // We add initial lemmas for transcendental functions: notify the plugin owning t, and
  // every plugin depending on it (in any registration order)
  tra_func_plugin_t* owner = tra_get_function_plugin_of_term(tra, t);
  if (owner != NULL) {
    // Lemma lifetimes as in new_term_notify (tra_func_plugin.h). After the pop of its scope, a
    // purification variable no longer stands for its term, while the attributes of t still assume
    // it does. An argument that is an application may contain a purification variable.
    term_table_t* terms = tra->ctx->terms;
    uint32_t n = term_kind(terms, t) == APP_TERM ? composite_term_arity(terms, t) : 0;
    bool scoped = false;
    for (uint32_t i = 1; i < n && !scoped; i++) {
      term_t a = composite_term_arg(terms, t, i);
      term_kind_t k = term_kind(terms, a);
      scoped = k != ARITH_CONSTANT &&
               (k != UNINTERPRETED_TERM || tra_depurify_term_step(tra->ctx->preprocessor, a) != a);
    }
    definition_trail_token_t definition = { .token.lemma = definition_token_lemma, .original = prop, .t = t };
    trail_token_t* fp_original = scoped ? prop : &definition.token;
    trail_token_t* fp_prop = tra_trace_enabled(tra, "tra::notify") ?
                             (trail_token_t*) create_printer_token(tra, fp_original) : fp_original;

    for (uint32_t i = 0; i < tra->num_tra_functions; i++) {
      tra_func_plugin_t* fp = tra->tra_functions[i];
      bool notify = fp == owner;
      for (uint32_t j = 0; j < fp->num_dependencies && !notify; j++) {
        notify = strcmp(owner->name, fp->dependencies[j]) == 0;
      }
      if (notify) fp->new_term_notify(fp, t, fp_prop);
    }
    free_masked_token(fp_prop, fp_original);

    // The implicit terms of t (see get_implicit_variables) get their variables with t
    if (owner->arity > 0) {
      int_hset_t implicit;
      init_int_hset(&implicit, 0);
      owner->get_implicit_variables(owner, t, &implicit);
      int_hset_close(&implicit);
      for (uint32_t i = 0; i < implicit.nelems; i++) tra->ctx->register_term(tra->ctx, implicit.data[i]);
      delete_int_hset(&implicit);
    }
  }

  tra_trace_exit_function(tra, "notify");
}

//NOTE: At the moment we are just forwarding to the NA plugin
static void tra_plugin_new_lemma_notify(plugin_t* plugin, ivector_t* lemma, trail_token_t* prop) {
  tra_plugin_t* tra = (tra_plugin_t*) plugin;
  trail_token_t* original_prop = prop;

  if (tra_trace_enabled(tra, "tra::lemma_notify")) {
    tra_trace_enter_function(tra, "lemma_notify");
    tra_trace_iprintf(tra, "[lemma notified: ");
    for (uint32_t i = 0; i < lemma->size; ++ i) {
      if (i) tra_trace_printf(tra, " \U00002228 ");
      tra_trace_print_term(tra, lemma->data[i]);
    }
    tra_trace_printf(tra, "]\n");

    tra_trace_iprintf(tra, "[current trail]\n");
    tra_trail_print(tra);

    prop = (trail_token_t*) create_printer_token(tra, prop);
  }

  tra->na_plugin->plugin_interface.new_lemma_notify((plugin_t*) tra->na_plugin, lemma, prop);

  free_masked_token(prop, original_prop);
  prop = original_prop;

  if (tra_trace_enabled(tra, "tra::lemma_notify")) {
    tra_trace_printf(tra, "\n");
    tra_trace_exit_function(tra, "lemma_notify");
  }
}


/**
 * Let t be a term that contains a purification variable of a popped scope.
 * Requires: no lemma of t is pending at the pop (t is registered with an assertion, and its lemmas
 *           are flushed right after that assertion).
 * Modifies: NA, to which every event is forwarded, and the strategy manager, to which RESTART and
 *           DELTA_BUMP are forwarded. On RESTART, every entry of cache_abstractions is invalidated,
 *           and cache_conflicts and term_wait_count are reset.
 * Ensures:  the invariants of tra_plugin_t hold; on RESTART also without the invalidation, since a
 *           restart backtracks through tra_plugin_pop. After a POP, the stale attributes of t are
 *           never read: no original atom contains t, and the pop removed the lemmas of t (see
 *           new_term_notify in tra_func_plugin.h).
 */
static void tra_plugin_event_notify(plugin_t* plugin, plugin_notify_kind_t kind) {
  tra_plugin_t* tra = (tra_plugin_t*) plugin;
  tra->na_plugin->plugin_interface.event_notify((plugin_t*) tra->na_plugin, kind);

  if (tra_trace_enabled(tra, "tra::event")) {
    tra_trace_enter_function(tra, "event");
    tra_trace_iprintf(tra, "[event: %s]\n", tra_notify_kind_to_string(kind));
  }

  if (kind == MCSAT_SOLVER_RESTART) {
    ptr_hmap_pair_t* itv_pair = ptr_hmap_first_record(&tra->cache_abstractions);
    while (itv_pair != NULL) {
      if (itv_pair->val != NULL) {
        ((tra_cache_abstraction_t*) itv_pair->val)->valid = false;
      }
      itv_pair = ptr_hmap_next_record(&tra->cache_abstractions, itv_pair);
    }

    ivector_reset(&tra->cache_conflicts);
    int_hmap_reset(&tra->term_wait_count);
  }

  if (kind == MCSAT_SOLVER_RESTART || kind == MCSAT_DELTA_BUMP) {
    assert(trail_is_at_base_level(tra->ctx->trail));
    tra->strategy_manager->event_notify(tra->strategy_manager, kind, tra->ctx->trail);
  }

  tra_trace_exit_function(tra, "event");
}

/* =================================
    Propagate (and related methods)
   ================================= */

/*
 * True iff t is an original constraint: an ARITH_EQ_ATOM, ARITH_GE_ATOM or ARITH_BINEQ_ATOM
 * input term (see input_terms). Only original constraints get a consistency check of their own.
 * This is a property of the atom's position in the input, not of its shape. A negative literal
 * is not an input term: it is checked through its atom.
 */
static bool tra_is_original_atom(tra_plugin_t* tra, term_t t) {
  if (int_hmap_find(&tra->input_terms, t) == NULL) return false;
  term_kind_t tk = term_kind(tra->ctx->terms, t);
  return tk == ARITH_EQ_ATOM || tk == ARITH_GE_ATOM || tk == ARITH_BINEQ_ATOM;
}

/*
 * Requires: f is an input assertion; the trail is at the base level.
 * Modifies: input_terms and input_terms_list: adds every subterm of f, and of the terms that
 *           the purification variables met stand for, as a positive term. A term already in
 *           input_terms is not explored again: its subterms are already there.
 * Ensures:  every term that the analysis reaches from an original constraint of f is analyzable
 *           (see tra_term_is_analyzable); otherwise MCSAT_EXCEPTION_UNSUPPORTED_THEORY is raised.
 *           The analysis runs during the search, where the handler of this exception is no longer
 *           valid: the check is made here, at assertion time, where it is.
 */
static void tra_plugin_new_assertion_notify(plugin_t* plugin, term_t f) {
  tra_plugin_t* tra = (tra_plugin_t*) plugin;
  term_table_t* terms = tra->ctx->terms;
  assert(trail_is_at_base_level(tra->ctx->trail));
  uint32_t first_new = tra->input_terms_list.size;

  ivector_t todo;
  init_ivector(&todo, 0);
  ivector_push(&todo, unsigned_term(f));
  while (todo.size > 0) {
    term_t t = ivector_pop2(&todo);
    int_hmap_pair_t* entry = int_hmap_get(&tra->input_terms, t);
    if (entry->val > 0) continue;
    entry->val = 1;
    ivector_push(&tra->input_terms_list, t);
    if (term_kind(terms, t) == UNINTERPRETED_TERM) {
      term_t def = preprocessor_purification_definition(tra->ctx->preprocessor, t);
      if (def != NULL_TERM) ivector_push(&todo, unsigned_term(def));
      continue;
    }
    uint32_t n = term_num_children(terms, t);
    for (uint32_t i = 0; i < n; i++) {
      term_t c = term_ith_subterm(terms, t, i);
      if (c != NULL_TERM) ivector_push(&todo, unsigned_term(c));
    }
  }

  // Check the new original constraints, exploring their subterms as the analysis does
  for (uint32_t j = first_new; j < tra->input_terms_list.size; j++) {
    if (tra_is_original_atom(tra, tra->input_terms_list.data[j])) {
      ivector_push(&todo, tra->input_terms_list.data[j]);
    }
  }
  while (todo.size > 0) {
    term_t t = ivector_pop2(&todo);
    int_hmap_pair_t* entry = int_hmap_find(&tra->input_terms, t);
    assert(entry != NULL);
    if (entry->val == 2) continue;
    if (!tra_term_is_analyzable(tra, t)) {
      delete_ivector(&todo);
      longjmp(*tra->ctx->exception, MCSAT_EXCEPTION_UNSUPPORTED_THEORY);
    }
    entry->val = 2;
    term_kind_t k = term_kind(terms, t);
    if (k == UNINTERPRETED_TERM) {
      term_t def = preprocessor_purification_definition(tra->ctx->preprocessor, t);
      if (def != NULL_TERM) ivector_push(&todo, unsigned_term(def));
      continue;
    }
    // The function of an application is not analyzed: only its arguments are
    uint32_t n = term_num_children(terms, t);
    for (uint32_t i = (k == APP_TERM) ? 1 : 0; i < n; i++) {
      term_t c = term_ith_subterm(terms, t, i);
      if (c != NULL_TERM) ivector_push(&todo, unsigned_term(c));
    }
  }
  delete_ivector(&todo);
}

/*
 * Runs the consistency check of every analyzed term that lists 'var_term' among
 * its variables (attribute_variable_terms) and is currently assigned in the trail.
 */
static void tra_wake_dependents(tra_plugin_t* tra, term_t var_term) {
  ptr_hmap_pair_t* dep_pair = ptr_hmap_find(&tra->attribute_variable_terms, var_term);
  if (dep_pair == NULL || dep_pair->val == NULL) return;

  tra_trace_iprintf_if(tra, "tra::propagate", "[assignment of a variable term: waking its dependents]\n");

  int_hset_t* dependents = (int_hset_t*) dep_pair->val;

  // Not closed, and z_flag is false (see attribute_variable_terms): walk the table, skip 0
  assert(!dependents->z_flag);
  for (uint32_t j = 0; j < dependents->size; j++) {
    term_t t = (term_t) dependents->data[j];
    if (t == 0) continue;

    // Only original constraints that are currently assigned are checked
    if (!tra_is_original_atom(tra, t)) continue;
    variable_t term_id = variable_db_get_variable_if_exists(tra->ctx->var_db, t);
    if (term_id == variable_null || !trail_has_value(tra->ctx->trail, term_id)) continue;

    tra_consistency_check(tra, t);
  }
}

static void tra_plugin_propagate(plugin_t* plugin, trail_token_t* prop) {
  tra_plugin_t* tra = (tra_plugin_t*) plugin;
  trail_token_t* original_prop = prop;
  const mcsat_trail_t* trail = tra->ctx->trail;
  variable_db_t* var_db = tra->ctx->var_db;

  if (tra_trace_enabled(tra, "tra::propagate")) {
    tra_trace_enter_function(tra, "propagate");
    tra_trace_iprintf(tra, "[trail before propagations]\n");
    tra_trail_print(tra);
    tra_trace_iprintf(tra, "[ask NA to propagate]\n");
    prop = (trail_token_t*) create_printer_token(tra, prop);
  }

  tra->na_plugin->plugin_interface.propagate((plugin_t*) tra->na_plugin, prop);

  free_masked_token(prop, original_prop);
  prop = original_prop;

  // Even if NA reported a conflict, the consistency checks below run: the conflicts that TRA
  // builds are assumed easier than those of NA.

  // Check new entries in the trail
  while(tra->last_known_trail_size < trail_size(trail)) {
    variable_t var = trail_at(trail, tra->last_known_trail_size);
    tra->last_known_trail_size++;

    term_t term = variable_db_get_term(var_db, var);

    if (tra_trace_enabled(tra, "tra::propagate")) {
      tra_trace_iprintf(tra, "[checking term]\n");
      tra_trace_print_term_infos(tra, term);
    }

    // Original constraints (see tra_is_original_atom) are analyzed when they enter
    // the trail, in their original form: an equality t1 = t2 is NOT rewritten into
    // t1 - t2 = 0, so that every tracked atom is a term of the variable database
    // (see the matching comment in tra_evaluate_consistency).
    if (tra_is_original_atom(tra, term)) tra_analyze_term(tra, term);

    // Wake up the consistency checks waiting on this assignment: every analyzed
    // term listing 'term' among its variables (atoms list themselves, so an
    // original constraint is checked here as its own dependent).
    tra_wake_dependents(tra, term);
  }

  // Raise a conflict (if any exists)
  if (tra->cache_conflicts.size > 0) prop->conflict(prop);

  tra_trace_exit_function(tra, "propagate");
}

/* ========
    Decide
   ======== */

static void tra_plugin_decide(plugin_t* plugin, variable_t x, trail_token_t* decide_token, bool must) {
  tra_plugin_t* tra = (tra_plugin_t*) plugin;
  trail_token_t* original_decide_token = decide_token;
  term_t x_term = variable_db_get_term(tra->ctx->var_db, x);

  if (tra_trace_enabled(tra, "tra::decide")) {
    tra_trace_enter_function(tra, "decide");
    tra_trace_print_term_infos(tra, x_term);
    decide_token = (trail_token_t*) create_printer_token(tra, decide_token);
  }

  bool na_can_decide = true;

  if (!must) {
    // Check with the transcendental function plugin if x can be decided.
    na_can_decide = tra_let_na_decide(tra, x_term);
  }

  if (na_can_decide) tra->na_plugin->plugin_interface.decide((plugin_t*) tra->na_plugin, x, decide_token, must);

  free_masked_token(decide_token, original_decide_token);
  decide_token = original_decide_token;

  if (tra_trace_enabled(tra, "tra::decide")) {
    if (trail_has_value(tra->ctx->trail, x)) {
      tra_trace_iprintf(tra, "[NA decided a value");
      if (must) tra_trace_printf(tra, " (it was mandatory)");
      tra_trace_printf(tra, "]\n");
    } else if (na_can_decide) {
      tra_trace_iprintf(tra, "[NA did not decide a value]\n");
    } else {
      tra_trace_iprintf(tra, "[A function plugin did not let NA decide a value]\n");
    }
    tra_trace_exit_function(tra, "decide");
  }
}

/**
 * Let t be the term of x.
 * Modifies: the trail (NA decides x = value); if t is a transcendental uninterpreted term (owned
 *           by a function plugin of arity 0), such as pi, also the attributes of t, E(t),
 *           tra->cache_conflicts and the delta level.
 * Ensures:  a conflict is reported if NA reports one. Otherwise, if t is a transcendental
 *           uninterpreted term, it is checked (see tra_check_trail_value_transcendental_constant):
 *           on TRA_CC_INCONSISTENT a conflict is reported, with t in tra->cache_conflicts; on
 *           TRA_CC_DELTA delta mode is triggered at the current decision level; on TRA_CC_UNKNOWN
 *           a failure is reported. Without this check an assumption such as pi = 3 could be
 *           accepted: the atom checks abstract pi by its definition, and NA rejects pi = 3 only when
 *           a bound on pi is in the trail. Applications, such as (sin 3), are never assumed: only
 *           symbols are.
 */
static void tra_plugin_decide_assignment(plugin_t* plugin, variable_t x, const mcsat_value_t* value, trail_token_t* decide) {
  tra_plugin_t* tra = (tra_plugin_t*) plugin;
  trail_token_t* original_decide = decide;

  if (tra_trace_enabled(tra, "tra::decide_assignment")) {
    tra_trace_enter_function(tra, "decide_assignment");
    decide = (trail_token_t*) create_printer_token(tra, decide);
  }

  tra->na_plugin->plugin_interface.decide_assignment((plugin_t*) tra->na_plugin, x, value, decide);

  term_t t = variable_db_get_term(tra->ctx->var_db, x);
  tra_func_plugin_t* fp = tra_get_function_plugin_of_term(tra, t);
  if (fp != NULL && fp->arity == 0 && fp->is_total_continuous && trail_is_consistent(tra->ctx->trail)) {
    tra_analyze_term(tra, t);
    tra_cc_verdict_t verdict = tra_check_trail_value_transcendental_constant(tra, t);
    if (verdict == TRA_CC_INCONSISTENT) {
      ivector_push(&tra->cache_conflicts, t);
      decide->conflict(decide);
    } else if (verdict == TRA_CC_DELTA) {
      tra_trace_iprintf_if(tra, "tra::decide_assignment", "[ctx->trigger_delta called: level=%u]\n",
                           tra->ctx->trail->decision_level);
      tra->ctx->trigger_delta(tra->ctx, tra->ctx->trail->decision_level);
    } else if (verdict == TRA_CC_UNKNOWN) {
      assert(false); // Fail in debug mode, else report a failure
      tra->ctx->report_failure(tra->ctx);
    }
  }

  free_masked_token(decide, original_decide);
  decide = original_decide;

  tra_trace_exit_function(tra, "decide_assignment");
}

/* =======
    Learn
   ======= */

//NOTE: At the moment we are just forwarding to the NA plugin
static void tra_plugin_learn(plugin_t* plugin, trail_token_t* prop) {
  tra_plugin_t* tra = (tra_plugin_t*) plugin;
  trail_token_t* original_prop = prop;
  
  if (tra_trace_enabled(tra, "tra::learn")) {
    tra_trace_enter_function(tra, "learn");
    prop = (trail_token_t*) create_printer_token(tra, prop);
  }

  tra->na_plugin->plugin_interface.learn((plugin_t*) tra->na_plugin, prop);

  free_masked_token(prop, original_prop);
  prop = original_prop;
  
  tra_trace_exit_function(tra, "learn");
}

/* ==============
    Get conflict
   ============== */

/**
 * Let C be tra->cache_conflicts at entry.
 * Modifies: tra->cache_conflicts := ∅.
 * Ensures:  conflict is the first nonempty conflict built by tra_get_range_conflict(C),
 *           tra_get_constant_term_conflict(C), tra_get_function_plugin_conflict(C), NA and,
 *           above the base level, tra_get_fallback_conflict(C), or ∅ if there is none;
 *           conflict = ∅ at the base level ==> the trail is unsatisfiable;
 *           conflict = ∅ above the base level ==> a failure is reported.
 */
static void tra_plugin_get_conflict(plugin_t* plugin, ivector_t* conflict) {
  tra_plugin_t* tra = (tra_plugin_t*) plugin;

  tra_trace_enter_function(tra, "get_conflict");

  // We first try explain our own conflicts before caring about others'
  if (tra->cache_conflicts.size > 0) {
    if (tra_trace_enabled(tra, "tra::get_conflict")) {
      tra_trace_iprintf(tra, "[%u term(s) whose trail value escapes their interval:", tra->cache_conflicts.size);
      for (uint32_t k = 0; k < tra->cache_conflicts.size; k++) {
        tra_trace_printf(tra, " ");
        tra_trace_print_term(tra, tra->cache_conflicts.data[k]);
      }
      tra_trace_printf(tra, "]\n");
    }
    
    // List of strategies 
    if(tra_get_range_conflict(tra, &tra->cache_conflicts, conflict)) goto conflict_found;
    if(tra_get_constant_term_conflict(tra, &tra->cache_conflicts, conflict)) goto conflict_found;
    if(tra_get_function_plugin_conflict(tra, &tra->cache_conflicts, conflict)) goto conflict_found;
  }
  assert(conflict->size == 0);

  // This call has no effect if the na_plugin is not in conflict state. Otherwise, conflict->size > 0,
  // except for a conflict found by NA's learn, which has no cause (see the base level case below)
  tra_trace_iprintf_if(tra, "tra::get_conflict", "[asking NA to produce a conflict]\n");

  tra->na_plugin->plugin_interface.get_conflict((plugin_t*) tra->na_plugin, conflict);

  if (conflict->size > 0) {
    if (tra_trace_enabled(tra, "tra::get_conflict")) {
      tra_trace_iprintf(tra, "[NA conflict:");
      for (uint32_t i = 0; i < conflict->size; ++ i) {
        tra_trace_printf(tra, " ");
        tra_trace_print_term(tra, conflict->data[i]);
      }
      tra_trace_printf(tra, "]\n");
    }
    goto conflict_found;
  }
  tra_trace_iprintf_if(tra, "tra::get_conflict", "[NA did not produce a conflict]\n");

  // Last resort at the base level: the empty conflict, from which MCSAT concludes unsat. 
  // This is sound because the base level holds no decision, and if we arrive here either 
  // NA or the TRA plugin have raised a conflict. However, we don't explain it.
  if (trail_is_at_base_level(tra->ctx->trail)) {
    tra_trace_iprintf_if(tra, "tra::get_conflict",
                         "[no conflict could be explained at the base level: empty conflict]\n");
    goto conflict_found;
  }

  if (tra_get_fallback_conflict(tra, &tra->cache_conflicts, conflict)) goto conflict_found;

  // This code is reachable if no strategy produced a conflict above the base level.
  // Report a failure in this case: the solver restarts
  tra_trace_iprintf_if(tra, "tra::get_conflict", "[no conflict could be explained: reporting a failure]\n");

  assert(false); // Fail in debug mode, else report a failure
  tra->ctx->report_failure(tra->ctx);

  conflict_found:

  ivector_reset(&tra->cache_conflicts);

  tra_trace_exit_function(tra, "get_conflict");
}

/* =========
    Explain
   ========= */

//NOTE: At the moment we are just forwarding to the NA plugin
static term_t tra_plugin_explain_propagation(plugin_t* plugin, variable_t var, ivector_t* reasons) {
  tra_plugin_t* tra = (tra_plugin_t*) plugin;

  tra_trace_enter_function(tra, "explain_propagation");

  term_t explanation = tra->na_plugin->plugin_interface.explain_propagation((plugin_t*) tra->na_plugin, var, reasons);

  if (tra_trace_enabled(tra, "tra::explain_propagation")) {
    tra_trace_iprintf(tra, "[NA explanation: ");
    tra_trace_print_term(tra, explanation);
    tra_trace_printf(tra, "]\n");
    tra_trace_exit_function(tra, "explain_propagation");
  }

  return explanation;
}

//NOTE: At the moment we are just forwarding to the NA plugin
static bool tra_plugin_explain_evaluation(plugin_t* plugin, term_t t, int_mset_t* vars, const mcsat_value_t* value) {
  tra_plugin_t* tra = (tra_plugin_t*) plugin;

  if (tra_trace_enabled(tra, "tra::explain_evaluation")) {
    tra_trace_enter_function(tra, "explain_evaluation");
    tra_trace_iprintf(tra, "[term: ");
    tra_trace_print_term(tra, t);
    tra_trace_printf(tra, "]\n");
  }

  bool result = tra->na_plugin->plugin_interface.explain_evaluation((plugin_t*) tra->na_plugin, t, vars, value);

  if (tra_trace_enabled(tra, "tra::explain_evaluation")) {
    tra_trace_iprintf(tra, "[NA explanation: %s]\n", result ? "true" : "false");
    tra_trace_exit_function(tra, "explain_evaluation");
  }

  return result;
}

/* ==========
    Simplify
   ========== */

//NOTE: At the moment we are just forwarding to the NA plugin
static bool tra_plugin_simplify_conflict_literal(plugin_t* plugin, term_t lit, ivector_t* output) {
  tra_plugin_t* tra = (tra_plugin_t*) plugin;

  tra_trace_enter_function(tra, "simplify_conflict_literal");

  bool result = tra->na_plugin->plugin_interface.simplify_conflict_literal((plugin_t*) tra->na_plugin, lit, output);

  tra_trace_exit_function(tra, "simplify_conflict_literal");

  return result;
}

/* ==============
    Push and pop
   ============== */

static void tra_plugin_push(plugin_t* plugin) {
  tra_plugin_t* tra = (tra_plugin_t*) plugin;

  tra_trace_enter_function(tra, "push");

  tra->na_plugin->plugin_interface.push((plugin_t*) tra->na_plugin);
  scope_holder_push(&tra->scope, &tra->last_known_trail_size, &tra->input_terms_list.size, NULL);

  tra_trace_exit_function(tra, "push");
}

static void tra_plugin_pop(plugin_t* plugin) {
    tra_plugin_t* tra = (tra_plugin_t*) plugin;
    const mcsat_trail_t* trail = tra->ctx->trail;

    if (tra_trace_enabled(tra, "tra::pop")) {
      tra_trace_enter_function(tra, "pop");
      tra_trace_iprintf(tra, "[current trail]\n");
      tra_trail_print(tra);
    }

    tra->na_plugin->plugin_interface.pop((plugin_t*) tra->na_plugin);

    // A pending conflict is stale after a pop, as in NA. It is not always consumed by get_conflict:
    // MCSAT skips get_conflict for a conflict at the base level or during an assertion
    ivector_reset(&tra->cache_conflicts);

    // Invalidate cache_abstractions for every term that is no longer fully assigned (a pop also
    // undoes each decision level, so this runs at every backtrack)
    ivector_t* unassigned = trail_get_unassigned((mcsat_trail_t*) trail);
    for (uint32_t i = 0; i < unassigned->size; i++) {
      variable_t v = unassigned->data[i];
      // attribute_variable_terms is keyed by the leaf's term_t, not its variable_t.
      // Note that v is guaranteed registered here (it came off the trail),
      // so this lookup is always safe.
      term_t v_term = variable_db_get_term(tra->ctx->var_db, v);
      ptr_hmap_pair_t* dep_pair = ptr_hmap_find(&tra->attribute_variable_terms, v_term);
      if (dep_pair == NULL || dep_pair->val == NULL) continue;

      int_hset_t* terms_using_vars = (int_hset_t*) dep_pair->val;

      // Not closed, and z_flag is false (see attribute_variable_terms): walk the table, skip 0.
      // A term without an entry keeps none: the entry created later on demand (see
      // tra_get_cached_abstraction_entry) is the fresh invalid entry that pop created before.
      assert(!terms_using_vars->z_flag);
      for (uint32_t j = 0; j < terms_using_vars->size; j++) {
        uint32_t elem = terms_using_vars->data[j];
        if (elem == 0) continue;
        ptr_hmap_pair_t* e = ptr_hmap_find(&tra->cache_abstractions, (term_t) elem);
        if (e != NULL && e->val != NULL) ((tra_cache_abstraction_t*) e->val)->valid = false;
      }
    }

    uint32_t input_terms_size;
    scope_holder_pop(&tra->scope, &tra->last_known_trail_size, &input_terms_size, NULL);
    while (tra->input_terms_list.size > input_terms_size) {
      term_t t = ivector_pop2(&tra->input_terms_list);
      int_hmap_pair_t* entry = int_hmap_find(&tra->input_terms, t);
      assert(entry != NULL);
      int_hmap_erase(&tra->input_terms, entry);
    }

    tra_trace_exit_function(tra, "pop");
}

/* ====
    GC
   ==== */

/**
 * Modifies: gc_vars: NA marks its variables, then the variables of the implicit terms of every
 *           marked application are marked.
 * Ensures:  an application that keeps its variable keeps the variables of its implicit terms (which
 *           got their variables with it, see tra_plugin_new_term_notify).
 */
static void tra_plugin_gc_mark(plugin_t* plugin, gc_info_t* gc_vars) {
    tra_plugin_t* tra = (tra_plugin_t*) plugin;

    tra_trace_enter_function(tra, "gc_mark");

    tra->na_plugin->plugin_interface.gc_mark((plugin_t*) tra->na_plugin, gc_vars);

    // The whole vector is scanned (it grows in the loop): the plugins after TRA mark variables
    // in the same round, after TRA has seen the new ones
    term_table_t* terms = tra->ctx->terms;
    int_hset_t implicit;
    init_int_hset(&implicit, 0);
    for (uint32_t i = 0; i < gc_vars->marked.size; i++) {
      term_t t = variable_db_get_term(tra->ctx->var_db, gc_vars->marked.data[i]);
      if (term_kind(terms, t) != APP_TERM || term_type_kind(terms, t) == FUNCTION_TYPE) continue;
      tra_func_plugin_t* fp = tra_get_function_plugin_of_term(tra, t);
      if (fp == NULL) continue;
      int_hset_reset(&implicit);
      fp->get_implicit_variables(fp, t, &implicit);
      int_hset_close(&implicit);
      for (uint32_t j = 0; j < implicit.nelems; j++) {
        variable_t u = variable_db_get_variable_if_exists(tra->ctx->var_db, implicit.data[j]);
        assert(u != variable_null);
        gc_info_mark(gc_vars, u);
      }
    }
    delete_int_hset(&implicit);

    tra_trace_exit_function(tra, "gc_mark");
}

//NOTE: At the moment we are just forwarding to the NA plugin
static void tra_plugin_gc_sweep(plugin_t* plugin, const gc_info_t* gc_vars) {
    tra_plugin_t* tra = (tra_plugin_t*) plugin;

    tra_trace_enter_function(tra, "gc_sweep");

    tra->na_plugin->plugin_interface.gc_sweep((plugin_t*) tra->na_plugin, gc_vars);

    tra_trace_exit_function(tra, "gc_sweep");
}

/**
 * Called:   before a term GC (yices_garbage_collect), which recycles the ids of unmarked terms.
 * Modifies: marks the keys of attribute_term_variables (the analyzed terms, which every other
 *           attribute map and every cache uses as keys), of attribute_variable_terms (the terms
 *           of their V sets), and the input terms; clears nothing.
 * Ensures:  every term that the plugin keeps is marked.
 * Note:     the plugin never erases an entry of its attribute maps or of cache_abstractions, so the
 *           analyzed terms, and the memory of their entries, are kept for the life of the context.
 */
static void tra_plugin_gc_mark_and_clear(plugin_t* plugin) {
  tra_plugin_t* tra = (tra_plugin_t*) plugin;
  ptr_hmap_t* maps[2] = { &tra->attribute_term_variables, &tra->attribute_variable_terms };
  for (uint32_t i = 0; i < 2; i++) {
    for (ptr_hmap_pair_t* p = ptr_hmap_first_record(maps[i]); p != NULL; p = ptr_hmap_next_record(maps[i], p)) {
      term_table_set_gc_mark(tra->ctx->terms, index_of((term_t) p->key));
    }
  }
  for (uint32_t i = 0; i < tra->input_terms_list.size; i++) {
    term_table_set_gc_mark(tra->ctx->terms, index_of(tra->input_terms_list.data[i]));
  }
}

/* ===================
    Exception handler
   =================== */

//NOTE: At the moment we are just forwarding to the NA plugin
static void tra_plugin_set_exception_handler(plugin_t* plugin, jmp_buf* handler) {
    tra_plugin_t* tra = (tra_plugin_t*) plugin;

    tra_trace_enter_function(tra, "set_exception_handler");

    tra->na_plugin->plugin_interface.set_exception_handler((plugin_t*) tra->na_plugin, handler);
    
    tra_trace_exit_function(tra, "set_exception_handler");
}

/* ========
    Tracer
   ======== */

static void tra_plugin_set_tracer(plugin_t* plugin, tracer_t* tracer) {
  tra_plugin_t* tra = (tra_plugin_t*) plugin;

  ((masked_context_t*) tra->na_ctx)->ctx.tracer = tracer;

  if (tra->na_plugin->plugin_interface.set_tracer != NULL) {
    tra->na_plugin->plugin_interface.set_tracer((plugin_t*) tra->na_plugin, tracer);
  }
}

/* ====================
    Model construction
   ==================== */

// Precision for displaying the model values
#define TRA_MODEL_PREC 64

/*
 * Maps the subterms of t that are registered constants or applications of registered functions 
 * to their values.
 */
static bool tra_model_map_transcendental(tra_plugin_t* tra, evaluator_t* eval, term_t t, int_hmap_t* visited) {
  int_hmap_pair_t* seen = int_hmap_find(visited, t);
  if (seen != NULL) return seen->val;

  term_table_t* terms = tra->ctx->terms;
  bool is_app = is_pos_term(t) && term_kind(terms, t) == APP_TERM; // a negated application has one child
  bool found = false;
  for (uint32_t i = is_app ? 1 : 0; i < term_num_children(terms, t); i++) { // arg 0 is the function
    term_t child = term_ith_subterm(terms, t, i); // NULL_TERM for the constant of a sum
    if (child != NULL_TERM && tra_model_map_transcendental(tra, eval, child, visited)) found = true;
  }

  bool is_real = term_type_kind(terms, t) == REAL_TYPE;
  tra_func_plugin_t* fp = is_real ? tra_get_function_plugin_of_term(tra, t) : NULL;
  // The lookup goes by name, and a let can name any occurrence: in (let ((pi (not b))) ...),
  // the Boolean occurrence (not b) is named pi
  if (fp != NULL && (is_app || fp->arity == 0)) {
    found = true;
    value_table_t* vtbl = model_get_vtbl(eval->model);
    uint32_t n = fp->arity, k;
    mcsat_value_t args[n + 1];
    for (k = 0; k < n; k++) {
      value_t a = eval_in_model(eval, term_ith_subterm(terms, t, k + 1));
      if (a < 0) break;
      mcsat_value_construct_from_value(&args[k], vtbl, a);
    }

    tra_prec_t precision = tra_add_precisions(tra->ctx->options->delta_precision, 1);
    precision = tra_max_precisions(precision, TRA_MODEL_PREC);
    mcsat_value_t v;
    if (k == n && tra_func_approximate_value(fp, args, precision, &v)) {
      int_hmap_pair_t* entry = int_hmap_get(&eval->model->map, t);
      value_t t_value = mcsat_value_to_value(&v, terms->types, term_type(terms, t), vtbl);
      if (entry->val < 0) model_map_term(eval->model, t, t_value);
      else entry->val = t_value;
      mcsat_value_destruct(&v);
    }
    for (uint32_t i = 0; i < k; i++) mcsat_value_destruct(&args[i]);
  }

  int_hmap_add(visited, t, found);
  return found;
}

static void tra_plugin_build_model(plugin_t* plugin, model_t* model) {
  tra_plugin_t* tra = (tra_plugin_t*) plugin;
  const preprocessor_t* pre = tra->ctx->preprocessor;
  term_table_t* terms = tra->ctx->terms;

  evaluator_t eval;
  init_evaluator(&eval, model);
  int_hmap_t visited;
  init_int_hmap(&visited, 0);

  for (uint32_t i = 0; i < NUM_TRA_FUNCS; i++) {
    if (tra_registered_functions[i].symbol.arity != 0) continue;
    term_t c = _o_yices_get_term_by_name(tra_registered_functions[i].symbol.name);
    if (c != NULL_TERM) tra_model_map_transcendental(tra, &eval, c, &visited);
  }

  // t is the preprocessed form of the solved term, which contains no solved variable: a
  // variable is solved only if it was never met before
  for (uint32_t i = 0; i < pre->equalities_list.size; i++) {
    term_t x = int_hmap_find((int_hmap_t*) &pre->equalities, pre->equalities_list.data[i])->val;
    int_hmap_pair_t* x_pre = int_hmap_find((int_hmap_t*) &pre->preprocess_map, x);
    if (x_pre == NULL || x_pre->val == x || term_type_kind(terms, x) != REAL_TYPE
        || model_find_term_value(model, x) != null_value) continue;

    term_t t = tra_depurify_term(pre, x_pre->val);
    if (!tra_model_map_transcendental(tra, &eval, t, &visited)) continue;
    value_t x_value = eval_in_model(&eval, t);
    if (x_value >= 0) model_map_term(model, x, x_value);
  }

  delete_int_hmap(&visited);
  delete_evaluator(&eval);
}

/* ===========
    Allocator
   =========== */

plugin_t* tra_plugin_allocator(void) {
  tra_plugin_t* plugin = safe_malloc(sizeof(tra_plugin_t));
  
  plugin_construct((plugin_t*) plugin);

  plugin->plugin_interface.construct           = tra_plugin_construct;                        
  plugin->plugin_interface.destruct            = tra_plugin_destruct;                         
  plugin->plugin_interface.new_term_notify     = tra_plugin_new_term_notify;                  
  plugin->plugin_interface.new_lemma_notify    = tra_plugin_new_lemma_notify;
  plugin->plugin_interface.new_assertion_notify = tra_plugin_new_assertion_notify;
  plugin->plugin_interface.event_notify        = tra_plugin_event_notify;
  plugin->plugin_interface.propagate           = tra_plugin_propagate;                        
  plugin->plugin_interface.decide              = tra_plugin_decide;                              
  plugin->plugin_interface.decide_assignment   = tra_plugin_decide_assignment;
  plugin->plugin_interface.learn               = tra_plugin_learn;
  plugin->plugin_interface.get_conflict        = tra_plugin_get_conflict;                     
  plugin->plugin_interface.explain_propagation = tra_plugin_explain_propagation;
  plugin->plugin_interface.explain_evaluation  = tra_plugin_explain_evaluation;
  plugin->plugin_interface.simplify_conflict_literal = tra_plugin_simplify_conflict_literal;
  plugin->plugin_interface.push                = tra_plugin_push;
  plugin->plugin_interface.pop                 = tra_plugin_pop;                              
  plugin->plugin_interface.gc_mark             = tra_plugin_gc_mark;
  plugin->plugin_interface.gc_sweep            = tra_plugin_gc_sweep;
  plugin->plugin_interface.gc_mark_and_clear   = tra_plugin_gc_mark_and_clear;
  plugin->plugin_interface.set_exception_handler = tra_plugin_set_exception_handler;
  plugin->plugin_interface.set_tracer          = tra_plugin_set_tracer;
  plugin->plugin_interface.build_model         = tra_plugin_build_model;

  return (plugin_t*) plugin;
}
