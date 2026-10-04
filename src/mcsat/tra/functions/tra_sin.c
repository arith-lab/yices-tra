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

#include "mcsat/tra/functions/tra_sin.h"

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <gmp.h>
#include <mpfr.h>
#include <poly/upolynomial.h>
#include <poly/value.h>

#include "yices_types.h"
#include "api/yices_api_lock_free.h"
#include "mcsat/mcsat_types.h"
#include "mcsat/plugin.h"
#include "mcsat/tracing.h"
#include "mcsat/value.h"
#include "mcsat/variable_db.h"
#include "terms/term_explorer.h"
#include "terms/terms.h"
#include "utils/int_vectors.h"
#include "utils/memalloc.h"

#include "mcsat/tra/tra_abstract_values.h"
#include "mcsat/tra/tra_conflict_utils.h"
#include "mcsat/tra/utils/tra_exact_values.h"
#include "mcsat/tra/utils/tra_term_explorer.h"
#include "mcsat/tra/utils/tra_tracing.h"

// twice_sin[n mod 12] = 2 sin(n π/6), where 3 marks the irrational values ±√3; hence
// 2 cos(n π/6) = twice_sin[(n + 3) mod 12], as cos(x) = sin(x + π/2)
static const int32_t twice_sin[12] = { 0, 1, 3, 2, 3, 1, 0, -1, 3, -2, 3, -1 };

/* ==============
    Dependencies
   ============== */

// sin depends only on pi
static void tra_sin_add_dependency(tra_func_plugin_t* self, tra_func_plugin_t* dependency) {
  assert(self != NULL && dependency != NULL);
  assert(strcmp(dependency->name, "pi") == 0);
  self->dependencies_plugins[0] = dependency;
}

/* =============
    Term Notify
   ============= */

/**
 * The new_term_notify of sin (contract in tra_func_plugin.h).
 * Let L(s), for s = (sin p), be the lemma p = q * pi ==> s = sin(q π), defined when σ(p) = q * pi
 * (see tra_term_explorer.h; 1 * pi is pi and 0 * pi is 0) for a rational q such that sin(q π) is
 * rational. By Niven's theorem, sin(q π) is rational iff 6q is an integer n with n mod 6 in
 * {0, 1, 3, 5}. For p = pi and p = 0, L(s) is s = 0.
 * Ensures:  if t is not an application (a notification of the dependency pi), nothing is done.
 *           Otherwise, for s = (sin pi) (if pi is declared) and s = (sin 0), L(s) is passed to
 *           prop if s = t or s has no variable (its flush then gives s, and pi, variables); then
 *           L(t) is passed if it is defined and t is neither of them.
 *           (P): while (sin pi) or (sin 0) has a variable, its lemma is asserted or pending (a
 *           definition lemma). Before the first flush, several applications may pass it.
 */
static void tra_sin_new_term_notify(tra_func_plugin_t* self, term_t t, trail_token_t* prop) {
  const plugin_context_t* ctx = tra_get_context(self->tra);
  term_table_t* terms = ctx->terms;

  // A notification of pi, which is not a sin application: the lemmas below would add sin terms
  // to formulas without sin
  if (term_kind(terms, t) != APP_TERM) return;

  // pi is NULL_TERM if it is not declared (possible through the API)
  term_t pi = _o_yices_get_term_by_name("pi");
  term_t zero = _o_yices_rational32(0, 1);
  term_t arg = composite_term_arg(terms, t, 1);

  mpq_t q;
  mpq_init(q);
  term_t args[3] = { pi, zero, arg };
  for (uint32_t i = 0; i < 3; i++) {
    if (args[i] == NULL_TERM) continue;
    // The anchors (sin pi) and (sin 0) if t is one of them or they have no variable, then t
    term_t s = _o_yices_application(composite_term_arg(terms, t, 0), 1, &args[i]);
    variable_t x = variable_db_get_variable_if_exists(ctx->var_db, s);
    if (i < 2 ? s != t && x != variable_null : arg == pi || arg == zero) continue;

    // d = σ(args[i]) = q * m; only a variable can stand for q * pi (the step is then a lookup)
    term_t d = term_kind(terms, args[i]) == UNINTERPRETED_TERM ?
               tra_depurify_term_step(ctx->preprocessor, args[i]) : args[i];
    term_t m = d;
    mpq_set_ui(q, d == zero ? 0 : 1, 1);
    if (term_kind(terms, d) == ARITH_POLY && term_num_children(terms, d) == 1) {
      sum_term_component(terms, d, 0, q, &m);
    }
    if (d != zero && m != pi) continue;
    // n = 6q = 6 num(q) / den(q), if it is an integer
    mpz_mul_ui(mpq_numref(q), mpq_numref(q), 6);
    if (!mpz_divisible_p(mpq_numref(q), mpq_denref(q))) continue;
    mpz_divexact(mpq_numref(q), mpq_numref(q), mpq_denref(q));
    int32_t v = twice_sin[mpz_fdiv_ui(mpq_numref(q), 12)];
    if (v == 3) continue;
    term_t value = _o_yices_arith_eq_atom(s, _o_yices_rational32(v, 2));
    prop->lemma(prop, _o_yices_implies(_o_yices_arith_eq_atom(args[i], d), value));
  }
  mpq_clear(q);
}

/* ==========
    Evaluate
   ========== */

static void tra_sin_eval(tra_func_plugin_t* self, tra_itype_t* args, tra_prec_t precision, tra_itype_t out) {
  self->abstract_domain->sin(out, args[0], precision);
}

/* ====================
    Conflicts: helpers
   ==================== */

// Initial precision of the mpfr extractions. Only a starting point: tra_interval_to_mpfr
// raises it as needed (see tra_abstract_values.h).
#define TRA_SIN_MPFR_PREC_INIT 64

// Encloses sin(c), or cos(c), in [lo_q, hi_q] at the given precision
static void tra_sin_enclose_at(tra_ilib_t* lib, const mpq_t c, tra_prec_t prec, bool cosine,
                               mpq_t lo_q, mpq_t hi_q) {
  mpfr_t lo, hi;
  mpfr_init2(lo, TRA_SIN_MPFR_PREC_INIT);
  mpfr_init2(hi, TRA_SIN_MPFR_PREC_INIT);
  bool ok = tra_enclose_unop_at(lib, cosine ? lib->cos : lib->sin, c, prec, NO_PREC, lo, hi);
  assert(ok); // sin and cos are bounded
  (void) ok;
  mpfr_get_q(lo_q, lo);
  mpfr_get_q(hi_q, hi);
  mpfr_clear(lo);
  mpfr_clear(hi);
}

/* ==========================
    Conflicts: dyadic Taylor
   ========================== */

// Initial and largest half degree m of the dyadic Taylor bounds: the literal has degree 2m
#define TRA_SIN_DYADIC_MIN_DEGREE 1
#define TRA_SIN_DYADIC_MAX_DEGREE 8

// Default half degree, and the |sin| above which the minimal one is used instead (see the
// choice of m in tra_sin_dyadic_taylor_conflict)
#define TRA_SIN_DYADIC_PEAK_DEGREE 2
#define TRA_SIN_DYADIC_PEAK_THRESHOLD 0.9

// Cap on 2m*(k + magnitude bits of the argument), the coefficient size of the literal once
// (x - c)^2m is expanded. At a fixed k, raising m only makes this worse, so exceeding the cap
// means giving up, not escalating further.
#define TRA_SIN_DYADIC_MAX_LITERAL_BITS 65536

/*
 * The dyadic Taylor bound of half degree m on sin at a centre c, with u = x - c:
 *
 *   above:  U = sum_{j<2m} a~_j u^j + u^2m/(2m)! + eps (u^2m + 1)
 *   below:  L = sum_{j<2m} a~_j u^j - u^2m/(2m)! - eps (u^2m + 1)
 *
 * where a~_j = +-mid[j%2]/j! and eps = sum_{j<2m} wid[j%2]/(2 j!), for the midpoints mid and
 * widths wid of enclosures of sin(c) (index 0) and cos(c) (index 1).
 */
