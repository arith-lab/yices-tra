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
 * tra_func_plugin.h
 *
 * Interface (tra_func_plugin_t) of the function plugins. A function plugin implements one
 * transcendental function or constant f, such as exp, sin or pi, registered in tra_config.h.
 * The TRA plugin calls its methods; the plugin reads the TRA plugin only through the
 * accessors at the end of this file. A plugin lists its conflict strategies in a table
 * (tra_func_strategy_t); the strategy manager (tra_strategy_manager.h) chooses its profile
 * (tra_func_profile_t).
 *
 * Notation for the contracts below, in addition to that of tra_abstract_values.h.
 *  - self is the plugin whose method is called, f is its function, n = self->arity, and
 *    dom(f) ⊆ R^n is the domain of f. When n = 0, f is a constant c.
 *  - self owns a term t when t is f (n = 0) or an application f(x_1, ..., x_n) (n > 0).
 *  - args is an array of n elements of self->abstract_domain (NULL when n = 0), which is
 *    never modified. F(args) = { f(s) : s ∈ (γ(args[0]) × ... × γ(args[n-1])) ∩ dom(f) };
 *    in particular F(args) = {c} when n = 0.
 *  - Precision arguments are never INF_PREC.
 *  - Valid and unsatisfiable refer to the theory of the reals in which every registered
 *    symbol (pi, sin, exp, ...) has its standard meaning. A literal is true in the trail
 *    when it evaluates to true under the trail values.
 *  - Called: states when the TRA plugin calls a method.
 *  - Unmarked clauses are mandatory. Clauses marked (P) are progress requirements: the
 *    TRA plugin relies on them to make progress, but not to be sound.
 */

#ifndef MCSAT_TRA_FUNC_PLUGIN_H_
#define MCSAT_TRA_FUNC_PLUGIN_H_

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "yices_types.h"
#include "mcsat/mcsat_types.h"
#include "mcsat/plugin.h"
#include "utils/int_hash_sets.h"
#include "utils/int_vectors.h"

#include "mcsat/tra/tra_abstract_values.h"

/* The TRA plugin. Function plugins see it only through the accessors at the end of this file. */
struct tra_plugin_s;

struct tra_func_plugin_s;

/*
 * A conflict strategy of a function plugin: an entry of its table (strategies below). A strategy
 * decides to which terms it applies (e.g. atoms, or the applications that self owns); on any
 * other term, it returns false at once, without side effect. Hence the relative order of two
 * strategies in a profile matters only on the terms to which both apply.
 */
typedef struct {
  const char* name;        // unique among the strategies of the plugin, e.g. "taylor"
  const char* explanation; // what the strategy relates, and the size of its conflicts
  bool initially_enabled;  // advice to managers: false for a strategy to enable only later

  /**
   * Called:   for the terms t of a failed consistency check, when the strategy is in the
   *           profile of self; self need not own t.
   * Requires: t is positive and not of function type; out_conflict is empty.
   * Ensures:  result <==> out_conflict is not empty;
   *           result ==> the literals of out_conflict are true in the trail and their
   *                      conjunction is unsatisfiable.
   */
  bool (*get_conflict)(struct tra_func_plugin_s* self, term_t t, ivector_t* out_conflict);
} tra_func_strategy_t;

/*
 * A profile of a function plugin fp: the strategies that the TRA plugin tries on a term, in this
 * order, before fp->default_conflict.
 * Invariant: order has room for fp->num_strategies indices; order[0], ..., order[size-1] are
 *            pairwise distinct indices below fp->num_strategies.
 */
typedef struct {
  uint32_t* order;
  uint32_t size;
} tra_func_profile_t;

