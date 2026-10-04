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

#include "mcsat/tra/utils/tra_exact_values.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include <poly/algebraic_number.h>
#include <poly/dyadic_interval.h>
#include <poly/dyadic_rational.h>
#include <poly/rational.h>
#include <poly/value.h>

#include "mcsat/plugin.h"
#include "mcsat/trail.h"
#include "mcsat/value.h"
#include "mcsat/variable_db.h"
#include "terms/term_explorer.h"
#include "terms/terms.h"

#ifndef NDEBUG
// mpfr_get_q makes GMP raise SIGFPE on an exponent outside MPFR's range, which ARB can
// produce and mpfr_number_p accepts. Zero, infinity and NaN carry no exponent.
static bool tra_mpfr_exp_in_range(const mpfr_t f) {
  if (!mpfr_regular_p(f)) return true;
  mpfr_exp_t e = mpfr_get_exp(f);
  return mpfr_get_emin() <= e && e <= mpfr_get_emax();
}
#endif

/*
 * Note on libpoly arithmetic: lp_value_add, lp_value_mul and lp_value_pow
 * compute into a local and swap it into the output, so the output may alias
 * an input. This is relied upon below to update accumulators in place.
 */

/**
 * Requires: *out is unconstructed.
 * Ensures:  *out is constructed, of type LP_VALUE_RATIONAL, and *out = q.
 */
static void lp_value_construct_from_yices_rational(lp_value_t* out, const rational_t* q) {
  lp_rational_t rat;
  lp_rational_construct(&rat);
  q_get_mpq(q, &rat);
  lp_value_construct(out, LP_VALUE_RATIONAL, &rat);
  lp_rational_destruct(&rat);
}

/**
 * Requires: *out is unconstructed.
 * Ensures:  result <==> value is real-valued;
 *           result ==> *out is constructed and *out = value;
 *           !result ==> *out is unconstructed.
 */
static bool lp_value_construct_from_mcsat_value(lp_value_t* out, const mcsat_value_t* value) {
  switch (value->type) {
  case VALUE_RATIONAL:
    lp_value_construct_from_yices_rational(out, &value->q);
    return true;
  case VALUE_LIBPOLY:
    lp_value_construct_copy(out, &value->lp_value);
    return true;
  default:
    return false;
  }
}

int tra_value_sign(const mcsat_value_t* v) {
  int s;
  switch (v->type) {
  case VALUE_RATIONAL:
    s = q_sgn(&v->q);
    break;
  case VALUE_LIBPOLY:
    assert(!lp_value_is_infinity(&v->lp_value));
    s = lp_value_sgn(&v->lp_value); // only its sign is specified, e.g. for algebraic values
    break;
  default:
    assert(false);
    s = 0;
  }
  return s < 0 ? -1 : (s > 0 ? 1 : 0);
}

int tra_value_cmp_mpq(const mcsat_value_t* v, const mpq_t q) {
  int cmp;
  switch (v->type) {
  case VALUE_RATIONAL: {
    mpq_t vq;
    mpq_init(vq);
    q_get_mpq(&v->q, vq);
    cmp = mpq_cmp(vq, q);
    mpq_clear(vq);
    break;
  }
  case VALUE_LIBPOLY:
    // Exact for every libpoly value type, algebraic numbers included.
    cmp = lp_value_cmp_rational(&v->lp_value, q);
    break;
  default:
    assert(false);
    cmp = 0;
  }
  return cmp < 0 ? -1 : (cmp > 0 ? 1 : 0);
}

//WARNING: naive implementation that converts to mpq for now 
// horrible performances when the exponent is large
int tra_value_cmp_mpfr(const mcsat_value_t* v, const mpfr_t f) {
  assert(!mpfr_nan_p(f));

  // An infinite endpoint carries no information beyond its side.
  if (mpfr_inf_p(f)) {
    return mpfr_sgn(f) > 0 ? -1 : 1;
  }
  assert(tra_mpfr_exp_in_range(f));

  mpq_t q;
  mpq_init(q);
  mpfr_get_q(q, f); // exact: f = m * 2^e
  int cmp = tra_value_cmp_mpq(v, q);
  mpq_clear(q);
  return cmp;
}

