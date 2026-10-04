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

#include "mcsat/tra/tra_consistency.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <gmp.h>
#include <mpfr.h>

#include "api/yices_api_lock_free.h"
#include "mcsat/mcsat_types.h"
#include "mcsat/plugin.h"
#include "mcsat/trail.h"
#include "mcsat/value.h"
#include "mcsat/variable_db.h"
#include "terms/rationals.h"
#include "terms/term_explorer.h"
#include "terms/terms.h"
#include "utils/bit_tricks.h"
#include "utils/int_hash_map.h"
#include "utils/int_hash_sets.h"
#include "utils/int_vectors.h"
#include "utils/memalloc.h"
#include "utils/ptr_hash_map.h"

#include "mcsat/tra/tra_abstract_values.h"
#include "mcsat/tra/tra_conflict_utils.h"
#include "mcsat/tra/tra_func_plugin.h"
#include "mcsat/tra/tra_plugin_internal.h"
#include "mcsat/tra/utils/tra_exact_values.h"
#include "mcsat/tra/utils/tra_term_explorer.h"
#include "mcsat/tra/utils/tra_tracing.h"

// Initial precision of the mpfr variables below; tra_abstraction_magnitude_bits works at it
#define TRA_MPFR_PREC_INIT  64

/* ===========================
    Trail values abstractions
   =========================== */

static tra_cache_abstraction_t* compute_abstraction(tra_plugin_t* tra, term_t t, tra_prec_t precision);

/* Result of the consistency check of an atom: the verdict, together with what
 * the abstraction J of the argument decides by itself (truth): 1 when J makes
 * the atom surely true, -1 surely false, 0 undecided. */
typedef struct {
  tra_cc_verdict_t verdict;
  int truth;
} tra_cc_result_t;

/* Truth value of a Boolean term under the current trail, as far as the
 * abstractions decide it. TRA_GUARD_DELTA means: decided only by trusting the
 * trail value of an atom that is delta-consistent. */
typedef enum {
  TRA_GUARD_TRUE,
  TRA_GUARD_FALSE,
  TRA_GUARD_DELTA,
  TRA_GUARD_UNKNOWN,
} tra_guard_value_t;

// Where t's trail value sits relative to 'interval'; TV_INSIDE if t has no trail value.
static tra_tv_position_t tra_trail_position_vs_interval(tra_plugin_t* tra, term_t t, tra_itype_t interval) {
  mcsat_value_t v;
  if (!term_evaluate(tra->ctx, t, &v)) return TV_INSIDE;

  tra_tv_position_t position = tra->abstract_domain->trail_value_position(interval, &v);
  mcsat_value_destruct(&v);

  if (tra_trace_enabled(tra, "tra::trail_vs_interval")) {
    tra_trace_iprintf(tra, "[trail vs interval: ");
    tra_trace_print_term(tra, t);
    tra_trace_printf(tra, " in position %s]\n", tra_tv_position_to_string(position));
  }

  return position;
}

// Build an interval around the value a term takes in the trail
static void interval_around_trail_value(tra_plugin_t* tra, term_t t, tra_prec_t precision, tra_cache_abstraction_t* out) {
  mcsat_value_t val;
  bool ok = term_evaluate(tra->ctx, t, &val);
  assert(ok);

  out->valid = true;
  out->trail_trusted = false;

  ok = tra_interval_around_value(tra->abstract_domain, &val, precision, out->interval);
  assert(ok); // a real variable only holds rational or libpoly values
  (void) ok;
  mcsat_value_destruct(&val);
}

/*
 * Evaluates an UNINTERPRETED_TERM into 'out': a transcendental constant (zero-arity
 * function), a purification variable (unwound one step), or a variable of the
 * problem (interval around its trail value).
 */
static void handle_variable(tra_plugin_t* tra, term_t t, tra_prec_t precision, tra_cache_abstraction_t* out) {
  tra_ilib_t* lib = tra->abstract_domain;

  tra_func_plugin_t* fp = tra_get_function_plugin_of_term(tra, t);
  if (fp != NULL) {
    assert(fp->arity == 0);
    fp->eval(fp, NULL, precision, out->interval);
    out->trail_position = tra_trail_position_vs_interval(tra, t, out->interval);
    return;
  }

  term_t t_step = tra_depurify_term_step(tra->ctx->preprocessor, t);
  if (t_step != t) {
    if (tra_trace_enabled(tra, "tra::abstraction")) {
      tra_trace_iprintf(tra, "[depurified ");
      tra_trace_print_term(tra, t);
      tra_trace_printf(tra,"]\n");
    }
    tra_cache_abstraction_t* step = compute_abstraction(tra, t_step, precision);
    lib->set(out->interval, step->interval);
    out->valid = step->valid;
    out->trail_trusted = step->trail_trusted;
    out->trail_position = tra_trail_position_vs_interval(tra, t, out->interval);
    return;
  }

  interval_around_trail_value(tra, t, precision, out);
}

/* ==========================
    Real-valued abstractions
   ========================== */

/**
 * Requires: lo and hi are initialised; both are overwritten.
 * Ensures:  result = max(tra_mpfr_magnitude_bits(lo), tra_mpfr_magnitude_bits(hi)), where
 *           [lo, hi] is the enclosure of γ(J) given by get_interval_overapproximation_mpfr.
 */
static tra_prec_t tra_abstraction_magnitude_bits(const tra_ilib_t* lib, tra_itype_t J, mpfr_t lo, mpfr_t hi) {
  lib->get_interval_overapproximation_mpfr(lo, hi, J);
  tra_prec_t mag_lo = tra_mpfr_magnitude_bits(lo);
  tra_prec_t mag_hi = tra_mpfr_magnitude_bits(hi);
  return mag_lo > mag_hi ? mag_lo : mag_hi;
}

/* 
 * Evaluates an ARITH_POLY c_0 * m_0  +  c_1 * m_1  +  ...  +  c_{n-1} * m_{n-1} term into 'out'.
 * Sets out->trail_trusted if it is set in the abstraction of some child (see
 * tra_cache_abstraction_t).
 */