/* A function plugin: the interface of the TRA plugin to one registered function f. */
typedef struct tra_func_plugin_s {
  const char* name;         // the symbol of f, as registered in tra_config.h
  int arity;                // n, as registered in tra_config.h
  bool is_total_continuous; // true only if dom(f) = R^n and f is continuous

  /* The TRA plugin, read only through the accessors at the end of this file. */
  const struct tra_plugin_s* tra;

  /* The names of the plugins that self depends on (e.g. "pi" for sin). */
  const char** dependencies;
  int num_dependencies;
  struct tra_func_plugin_s** dependencies_plugins; // the plugins named in dependencies

  /**
   * Called:   once for each name in dependencies, with the registered plugin of that name,
   *           when the TRA plugin is constructed.
   */
  void (*add_dependency)(struct tra_func_plugin_s* self, struct tra_func_plugin_s* dependency);

  /**
   * Called:   when t is a new term owned by self, or by a plugin in dependencies (each time t
   *           gets a variable); possibly several times for the same t.
   * Ensures:  self uses prop only through prop->lemma, and every lemma that it passes is valid.
   *           If every argument of t is a rational constant or an uninterpreted term other than a
   *           purification variable, the lemma is kept as long as t has a variable (a definition
   *           lemma of t); otherwise, the pop of the scope that holds the lemma removes it.
   */
  void (*new_term_notify)(struct tra_func_plugin_s* self, term_t t, trail_token_t* prop);

  /* The abstract domain of the elements that the TRA plugin passes to self. */
  tra_ilib_t* abstract_domain;

  /**
   * Called:   once, when the TRA plugin is constructed, with the domain of the elements
   *           that the TRA plugin passes to self.
   * Ensures:  self->abstract_domain = abstract_domain.
   */
  void (*add_abstract_domain)(struct tra_func_plugin_s* self, tra_ilib_t* abstract_domain);

  /**
   * Ensures:  result ==> γ(args[0]) × ... × γ(args[n-1]) ⊆ dom(f);
   *           is_total_continuous ==> result.
   */
  bool (*is_in_domain) (struct tra_func_plugin_s*, tra_itype_t* args);

  /**
   * Called:   before t is decided, when self owns t and the decision is not mandatory.
   * Ensures:  result = NULL_TERM, to let NA decide t, or result is a term with a registered
   *           variable, which the TRA plugin hints as the next decision instead of t;
   *           (P):  result != NULL_TERM ==> result has no trail value, and the plugin
   *                 owning result, if any, returns NULL_TERM on it.
   */
  term_t (*let_na_decide)(struct tra_func_plugin_s* self, term_t t);

  /**
   * Called:   when an application t owned by self (n > 0) is notified, analyzed, or marked by
   *           the GC; the result depends on t only.
   * Ensures:  (P):  the terms added to vars are the terms, other than the arguments of t,
   *                 whose trail values the strategies and default_conflict of self need on t (see
   *                 tra_get_term_variables). The TRA plugin gives them variables when t gets
   *                 its variable, and keeps these variables as long as t has one.
   */
  void (*get_implicit_variables)(struct tra_func_plugin_s* self, term_t t, int_hset_t* vars);

  /**
   * Requires: t was returned by let_na_decide(self, ·); *value is unconstructed.
   * Ensures:  result ==> *value is constructed and of type VALUE_LIBPOLY (the TRA plugin
   *                      hints it as the value of t);
   *           !result ==> *value is unconstructed.
   */
  bool (*hint_argument_value)(struct tra_func_plugin_s* self, term_t t, mcsat_value_t* value);

  /**
   * Ensures:  F(args) ⊆ γ(out);
   *           (P):  w(F(args)) <= 2^-(precision+1) ==> p(out) >= precision.
   */
  void (*eval)(struct tra_func_plugin_s* self, tra_itype_t* args, tra_prec_t precision, tra_itype_t out);

  /**
   * Requires: is_in_domain(self, args); precision != INF_PREC.
   * Ensures:  result is a precision other than INF_PREC;
   *           precision = NO_PREC ==> result = NO_PREC;
   *           (P):  result != NO_PREC ==> w(F(J)) <= 2^-(precision+1) for every array J
   *                 of n elements such that γ(J[i]) ⊆ γ(args[i]) and w(γ(J[i])) <= 2^-result.
   */
  tra_prec_t (*refine_precision)(struct tra_func_plugin_s* self, tra_itype_t* args, tra_prec_t precision);

  /* The conflict strategies of self: a static table of num_strategies entries (NULL when
   * num_strategies = 0). */
  const tra_func_strategy_t* strategies;
  uint32_t num_strategies;

  /* The profile of self: allocated empty and freed by the TRA plugin, and written only by
   * tra_func_set_strategy_profile (called by the strategy manager, see tra_strategy_manager.h). */
  tra_func_profile_t profile;

  /**
   * The default conflict of self.
   * Called:   for the terms t of a failed consistency check, when no strategy of self->profile
   *           returned true on t; self need not own t.
   * Requires, Ensures: as get_conflict of tra_func_strategy_t, and
   *           (P):  out_conflict is not empty if self owns t, t has a trail value v ∉ γ(I),
   *                 and every argument x_i of t has a trail value v_i ∈ γ(I_i), where
   *                 I = tra_get_abstraction(self->tra, t) is not NULL and
   *                 I_i = tra_get_argument_abstraction(self->tra, t, i-1).
   *
   * Example: let the trail be x -> 3, (exp x) -> 7, (sin (exp x)) -> 0.7, with I(x) = [3, 3],
   * I(exp x) = [20, 21] and I(sin (exp x)) = [0.2, 0.5]. For t = (sin (exp x)), the sin
   * plugin may leave out_conflict empty, as 7 ∉ [20, 21]. For t = (exp x), the exp plugin
   * must add a conflict, as 3 ∈ [3, 3] and 7 ∉ [20, 21]. For instance
   * [3 <= x, x <= 3, (exp x) < 20]: its literals are true in the trail, and their conjunction
   * is unsatisfiable, as exp(3) > 20.
   */
  bool (*default_conflict)(const struct tra_func_plugin_s* self, term_t t, ivector_t* out_conflict);

  /**
   * Called:   once, when the TRA plugin is destructed.
   * Ensures:  the memory of self, and of what self owns, is released.
   */
  void (*destruct) (struct tra_func_plugin_s* self);

} tra_func_plugin_t;

/* ================================================
    Default implementations of some methods above
   ================================================ */

// See tra_conflict_utils.h for implementations of default_conflict.

/**
 * Ensures:  self->abstract_domain = domain.
 */
