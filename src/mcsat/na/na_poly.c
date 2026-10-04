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

#include "mcsat/na/na_poly.h"
#include "mcsat/na/poly-backends/tra_flint_poly.h"
#include "mcsat/na/poly-backends/tra_libpoly_poly.h"

#include <poly/monomial.h>
#include <poly/polynomial.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "utils/memalloc.h"

/* =====================
    Polynomial backends
   ===================== */

na_poly_backend_t* na_poly_backend_allocator(void) {
  const char* libpoly = getenv("YICES_LIBPOLY");
  if (libpoly != NULL && (strcmp(libpoly, "1") == 0 || strcmp(libpoly, "true") == 0)) {
    return tra_libpoly_backend_allocator();
  }
  return tra_flint_backend_allocator();
}

/* =======================
    Polynomial operations
   ======================= */

/** Data of coefficients_traverse */
typedef struct {
  lp_variable_t y;
  size_t k;
  lp_polynomial_t** coefficients;
} coefficients_data_t;

/**
 * A callback of lp_polynomial_traverse. Let d be the coefficients_data_t data, and j the degree
 * of d->y in the monomial m.
 * Requires: d->coefficients is NULL, or it has d->k + 1 elements and j <= d->k.
 * Modifies: d->k := max(d->k, j) if d->coefficients is NULL; otherwise
 *           d->coefficients[j] := d->coefficients[j] + m / y^j.
 */
static
void coefficients_traverse(const lp_polynomial_context_t* ctx, lp_monomial_t* m, void* data) {
  coefficients_data_t* d = (coefficients_data_t*) data;
  lp_monomial_t rest;
  lp_monomial_construct(ctx, &rest);
  lp_monomial_set_coefficient(ctx, &rest, &m->a);
  size_t i, j = 0;
  for (i = 0; i < m->n; ++ i) {
    if (m->p[i].x == d->y) {
      j = m->p[i].d;
    } else {
      lp_monomial_push(&rest, m->p[i].x, m->p[i].d);
    }
  }
  if (d->coefficients == NULL) {
    d->k = j > d->k ? j : d->k;
  } else {
    assert(j <= d->k);
    lp_polynomial_add_monomial(d->coefficients[j], &rest);
  }
  lp_monomial_destruct(&rest);
}

/**
 * Ensures: *k = deg_y(p); result is a new array of k + 1 new polynomials p_0, ..., p_k of the
 *          context of p, in which y does not occur, and p = sum_j p_j * y^j.
 */
static
lp_polynomial_t** coefficients_new(const lp_polynomial_t* p, lp_variable_t y, size_t* k) {
  coefficients_data_t d = { y, na_poly_degree(p, y), NULL };
  d.coefficients = safe_malloc((d.k + 1) * sizeof(lp_polynomial_t*));
  size_t j;
  for (j = 0; j <= d.k; ++ j) {
    d.coefficients[j] = lp_polynomial_new(lp_polynomial_get_context(p));
  }
  lp_polynomial_traverse(p, coefficients_traverse, &d);
  *k = d.k;
  return d.coefficients;
}

/**
 * Requires: coefficients and k come from coefficients_new.
 * Ensures:  their memory is released.
 */
static
void coefficients_delete(lp_polynomial_t** coefficients, size_t k) {
  size_t j;
  for (j = 0; j <= k; ++ j) {
    lp_polynomial_delete(coefficients[j]);
  }
  safe_free(coefficients);
}

size_t na_poly_degree(const lp_polynomial_t* p, lp_variable_t y) {
  coefficients_data_t d = { y, 0, NULL };
  lp_polynomial_traverse(p, coefficients_traverse, &d);
  return d.k;
}

/**
 * A callback of lp_polynomial_traverse. Let d be the size_t data.
 * Modifies: d := max(d, total degree of m).
 */
static
void total_degree_traverse(const lp_polynomial_context_t* ctx, lp_monomial_t* m, void* data) {
  size_t* d = (size_t*) data;
  size_t i, k = 0;
  for (i = 0; i < m->n; ++ i) {
    k += m->p[i].d;
  }
  if (k > *d) {
    *d = k;
  }
}

size_t na_poly_total_degree(const lp_polynomial_t* p) {
  size_t d = 0;
  lp_polynomial_traverse(p, total_degree_traverse, &d);
  return d;
}

void na_poly_substitute(lp_polynomial_t* p, lp_variable_t y, const lp_polynomial_t* e) {
  size_t k, e_k;
  lp_polynomial_t** p_j = coefficients_new(p, y, &k);
  lp_polynomial_t** e_j = coefficients_new(e, y, &e_k);
  assert(e_k == 1 && lp_polynomial_is_constant(e_j[1]) && !lp_polynomial_is_zero(e_j[1]));

  // Horner: after step j, result = sum_{i >= j} p_i (-e0)^(i-j) c^(k-i), and c_power = c^(k-j+1)
  lp_polynomial_t* c_power = lp_polynomial_new_copy(e_j[1]);
  lp_polynomial_neg(e_j[0], e_j[0]);
  lp_polynomial_t* result = lp_polynomial_new_copy(p_j[k]);
  size_t j;
  for (j = k; j-- > 0; ) {
    lp_polynomial_mul(result, result, e_j[0]);
    lp_polynomial_mul(p_j[j], p_j[j], c_power);
    lp_polynomial_add(result, result, p_j[j]);
    lp_polynomial_mul(c_power, c_power, e_j[1]);
  }
  // A fresh result: libpoly keeps the cached hash of a polynomial modified in place
  lp_polynomial_swap(p, result);

  lp_polynomial_delete(result);
  lp_polynomial_delete(c_power);
  coefficients_delete(p_j, k);
  coefficients_delete(e_j, e_k);
}