static void handle_poly(tra_plugin_t* tra, term_t t, tra_prec_t precision, tra_cache_abstraction_t* out) {
  tra_ilib_t* lib = tra->abstract_domain;
  term_table_t* terms = tra->ctx->terms;

  uint32_t n = term_num_children(terms, t);

  // Accumulator starts at 0, we add each summand to it.
  lib->set_z(out->interval, 0);

  if (n <= 0) return;

  // Precision every summand is computed at. Each injects 4 error sources of at most
  // 2^-children_precision into the width of out: max|coeff_iv| * w(summand_iv) and
  // max|summand_iv| * w(coeff_iv), from w(X*Y) <= max|X| * w(Y) + max|Y| * w(X),
  // plus what the mul and the add each add on their own.
  tra_prec_t children_precision = NO_PREC;

  // To compute precision on the coefficients
  mpfr_t lo, hi;

  if(precision != NO_PREC){
    assert(n >= 1); // binlog(n) = ceil(log2 n) needs n >= 1
    children_precision = tra_add_precisions(precision, 2 + (tra_prec_t) binlog(n));
    mpfr_init2(lo, TRA_MPFR_PREC_INIT);
    mpfr_init2(hi, TRA_MPFR_PREC_INIT);
  }

  // c_i reused across summands
  tra_itype_t coeff_iv = lib->alloc();
  mpq_t coeff;
  mpq_init(coeff);

  for (uint32_t i = 0; i < n; i++) {
    term_t child = NULL_TERM;
    sum_term_component(terms, t, i, coeff, &child);

    if (child == NULL_TERM) {
      // Pure constant summand, just add c_i
      lib->set_mpz_div(coeff_iv, mpq_numref(coeff), mpq_denref(coeff), children_precision);
      lib->add(out->interval, out->interval, coeff_iv, children_precision);
      continue;
    }

    tra_prec_t child_precision = children_precision;
    if (children_precision != NO_PREC) {
      tra_prec_t mag_num = (tra_prec_t) mpz_sizeinbase(mpq_numref(coeff), 2);
      tra_prec_t mag_den = (tra_prec_t) mpz_sizeinbase(mpq_denref(coeff), 2);
      tra_prec_t c_bits = mag_num - mag_den + 2;
      if (c_bits < 0) c_bits = 0;
      child_precision = tra_add_precisions(children_precision, c_bits);
    }

    tra_cache_abstraction_t* child_val = compute_abstraction(tra, child, child_precision);
    out->trail_trusted = out->trail_trusted || child_val->trail_trusted;

    if (children_precision != NO_PREC) {
      tra_prec_t m_bits = tra_abstraction_magnitude_bits(lib, child_val->interval, lo, hi);
      child_precision = tra_add_precisions(children_precision, m_bits);
    } 
    
    // coeff_iv := c_i
    lib->set_mpz_div(coeff_iv, mpq_numref(coeff), mpq_denref(coeff), child_precision);

    // coeff_iv := c_i * child_interval, then out += coeff_iv
    lib->mul(coeff_iv, coeff_iv, child_val->interval, children_precision);
    lib->add(out->interval, out->interval, coeff_iv, children_precision);
  }

  out->trail_position = tra_trail_position_vs_interval(tra, t, out->interval);

  if(precision != NO_PREC){
    mpfr_clear(lo);
    mpfr_clear(hi);
  }
  mpq_clear(coeff);
  lib->free(coeff_iv);
}

/*
 * Evaluates a POWER_PRODUCT m_0^{e_0} * m_1^{e_1} * ... * m_{n-1}^{e_{n-1}} term into 'out'.
 * Sets out->trail_trusted if it is set in some child abstraction from which 'out' is computed
 * (the refined pass, if it runs, replaces the first one).
 */
static void handle_power_product(tra_plugin_t* tra, term_t t, tra_prec_t precision, tra_cache_abstraction_t* out) {
  tra_ilib_t* lib = tra->abstract_domain;
  term_table_t* terms = tra->ctx->terms;

  uint32_t n = term_num_children(terms, t);

  // Accumulator starts at 1
  lib->set_z(out->interval, 1);

  if (n <= 0) return;

  // Coarse pass
  tra_prec_t children_precision = tra_min_precisions(precision, tra->ctx->options->delta_precision);
  
  // With precision == NO_PREC the refined pass has nothing to refine to
  bool no_prec_regime = (precision == NO_PREC);

  // mag_out needed later for the refined pass. It will satisfy
  // log2|out| <= mag_out := sum_j e_j * mag_j
  tra_prec_t* mag = safe_malloc(n * sizeof(tra_prec_t));
  tra_prec_t mag_out = 0;

  for (uint32_t i = 0; i < n; i++) {
    term_t child;
    uint32_t exp;
    product_term_component(terms, t, i, &child, &exp);
    tra_cache_abstraction_t* child_val = compute_abstraction(tra, child, children_precision);

    out->trail_trusted = out->trail_trusted || child_val->trail_trusted;
    no_prec_regime = no_prec_regime || (lib->get_precision(child_val->interval) < children_precision);
  }

  if(!no_prec_regime){ 
    out->trail_trusted = false;

    mpfr_t lo, hi;
    mpfr_init2(lo, TRA_MPFR_PREC_INIT);
    mpfr_init2(hi, TRA_MPFR_PREC_INIT);

    for (uint32_t i = 0; i < n; i++) {
      term_t child;
      uint32_t exp;
      product_term_component(terms, t, i, &child, &exp);

      //previous loop ensures the following is a cache hit
      tra_cache_abstraction_t* child_val = tra_get_cached_abstraction_entry(tra, child); 
      assert(child_val->valid);

      mag[i] = tra_abstraction_magnitude_bits(lib, child_val->interval, lo, hi);
      mag_out += (tra_prec_t) exp * mag[i]; 
    }

    mpfr_clear(lo);
    mpfr_clear(hi);

    // Each factor injects 3 error sources of at most 2^-children_precision into the
    // width of out: the child's own error, pow_ui's rounding, and the mul's rounding.
    // Budget 4n slots, as in handle_poly.
    assert(n >= 1); // binlog(n) = ceil(log2 n) needs n >= 1
    tra_prec_t shared_extra = 2 + (tra_prec_t) binlog(n);

    children_precision = tra_add_precisions(precision, shared_extra);
  }

  // to represent m_i^e_i
  tra_itype_t pow_iv = lib->alloc();

  for (uint32_t i = 0; i < n; i++) {
    term_t child;
    uint32_t exp;
    product_term_component(terms, t, i, &child, &exp);

    tra_prec_t child_precision = children_precision;
    tra_prec_t mul_precision   = children_precision;

    assert(tra_get_cached_abstraction_entry(tra, child)->valid);
    tra_itype_t child_itv = tra_get_cached_abstraction_entry(tra, child)->interval;

    if (!no_prec_regime) {
      // Refined pass: d(out)/d(m_i) = e_i * m_i^{e_i-1} * prod_{j!=i} m_j^{e_j},
      // so log2|d(out)/d(m_i)| <= ceil(log2 e_i) + mag_out - mag_i.
      assert(exp >= 1); // exponents of power products are positive; binlog(e) = ceil(log2 e)
      tra_prec_t sensitivity_bits = (tra_prec_t) binlog(exp) + mag_out - mag[i];
      if (sensitivity_bits < 0) sensitivity_bits = 0;
      child_precision = tra_add_precisions(children_precision, sensitivity_bits);

      // Compute child at a higher precision
      tra_cache_abstraction_t* child_val = compute_abstraction(tra, child, child_precision);
      child_itv = child_val->interval;
      
      out->trail_trusted = out->trail_trusted || child_val->trail_trusted;

      // mul has no unit gain: its rounding is scaled by the factors after i, i.e. by
      // 2^(sum_{j>i} e_j*mag_j) <= 2^(mag_out - e_i*mag_i). Always <= sensitivity_bits.
      tra_prec_t mul_bits = mag_out - (tra_prec_t) exp * mag[i];
      if (mul_bits < 0) mul_bits = 0;
      mul_precision = tra_add_precisions(children_precision, mul_bits);
    }

    // pow_iv := child_itv ^ e_i
    lib->pow_ui(pow_iv, child_itv, (unsigned long) exp, child_precision);

    // out := out * pow_iv
    lib->mul(out->interval, out->interval, pow_iv, mul_precision);
  }

  out->trail_position = tra_trail_position_vs_interval(tra, t, out->interval);

  lib->free(pow_iv);
  safe_free(mag);
}