static term_t tra_sin_dyadic_taylor_bound(term_t u, long m, mpq_t mid[2], mpq_t wid[2], bool above) {
  mpq_t eps, jfact, coef, err;
  mpz_t fact;
  mpq_inits(eps, jfact, coef, err, NULL);
  mpz_init(fact);

  // One pass, with u^j in power and j! in fact, sums a~_j u^j for j < 2m and the u^2m/(2m)!
  // term, accumulating eps along the way; the eps term is added at the end
  term_t power = _o_yices_rational32(1, 1);
  term_t bound = NULL_TERM;
  mpz_set_ui(fact, 1);
  mpq_set_ui(eps, 0, 1);
  for (long j = 0; j <= 2 * m; j++) {
    if (j > 0) {
      mpz_mul_ui(fact, fact, (unsigned long) j);
      power = _o_yices_mul(power, u);
    }
    mpq_set_z(jfact, fact);

    if (j == 2 * m) {
      mpq_inv(coef, jfact);
      if (!above) mpq_neg(coef, coef);
    } else {
      // The derivatives of sin at c cycle through sin, cos, -sin, -cos
      mpq_div(coef, mid[j % 2], jfact);
      if (j % 4 >= 2) mpq_neg(coef, coef);
      mpq_div(err, wid[j % 2], jfact);
      mpq_div_2exp(err, err, 1);
      mpq_add(eps, eps, err);
    }

    term_t summand = _o_yices_mul(_o_yices_mpq(coef), power);
    bound = (j == 0) ? summand : _o_yices_add(bound, summand);
  }
  if (!above) mpq_neg(eps, eps);
  bound = _o_yices_add(bound, _o_yices_mul(_o_yices_mpq(eps),
                                           _o_yices_add(power, _o_yices_rational32(1, 1))));

  mpz_clear(fact);
  mpq_clears(eps, jfact, coef, err, NULL);
  return bound;
}

/*
 * Dyadic Taylor strategy. If the trail value t* of t = (sin x) escapes the enclosure of t,
 * pushes the single literal t > U (or t < L), true on the trail, for a dyadic Taylor bound
 * (see tra_sin_dyadic_taylor_bound) centred at a dyadic c near the trail value x* of x.
 */
static bool tra_sin_dyadic_taylor_conflict(tra_func_plugin_t* self, term_t t, ivector_t* conflict) {
  if (!tra_application_is_of_the_plugin(self, t)) return false;

  assert(conflict->size == 0);

  const plugin_context_t* ctx = tra_get_context(self->tra);
  tra_ilib_t* lib = self->abstract_domain;

  mcsat_value_t t_val, arg_val;
  term_t arg = composite_term_arg(ctx->terms, t, 1);
  bool t_val_ok = term_evaluate(ctx, t, &t_val);
  bool arg_val_ok = term_evaluate(ctx, arg, &arg_val);
  assert(t_val_ok && arg_val_ok);

  // [lo, hi] encloses I_t, the enclosure of t, which t* escapes past H. p, c, m and k: see
  // below. mid/wid: the midpoints and widths of the enclosures of sin(c) (index 0) and
  // cos(c) (index 1).
  mpfr_t lo, hi;
  mpq_t H, c, lo_q, hi_q, max_width;
  mpq_t mid[2], wid[2];
  mpz_t fact;
  long p, m, k, xbits;
  mpfr_init2(lo, TRA_SIN_MPFR_PREC_INIT);
  mpfr_init2(hi, TRA_SIN_MPFR_PREC_INIT);
  mpq_inits(H, c, lo_q, hi_q, max_width, mid[0], mid[1], wid[0], wid[1], NULL);
  mpz_init(fact);

  tra_itype_t I_t = tra_get_abstraction(self->tra, t);
  if (I_t == NULL || !tra_interval_to_mpfr(lib, I_t, NO_PREC, lo, hi)) {
    tra_trace_iprintf_if(self->tra, "tra::sin", "[sin: dyadic taylor: no enclosure]\n");
    goto done;
  }
  assert(lib->trail_value_position(I_t, &t_val) != TV_INSIDE);
  assert(tra_value_cmp_mpfr(&t_val, lo) < 0 || tra_value_cmp_mpfr(&t_val, hi) > 0);

  bool above = tra_conflict_escape_endpoint(&t_val, lo, hi, H);

  // The literal is a theorem whatever c, m and the enclosures: with a_j = sin^(j)(c)/j!,
  // Taylor's theorem and |sin^(2m)| <= 1 give |sin(x) - sum_{j<2m} a_j u^j| <= u^2m/(2m)!, and
  // |a~_j - a_j| <= e_j = wid/(2 j!) with |u|^j <= 1 + u^2m for j < 2m.
  // The parameters decide whether it cuts t*. Say t* escapes past the upper end H, so that
  // sin(x*) <= H (below is symmetric), and t* - H > 2^(1-p). At x*,
  // U - sin <= 2 u^2m/(2m)! + 2 eps (u^2m + 1), so U(x*) <= H + 2^-(p+1) < t* as soon as
  //   (i)  |x* - c| <= 2^-k, where k >= 0 and (2m)! 2^(2mk) >= 2^(p+3),
  //   (ii) eps <= 2^-(p+4),
  // since then |u| <= 1, 2 u^2m/(2m)! <= 2^-(p+2) and 2 eps (u^2m + 1) <= 2^-(p+2).

  // The separation gives t* - H > 2^-p, and the above wants 2^(1-p): hence p + 1. Past the
  // size budget the literal would be rejected anyway.
  p = tra_value_separation(&t_val, H, above, TRA_SIN_DYADIC_MAX_LITERAL_BITS);
  if (p < 0) {
    tra_trace_iprintf_if(self->tra, "tra::sin", "[sin: dyadic taylor: trail value not separated from the enclosure]\n");
    goto done;
  }
  p++;

  // Magnitude of x*, in bits. It enters the size budget, as expanding (x - c)^2m in x
  // produces c^2m as its constant term.
  tra_value_bounds_mpq(&arg_val, lo_q, hi_q);
  xbits = (long) mpz_sizeinbase(mpq_numref(lo_q), 2) - (long) mpz_sizeinbase(mpq_denref(lo_q), 2) + 1;
  if (xbits < 0) xbits = 0;

  // m starts at the peak degree, or at the minimal one at a trough for U (sin(x*) <= hi <
  // -threshold) and at a peak for L (sin(x*) >= lo > threshold). The u^2m term of U is
  // u^2m/(2m)!, the true one (-1)^m sin(c) u^2m/(2m)!, so the leading error of U carries the
  // factor 1 - (-1)^m sin(c), and that of L 1 + (-1)^m sin(c). For m = 1 these are
  // 1 + sin(c) and 1 - sin(c): exact at those two corners, and worse than |sin| <= 1 at the
  // other two. m = 2 divides both factors by 12 and swaps the exact corners, so it is the
  // default. Only sharpness is at stake.
  m = TRA_SIN_DYADIC_PEAK_DEGREE;
  if (above ? mpfr_cmp_d(hi, -TRA_SIN_DYADIC_PEAK_THRESHOLD) < 0
            : mpfr_cmp_d(lo, TRA_SIN_DYADIC_PEAK_THRESHOLD) > 0) {
    m = TRA_SIN_DYADIC_MIN_DEGREE;
  }

  // From there, the least m whose k fits the size budget. The least k >= 0 of (i) is that
  // of 2mk + bits((2m)!) >= p+4, since bits(X) >= p+4 iff X >= 2^(p+3).
  for (; m <= TRA_SIN_DYADIC_MAX_DEGREE; m++) {
    mpz_fac_ui(fact, 2 * m);
    long need = p + 4 - (long) mpz_sizeinbase(fact, 2);
    k = need <= 0 ? 0 : (need + 2 * m - 1) / (2 * m);
    if (2 * m * (k + xbits) <= TRA_SIN_DYADIC_MAX_LITERAL_BITS) break;
  }
  if (m > TRA_SIN_DYADIC_MAX_DEGREE) {
    tra_trace_iprintf_if(self->tra, "tra::sin", "[sin: dyadic taylor: the literal would be too large]\n");
    goto done;
  }

  // (i): c is a multiple of 2^-k within 2^-k of x*
  tra_value_nearest_dyadic(&arg_val, k, c);

  if (tra_trace_enabled(self->tra, "tra::sin")) {
    tra_trace_iprintf(self->tra, "[sin: dyadic taylor parameters: p = %ld, m = %ld, k = %ld, c = ", p, m, k);
    mpq_out_str(ctx_trace_out(ctx), 10, c);
    tra_trace_printf(self->tra, "]\n");
  }

  // (ii): every derivative of sin at c is +-sin(c) or +-cos(c), so e_j = wid/(2 j!) <= wid/2
  // for one of the two enclosures, and widths of at most 2^-(p+4)/m give eps <= 2m 2^-(p+5)/m.
  mpq_set_ui(max_width, 1, (unsigned long) m);
  mpq_div_2exp(max_width, max_width, p + 4);
  bool sharp = false;
  for (long prec = p + 8, tries = 0; !sharp && tries < 4; prec *= 2, tries++) {
    for (int i = 0; i < 2; i++) {
      tra_sin_enclose_at(lib, c, prec, i == 1, lo_q, hi_q);
      mpq_sub(wid[i], hi_q, lo_q);
      mpq_add(mid[i], lo_q, hi_q);
      mpq_div_2exp(mid[i], mid[i], 1);
    }
    sharp = mpq_cmp(wid[0], max_width) <= 0 && mpq_cmp(wid[1], max_width) <= 0;
  }
  if (!sharp) {
    tra_trace_iprintf_if(self->tra, "tra::sin", "[sin: dyadic taylor: the coefficients are not sharp enough]\n");
    goto done;
  }

  term_t u = _o_yices_sub(arg, _o_yices_mpq(c));
  term_t bound = tra_sin_dyadic_taylor_bound(u, m, mid, wid, above);

  // That the literal is true on the trail is checked rather than trusted: the
  // surrounding machinery only checks that a conflict literal has a value.
  term_t literal = tra_conflict_cutting_literal(ctx, t, bound, above);
  if (literal == NULL_TERM) {
    tra_trace_iprintf_if(self->tra, "tra::sin",
                         "[sin: dyadic taylor: the bound does not cut the trail value, or the literal degenerated]\n");
    goto done;
  }

  ivector_push(conflict, literal);

done:
  mpz_clear(fact);
  mpq_clears(H, c, lo_q, hi_q, max_width, mid[0], mid[1], wid[0], wid[1], NULL);
  if (t_val_ok) mcsat_value_destruct(&t_val);
  if (arg_val_ok) mcsat_value_destruct(&arg_val);
  mpfr_clear(lo);
  mpfr_clear(hi);

  return conflict->size > 0;
}

