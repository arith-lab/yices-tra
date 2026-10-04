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
 * tra_arb_domain.c
 *
 * Implementation of tra_ilib_t using the ARB library for rigorous
 * interval arithmetic.
 *
 * ARB documentation: https://arblib.org/
 *
 * ARB uses:
 *   arb_t    — interval ball type, defined as arb_struct[1] (stack-friendly)
 *   slong    — FLINT's word-sized signed integer, used as precision (bits)
 *   fmpz_t   — FLINT's arbitrary-precision integer, used for exact conversions
 *   fmpq_t   — FLINT's rational, the coefficients of the number of an exact element
 *
 * All tra_itype_t values managed by this backend are heap-allocated
 * arb_value_t objects: a known number (a rational, or an arb_symbolic_t), or a ball.
 * Use the helper as_value() to perform the cast.
 *
 * To use this backend, include abstractions/tra_arb_domain.h and pass
 * &tra_arb_ilib wherever a tra_ilib_t* is expected.
 */

#include "mcsat/tra/abstractions/tra_arb_domain.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <flint/arb.h>
#include <flint/arf.h>
#include <flint/flint.h>
#include <flint/fmpq.h>
#include <flint/fmpz.h>
#include <flint/mag.h>
#include <gmp.h>
#include <mpfr.h>
#include <poly/algebraic_number.h>
#include <poly/dyadic_interval.h>
#include <poly/dyadic_rational.h>
#include <poly/poly.h>
#include <poly/upolynomial.h>
#include <poly/value.h>

#include "mcsat/mcsat_types.h"
#include "mcsat/value.h"
#include "terms/rationals.h"
#include "utils/memalloc.h"

#include "mcsat/tra/utils/tra_exact_values.h"


/* ================
 *  Representation
 * ================ */

/*
 * A symbolic value: the real number
 *
 *   rational + sqrt_coeff * sqrt(radicand) + pi_coeff * pi + exp_coeff * exp(exp_arg)
 *
 * with rational coefficients, a rational exp_arg and an integer radicand. Normal form: radicand
 * is 0 iff sqrt_coeff is 0, and is never a square (so its square root is irrational), but need
 * not be square-free; exp_arg is 0 iff exp_coeff is 0 (exp(0) is never created). The number is
 * rational if sqrt_coeff, pi_coeff and exp_coeff are 0, and it is treated as rational only then
 * (the converse holds when pi_coeff or exp_coeff is 0; whether pi + e is rational is open).
 */
typedef struct {
  fmpq_t rational;
  fmpq_t sqrt_coeff;
  fmpz_t radicand;
  fmpq_t pi_coeff;
  fmpq_t exp_coeff;
  fmpq_t exp_arg;
} arb_symbolic_t;

static bool symbolic_is_rational(const arb_symbolic_t* s) {
  return fmpq_is_zero(s->sqrt_coeff) && fmpq_is_zero(s->pi_coeff) && fmpq_is_zero(s->exp_coeff);
}

/*
 * A value x of the domain (see tra_arb_domain.h):
 *  - if x->exact, x abstracts the single number x->number, in normal form; otherwise
 *    x->number is unspecified;
 *  - if value_is_rational(x), γ(x) = {x->number.rational}, and x->ball is unspecified;
 *  - otherwise γ(x) = x->ball, which contains x->number if x->exact.
 * Operations between exact values are exact when the operations on symbolic values give the
 * result; otherwise, they are computed on the balls.
 */
typedef struct {
  bool exact;
  arb_symbolic_t number;
  arb_t ball;
} arb_value_t;

/* Cast opaque pointer to arb_value_t* */
static inline arb_value_t* as_value(tra_itype_t x) {
  assert(x != NULL);
  return (arb_value_t*) x;
}

/* x is exact and its number is rational: the point {number.rational} */
static bool value_is_rational(const arb_value_t* x) {
  return x->exact && symbolic_is_rational(&x->number);
}

/* x becomes exact with the rational number.rational, which the caller has set */
static void value_set_rational(arb_value_t* x) {
  x->exact = true;
  fmpq_zero(x->number.sqrt_coeff);
  fmpz_zero(x->number.radicand);
  fmpq_zero(x->number.pi_coeff);
  fmpq_zero(x->number.exp_coeff);
  fmpq_zero(x->number.exp_arg);
}

/* ====================================
 *  Internal helpers: symbolic values
 * ==================================== */

/*
 * The operations below compute exactly on symbolic values.
 *   Requires: the result c is not initialised and aliases no argument.
 *   Ensures:  result ==> c is initialised, in normal form, to the exact result;
 *             !result ==> c is not initialised. A false result does not mean that the exact
 *             result has no normal form (e.g. sqrt(2) sqrt(3) = sqrt(6)).
 * They never multiply pi or exp(exp_arg) by pi or by an exponential (see tra_arb_domain.h for
 * why). Two values are compatible when their radicands are equal or one of their sqrt_coeff is 0,
 * and their exp_args are equal or one of their exp_coeff is 0.
 */

/* s := 0 */
static void symbolic_init(arb_symbolic_t* s) {
  fmpq_init(s->rational);
  fmpq_init(s->sqrt_coeff);
  fmpz_init(s->radicand);
  fmpq_init(s->pi_coeff);
  fmpq_init(s->exp_coeff);
  fmpq_init(s->exp_arg);
}

static void symbolic_clear(arb_symbolic_t* s) {
  fmpq_clear(s->rational);
  fmpq_clear(s->sqrt_coeff);
  fmpz_clear(s->radicand);
  fmpq_clear(s->pi_coeff);
  fmpq_clear(s->exp_coeff);
  fmpq_clear(s->exp_arg);
}

static void symbolic_set(arb_symbolic_t* dst, const arb_symbolic_t* src) {
  fmpq_set(dst->rational, src->rational);
  fmpq_set(dst->sqrt_coeff, src->sqrt_coeff);
  fmpz_set(dst->radicand, src->radicand);
  fmpq_set(dst->pi_coeff, src->pi_coeff);
  fmpq_set(dst->exp_coeff, src->exp_coeff);
  fmpq_set(dst->exp_arg, src->exp_arg);
}

/* Restores the normal form (see arb_symbolic_t) after a change of the coefficients */
static void symbolic_normalize(arb_symbolic_t* s) {
  if (fmpq_is_zero(s->sqrt_coeff)) fmpz_zero(s->radicand);
  if (fmpq_is_zero(s->exp_coeff)) fmpq_zero(s->exp_arg);
  assert(fmpq_is_zero(s->exp_coeff) || !fmpq_is_zero(s->exp_arg)); // exp(0) is never created
}

