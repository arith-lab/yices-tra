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

#include "mcsat/na/poly-backends/tra_flint_poly.h"

#include <gmp.h>
#include <flint/fmpz.h>
#include <flint/fmpz_mpoly.h>
#include <flint/fmpz_mpoly_factor.h>
#include <flint/fmpq.h>
#include <flint/fmpq_poly.h>
#include <flint/fmpq_vec.h>
#include <flint/fmpz_poly.h>
#include <flint/fmpz_poly_factor.h>
#include <flint/acb.h>
#include <flint/arb_fmpz_poly.h>
#include <poly/algebraic_number.h>
#include <poly/assignment.h>
#include <poly/dyadic_interval.h>
#include <poly/dyadic_rational.h>
#include <poly/upolynomial.h>
#include <poly/value.h>
#include <poly/monomial.h>
#include <poly/polynomial.h>
#include <poly/polynomial_context.h>
#include <poly/variable_list.h>

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>

#include "utils/memalloc.h"

/** Above this degree in the top variable, libpoly isolates the roots: Arb isolates every complex
 *  root, and libpoly only the real ones (degree 512: 29 s against 0.1 s) */
#define TRA_FLINT_ROOTS_MAX_DEGREE 64

/** The target precision, in bits, of the root enclosures computed by Arb */
#define TRA_FLINT_ROOTS_PREC 64

/** Data of to_flint */
typedef struct {
  const lp_variable_list_t* vars;     // FLINT variable i is vars->list[i]
  fmpz_mpoly_struct* P;               // a polynomial of ctx
  const fmpz_mpoly_ctx_struct* ctx;
  ulong* exp;                         // scratch, with vars->list_size elements
  fmpz* c;                            // scratch, initialised
} to_flint_data_t;

/**
 * A callback of lp_polynomial_traverse. Let d be the to_flint_data_t data.
 * Requires: the variables of the monomial m are in d->vars.
 * Modifies: d->P, by appending m, FLINT variable i standing for d->vars->list[i]; the scratch
 *           d->exp and d->c.
 */
static
void to_flint(const lp_polynomial_context_t* lp_ctx, lp_monomial_t* m, void* data) {
  to_flint_data_t* d = (to_flint_data_t*) data;
  size_t j;
  for (j = 0; j < d->vars->list_size; ++ j) {
    d->exp[j] = 0;
  }
  for (j = 0; j < m->n; ++ j) {
    int i = lp_variable_list_index(d->vars, m->p[j].x);
    assert(i >= 0);
    d->exp[i] = m->p[j].d;
  }
  fmpz_set_mpz(d->c, &m->a);
  fmpz_mpoly_push_term_fmpz_ui(d->P, d->c, d->exp, d->ctx);
}

/**
 * Requires: the context of A is over the integers; the variables of A are in vars; ctx has
 *           vars->list_size variables; P is initialised in ctx.
 * Modifies: P.
 * Ensures:  P = A, FLINT variable i standing for vars->list[i].
 */
static
void flint_from_lp(fmpz_mpoly_t P, const lp_polynomial_t* A, const lp_variable_list_t* vars, const fmpz_mpoly_ctx_t ctx) {
  assert(lp_polynomial_get_context(A)->K == lp_Z);
  assert(fmpz_mpoly_ctx_nvars(ctx) == (slong) vars->list_size);
  fmpz_t c;
  fmpz_init(c);
  ulong* exp = (ulong*) safe_malloc(vars->list_size * sizeof(ulong));
  to_flint_data_t d = { vars, P, ctx, exp, c };
  fmpz_mpoly_zero(P, ctx);
  lp_polynomial_traverse(A, to_flint, &d);
  fmpz_mpoly_sort_terms(P, ctx);
  fmpz_mpoly_combine_like_terms(P, ctx);
  safe_free(exp);
  fmpz_clear(c);
}