/* ======================
    Conflicts: pi Taylor
   ====================== */

// Degree caps of the pi Taylor bounds, at the centre 0 and at the other multiples of pi/2.
// Away from 0 the bounds are polynomials in the argument and pi, which cost much more in the
// NA projection, hence the lower cap.
#define TRA_SIN_PI_TAYLOR_MAX_DEGREE_AT_0 9
#define TRA_SIN_PI_TAYLOR_MAX_DEGREE_ELSEWHERE 5

// Largest |n| for which the centre n*pi/2 is tried
#define TRA_SIN_PI_TAYLOR_MAX_N (1L << 20)

// Half-width, in multiples of pi/2, of the guard around the centre of the pi Taylor conflicts,
// and the precision of the enclosure of pi it is computed from (see tra_sin_push_centre_guard)
#define TRA_SIN_PI_TAYLOR_GUARD_RADIUS 1
#define TRA_SIN_PI_TAYLOR_GUARD_PI_PREC 64

// The Taylor polynomial of degree deg of sin at a centre n*pi/2 with n = r mod 4, in
// u = x - n*pi/2. Its coefficients are exact.
static term_t tra_sin_pi_taylor_polynomial(term_t u, long r, long deg) {
  static const int derivative_at_zero[4] = { 0, 1, 0, -1 };  // sin^(k)(0) = sin(k*pi/2)
  term_t power = _o_yices_rational32(1, 1);
  term_t polynomial = _o_yices_rational32(0, 1);
  mpq_t coefficient;
  mpz_t factorial;
  mpq_init(coefficient);
  mpz_init_set_ui(factorial, 1);

  for (long j = 0; j <= deg; j++) {
    if (j > 0) {
      mpz_mul_ui(factorial, factorial, (unsigned long) j);
      power = _o_yices_mul(power, u);
    }
    int sign = derivative_at_zero[(r + j) % 4];
    if (sign != 0) {
      mpq_set_z(coefficient, factorial);
      mpq_inv(coefficient, coefficient);
      if (sign < 0) mpq_neg(coefficient, coefficient);
      polynomial = _o_yices_add(polynomial, _o_yices_mul(_o_yices_mpq(coefficient), power));
    }
  }

  mpq_clear(coefficient);
  mpz_clear(factorial);
  return polynomial;
}

/*
 * pl <= pi <= ph, the rational endpoints of the enclosure of pi that lib computes at precision
 * prec. Returns false, leaving pl and ph unchanged, if the enclosure is unbounded.
 */
static bool tra_sin_pi_bounds(tra_ilib_t* lib, tra_prec_t prec, mpq_t pl, mpq_t ph) {
  mpfr_t lo, hi;
  mpfr_init2(lo, TRA_SIN_MPFR_PREC_INIT);
  mpfr_init2(hi, TRA_SIN_MPFR_PREC_INIT);
  tra_itype_t I_pi = lib->alloc();
  lib->const_pi(I_pi, prec);
  bool ok = tra_interval_to_mpfr(lib, I_pi, NO_PREC, lo, hi);
  lib->free(I_pi);
  if (ok) {
    mpfr_get_q(pl, lo);
    mpfr_get_q(ph, hi);
  }
  mpfr_clears(lo, hi, (mpfr_ptr) 0);
  return ok;
}

/*
 * Requires: cl <= c <= ch for a constant c.
 * Ensures:  out = k * c rounded to a multiple of 2^-bits, upwards if dir = 1 (out >= k * c) and
 *           downwards if dir = -1 (out <= k * c): the product takes the endpoint that keeps the
 *           direction for the sign of k.
 */
static void tra_sin_mul_enclosed(mpq_t out, const mpq_t k, const mpq_t cl, const mpq_t ch, long bits, int dir) {
  mpq_mul(out, k, ((mpq_sgn(k) >= 0) == (dir > 0)) ? ch : cl);
  tra_mpq_round_dyadic(out, out, bits, dir);
}

/*
 * Pushes the guard lo <= x <= hi around the centre c = n*pi/2 of a pi Taylor conflict, where
 * lo = floor(c - R*pi/2) and hi = ceil(c + R*pi/2) are integers, R = TRA_SIN_PI_TAYLOR_GUARD_RADIUS.
 * The guard does not involve the term pi: its bounds come from an enclosure of the number pi.
 * Pushes nothing unless lo < x* < hi, for the trail value x* of x.
 */
static void tra_sin_push_centre_guard(tra_ilib_t* lib, term_t x, const mcsat_value_t* x_val, long n,
                                      ivector_t* conflict) {
  mpq_t pl, ph, k, lo, hi;
  mpq_inits(pl, ph, k, lo, hi, NULL);
  if (tra_sin_pi_bounds(lib, TRA_SIN_PI_TAYLOR_GUARD_PI_PREC, pl, ph)) {
    // lo <= (n - R) pi/2 and hi >= (n + R) pi/2, rounded outwards to integers
    mpq_set_si(k, n - TRA_SIN_PI_TAYLOR_GUARD_RADIUS, 2);
    mpq_canonicalize(k);
    tra_sin_mul_enclosed(lo, k, pl, ph, 0, -1);
    mpq_set_si(k, n + TRA_SIN_PI_TAYLOR_GUARD_RADIUS, 2);
    mpq_canonicalize(k);
    tra_sin_mul_enclosed(hi, k, pl, ph, 0, 1);

    // x* lies within pi/4 of c when the trail value of pi is close to pi, hence inside.
    // It may lie outside when pi has no trail value: then n = 0 whatever x* is.
    tra_conflict_push_bounds(x, x_val, lo, hi, conflict);
  }
  mpq_clears(pl, ph, k, lo, hi, NULL);
}

/*
 * Pi Taylor strategy. If the trail value of t = (sin x) escapes the enclosure of t, pushes
 * the negation of a Taylor truncation of sin at the multiple c = n*pi/2 of pi/2 nearest to
 * the trail value x* of x, with pi kept symbolic, of the least degree that cuts the trail
 * value. The literal comes with its guards: the sign of u = x - c for even n, and a window
 * around c (see tra_sin_push_centre_guard), as the bound is sharp only near c.
 */