/*
 * Evaluates an APP_TERM 't', i.e., a call to a function plugin (e.g. sin(x), exp(x)), into 'out'.
 * Sets out->trail_trusted if the argument abstractions are not in the domain of the function
 * (then 'out' is R or encloses the trail value of t), or if it is set in some argument
 * abstraction from which 'out' is computed.
 */
static void handle_function_call(tra_plugin_t* tra, term_t t, tra_prec_t precision, tra_cache_abstraction_t* out) {
  tra_ilib_t* lib = tra->abstract_domain;
  term_table_t* terms = tra->ctx->terms;

  // WARNING: Uninterpreted non-transcendental functions are not supported yet
  tra_func_plugin_t* fp = tra_get_function_plugin_of_term(tra, t);
  assert(fp != NULL);

  ivector_t children;
  init_ivector(&children, 0);
  get_term_children(terms, t, &children);
  uint32_t n_args = children.size - 1; // exclude function name

  // Allocate one interval per argument
  tra_itype_t* arg_iv = safe_malloc(n_args * sizeof(tra_itype_t));

  // First pass, child abstractions at precision <= delta
  
  tra_prec_t children_precision = tra_min_precisions(precision, tra->ctx->options->delta_precision);

  // With precision == NO_PREC the refined pass has nothing to refine to
  bool no_prec_regime = (precision == NO_PREC);

  for (uint32_t i = 0; i < n_args; i++) {
    term_t child = children.data[i+1];
    tra_cache_abstraction_t* child_val = compute_abstraction(tra, child, children_precision);
    if (lib->get_precision(child_val->interval) < children_precision) no_prec_regime = true;
    out->trail_trusted = out->trail_trusted || child_val->trail_trusted;

    arg_iv[i] = child_val->interval;
  }

  // Domain check
  assert(fp->arity == (int) n_args);
  if (!fp->is_in_domain(fp, arg_iv)) {
    assert(!fp->is_total_continuous); // contract of is_in_domain
    // If children were not computed to the right precision, give up
    // Else trust the value in the trail
    if (no_prec_regime) lib->set_R(out->interval);
    else interval_around_trail_value(tra, t, precision, out);
    out->trail_trusted = true;
  } else {
    if (!no_prec_regime) {
      children_precision = fp->refine_precision(fp, arg_iv, precision);
      if (children_precision != NO_PREC) {
        out->trail_trusted = false;
        for (uint32_t i = 0; i < n_args; i++) {
          term_t child = children.data[i+1];
          tra_cache_abstraction_t* child_val = compute_abstraction(tra, child, children_precision);
          out->trail_trusted = out->trail_trusted || child_val->trail_trusted;
          // arg_iv[i] is the interval of E(child), which the refined pass updates in place
          assert(arg_iv[i] == child_val->interval);
        }
      }
    }

    // Evaluate
    fp->eval(fp, arg_iv, precision, out->interval);

    out->trail_position = tra_trail_position_vs_interval(tra, t, out->interval);
  }

  // Store the intervals of the arguments
  assert(out->n_args == n_args);
  for (uint32_t i = 0; i < n_args; i++) lib->set(out->args[i], arg_iv[i]);

  safe_free(arg_iv);
  delete_ivector(&children);
}

/* 
 * Evaluates an ARITH_RDIV term 'num/den' into 'out'.
 * Sets out->trail_trusted if it is set for 'num' or 'den', or if the abstraction of 'den' contains
 * zero while 'den' is not known to be exactly zero (then 'out' is R or encloses the trail value
 * of t; see tra_cache_abstraction_t).
 */