/**
 * Requires: ctx has vars->list_size variables; lp_ctx is over the integers.
 * Ensures:  result is a new polynomial of lp_ctx equal to P, FLINT variable i standing for
 *           vars->list[i].
 */
static
lp_polynomial_t* lp_from_flint(const fmpz_mpoly_t P, const lp_variable_list_t* vars, const fmpz_mpoly_ctx_t ctx,
    const lp_polynomial_context_t* lp_ctx) {
  assert(fmpz_mpoly_ctx_nvars(ctx) == (slong) vars->list_size);
  assert(lp_ctx->K == lp_Z);
  lp_polynomial_t* A = lp_polynomial_new(lp_ctx);
  ulong* exp = (ulong*) safe_malloc(vars->list_size * sizeof(ulong));
  lp_monomial_t m;
  lp_monomial_construct(lp_ctx, &m);
  slong j;
  size_t k;
  for (j = 0; j < fmpz_mpoly_length(P, ctx); ++ j) {
    // Over the integers, the coefficient of m is the integer m.a (lp_monomial_set_coefficient)
    lp_monomial_clear(lp_ctx, &m);
    fmpz_get_mpz(&m.a, P->coeffs + j);
    fmpz_mpoly_get_term_exp_ui(exp, P, j, ctx);
    for (k = 0; k < vars->list_size; ++ k) {
      if (exp[k] > 0) {
        lp_monomial_push(&m, vars->list[k], exp[k]);
      }
    }
    lp_polynomial_add_monomial(A, &m);
  }
  lp_monomial_destruct(&m);
  safe_free(exp);
  return A;
}

/**
 * The factor_square_free operation of na_poly_backend_t.
 * Requires, Ensures: as factor_square_free in na_poly_backend_t; moreover,
 * Ensures:  the non-constant factors are pairwise coprime.
 */
static
void tra_flint_factor_square_free(const lp_polynomial_t* A, lp_polynomial_t*** factors,
    size_t** multiplicities, size_t* size) {
  assert(!lp_polynomial_is_constant(A));
  assert(lp_polynomial_get_context(A)->K == lp_Z);
  lp_variable_list_t vars;
  lp_variable_list_construct(&vars);
  lp_polynomial_get_variables(A, &vars);
  fmpz_mpoly_ctx_t ctx;
  fmpz_mpoly_ctx_init(ctx, vars.list_size, ORD_LEX);
  fmpz_mpoly_t P;
  fmpz_mpoly_init(P, ctx);
  flint_from_lp(P, A, &vars, ctx);

  fmpz_mpoly_factor_t f;
  fmpz_mpoly_factor_init(f, ctx);
  if (!fmpz_mpoly_factor_squarefree(f, P, ctx)) {
    lp_polynomial_factor_square_free(A, factors, multiplicities, size);
  } else {
    *size = f->num;
    *factors = (lp_polynomial_t**) safe_malloc(f->num * sizeof(lp_polynomial_t*));
    *multiplicities = (size_t*) safe_malloc(f->num * sizeof(size_t));
    slong i;
    for (i = 0; i < f->num; ++ i) {
      (*factors)[i] = lp_from_flint(f->poly + i, &vars, ctx, lp_polynomial_get_context(A));
      (*multiplicities)[i] = fmpz_get_ui(f->exp + i);
      // FLINT makes the leading coefficient positive in its own order of the variables; libpoly's
      // order may differ
      if (lp_polynomial_lc_sgn((*factors)[i]) < 0) {
        lp_polynomial_neg((*factors)[i], (*factors)[i]);
      }
    }
  }

  fmpz_mpoly_factor_clear(f, ctx);
  fmpz_mpoly_clear(P, ctx);
  fmpz_mpoly_ctx_clear(ctx);
  lp_variable_list_destruct(&vars);
}

/**
 * The resultant operation of na_poly_backend_t.
 * Requires, Modifies, Ensures: as resultant in na_poly_backend_t.
 */