static bool tra_sin_pi_taylor_conflict(tra_func_plugin_t* self, term_t t, ivector_t* conflict) {
  if (!tra_application_is_of_the_plugin(self, t)) return false;

  assert(conflict->size == 0);

  const plugin_context_t* ctx = tra_get_context(self->tra);
  term_table_t* terms = ctx->terms;
  tra_ilib_t* lib = self->abstract_domain;
  term_t arg = composite_term_arg(terms, t, 1);
  term_t pi = _o_yices_get_term_by_name("pi");

  bool conflict_generated = false;

  mcsat_value_t t_val, arg_val, pi_val, u_val;
  bool t_val_ok = term_evaluate(ctx, t, &t_val);
  bool arg_val_ok = term_evaluate(ctx, arg, &arg_val);
  bool pi_val_ok = pi != NULL_TERM && term_evaluate(ctx, pi, &pi_val);
  bool u_val_ok = false;
  mpq_t half_n;
  mpq_init(half_n);

  if (!t_val_ok || !arg_val_ok) {
    tra_trace_iprintf_if(self->tra, "tra::sin", "[sin: pi taylor: no trail value]\n");
    goto done;
  }

  // The side of its enclosure the trail value of t escapes on: above needs an upper bound
  tra_itype_t I_t = tra_get_abstraction(self->tra, t);
  tra_tv_position_t position = (I_t != NULL) ? lib->trail_value_position(I_t, &t_val) : TV_UNKNOWN;
  if (position != TV_ABOVE && position != TV_BELOW) {
    tra_trace_iprintf_if(self->tra, "tra::sin", "[sin: pi taylor: trail value not outside the enclosure]\n");
    goto done;
  }
  bool above = (position == TV_ABOVE);

  // n = round(2x/pi), from the trail values of x and of pi (u is measured from the latter),
  // and n = 0 when pi has none: any n gives valid bounds, it only decides how sharp they are
  long n = 0;
  if (pi_val_ok) {
    double n_approx = 2.0 * lp_value_to_double(&arg_val.lp_value) / lp_value_to_double(&pi_val.lp_value);
    if (!(fabs(n_approx) < TRA_SIN_PI_TAYLOR_MAX_N + 0.5)) {
      tra_trace_iprintf_if(self->tra, "tra::sin", "[sin: pi taylor: centre too far from 0]\n");
      goto done;
    }
    n = lround(n_approx);
  }

  term_t u = arg;
  if (n != 0) {
    mpq_set_si(half_n, n, 2);
    mpq_canonicalize(half_n);
    u = _o_yices_sub(arg, _o_yices_mul(_o_yices_mpq(half_n), pi));
  }
  u_val_ok = term_evaluate(ctx, u, &u_val);
  if (!u_val_ok) {
    tra_trace_iprintf_if(self->tra, "tra::sin", "[sin: pi taylor: cannot evaluate the offset from the centre]\n");
    goto done;
  }

  // The family of truncations that bounds sin on the needed side, by its first degree:
  // - n odd, s = sin(c) = +-1: sin(c + u) = s*cos(u), and the truncations of cos(u) of
  //   degree 4k are upper bounds, those of degree 4k+2 lower bounds, for every u;
  // - n even, s = cos(c) = +-1: sin(c + u) = s*sin(u), and for u >= 0 the truncations of
  //   sin(u) of degree 4k+1 are upper bounds, those of degree 4k+3 lower bounds (swapped
  //   for u <= 0), so the literal is guarded by the sign of u.
  long r = ((n % 4) + 4) % 4;
  long max_degree = (n == 0) ? TRA_SIN_PI_TAYLOR_MAX_DEGREE_AT_0 : TRA_SIN_PI_TAYLOR_MAX_DEGREE_ELSEWHERE;
  long start;
  term_t guard = NULL_TERM;
  if (r % 2 == 1) {
    int s = (r == 1) ? 1 : -1;
    start = (above == (s > 0)) ? 4 : 2;
  } else {
    int s = (r == 0) ? 1 : -1;
    int e = (lp_value_sgn(&u_val.lp_value) >= 0) ? 1 : -1;
    start = (above == (s * e > 0)) ? 1 : 3;
    guard = (e > 0) ? _o_yices_arith_geq0_atom(u) : _o_yices_arith_leq0_atom(u);
  }

  for (long degree = start; degree <= max_degree; degree += 4) {
    term_t bound = tra_sin_pi_taylor_polynomial(u, r, degree);
    term_t literal = tra_conflict_cutting_literal(ctx, t, bound, above);
    if (literal == NULL_TERM) continue;  // the bound does not cut the trail value

    tra_trace_iprintf_if(self->tra, "tra::sin", "[sin: pi taylor n = %ld, degree %ld, %s bound]\n",
                         n, degree, above ? "upper" : "lower");
    tra_sin_push_centre_guard(lib, arg, &arg_val, n, conflict);
    if (guard != NULL_TERM && guard != true_term) ivector_push(conflict, guard);
    ivector_push(conflict, literal);
    conflict_generated = true;
    goto done;
  }
  tra_trace_iprintf_if(self->tra, "tra::sin", "[sin: pi taylor: no bound cuts the trail value]\n");

done:
  mpq_clear(half_n);
  if (t_val_ok) mcsat_value_destruct(&t_val);
  if (arg_val_ok) mcsat_value_destruct(&arg_val);
  if (pi_val_ok) mcsat_value_destruct(&pi_val);
  if (u_val_ok) mcsat_value_destruct(&u_val);

  assert(!conflict_generated || conflict->size > 0);
  return conflict_generated;
}

/* ===================
    Conflicts: linear
   =================== */

// Largest separation, in bits, between the trail value of a sin term and its enclosure
// for which the linear strategy (tra_sin_linear_conflict) builds a bound. Past it, the
// constants of the line grow with the separation while the line cuts off less and less.
// TODO: justify the value (by tests or theory); 200 is the value tested, not tuned.
#define TRA_SIN_LINEAR_MAX_BITS 200

// Bits of the dyadic endpoints of the curvature regions of the linear strategy: a
// region loses at most 2^-40 at each end, near the multiples of pi the pi Taylor handles
#define TRA_SIN_LINEAR_REGION_BITS 40

// Precision of the enclosure of pi from which the curvature regions are computed
#define TRA_SIN_LINEAR_PI_PREC 256

/*
 * The curvature region [a, b] of sin that holds x*, of which zlo is a lower bound: a >= j*pi
 * and b <= (j+1)*pi, rounded inwards to multiples of 2^-TRA_SIN_LINEAR_REGION_BITS, where sin
 * is concave for even j and convex for odd j. Returns false, and traces why, if x* is too large
 * or lies outside [a, b], near a multiple of pi.
 */
static bool tra_sin_curvature_region(tra_func_plugin_t* self, const mcsat_value_t* x_val, const mpq_t zlo,
                                     mpq_t a, mpq_t b, bool* concave) {
  double x_approx = mpq_get_d(zlo);
  if (!(fabs(x_approx) < 1e12)) {
    tra_trace_iprintf_if(self->tra, "tra::sin", "[sin: linear: argument too large]\n");
    return false;
  }

  mpq_t pi_lo, pi_hi, tmp;
  mpq_inits(pi_lo, pi_hi, tmp, NULL);
  tra_sin_pi_bounds(self->abstract_domain, TRA_SIN_LINEAR_PI_PREC, pi_lo, pi_hi); // cannot fail: pi is bounded

  // a >= j pi and b <= (j+1) pi: each end is rounded inwards
  long j = (long) floor(x_approx / mpq_get_d(pi_lo));
  mpq_set_si(tmp, j, 1);
  tra_sin_mul_enclosed(a, tmp, pi_lo, pi_hi, TRA_SIN_LINEAR_REGION_BITS, 1);
  mpq_set_si(tmp, j + 1, 1);
  tra_sin_mul_enclosed(b, tmp, pi_lo, pi_hi, TRA_SIN_LINEAR_REGION_BITS, -1);
  *concave = (j % 2 == 0);

  bool inside = tra_value_cmp_mpq(x_val, a) >= 0 && tra_value_cmp_mpq(x_val, b) <= 0;
  if (!inside) tra_trace_iprintf_if(self->tra, "tra::sin", "[sin: linear: argument at a change of curvature]\n");

  mpq_clears(pi_lo, pi_hi, tmp, NULL);
  return inside;
}