static inline void tra_func_default_add_abstract_domain(tra_func_plugin_t* self, tra_ilib_t* domain) {
  self->abstract_domain = domain;
}

/**
 * Ensures:  result = true. This meets the contract of is_in_domain when dom(f) = R^n.
 */
static inline bool tra_func_default_is_in_domain(tra_func_plugin_t* self, tra_itype_t* args) {
  return true;
}

/**
 * Ensures:  result = NULL_TERM.
 */
static inline term_t tra_func_default_let_na_decide(tra_func_plugin_t* self, term_t t) {
  return NULL_TERM;
}

/**
 * Ensures:  vars is unchanged. This meets the contract of get_implicit_variables when
 *           the strategies and default_conflict need only the trail values of the arguments.
 */
static inline void tra_func_default_get_implicit_variables(tra_func_plugin_t* self, term_t t, int_hset_t* vars) {
}

/**
 * Ensures:  result = false.
 */
static inline bool tra_func_default_hint_argument_value(tra_func_plugin_t* self, term_t t, mcsat_value_t* value) {
  return false;
}

/**
 * Ensures:  result = tra_add_precisions(precision, 1). This meets the contract of
 *           refine_precision when n = 0, or when n = 1 and |f(s) - f(s')| <= |s - s'|
 *           for all s, s' ∈ dom(f).
 */
static inline tra_prec_t tra_func_default_refine_precision(tra_func_plugin_t* self, tra_itype_t* args, tra_prec_t precision) {
  assert(precision != INF_PREC);
  return tra_add_precisions(precision, 1);
}

/**
 * Requires: false, i.e. num_dependencies = 0, so that the TRA plugin never calls it.
 */
static inline void tra_func_default_add_dependency(tra_func_plugin_t* self, tra_func_plugin_t* dependency) {
  // This should never be called.
  assert(false);
}

/* ===========
    Utilities
   =========== */

/**
 * Requires: t is positive and not of function type.
 * Ensures:  result <==> fp owns t.
 */
bool tra_application_is_of_the_plugin(const tra_func_plugin_t* fp, term_t t);

/**
 * Requires: args holds fp->arity real values (NULL when fp->arity = 0); v is unconstructed.
 * Ensures:  result ==> v is constructed, and v is a rational, the midpoint of an interval that
 *           contains f(args);
 *           (A): result ==> v is within 2^-precision of f(args), unless tra_interval_to_mpfr
 *                stopped at 2^20 bits: then no bound on |v - f(args)| is guaranteed;
 *           !result ==> v is unconstructed.
 */
bool tra_func_approximate_value(const tra_func_plugin_t* fp, const mcsat_value_t* args, tra_prec_t precision,
                                mcsat_value_t* v);

/**
 * Requires: order holds n elements; order may be NULL if n = 0, and may point into fp->profile.order.
 * Modifies: fp->profile.
 * Ensures:  result <==> order[0], ..., order[n-1] are pairwise distinct indices below
 *           fp->num_strategies;
 *           result ==> fp->profile is order[0], ..., order[n-1];
 *           !result ==> fp->profile is unchanged.
 */
bool tra_func_set_strategy_profile(tra_func_plugin_t* fp, const uint32_t* order, uint32_t n);

/* ====================================
    Read-only access to the TRA plugin
   ==================================== */

/*
 * In the contracts below, I(t) is the cached abstraction of t: the element that the last
 * consistency check computed for t, unless it was invalidated since. The elements and sets
 * returned are read-only, and the accessors do not modify the TRA plugin.
 *
 * Guarantee: if a plugin owns t and I(t) is defined, then the trail value of t lies in
 * γ(I(t)), or F(I_1, ..., I_n) ⊆ γ(I(t)) for I_i = tra_get_argument_abstraction(tra, t, i-1).
 * This makes the conflicts built from these elements sound (see tra_conflict_utils.h).
 */

/**
 * Ensures:  result is the plugin context of the TRA plugin.
 */
const plugin_context_t* tra_get_context(const struct tra_plugin_s* tra);

/**
 * Ensures:  result = I(t) if I(t) is defined, and result = NULL otherwise.
 */
tra_itype_t tra_get_abstraction(const struct tra_plugin_s* tra, term_t t);

/**
 * Requires: t is an application with more than i arguments.
 * Ensures:  result is the element of the i-th argument of t (from 0) from which I(t) was
 *           computed if I(t) is defined, and result = NULL otherwise.
 */
tra_itype_t tra_get_argument_abstraction(const struct tra_plugin_s* tra, term_t t, uint32_t i);

/**
 * Ensures:  result = NULL if t was never analyzed (see tra_analyze_term). Otherwise, result
 *           is the closed set V(t) of the variables, atoms, applications, divisions and
 *           if-then-elses occurring in t (t included, when it is one of them), and of the
 *           implicit variables of its applications (see get_implicit_variables). The TRA
 *           plugin checks t only when every term of V(t) has a registered variable with a
 *           trail value.
 */
const int_hset_t* tra_get_term_variables(const struct tra_plugin_s* tra, term_t t);

#endif /* MCSAT_TRA_FUNC_PLUGIN_H_ */
