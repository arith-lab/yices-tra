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

#include "mcsat/tra/tra_analyze_term.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <gmp.h>

#include "api/yices_api_lock_free.h"
#include "mcsat/plugin.h"
#include "terms/rationals.h"
#include "terms/term_explorer.h"
#include "terms/terms.h"
#include "terms/types.h"
#include "utils/int_hash_map.h"
#include "utils/int_hash_sets.h"
#include "utils/memalloc.h"
#include "utils/ptr_hash_map.h"

#include "mcsat/tra/tra_abstract_values.h"
#include "mcsat/tra/tra_func_plugin.h"
#include "mcsat/tra/tra_plugin_internal.h"
#include "mcsat/tra/utils/tra_term_explorer.h"
#include "mcsat/tra/utils/tra_tracing.h"

/**
 * Requires: child is a purified arithmetic or Boolean term.
 * Modifies: child, using tra_analyze_term.
 * Ensures:  the attributes of child are folded into those accumulated for its parent:
 *           *is_transcendental is or-ed with attribute_transcendental(child),
 *           *is_total_continuous and *is_constant are and-ed with
 *           attribute_total_continuous(child) and attribute_constant(child), and
 *           V(child) ⊆ my_vars.
 */
static void tra_analyze_child(tra_plugin_t* tra, term_t child,
                              bool* is_transcendental, bool* is_total_continuous, bool* is_constant,
                              int_hset_t* my_vars) {
  tra_analyze_term(tra, child);

  *is_transcendental    = *is_transcendental    || int_hmap_get(&tra->attribute_transcendental, child)->val;
  *is_total_continuous  = *is_total_continuous  && int_hmap_get(&tra->attribute_total_continuous, child)->val;
  *is_constant          = *is_constant          && int_hmap_get(&tra->attribute_constant, child)->val;

  ptr_hmap_pair_t* vs_child = ptr_hmap_get(&tra->attribute_term_variables, child);
  assert(vs_child->val != NULL);

  int_hset_t* child_vars = (int_hset_t*) vs_child->val;
  assert(int_hset_is_closed(child_vars));

  for (uint32_t j = 0; j < child_vars->nelems; j++) {
    int_hset_add(my_vars, child_vars->data[j]);
  }
}

/* ===========================
    Analysis of Boolean terms
   =========================== */

/*
 * The range of a Boolean term encodes its truth value: {1} when surely true,
 * {-1} when surely false, [-1, 1] (the join of both) when unknown. In the
 * helpers below a truth value is an int: 1, -1 or 0 for unknown. The encoding is
 * chosen so that the truth value of a Boolean term is that of its range >= 0 (see
 * tra_get_atom_truth): {1} is positive, {-1} is negative.
 */
static void tra_bool_range_set(tra_ilib_t* lib, tra_itype_t r, int truth) {
  if (truth != 0) {
    lib->set_z(r, truth);
  } else {
    tra_itype_t minus_one = lib->alloc();
    lib->set_z(minus_one, -1);
    lib->set_z(r, 1);
    lib->join(r, r, minus_one, NO_PREC);
    lib->free(minus_one);
  }
}

// The range stored for the (already analyzed) term t
static tra_itype_t tra_range_of(tra_plugin_t* tra, term_t t) {
  ptr_hmap_pair_t* r = ptr_hmap_get(&tra->attribute_range, t);
  assert(r->val != NULL);
  return (tra_itype_t) r->val;
}

/*
 * Analysis of a Boolean term (atoms, connectives, guards of if-then-elses).
 * The range encodes the truth value known statically, see tra_bool_range_set.
 */