/*
 * The tangent s0 + s1 (x - c) to sin at a dyadic c near x*, kept in [a, b], raised (concave
 * region) or lowered (convex region) by the error of the slope times the largest distance from
 * c to [a, b]: it bounds sin on all of [a, b]. p sets the accuracy of c, prec that of sin(c)
 * and cos(c).
 */
static void tra_sin_linear_tangent(tra_ilib_t* lib, const mcsat_value_t* x_val, long p, tra_prec_t prec,
                                   const mpq_t a, const mpq_t b, bool concave, mpq_t c, mpq_t s0, mpq_t s1) {
  mpq_t zlo, zhi, w, reach, tmp;
  mpq_inits(zlo, zhi, w, reach, tmp, NULL);

  long k = (p + 1) / 2 + 2;
  tra_value_nearest_dyadic(x_val, k, c);
  if (mpq_cmp(c, a) < 0) mpq_set(c, a);
  if (mpq_cmp(c, b) > 0) mpq_set(c, b);

  // sin(c) rounded outwards, the midpoint of cos(c) as the slope
  tra_sin_enclose_at(lib, c, prec, false, zlo, zhi);
  mpq_set(s0, concave ? zhi : zlo);
  tra_sin_enclose_at(lib, c, prec, true, zlo, zhi);
  mpq_add(s1, zlo, zhi);
  mpq_div_2exp(s1, s1, 1);
  mpq_sub(w, zhi, zlo);
  mpq_div_2exp(w, w, 1);
  mpq_sub(reach, b, c);
  mpq_sub(tmp, c, a);
  if (mpq_cmp(tmp, reach) > 0) mpq_set(reach, tmp);
  mpq_mul(w, w, reach);
  if (concave) mpq_add(s0, s0, w);
  else mpq_sub(s0, s0, w);

  mpq_clears(zlo, zhi, w, reach, tmp, NULL);
}

/*
 * The chord s0 + s1 (x - c) of sin over the cell [glo, ghi] of width 2^-k that holds x*, with
 * glo the multiple of 2^-k below zlo, cut to [a, b]. The values of sin at its ends are rounded
 * towards the inside of the arc: it bounds sin on the cell. Returns false if the cell misses x*.
 */
static bool tra_sin_linear_chord(tra_ilib_t* lib, const mcsat_value_t* x_val, const mpq_t zlo, long p,
                                 tra_prec_t prec, const mpq_t a, const mpq_t b, bool concave,
                                 mpq_t c, mpq_t s0, mpq_t s1, mpq_t glo, mpq_t ghi) {
  mpq_t elo, ehi, tmp;
  mpq_inits(elo, ehi, tmp, NULL);
  bool ok = false;

  long k = (p + 1) / 2 + 1;
  tra_mpq_round_dyadic(glo, zlo, k, -1);
  mpq_set_ui(tmp, 1, 1);
  mpq_div_2exp(tmp, tmp, k);
  mpq_add(ghi, glo, tmp);
  if (mpq_cmp(glo, a) < 0) mpq_set(glo, a);
  if (mpq_cmp(ghi, b) > 0) mpq_set(ghi, b);
  if (mpq_cmp(glo, ghi) >= 0 || tra_value_cmp_mpq(x_val, ghi) > 0) goto done;

  tra_sin_enclose_at(lib, glo, prec, false, elo, ehi);
  mpq_set(s0, concave ? elo : ehi);
  tra_sin_enclose_at(lib, ghi, prec, false, elo, ehi);
  mpq_set(s1, concave ? elo : ehi);
  mpq_sub(s1, s1, s0);
  mpq_sub(tmp, ghi, glo);
  mpq_div(s1, s1, tmp);
  mpq_set(c, glo);
  ok = true;

 done:
  mpq_clears(elo, ehi, tmp, NULL);
  return ok;
}

/*
 * Linear strategy. If the trail value t* of t = (sin x) escapes the enclosure of t, pushes a
 * line bounding sin on part of the curvature region of sin that holds x* (see
 * tra_sin_curvature_region), guarded by that part. On the side of the arc where tangents bound
 * sin (above a concave arc, below a convex one), it is the tangent at a point near x*, valid on
 * the whole region; on the other side, the chord over a small cell around x*, valid on the cell.
 * The literals do not involve pi.
 */
static bool tra_sin_linear_conflict(tra_func_plugin_t* self, term_t t, ivector_t* conflict) {
  if (!tra_application_is_of_the_plugin(self, t)) return false;

  assert(conflict->size == 0);

  const plugin_context_t* ctx = tra_get_context(self->tra);
  term_table_t* terms = ctx->terms;
  tra_ilib_t* lib = self->abstract_domain;
  term_t arg = composite_term_arg(terms, t, 1);

  bool conflict_generated = false;

  mcsat_value_t t_val, arg_val;
  bool t_val_ok = term_evaluate(ctx, t, &t_val);
  bool arg_val_ok = term_evaluate(ctx, arg, &arg_val);

  // H: the endpoint of the enclosure of t that its trail value escapes past. [zlo, zhi]: the
  // enclosure of x*. [a, b]: the curvature region. The line is s0 + s1*(x - c), guarded by x in
  // [glo, ghi].
  mpq_t H, zlo, zhi, a, b, c, s0, s1, glo, ghi;
  mpq_inits(H, zlo, zhi, a, b, c, s0, s1, glo, ghi, NULL);
  mpfr_t lo, hi;
  mpfr_init2(lo, TRA_SIN_MPFR_PREC_INIT);
  mpfr_init2(hi, TRA_SIN_MPFR_PREC_INIT);

  if (!t_val_ok || !arg_val_ok) {
    tra_trace_iprintf_if(self->tra, "tra::sin", "[sin: linear: no trail value]\n");
    goto done;
  }

  tra_itype_t I_t = tra_get_abstraction(self->tra, t);
  if (I_t == NULL || !tra_interval_to_mpfr(lib, I_t, NO_PREC, lo, hi)) {
    tra_trace_iprintf_if(self->tra, "tra::sin", "[sin: linear: no enclosure]\n");
    goto done;
  }
  bool above = tra_conflict_escape_endpoint(&t_val, lo, hi, H);

  // The separation of t* from H, 2^-p
  long p = tra_value_separation(&t_val, H, above, TRA_SIN_LINEAR_MAX_BITS);
  if (p < 0) {
    tra_trace_iprintf_if(self->tra, "tra::sin", "[sin: linear: trail value not separated from the enclosure]\n");
    goto done;
  }
  if (p > TRA_SIN_LINEAR_MAX_BITS) {
    tra_trace_iprintf_if(self->tra, "tra::sin", "[sin: linear: separation too small for a linear bound]\n");
    goto done;
  }

  bool concave;
  tra_value_bounds_mpq(&arg_val, zlo, zhi);
  if (!tra_sin_curvature_region(self, &arg_val, zlo, a, b, &concave)) goto done;

  // Tangents bound a concave arc from above, a convex one from below
  bool tangent = (above == concave);
  tra_prec_t prec = p + 16;
  if (tangent) {
    tra_sin_linear_tangent(lib, &arg_val, p, prec, a, b, concave, c, s0, s1);
    mpq_set(glo, a);
    mpq_set(ghi, b);
  } else if (!tra_sin_linear_chord(lib, &arg_val, zlo, p, prec, a, b, concave, c, s0, s1, glo, ghi)) {
    tra_trace_iprintf_if(self->tra, "tra::sin", "[sin: linear: the cell misses the argument]\n");
    goto done;
  }

  term_t bound = _o_yices_add(_o_yices_mpq(s0), _o_yices_mul(_o_yices_mpq(s1), _o_yices_sub(arg, _o_yices_mpq(c))));
  term_t literal = tra_conflict_cutting_literal(ctx, t, bound, above);
  if (literal == NULL_TERM) {
    tra_trace_iprintf_if(self->tra, "tra::sin",
                         "[sin: linear: the linear bound does not cut the trail value, or the literal degenerated]\n");
    goto done;
  }

  tra_trace_iprintf_if(self->tra, "tra::sin",
                       "[sin: linear %s, separation of %ld bits]\n", tangent ? "tangent" : "chord", p);
  term_t guard_lo = _o_yices_arith_geq_atom(arg, _o_yices_mpq(glo));
  term_t guard_hi = _o_yices_arith_leq_atom(arg, _o_yices_mpq(ghi));
  if (guard_lo != true_term) ivector_push(conflict, guard_lo);
  if (guard_hi != true_term) ivector_push(conflict, guard_hi);
  ivector_push(conflict, literal);
  conflict_generated = true;

done:
  mpq_clears(H, zlo, zhi, a, b, c, s0, s1, glo, ghi, NULL);
  mpfr_clear(lo);
  mpfr_clear(hi);
  if (t_val_ok) mcsat_value_destruct(&t_val);
  if (arg_val_ok) mcsat_value_destruct(&arg_val);

  assert(!conflict_generated || conflict->size > 0);
  return conflict_generated;
}

