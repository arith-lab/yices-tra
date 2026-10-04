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

#include "mcsat/tra/functions/tra_exp.h"

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include <gmp.h>
#include <mpfr.h>
#include <poly/integer.h>
#include <poly/poly.h>
#include <poly/value.h>

#include "yices_types.h"
#include "api/yices_api_lock_free.h"
#include "mcsat/mcsat_types.h"
#include "mcsat/plugin.h"
#include "mcsat/tracing.h"
#include "mcsat/trail.h"
#include "mcsat/value.h"
#include "mcsat/variable_db.h"
#include "terms/term_explorer.h"
#include "terms/terms.h"
#include "utils/int_hash_sets.h"
#include "utils/int_vectors.h"
#include "utils/memalloc.h"

#include "mcsat/tra/tra_abstract_values.h"
#include "mcsat/tra/tra_conflict_utils.h"
#include "mcsat/tra/utils/tra_exact_values.h"
#include "mcsat/tra/utils/tra_tracing.h"

/* =========
    Helpers
   ========= */

// The Maclaurin polynomial T_n(x) = sum_{i <= n} x^i / i!
static term_t tra_exp_maclaurin(term_t x, uint32_t n) {
  term_t approximation = _o_yices_rational32(1, 1);
  term_t power = _o_yices_rational32(1, 1); // x^i
  term_t inverse_factorial = _o_yices_rational32(1, 1); // 1/i!

  for (uint32_t i = 1; i <= n; i++) {
    power = _o_yices_mul(power, x);
    inverse_factorial = _o_yices_mul(inverse_factorial, _o_yices_rational32(1, i));
    approximation = _o_yices_add(approximation, _o_yices_mul(power, inverse_factorial));
  }
  return approximation;
}

// t den - num, for the [m/m] Pade approximant num/den of exp at 0 (m = 1 or 2)
static term_t tra_exp_pade_difference(term_t t, term_t x, uint32_t m) {
  assert(m == 1 || m == 2);
  term_t den, num;
  if (m == 1) {
    // [1/1] = (2 + x) / (2 - x)
    term_t two = _o_yices_rational32(2, 1);
    den = _o_yices_sub(two, x);
    num = _o_yices_add(two, x);
  } else {
    // [2/2] = (12 + 6x + x^2) / (12 - 6x + x^2)
    term_t twelve = _o_yices_rational32(12, 1);
    term_t six_x = _o_yices_mul(_o_yices_rational32(6, 1), x);
    term_t x_squared = _o_yices_mul(x, x);
    den = _o_yices_add(_o_yices_sub(twelve, six_x), x_squared);
    num = _o_yices_add(_o_yices_add(twelve, six_x), x_squared);
  }
  return _o_yices_sub(_o_yices_mul(t, den), num);
}

/**
 * Requires: t is an application owned by self.
 * Ensures:  result is the term (exp 1). Terms are hash-consed, so it is always the same term. 
 *           Important: it is not cached, because a term GC may recycle its id.
 */
static term_t tra_exp_e_term(const tra_func_plugin_t* self, term_t t) {
  term_table_t* terms = tra_get_context(self->tra)->terms;
  assert(term_kind(terms, t) == APP_TERM && tra_application_is_of_the_plugin(self, t));
  term_t one = _o_yices_rational32(1, 1);
  return _o_yices_application(composite_term_arg(terms, t, 0), 1, &one);
}

/* =============
    Term Notify
   ============= */

// Enclosure of e in the lemmas on (exp 1): (NUM - 1)/DEN < e < NUM/DEN
#define TRA_EXP_LEMMA_E_NUM 28245729
#define TRA_EXP_LEMMA_E_DEN 10391023

// Adds lemmas 1 + x <= (exp x) and (exp x) > 0, and the bounds on e when t is (exp 1), which gets
// its variable with any application of exp (see tra_exp_get_implicit_variables)
static void tra_exp_new_term_notify(tra_func_plugin_t* self, term_t t, trail_token_t* prop) {
  const plugin_context_t* ctx = tra_get_context(self->tra);

  assert(term_kind(ctx->terms, t) == APP_TERM && tra_application_is_of_the_plugin(self, t));
  // Recall: for APP_TERM, arg[0] is the function symbol, arg[1] is the first argument
  assert(composite_term_arity(ctx->terms, t) == 2);

  if (t == tra_exp_e_term(self, t)) {
    term_t lower_bound = _o_yices_rational32(TRA_EXP_LEMMA_E_NUM - 1, TRA_EXP_LEMMA_E_DEN);
    term_t upper_bound = _o_yices_rational32(TRA_EXP_LEMMA_E_NUM, TRA_EXP_LEMMA_E_DEN);
    prop->lemma(prop, _o_yices_arith_geq_atom(t, lower_bound));
    prop->lemma(prop, _o_yices_arith_geq_atom(upper_bound, t));
  }

  term_t x = composite_term_arg(ctx->terms, t, 1);

  // 1 + x <= (exp x); higher odd degrees would be sharper
  prop->lemma(prop, _o_yices_arith_leq_atom(tra_exp_maclaurin(x, 1), t));

  // (exp x) > 0: the other lemmas imply it only for x > -1
  prop->lemma(prop, _o_yices_arith_gt0_atom(t));
}

/* =======================
    Let NA plugin decide
   ======================= */

// (exp 1) is decided before any other application of exp
static term_t tra_exp_let_na_decide(tra_func_plugin_t* self, term_t t) {
  const plugin_context_t* ctx = tra_get_context(self->tra);
  term_t e = tra_exp_e_term(self, t);
  if (t == e) return NULL_TERM;

  variable_t e_var = variable_db_get_variable_if_exists(ctx->var_db, e);
  assert(e_var != variable_null);
  return trail_has_value(ctx->trail, e_var) ? NULL_TERM : e;
}

/* ====================
    Implicit variables
   ==================== */

// (exp 1), for every application other than (exp 1)
static void tra_exp_get_implicit_variables(tra_func_plugin_t* self, term_t t, int_hset_t* vars) {
  term_t e = tra_exp_e_term(self, t);
  if (t != e) int_hset_add(vars, e);
}

/* ==========
    Evaluate
   ========== */

static void tra_exp_eval(tra_func_plugin_t* self, tra_itype_t* args, tra_prec_t prec, tra_itype_t out) {
  assert(prec != INF_PREC);

  self->abstract_domain->exp(out, args[0], prec);
}

/* ==================
    Refine precision
   ================== */

// The precision the argument needs for its image to have the given precision
static tra_prec_t tra_exp_refine_precision(tra_func_plugin_t* self, tra_itype_t* args, tra_prec_t precision) {
  assert(precision != INF_PREC);

  if (precision == NO_PREC) return NO_PREC;

  tra_ilib_t* lib = self->abstract_domain;

  mpfr_t lo, hi;
  mpfr_init2(lo, 64);
  mpfr_init2(hi, 64);
  lib->get_interval_overapproximation_mpfr(lo, hi, args[0]);

  tra_prec_t result = NO_PREC;
  tra_prec_t add = 0;

  if (mpfr_inf_p(hi) || mpfr_inf_p(lo) || mpfr_nan_p(lo) || mpfr_nan_p(hi)) goto done;

  // add = ceil(hi log2(e)), so that e^hi <= 2^add
  if (mpfr_cmp_ui(hi, 0) > 0) {
    add = mpfr_get_si(hi, MPFR_RNDU);
    assert(add >= 0);
    if (add == INF_PREC) goto done;

    double l = ceil(add * 1.4426950409); // 1.44... > log2(e)
    if (l >= 9223372036854775808.0 /* 0x1p63, == LONG_MAX + 1: would overflow tra_prec_t */) goto done;
    add = (tra_prec_t) l;
    if (add < 0) goto done;
  }

  // w(exp(J)) <= w(J) * e^hi <= w(J) * 2^add.
  // The trailing +1 brings the image down to 2^-(precision+1),
  // so that the abstract domain can achieve 'precision'.
  result = tra_add_precisions(tra_add_precisions(precision, add), 1);

 done:
  mpfr_clear(lo);
  mpfr_clear(hi);

  return result;
}