static void tra_analyze_boolean_term(tra_plugin_t* tra, term_t t,
                                     bool* is_transcendental, bool* is_total_continuous, bool* is_constant,
                                     int_hset_t* my_vars, tra_itype_t my_range) {
  term_table_t* terms = tra->ctx->terms;
  tra_ilib_t* lib = tra->abstract_domain;
  assert(is_boolean_term(terms, t));

  *is_total_continuous = false;

  // Negative literal: the attributes are those of the atom
  if (is_neg_term(t)) {
    term_t atom = unsigned_term(t);
    tra_analyze_child(tra, atom, is_transcendental, is_total_continuous, is_constant, my_vars);
    tra_bool_range_set(lib, my_range, -tra_get_atom_truth(lib, tra_range_of(tra, atom), false));
    return;
  }

  switch (term_kind(terms, t)) {

    case CONSTANT_TERM:
      // true / false: no variables, constant
      assert(t == true_term || t == false_term);
      tra_bool_range_set(lib, my_range, t == true_term ? 1 : -1);
      return;

    case UNINTERPRETED_TERM: {
      // Boolean variable: a variable itself. It may be a purification variable
      // standing for a Boolean term: unwind one step and inherit from that term.
      int_hset_add(my_vars, t);
      term_t t_step = tra_depurify_term_step(tra->ctx->preprocessor, t);
      if (t_step != t) {
        tra_analyze_child(tra, t_step, is_transcendental, is_total_continuous, is_constant, my_vars);
        tra_bool_range_set(lib, my_range, tra_get_atom_truth(lib, tra_range_of(tra, t_step), false));
      } else {
        *is_constant = false;
        tra_bool_range_set(lib, my_range, 0);
      }
      return;
    }

    // Atoms are variables themselves, on top of the variables of their arguments
    case ARITH_EQ_ATOM:
    case ARITH_GE_ATOM: {
      int_hset_add(my_vars, t);
      term_t arg = arith_atom_arg(terms, t);
      tra_analyze_child(tra, arg, is_transcendental, is_total_continuous, is_constant, my_vars);

      bool eq = term_kind(terms, t) == ARITH_EQ_ATOM;
      tra_bool_range_set(lib, my_range, tra_get_atom_truth(lib, tra_range_of(tra, arg), eq));
      return;
    }

    case ARITH_BINEQ_ATOM: {
      int_hset_add(my_vars, t);
      composite_term_t* eq = arith_bineq_atom_desc(terms, t);
      assert(eq->arity == 2);

      // t1 = t2 is analyzed as t1 - t2 = 0
      term_t diff = _o_yices_sub(eq->arg[0], eq->arg[1]);
      tra_analyze_child(tra, diff, is_transcendental, is_total_continuous, is_constant, my_vars);

      // Equality rule on the range of the difference
      tra_bool_range_set(lib, my_range, tra_get_atom_truth(lib, tra_range_of(tra, diff), true));
      return;
    }

    case OR_TERM:
    case XOR_TERM:
    case EQ_TERM:       // equality between Boolean terms
    case ITE_TERM:      // Boolean if-then-else
    case ITE_SPECIAL: {
      // Connectives: the variables of the children, without themselves.
      // The truth value follows three-valued logic on the children.
      composite_term_t* d = composite_term_desc(terms, t);
      uint32_t n = d->arity;
      for (uint32_t i = 0; i < n; i++) {
        tra_analyze_child(tra, d->arg[i], is_transcendental, is_total_continuous, is_constant, my_vars);
      }

      int truth = 0;
      switch (term_kind(terms, t)) {
        case OR_TERM: {
          truth = -1;                       // false until a child is true or unknown
          for (uint32_t i = 0; i < n; i++) {
            int c = tra_get_atom_truth(lib, tra_range_of(tra, d->arg[i]), false);
            if (c == 1) { truth = 1; break; }
            if (c == 0) truth = 0;
          }
          break;
        }
        case XOR_TERM: {
          truth = -1;                       // parity of the true children
          for (uint32_t i = 0; i < n; i++) {
            int c = tra_get_atom_truth(lib, tra_range_of(tra, d->arg[i]), false);
            if (c == 0) { truth = 0; break; }
            if (c == 1) truth = -truth;
          }
          break;
        }
        case EQ_TERM: {
          assert(n == 2);
          int a = tra_get_atom_truth(lib, tra_range_of(tra, d->arg[0]), false);
          int b = tra_get_atom_truth(lib, tra_range_of(tra, d->arg[1]), false);
          truth = (a != 0 && b != 0) ? (a == b ? 1 : -1) : 0;
          break;
        }
        default: {                          // ITE_TERM, ITE_SPECIAL
          assert(n == 3);
          int g = tra_get_atom_truth(lib, tra_range_of(tra, d->arg[0]), false);
          int a = tra_get_atom_truth(lib, tra_range_of(tra, d->arg[1]), false);
          int b = tra_get_atom_truth(lib, tra_range_of(tra, d->arg[2]), false);
          truth = (g == 1) ? a : (g == -1) ? b : (a == b ? a : 0);
          break;
        }
      }
      tra_bool_range_set(lib, my_range, truth);
      return;
    }

    default:
      // TODO: other Boolean terms (distinct, uninterpreted predicates, ...)
      assert(false); // unreachable
      return;
  }
}