/* =================
    Conflicts: pairs
   ================= */

// Limits of the double guess of 6d in tra_sin_pi_multiple (algebraic values): its absolute value is
// below the first, and its distance to the nearest integer is at most the second
#define TRA_SIN_PAIR_GUESS_MAX 1e12
#define TRA_SIN_PAIR_GUESS_TOLERANCE 1e-6

/*
 * Let a solution be a rational d such that 6d is an integer and a - kb = d pi exactly (d = 0 and
 * a = kb if pi is NULL).
 * Requires: k != 0; pi is NULL or a nonzero value; d is initialized.
 * Modifies: d; the isolating intervals of a, b and pi (libpoly refines them).
 * Ensures:  result ==> d is a solution;
 *           (P):  result if a solution d exists and either pi is NULL, or a, b and pi are rational
 *                 (as lp_value_is_rational), or |6d| < TRA_SIN_PAIR_GUESS_MAX and 6(a - kb)/pi,
 *                 evaluated in doubles, is within TRA_SIN_PAIR_GUESS_TOLERANCE of 6d.
 */
static bool tra_sin_pi_multiple(const lp_value_t* a, const lp_value_t* b, const lp_value_t* pi, long k, mpq_t d) {
  assert(k != 0);
  if (lp_value_is_rational(a) && lp_value_is_rational(b) && (pi == NULL || lp_value_is_rational(pi))) {
    // Exact, with no bound on the magnitudes: d = (a - kb)/pi
    mpq_t q;
    mpq_init(q);
    lp_value_get_rational(a, d);
    lp_value_get_rational(b, q);
    mpz_mul_si(mpq_numref(q), mpq_numref(q), k);
    mpq_canonicalize(q);
    mpq_sub(d, d, q);
    bool found = mpq_sgn(d) == 0;
    if (!found && pi != NULL) {
      lp_value_get_rational(pi, q);
      mpq_div(d, d, q);
      // 6d is an integer iff den(d) divides 6 num(d), computed in the numerator of q (now unused)
      mpz_mul_ui(mpq_numref(q), mpq_numref(d), 6);
      found = mpz_divisible_p(mpq_numref(q), mpq_denref(d));
    }
    mpq_clear(q);
    return found;
  }

  // Some value is algebraic: n = round(6 (a - kb)/pi) from doubles (n = 0 if pi is NULL), then an
  // exact check of a = c for c = kb + (n/6) pi
  long n = 0;
  if (pi != NULL) {
    double six_d = 6 * (lp_value_to_double(a) - k * lp_value_to_double(b)) / lp_value_to_double(pi);
    if (!(fabs(six_d) < TRA_SIN_PAIR_GUESS_MAX)) return false; // also rejects NaN
    n = lround(six_d);
    if (fabs(six_d - n) > TRA_SIN_PAIR_GUESS_TOLERANCE) return false;
  }
  mpq_set_si(d, n, 6);
  mpq_canonicalize(d);
  // For k = ±1, copying or negating b is cheaper than a product
  lp_value_t c;
  lp_value_construct_zero(&c);
  if (k == 1) lp_value_assign(&c, b);
  else if (k == -1) lp_value_neg(&c, b);
  else {
    lp_value_t k_val;
    lp_value_construct_int(&k_val, k);
    lp_value_mul(&c, &k_val, b);
    lp_value_destruct(&k_val);
  }
  if (n != 0) {
    lp_value_t d_pi;
    lp_value_construct(&d_pi, LP_VALUE_RATIONAL, d);
    lp_value_mul(&d_pi, &d_pi, pi);
    lp_value_add(&c, &c, &d_pi);
    lp_value_destruct(&d_pi);
  }

  // Two algebraic numbers are equal only if their polynomials share a root: a constant gcd spares
  // lp_value_cmp the refinements that separate a near miss (seconds for a gap of 10^-10000)
  bool may_be_equal = true;
  if (a->type == LP_VALUE_ALGEBRAIC && c.type == LP_VALUE_ALGEBRAIC && a->value.a.f != NULL && c.value.a.f != NULL) {
    lp_upolynomial_t* g = lp_upolynomial_gcd(a->value.a.f, c.value.a.f);
    may_be_equal = lp_upolynomial_degree(g) > 0;
    lp_upolynomial_delete(g);
  }
  bool found = may_be_equal && lp_value_cmp(a, &c) == 0;
  lp_value_destruct(&c);
  return found;
}

/*
 * Pair strategy, for an atom. For applications (sin a) != (sin b), σ ∈ {1, -1} and an integer n
 * with e = σ twice_sin[(n + 3) mod 12] ≠ ±3 (so e = 2σ cos(nπ/6) ∈ {0, ±1, ±2}), let R be P = c:
 *   P = (sin a) - (e/2) (sin b)                        if e = ±2,
 *   P = (sin a)^2 + (sin b)^2 - e (sin a) (sin b)      otherwise,
 * with c = 1 - e^2/4. By sin^2 A + sin^2 B - 2 cos(A - B) sin A sin B = sin^2(A - B) at A = a
 * and B = σb, the equation a - σb = (n/6) π implies R.
 * Requires: conflict is empty; the term named pi, if any, is owned by the pi plugin.
 * Modifies: conflict.
 * Ensures:  result <==> conflict is not empty;
 *           result ==> conflict = [E, L] for such (sin a), (sin b) in V(atom) whose arguments have trail
 *                      values a*, b*, where 6d = n and a* - σb* = d pi* exactly for the nonzero trail
 *                      value pi* of pi (d = 0 if pi has no such value), E is a - σb = d pi (left out
 *                      when it is true_term), and L is P > c or P < c, true on the trail.
 *                      Hence conflict meets the contract of tra_func_strategy_t;
 *           (P):  result if V(atom) is defined and some such pair, whose d tra_sin_pi_multiple finds,
 *                 violates R on the trail.
 * The literals do not depend on the abstractions (hence on delta).
 */