/* ===================
    Conflicts: Taylor
   =================== */

// Cap on the degree in the argument of the Taylor literals
#define TRA_EXP_TAYLOR_MAX_DEGREE 5

/*
 * Taylor strategy. If the trail value of t = (exp x) escapes the enclosure of t, pushes a
 * single literal, true on the trail, that negates a Taylor bound on exp at 0 of the least
 * degree up to TRA_EXP_TAYLOR_MAX_DEGREE.
 */
static bool tra_exp_taylor_conflict(tra_func_plugin_t* self, term_t t, ivector_t* conflict) {
  if (!tra_application_is_of_the_plugin(self, t)) return false;
  assert(conflict->size == 0);

  const plugin_context_t* ctx = tra_get_context(self->tra);
  tra_ilib_t* lib = self->abstract_domain;
  bool conflict_generated = false;

  tra_itype_t I_t = tra_get_abstraction(self->tra, t);
  if (I_t == NULL) return conflict_generated;

  mcsat_value_t t_val, arg_val;
  term_t arg = composite_term_arg(ctx->terms, t, 1);
  bool t_val_ok = term_evaluate(ctx, t, &t_val);
  bool arg_val_ok = term_evaluate(ctx, arg, &arg_val);
  assert(t_val_ok && arg_val_ok);

  tra_tv_position_t position = lib->trail_value_position(I_t, &t_val);
  if (position == TV_ABOVE || position == TV_BELOW) {
    // With r_n(y) = y^(n+1)/(n+1)!, the Lagrange remainder e^y - T_n(y) = e^xi r_n(y), for
    // some xi between 0 and y, gives for every y
    //   (L)  e^y >= T_n(y)               for odd n, as the remainder is >= 0
    //   (U)  e^y (1 - r_n(y)) <= T_n(y)  for even n, as the remainder is <= e^y r_n(y)
    // Both are sharp for y >= 0 and poor for y << 0, where T_n alternates. So y = x when
    // the trail value of x is >= 0, and y = -x otherwise, where e^y = 1/t.
    bool nonneg = lp_value_sgn(&arg_val.lp_value) >= 0;
    term_t one = _o_yices_rational32(1, 1);
    term_t y = nonneg ? arg : _o_yices_neg(arg);

    // e^y = num/den with den > 0, so the negations of the bounds are
    //   not (L):  den T_n(y) - num > 0
    //   not (U):  num (1 - r_n(y)) - den T_n(y) > 0
    term_t num = nonneg ? t : one;
    term_t den = nonneg ? one : t;
    bool upper = (position == TV_ABOVE) == nonneg; // e^y must go down: (U)
    uint32_t extra = upper ? 1 : 0; // the literal has degree n + extra in y

    for (uint32_t n = upper ? 0 : 1; n + extra <= TRA_EXP_TAYLOR_MAX_DEGREE; n += 2) {
      term_t taylor = tra_exp_maclaurin(y, n);
      term_t poly;
      if (upper) {
        term_t remainder = _o_yices_sub(tra_exp_maclaurin(y, n + 1), taylor); // r_n(y)
        poly = _o_yices_sub(_o_yices_mul(num, _o_yices_sub(one, remainder)), _o_yices_mul(den, taylor));
      } else {
        poly = _o_yices_sub(_o_yices_mul(den, taylor), num);
      }

      // The least n whose literal is true on the trail
      term_t literal = tra_conflict_cutting_literal(ctx, poly, _o_yices_rational32(0, 1), true);
      if (literal == NULL_TERM) continue;

      tra_trace_iprintf_if(self->tra, "tra::exp", "[exp: taylor: %s bound of degree %u at %s]\n",
                           upper ? "upper" : "lower", n, nonneg ? "x" : "-x");
      ivector_push(conflict, literal);
      conflict_generated = true;
      break;
    }
  }

  if (!conflict_generated) {
    tra_trace_iprintf_if(self->tra, "tra::exp", "[exp: taylor: no degree up to %d cuts the trail value]\n",
                         TRA_EXP_TAYLOR_MAX_DEGREE);
  }
  if (t_val_ok) mcsat_value_destruct(&t_val);
  if (arg_val_ok) mcsat_value_destruct(&arg_val);

  assert(!conflict_generated || conflict->size > 0);
  return conflict_generated;
}

/* =================
    Conflicts: Pade
   ================= */

/*
 * Pade strategy. If the trail value of t = (exp x) escapes the enclosure of t, pushes the
 * negation of a [1/1] or [2/2] Pade bound on the escaping side, with its guard on x, when
 * they are true on the trail.
 */
static bool tra_exp_pade_conflict(tra_func_plugin_t* self, term_t t, ivector_t* conflict) {
  if (!tra_application_is_of_the_plugin(self, t)) return false;
  assert(conflict->size == 0);

  const plugin_context_t* ctx = tra_get_context(self->tra);
  tra_ilib_t* lib = self->abstract_domain;
  bool conflict_generated = false;

  tra_itype_t I_t = tra_get_abstraction(self->tra, t);
  if (I_t == NULL) return conflict_generated;

  mcsat_value_t t_val, x_val;
  term_t x = composite_term_arg(ctx->terms, t, 1);
  bool t_val_ok = term_evaluate(ctx, t, &t_val);
  bool x_val_ok = term_evaluate(ctx, x, &x_val);
  assert(t_val_ok && x_val_ok);

  tra_tv_position_t position = lib->trail_value_position(I_t, &t_val);
  if (position == TV_ABOVE || position == TV_BELOW) {
    // With f_mm = tra_exp_pade_difference(t, x, m), at t = e^x f_11 has the sign of -x and
    // f_22 that of x, for every x (for x >= 2, t (2 - x) <= 0 < 2 + x). So:
    //   x >= 0:  f_11 <= 0 (upper bound)  and  f_22 >= 0 (lower bound)
    //   x <= 0:  f_11 >= 0 (lower bound)  and  f_22 <= 0 (upper bound)
    // The side of the escape and the sign of the trail value of x select one of them.
    bool above = (position == TV_ABOVE);
    bool nonneg = lp_value_sgn(&x_val.lp_value) >= 0;
    uint32_t m = (above == nonneg) ? 1 : 2;

    // The negation of the bound is f > 0 for an upper bound, -f > 0 for a lower one
    term_t f = tra_exp_pade_difference(t, x, m);
    term_t poly = above ? f : _o_yices_neg(f);

    // As in the eager lemmas, the upper [1/1] bound is also guarded by x < 2
    bool below_two_guard = above && nonneg;
    term_t two = _o_yices_rational32(2, 1);
    bool guard_holds = true;
    if (below_two_guard) {
      mpq_t two_q;
      mpq_init(two_q);
      mpq_set_ui(two_q, 2, 1);
      guard_holds = tra_value_cmp_mpq(&x_val, two_q) < 0;
      mpq_clear(two_q);
    }

    // The negation of the bound must hold on the trail
    mcsat_value_t poly_val;
    bool poly_val_ok = guard_holds && term_evaluate(ctx, poly, &poly_val);
    bool cuts = poly_val_ok && lp_value_sgn(&poly_val.lp_value) > 0;
    if (poly_val_ok) mcsat_value_destruct(&poly_val);

    term_t literal = cuts ? _o_yices_arith_gt0_atom(poly) : NULL_TERM;
    if (literal != NULL_TERM && literal != true_term && literal != false_term) {
      term_t guard = nonneg ? _o_yices_arith_geq0_atom(x) : _o_yices_arith_leq0_atom(x);
      if (guard != true_term) ivector_push(conflict, guard);
      if (below_two_guard) {
        term_t guard2 = _o_yices_arith_lt_atom(x, two);
        if (guard2 != true_term) ivector_push(conflict, guard2);
      }
      ivector_push(conflict, literal);
      conflict_generated = true;

      tra_trace_iprintf_if(self->tra, "tra::exp", "[exp: pade: [%u/%u] %s bound]\n", m, m, above ? "upper" : "lower");
    }
  }

  if (!conflict_generated) {
    tra_trace_iprintf_if(self->tra, "tra::exp", "[exp: pade: the bound does not cut the trail value]\n");
  }
  if (t_val_ok) mcsat_value_destruct(&t_val);
  if (x_val_ok) mcsat_value_destruct(&x_val);

  assert(!conflict_generated || conflict->size > 0);
  return conflict_generated;
}