static void handle_divisibility(tra_plugin_t* tra, term_t t, tra_prec_t precision, tra_cache_abstraction_t* out) {
  tra_ilib_t* lib = tra->abstract_domain;
  term_table_t* terms = tra->ctx->terms;

  composite_term_t* rdiv = arith_rdiv_term_desc(terms, t);
  assert(rdiv->arity == 2);
  term_t num = rdiv->arg[0];
  term_t den = rdiv->arg[1];

  tra_prec_t children_precision = tra_min_precisions(precision, tra->ctx->options->delta_precision);

  // Coarse pass
  tra_cache_abstraction_t* num_val = compute_abstraction(tra, num, children_precision);
  tra_cache_abstraction_t* den_val = compute_abstraction(tra, den, children_precision);

  bool in_domain = !lib->zero_in(den_val->interval);
  bool no_prec_regime = precision == NO_PREC
    || lib->get_precision(num_val->interval) < children_precision 
    || lib->get_precision(den_val->interval) < children_precision;

  if (!in_domain) {
    // If numerator or denominator are not computed to the right precision 
    // we give up and set out = [-infty,+infty] with trail "trusted" (superflous)
    if (no_prec_regime) { 
      lib->set_R(out->interval);
      out->trail_trusted = true;
    } else {
      // Trust value in trail for the parent term.
      // In fact, if the precision on the denominator is infinite, 
      // we are not trusting the trail, we are just respecting the SMT-LIB semantics.
      interval_around_trail_value(tra, t, precision, out);
      out->trail_trusted = lib->get_precision(den_val->interval) != INF_PREC;

      // If den is not transcendental and it is assigned zero in the trail
      // we are not trusting the trail, we are just respecting the SMT-LIB semantics.
      int_hmap_pair_t* den_transcendental = int_hmap_find(&tra->attribute_transcendental, den);
      if (out->trail_trusted && den_transcendental != NULL && !den_transcendental->val) {
        variable_t den_var = variable_db_get_variable_if_exists(tra->ctx->var_db, den);
        if (den_var != variable_null && trail_has_value(tra->ctx->trail, den_var)
            && mcsat_value_is_zero(trail_get_value(tra->ctx->trail, den_var))) {
          out->trail_trusted = false;
        }
      }
    }
  } else {
    if (!no_prec_regime) {
      // Three error sources: w(num_iv) amplified by |d(out)/d(num)|,
      // w(den_iv) amplified by |d(out)/d(den)|, and div's own rounding. 
      // Budget 4 slots for 3 sources, as in handle_poly.
      children_precision = tra_add_precisions(precision, 2);

      // out = num/den, seen as a function, has partial derivatives: 
      // |d(out)/d(num)| = 1/|den|, |d(out)/d(den)| = |out|/|den|.
      // Both partial derivatives depends on a LOWER bound on |den|. 
      // The derivative w.r.t. den depends on an UPPER bound on |num|.
      mpfr_t lo, hi;
      mpfr_init2(lo, TRA_MPFR_PREC_INIT);
      mpfr_init2(hi, TRA_MPFR_PREC_INIT);

      tra_prec_t mag_num = tra_abstraction_magnitude_bits(lib, num_val->interval, lo, hi);

      lib->get_interval_overapproximation_mpfr(lo, hi, den_val->interval);
      // den is precise here (not the no-precision regime), hence bounded: mpfr_get_exp
      // below is defined on both ends
      assert(mpfr_number_p(lo) && mpfr_number_p(hi));

      // 0 is not in den_iv, so lo and hi share sign and are both nonzero:
      // floor(log2(|x|)) = e - 1 in mpfr's m * 2^e convention (0.5 <= |m| < 1).
      tra_prec_t mag_den_lo_bits = (tra_prec_t) (mpfr_get_exp(lo) - 1);
      tra_prec_t mag_den_hi_bits = (tra_prec_t) (mpfr_get_exp(hi) - 1);
      tra_prec_t mag_den = mag_den_lo_bits < mag_den_hi_bits ? mag_den_lo_bits : mag_den_hi_bits;

      mpfr_clear(lo);
      mpfr_clear(hi);

      tra_prec_t mag_out = mag_num - mag_den;

      tra_prec_t boost_num = mag_out - mag_num; // = -mag_den
      if (boost_num < 0) boost_num = 0;
      tra_prec_t boost_den = mag_out - mag_den;
      if (boost_den < 0) boost_den = 0;

      tra_prec_t child_precision_num = tra_add_precisions(children_precision, boost_num);
      tra_prec_t child_precision_den = tra_add_precisions(children_precision, boost_den);

      // Refined pass: recompute num and den at their required precisions
      num_val = compute_abstraction(tra, num, child_precision_num);
      den_val = compute_abstraction(tra, den, child_precision_den);
    }

    lib->div(out->interval, num_val->interval, den_val->interval, children_precision);

    out->trail_position = tra_trail_position_vs_interval(tra, t, out->interval);
  }

  out->trail_trusted = out->trail_trusted || num_val->trail_trusted || den_val->trail_trusted;
}

/* ====================
    Consistency check
   ==================== */

term_t tra_atom_argument(tra_plugin_t* tra, term_t t) {
  term_table_t* terms = tra->ctx->terms;
  if (term_kind(terms, t) != ARITH_BINEQ_ATOM) return arith_atom_arg(terms, t);

  // compute the abstraction of t1-t2 instead
  composite_term_t* bineq = arith_bineq_atom_desc(terms, t);
  assert(bineq->arity == 2);
  term_t s = _o_yices_sub(bineq->arg[0], bineq->arg[1]);
  assert(int_hmap_find(&tra->attribute_transcendental, s) != NULL); // analyzed with the atom
  return s;
}

/*
 * Consistency check of the atom t (assigned in the trail) against the
 * abstraction J of its argument. See tra_cc_result_t.
 */