static bool tra_sin_pair_conflict(tra_func_plugin_t* self, term_t atom, ivector_t* conflict) {
  if(!is_boolean_term(tra_get_context(self->tra)->terms, atom)) return false;

  assert(conflict->size == 0);

  const plugin_context_t* ctx = tra_get_context(self->tra);
  term_table_t* terms = ctx->terms;
  const int_hset_t* vars = tra_get_term_variables(self->tra, atom);
  if (vars == NULL) return false;
  term_t pi = _o_yices_get_term_by_name("pi");
  assert(pi == NULL_TERM || tra_application_is_of_the_plugin(self->dependencies_plugins[0], pi));

  // The applications of sin in V(atom) whose argument has a trail value, and these values
  term_t* app = (term_t*) safe_malloc(vars->nelems * sizeof(term_t));
  mcsat_value_t* arg_val = (mcsat_value_t*) safe_malloc(vars->nelems * sizeof(mcsat_value_t));
  uint32_t k = 0;
  for (uint32_t i = 0; i < vars->nelems; i++) {
    term_t s = vars->data[i];
    if (!tra_application_is_of_the_plugin(self, s)) continue;
    if (term_evaluate(ctx, composite_term_arg(terms, s, 1), &arg_val[k])) app[k++] = s;
  }

  // pi_star is the trail value of pi if it is nonzero, and NULL otherwise: then only d = 0 is found
  mcsat_value_t pi_val;
  bool pi_val_ok = pi != NULL_TERM && term_evaluate(ctx, pi, &pi_val);
  const lp_value_t* pi_star = pi_val_ok && lp_value_sgn(&pi_val.lp_value) != 0 ? &pi_val.lp_value : NULL;
  mpq_t d;
  mpz_t n;
  mpq_init(d);
  mpz_init(n);

  bool conflict_generated = false;
  for (uint32_t i = 0; i < k; i++) {
    for (uint32_t j = i + 1; j < k; j++) {
      // a is the argument of larger term index: when a stands for b + d pi, E is its purification
      // equation, true at the base level (efficiency only)
      uint32_t ia = i, ib = j;
      if (composite_term_arg(terms, app[i], 1) < composite_term_arg(terms, app[j], 1)) { ia = j; ib = i; }
      term_t sin_a = app[ia], sin_b = app[ib];
      term_t a = composite_term_arg(terms, sin_a, 1), b = composite_term_arg(terms, sin_b, 1);

      // The difference family (σ = 1) first, then the sum family
      for (int32_t sigma = 1; sigma >= -1; sigma -= 2) {
        if (!tra_sin_pi_multiple(&arg_val[ia].lp_value, &arg_val[ib].lp_value, pi_star, sigma, d)) continue;

        // e = 2σ cos(n π/6) for the integer n = 6d; the irrational values ±√3 (entry 3) are skipped
        mpz_mul_ui(n, mpq_numref(d), 6);
        mpz_divexact(n, n, mpq_denref(d));
        int32_t e = twice_sin[(mpz_fdiv_ui(n, 12) + 3) % 12];
        if (e == 3) continue;
        e *= sigma;

        term_t P = (e == 2)  ? _o_yices_sub(sin_a, sin_b)
                 : (e == -2) ? _o_yices_add(sin_a, sin_b)
                 : _o_yices_sub(_o_yices_add(_o_yices_square(sin_a), _o_yices_square(sin_b)),
                                _o_yices_mul(_o_yices_rational32(e, 1), _o_yices_mul(sin_a, sin_b)));
        term_t c = _o_yices_rational32(4 - e * e, 4);
        term_t L = tra_conflict_cutting_literal(ctx, P, c, true);
        if (L == NULL_TERM) L = tra_conflict_cutting_literal(ctx, P, c, false);
        if (L == NULL_TERM) continue; // R holds on the trail

        // d pi is written 0 for d = 0, as pi may then be NULL_TERM
        term_t E = _o_yices_arith_eq_atom(sigma > 0 ? _o_yices_sub(a, b) : _o_yices_add(a, b),
                                          mpq_sgn(d) == 0 ? _o_yices_rational32(0, 1)
                                                          : _o_yices_mul(_o_yices_mpq(d), pi));
        assert(E != false_term); // a* - σb* - d pi* = 0 exactly
        // E is true_term if a - σb - d pi is 0 as a polynomial (e.g. a = pi, b = 0): L alone is unsat
        if (E != true_term) ivector_push(conflict, E);
        ivector_push(conflict, L);
        conflict_generated = true;

        if (tra_trace_enabled(self->tra, "tra::sin")) {
          tra_trace_iprintf(self->tra, "[sin: pair: ");
          tra_trace_print_term(self->tra, sin_a);
          tra_trace_printf(self->tra, " and ");
          tra_trace_print_term(self->tra, sin_b);
          tra_trace_printf(self->tra, ", %s, d = ", sigma > 0 ? "difference" : "sum");
          mpq_out_str(ctx_trace_out(ctx), 10, d);
          tra_trace_printf(self->tra, "]\n");
        }
        goto done;
      }
    }
  }

done:
  for (uint32_t i = 0; i < k; i++) mcsat_value_destruct(&arg_val[i]);
  if (pi_val_ok) mcsat_value_destruct(&pi_val);
  safe_free(app);
  safe_free(arg_val);
  mpq_clear(d);
  mpz_clear(n);

  assert(!conflict_generated || conflict->size > 0);
  return conflict_generated;
}

/* ===========================
    Conflicts: multiple angles
   =========================== */

// Largest |k| of the relations a = kb + d pi of the multiple-angle strategy: its literals have
// degree |k| in (sin b)
#define TRA_SIN_MULTIPLE_MAX_K 6

/*
 * Multiple-angle strategy, for an atom. Let T_K be the Chebyshev polynomial of the first kind
 * (T_0 = 1, T_1 = s, T_{j+1} = 2s T_j - T_{j-1}). For applications (sin a) != (sin b), an integer k
 * with 2 <= |k| <= TRA_SIN_MULTIPLE_MAX_K and a rational d, let (K, D) = (k, d) if k > 0 and
 * (K, D) = (-k, 1 - d) otherwise, and n = D + (K - 1)/2. If n is an integer (K odd and D an
 * integer, or K even and D ∈ Z + 1/2), let R be Q = 0 with Q = (sin a) - (-1)^n T_K(sin b).
 * The equation a - kb = d π implies R: T_K(sin b) = sin φ for φ = Kb + (1 - K)π/2 (as sin b =
 * cos(π/2 - b) and T_K(cos x) = cos(Kx)), sin a = sin(Kb + Dπ) (for k < 0, as sin x = sin(π - x)),
 * and Kb + Dπ = φ + nπ. For n ∈ Z + 1/2, only sin^2 a + T_K(sin b)^2 = 1 holds; it is left out, as
 * its degree 2K stalls the NA projection (e.g. trigpoly-356-4b).
 * Let σ* be tra_depurify_term. Only structural relations are used: a relation that holds only at
 * the trail values yields a valid lemma that relates unrelated terms, and it can stall NA.
 * Requires: conflict is empty; the term named pi, if any, is owned by the pi plugin.
 * Modifies: conflict.
 * Ensures:  result <==> conflict is not empty;
 *           result ==> conflict = [E, L] for such k and (sin a), (sin b) in V(atom) whose arguments
 *                      have trail values a*, b*, where a* - kb* = d pi* exactly for the nonzero trail value
 *                      pi* of pi (d = 0 if pi has no such value), σ*(a) - kσ*(b) - d pi is the zero
 *                      polynomial, E is a - kb = d pi (left out when it is true_term), and L is Q > 0
 *                      or Q < 0, true on the trail.
 *                      Hence conflict meets the contract of tra_func_strategy_t;
 *           (P):  result if V(atom) is defined and some such pair and k, whose d tra_sin_pi_multiple
 *                 finds, violates R on the trail.
 * The literals do not depend on the abstractions (hence on delta).
 */