/* ======================
    Conflicts: Hyperbola
   ====================== */

// Cap on the bit size of the constants of a hyperbola literal. Both the separation
// exponent p and the magnitude of e^x feed it; past the cap the literal is not worth
// building and the next strategies are tried instead.
#define TRA_EXP_HYPERBOLA_MAX_LITERAL_BITS 65536

// Initial precision of the mpfr extractions. Only a starting point: tra_interval_to_mpfr
// raises it as needed (see tra_abstract_values.h).
#define TRA_EXP_HYPERBOLA_MPFR_PREC_INIT 64

// Radius of the guard around the argument (see tra_exp_push_argument_guard)
#define TRA_EXP_HYPERBOLA_GUARD_RADIUS 1

// Bits beyond the precision of I(t) of the first literal tried (see tra_exp_hyperbola_cut)
#define TRA_EXP_HYPERBOLA_EXTRA_BITS 2

/*
 * Pushes the guard floor(x*) - W <= x <= ceil(x*) + W, where x* is the trail value of x and
 * W = TRA_EXP_HYPERBOLA_GUARD_RADIUS. Both literals are true on the trail. Nothing is pushed
 * for a constant x.
 */
static void tra_exp_push_argument_guard(term_t x, const mcsat_value_t* x_val, ivector_t* conflict) {
  if (x_val->type != VALUE_LIBPOLY) return;

  lp_integer_t lo, hi;
  lp_integer_construct(&lo);
  lp_integer_construct(&hi);
  lp_value_floor(&x_val->lp_value, &lo);
  lp_value_ceiling(&x_val->lp_value, &hi);
  mpz_sub_ui(&lo, &lo, TRA_EXP_HYPERBOLA_GUARD_RADIUS);
  mpz_add_ui(&hi, &hi, TRA_EXP_HYPERBOLA_GUARD_RADIUS);

  // x* is in the interior of the guard, as W >= 1
  mpq_t lo_q, hi_q;
  mpq_inits(lo_q, hi_q, NULL);
  mpq_set_z(lo_q, &lo);
  mpq_set_z(hi_q, &hi);
  assert(tra_value_cmp_mpq(x_val, lo_q) > 0 && tra_value_cmp_mpq(x_val, hi_q) < 0);
  tra_conflict_push_bounds(x, x_val, lo_q, hi_q, conflict);
  mpq_clears(lo_q, hi_q, NULL);
  lp_integer_destruct(&lo);
  lp_integer_destruct(&hi);
}

/*
 * The negation of a hyperbola bound on t = (exp x) at a centre x0 near the trail value x*,
 * with u = x - x0 and E a rational enclosing e^{x0} from the relevant side:
 *
 *   above:  (1 - u) t > E                          (E >= e^{x0})
 *   below:  t (t + 4E(1 - u)) - E^2 (2u + 5) < 0   (0 < E <= e^{x0})
 *
 * Here p is the precision of the bound at x* and 2^mbits bounds e^{x*}. Returns NULL_TERM,
 * and traces why when the reason is known, if the literal cannot be built or is not true on the trail.
 */
static term_t tra_exp_hyperbola_literal(tra_func_plugin_t* self, term_t t, const mcsat_value_t* t_val,
                                        term_t x, const mcsat_value_t* x_val,
                                        bool above, long p, long mbits) {
  const plugin_context_t* ctx = tra_get_context(self->tra);
  tra_ilib_t* lib = self->abstract_domain;

  // x* in [xlo, xhi] and e^{x0} in [elo, ehi], w is scratch for widths. I_x is the enclosure
  // of x*.
  mpfr_t xlo, xhi, elo, ehi, w;
  mpfr_ptr Eend;
  mpq_t x0, E, tmp;
  mcsat_value_t poly_val;
  bool poly_val_ok = false;
  term_t u, one_minus_u, poly, literal = NULL_TERM;
  tra_itype_t I_x = lib->alloc();

  mpfr_inits2(TRA_EXP_HYPERBOLA_MPFR_PREC_INIT, xlo, xhi, elo, ehi, w, (mpfr_ptr) 0);
  mpq_inits(x0, E, tmp, NULL);

  // Both bounds are theorems for every rational centre x0: the first negates the tangent
  // hyperbola, the second the contact-4 hyperbola. The centre only decides how sharp they
  // are. With d = x* - x0 and sigma the width of the enclosure of e^{x0}, the error of the
  // bound at x* is at most e^{x*} d^2 + 2 sigma above and e^{x0} d^4/4 + 2 sigma below, from
  // e^u/(1+u) <= 1 + u^2 and e^u - psi(u) <= u^4/4 for |u| <= 1/2 (psi being the lower bound
  // in units of E). So d <= 2^-(k+1) for 2k (resp. 4k) >= p + mbits, and sigma <= 2^-(p+3),
  // keep the error below 2^-(p+1).
  long k = above ? (p + mbits + 1) / 2 : (p + mbits + 3) / 4;

  // x_val comes from term_evaluate, which always tags it VALUE_LIBPOLY, and that
  // branch cannot fail: a value that is neither rational nor algebraic is the only
  // way out. The extraction below can fail, though, see tra_interval_to_mpfr.
  bool I_x_ok = tra_interval_around_value(lib, x_val, k + 1, I_x);
  assert(I_x_ok);
  (void) I_x_ok;
  if (!tra_interval_to_mpfr(lib, I_x, k + 1, xlo, xhi)) goto done;

  mpfr_sub(w, xhi, xlo, MPFR_RNDU);
  if (mpfr_cmp_ui_2exp(w, 1, -(k + 1)) > 0 ||
      (!mpfr_zero_p(xlo) && labs((long) mpfr_get_exp(xlo)) > TRA_EXP_HYPERBOLA_MAX_LITERAL_BITS)) {
    tra_trace_iprintf_if(self->tra, "tra::exp", "[exp: the centre is not accurate enough]\n");
    goto done;
  }
  // The centre is the lower end of the enclosure of x*, a dyadic
  mpfr_get_q(x0, xlo);
  if (!tra_enclose_unop_at(lib, lib->exp, x0, p + 3, p + 3, elo, ehi)) goto done;

  // E rounds e^{x0} the safe way for its side, an exact read of a dyadic either way.
  // Only that endpoint has to be positive, E must dominate e^{x0} > 0, the quadratic
  // needs E > 0, so an elo that underflowed to zero does not stop the upper literal.
  Eend = above ? ehi : elo;

  // arb_ilib_exp drops to its minimum precision for an argument whose magnitude does
  // not fit a double, so the width is checked, not trusted.
  mpfr_sub(w, ehi, elo, MPFR_RNDU);
  if (mpfr_cmp_ui_2exp(w, 1, -(p + 3)) > 0 || mpfr_sgn(Eend) <= 0 ||
      labs((long) mpfr_get_exp(Eend)) > TRA_EXP_HYPERBOLA_MAX_LITERAL_BITS) {
    tra_trace_iprintf_if(self->tra, "tra::exp", "[exp: the enclosure of the centre's image is not sharp enough]\n");
    goto done;
  }
  mpfr_get_q(E, Eend);

  u = _o_yices_sub(x, _o_yices_mpq(x0));
  one_minus_u = _o_yices_sub(_o_yices_rational32(1, 1), u);

  if (above) {
    poly = _o_yices_sub(_o_yices_mul(one_minus_u, t), _o_yices_mpq(E));
    literal = _o_yices_arith_gt0_atom(poly); // (1 - u) t > E
  } else {
    mpq_mul_2exp(tmp, E, 2);
    poly = _o_yices_mul(t, _o_yices_add(t, _o_yices_mul(_o_yices_mpq(tmp), one_minus_u))); // t (t + 4E(1 - u))
    mpq_mul(tmp, E, E);
    poly = _o_yices_sub(poly, _o_yices_mul(_o_yices_mpq(tmp),
                                           _o_yices_add(_o_yices_mul(_o_yices_rational32(2, 1), u),
                                                        _o_yices_rational32(5, 1)))); // t (t + 4E(1 - u)) - E^2 (2u + 5)
    literal = _o_yices_arith_lt0_atom(poly); // t (t + 4E(1 - u)) - E^2 (2u + 5) < 0
  }

  if (tra_trace_enabled(self->tra, "tra::exp")) {
    tra_trace_iprintf(self->tra, "[exp: hyperbola parameters: p = %ld, mbits = %ld, k = %ld, x0 = ", p, mbits, k);
    mpq_out_str(ctx_trace_out(ctx), 10, x0);
    tra_trace_printf(self->tra, "]\n");
  }

  // Verify that the literal is true on the trail
  poly_val_ok = term_evaluate(ctx, poly, &poly_val);
  if (!poly_val_ok || lp_value_sgn(&poly_val.lp_value) != (above ? 1 : -1)) {
    tra_trace_iprintf_if(self->tra, "tra::exp", "[exp: the hyperbola bound does not cut the trail value]\n");
    literal = NULL_TERM;
  }

 done:
  if (poly_val_ok) mcsat_value_destruct(&poly_val);
  mpq_clears(x0, E, tmp, NULL);
  mpfr_clears(xlo, xhi, elo, ehi, w, (mpfr_ptr) 0);
  lib->free(I_x);

  return literal;
}