static tra_cc_result_t tra_evaluate_consistency(tra_plugin_t* tra, term_t t) {
  int truth = 0;
  tra_ilib_t* lib = tra->abstract_domain;
  term_table_t* terms = tra->ctx->terms;
  long delta = tra->ctx->options->delta_precision;

  // Every atom the plugin evaluates is a term of the variable database, whose
  // value the wake-up mechanism (tra_wake_dependents) guarantees to be in the trail.
  variable_t term_id = variable_db_get_variable_if_exists(tra->ctx->var_db, t);
  assert(term_id != variable_null && trail_has_value(tra->ctx->trail, term_id));

  tra_prec_t precision = NO_PREC;

  term_kind_t tk = term_kind(terms, t);
  assert(tk == ARITH_EQ_ATOM || tk == ARITH_GE_ATOM || tk == ARITH_BINEQ_ATOM);

  bool eq_atom = tk != ARITH_GE_ATOM;
  term_t s = tra_atom_argument(tra, t);

  // Worse-case, we perform the interval analysis twice, 
  // once in NO_PREC mode, and once refining the precision.
  interval_analysis: ;

  tra_cache_abstraction_t* s_val = compute_abstraction(tra, s, precision);

  bool trail_trusted = s_val->trail_trusted;
  tra_itype_t J = s_val->interval;

  truth = tra_get_atom_truth(lib, J, eq_atom);
  bool term_surely_true = truth > 0;
  bool term_surely_false = truth < 0;
  // The width budget 2^-delta is split: J gets 2^-(delta+1), and the extraction of [lo, hi] below,
  // which may round the endpoints outward, the other 2^-(delta+1)
  bool precise = lib->get_precision(J) >= tra_add_precisions(delta,1);

  assert(lib->is_bounded(J) || !precise);

  if (!term_surely_true && !term_surely_false) {
    if (precision == NO_PREC && ((trail_trusted || !precise))) {
      tra_trace_iprintf_if(tra, "tra::consistency",
                           "[working with NO_PREC led to an imprecise result. Switching to precision = delta.]\n");
      precision = tra_add_precisions(delta,1);
      goto interval_analysis;
    }

    if (!precise) {
      // Impossible to perform consistency check
      tra_trace_iprintf_if(tra, "tra::consistency", "[bad result at precision = delta: reporting a failure]\n");
      s_val->valid = false;
      return (tra_cc_result_t) { TRA_CC_UNKNOWN, truth };
    }
  }

  if(precise){
    // J is precise. In this case we (implicitly) expand J to an interval of 
    // width 2^-delta, and check inconsistency with respect to the the expansion
    mpfr_t lo, hi;
    mpfr_init2(lo, TRA_MPFR_PREC_INIT);
    mpfr_init2(hi, TRA_MPFR_PREC_INIT);
    bool enclosed = tra_interval_to_mpfr(lib, J, tra_add_precisions(delta, 1), lo, hi);

#ifndef NDEBUG
    assert(enclosed);
    // Only when J is exactly [lo, hi] does a width bound on J transfer to hi - lo.
    if (lib->contains_interval_mpfr(J, lo, hi)) {
      // hi - lo <= 2^-delta, checked exactly on the dyadic rationals lo and hi
      assert(delta >= 0);
      mpq_t width, q_lo, eps;
      mpq_inits(width, q_lo, eps, NULL);
      mpfr_get_q(width, hi);
      mpfr_get_q(q_lo, lo);
      mpq_sub(width, width, q_lo);
      mpq_set_ui(eps, 1, 1);
      mpq_div_2exp(eps, eps, (mp_bitcnt_t) delta);
      assert(mpq_cmp(width, eps) <= 0);
      mpq_clears(width, q_lo, eps, NULL);
    }
#endif

    // Exact value of s in the trail, and check if it is surely 
    // within 2^-delta of the real value
    mcsat_value_t s_trail_value;
    term_evaluate(tra->ctx, s, &s_trail_value);
    
    bool s_delta_close = enclosed
            && tra_value_close_mpfr(&s_trail_value, lo, delta)
            && tra_value_close_mpfr(&s_trail_value, hi, delta);

    mcsat_value_destruct(&s_trail_value);
    mpfr_clear(lo);
    mpfr_clear(hi);

    if(!s_delta_close) return (tra_cc_result_t) { TRA_CC_INCONSISTENT, truth };
  }

  assert(precise || term_surely_true || term_surely_false);

  if(!term_surely_true && !term_surely_false) {
    // in this case J is precise and contains 0 in its interior.
    // We found s_delta_close = true, so we return delta-consistent.
    return (tra_cc_result_t) { TRA_CC_DELTA, truth };
  }

  // Now we know that the term is surely true or surely false, 
  // but it might be imprecise. Just look at the Boolean the 
  // inequality takes for the consistency.

  // Polarity of the constraint in the trail
  assert(variable_db_is_boolean(tra->ctx->var_db, term_id));
  bool asserted_true = trail_get_value(tra->ctx->trail, term_id)->b;
  bool polarity_agrees_with_J = (asserted_true && term_surely_true) || (!asserted_true && term_surely_false);

  if(polarity_agrees_with_J){
    if(trail_trusted) return (tra_cc_result_t) { TRA_CC_DELTA, truth };
    return (tra_cc_result_t) { TRA_CC_CONSISTENT, truth };
  }
  return (tra_cc_result_t) { TRA_CC_INCONSISTENT, truth };
}

/* ==================
    ITE abstractions
   ================== */

static inline tra_guard_value_t tra_guard_of_bool(bool b) {
  return b ? TRA_GUARD_TRUE : TRA_GUARD_FALSE;
}

/*
 * Evaluates the guard b of an if-then-else.
 */
static tra_guard_value_t tra_evaluate_guard(tra_plugin_t* tra, term_t b) {
  term_table_t* terms = tra->ctx->terms;
  assert(is_boolean_term(terms, b));

  if (is_neg_term(b)) {
    tra_guard_value_t v = tra_evaluate_guard(tra, unsigned_term(b));
    if (v == TRA_GUARD_TRUE) return TRA_GUARD_FALSE;
    if (v == TRA_GUARD_FALSE) return TRA_GUARD_TRUE;
    return v;
  }

  switch (term_kind(terms, b)) {

    case CONSTANT_TERM:
      assert(b == true_term || b == false_term);
      return tra_guard_of_bool(b == true_term);

    case UNINTERPRETED_TERM: {
      // A purification variable stands for a Boolean term: evaluate that term
      term_t b_step = tra_depurify_term_step(tra->ctx->preprocessor, b);
      if (b_step != b) return tra_evaluate_guard(tra, b_step);
      
      variable_t var = variable_db_get_variable_if_exists(tra->ctx->var_db, b);
      assert(var != variable_null && trail_has_value(tra->ctx->trail, var));
      return tra_guard_of_bool(trail_get_value(tra->ctx->trail, var)->b);
    }

    case ARITH_EQ_ATOM:
    case ARITH_GE_ATOM:
    case ARITH_BINEQ_ATOM: {
      tra_cc_result_t r = tra_evaluate_consistency(tra, b);
      if (r.truth != 0) return tra_guard_of_bool(r.truth > 0);
      if (r.verdict == TRA_CC_DELTA) return TRA_GUARD_DELTA;
      return TRA_GUARD_UNKNOWN;
    }

    case OR_TERM: {
      composite_term_t* d = composite_term_desc(terms, b);
      tra_guard_value_t value = TRA_GUARD_FALSE;
      for (uint32_t i = 0; i < d->arity; i++) {
        tra_guard_value_t c = tra_evaluate_guard(tra, d->arg[i]);
        if (c == TRA_GUARD_TRUE) return TRA_GUARD_TRUE;
        if (c == TRA_GUARD_UNKNOWN) value = TRA_GUARD_UNKNOWN;
        else if (c == TRA_GUARD_DELTA && value == TRA_GUARD_FALSE) value = TRA_GUARD_DELTA;
      }
      return value;
    }

    case XOR_TERM: {
      composite_term_t* d = composite_term_desc(terms, b);
      bool odd = false, delta = false;
      for (uint32_t i = 0; i < d->arity; i++) {
        tra_guard_value_t c = tra_evaluate_guard(tra, d->arg[i]);
        if (c == TRA_GUARD_UNKNOWN) return TRA_GUARD_UNKNOWN;
        if (c == TRA_GUARD_DELTA) delta = true;
        else if (c == TRA_GUARD_TRUE) odd = !odd;
      }
      return delta ? TRA_GUARD_DELTA : tra_guard_of_bool(odd);
    }

    case EQ_TERM: {
      composite_term_t* d = composite_term_desc(terms, b);
      assert(d->arity == 2);
      tra_guard_value_t x = tra_evaluate_guard(tra, d->arg[0]);
      tra_guard_value_t y = tra_evaluate_guard(tra, d->arg[1]);
      if (x == TRA_GUARD_UNKNOWN || y == TRA_GUARD_UNKNOWN) return TRA_GUARD_UNKNOWN;
      if (x == TRA_GUARD_DELTA || y == TRA_GUARD_DELTA) return TRA_GUARD_DELTA;
      return tra_guard_of_bool(x == y);
    }

    case ITE_TERM:
    case ITE_SPECIAL: {
      composite_term_t* d = composite_term_desc(terms, b);
      assert(d->arity == 3);
      tra_guard_value_t g = tra_evaluate_guard(tra, d->arg[0]);
      if (g == TRA_GUARD_TRUE) return tra_evaluate_guard(tra, d->arg[1]);
      if (g == TRA_GUARD_FALSE) return tra_evaluate_guard(tra, d->arg[2]);
      tra_guard_value_t x = tra_evaluate_guard(tra, d->arg[1]);
      tra_guard_value_t y = tra_evaluate_guard(tra, d->arg[2]);
      if (x == y && (x == TRA_GUARD_TRUE || x == TRA_GUARD_FALSE)) return x; // the guard does not matter
      return g; // DELTA or UNKNOWN
    }

    default:
      assert(false); // other Boolean terms are not supported
      return TRA_GUARD_UNKNOWN;
  }
}