/* ===============================
    Analysis of Real-valued terms
   =============================== */

/**
 * Requires: t is a purified arithmetic (real or integer) term; *is_transcendental = false,
 *           *is_total_continuous = *is_constant = true, and γ(my_range) = R, where my_range
 *           is the entry of t in attribute_range.
 * Ensures:  the accumulators and my_range hold the attributes of t.
 */
static void tra_analyze_real_term(tra_plugin_t* tra, term_t t,
                                  bool* is_transcendental, bool* is_total_continuous, bool* is_constant,
                                  int_hset_t* my_vars, tra_itype_t my_range) {
  term_table_t* terms = tra->ctx->terms;
  tra_ilib_t* lib = tra->abstract_domain;

  // Check if term is transcendental
  tra_func_plugin_t* fp = tra_get_function_plugin_of_term(tra, t);
  *is_transcendental = fp != NULL;
  if(*is_transcendental) { 
    *is_total_continuous = fp->is_total_continuous;
    if (fp->arity == 0) fp->eval(fp, NULL, NO_PREC, my_range);
  }

  switch (term_kind(terms, t)) {

    case ARITH_CONSTANT: {
      // Exact rational constant
      rational_t* c = rational_term_desc(terms, t);
      mpq_t q;
      mpq_init(q);
      q_get_mpq(c, q);
      lib->set_mpz_div(my_range, mpq_numref(q), mpq_denref(q), NO_PREC);
      mpq_clear(q);
      return;
    }

    case UNINTERPRETED_TERM: {
      // Variable, or zero-arity transcendental constant: a variable itself.
      // A purification variable inherits from the term it stands for.
      int_hset_add(my_vars, t);

      term_t t_step = tra_depurify_term_step(tra->ctx->preprocessor, t);
      if (t_step != t) {
        // A purification variable has no name, so no plugin owns it and the accumulators
        // still hold their initial values (Requires): folding t_step into them sets them to
        // the attributes of t_step
        assert(fp == NULL && !*is_transcendental && *is_total_continuous && *is_constant);
        tra_analyze_child(tra, t_step, is_transcendental, is_total_continuous, is_constant, my_vars);
        lib->set(my_range, tra_range_of(tra, t_step));
      } else if (!*is_transcendental) {
        // A genuine variable
        *is_constant = false;
        *is_total_continuous = true;
      } // else a transcendental constant: nothing more to do
      return;
    }

    case APP_TERM: {
      // Applications of uninterpreted functions other than the registered ones are not
      // supported: the consistency check could not evaluate them
      assert(fp != NULL);

      // An application is itself a variable the plugin may be asked to decide.
      // arg[0] is the function, the arguments follow.
      int_hset_add(my_vars, t);

      composite_term_t* app = app_term_desc(terms, t);
      uint32_t n_args = app->arity - 1;
      for (uint32_t i = 0; i < n_args; i++) {
        tra_analyze_child(tra, app->arg[i + 1], is_transcendental, is_total_continuous, is_constant, my_vars);
      }

      // Add implicit variables and evaluate the arguments
      if (fp != NULL) {
        assert(fp->arity == (int) n_args);
        assert(fp->arity > 0);

        fp->get_implicit_variables(fp, t, my_vars);

        tra_itype_t* args = (tra_itype_t*) safe_malloc(n_args * sizeof(tra_itype_t));
        for (uint32_t i = 0; i < n_args; i++) {
          args[i] = tra_range_of(tra, app->arg[i + 1]); // borrowed, owned by attribute_range
        }
        fp->eval(fp, args, NO_PREC, my_range);
        safe_free(args);
      }

      // TODO: Would be nice to de-comment the next lines. 
      //   A total-continuous, constant application has a unique value, so in principle 
      //   we do not need values for its children in order to do a proper 
      //   consistency check / conflict analysis. However, we must then have 
      //   plugin to generate conflicts that are smart enough.
      //   See matching comment in tra_extract_conflicts_logic.
      //
      // if (*is_total_continuous && *is_constant) {
      //   int_hset_reset(my_vars);
      //   int_hset_add(my_vars, t);
      // }
      return;
    }

    case ARITH_RDIV: {
      // A division is a variable the plugin may be asked to decide, and it is
      // partial: the denominator may be zero.
      int_hset_add(my_vars, t);
      *is_total_continuous = false;

      composite_term_t* rdiv = arith_rdiv_term_desc(terms, t);
      assert(rdiv->arity == 2);
      term_t num = rdiv->arg[0];
      term_t den = rdiv->arg[1];
      tra_analyze_child(tra, num, is_transcendental, is_total_continuous, is_constant, my_vars);
      tra_analyze_child(tra, den, is_transcendental, is_total_continuous, is_constant, my_vars);

      // Range: refined only when the denominator is guaranteed nonzero
      tra_itype_t r_den = tra_range_of(tra, den);
      if (!lib->zero_in(r_den)) {
        lib->div(my_range, tra_range_of(tra, num), r_den, NO_PREC);
      }
      return;
    }

    case ARITH_ABS: {
      // Unreachable in practice: the preprocessor rewrites abs into an ite
      term_t arg = arith_abs_arg(terms, t);
      tra_analyze_child(tra, arg, is_transcendental, is_total_continuous, is_constant, my_vars);

      lib->abs(my_range, tra_range_of(tra, arg));
      return;
    }

    case ITE_TERM:
    case ITE_SPECIAL: {
      // ite(a, b, c): the ite plugin makes it a variable the plugin may be asked
      // to decide. It is total but discontinuous at the guard.
      int_hset_add(my_vars, t);
      *is_total_continuous = false;

      composite_term_t* ite = ite_term_desc(terms, t);
      assert(ite->arity == 3);
      term_t guard = ite->arg[0];
      term_t t_true = ite->arg[1];
      term_t t_false = ite->arg[2];

      // The guard is Boolean: tra_analyze_term dispatches it to tra_analyze_boolean_term
      tra_analyze_child(tra, guard, is_transcendental, is_total_continuous, is_constant, my_vars);
      tra_analyze_child(tra, t_true, is_transcendental, is_total_continuous, is_constant, my_vars);
      tra_analyze_child(tra, t_false, is_transcendental, is_total_continuous, is_constant, my_vars);

      // Range: the taken branch when the guard is decided statically, else the join
      int guard_truth = tra_get_atom_truth(lib, tra_range_of(tra, guard), false);
      if (guard_truth == 1) {
        lib->set(my_range, tra_range_of(tra, t_true));
      } else if (guard_truth == -1) {
        lib->set(my_range, tra_range_of(tra, t_false));
      } else {
        lib->join(my_range, tra_range_of(tra, t_true), tra_range_of(tra, t_false), NO_PREC);
      }
      return;
    }

    case ARITH_POLY: {
      // Sum c_0 * m_0 + ... + c_{n-1} * m_{n-1}; a NULL_TERM monomial is the constant term
      mpq_t coeff;
      term_t child;
      mpq_init(coeff);

      uint32_t n = term_num_children(terms, t);

      // Range: the sum of the coefficients times the ranges of the monomials
      lib->set_z(my_range, 0);
      tra_itype_t coeff_range = lib->alloc();
      tra_itype_t term_range = lib->alloc();

      for (uint32_t i = 0; i < n; i++) {
        sum_term_component(terms, t, i, coeff, &child);
        lib->set_mpz_div(coeff_range, mpq_numref(coeff), mpq_denref(coeff), NO_PREC);

        if (child != NULL_TERM) {
          tra_analyze_child(tra, child, is_transcendental, is_total_continuous, is_constant, my_vars);

          lib->mul(term_range, coeff_range, tra_range_of(tra, child), NO_PREC);
        } else {
          lib->set(term_range, coeff_range);
        }
        lib->add(my_range, my_range, term_range, NO_PREC);
      }

      lib->free(coeff_range);
      lib->free(term_range);
      mpq_clear(coeff);
      return;
    }

    case POWER_PRODUCT: {
      // Product t_0^e_0 * ... * t_{n-1}^e_{n-1}
      uint32_t n = term_num_children(terms, t);
      term_t child;
      uint32_t exp;

      // Range: the product of the powers of the ranges of the factors
      lib->set_z(my_range, 1);
      tra_itype_t factor_range = lib->alloc();

      for (uint32_t i = 0; i < n; i++) {
        product_term_component(terms, t, i, &child, &exp);
        if (child != NULL_TERM) {
          tra_analyze_child(tra, child, is_transcendental, is_total_continuous, is_constant, my_vars);

          lib->pow_ui(factor_range, tra_range_of(tra, child), exp, NO_PREC);
          lib->mul(my_range, my_range, factor_range, NO_PREC);
        }
      }

      lib->free(factor_range);
      return;
    }

    default:
      // SELECT_TERM, ARITH_FLOOR, ARITH_CEIL, ARITH_IDIV, ARITH_MOD and anything else
      assert(false); // unreachable
      return;
  }
}