/*
 * A hyperbola literal (see tra_exp_hyperbola_literal) true on the trail, for t = (exp x) whose
 * positive trail value t_val escapes the enclosure I_t of t, rounded outwards to [lo, hi].
 * Returns NULL_TERM, and traces why when the reason is known, if there is none.
 */
static term_t tra_exp_hyperbola_cut(tra_func_plugin_t* self, term_t t, tra_itype_t I_t,
                                    const mpfr_t lo, const mpfr_t hi, const mcsat_value_t* t_val,
                                    term_t x, const mcsat_value_t* x_val) {
  tra_ilib_t* lib = self->abstract_domain;
  term_t literal = NULL_TERM;
  mpq_t H;
  mpq_init(H);

  // mpfr_get_q writes out ~|exponent| bits, so an extreme endpoint is refused rather than
  // materialized (as tra_mpfr_to_q_is_safe does in tra_conflict_utils.c, with a smaller bound).
  // A nonpositive hi is unusable.
  if (mpfr_sgn(hi) <= 0 || labs((long) mpfr_get_exp(hi)) > TRA_EXP_HYPERBOLA_MAX_LITERAL_BITS ||
      (!mpfr_zero_p(lo) && labs((long) mpfr_get_exp(lo)) > TRA_EXP_HYPERBOLA_MAX_LITERAL_BITS)) {
    tra_trace_iprintf_if(self->tra, "tra::exp", "[exp: the enclosure of the term is unusable]\n");
    goto done;
  }

  // H is the endpoint of [lo, hi] past which t_val escapes. As [lo, hi] rounds I(t)
  // outwards, a value outside I(t) could still be inside [lo, hi].
  bool above = tra_conflict_escape_endpoint(t_val, lo, hi, H);
  if (!above && tra_value_cmp_mpq(t_val, H) >= 0) {
    tra_trace_iprintf_if(self->tra, "tra::exp", "[exp: the trail value does not escape the rounded enclosure]\n");
    goto done;
  }

  // The least power of two p with |t_val - H| > 2^-p. As e^{x*} lies in [lo, hi], a bound
  // of precision p falls strictly between e^{x*} and t_val: its literal cuts.
  long p;
  for (p = 1; p <= TRA_EXP_HYPERBOLA_MAX_LITERAL_BITS && tra_value_close_mpq(t_val, H, p); p *= 2);
  long mbits = tra_mpfr_magnitude_bits(hi);
  if (p + mbits > TRA_EXP_HYPERBOLA_MAX_LITERAL_BITS) {
    tra_trace_iprintf_if(self->tra, "tra::exp", "[exp: the literal would be too large]\n");
    goto done;
  }

  // First the precision of I(t) plus TRA_EXP_HYPERBOLA_EXTRA_BITS. As I(t) usually encloses
  // e^{x*} well inside, this literal usually cuts as well, and its bound then falls inside
  // I(t): the next trail value of t, between e^{x*} and the bound, needs no new conflict.
  tra_prec_t prec_t = lib->get_precision(I_t);
  if (prec_t >= 1 && prec_t <= TRA_EXP_HYPERBOLA_MAX_LITERAL_BITS) {
    long p_first = prec_t + TRA_EXP_HYPERBOLA_EXTRA_BITS;
    if (p_first + mbits <= TRA_EXP_HYPERBOLA_MAX_LITERAL_BITS && p_first != p) {
      literal = tra_exp_hyperbola_literal(self, t, t_val, x, x_val, above, p_first, mbits);
    }
  }
  if (literal == NULL_TERM) literal = tra_exp_hyperbola_literal(self, t, t_val, x, x_val, above, p, mbits);

 done:
  mpq_clear(H);
  return literal;
}

/*
 * Hyperbola strategy. If the trail value of t = (exp x) escapes the enclosure of t, pushes a
 * hyperbola literal true on the trail (see tra_exp_hyperbola_cut), and the guard of
 * tra_exp_push_argument_guard around x. A nonpositive trail value is cut by t <= 0 alone.
 */
static bool tra_exp_hyperbola_conflict(tra_func_plugin_t* self, term_t t, ivector_t* conflict) {
  if (!tra_application_is_of_the_plugin(self, t)) return false;
  assert(conflict->size == 0);

  const plugin_context_t* ctx = tra_get_context(self->tra);
  tra_ilib_t* lib = self->abstract_domain;
  bool conflict_generated = false;

  tra_itype_t I_t = tra_get_abstraction(self->tra, t);
  if (I_t == NULL) return conflict_generated;

  mcsat_value_t t_val;
  bool t_val_ok = term_evaluate(ctx, t, &t_val);
  assert(t_val_ok); // a conflict term always has a trail value

  term_t x = composite_term_arg(ctx->terms, t, 1);
  mcsat_value_t x_val;
  bool x_val_ok = term_evaluate(ctx, x, &x_val);
  assert(x_val_ok);

  term_t literal = NULL_TERM;
  bool guarded = false;
  mpfr_t lo, hi;
  mpfr_inits2(TRA_EXP_HYPERBOLA_MPFR_PREC_INIT, lo, hi, (mpfr_ptr) 0);

  if (!tra_interval_to_mpfr(lib, I_t, NO_PREC, lo, hi)) goto done;

  if (lp_value_sgn(&t_val.lp_value) <= 0) {
    literal = _o_yices_arith_leq0_atom(t); // negates exp > 0, a theorem everywhere
  } else {
    // The bound is sharp only near x*: guarded, the learned clause acts only there,
    // instead of being a unit clause over the whole line
    literal = tra_exp_hyperbola_cut(self, t, I_t, lo, hi, &t_val, x, &x_val);
    if (literal == NULL_TERM) goto done;
    guarded = true;
  }

  if (literal == NULL_TERM || literal == true_term || literal == false_term) {
    tra_trace_iprintf_if(self->tra, "tra::exp", "[exp: the literal degenerated]\n");
    goto done;
  }
  if (guarded) tra_exp_push_argument_guard(x, &x_val, conflict);
  ivector_push(conflict, literal);
  conflict_generated = true;

 done:
  if (t_val_ok) mcsat_value_destruct(&t_val);
  if (x_val_ok) mcsat_value_destruct(&x_val);
  mpfr_clears(lo, hi, (mpfr_ptr) 0);

  assert(!conflict_generated || conflict->size > 0);
  return conflict_generated;
}

/* =====================
    Conflicts: Preimage
   ===================== */