bool tra_value_close_mpq(const mcsat_value_t* v, const mpq_t q, long delta) {
  // |v - q| <= eps  iff  q - eps <= v <= q + eps, with eps = 2^-delta exact.
  mpq_t lo, hi, eps;
  mpq_inits(lo, hi, eps, NULL);
  mpq_set_ui(eps, 1, 1);
  if (delta >= 0) {
    mpq_div_2exp(eps, eps, (mp_bitcnt_t) delta);
  } else {
    mpq_mul_2exp(eps, eps, (mp_bitcnt_t) -delta);
  }
  mpq_sub(lo, q, eps);
  mpq_add(hi, q, eps);

  bool close = tra_value_cmp_mpq(v, lo) >= 0 && tra_value_cmp_mpq(v, hi) <= 0;

  mpq_clears(lo, hi, eps, NULL);
  return close;
}

//WARNING: naive implementation that converts to mpq for now 
// horrible performances when the exponent is large
bool tra_value_close_mpfr(const mcsat_value_t* v, const mpfr_t f, long delta) {
  if (!mpfr_number_p(f)) {
    return false;
  }
  assert(tra_mpfr_exp_in_range(f));

  mpq_t q;
  mpq_init(q);
  mpfr_get_q(q, f); // exact: f = m * 2^e
  bool close = tra_value_close_mpq(v, q, delta);
  mpq_clear(q);
  return close;
}

void tra_value_bounds_mpq(const mcsat_value_t* v, mpq_t lo, mpq_t hi) {
  if (v->type == VALUE_RATIONAL) {
    q_get_mpq(&v->q, lo);
    mpq_set(hi, lo);
    return;
  }

  assert(v->type == VALUE_LIBPOLY);
  if (lp_value_is_rational(&v->lp_value)) {
    lp_value_get_rational(&v->lp_value, lo);
    mpq_set(hi, lo);
    return;
  }

  assert(v->lp_value.type == LP_VALUE_ALGEBRAIC);
  const lp_dyadic_interval_t* I = &v->lp_value.value.a.I;
  lp_dyadic_rational_get_num(&I->a, mpq_numref(lo));
  lp_dyadic_rational_get_den(&I->a, mpq_denref(lo));
  mpq_canonicalize(lo);
  lp_dyadic_rational_get_num(&I->b, mpq_numref(hi));
  lp_dyadic_rational_get_den(&I->b, mpq_denref(hi));
  mpq_canonicalize(hi);
}

void tra_mpq_round_dyadic(mpq_t out, const mpq_t x, long k, int dir) {
  assert(k >= 0);
  mpq_t y;
  mpz_t z;
  mpq_init(y);
  mpz_init(z);
  mpq_mul_2exp(y, x, k);
  if (dir == 0) {
    mpq_t half;
    mpq_init(half);
    mpq_set_ui(half, 1, 2);
    mpq_add(y, y, half);
    mpq_clear(half);
  }
  if (dir > 0) mpz_cdiv_q(z, mpq_numref(y), mpq_denref(y));
  else mpz_fdiv_q(z, mpq_numref(y), mpq_denref(y));
  mpq_set_z(out, z);
  mpq_div_2exp(out, out, k);
  mpz_clear(z);
  mpq_clear(y);
}

void tra_value_nearest_dyadic(const mcsat_value_t* v, long k, mpq_t c) {
  if (!(v->type == VALUE_RATIONAL || lp_value_is_rational(&v->lp_value))) {
    const lp_algebraic_number_t* a = &v->lp_value.value.a;
    while (a->f != NULL && lp_dyadic_interval_size(&a->I) > -(int) (k + 1)) {
      lp_algebraic_number_refine_const(a);
    }
  }
  mpq_t lo, hi;
  mpq_inits(lo, hi, NULL);
  tra_value_bounds_mpq(v, lo, hi);
  tra_mpq_round_dyadic(c, lo, k, 0);
  mpq_clears(lo, hi, NULL);
}