static
void tra_flint_resultant(lp_polynomial_t* R, const lp_polynomial_t* A, const lp_polynomial_t* B) {
  assert(lp_polynomial_get_context(A)->K == lp_Z);
  assert(lp_polynomial_context_equal(lp_polynomial_get_context(A), lp_polynomial_get_context(B)));
  lp_variable_t y = lp_polynomial_top_variable(A);
  assert(lp_polynomial_top_variable(B) == y);
  lp_variable_list_t vars;
  lp_variable_list_construct(&vars);
  lp_polynomial_get_variables(A, &vars);
  lp_polynomial_get_variables(B, &vars);
  fmpz_mpoly_ctx_t ctx;
  fmpz_mpoly_ctx_init(ctx, vars.list_size, ORD_LEX);
  fmpz_mpoly_t P, Q, S;
  fmpz_mpoly_init(P, ctx);
  fmpz_mpoly_init(Q, ctx);
  fmpz_mpoly_init(S, ctx);
  flint_from_lp(P, A, &vars, ctx);
  flint_from_lp(Q, B, &vars, ctx);
  if (fmpz_mpoly_resultant(S, P, Q, lp_variable_list_index(&vars, y), ctx)) {
    lp_polynomial_t* S_lp = lp_from_flint(S, &vars, ctx, lp_polynomial_get_context(A));
    lp_polynomial_swap(R, S_lp);
    lp_polynomial_delete(S_lp);
  } else {
    lp_polynomial_resultant(R, A, B);
  }
  fmpz_mpoly_clear(P, ctx);
  fmpz_mpoly_clear(Q, ctx);
  fmpz_mpoly_clear(S, ctx);
  fmpz_mpoly_ctx_clear(ctx);
  lp_variable_list_destruct(&vars);
}

/** Data of evaluate_monomial */
typedef struct {
  lp_variable_t y;
  const lp_assignment_t* M;
  fmpq* coefficients;
  size_t deg;           // coefficients has deg + 1 elements
  bool rational;        // false once a variable other than y has an irrational value in M
} evaluate_data_t;

/**
 * A callback of lp_polynomial_traverse. Let d be the evaluate_data_t data, k the degree of d->y in
 * the monomial m, and m_M the value of m / y^k when every variable other than d->y takes its value
 * in d->M.
 * Requires: every variable of m other than d->y has a value in d->M; k <= d->deg.
 * Modifies: if d->rational: d->rational := false if one of these values is irrational, and
 *           d->coefficients[k] := d->coefficients[k] + m_M otherwise.
 */
static
void evaluate_monomial(const lp_polynomial_context_t* lp_ctx, lp_monomial_t* m, void* data) {
  evaluate_data_t* d = (evaluate_data_t*) data;
  if (!d->rational) {
    return;
  }
  size_t j, k = 0;
  fmpq_t value, power;
  fmpq_init(value);
  fmpq_init(power);
  fmpz_set_mpz(fmpq_numref(value), &m->a);
  for (j = 0; j < m->n; ++ j) {
    if (m->p[j].x == d->y) {
      k = m->p[j].d;
      continue;
    }
    const lp_value_t* v = lp_assignment_get_value(d->M, m->p[j].x);
    assert(v->type != LP_VALUE_NONE);
    d->rational = lp_value_is_rational(v);
    if (!d->rational) {
      break;
    }
    lp_rational_t q;
    mpq_init(&q);
    lp_value_get_rational(v, &q);
    fmpq_set_mpq(power, &q);
    mpq_clear(&q);
    fmpq_pow_si(power, power, m->p[j].d);
    fmpq_mul(value, value, power);
  }
  assert(k <= d->deg);
  if (d->rational) {
    fmpq_add(d->coefficients + k, d->coefficients + k, value);
  }
  fmpq_clear(value);
  fmpq_clear(power);
}