// Bounds on ln 2 = 0.69314718...: LOWER/DEN < ln 2 < UPPER/DEN
#define TRA_EXP_PREIMAGE_LN2_LOWER 6931471UL
#define TRA_EXP_PREIMAGE_LN2_UPPER 6931472UL
#define TRA_EXP_PREIMAGE_LN2_DEN 10000000UL

/*
 * Preimage strategy. If the trail value v > 0 of t = (exp x) lies below the enclosure of t,
 * pushes { t <= 2^k, x > l }, with 2^k the least power of two >= v and l an integer >= k ln 2,
 * when x* > l. Symmetrically above: { t >= 2^k, x < l }, with 2^k the greatest power of two
 * <= v and l an integer <= k ln 2, when x* < l.
 */
static bool tra_exp_preimage_conflict(tra_func_plugin_t* self, term_t t, ivector_t* conflict) {
  if (!tra_application_is_of_the_plugin(self, t)) return false;
  assert(conflict->size == 0);

  const plugin_context_t* ctx = tra_get_context(self->tra);
  tra_ilib_t* lib = self->abstract_domain;
  bool conflict_generated = false;

  tra_itype_t I_t = tra_get_abstraction(self->tra, t);
  if (I_t == NULL) return conflict_generated;

  mcsat_value_t t_val, x_val;
  term_t x = composite_term_arg(ctx->terms, t, 1);
  bool t_val_ok = term_evaluate(ctx, t, &t_val);
  bool x_val_ok = term_evaluate(ctx, x, &x_val);
  assert(t_val_ok && x_val_ok);

  // c = 2^k and l as above, next the neighbouring power of two
  mpq_t c, next, l;
  mpz_t z;
  mpq_inits(c, next, l, NULL);
  mpz_init(z);

  // The conflict holds as x > l implies e^x > e^l >= 2^k (below). Its constants come from
  // v, not from e^{x*}: they stay small when x* is huge and v is not, where the other
  // strategies need numbers the size of e^{x*}. It applies only when x* and ln v are about
  // 2 or more apart.
  tra_tv_position_t position = lib->trail_value_position(I_t, &t_val);
  double v = lp_value_to_double(&t_val.lp_value);
  if ((position == TV_ABOVE || position == TV_BELOW) && v > 0 && isfinite(v)) {
    bool below = (position == TV_BELOW);

    // k from the double (2^(e-1) <= v < 2^e), then made exact: below, the least k with
    // 2^k >= v; above, the greatest k with 2^k <= v
    int e;
    frexp(v, &e);
    long k = below ? e : e - 1;
    mpq_set_ui(c, 1, 1);
    if (k >= 0) mpq_mul_2exp(c, c, (mp_bitcnt_t) k);
    else mpq_div_2exp(c, c, (mp_bitcnt_t) -k);
    if (below) {
      for (; tra_value_cmp_mpq(&t_val, c) > 0; k++) mpq_mul_2exp(c, c, 1);
      for (mpq_div_2exp(next, c, 1); tra_value_cmp_mpq(&t_val, next) <= 0; k--) {
        mpq_set(c, next);
        mpq_div_2exp(next, c, 1);
      }
    } else {
      for (; tra_value_cmp_mpq(&t_val, c) < 0; k--) mpq_div_2exp(c, c, 1);
      for (mpq_mul_2exp(next, c, 1); tra_value_cmp_mpq(&t_val, next) >= 0; k++) {
        mpq_set(c, next);
        mpq_mul_2exp(next, c, 1);
      }
    }

    // l = ceil (below) or floor (above) of k times the bound on ln 2 that keeps
    // l >= k ln 2 (below) or l <= k ln 2 (above)
    unsigned long ln2 = ((k >= 0) == below) ? TRA_EXP_PREIMAGE_LN2_UPPER : TRA_EXP_PREIMAGE_LN2_LOWER;
    mpz_set_si(z, k);
    mpz_mul_ui(z, z, ln2);
    if (below) mpz_cdiv_q_ui(z, z, TRA_EXP_PREIMAGE_LN2_DEN);
    else mpz_fdiv_q_ui(z, z, TRA_EXP_PREIMAGE_LN2_DEN);
    mpq_set_z(l, z);

    // Both literals must hold on the trail: t* is on the right side of c by construction
    int cmp = tra_value_cmp_mpq(&x_val, l);
    if (below ? cmp > 0 : cmp < 0) {
      term_t t_literal = below ? _o_yices_arith_leq_atom(t, _o_yices_mpq(c))
                               : _o_yices_arith_geq_atom(t, _o_yices_mpq(c));
      term_t x_literal = below ? _o_yices_arith_gt_atom(x, _o_yices_mpq(l))
                               : _o_yices_arith_lt_atom(x, _o_yices_mpq(l));
      if (t_literal != true_term && t_literal != false_term && x_literal != false_term) {
        ivector_push(conflict, t_literal);
        if (x_literal != true_term) ivector_push(conflict, x_literal); // true for a constant argument
        conflict_generated = true;

        if (tra_trace_enabled(self->tra, "tra::exp")) {
          tra_trace_iprintf(self->tra, "[exp: preimage: t %s 2^%ld, x %s ", below ? "<=" : ">=", k, below ? ">" : "<");
          mpq_out_str(ctx_trace_out(ctx), 10, l);
          tra_trace_printf(self->tra, "]\n");
        }
      }
    }
  }

  if (!conflict_generated) tra_trace_iprintf_if(self->tra, "tra::exp", "[exp: preimage: x* and ln v too close]\n");
  mpz_clear(z);
  mpq_clears(c, next, l, NULL);
  if (t_val_ok) mcsat_value_destruct(&t_val);
  if (x_val_ok) mcsat_value_destruct(&x_val);

  assert(!conflict_generated || conflict->size > 0);
  return conflict_generated;
}

/* ===================
    Conflicts: Atoms
   =================== */

// The trail values of the argument and of an application of exp
typedef struct { mcsat_value_t arg, app; } tra_exp_values_t;

/*
 * Let A be the applications of exp in V(atom), without (exp 1) unless with_e, whose argument and
 * value are on the trail, in the order of V(atom).
 * Modifies: *app, and *val if val is not NULL: arrays allocated here, which the caller frees
 *           (after destructing the values of *val).
 * Ensures:  result = |A|; (*app)[0..result) = A; if val is not NULL, (*val)[i] holds the trail
 *           values of the argument and of (*app)[i].
 */
static uint32_t tra_exp_atom_applications(tra_func_plugin_t* self, term_t atom, bool with_e,
                                          term_t** app, tra_exp_values_t** val) {
  const plugin_context_t* ctx = tra_get_context(self->tra);
  const int_hset_t* vars = tra_get_term_variables(self->tra, atom);
  uint32_t n = (vars == NULL) ? 0 : vars->nelems;
  *app = (term_t*) safe_malloc(n * sizeof(term_t));
  if (val != NULL) *val = (tra_exp_values_t*) safe_malloc(n * sizeof(tra_exp_values_t));

  tra_exp_values_t scratch; // the values, when the caller wants none
  uint32_t k = 0;
  for (uint32_t i = 0; i < n; i++) {
    term_t s = vars->data[i];
    if (!tra_application_is_of_the_plugin(self, s) || (!with_e && s == tra_exp_e_term(self, s))) continue;
    tra_exp_values_t* v = (val != NULL) ? &(*val)[k] : &scratch;
    if (!term_evaluate(ctx, composite_term_arg(ctx->terms, s, 1), &v->arg)) continue;
    if (!term_evaluate(ctx, s, &v->app)) {
      mcsat_value_destruct(&v->arg);
      continue;
    }
    if (val == NULL) {
      mcsat_value_destruct(&v->arg);
      mcsat_value_destruct(&v->app);
    }
    (*app)[k++] = s;
  }
  return k;
}

/*
 * Monotonicity strategy, for an atom. If two applications (exp a) and (exp b) in the atom
 * have trail values in the wrong order, pushes
 *
 *   [a < b, (exp a) >= (exp b)]    when a* < b* and (exp a)* >= (exp b)*
 *   [a <= b, (exp a) > (exp b)]    when a* = b* and (exp a)* > (exp b)*
 *
 * The literals are linear, and the conflict does not depend on the abstractions (hence on delta).
 */