static bool tra_sin_multiple_angle_conflict(tra_func_plugin_t* self, term_t atom, ivector_t* conflict) {
  if(!is_boolean_term(tra_get_context(self->tra)->terms, atom)) return false;

  assert(conflict->size == 0);

  const plugin_context_t* ctx = tra_get_context(self->tra);
  term_table_t* terms = ctx->terms;
  const int_hset_t* vars = tra_get_term_variables(self->tra, atom);
  if (vars == NULL) return false;
  term_t pi = _o_yices_get_term_by_name("pi");
  assert(pi == NULL_TERM || tra_application_is_of_the_plugin(self->dependencies_plugins[0], pi));

  // The applications of sin in V(atom) whose argument has a trail value, these values, and σ* of
  // the arguments
  term_t* app = (term_t*) safe_malloc(vars->nelems * sizeof(term_t));
  mcsat_value_t* arg_val = (mcsat_value_t*) safe_malloc(vars->nelems * sizeof(mcsat_value_t));
  term_t* dep = (term_t*) safe_malloc(vars->nelems * sizeof(term_t));
  uint32_t m = 0;
  for (uint32_t i = 0; i < vars->nelems; i++) {
    term_t s = vars->data[i];
    if (!tra_application_is_of_the_plugin(self, s)) continue;
    term_t arg = composite_term_arg(terms, s, 1);
    if (!term_evaluate(ctx, arg, &arg_val[m])) continue;
    dep[m] = tra_depurify_term(ctx->preprocessor, arg);
    app[m++] = s;
  }

  // pi_star is the trail value of pi if it is nonzero, and NULL otherwise: then only d = 0 is found
  mcsat_value_t pi_val;
  bool pi_val_ok = pi != NULL_TERM && term_evaluate(ctx, pi, &pi_val);
  const lp_value_t* pi_star = pi_val_ok && lp_value_sgn(&pi_val.lp_value) != 0 ? &pi_val.lp_value : NULL;
  term_t zero = _o_yices_rational32(0, 1);
  mpq_t d;
  mpz_t w;
  mpq_init(d);
  mpz_init(w);

  bool conflict_generated = false;
  for (uint32_t i = 0; i < m; i++) {
    for (uint32_t j = 0; j < m; j++) {
      if (i == j) continue;
      term_t sin_a = app[i], sin_b = app[j];
      term_t a = composite_term_arg(terms, sin_a, 1), b = composite_term_arg(terms, sin_b, 1);

      // The least degrees first
      for (long K = 2; K <= TRA_SIN_MULTIPLE_MAX_K; K++) {
        for (long k = K; k >= -K; k -= 2 * K) {
          if (!tra_sin_pi_multiple(&arg_val[i].lp_value, &arg_val[j].lp_value, pi_star, k, d)) continue;

          // w = 2n = 2D + K - 1, if 2d is an integer: n is an integer iff w is even, and then
          // (-1)^n = 1 iff w = 0 mod 4
          mpz_mul_ui(w, mpq_numref(d), 2);
          if (!mpz_divisible_p(w, mpq_denref(d))) continue;
          mpz_divexact(w, w, mpq_denref(d));
          if (k < 0) mpz_ui_sub(w, 2, w);
          mpz_add_ui(w, w, (unsigned long) (K - 1));
          unsigned long r = mpz_fdiv_ui(w, 4);
          if (r % 2 == 1) continue;

          // d pi is written 0 for d = 0, as pi may then be NULL_TERM
          term_t k_term = _o_yices_rational32(k, 1);
          term_t d_pi = mpq_sgn(d) == 0 ? zero : _o_yices_mul(_o_yices_mpq(d), pi);
          if (_o_yices_sub(dep[i], _o_yices_add(_o_yices_mul(k_term, dep[j]), d_pi)) != zero) continue;

          term_t P_prev = _o_yices_rational32(1, 1), P = sin_b;
          for (long l = 1; l < K; l++) {
            term_t P_next = _o_yices_sub(_o_yices_mul(_o_yices_rational32(2, 1), _o_yices_mul(sin_b, P)), P_prev);
            P_prev = P;
            P = P_next;
          }
          term_t Q = (r == 0) ? _o_yices_sub(sin_a, P) : _o_yices_add(sin_a, P);
          term_t L = tra_conflict_cutting_literal(ctx, Q, zero, true);
          if (L == NULL_TERM) L = tra_conflict_cutting_literal(ctx, Q, zero, false);
          if (L == NULL_TERM) continue; // R holds on the trail

          term_t E = _o_yices_arith_eq_atom(_o_yices_sub(a, _o_yices_mul(k_term, b)), d_pi);
          assert(E != false_term); // a* - kb* - d pi* = 0 exactly
          if (E != true_term) ivector_push(conflict, E);
          ivector_push(conflict, L);
          conflict_generated = true;

          if (tra_trace_enabled(self->tra, "tra::sin")) {
            tra_trace_iprintf(self->tra, "[sin: multiple angle: ");
            tra_trace_print_term(self->tra, sin_a);
            tra_trace_printf(self->tra, " and ");
            tra_trace_print_term(self->tra, sin_b);
            tra_trace_printf(self->tra, ", k = %ld, d = ", k);
            mpq_out_str(ctx_trace_out(ctx), 10, d);
            tra_trace_printf(self->tra, "]\n");
          }
          goto done;
        }
      }
    }
  }

done:
  for (uint32_t i = 0; i < m; i++) mcsat_value_destruct(&arg_val[i]);
  if (pi_val_ok) mcsat_value_destruct(&pi_val);
  safe_free(app);
  safe_free(arg_val);
  safe_free(dep);
  mpq_clear(d);
  mpz_clear(w);

  assert(!conflict_generated || conflict->size > 0);
  return conflict_generated;
}

/* =====================
    Conflict strategies
   ===================== */

// The conflict strategies of sin (see tra_func_strategy_t). The atom strategies, the more
// expressive ones, are not initially enabled. The numbers in the explanations follow
// TRA_SIN_MULTIPLE_MAX_K, TRA_SIN_PI_TAYLOR_MAX_DEGREE_AT_0, TRA_SIN_PI_TAYLOR_MAX_DEGREE_ELSEWHERE,
// TRA_SIN_DYADIC_MIN_DEGREE and TRA_SIN_DYADIC_MAX_DEGREE: update them together
static const tra_func_strategy_t tra_sin_strategies[] = {
  { "pair", 
    "Two sin applications of the atom whose arguments differ or sum by d pi, with 6d an integer, "
    "related by an identity. "
    "Conflict: 1 or 2 literals: a -+ b = d pi, linear, and an identity of degree 1 or 2 in the two sines. "
    "Independent of the parameter delta.",
    false, tra_sin_pair_conflict },
  { "multiple_angle", 
    "Two sin applications of the atom with a - kb = d pi and 2 <= |k| <= 6, related by a Chebyshev identity. "
    "Conflict: 1 or 2 literals: a - kb = d pi, linear, and sin a = +-T_|k|(sin b), of degree |k|. "
    "Independent of the parameter delta.",
    false, tra_sin_multiple_angle_conflict },
  { "linear", 
    "A tangent or chord of sin near the argument, guarded by a region where sin is convex or concave. "
    "Conflict: 1 to 3 literals, all linear in the argument and sin(x); no pi. "
    "Its constants depend on the parameter delta.",
    true, tra_sin_linear_conflict },
  { "pi_taylor", 
    "The negation of a Taylor bound of sin at the multiple n pi/2 nearest to the argument, with pi symbolic, "
    "guarded by an integer window and, for even n, the sign of x - n pi/2. "
    "Conflict: 1 to 4 literals; the bound has degree 1 in sin(x) and, jointly in the argument and pi, degree at "
    "most 9 at n = 0 (no pi) and at most 5 elsewhere; the guards are linear. "
    "Its coefficients do not depend on the parameter delta.",
    true, tra_sin_pi_taylor_conflict },
  { "dyadic_taylor", 
    "The negation of a Taylor bound of sin at a dyadic centre near the argument. "
    "Conflict: 1 literal, of even degree in [2, 16] in the argument and 1 in sin(x); no pi. "
    "Its centre and coefficients depend on the parameter delta.",
    true, tra_sin_dyadic_taylor_conflict },
};

/* ==========
    Destruct
   ========== */

static void tra_sin_destruct(tra_func_plugin_t* self) {
  safe_free(self->dependencies);
  safe_free(self->dependencies_plugins);
  safe_free(self);
}

/* ===========
    Allocator
   =========== */

tra_func_plugin_t* tra_sin_plugin_allocator(const struct tra_plugin_s* tra){
  tra_func_plugin_t* plugin = (tra_func_plugin_t*) safe_malloc(sizeof(tra_func_plugin_t));
  plugin->name = "sin";
  plugin->arity = 1;
  plugin->is_total_continuous = true;

  plugin->tra = tra;

  plugin->dependencies = (const char**) safe_malloc(sizeof(const char*));
  plugin->dependencies[0] = "pi";
  plugin->num_dependencies = 1;
  plugin->dependencies_plugins = (tra_func_plugin_t**) safe_malloc(sizeof(tra_func_plugin_t*));
  plugin->add_dependency = tra_sin_add_dependency;

  plugin->abstract_domain = NULL;
  plugin->add_abstract_domain = tra_func_default_add_abstract_domain;

  plugin->new_term_notify = tra_sin_new_term_notify;

  plugin->is_in_domain = tra_func_default_is_in_domain;

  plugin->let_na_decide = tra_func_default_let_na_decide;

  plugin->get_implicit_variables = tra_func_default_get_implicit_variables;

  plugin->hint_argument_value = tra_func_default_hint_argument_value;

  plugin->eval = tra_sin_eval;

  plugin->refine_precision = tra_func_default_refine_precision; // sin is 1-Lipschitz

  plugin->strategies = tra_sin_strategies;
  plugin->num_strategies = sizeof(tra_sin_strategies) / sizeof(tra_sin_strategies[0]);
  plugin->default_conflict = tra_func_default_get_conflict;

  plugin->destruct = tra_sin_destruct;

  return plugin;
}