/**
 * Requires: x is finite, and x = 0 or x = m * 2^e with m an odd integer and |e| <= LONG_MAX;
 *           q is not constructed.
 * Ensures:  q is constructed, and q = x.
 */
static
void dyadic_from_arf(lp_dyadic_rational_t* q, const arf_t x) {
  assert(arf_is_finite(x));
  fmpz_t man, exp;
  fmpz_init(man);
  fmpz_init(exp);
  arf_get_fmpz_2exp(man, exp, x);
  assert(fmpz_bits(exp) < FLINT_BITS);
  mpz_t z;
  mpz_init(z);
  fmpz_get_mpz(z, man);
  slong e = fmpz_get_si(exp);
  lp_dyadic_rational_construct_from_integer(q, z);
  if (e >= 0) {
    lp_dyadic_rational_mul_2exp(q, q, (unsigned long) e);
  } else {
    lp_dyadic_rational_div_2exp(q, q, (unsigned long) -e);
  }
  mpz_clear(z);
  fmpz_clear(man);
  fmpz_clear(exp);
}

/**
 * The roots_isolate operation of na_poly_backend_t.
 * Requires, Ensures: as roots_isolate in na_poly_backend_t.
 */
static
void tra_flint_roots_isolate(const lp_polynomial_t* A, const lp_assignment_t* M, lp_value_t* roots,
    size_t* roots_size) {
  // When every variable other than the top one has a rational value, A_M has rational coefficients.
  // If moreover A has degree at most TRA_FLINT_ROOTS_MAX_DEGREE in the top variable, FLINT factors
  // A_M into irreducible factors; a linear factor gives a rational root; Arb isolates the real roots
  // of the other factors, which become algebraic numbers with that factor as defining polynomial.
  // Otherwise libpoly computes the roots.
  assert(lp_polynomial_get_context(A)->K == lp_Z);
  lp_variable_t y = lp_polynomial_top_variable(A);
  size_t deg = lp_polynomial_degree(A);

  if (deg > TRA_FLINT_ROOTS_MAX_DEGREE) {
    lp_polynomial_roots_isolate(A, M, roots, roots_size);
    return;
  }

  // A_M, with integer coefficients
  evaluate_data_t d = { y, M, _fmpq_vec_init(deg + 1), deg, true };
  lp_polynomial_traverse(A, evaluate_monomial, &d);
  if (!d.rational) {
    _fmpq_vec_clear(d.coefficients, deg + 1);
    lp_polynomial_roots_isolate(A, M, roots, roots_size);
    return;
  }
  fmpq_poly_t u_q;
  fmpq_poly_init(u_q);
  size_t i;
  for (i = 0; i <= deg; ++ i) {
    fmpq_poly_set_coeff_fmpq(u_q, i, d.coefficients + i);
  }
  fmpz_poly_t u;
  fmpz_poly_init(u);
  fmpq_poly_get_numerator(u, u_q);
  _fmpq_vec_clear(d.coefficients, deg + 1);
  fmpq_poly_clear(u_q);
  assert(!fmpz_poly_is_zero(u));

  // The distinct real roots of the irreducible factors of A_M
  *roots_size = 0;
  fmpz_poly_factor_t fac;
  fmpz_poly_factor_init(fac);
  fmpz_poly_factor(fac, u);
  slong f, j;
  bool isolated = true;
  for (f = 0; f < fac->num && isolated; ++ f) {
    const fmpz_poly_struct* g = fac->p + f;
    slong g_deg = fmpz_poly_degree(g);
    if (g_deg == 1) {
      fmpq_t r;
      fmpq_init(r);
      fmpq_set_fmpz_frac(r, fmpz_poly_get_coeff_ptr(g, 0), fmpz_poly_get_coeff_ptr(g, 1));
      fmpq_neg(r, r);
      lp_rational_t q;
      mpq_init(&q);
      fmpq_get_mpq(&q, r);
      lp_value_construct(roots + (*roots_size) ++, LP_VALUE_RATIONAL, &q);
      mpq_clear(&q);
      fmpq_clear(r);
      continue;
    }
    acb_ptr g_roots = _acb_vec_init(g_deg);
    arb_fmpz_poly_complex_roots(g_roots, g, 0, TRA_FLINT_ROOTS_PREC);
    lp_integer_t* g_coeffs = (lp_integer_t*) safe_malloc((g_deg + 1) * sizeof(lp_integer_t));
    for (j = 0; j <= g_deg; ++ j) {
      mpz_init(g_coeffs + j);
      fmpz_get_mpz(g_coeffs + j, fmpz_poly_get_coeff_ptr(g, j));
    }
    lp_upolynomial_t* g_lp = lp_upolynomial_construct(lp_Z, g_deg, g_coeffs);
    // The real roots come first, with an imaginary part exactly 0
    for (j = 0; j < g_deg && isolated && arb_is_zero(acb_imagref(g_roots + j)); ++ j) {
      arf_t lo, hi;
      arf_init(lo);
      arf_init(hi);
      // Exact bounds: the enclosures are disjoint, but bounds rounded outward may not be
      arb_get_lbound_arf(lo, acb_realref(g_roots + j), ARF_PREC_EXACT);
      arb_get_ubound_arf(hi, acb_realref(g_roots + j), ARF_PREC_EXACT);
      lp_dyadic_rational_t a, b;
      dyadic_from_arf(&a, lo);
      dyadic_from_arf(&b, hi);
      // g is irreducible of degree at least 2, so a and b are not roots of g, and the enclosure
      // holds no other root: (a, b) isolates this root, and g changes sign on it. The test of the
      // signs only guards against a failure of this argument (then libpoly computes the roots).
      int sgn_a = lp_upolynomial_sgn_at_dyadic_rational(g_lp, &a);
      int sgn_b = lp_upolynomial_sgn_at_dyadic_rational(g_lp, &b);
      isolated = sgn_a * sgn_b < 0;
      if (isolated) {
        lp_dyadic_interval_t I;
        lp_dyadic_interval_construct(&I, &a, 1, &b, 1);
        lp_algebraic_number_t alpha;
        lp_algebraic_number_construct(&alpha, lp_upolynomial_construct_copy(g_lp), &I);
        lp_value_construct(roots + (*roots_size) ++, LP_VALUE_ALGEBRAIC, &alpha);
        lp_algebraic_number_destruct(&alpha);
        lp_dyadic_interval_destruct(&I);
      }
      lp_dyadic_rational_destruct(&a);
      lp_dyadic_rational_destruct(&b);
      arf_clear(lo);
      arf_clear(hi);
    }
    lp_upolynomial_delete(g_lp);
    for (j = 0; j <= g_deg; ++ j) {
      mpz_clear(g_coeffs + j);
    }
    safe_free(g_coeffs);
    _acb_vec_clear(g_roots, g_deg);
  }
  fmpz_poly_factor_clear(fac);
  fmpz_poly_clear(u);
  assert(isolated);
  if (!isolated) {
    for (i = 0; i < *roots_size; ++ i) {
      lp_value_destruct(roots + i);
    }
    lp_polynomial_roots_isolate(A, M, roots, roots_size);
    return;
  }
  assert(*roots_size <= deg);

  qsort(roots, *roots_size, sizeof(lp_value_t), lp_value_cmp_void);
}

static void tra_flint_destruct(na_poly_backend_t* self) {
  safe_free(self);
}

na_poly_backend_t* tra_flint_backend_allocator(void) {
  na_poly_backend_t* backend = (na_poly_backend_t*) safe_malloc(sizeof(na_poly_backend_t));
  backend->factor_square_free = tra_flint_factor_square_free;
  backend->roots_isolate = tra_flint_roots_isolate;
  backend->resultant = tra_flint_resultant;
  backend->destruct = tra_flint_destruct;
  return backend;
}
