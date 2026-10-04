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

#include "mcsat/tra/tra_plugin_internal.h"

#include <assert.h>
#include <stdbool.h>
#include <string.h>

#include "terms/terms.h"
#include "utils/memalloc.h"
#include "utils/ptr_hash_map.h"

// Find the registered function plugin with the given name or NULL if none
static tra_func_plugin_t* tra_get_function_plugin(const tra_plugin_t* tra, const char* name) {
  for (uint32_t i = 0; i < tra->num_tra_functions; i++) {
    if (strcmp(name, tra->tra_functions[i]->name) == 0) {
      return tra->tra_functions[i];
    }
  }
  return NULL;
}

/* Find the function plugin owning the term t. 
 * Returns NULL if t is not owned by any.
 */
tra_func_plugin_t* tra_get_function_plugin_of_term(const tra_plugin_t* tra, term_t t) {
  assert(is_pos_term(t));
  // A function symbol has the name of its plugin, but no plugin owns it
  assert(term_type_kind(tra->ctx->terms, t) != FUNCTION_TYPE);
  const char* fun_name = NULL;
  switch (term_kind(tra->ctx->terms, t)) {
  case UNINTERPRETED_TERM:
    fun_name = term_name(tra->ctx->terms, t);
    break;
  case APP_TERM:
    fun_name = term_name(tra->ctx->terms, composite_term_arg(tra->ctx->terms, t, 0));
    break;
  default:
    break;
  }
  tra_func_plugin_t* fp = fun_name == NULL ? NULL : tra_get_function_plugin(tra, fun_name);
  // The shape of t matches the arity of its owner: set-logic declares pi, sin and exp with their
  // signatures (other inputs are not supported, e.g. a native sin::(-> real real real))
  assert(fp == NULL || (term_kind(tra->ctx->terms, t) == APP_TERM ?
                        composite_term_arity(tra->ctx->terms, t) == (uint32_t) fp->arity + 1 : fp->arity == 0));
  return fp;
}

/* 
 * Get current cache entry for the abstraction of a term, 
 * or initialize an invalid one if it does not exist 
 */
tra_cache_abstraction_t* tra_get_cached_abstraction_entry(tra_plugin_t* tra, term_t t) {
 tra_ilib_t* lib = tra->abstract_domain;
 ptr_hmap_pair_t* cached = ptr_hmap_get(&tra->cache_abstractions, t);

  if (cached->val == NULL) {
    // Entry does not exist, allocate
    tra_cache_abstraction_t* e = safe_malloc(sizeof(tra_cache_abstraction_t));
    e->trail_trusted = false;
    e->trail_position = TV_INSIDE;
    e->valid          = false;
    e->interval       = lib->alloc();
    e->n_args = term_kind(tra->ctx->terms, t) == APP_TERM ? composite_term_arity(tra->ctx->terms, t) - 1 : 0;
    e->args   = e->n_args > 0 ? safe_malloc(e->n_args * sizeof(tra_itype_t)) : NULL;
    for (uint32_t i = 0; i < e->n_args; i++) e->args[i] = lib->alloc();
    cached->val = e;
  }

  return cached->val;
}