static bool tra_exp_monotonicity_conflict(tra_func_plugin_t* self, term_t atom, ivector_t* conflict) {
  if(!is_boolean_term(tra_get_context(self->tra)->terms, atom)) return false;
  assert(conflict->size == 0);

  const plugin_context_t* ctx = tra_get_context(self->tra);
  term_t* app;
  tra_exp_values_t* val;
  uint32_t k = tra_exp_atom_applications(self, atom, true, &app, &val);

  bool conflict_generated = false;
  for (uint32_t i = 0; i < k && !conflict_generated; i++) {
    for (uint32_t j = i + 1; j < k && !conflict_generated; j++) {
      int c = lp_value_cmp(&val[i].arg.lp_value, &val[j].arg.lp_value);
      int d = lp_value_cmp(&val[i].app.lp_value, &val[j].app.lp_value);

      // x is the application with the smaller argument, or the larger value for equal arguments
      uint32_t x = i, y = j;
      if (c > 0 || (c == 0 && d < 0)) { x = j; y = i; c = -c; d = -d; }
      if (c < 0 ? d < 0 : d == 0) continue; // the order agrees with monotonicity

      term_t a = composite_term_arg(ctx->terms, app[x], 1);
      term_t b = composite_term_arg(ctx->terms, app[y], 1);
      term_t arg_literal = (c < 0) ? _o_yices_arith_lt_atom(a, b) : _o_yices_arith_leq_atom(a, b);
      term_t app_literal = (c < 0) ? _o_yices_arith_geq_atom(app[x], app[y]) : _o_yices_arith_gt_atom(app[x], app[y]);
      if (arg_literal == false_term || app_literal == true_term || app_literal == false_term) continue;

      // a < b is true_term for constant arguments: then the conflict is the other literal alone
      if (arg_literal != true_term) ivector_push(conflict, arg_literal);
      ivector_push(conflict, app_literal);
      conflict_generated = true;

      if (tra_trace_enabled(self->tra, "tra::exp")) {
        tra_trace_iprintf(self->tra, "[exp: monotonicity: ");
        tra_trace_print_term(self->tra, app[x]);
        tra_trace_printf(self->tra, " and ");
        tra_trace_print_term(self->tra, app[y]);
        tra_trace_printf(self->tra, "]\n");
      }
    }
  }

  for (uint32_t i = 0; i < k; i++) {
    mcsat_value_destruct(&val[i].arg);
    mcsat_value_destruct(&val[i].app);
  }
  safe_free(app);
  safe_free(val);

  assert(!conflict_generated || conflict->size > 0);
  return conflict_generated;
}

/*
 * Requires: literal is true on the trail.
 * Let G be the guard poly <= 0.
 * Ensures:  result <==> G is true on the trail (and is not false_term);
 *           result ==> G, unless it is true_term, then literal are appended to conflict;
 *           !result ==> conflict is unchanged.
 */
static bool tra_exp_push_guarded(const plugin_context_t* ctx, term_t poly, term_t literal, ivector_t* conflict) {
  term_t guard = _o_yices_arith_leq0_atom(poly);
  if (guard == false_term) return false;
  if (guard != true_term) {
    mcsat_value_t poly_val;
    if (!term_evaluate(ctx, poly, &poly_val)) return false;
    bool holds = lp_value_sgn(&poly_val.lp_value) <= 0;
    mcsat_value_destruct(&poly_val);
    if (!holds) return false;
    ivector_push(conflict, guard);
  }
  ivector_push(conflict, literal);
  return true;
}

/*
 * Constants of the ratio strategy:
 *  - MAX_BITS caps k, a power of two: a finer δ gives a local cut, which the strategies on
 *    applications do better;
 *  - MAX_DELTA caps |u* - v*|, as κ >= e^δ has about 1.45 |δ| bits;
 *  - κ has k + EXTRA_BITS + 1 bits, hence is within a factor 1 + 2^-(k + EXTRA_BITS) of e^δ;
 *  - δ = c/2^k with |c| <= 2^14 (by the caps) is exact in mpfr at DELTA_PREC bits.
 */
#define TRA_EXP_RATIO_MAX_BITS 4
#define TRA_EXP_RATIO_MAX_DELTA 1024
#define TRA_EXP_RATIO_EXTRA_BITS 8
#define TRA_EXP_RATIO_DELTA_PREC 32

/*
 * Ratio strategy, for an atom. As (exp u) = (exp v) e^(u-v), if e^δ <= κ then u - v <= δ
 * implies (exp u) <= κ (exp v). For two applications (exp u) and (exp v) in the atom, pushes
 *
 *   [u - v <= δ, (exp u) > κ (exp v)]
 *
 * with δ = ceil(2^k (u* - v*)) / 2^k and a rational κ >= e^δ, for the least k in 0, 1, 2, 4, ...,
 * TRA_EXP_RATIO_MAX_BITS whose literals are true on the trail: the coarser δ, the larger the
 * region that the clause excludes. Like monotonicity, it does not depend on the abstractions.
 */
static bool tra_exp_ratio_conflict(tra_func_plugin_t* self, term_t atom, ivector_t* conflict) {
  if(!is_boolean_term(tra_get_context(self->tra)->terms, atom)) return false;
  assert(conflict->size == 0);

  const plugin_context_t* ctx = tra_get_context(self->tra);
  term_t* app;
  uint32_t n = tra_exp_atom_applications(self, atom, false, &app, NULL);

  mpz_t C, c;
  mpq_t delta, kappa;
  mpfr_t delta_fr, kappa_fr;
  mpz_inits(C, c, NULL);
  mpq_inits(delta, kappa, NULL);
  mpfr_inits2(TRA_EXP_RATIO_DELTA_PREC, delta_fr, kappa_fr, (mpfr_ptr) 0);
  term_t scale = _o_yices_rational32(1 << TRA_EXP_RATIO_MAX_BITS, 1); // 2^K

  bool conflict_generated = false;
  for (uint32_t i = 0; i < n && !conflict_generated; i++) {
    for (uint32_t j = 0; j < n && !conflict_generated; j++) {
      if (i == j) continue;
      // C = ceil(2^K (u* - v*)), K = TRA_EXP_RATIO_MAX_BITS. Then c = ceil(C / 2^(K-k)) is
      // ceil(2^k (u* - v*)), as ceil(ceil(y) / m) = ceil(y / m) for every integer m >= 1.
      term_t d = _o_yices_sub(composite_term_arg(ctx->terms, app[i], 1), composite_term_arg(ctx->terms, app[j], 1));
      mcsat_value_t y;
      if (!term_evaluate(ctx, _o_yices_mul(scale, d), &y)) continue;
      lp_value_ceiling(&y.lp_value, C);
      mcsat_value_destruct(&y);
      // |C| <= 2^K cap iff |u* - v*| <= cap, up to rounding
      if (mpz_cmpabs_ui(C, TRA_EXP_RATIO_MAX_DELTA << TRA_EXP_RATIO_MAX_BITS) > 0) continue;

      for (uint32_t k = 0; k <= TRA_EXP_RATIO_MAX_BITS && !conflict_generated; k = (k == 0) ? 1 : 2 * k) {
        mpz_cdiv_q_2exp(c, C, TRA_EXP_RATIO_MAX_BITS - k);
        mpq_set_z(delta, c);
        mpq_div_2exp(delta, delta, k);

        // κ = e^δ rounded up: MPFR rounds correctly, so κ >= e^δ
        int exact = mpfr_set_q(delta_fr, delta, MPFR_RNDN);
        assert(exact == 0);
        (void) exact;
        mpfr_set_prec(kappa_fr, k + TRA_EXP_RATIO_EXTRA_BITS + 1);
        mpfr_exp(kappa_fr, delta_fr, MPFR_RNDU);
        mpfr_get_q(kappa, kappa_fr);

        term_t literal = tra_conflict_cutting_literal(ctx, app[i], _o_yices_mul(_o_yices_mpq(kappa), app[j]), true);
        if (literal == NULL_TERM) continue;
        conflict_generated = tra_exp_push_guarded(ctx, _o_yices_sub(d, _o_yices_mpq(delta)), literal, conflict);

        if (conflict_generated && tra_trace_enabled(self->tra, "tra::exp")) {
          tra_trace_iprintf(self->tra, "[exp: ratio: ");
          tra_trace_print_term(self->tra, app[i]);
          tra_trace_printf(self->tra, " and ");
          tra_trace_print_term(self->tra, app[j]);
          tra_trace_printf(self->tra, ", k = %u]\n", k);
        }
      }
    }
  }

  mpz_clears(C, c, NULL);
  mpq_clears(delta, kappa, NULL);
  mpfr_clears(delta_fr, kappa_fr, (mpfr_ptr) 0);
  safe_free(app);

  assert(!conflict_generated || conflict->size > 0);
  return conflict_generated;
}