/*
 * c := the algebraic number a, if it has degree at most 2. A rational stored as a root of a
 * reducible polynomial (libpoly does not factor) gives a rational value.
 */
static bool symbolic_of_algebraic(arb_symbolic_t* c, const lp_algebraic_number_t* a) {
  // A point: then f may be gone and only I.a = m/2^k is valid (a refinement can collapse a)
  if (a->f == NULL || lp_dyadic_interval_is_point(&a->I)) {
    symbolic_init(c);
    fmpz_set_mpz(fmpq_numref(c->rational), &a->I.a.a);
    fmpz_one_2exp(fmpq_denref(c->rational), a->I.a.n);
    _fmpq_canonicalise(fmpq_numref(c->rational), fmpq_denref(c->rational));
    return true;
  }
  size_t deg = lp_upolynomial_degree(a->f);
  if (deg > 2) return false;

  // f = A x^2 + B x + C (A = 0 when deg = 1)
  lp_integer_t coeff[3];
  for (size_t i = 0; i < 3; i++) mpz_init(&coeff[i]);
  lp_upolynomial_unpack(a->f, coeff);
  fmpz_t A, B, C, disc, t;
  fmpz_init(A);
  fmpz_init(B);
  fmpz_init(C);
  fmpz_init(disc);
  fmpz_init(t);
  fmpz_set_mpz(A, &coeff[2]);
  fmpz_set_mpz(B, &coeff[1]);
  fmpz_set_mpz(C, &coeff[0]);
  for (size_t i = 0; i < 3; i++) mpz_clear(&coeff[i]);

  symbolic_init(c);
  if (deg == 1) {
    // The root -C/B
    assert(!fmpz_is_zero(B));
    fmpz_neg(C, C);
    fmpq_set_fmpz_frac(c->rational, C, B);
  } else {
    // The roots r1 < r2 of f are (-B + sign sqrt(disc))/(2A), disc = B^2 - 4AC, with sign = sgn A
    // for r2. a lies in the open interval (lo, hi), which excludes the other root: so a = r2 iff
    // hi > r2 iff f(hi) has the sign of A, and f(hi) = 0 means hi = r2, so a = r1.
    fmpz_mul(disc, B, B);
    fmpz_mul(t, A, C);
    fmpz_submul_ui(disc, t, 4);
    assert(!fmpz_is_zero(A) && fmpz_sgn(disc) >= 0); // a has degree 2 and is real
    int sign = lp_upolynomial_sgn_at_dyadic_rational(a->f, &a->I.b);

    fmpz_set_si(t, sign != 0 ? sign : -fmpz_sgn(A));
    fmpz_mul_2exp(A, A, 1);                     // 2A
    fmpz_neg(B, B);
    fmpq_set_fmpz_frac(c->rational, B, A);      // -B/(2A)
    fmpq_set_fmpz_frac(c->sqrt_coeff, t, A);    // sign/(2A)
    if (fmpz_is_square(disc)) {
      fmpz_sqrt(t, disc);
      fmpq_mul_fmpz(c->sqrt_coeff, c->sqrt_coeff, t);
      fmpq_add(c->rational, c->rational, c->sqrt_coeff);
      fmpq_zero(c->sqrt_coeff);
    } else {
      fmpz_set(c->radicand, disc);
    }
  }
  fmpz_clear(A);
  fmpz_clear(B);
  fmpz_clear(C);
  fmpz_clear(disc);
  fmpz_clear(t);
  return true;
}

/* c := x + y, or x - y if sub, if x and y are compatible */
static bool symbolic_add_sub(arb_symbolic_t* c, const arb_symbolic_t* x, const arb_symbolic_t* y, bool sub) {
  bool x_sqrt = !fmpq_is_zero(x->sqrt_coeff), x_exp = !fmpq_is_zero(x->exp_coeff);
  if (x_sqrt && !fmpq_is_zero(y->sqrt_coeff) && !fmpz_equal(x->radicand, y->radicand)) return false;
  if (x_exp && !fmpq_is_zero(y->exp_coeff) && !fmpq_equal(x->exp_arg, y->exp_arg)) return false;

  symbolic_init(c);
  void (*op)(fmpq_t, const fmpq_t, const fmpq_t) = sub ? fmpq_sub : fmpq_add;
  op(c->rational, x->rational, y->rational);
  op(c->sqrt_coeff, x->sqrt_coeff, y->sqrt_coeff);
  fmpz_set(c->radicand, x_sqrt ? x->radicand : y->radicand);
  op(c->pi_coeff, x->pi_coeff, y->pi_coeff);
  op(c->exp_coeff, x->exp_coeff, y->exp_coeff);
  fmpq_set(c->exp_arg, x_exp ? x->exp_arg : y->exp_arg);
  symbolic_normalize(c);
  return true;
}

/* c := x * y, if x or y is rational, or both are algebraic (pi_coeff = exp_coeff = 0) with
 * compatible radicands */
static bool symbolic_mul(arb_symbolic_t* c, const arb_symbolic_t* x, const arb_symbolic_t* y) {
  if (symbolic_is_rational(y)) {
    const arb_symbolic_t* t = x; // y is rational: then x is the rational one below
    x = y;
    y = t;
  }

  if (symbolic_is_rational(x)) {
    // A rational times y: every coefficient of y is scaled
    symbolic_init(c);
    symbolic_set(c, y);
    fmpq_mul(c->rational, y->rational, x->rational);
    fmpq_mul(c->sqrt_coeff, y->sqrt_coeff, x->rational);
    fmpq_mul(c->pi_coeff, y->pi_coeff, x->rational);
    fmpq_mul(c->exp_coeff, y->exp_coeff, x->rational);
  } else {
    bool algebraic = fmpq_is_zero(x->pi_coeff) && fmpq_is_zero(x->exp_coeff)
      && fmpq_is_zero(y->pi_coeff) && fmpq_is_zero(y->exp_coeff);
    if (!algebraic || !fmpz_equal(x->radicand, y->radicand)) return false;

    // (u + v sqrt(D)) (u' + v' sqrt(D)) = (v v' D + u u') + (u v' + v u') sqrt(D)
    symbolic_init(c);
    fmpq_mul(c->rational, x->sqrt_coeff, y->sqrt_coeff);
    fmpq_mul_fmpz(c->rational, c->rational, x->radicand);
    fmpq_addmul(c->rational, x->rational, y->rational);
    fmpq_mul(c->sqrt_coeff, x->rational, y->sqrt_coeff);
    fmpq_addmul(c->sqrt_coeff, x->sqrt_coeff, y->rational);
    fmpz_set(c->radicand, x->radicand);
  }
  symbolic_normalize(c);
  return true;
}