/*
 * Evaluates a real-valued ite(b, x, y) into 'out'.
 */
static void handle_ite(tra_plugin_t* tra, term_t t, tra_prec_t precision, tra_cache_abstraction_t* out) {
  tra_ilib_t* lib = tra->abstract_domain;
  composite_term_t* ite = ite_term_desc(tra->ctx->terms, t);
  assert(ite->arity == 3);

  tra_guard_value_t g = tra_evaluate_guard(tra, ite->arg[0]);

  tra_trace_iprintf_if(tra, "tra::abstraction", "[ite guard: %s]\n",
                       g == TRA_GUARD_TRUE ? "true" : g == TRA_GUARD_FALSE ? "false"
                       : g == TRA_GUARD_DELTA ? "delta" : "unknown");

  switch (g) {
    case TRA_GUARD_TRUE:
    case TRA_GUARD_FALSE: {
      tra_cache_abstraction_t* branch = compute_abstraction(tra, ite->arg[g == TRA_GUARD_TRUE ? 1 : 2], precision);
      lib->set(out->interval, branch->interval);
      out->trail_trusted = branch->trail_trusted;
      break;
    }
    case TRA_GUARD_DELTA:
      interval_around_trail_value(tra, t, precision, out);
      out->trail_trusted = true;
      return; // the trail value is inside by construction
    case TRA_GUARD_UNKNOWN: {
      tra_prec_t branch_precision = tra_add_precisions(precision, 1);
      tra_cache_abstraction_t* x = compute_abstraction(tra, ite->arg[1], branch_precision);
      tra_cache_abstraction_t* y = compute_abstraction(tra, ite->arg[2], branch_precision);
      lib->join(out->interval, x->interval, y->interval, precision);
      out->trail_trusted = x->trail_trusted || y->trail_trusted;
      break;
    }
  }

  out->trail_position = tra_trail_position_vs_interval(tra, t, out->interval);
}

/* ==========================
    Compute full abstraction
   ========================== */

/* Computes and returns E(t), an overapproximation of the term 't'. Its trail_trusted holds iff
 * somewhere in the computation the trail value had to be trusted because an overapproximation
 * was not fully contained in the domain of a function.
 * 
 * If precision = NO_PREC, all recursive calls to compute_abstraction are performed 
 * with precision = NO_PREC.
 */
static tra_cache_abstraction_t* compute_abstraction(tra_plugin_t* tra, term_t t, tra_prec_t precision) {
  tra_ilib_t* lib = tra->abstract_domain;

  // Cache lookup
  tra_cache_abstraction_t* cached = tra_get_cached_abstraction_entry(tra, t);
  assert(cached != NULL);

  if(cached->valid && lib->get_precision(cached->interval) >= precision){
    if (tra_trace_enabled(tra, "tra::abstraction")) {
      tra_trace_iprintf(tra, "[abstraction: cache hit for term ");
      tra_trace_print_term(tra, t);
      tra_trace_printf(tra, "]\n");
      tra_trace_print_abstraction(tra, t);
    }
    return cached;
  }

  // Compute abstraction
  term_table_t* terms = tra->ctx->terms;

  cached->trail_trusted = false;
  cached->trail_position = TV_INSIDE;
  cached->valid = true;

  switch (term_kind(terms, t)) {

    case ARITH_CONSTANT: {
      // Exact rational constant
      rational_t* c = rational_term_desc(terms, t);
      mpq_t q;
      mpq_init(q);
      q_get_mpq(c, q);
      lib->set_mpz_div(cached->interval, mpq_numref(q), mpq_denref(q), precision);
      mpq_clear(q);
      break;
    }

    case UNINTERPRETED_TERM:
      handle_variable(tra, t, precision, cached);
      break;

    case ARITH_POLY:
      handle_poly(tra, t, precision, cached);
      break;

    case POWER_PRODUCT:
      handle_power_product(tra, t, precision, cached);
      break;

    case APP_TERM:
      handle_function_call(tra, t, precision, cached);
      break;

    case ARITH_RDIV:
      handle_divisibility(tra, t, precision, cached);
      break;

    case ARITH_ABS: {
      // abs is 1-Lipschitz on intervals (its width never exceeds the child's),
      // so the child can be requested directly at the target precision
      term_t child = arith_abs_arg(terms, t);
      tra_cache_abstraction_t* child_val = compute_abstraction(tra, child, precision);
      lib->abs(cached->interval, child_val->interval);
      cached->trail_trusted = child_val->trail_trusted;
      cached->trail_position = tra_trail_position_vs_interval(tra, t, cached->interval);
      break;
    }

    case ITE_TERM:
    case ITE_SPECIAL:
      handle_ite(tra, t, precision, cached);
      break;

    default:
      assert(false); // other terms not supported yet
      break;
  }

  if (tra_trace_enabled(tra, "tra::abstraction")) {
    tra_trace_print_abstraction(tra, t);
  }

  // Every operation meets the requested precision, except the join of the two
  // branches of an ite with an undecided guard (see handle_ite)
  assert(lib->get_precision(cached->interval) >= precision 
    || lib->get_precision(cached->interval) == NO_PREC
    || term_kind(terms, t) == ITE_TERM || term_kind(terms, t) == ITE_SPECIAL);

  return cached;
}

/* ==============================
    Unassigned variables
   ============================== */

/* True if at least one variable of 'vars' is unassigned. */
static bool tra_has_unassigned_variable(tra_plugin_t* tra, int_hset_t* vars) {
  assert(int_hset_is_closed(vars));

  for(uint32_t i = 0; i < vars->nelems; i++) {
    term_t t = vars->data[i];
    variable_t var = variable_db_get_variable_if_exists(tra->ctx->var_db, t);
    if (var == variable_null || !trail_has_value(tra->ctx->trail, var)) {
      return true;
    }
  }

  return false;
}

