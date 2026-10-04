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

#ifndef NA_POLY_H_
#define NA_POLY_H_

#include <stddef.h>

#include <poly/poly.h>
#include <poly/assignment.h>
#include <poly/value.h>

/*
 * Polynomial operations of NA: the interface of the backends of the projection (implemented in
 * poly-backends/), and operations on libpoly polynomials that libpoly does not provide.
 */

/* =====================
    Polynomial backends
   ===================== */

/**
 * The polynomial operations of the projection (na_plugin_explain.c). Each implementation in
 * poly-backends/ has an allocator, which returns a new backend.
 */
typedef struct na_poly_backend_s {
  /**
   * Square-free factorisation.
   * Requires: A is not constant; the context of A is over the integers.
   * Ensures:  *factors and *multiplicities are new arrays of *size elements, which the caller
   *           releases with free; the factors are new polynomials of the context of A, which the
   *           caller releases with lp_polynomial_delete;
   *           A = c * prod_i factors[i]^multiplicities[i] for a nonzero constant c;
   *           every non-constant factor is square-free and has a positive leading coefficient
   *           (lp_polynomial_lc_sgn > 0; the description of cells relies on it).
   */
  void (*factor_square_free)(const lp_polynomial_t* A, lp_polynomial_t*** factors, size_t** multiplicities, size_t* size);

  /**
   * Real root isolation.
   * Let y be the top variable of A, and A_M the univariate polynomial in y obtained from A by
   * replacing every other variable by its value in M.
   * Requires: the context of A is over the integers; M assigns every variable of A other than y;
   *           A_M is not zero; roots has room for deg_y(A) values, which are not constructed.
   * Ensures:  roots[0 .. *roots_size) are constructed, and the caller destructs them; they are the
   *           distinct real roots of A_M, in increasing order.
   */
  void (*roots_isolate)(const lp_polynomial_t* A, const lp_assignment_t* M, lp_value_t* roots, size_t* roots_size);

  /**
   * Resultant, or NULL.
   * The projection needs the principal subresultant coefficients (psc) of two polynomials, from
   * the first one (the resultant) up to the first one that does not vanish at the assignment.
   * - If resultant is NULL, the projection computes all psc with libpoly.
   * - Otherwise, it computes the resultant first, and the other psc only if the resultant vanishes.
   * libpoly uses NULL: its resultant costs as much as all its psc.
   * 
   * Requires: R, A and B are polynomials of one context, which is over the integers; A and B have
   *           the same top variable y.
   * Modifies: R.
   * Ensures:  R is the resultant of A and B in y, up to a nonzero constant factor.
   */
  void (*resultant)(lp_polynomial_t* R, const lp_polynomial_t* A, const lp_polynomial_t* B);

  /**
   * Ensures:  the memory of self is released.
   */
  void (*destruct)(struct na_poly_backend_s* self);
} na_poly_backend_t;

/**
 * Ensures:  result is a new backend: libpoly's (tra_libpoly_backend_allocator) if the environment
 *           variable YICES_LIBPOLY is "1" or "true", and FLINT's (tra_flint_backend_allocator)
 *           otherwise.
 */
na_poly_backend_t* na_poly_backend_allocator(void);

/* =======================
    Polynomial operations
   ======================= */

/**
 * Let deg_y(p) be the degree of p in the variable y (0 if y does not occur in p).
 * Ensures: result = deg_y(p).
 */
size_t na_poly_degree(const lp_polynomial_t* p, lp_variable_t y);

/**
 * Ensures: result is the total degree of p (0 if p is constant).
 */
size_t na_poly_total_degree(const lp_polynomial_t* p);

/**
 * Let p0 be p before the call, k = deg_y(p0), and e = c*y + e0 where y occurs neither in c nor
 * in e0.
 * Requires: deg_y(e) = 1, and c is a nonzero constant.
 * Modifies: p.
 * Ensures:  p = c^k * p0[y := -e0/c]; hence y does not occur in p, and p = c^k * p0 at every
 *           point where e = 0.
 */
void na_poly_substitute(lp_polynomial_t* p, lp_variable_t y, const lp_polynomial_t* e);

#endif /* NA_POLY_H_ */