/* c := x / y, if y is a nonzero algebraic number (pi_coeff = exp_coeff = 0) and symbolic_mul
 * gives x * (1/y) */
static bool symbolic_div(arb_symbolic_t* c, const arb_symbolic_t* x, const arb_symbolic_t* y) {
  if (!fmpq_is_zero(y->pi_coeff) || !fmpq_is_zero(y->exp_coeff)) return false;

  // 1/(u + v sqrt(D)) = (u - v sqrt(D))/N with N = u^2 - v^2 D, and N = 0 iff u = v = 0
  // (sqrt(D) is irrational)
  fmpq_t norm;
  fmpq_init(norm);
  fmpq_mul(norm, y->sqrt_coeff, y->sqrt_coeff);
  fmpq_mul_fmpz(norm, norm, y->radicand);
  fmpq_neg(norm, norm);
  fmpq_addmul(norm, y->rational, y->rational);
  bool ok = !fmpq_is_zero(norm);
  if (ok) {
    arb_symbolic_t inverse;
    symbolic_init(&inverse);
    symbolic_set(&inverse, y);
    fmpq_div(inverse.rational, y->rational, norm);
    fmpq_div(inverse.sqrt_coeff, y->sqrt_coeff, norm);
    fmpq_neg(inverse.sqrt_coeff, inverse.sqrt_coeff);
    ok = symbolic_mul(c, x, &inverse);
    symbolic_clear(&inverse);
  }
  fmpq_clear(norm);
  return ok;
}

/* c := x^n, with x^0 = 1, if x is algebraic (pi_coeff = exp_coeff = 0) or n <= 1 */
static bool symbolic_pow_ui(arb_symbolic_t* c, const arb_symbolic_t* x, unsigned long n) {
  if (n > 1 && !(fmpq_is_zero(x->pi_coeff) && fmpq_is_zero(x->exp_coeff))) return false;

  // In Q(sqrt(radicand)) symbolic_mul gives every product, and n <= 1 multiplies x only by 1
  symbolic_init(c);
  fmpq_one(c->rational);
  for (unsigned long i = 0; i < n; i++) {
    arb_symbolic_t t;
    bool ok = symbolic_mul(&t, c, x);
    assert(ok);
    (void) ok;
    symbolic_clear(c);
    *c = t;
  }
  return true;
}

/* =====================================
 *  Internal helpers: working precision
 * ===================================== */

// ARB's minimum precision
#define ARB_MIN_PREC 42

// Magnitudes above this are treated as unusable
#define ARB_MAX_USABLE_MAG (1L << 26)

/* ceil(log2(m)) for a finite non-zero m, from its exponent: a mag lies in
 * [2^(exp-1), 2^exp), so the answer is exp, or exp-1 when m is exactly 2^(exp-1).
 * Returns false if the exponent does not fit a word
 */
static bool ceil_log2_of_mag(mag_srcptr m, slong* out) {
  if (COEFF_IS_MPZ(MAG_EXP(m))) return false;

  slong e = (slong) MAG_EXP(m);
  *out = (mag_cmp_2exp_si(m, e - 1) <= 0) ? e - 1 : e;
  return true;
}

/* Lower bound on log2|x|: |x| >= 2^result. May be negative; 0 if x contains zero. */
static slong ball_mag_lower_bits(arb_struct* x) {
  mag_t m;
  mag_init(m);
  arb_get_mag_lower(m, x);
  // A mag lies in [2^(exp-1), 2^exp), see ceil_log2_of_mag
  slong bits = (mag_is_special(m) || COEFF_IS_MPZ(MAG_EXP(m))) ? 0 : (slong) MAG_EXP(m) - 1;
  mag_clear(m);
  return bits;
}

/* ARB working bits for an operation requested at precision 'prec' whose result
 * has magnitude at most 2^result_mag.
 *
 * This is THE way to obtain a working precision in this file.
 */
static slong working_prec(tra_prec_t prec, slong result_mag) {
  assert(prec != INF_PREC);

  // Rounding the midpoint to prec + result_mag + 3 bits widens the result by at
  // most 2^-prec / 4, which leaves room for a w(img) of up to 2^-(prec+1).
  tra_prec_t arb_prec = tra_add_precisions(prec, result_mag + 3);
  return arb_prec == NO_PREC ? ARB_MIN_PREC : arb_prec;
}

/* Upper bound on log2|x|: |x| <= 2^result. Never negative.
 * Returns 0 when |x| is too large to compute with (magnitude at least
 * ARB_MAX_USABLE_MAG, or an exponent that does not fit a word); in this case the
 * caller works at a lower precision and gets a wider result.
 */
static slong value_get_mag_bits(arb_value_t* x) {
  slong bits = 0;
  if (value_is_rational(x)) {
    // |num| < 2^bits(num) and |den| >= 2^(bits(den) - 1)
    if (!fmpq_is_zero(x->number.rational)) bits = (slong) fmpz_bits(fmpq_numref(x->number.rational)) - (slong) fmpz_bits(fmpq_denref(x->number.rational)) + 1;
  } else {
    mag_t m;
    mag_init(m);
    arb_get_mag(m, x->ball);
    if (!mag_is_zero(m) && !mag_is_inf(m)) ceil_log2_of_mag(m, &bits);
    mag_clear(m);
  }
  return (bits > 0 && bits < ARB_MAX_USABLE_MAG) ? bits : 0;
}

/* ==========================
 *  Internal helpers: values
 * ========================== */

/* Sign of a - q, where q is the value of the rational x and a is finite */
static int rational_cmp_mpfr(arb_value_t* x, const mpfr_t a) {
  assert(value_is_rational(x));
  mpq_t q;
  mpq_init(q);
  fmpq_get_mpq(q, x->number.rational);
  int cmp = mpfr_cmp_q(a, q);
  mpq_clear(q);
  return cmp;
}

// A point ball is made rational only if 2^-ARB_MAX_EXACT_EXP < |mid| < 2^ARB_MAX_EXACT_EXP
#define ARB_MAX_EXACT_EXP (1L << 20)

/*
 * Requires: s is NULL, or s is initialised and, if s is not rational, x->ball encloses it.
 * Modifies: s, which is cleared.
 * Ensures:  s != NULL ==> x is exact with the number s;
 *           s = NULL ==> x is its ball, made rational when the ball is a finite point of moderate
 *           magnitude.
 */