long tra_value_separation(const mcsat_value_t* v, const mpq_t H, bool above, long max_bits) {
  mpq_t lo, hi, gap, width, min_width;
  mpq_inits(lo, hi, gap, width, min_width, NULL);
  mpq_set_ui(min_width, 1, 1);
  mpq_div_2exp(min_width, min_width, max_bits + 1);

  long p = -1;
  while (true) {
    tra_value_bounds_mpq(v, lo, hi);
    if (above ? mpq_cmp(lo, H) > 0 : mpq_cmp(hi, H) < 0) {
      // gap = N/D with 2^(bits(N)-1) <= N and D < 2^bits(D), so 2^-p < gap
      mpq_sub(gap, above ? lo : hi, H);
      mpq_abs(gap, gap);
      p = (long) mpz_sizeinbase(mpq_denref(gap), 2) - (long) mpz_sizeinbase(mpq_numref(gap), 2) + 1;
      if (p < 0) p = 0;
      break;
    }
    if (v->type == VALUE_RATIONAL || lp_value_is_rational(&v->lp_value)) break;
    mpq_sub(width, hi, lo);
    if (mpq_cmp(width, min_width) <= 0) break;
    lp_algebraic_number_refine_const(&v->lp_value.value.a);
  }

  mpq_clears(lo, hi, gap, width, min_width, NULL);
  return p;
}

/* Recursive worker for term_evaluate */
static
bool term_evaluate_rec(const plugin_context_t* ctx, term_t t, lp_value_t* out) {
  term_table_t* terms = ctx->terms;

  if (term_kind(terms, t) == ARITH_CONSTANT) {
    lp_value_construct_from_yices_rational(out, rational_term_desc(terms, t));
    return true;
  }

  bool is_sum = term_is_sum(terms, t);
  if (!is_sum && !term_is_product(terms, t)) {
    // Leaf: anything else is read from the trail. Polynomials are never read from
    // the trail even when they are mcsat variables: the NA plugin reconciles a
    // polynomial variable with its leaves only when it processes the evaluation
    // constraint, so its trail value can be stale when this runs.
    variable_t v = variable_db_get_variable_if_exists(ctx->var_db, t);
    if (v == variable_null || !trail_has_value(ctx->trail, v)) {
      return false;
    }
    return lp_value_construct_from_mcsat_value(out, trail_get_value(ctx->trail, v));
  }

  // Sum: out = sum_i c_i * child_i (child NULL_TERM marks the constant summand).
  // Product: out = prod_i child_i ^ exp_i.
  uint32_t n = term_num_children(terms, t);
  mpq_t coeff;
  bool ok = true;

  mpq_init(coeff);
  lp_value_construct_int(out, is_sum ? 0 : 1);

  for (uint32_t i = 0; ok && i < n; i++) {
    term_t child;
    lp_value_t factor;

    if (is_sum) {
      sum_term_component(terms, t, i, coeff, &child);
      lp_value_construct(&factor, LP_VALUE_RATIONAL, coeff);
      if (child != NULL_TERM) {
        lp_value_t child_val;
        ok = term_evaluate_rec(ctx, child, &child_val);
        if (ok) {
          lp_value_mul(&factor, &factor, &child_val);
          lp_value_destruct(&child_val);
        }
      }
      if (ok) {
        lp_value_add(out, out, &factor);
      }
      lp_value_destruct(&factor);
    } else {
      uint32_t exp;
      product_term_component(terms, t, i, &child, &exp);
      ok = term_evaluate_rec(ctx, child, &factor);
      if (ok) {
        lp_value_pow(&factor, &factor, exp);
        lp_value_mul(out, out, &factor);
        lp_value_destruct(&factor);
      }
    }
  }

  mpq_clear(coeff);
  if (!ok) {
    lp_value_destruct(out);
  }
  return ok;
}

bool term_evaluate(const plugin_context_t* ctx, term_t t, mcsat_value_t* value) {
  lp_value_t v;
  if (!term_evaluate_rec(ctx, t, &v)) {
    return false;
  }
  // Move v into *value rather than deep-copying it (algebraic numbers carry a polynomial).
  value->type = VALUE_LIBPOLY;
  lp_value_construct_none(&value->lp_value);
  lp_value_swap(&value->lp_value, &v);
  lp_value_destruct(&v);
  return true;
}

int tra_value_cmp_terms(const plugin_context_t* ctx, term_t t1, term_t t2) {
  assert(is_arithmetic_term(ctx->terms, t1) && is_arithmetic_term(ctx->terms, t2));
  mcsat_value_t v1, v2;
  bool ok1 = term_evaluate(ctx, t1, &v1);
  bool ok2 = term_evaluate(ctx, t2, &v2);
  int cmp = (ok1 && ok2) ? lp_value_cmp(&v1.lp_value, &v2.lp_value) : 0;
  if (ok1) mcsat_value_destruct(&v1);
  if (ok2) mcsat_value_destruct(&v2);
  return (cmp > 0) - (cmp < 0);
}