/* =====================
    Conflict extraction
   ===================== */

static void tra_extract_conflicts(tra_plugin_t* tra, term_t t);

/**
 * Requires: t is owned by a function plugin; children holds the n arguments of t (NULL when
 *           n = 0, i.e. when t is a constant).
 * Modifies: tra->cache_conflicts; tra->cache_abstractions (new invalid entries may be created).
 * Let E(t) be the cache entry of t (see tra_cache_abstraction_t), valid or not, and call a
 * position outside if it is TV_ABOVE or TV_BELOW (TV_UNKNOWN counts as inside).
 * Ensures:  E(t).trail_position is not outside ==> nothing happens;
 *           otherwise, the position of children[i] relative to E(t).args[i] is not outside
 *           for every i < n ==> t is pushed to tra->cache_conflicts;
 *           otherwise, tra_extract_conflicts is applied to every child.
 */
static void tra_extract_conflicts_logic(tra_plugin_t* tra, term_t t, const term_t* children, uint32_t n) {
  assert(tra_get_function_plugin_of_term(tra, t) != NULL);
  tra_cache_abstraction_t* t_val = tra_get_cached_abstraction_entry(tra, t);

  if (t_val->trail_position != TV_ABOVE && t_val->trail_position != TV_BELOW) return;

  // Not the children's own entries: a later refinement of a child need not be
  // included in the abstraction I(t) was computed from
  assert(t_val->n_args == n);
  bool children_inside = true;
  for (uint32_t i = 0; i < n && children_inside; i++) {
    tra_tv_position_t position = tra_trail_position_vs_interval(tra, children[i], t_val->args[i]);
    children_inside = position != TV_ABOVE && position != TV_BELOW;
  }

  if (children_inside) {
    // TODO: It would be nice to add to the condition above the following or.
    //   However, it requires function plugins that handle correctly
    //   total-continuous constant terms, in case the default strategy for these fails.
    //   I'm thus leaving it out for now.
    // || (int_hmap_get(&tra->attribute_constant, t)
    //                      && int_hmap_get(&tra->attribute_total_continuous, t))) {
    if (tra_trace_enabled(tra, "tra::extract_conflicts")) {
      tra_trace_iprintf(tra, "[conflict: ");
      tra_trace_print_term(tra, t);
      tra_trace_printf(tra, "]\n");
    }
    ivector_push(&tra->cache_conflicts, t);
  } else {
    for (uint32_t i = 0; i < n; i++) tra_extract_conflicts(tra, children[i]);
  }
}

/*
 * Walks 't' looking for conflicts, calling tra_extract_conflicts_logic
 */
static void tra_extract_conflicts(tra_plugin_t* tra, term_t t) {
  term_table_t* terms = tra->ctx->terms;
  term_kind_t tk = term_kind(terms, t); // the kind of the atom for a negative literal

  switch (tk) {

    case ARITH_CONSTANT:
    case CONSTANT_TERM:
      return;

    // Atoms: recurse into the arithmetic arguments; if that produced conflicts, or if the trail
    // value of the argument escapes its abstraction, record the atom too. Without the latter, no
    // conflict is raised when the abstraction knows a relation between terms that NA does not.
    case ARITH_EQ_ATOM:
    case ARITH_GE_ATOM:
    case ARITH_BINEQ_ATOM: {
      uint32_t before = tra->cache_conflicts.size;
      if (tk == ARITH_BINEQ_ATOM) {
        composite_term_t* eq = arith_bineq_atom_desc(terms, t);
        tra_extract_conflicts(tra, eq->arg[0]);
        tra_extract_conflicts(tra, eq->arg[1]);
      } else {
        tra_extract_conflicts(tra, arith_atom_arg(terms, t));
      }
      tra_cache_abstraction_t* s_val = tra_get_cached_abstraction_entry(tra, tra_atom_argument(tra, t));
      bool escapes = s_val->valid && (s_val->trail_position == TV_ABOVE || s_val->trail_position == TV_BELOW);
      if (tra->cache_conflicts.size > before || escapes) {
        term_t atom = unsigned_term(t);
        if (tra_trace_enabled(tra, "tra::extract_conflicts")) {
          tra_trace_iprintf(tra, "[conflicting atom: ");
          tra_trace_print_term(tra, atom);
          tra_trace_printf(tra, "]\n");
        }
        ivector_push(&tra->cache_conflicts, atom);
      }
      return;
    }

    // Boolean connectives, and if-then-elses of either type: conflicts come from
    // the children (for a real ite: the guard and both branches)
    case OR_TERM:
    case XOR_TERM:
    case EQ_TERM:
    case ITE_TERM:
    case ITE_SPECIAL: {
      composite_term_t* d = composite_term_desc(terms, t);
      for (uint32_t i = 0; i < d->arity; i++) tra_extract_conflicts(tra, d->arg[i]);
      return;
    }

    case UNINTERPRETED_TERM: {
      if (is_boolean_term(terms, t)) return; // Boolean variable

      if (tra_get_function_plugin_of_term(tra, t) != NULL) {
        // A registered constant (pi): no children, so "every child is inside" holds vacuously
        tra_extract_conflicts_logic(tra, t, NULL, 0);
        return;
      }

      // Purification variable: unwind one step
      term_t t_step = tra_depurify_term_step(tra->ctx->preprocessor, t);
      if (t_step != t) tra_extract_conflicts(tra, t_step);
      return;
    }

    case ARITH_POLY: {
      uint32_t n = term_num_children(terms, t);
      mpq_t coeff;
      mpq_init(coeff);
      for (uint32_t i = 0; i < n; i++) {
        term_t child = NULL_TERM;
        sum_term_component(terms, t, i, coeff, &child);
        if (child != NULL_TERM) tra_extract_conflicts(tra, child);
      }
      mpq_clear(coeff);
      return;
    }

    case POWER_PRODUCT: {
      uint32_t n = term_num_children(terms, t);
      for (uint32_t i = 0; i < n; i++) {
        term_t child;
        uint32_t exp;
        product_term_component(terms, t, i, &child, &exp);
        tra_extract_conflicts(tra, child);
      }
      return;
    }

    case APP_TERM: {
      // arg[0] is the function symbol; the arguments follow
      composite_term_t* app = app_term_desc(terms, t);
      if (is_boolean_term(terms, t)) { // uninterpreted predicate: conflicts come from the arguments
        for (uint32_t i = 1; i < app->arity; i++) tra_extract_conflicts(tra, app->arg[i]);
        return;
      }
      tra_extract_conflicts_logic(tra, t, app->arg + 1, app->arity - 1);
      return;
    }

    // NA knows the definitions of division and abs as polynomial constraints (and
    // that of an ite as lemmas): a value escaping while the arguments are inside is
    // NA's conflict to find, not ours. Only their arguments are explored.
    case ARITH_RDIV: {
      composite_term_t* rdiv = arith_rdiv_term_desc(terms, t);
      assert(rdiv->arity == 2);
      tra_extract_conflicts(tra, rdiv->arg[0]);
      tra_extract_conflicts(tra, rdiv->arg[1]);
      return;
    }

    case ARITH_ABS:
      tra_extract_conflicts(tra, arith_abs_arg(terms, t));
      return;

    default:
      assert(false);
      return;
  }
}