static void value_set_number(arb_value_t* x, arb_symbolic_t* s) {
  if (s != NULL) {
    symbolic_set(&x->number, s);
    symbolic_clear(s);
    x->exact = true;
    return;
  }

  x->exact = false;
  if (!arb_is_exact(x->ball) || !arb_is_finite(x->ball)) return;

  arf_srcptr mid = arb_midref(x->ball);
  if (!arf_is_zero(mid) && (arf_cmpabs_2exp_si(mid, ARB_MAX_EXACT_EXP) >= 0
                            || arf_cmpabs_2exp_si(mid, -ARB_MAX_EXACT_EXP) <= 0)) {
    return;
  }
  arf_get_fmpq(x->number.rational, mid);
  value_set_rational(x);
}

/* A ball enclosing x. It is either 
 *  - the ball we already have 
 *  - a point ball if x is dyadic 
 *  - a new ball rounded to wp bits
 */
static arb_struct* value_ball(arb_value_t* x, slong wp, arb_t tmp) {
  if (!value_is_rational(x)) return x->ball;

  const fmpz* den = fmpq_denref(x->number.rational);
  slong k = fmpz_val2(den);
  if ((slong) fmpz_bits(den) == k + 1) {
    arb_set_fmpz(tmp, fmpq_numref(x->number.rational));
    arb_mul_2exp_si(tmp, tmp, -k);
  } else {
    arb_set_fmpq(tmp, x->number.rational, wp);
  }
  return tmp;
}

/* A ball holding the algebraic number a, whose isolating interval is first refined
 * to width at most 2^-(prec+1).
 */
static void algebraic_ball(arb_t out, const lp_algebraic_number_t* a, tra_prec_t prec) {
  tra_prec_t refine_target = tra_add_precisions(prec, 1);
  while (a->f && lp_dyadic_interval_size(&a->I) > -refine_target) {
    lp_algebraic_number_refine_const(a);
  }

  // The refinement step above might collapse to a single rational point
  const lp_dyadic_rational_t* upper = lp_dyadic_interval_is_point(&a->I) ? &a->I.a : &a->I.b;

  mpfr_t lo, hi;
  mpfr_init2(lo, mpz_sizeinbase(&a->I.a.a, 2) + 2);
  mpfr_init2(hi, mpz_sizeinbase(&upper->a, 2) + 2);
  mpfr_set_z_2exp(lo, &a->I.a.a, -(mpfr_exp_t) a->I.a.n, MPFR_RNDD);
  mpfr_set_z_2exp(hi, &upper->a, -(mpfr_exp_t) upper->n, MPFR_RNDU);

  slong mag_lo = tra_mpfr_magnitude_bits(lo);
  slong mag_hi = tra_mpfr_magnitude_bits(hi);
  arb_set_interval_mpfr(out, lo, hi, working_prec(prec, FLINT_MAX(mag_lo, mag_hi)));

  mpfr_clear(lo);
  mpfr_clear(hi);
}

/* =======================================
 *  Internal helpers: abstract operations
 * ======================================= */

/*
 * z := the hull of x and y. Non-finite balls stand for R. Containment is tested
 * first, so that an input is copied unchanged. Unlike arb_union, the radius is kept
 * exact when (hi - lo)/2 is representable (so that the hull of {0,2} is
 * nonnegative), else rounded up. The midpoint rounding is added to the radius.
 */
static void ball_hull(arb_t z, const arb_t x, const arb_t y, slong wp) {
  if (arb_contains(x, y)) { arb_set(z, x); return; }
  if (arb_contains(y, x)) { arb_set(z, y); return; }
  if (!arb_is_finite(x) || !arb_is_finite(y)) { arb_zero_pm_inf(z); return; }

  arf_t lo, hi, t, mid, half_width;
  mag_t rad;
  arf_init(lo);
  arf_init(hi);
  arf_init(t);
  arf_init(mid);
  arf_init(half_width);
  mag_init(rad);

  arb_get_lbound_arf(lo, x, wp);
  arb_get_lbound_arf(t, y, wp);
  arf_min(lo, lo, t);
  arb_get_ubound_arf(hi, x, wp);
  arb_get_ubound_arf(t, y, wp);
  arf_max(hi, hi, t);

  int mid_inexact = arf_add(mid, lo, hi, wp, ARF_RND_DOWN);
  arf_mul_2exp_si(mid, mid, -1);

  arf_sub(half_width, hi, lo, wp, ARF_RND_UP);
  arf_mul_2exp_si(half_width, half_width, -1);

  arf_get_mag_lower(rad, half_width);
  arf_set_mag(t, rad);
  if (!arf_equal(t, half_width)) arf_get_mag(rad, half_width);

  if (mid_inexact) arf_mag_add_ulp(rad, rad, mid, wp);

  arf_swap(arb_midref(z), mid);
  mag_swap(arb_radref(z), rad);

  arf_clear(lo);
  arf_clear(hi);
  arf_clear(t);
  arf_clear(mid);
  arf_clear(half_width);
  mag_clear(rad);
}

typedef enum { VALUE_ADD, VALUE_SUB, VALUE_MUL, VALUE_DIV, VALUE_JOIN } value_op_t;

/* c := a op b at wp, the hull for VALUE_JOIN. Exact when a and b are exact and the operation on
 * their numbers gives the result; otherwise, and when that result is irrational, computed on the
 * balls (see value_ball), a product by a point ball exactly. c may alias a or b.
 */
static void value_binop(arb_value_t* c, arb_value_t* a, arb_value_t* b, slong wp, value_op_t op) {
  static void (*const rational_op[])(fmpq_t, const fmpq_t, const fmpq_t) = { fmpq_add, fmpq_sub, fmpq_mul, fmpq_div };
  static void (*const ball_op[])(arb_t, const arb_t, const arb_t, slong) = { arb_add, arb_sub, arb_mul, arb_div, ball_hull };

  // Two rationals, but for a division by 0 (see arb_ilib_div): the fast path for the most frequent
  // case, equal to the operation on their numbers
  if (op != VALUE_JOIN && value_is_rational(a) && value_is_rational(b)
      && (op != VALUE_DIV || !fmpq_is_zero(b->number.rational))) {
    rational_op[op](c->number.rational, a->number.rational, b->number.rational);
    value_set_rational(c);
    return;
  }

  arb_symbolic_t result;
  bool exact = false;
  if (a->exact && b->exact) {
    switch (op) {
    case VALUE_ADD:  exact = symbolic_add_sub(&result, &a->number, &b->number, false); break;
    case VALUE_SUB:  exact = symbolic_add_sub(&result, &a->number, &b->number, true); break;
    case VALUE_MUL:  exact = symbolic_mul(&result, &a->number, &b->number); break;
    case VALUE_DIV:  exact = symbolic_div(&result, &a->number, &b->number); break;
    case VALUE_JOIN: break;
    }
  }
  if (exact && symbolic_is_rational(&result)) {
    value_set_number(c, &result);
    return;
  }

  arb_t ta, tb;
  arb_init(ta);
  arb_init(tb);
  arb_struct* x = value_ball(a, wp, ta);
  arb_struct* y = value_ball(b, wp, tb);
  ball_op[op](c->ball, x, y, (op == VALUE_MUL && (arb_is_exact(x) || arb_is_exact(y))) ? ARF_PREC_EXACT : wp);
  value_set_number(c, exact ? &result : NULL);
  arb_clear(ta);
  arb_clear(tb);
}