/* ================================
    Analysis of terms (dispatcher)
   ================================ */

/* 
 * Fills attributes of a term (attribute_term_variables, attribute_transcendental,
 * attribute_total_continuous, attribute_constant, attribute_range and 
 * attribute_variable_terms). The term is assumed to be purified.
 * Boolean terms (atoms, connectives, guards of if-then-elses) get the same
 * attributes; their range encodes the statically known truth value, see
 * tra_bool_range_set.
 */
void tra_analyze_term(tra_plugin_t* tra, term_t t) {
  if (int_hmap_find(&tra->attribute_transcendental, t) != NULL) {
    // term already analyzed
    assert(ptr_hmap_find(&tra->attribute_term_variables, t) != NULL);
    return;
  }
  assert(ptr_hmap_find(&tra->attribute_term_variables, t) == NULL);

  if (tra_trace_enabled(tra, "tra::analyze_term")) {
    tra_trace_enter_function(tra, "analyze_term");
    tra_trace_print_term_infos(tra, t);
  }

  term_table_t* terms = tra->ctx->terms;
  tra_ilib_t* lib = tra->abstract_domain;

  // term initialized as not transcendental and with no variables.
  // NOTE: we deliberately do not store the returned pair pointer in a local variable: 
  // recursive calls to tra_analyze_term below can insert new entries into
  // attribute_transcendental and trigger a rehash, which might leave the returned 
  // pointer dangling. (Just call int_hmap_get again instead.)
  bool is_transcendental = false;

  ptr_hmap_pair_t* term_vars = ptr_hmap_get(&tra->attribute_term_variables, t);
  assert(term_vars->val == NULL);
  term_vars->val = safe_malloc(sizeof(int_hset_t));
  init_int_hset((int_hset_t*) term_vars->val, 0);
  int_hset_t* my_vars = (int_hset_t*) term_vars->val;

  // Term initialized as total-continuous 
  bool is_total_continuous = true;

  // Also, term is initialized as constant
  // NOTE: above by "variables" we mean any term that the plugin can be asked to 
  // decide a value for (this includes function applications). 
  // For instance, (sin (exp 16)) is a constant, but it has variables, e.g. (exp 16). 
  bool is_constant = true;

  // The range is initialized to R, i.e. no information yet
  tra_itype_t my_range = lib->alloc();
  lib->set_R(my_range);
  ptr_hmap_get(&tra->attribute_range, t)->val = my_range;

  switch (term_type_kind(terms, t)) {
    case BOOL_TYPE:
      tra_analyze_boolean_term(tra, t, &is_transcendental, &is_total_continuous, &is_constant, my_vars, my_range);
      break;
    case REAL_TYPE:
    case INT_TYPE:
      tra_analyze_real_term(tra, t, &is_transcendental, &is_total_continuous, &is_constant, my_vars, my_range);
      break;
    default:
      // Only Boolean and arithmetic terms are supported
      assert(false); // unreachable
      break;
  }

  int_hmap_get(&tra->attribute_transcendental, t)->val = is_transcendental;
  int_hmap_get(&tra->attribute_total_continuous, t)->val = is_total_continuous;
  int_hmap_get(&tra->attribute_constant, t)->val = is_constant;

  int_hset_close(my_vars);

  // Populate attribute_variable_terms, the reverse of attribute_term_variables
  for (uint32_t i = 0; i < my_vars->nelems; i++) {
    term_t vt = (term_t) my_vars->data[i];

    ptr_hmap_pair_t* vt_pair = ptr_hmap_get(&tra->attribute_variable_terms, vt);
    if (vt_pair->val == NULL) {
      vt_pair->val = safe_malloc(sizeof(int_hset_t));
      init_int_hset((int_hset_t*) vt_pair->val, 0);
    }
    bool added = int_hset_add((int_hset_t*) vt_pair->val, (uint32_t) t);

    if (tra_trace_enabled(tra, "tra::analyze_term")) {
      tra_trace_iprintf(tra, "[attribute_variable_terms: ");
      tra_trace_print_term(tra, vt);
      tra_trace_printf(tra, " -> ");
      tra_trace_print_term(tra, t);
      tra_trace_printf(tra, "%s]\n", added ? "" : " (already present)");
    }
  }

  if (tra_trace_enabled(tra, "tra::analyze_term")) {
    tra_trace_print_term_infos(tra, t);
    tra_trace_iprintf(tra, "[transcendental: %s]\n", is_transcendental ? "true" : "false");
    tra_trace_iprintf(tra, "[total_continuous: %s]\n", is_total_continuous ? "true" : "false");
    tra_trace_iprintf(tra, "[constant: %s]\n", is_constant ? "true" : "false");
    tra_trace_iprintf(tra, "[variables: ");
    for (uint32_t i = 0; i < my_vars->nelems; i++) {
      if (i) tra_trace_printf(tra, ", ");
      tra_trace_print_term(tra, (term_t) my_vars->data[i]);
    }
    tra_trace_printf(tra, "]\n");
    tra_trace_iprintf(tra, "[range: ");
    tra_trace_print_interval(tra, my_range, 15);
    tra_trace_printf(tra, "]\n");
    tra_trace_exit_function(tra, "analyze_term");
  }

  return;
}

bool tra_term_is_analyzable(tra_plugin_t* tra, term_t t) {
  term_table_t* terms = tra->ctx->terms;
  if (is_neg_term(t)) return true;
  term_kind_t k = term_kind(terms, t);
  switch (term_type_kind(terms, t)) {
    case BOOL_TYPE:
      return k == CONSTANT_TERM || k == UNINTERPRETED_TERM || k == ARITH_EQ_ATOM
          || k == ARITH_GE_ATOM || k == ARITH_BINEQ_ATOM || k == OR_TERM || k == XOR_TERM
          || k == EQ_TERM || k == ITE_TERM || k == ITE_SPECIAL;
    case REAL_TYPE:
    case INT_TYPE:
      return k == ARITH_CONSTANT || k == UNINTERPRETED_TERM || k == ARITH_RDIV || k == ARITH_ABS
          || k == ITE_TERM || k == ITE_SPECIAL || k == ARITH_POLY || k == POWER_PRODUCT
          || (k == APP_TERM && tra_get_function_plugin_of_term(tra, t) != NULL);
    default:
      return false;
  }
}