// Precision of the mpfr bounds on ln(c/A) in tra_exp_mean_conflict
#define TRA_EXP_MEAN_LOG_PREC 64

/*
 * Jensen and Hoeffding strategies, for lhs against c (exp w).
 * Requires: conflict is empty; c > 0; lhs is a sum of k >= 2 monomials α_i (exp u_i), with α_i > 0
 *           and (exp u_i) owned by self; target is an application (exp w) owned by self, or
 *           NULL_TERM for (exp 0) = 1.
 * Let A = sum_i α_i, m = sum_i (α_i/A) u_i and s = m + (sum_i u_i^2 - (sum_i u_i)^2 / k) / 4.
 * Modifies: conflict.
 * Ensures:  result ==> conflict is one of the following, without the guard if it is true_term,
 *                      and its literals are true on the trail:
 *             [s - w - λ <= 0, lhs > c (exp w)]    with λ <= ln(c/A)    (Hoeffding)
 *             [w + λ - m <= 0, lhs < c (exp w)]    with λ >= ln(c/A)    (Jensen)
 *           where > and < are >= and <= if c != A;
 *           !result ==> conflict is empty.
 *
 * Soundness. lhs = A sum_i p_i e^{u_i} with p_i = α_i/A. Jensen gives lhs >= A e^m. Hoeffding's
 * lemma gives lhs <= A e^{m + (max u_i - min u_i)^2/8} <= A e^s, as (max - min)^2 <= 2 sum_i (u_i - ū)^2.
 * Hence each guard, with A e^λ <= c (resp. >= c), refutes the literal. For c != A, λ is rational
 * and ln(c/A) is not (Lindemann): A e^λ != c, so the non-strict literal is refuted too.
 */
static bool tra_exp_mean_conflict(tra_func_plugin_t* self, term_t lhs, term_t target, const mpq_t c,
                                  ivector_t* conflict) {
  const plugin_context_t* ctx = tra_get_context(self->tra);
  term_table_t* terms = ctx->terms;
  assert(conflict->size == 0 && mpq_sgn(c) > 0 && term_is_sum(terms, lhs) && term_num_children(terms, lhs) >= 2);
  assert(target == NULL_TERM || tra_application_is_of_the_plugin(self, target));
  term_t bound = (target == NULL_TERM) ? _o_yices_mpq(c) : _o_yices_mul(_o_yices_mpq(c), target);
  mcsat_value_t d_val;
  if (!term_evaluate(ctx, _o_yices_sub(lhs, bound), &d_val)) return false;
  int sgn = lp_value_sgn(&d_val.lp_value); // the sign of lhs - c (exp w) on the trail
  mcsat_value_destruct(&d_val);

  uint32_t k = term_num_children(terms, lhs);

  // a = A and ratio = c/A; q is a scratch rational, read into a term right after it is set
  mpq_t a, ratio, q;
  mpq_inits(a, ratio, q, NULL);
  term_t am = _o_yices_rational32(0, 1), sum = am, squares = am; // A m, sum_i u_i, sum_i u_i^2
  for (uint32_t i = 0; i < k; i++) {
    term_t x;
    sum_term_component(terms, lhs, i, q, &x);
    assert(x != NULL_TERM && tra_application_is_of_the_plugin(self, x) && mpq_sgn(q) > 0);
    mpq_add(a, a, q);
    term_t u = composite_term_arg(terms, x, 1);
    am = _o_yices_add(am, _o_yices_mul(_o_yices_mpq(q), u));
    sum = _o_yices_add(sum, u);
    squares = _o_yices_add(squares, _o_yices_square(u));
  }
  bool strict = mpq_equal(a, c);
  mpq_inv(q, a);
  term_t m = _o_yices_mul(_o_yices_mpq(q), am);
  term_t spread = _o_yices_sub(squares, _o_yices_mul(_o_yices_rational32(1, k), _o_yices_square(sum)));
  term_t s = _o_yices_add(m, _o_yices_mul(_o_yices_rational32(1, 4), spread));
  mpq_div(ratio, c, a);

  mpfr_t lambda;
  mpfr_init2(lambda, TRA_EXP_MEAN_LOG_PREC);
  bool conflict_generated = false;
  for (int pass = 0; pass < 2 && !conflict_generated; pass++) {
    // Hoeffding refutes lhs > c (exp w), Jensen lhs < c (exp w); on equality, only the non-strict ones
    bool hoeffding = (pass == 0);
    if ((hoeffding ? sgn < 0 : sgn > 0) || (sgn == 0 && strict)) continue;

    // λ rounded down for Hoeffding and up for Jensen: mpfr_set_q and mpfr_log round correctly
    mpfr_rnd_t rnd = hoeffding ? MPFR_RNDD : MPFR_RNDU;
    mpfr_set_q(lambda, ratio, rnd);
    mpfr_log(lambda, lambda, rnd);
    mpfr_get_q(q, lambda);
    term_t w_lambda = _o_yices_mpq(q);
    if (target != NULL_TERM) w_lambda = _o_yices_add(composite_term_arg(terms, target, 1), w_lambda);

    term_t literal = hoeffding ? (strict ? _o_yices_arith_gt_atom(lhs, bound) : _o_yices_arith_geq_atom(lhs, bound))
                               : (strict ? _o_yices_arith_lt_atom(lhs, bound) : _o_yices_arith_leq_atom(lhs, bound));
    assert(literal != true_term && literal != false_term); // lhs - c (exp w) is not constant
    term_t guard_poly = hoeffding ? _o_yices_sub(s, w_lambda) : _o_yices_sub(w_lambda, m);
    conflict_generated = tra_exp_push_guarded(ctx, guard_poly, literal, conflict);

    if (conflict_generated && tra_trace_enabled(self->tra, "tra::exp")) {
      tra_trace_iprintf(self->tra, "[exp: sum: %s: ", hoeffding ? "hoeffding" : "jensen");
      tra_trace_print_term(self->tra, literal);
      tra_trace_printf(self->tra, "]\n");
    }
  }

  mpfr_clear(lambda);
  mpq_clears(a, ratio, q, NULL);
  return conflict_generated;
}

/*
 * Sum strategy, for an atom. Applies tra_exp_mean_conflict to:
 *  - pairs: for two applications (exp u) and (exp v) in the atom, lhs = p (exp u) + (1-p) (exp v),
 *    against each other application in the atom, with c = 1. Here p = α/(α+β) if the atom is a
 *    sum whose coefficients α of (exp u) and β of (exp v) have the same sign, and p = 1/2 otherwise;
 *  - groups, if the atom is a sum: for each monomial γ t of the sum, with t an application of exp
 *    or the constant 1, lhs is the sum of the monomials |α| (exp u) of the sum with α γ < 0, when
 *    there are at least two, and c = |γ|.
 */