/* x := op(a) at wp, the ball of a rational a being rounded to arg_wp bits. x may alias a.
 * op must be strongly transcendental: op(a) is transcendental for every rational a but
 * one, special, which the caller handles. For instance, by Lindemann-Weierstrass, e^a,
 * sin(a) and cos(a) are transcendental for every rational a != 0, and log(a) for every
 * rational a != 1.
 */
static void value_strongly_transcendental_unop(arb_value_t* x, arb_value_t* a, slong arg_wp, slong wp,
                                               void (*arb_op)(arb_t, const arb_t, slong)) {
  arb_t ta;
  arb_init(ta);
  arb_op(x->ball, value_ball(a, arg_wp, ta), wp);
  value_set_number(x, NULL);
  arb_clear(ta);
}

/* ===========
 *  Lifecycle
 * =========== */

static tra_itype_t arb_ilib_alloc(void) {
  arb_value_t* x = (arb_value_t*) safe_malloc(sizeof(arb_value_t));
  x->exact = true;
  symbolic_init(&x->number);
  arb_init(x->ball);
  return (tra_itype_t) x;
}

static void arb_ilib_free(tra_itype_t x) {
  symbolic_clear(&as_value(x)->number);
  arb_clear(as_value(x)->ball);
  free(x);
}

/* ============
 *  Extraction
 * ============ */

static void arb_ilib_get_interval_overapproximation_mpfr(mpfr_t lo, mpfr_t hi, tra_itype_t x) {
  arb_value_t* v = as_value(x);
  if (!value_is_rational(v)) {
    arb_get_interval_mpfr(lo, hi, v->ball);
    return;
  }
  fmpq_get_mpfr(lo, v->number.rational, MPFR_RNDD);
  fmpq_get_mpfr(hi, v->number.rational, MPFR_RNDU);
}

/* A ball is convex: [lo,hi] is in it iff lo and hi are (arb_contains_mpfr is exact) */
static bool arb_ilib_contains_interval_mpfr(tra_itype_t x, const mpfr_t lo, const mpfr_t hi) {
  assert(!mpfr_nan_p(lo) && !mpfr_nan_p(hi));
  if (mpfr_cmp(lo, hi) > 0) return true; // empty box, vacuously inside

  arb_value_t* v = as_value(x);
  // Only the point box [q, q] is inside {q}
  if (value_is_rational(v)) return mpfr_number_p(lo) && mpfr_equal_p(lo, hi) && rational_cmp_mpfr(v, lo) == 0;
  arb_struct* b = v->ball;

  if (arf_is_nan(arb_midref(b))) return false;
  if (mag_is_inf(arb_radref(b))) return true;  // x is all of R, see arb_ilib_set_R
  if (!arf_is_finite(arb_midref(b))) return false;
  return arb_contains_mpfr(b, lo) && arb_contains_mpfr(b, hi);
}

static tra_prec_t arb_ilib_get_precision(tra_itype_t x) {
  arb_value_t* v = as_value(x);
  if (value_is_rational(v)) return INF_PREC;

  mag_srcptr rad = arb_radref(v->ball);
  if (!arf_is_finite(arb_midref(v->ball)) || mag_is_inf(rad)) return NO_PREC;

  if (mag_is_zero(rad)) return INF_PREC;

  // The precision is floor(-log2(2 rad)) = -ceil(log2(rad)) - 1, so that the width 2 rad is at
  // most 2^-precision. It must be positive (it is not, e.g., after an overflow); a mag exponent
  // that fits a word keeps it below INF_PREC.
  slong rad_bits;
  return (ceil_log2_of_mag(rad, &rad_bits) && rad_bits < -1) ? (tra_prec_t) (-rad_bits - 1) : NO_PREC;
}

/* ============
 *  Assignment
 * ============ */

static void arb_ilib_set(tra_itype_t dst, tra_itype_t src) {
  arb_value_t *d = as_value(dst), *s = as_value(src);
  d->exact = s->exact;
  if (s->exact) symbolic_set(&d->number, &s->number);
  if (!value_is_rational(s)) arb_set(d->ball, s->ball);
}

static void arb_ilib_set_z(tra_itype_t x, long n) {
  fmpq_set_si(as_value(x)->number.rational, (slong) n, 1);
  value_set_rational(as_value(x));
}

static void arb_ilib_set_mpz(tra_itype_t x, const mpz_t n) {
  fmpz_set_mpz(fmpq_numref(as_value(x)->number.rational), n);
  fmpz_one(fmpq_denref(as_value(x)->number.rational));
  value_set_rational(as_value(x));
}

static void arb_ilib_set_mpz_div(tra_itype_t x,
                                  const mpz_t a, const mpz_t b,
                                  tra_prec_t prec) {
  assert(mpz_sgn(b) > 0);
  assert(prec != INF_PREC);
  (void) prec; // a/b is kept exact

  arb_value_t* v = as_value(x);
  fmpz_set_mpz(fmpq_numref(v->number.rational), a);
  fmpz_set_mpz(fmpq_denref(v->number.rational), b);
  _fmpq_canonicalise(fmpq_numref(v->number.rational), fmpq_denref(v->number.rational));
  value_set_rational(v);
}

static void arb_ilib_set_mpfr(tra_itype_t x, const mpfr_t a, tra_prec_t prec) {
  assert(prec != INF_PREC);
  assert(mpfr_number_p(a));

  arb_value_t* v = as_value(x);
  // The point a, rounded to the working precision
  arb_set_interval_mpfr(v->ball, a, a, working_prec(prec, tra_mpfr_magnitude_bits(a)));
  value_set_number(v, NULL);
}

static void arb_ilib_set_algebraic_lp(tra_itype_t out_, const lp_algebraic_number_t* a, tra_prec_t prec) {
  assert(prec != INF_PREC);
  arb_value_t* out = as_value(out_);
  algebraic_ball(out->ball, a, prec);
  value_set_number(out, NULL);

  // a exactly when it has degree at most 2: a rational a becomes a point, a quadratic one keeps
  // its ball
  arb_symbolic_t number;
  if (!out->exact && symbolic_of_algebraic(&number, a)) value_set_number(out, &number);
}