/* =============
    Entry point
   ============= */

/*
 *  This is just a wrapper around tra_evaluate_consistency
 */
void tra_consistency_check(tra_plugin_t* tra, term_t t) {
  if (tra_trace_enabled(tra, "tra::consistency")) {
    tra_trace_enter_function(tra, "consistency");
    tra_trace_print_term_infos(tra, t);
  }

  //Check if the term is transcendental. If not, skip the check.
  int_hmap_pair_t* transcendental = int_hmap_find(&tra->attribute_transcendental, t);
  assert(transcendental != NULL); // t is analyzed
  if (!transcendental->val) {
    tra_trace_exit_function(tra, "consistency");
    return;
  }

  //Check if all variables of the term are assigned. If not, skip the check. 
  ptr_hmap_pair_t* term_variables_pair = ptr_hmap_find(&tra->attribute_term_variables, t);
  assert(term_variables_pair != NULL && term_variables_pair->val != NULL);
  int_hset_t* term_variables = (int_hset_t*) term_variables_pair->val;

  bool has_unassigned_variables = tra_has_unassigned_variable(tra, term_variables);

  if (has_unassigned_variables) {
    if (tra_trace_enabled(tra, "tra::consistency")) {
      int_hset_t term_unassigned_variables;
      init_int_hset(&term_unassigned_variables, 0);
      assert(int_hset_is_closed(term_variables));
      for (uint32_t i = 0; i < term_variables->nelems; i++) {
        variable_t var = variable_db_get_variable_if_exists(tra->ctx->var_db, term_variables->data[i]);
        if (var == variable_null || !trail_has_value(tra->ctx->trail, var)) {
          int_hset_add(&term_unassigned_variables, term_variables->data[i]);
        }
      }
      int_hset_close(&term_unassigned_variables);

      tra_trace_iprintf(tra, "[consistency check skipped: the term ");
      tra_trace_print_term(tra, t);
      tra_trace_printf(tra, " has unassigned variables ");
      for(uint32_t i = 0; i < term_unassigned_variables.nelems; i++) {
        tra_trace_print_term(tra, term_unassigned_variables.data[i]);
        if (i + 1 < term_unassigned_variables.nelems) {
          tra_trace_printf(tra, ", ");
        }
      }
      tra_trace_printf(tra, "]\n");
      tra_trace_exit_function(tra, "consistency");

      delete_int_hset(&term_unassigned_variables);
    }

    return;
  }

  tra_cc_verdict_t result = tra_evaluate_consistency(tra, t).verdict;

  if (result == TRA_CC_DELTA) {
    tra_trace_iprintf_if(tra, "tra::consistency", "[ctx->trigger_delta called: level=%u]\n",
                         tra->ctx->trail->decision_level);
    tra->ctx->trigger_delta(tra->ctx, tra->ctx->trail->decision_level);
  }

  if (result == TRA_CC_INCONSISTENT || result == TRA_CC_UNKNOWN){ 
    
    tra_trace_enter_function(tra, "extract_conflicts");
    tra_extract_conflicts(tra, t); 
    tra_trace_exit_function(tra, "extract_conflicts");

    if(tra->cache_conflicts.size > 0) {
      if (tra_trace_enabled(tra, "tra::consistency")) {
            tra_trace_iprintf(tra, "[conflict detected]\n");
            tra_trace_exit_function(tra, "consistency");
          }
      return;
    } 

    tra_trace_exit_function(tra, "consistency");

    // If we are in INCONSISTENT state but cache_conflicts == 0, 
    // then the inconsistency is at the NA level and we do not 
    // need to raise a conflict. 
    if(result == TRA_CC_UNKNOWN){
      assert(false); 
      // Fail in debug mode, else we are essentially are unable to find unsat
      // but we can reset and still try to find sat
      tra->ctx->report_failure(tra->ctx);
    }
    return;
  }

  if (tra_trace_enabled(tra, "tra::consistency")) {
    tra_trace_iprintf(tra, "[consistency check: %s]\n", 
      result == TRA_CC_CONSISTENT ? "consistent" : "delta-consistent");
      // INCONSITENT and UNKNOWN already returned.
    tra_trace_exit_function(tra, "consistency");
  }
}

tra_cc_verdict_t tra_check_trail_value_transcendental_constant(tra_plugin_t* tra, term_t t) {
  const tra_ilib_t* lib = tra->abstract_domain;
  assert(ptr_hmap_find(&tra->attribute_term_variables, t) != NULL);
  assert(!tra_has_unassigned_variable(tra, (int_hset_t*) ptr_hmap_find(&tra->attribute_term_variables, t)->val));
  assert(int_hmap_find(&tra->attribute_transcendental, t)->val && int_hmap_find(&tra->attribute_constant, t)->val
         && int_hmap_find(&tra->attribute_total_continuous, t)->val);

  // NO_PREC first, as in tra_evaluate_consistency; then from delta on, the precision doubles
  // until v(t) escapes I(t) with a literal. I(t) does not depend on the trail: no restart is needed.
  tra_prec_t delta_precision = tra->ctx->options->delta_precision;
  assert(delta_precision >= 1);
  for (tra_prec_t p = NO_PREC; ; p = (p == NO_PREC) ? delta_precision : 2 * p) {
    tra_cache_abstraction_t* e = compute_abstraction(tra, t, p);
    assert(!e->trail_trusted); // t is constant and total-continuous: I(t) encloses c
    bool outside = e->trail_position == TV_BELOW || e->trail_position == TV_ABOVE;
    if (outside && tra_conflict_escape_literal(tra->ctx, lib, t, e->interval, NULL) != NULL_TERM) {
      return TRA_CC_INCONSISTENT;
    }
    tra_prec_t precision = lib->get_precision(e->interval);
    if (e->trail_position == TV_INSIDE && precision == INF_PREC) return TRA_CC_CONSISTENT;
    if (p >= TRA_CONFLICT_PREC_LIMIT) {
      return e->trail_position == TV_INSIDE && precision >= delta_precision ? TRA_CC_DELTA : TRA_CC_UNKNOWN;
    }
  }
}