static bool tra_exp_sum_conflict(tra_func_plugin_t* self, term_t atom, ivector_t* conflict) {
  if(!is_boolean_term(tra_get_context(self->tra)->terms, atom)) return false;
  assert(conflict->size == 0);

  const plugin_context_t* ctx = tra_get_context(self->tra);
  term_table_t* terms = ctx->terms;
  term_t* app;
  uint32_t n = tra_exp_atom_applications(self, atom, false, &app, NULL);

  // The sum of the atom, if any, and its number of summands
  term_kind_t kind = term_kind(terms, atom);
  term_t sum = (kind == ARITH_GE_ATOM || kind == ARITH_EQ_ATOM) ? arith_atom_arg(terms, atom) : NULL_TERM;
  uint32_t summands = (sum != NULL_TERM && term_is_sum(terms, sum)) ? term_num_children(terms, sum) : 0;

  mpq_t p, q, total, coeff, gamma_q, one;
  mpq_inits(p, q, total, coeff, gamma_q, one, NULL);
  mpq_set_ui(one, 1, 1);

  bool conflict_generated = false;
  for (uint32_t i = 0; i < n && !conflict_generated; i++) {
    for (uint32_t j = i + 1; j < n && !conflict_generated; j++) {
      // p = α/(α+β) and q = 1 - p = β/(α+β), computed in place from p = α and q = β
      mpq_set_ui(p, 0, 1);
      mpq_set_ui(q, 0, 1);
      for (uint32_t h = 0; h < summands; h++) {
        term_t child;
        sum_term_component(terms, sum, h, coeff, &child);
        if (child == app[i]) mpq_set(p, coeff);
        if (child == app[j]) mpq_set(q, coeff);
      }
      if (mpq_sgn(p) * mpq_sgn(q) <= 0) {
        mpq_set_ui(p, 1, 1);
        mpq_set_ui(q, 1, 1);
      }
      mpq_add(total, p, q);
      mpq_div(p, p, total);
      mpq_div(q, q, total);
      assert(mpq_sgn(p) > 0 && mpq_sgn(q) > 0);

      term_t lhs = _o_yices_add(_o_yices_mul(_o_yices_mpq(p), app[i]), _o_yices_mul(_o_yices_mpq(q), app[j]));
      for (uint32_t l = 0; l < n && !conflict_generated; l++) {
        if (l != i && l != j) conflict_generated = tra_exp_mean_conflict(self, lhs, app[l], one, conflict);
      }
    }
  }

  ivector_t group;
  init_ivector(&group, 0);
  for (uint32_t g = 0; g < summands && !conflict_generated; g++) {
    term_t target;
    sum_term_component(terms, sum, g, gamma_q, &target);
    if (target != NULL_TERM && !tra_application_is_of_the_plugin(self, target)) continue;
    ivector_reset(&group);
    for (uint32_t h = 0; h < summands; h++) {
      term_t child;
      sum_term_component(terms, sum, h, coeff, &child);
      if (child == NULL_TERM || mpq_sgn(coeff) * mpq_sgn(gamma_q) >= 0) continue;
      if (!tra_application_is_of_the_plugin(self, child)) continue;
      mpq_abs(coeff, coeff);
      ivector_push(&group, _o_yices_mul(_o_yices_mpq(coeff), child));
    }
    if (group.size < 2) continue;
    mpq_abs(gamma_q, gamma_q);
    conflict_generated = tra_exp_mean_conflict(self, _o_yices_sum(group.size, group.data), target, gamma_q, conflict);
  }

  delete_ivector(&group);
  mpq_clears(p, q, total, coeff, gamma_q, one, NULL);
  safe_free(app);

  assert(!conflict_generated || conflict->size > 0);
  return conflict_generated;
}

/* =====================
    Conflict strategies
   ===================== */

// The conflict strategies of exp (see tra_func_strategy_t). The numbers in the explanations follow
// TRA_EXP_RATIO_MAX_BITS, TRA_EXP_RATIO_MAX_DELTA, TRA_EXP_TAYLOR_MAX_DEGREE and
// TRA_EXP_HYPERBOLA_GUARD_RADIUS: update them together
static const tra_func_strategy_t tra_exp_strategies[] = {
  { "monotonicity", 
    "Two exp applications of the atom whose trail values contradict monotonicity. " 
    "Conflict: 1 or 2 literals, linear in the two arguments and the two applications. "
    "Independent of the parameter delta.",
    true, tra_exp_monotonicity_conflict },
  { "ratio", 
    "Two exp applications of the atom: u - v <= d implies exp(u) <= k exp(v), with k >= e^d dyadic, "
    "d a multiple of 2^-j, j in {0, 1, 2, 4}, and |d| <= 1024. "
    "Conflict: 1 or 2 literals, linear. "
    "Independent of the parameter delta.",
    true, tra_exp_ratio_conflict },
  { "sum", 
    "A weighted sum of exp applications of the atom against another application or a constant, bounded "
    "by convexity (Jensen) or by Hoeffding's lemma. " 
    "Conflict: 1 or 2 literals: a comparison, linear in the applications, and a guard on the arguments, " 
    "linear (Jensen) or quadratic (Hoeffding). "
    "Independent of the parameter delta.",
    true, tra_exp_sum_conflict },
  { "taylor", 
    "The negation of a Taylor bound of exp at 0, of the least degree that cuts the trail value. "
    "Conflict: 1 literal, without guard, of degree 1, 3 or 5 in the argument and 1 in exp(x). "
    "Its coefficients do not depend on the parameter delta.",
    true, tra_exp_taylor_conflict },
  { "pade", 
    "The negation of a [1/1] or [2/2] Pade bound of exp, guarded by the sign of the argument, and by "
    "x < 2 for the [1/1] upper bound. "
    "Conflict: 1 to 3 literals; the bound has degree 1 or 2 in the argument and 1 in exp(x); the guards are linear. " 
    "Its coefficients do not depend on the parameter delta.",
    true, tra_exp_pade_conflict },
  { "hyperbola", 
    "The negation of a hyperbola bound of exp near the argument, guarded by floor(x*) - 1 <= x <= ceil(x*) + 1. "
    "Conflict: 1 or 3 literals; the bound has degree 1 in the argument and 1 (upper bound) or 2 (lower bound) in exp(x). " 
    "Its constants depend on the parameter delta.",
    true, tra_exp_hyperbola_conflict },
  { "preimage", 
    "A power of two 2^k that bounds exp(x), with an integer bound l on x from k ln 2. " 
    "Conflict: 1 or 2 literals, linear; it applies only when the trail values of x and ln(exp x) are "
    "about 2 or more apart. "
    "Its constants do not depend on the parameter delta.",
    true, tra_exp_preimage_conflict },
};

/* ==========
    Destruct
   ========== */

static void tra_exp_destruct(tra_func_plugin_t* self) {
  safe_free(self);
}

/* ===========
    Allocator
   =========== */

tra_func_plugin_t* tra_exp_plugin_allocator(const struct tra_plugin_s* tra){
  tra_func_plugin_t* plugin = (tra_func_plugin_t*) safe_malloc(sizeof(tra_func_plugin_t));
  plugin->name = "exp";
  plugin->arity = 1;
  plugin->is_total_continuous = true;

  plugin->tra = tra;

  plugin->dependencies = NULL;
  plugin->num_dependencies = 0;
  plugin->dependencies_plugins = NULL;
  plugin->add_dependency = tra_func_default_add_dependency;

  plugin->abstract_domain = NULL;
  plugin->add_abstract_domain = tra_func_default_add_abstract_domain;

  plugin->new_term_notify = tra_exp_new_term_notify;

  plugin->is_in_domain = tra_func_default_is_in_domain;

  plugin->let_na_decide = tra_exp_let_na_decide;

  plugin->get_implicit_variables = tra_exp_get_implicit_variables;

  plugin->hint_argument_value = tra_func_default_hint_argument_value;

  plugin->eval = tra_exp_eval;

  plugin->refine_precision = tra_exp_refine_precision;

  plugin->strategies = tra_exp_strategies;
  plugin->num_strategies = sizeof(tra_exp_strategies) / sizeof(tra_exp_strategies[0]);
  plugin->default_conflict = tra_func_increasing_default_get_conflict;

  plugin->destruct = tra_exp_destruct;

  return plugin;
}