static void arb_ilib_set_R(tra_itype_t x) {
  as_value(x)->exact = false;
  arb_zero_pm_inf(as_value(x)->ball);
}

/* ===========================
 *  Arithmetic on tra_itype_t
 * =========================== */

static void arb_ilib_add(tra_itype_t c, tra_itype_t a_, tra_itype_t b_, tra_prec_t prec) {
  assert(prec != INF_PREC);
  arb_value_t *a = as_value(a_), *b = as_value(b_);

  // These copy verbatim
  if (value_is_rational(a) && fmpq_is_zero(a->number.rational)) { arb_ilib_set(c, b_); return; }
  if (value_is_rational(b) && fmpq_is_zero(b->number.rational)) { arb_ilib_set(c, a_); return; }

  // |a + b| ≤ |a| + |b| ≤ 2 * max(|a|, |b|) ⇒ at most max_mag + 1 bits
  slong ma = value_get_mag_bits(a);
  slong mb = value_get_mag_bits(b);
  value_binop(as_value(c), a, b, working_prec(prec, FLINT_MAX(ma, mb) + 1), VALUE_ADD);
}

static void arb_ilib_sub(tra_itype_t c, tra_itype_t a_, tra_itype_t b_, tra_prec_t prec) {
  assert(prec != INF_PREC);
  arb_value_t *a = as_value(a_), *b = as_value(b_);

  // |a - b| ≤ |a| + |b| ≤ 2 * max(|a|, |b|) ⇒ at most max_mag + 1 bits
  slong ma = value_get_mag_bits(a);
  slong mb = value_get_mag_bits(b);
  value_binop(as_value(c), a, b, working_prec(prec, FLINT_MAX(ma, mb) + 1), VALUE_SUB);
}

static void arb_ilib_mul(tra_itype_t c, tra_itype_t a_, tra_itype_t b_, tra_prec_t prec) {
  assert(prec != INF_PREC);
  arb_value_t *a = as_value(a_), *b = as_value(b_);

  // A rational 0 absorbs any real, however wide its ball
  if ((value_is_rational(a) && fmpq_is_zero(a->number.rational)) || (value_is_rational(b) && fmpq_is_zero(b->number.rational))) { arb_ilib_set_z(c, 0); return; }

  // |a * b| = |a| * |b| <= 2^ma * 2^mb = 2^(ma+mb): unlike a sum, bit-bounds
  // on a product compose exactly, with no carry -- no extra bit needed.
  slong wp = working_prec(prec, value_get_mag_bits(a) + value_get_mag_bits(b));
  value_binop(as_value(c), a, b, wp, VALUE_MUL);
}

static void arb_ilib_div(tra_itype_t c, tra_itype_t a_, tra_itype_t b_, tra_prec_t prec) {
  assert(prec != INF_PREC);
  arb_value_t *a = as_value(a_), *b = as_value(b_);
  assert(!value_is_rational(b) || !fmpq_is_zero(b->number.rational)); // callers exclude a zero denominator

  // |a / b| needs an upper bound on |a| but a LOWER bound on |b|, for which a rough
  // ball of b is enough
  arb_t tb;
  arb_init(tb);
  slong result_mag = value_get_mag_bits(a) - ball_mag_lower_bits(value_ball(b, ARB_MIN_PREC, tb));
  arb_clear(tb);

  value_binop(as_value(c), a, b, working_prec(prec, FLINT_MAX(result_mag, 0)), VALUE_DIV);

  // When the ball of b contains 0, which a product domain allows (see tra_product_domain.h),
  // arb_div returns the indeterminate ball [nan +/- inf]: we use R = [0 +/- inf] instead
  arb_value_t* r = as_value(c);
  if (!value_is_rational(r) && arf_is_nan(arb_midref(r->ball))) arb_zero_pm_inf(r->ball);
}

static void arb_ilib_abs(tra_itype_t b_, tra_itype_t a_) {
  arb_value_t *b = as_value(b_), *a = as_value(a_);
  if (value_is_rational(a)) {
    fmpq_abs(b->number.rational, a->number.rational);
    value_set_rational(b);
  } else {
    arb_nonnegative_abs(b->ball, a->ball);
    b->exact = false;
  }
}

static void arb_ilib_pow_ui(tra_itype_t c_, tra_itype_t a_, unsigned long n, tra_prec_t prec) {
  assert(prec != INF_PREC);
  arb_value_t *c = as_value(c_), *a = as_value(a_);

  if (value_is_rational(a)) {
    fmpq_pow_si(c->number.rational, a->number.rational, (slong) n);
    value_set_rational(c);
    return;
  }
  arb_symbolic_t power;
  bool exact = a->exact && symbolic_pow_ui(&power, &a->number, n);
  if (exact && symbolic_is_rational(&power)) {
    value_set_number(c, &power);
    return;
  }

  // |a^n| = |a|^n <= (2^ma)^n = 2^(ma*n): exact, no extra bit needed.
  // An ma * n that would overflow falls back to the minimal working precision.
  slong ma = value_get_mag_bits(a);
  slong wp = (n > 0 && (unsigned long) ma > (unsigned long) LONG_MAX / n)
    ? ARB_MIN_PREC : working_prec(prec, ma * (slong) n);

  arb_pow_ui(c->ball, a->ball, n, wp);
  value_set_number(c, exact ? &power : NULL);
}

/* ====================
 *  Lattice operations
 * ==================== */

static void arb_ilib_join(tra_itype_t c, tra_itype_t a_, tra_itype_t b_, tra_prec_t prec) {
  assert(prec != INF_PREC);
  arb_value_t *a = as_value(a_), *b = as_value(b_);

  // The join of a number with itself is that number. An irrational one keeps the hull of both
  // balls, as the queries read the ball.
  const arb_symbolic_t *x = &a->number, *y = &b->number;
  bool same = a->exact && b->exact && fmpq_equal(x->rational, y->rational) && fmpq_equal(x->sqrt_coeff, y->sqrt_coeff)
      && fmpz_equal(x->radicand, y->radicand) && fmpq_equal(x->pi_coeff, y->pi_coeff)
      && fmpq_equal(x->exp_coeff, y->exp_coeff) && fmpq_equal(x->exp_arg, y->exp_arg);
  if (same && value_is_rational(a)) { arb_ilib_set(c, a_); return; }
  arb_symbolic_t number; // saved first, as c may alias a or b
  if (same) {
    symbolic_init(&number);
    symbolic_set(&number, x);
  }

  // |x u y| <= max(|x|, |y|)
  slong ma = value_get_mag_bits(a);
  slong mb = value_get_mag_bits(b);
  value_binop(as_value(c), a, b, working_prec(prec, FLINT_MAX(ma, mb)), VALUE_JOIN);
  if (same) value_set_number(as_value(c), &number);
}

/* ==========================
 *  Transcendental functions
 * ========================== */

static void arb_ilib_const_pi(tra_itype_t x, tra_prec_t prec) {
  assert(prec != INF_PREC);
  arb_const_pi(as_value(x)->ball, working_prec(prec, 2)); // pi < 2^2
  arb_symbolic_t pi;
  symbolic_init(&pi);
  fmpq_one(pi.pi_coeff);
  value_set_number(as_value(x), &pi);
  assert(prec == NO_PREC || arb_ilib_get_precision(x) >= prec);
}

/* x := cos(a) if cosine, else sin(a), at wp. Exact when a is exactly k pi/2 for an integer k:
 * sin(k pi/2) is 0, 1, 0, -1 for k = 0, 1, 2, 3 mod 4, and cos(y) = sin(y + pi/2). x may alias a.
 */
static void value_sin_cos(arb_value_t* x, arb_value_t* a, slong wp, bool cosine) {
  static const slong sin_half_pi[4] = { 0, 1, 0, -1 };

  // a = (p/q) pi, with p/q in lowest terms, is k pi/2 for the integer k = 2p/q iff q <= 2
  const arb_symbolic_t* s = &a->number;
  const fmpz* q = fmpq_denref(s->pi_coeff);
  if (a->exact && fmpq_is_zero(s->rational) && fmpq_is_zero(s->sqrt_coeff) && fmpq_is_zero(s->exp_coeff)
      && fmpz_cmp_ui(q, 2) <= 0) {
    ulong k = fmpz_fdiv_ui(fmpq_numref(s->pi_coeff), 4) * (fmpz_is_one(q) ? 2 : 1) + cosine;
    fmpq_set_si(x->number.rational, sin_half_pi[k % 4], 1);
    value_set_rational(x);
  } else {
    value_strongly_transcendental_unop(x, a, wp, wp, cosine ? arb_cos : arb_sin);
  }
}

static void arb_ilib_sin(tra_itype_t x, tra_itype_t a, tra_prec_t prec) {
  assert(prec != INF_PREC);

  // |sin(a)| ≤ 1 ⇒ output magnitude 0, but ARB needs input magnitude
  // for argument reduction (a mod 2π) when |a| is large.
  slong wp = working_prec(prec, value_get_mag_bits(as_value(a)));
  value_sin_cos(as_value(x), as_value(a), wp, false);
}

static void arb_ilib_cos(tra_itype_t x, tra_itype_t a, tra_prec_t prec) {
  assert(prec != INF_PREC);

  // Same reasoning as arb_ilib_sin: |cos(a)| ≤ 1 ⇒ output magnitude 0, but ARB
  // needs the input magnitude for the argument reduction (a mod 2π).
  slong wp = working_prec(prec, value_get_mag_bits(as_value(a)));
  value_sin_cos(as_value(x), as_value(a), wp, true);
}

static void arb_ilib_exp(tra_itype_t x, tra_itype_t a_, tra_prec_t prec) {
  assert(prec != INF_PREC);
  arb_value_t* a = as_value(a_);
  if (value_is_rational(a) && fmpq_is_zero(a->number.rational)) { arb_ilib_set_z(x, 1); return; }

  // exp is monotone increasing, so log2(exp(a)) <= a_hi * log2(e), where
  // a_hi is a SIGNED upper bound on a (not |a| -- for negative a, exp(a)
  // is tiny, not huge, and using |a| would overestimate the precision needed).
  arb_t ta;
  arf_t a_hi;
  arb_init(ta);
  arf_init(a_hi);
  arb_get_ubound_arf(a_hi, value_ball(a, 64, ta), 64); // 64 bits: only need a magnitude estimate
  double bits_d = ceil(arf_get_d(a_hi, ARF_RND_UP) * 1.4426950409); // > log2(e)
  arf_clear(a_hi);
  arb_clear(ta);

  // Unusable magnitudes (as in value_get_mag_bits) are handled at the minimal precision,
  // plus the mag(a) bits needed to reduce a modulo log(2): with fewer, the ball is [0 +/- inf]
  slong mag_a = value_get_mag_bits(a);
  slong wp = (isfinite(bits_d) && bits_d < ARB_MAX_USABLE_MAG)
    ? working_prec(prec, bits_d > 0 ? (slong) bits_d : 0) : ARB_MIN_PREC + mag_a;

  // exp(q) of a rational a = q ≠ 0, built first as x may alias a
  arb_symbolic_t exp_q;
  bool exact = value_is_rational(a);
  if (exact) {
    symbolic_init(&exp_q);
    fmpq_one(exp_q.exp_coeff);
    fmpq_set(exp_q.exp_arg, a->number.rational);
  }

  // A rational argument is rounded relatively, and exp scales its error by e^a:
  // mag(a) more bits keep that error within 2^-(prec+3)
  value_strongly_transcendental_unop(as_value(x), a, wp + mag_a, wp, arb_exp);
  if (exact) value_set_number(as_value(x), &exp_q);
}

/* ==========
 *  Querying
 * ========== */

static bool arb_ilib_zero_in(tra_itype_t x) {
  arb_value_t* v = as_value(x);
  return value_is_rational(v) ? fmpq_is_zero(v->number.rational) : arb_contains_zero(v->ball);
}

static bool arb_ilib_avoids_mpfr(tra_itype_t x, mpfr_t a) {
  assert(mpfr_number_p(a));
  arb_value_t* v = as_value(x);
  if (!value_is_rational(v)) return !arb_contains_mpfr(v->ball, a);
  return rational_cmp_mpfr(v, a) != 0;
}

/* Exact position of q relative to x, TV_UNKNOWN only for a non-finite x */
static tra_tv_position_t rational_position(const mpq_t q, arb_struct* x) {
  if (!arb_is_finite(x)) return TV_UNKNOWN;

  fmpq_t fq;
  fmpq_init(fq);
  fmpq_set_mpq(fq, q);
  tra_tv_position_t result = TV_INSIDE;
  if (!arb_contains_fmpq(x, fq)) {
    // q is outside [m - r, m + r], so either q > m + r >= m or q < m - r <= m:
    // comparing q with the midpoint m gives the side. With q = num/den and den > 0,
    // q > m iff num > den * m, and den * m is computed exactly (ARF_PREC_EXACT).
    arf_t num, den_mid;
    arf_init(num);
    arf_init(den_mid);
    arf_set_fmpz(num, fmpq_numref(fq));
    arf_mul_fmpz(den_mid, arb_midref(x), fmpq_denref(fq), ARF_PREC_EXACT, ARF_RND_DOWN);
    result = arf_cmp(num, den_mid) > 0 ? TV_ABOVE : TV_BELOW;
    arf_clear(num);
    arf_clear(den_mid);
  }
  fmpq_clear(fq);
  return result;
}

/* Refine an algebraic number's own isolating interval (see algebraic_ball)
 * until its position relative to x is decided, or the precision cap is hit
 * (result stays TV_UNKNOWN). 
 */
static tra_tv_position_t algebraic_position(const lp_algebraic_number_t* a, arb_struct* x) {
  arb_t witness;
  arb_init(witness);

  tra_tv_position_t result = TV_UNKNOWN;
  for (tra_prec_t p = ARB_MIN_PREC; result == TV_UNKNOWN; p *= 2) {
    algebraic_ball(witness, a, p);
    if (arb_gt(witness, x)) result = TV_ABOVE;
    else if (arb_lt(witness, x)) result = TV_BELOW;
    else if (arb_contains(x, witness)) result = TV_INSIDE;
    else if (p > (INF_PREC - 1) / 2) break;
  }

  arb_clear(witness);
  return result;
}

static tra_tv_position_t arb_ilib_trail_value_position(tra_itype_t x_, const mcsat_value_t* v) {
  arb_value_t* x = as_value(x_);
  assert(v->type == VALUE_RATIONAL || v->type == VALUE_LIBPOLY);
  if (!value_is_rational(x) && v->type == VALUE_LIBPOLY && !lp_value_is_rational(&v->lp_value)) {
    return algebraic_position(&v->lp_value.value.a, x->ball);
  }

  // q is the value of x when x is rational, else the rational v
  mpq_t q;
  mpq_init(q);
  tra_tv_position_t result;
  if (value_is_rational(x)) {
    fmpq_get_mpq(q, x->number.rational);
    int cmp = tra_value_cmp_mpq(v, q);
    result = cmp == 0 ? TV_INSIDE : (cmp < 0 ? TV_BELOW : TV_ABOVE);
  } else {
    if (v->type == VALUE_RATIONAL) q_get_mpq((rational_t*) &v->q, q);
    else lp_value_get_rational(&v->lp_value, q); // an lp_rational_t is an mpq
    result = rational_position(q, x->ball);
  }
  mpq_clear(q);
  return result;
}

static bool arb_ilib_is_nonnegative(tra_itype_t x) {
  arb_value_t* v = as_value(x);
  return value_is_rational(v) ? fmpq_sgn(v->number.rational) >= 0 : arb_is_nonnegative(v->ball) != 0;
}

static bool arb_ilib_is_nonpositive(tra_itype_t x) {
  arb_value_t* v = as_value(x);
  return value_is_rational(v) ? fmpq_sgn(v->number.rational) <= 0 : arb_is_nonpositive(v->ball) != 0;
}

static bool arb_ilib_is_bounded(tra_itype_t x) {
  arb_value_t* v = as_value(x);
  return value_is_rational(v) || arb_is_finite(v->ball) != 0;
}

/* ========
 *  Output
 * ======== */

static void arb_ilib_fprintd(FILE *fp, tra_itype_t x, long digits) {
  arb_value_t* v = as_value(x);
  if (!value_is_rational(v)) {
    fprintf(fp, "[");
    arb_fprintd(fp, v->ball, digits);
    if (v->exact) {
      // The rational part, then the nonzero terms
      const arb_symbolic_t* s = &v->number;
      fprintf(fp, " = ");
      fmpq_fprint(fp, s->rational);
      if (!fmpq_is_zero(s->sqrt_coeff)) {
        fprintf(fp, " + ");
        fmpq_fprint(fp, s->sqrt_coeff);
        fprintf(fp, "*sqrt(");
        fmpz_fprint(fp, s->radicand);
        fprintf(fp, ")");
      }
      if (!fmpq_is_zero(s->pi_coeff)) {
        fprintf(fp, " + ");
        fmpq_fprint(fp, s->pi_coeff);
        fprintf(fp, "*pi");
      }
      if (!fmpq_is_zero(s->exp_coeff)) {
        fprintf(fp, " + ");
        fmpq_fprint(fp, s->exp_coeff);
        fprintf(fp, "*exp(");
        fmpq_fprint(fp, s->exp_arg);
        fprintf(fp, ")");
      }
    }
    fprintf(fp, "]");
    return;
  }

  mpfr_t f;
  mpfr_init2(f, 64);
  fmpq_get_mpfr(f, v->number.rational, MPFR_RNDN);
  mpfr_fprintf(fp, "[%.*Rg exact]", (int) digits, f);
  mpfr_clear(f);
}

/* =========================
 *  Global library instance
 * ========================= */

tra_ilib_t tra_arb_ilib = {
  .alloc             = arb_ilib_alloc,
  .free              = arb_ilib_free,
  .set               = arb_ilib_set,
  .set_z             = arb_ilib_set_z,
  .set_mpz           = arb_ilib_set_mpz,
  .set_mpz_div       = arb_ilib_set_mpz_div,
  .set_mpfr          = arb_ilib_set_mpfr,
  .set_algebraic_lp  = arb_ilib_set_algebraic_lp,
  .set_R             = arb_ilib_set_R,
  .const_pi          = arb_ilib_const_pi,
  .get_interval_overapproximation_mpfr = arb_ilib_get_interval_overapproximation_mpfr,
  .contains_interval_mpfr = arb_ilib_contains_interval_mpfr,
  .get_precision     = arb_ilib_get_precision,
  .zero_in           = arb_ilib_zero_in,
  .avoids_mpfr       = arb_ilib_avoids_mpfr,
  .trail_value_position = arb_ilib_trail_value_position,
  .is_nonnegative    = arb_ilib_is_nonnegative,
  .is_nonpositive    = arb_ilib_is_nonpositive,
  .is_bounded        = arb_ilib_is_bounded,
  .add               = arb_ilib_add,
  .sub               = arb_ilib_sub,
  .mul               = arb_ilib_mul,
  .div               = arb_ilib_div,
  .abs               = arb_ilib_abs,
  .pow_ui            = arb_ilib_pow_ui,
  .join              = arb_ilib_join,
  .sin               = arb_ilib_sin,
  .cos               = arb_ilib_cos,
  .exp               = arb_ilib_exp,
  .fprintd           = arb_ilib_fprintd,
};
