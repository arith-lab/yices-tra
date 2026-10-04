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
 * tra_abstract_values.h
 *
 * Interface (tra_ilib_t) of the abstract domains with which the TRA plugin and the
 * function plugins over-approximate real numbers. A domain is an instance of tra_ilib_t,
 * such as tra_arb_ilib (abstractions/tra_arb_domain.c). Its elements (tra_itype_t) are
 * opaque, and are only accessed through the function pointers of the instance:
 *
 *   tra_ilib_t* lib = &tra_arb_ilib;
 *   tra_itype_t x = lib->alloc();
 *   lib->const_pi(x, 256);
 *   // ... computations ...
 *   lib->free(x);
 *
 * Notation for the contracts below.
 *  - Every element x denotes a set γ(x) ⊆ R. A domain needs no element for the empty set.
 *  - A precision is NO_PREC (-1), INF_PREC (LONG_MAX), or an integer in [1, LONG_MAX-1].
 *    p(x) stands for get_precision(x). The precision arguments of the tra_ilib_t operations
 *    are never INF_PREC. As NO_PREC is the least precision, p(x) >= NO_PREC always holds.
 *  - For S ⊆ R, w(S) = sup S - inf S is the width of S (w(∅) = 0), and v < S means that
 *    v < s for every s ∈ S (similarly for v > S). Operations on reals are lifted to sets
 *    pointwise, e.g. S + T = { s + t : s ∈ S, t ∈ T } and exp(S) = { exp(s) : s ∈ S }.
 *  - For mpfr values lo and hi, possibly infinite, [lo, hi] = { t ∈ R : lo <= t <= hi }.
 *  - A real-valued mcsat value (VALUE_RATIONAL or VALUE_LIBPOLY) stands for the real
 *    number it denotes.
 *  - Element arguments are allocated by the same domain, and may alias. In an Ensures
 *    clause, the element that is set (e.g. c in add) denotes its value after the call,
 *    and the other elements denote their value before the call.
 *  - Unmarked clauses are soundness requirements, which every domain must meet. Clauses
 *    marked (A) are accuracy requirements, on which the plugins rely to make progress.
 *    In an accuracy clause w(S) <= 2^-(prec+1) ==> p(x) >= prec, the spare bit leaves
 *    room for rounding. A domain that meets only the soundness requirements, such as
 *    the sign domain, is used as a component of a product (abstractions/
 *    tra_product_domain.h) with a domain that meets both, such as ARB.
 */

#ifndef TRA_ABSTRACT_VALUES_H_
#define TRA_ABSTRACT_VALUES_H_

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>

#include <gmp.h>
#include <mpfr.h>
#include <poly/poly.h>

#include "mcsat/mcsat_types.h"


#ifndef INF_PREC
#define INF_PREC LONG_MAX
#endif

#ifndef NO_PREC
#define NO_PREC -1
#endif

// TODO: A few things I have in mind that merits discussion:
//  1. We could easily extend this interface to easily support some sort of ICP.
//     We could perform ICP at decision level 0, for all inequalities
//     that enter the trail. E.g., if we get x <= pi at level zero, at the
//     moment we are only learning x in R and pi in some initial interval [a,b],
//     but we could also learn x in (-infty, b]. This would require taking
//     inverses of functions, which is not currently supported below.
//  2. Join is now part of the interface (see "Lattice operations"); a meet
//     can be added once a use for it appears.
//     We should also probably add a way of intersecting with an mpfr_t interval
//     [lo,hi], so that distinct domains can be combined through this operation,
//     in the product domain.
//  3. tra_tv_position_t does not define TV_OUTSIDE, which would be useful if
//     we were to implement abstract domains other than intervals, e.g., intervals
//     that are periodic on 2*pi. We however need to be careful with the full
//     pipeline if we were to add such a token.

/*
 * Precision in bits (matches ARB's slong and MPFR's mpfr_prec_t). See the notation above.
 */
typedef long tra_prec_t;

/**
 * Requires: a or b is a precision.
 * Ensures:  result = INF_PREC  if a = b = INF_PREC;
 *           result = NO_PREC   otherwise, if a < 0, b < 0 or a + b >= INF_PREC;
 *           result = a + b     otherwise.
 */
static inline tra_prec_t tra_add_precisions(tra_prec_t a, tra_prec_t b) {
  assert(a == NO_PREC || a >= 1 || b == NO_PREC || b >= 1);

  if(a == INF_PREC && b == INF_PREC) return INF_PREC;
  if(a <= NO_PREC || b <= NO_PREC) return NO_PREC;
  if(a >= INF_PREC - b) return NO_PREC;

  return a + b;
}

/**
 * Requires: a and b are precisions.
 * Ensures:  result = min(a, b).
 */
static inline tra_prec_t tra_min_precisions(tra_prec_t a, tra_prec_t b) {
  assert(a == NO_PREC || a >= 1);
  assert(b == NO_PREC || b >= 1);
  return a < b ? a : b;
}

/**
 * Requires: a and b are precisions.
 * Ensures:  result = max(a, b).
 */
static inline tra_prec_t tra_max_precisions(tra_prec_t a, tra_prec_t b) {
  assert(a == NO_PREC || a >= 1);
  assert(b == NO_PREC || b >= 1);
  return a > b ? a : b;
}

/*
 * Opaque element of an abstract domain, created by alloc and destroyed by free.
 */
struct tra_interval_s;
typedef struct tra_interval_s *tra_itype_t;

/*
 * Position of a trail value relative to an element (see trail_value_position).
 */
typedef enum {
  TV_INSIDE,
  TV_BELOW,
  TV_ABOVE,
  TV_UNKNOWN,
} tra_tv_position_t;

/*
 * The abstract domain interface.
 */
typedef struct tra_ilib_s {

  /* ===========
   *  Lifecycle
   * =========== */

  /**
   * Ensures:  result is a new element (not NULL), and γ(result) = {0}.
   */
  tra_itype_t (*alloc)(void);

  /**
   * Requires: x is not used after the call.
   * Ensures:  the memory of x is released.
   */
  void (*free)(tra_itype_t x);

  /* ============
   *  Extraction
   * ============ */

  /**
   * Requires: lo and hi are initialised.
   * Ensures:  lo and hi are not NaN, and γ(x) ⊆ [lo, hi];
   *           (A):  lo is inf γ(x) rounded down, and hi is sup γ(x) rounded up,
   *                 to the mpfr precisions of lo and hi.
   */
  void (*get_interval_overapproximation_mpfr)(mpfr_t lo, mpfr_t hi, tra_itype_t x);

  /**
   * Requires: lo and hi are not NaN.
   * Ensures:  result ==> [lo, hi] ⊆ γ(x);
   *           (A):  [lo, hi] ⊆ γ(x) ==> result.
   */
  bool (*contains_interval_mpfr)(tra_itype_t x, const mpfr_t lo, const mpfr_t hi);

  /**
   * Ensures:  result is a precision;
   *           result = INF_PREC ==> γ(x) is a singleton;
   *           result != NO_PREC ==> w(γ(x)) <= 2^-result.
   */
  tra_prec_t (*get_precision)(tra_itype_t x);

  /* ============
   *  Assignment
   * ============ */

  /**
   * Ensures:  γ(dst) = γ(src) and p(dst) = p(src);
   *           later changes to src do not affect dst.
   */
  void (*set)(tra_itype_t dst, tra_itype_t src);

  /**
   * Ensures:  n ∈ γ(x);
   *           (A):  γ(x) = {n} and p(x) = INF_PREC.
   */
  void (*set_z)(tra_itype_t x, long n);

  /**
   * Ensures:  n ∈ γ(x);
   *           (A):  γ(x) = {n} and p(x) = INF_PREC.
   */
  void (*set_mpz)(tra_itype_t x, const mpz_t n);

  /**
   * Requires: b > 0.
   * Ensures:  a/b ∈ γ(x);
   *           (A):  p(x) >= precision;
   *           (A):  b = 1 ==> γ(x) = {a} and p(x) = INF_PREC.
   */
  void (*set_mpz_div)(tra_itype_t x,
                      const mpz_t a, const mpz_t b,
                      tra_prec_t precision);

  /**
   * Requires: a is finite.
   * Ensures:  a ∈ γ(x);
   *           (A):  p(x) >= precision.
   */
  void (*set_mpfr)(tra_itype_t x, const mpfr_t a, tra_prec_t precision);

  /**
   * Modifies: the isolating interval of *a, which may be refined in place; 
   *           *a still denotes the same number, and must not be copied by value.
   * Ensures:  *a ∈ γ(x);
   *           (A):  p(x) >= precision.
   */
  void (*set_algebraic_lp)(tra_itype_t x, const lp_algebraic_number_t* a, tra_prec_t precision);

  /**
   * Ensures:  γ(x) = R.
   */
  void (*set_R)(tra_itype_t x);

  /* ===========================
   *  Arithmetic on tra_itype_t
   * =========================== */

  /**
   * Ensures:  γ(a) + γ(b) ⊆ γ(c);
   *           (A):  w(γ(a) + γ(b)) <= 2^-(prec+1) ==> p(c) >= prec.
   */
  void (*add)(tra_itype_t c, tra_itype_t a, tra_itype_t b, tra_prec_t prec);

  /**
   * Ensures:  γ(a) - γ(b) ⊆ γ(c);
   *           (A):  w(γ(a) - γ(b)) <= 2^-(prec+1) ==> p(c) >= prec.
   */
  void (*sub)(tra_itype_t c, tra_itype_t a, tra_itype_t b, tra_prec_t prec);

  /**
   * Ensures:  γ(a) * γ(b) ⊆ γ(c);
   *           (A):  w(γ(a) * γ(b)) <= 2^-(prec+1) ==> p(c) >= prec.
   */
  void (*mul)(tra_itype_t c, tra_itype_t a, tra_itype_t b, tra_prec_t prec);

  /**
   * Requires: 0 ∉ γ(b).
   * Ensures:  γ(a) / γ(b) ⊆ γ(c);
   *           (A):  w(γ(a) / γ(b)) <= 2^-(prec+1) ==> p(c) >= prec.
   */
  void (*div)(tra_itype_t c, tra_itype_t a, tra_itype_t b, tra_prec_t prec);

  /**
   * Ensures:  |γ(a)| ⊆ γ(b);
   *           (A):  w(|γ(a)|) <= 2^-(p(a)+1) ==> p(b) >= p(a).
   */
  void (*abs)(tra_itype_t b, tra_itype_t a);

  /**
   * Ensures:  γ(a)^n ⊆ γ(c), where s^0 = 1 for every real s;
   *           (A):  w(γ(a)^n) <= 2^-(prec+1) ==> p(c) >= prec.
   */
  void (*pow_ui)(tra_itype_t c, tra_itype_t a, unsigned long n, tra_prec_t prec);

  /* ====================
   *  Lattice operations
   * ==================== */

  /**
   * Ensures:  γ(a) ∪ γ(b) ⊆ γ(c);
   *           (A):  w(γ(a) ∪ γ(b)) <= 2^-(prec+1) ==> p(c) >= prec.
   */
  void (*join)(tra_itype_t c, tra_itype_t a, tra_itype_t b, tra_prec_t prec);

  /* ==========================
   *  Transcendental functions
   * ========================== */

  /**
   * Ensures:  pi ∈ γ(x);
   *           (A):  p(x) >= prec.
   */
  void (*const_pi)(tra_itype_t x, tra_prec_t prec);

  /**
   * Ensures:  sin(γ(a)) ⊆ γ(x);
   *           (A):  w(sin(γ(a))) <= 2^-(prec+1) ==> p(x) >= prec.
   */
  void (*sin)(tra_itype_t x, tra_itype_t a, tra_prec_t prec);

  /**
   * Ensures:  cos(γ(a)) ⊆ γ(x);
   *           (A):  w(cos(γ(a))) <= 2^-(prec+1) ==> p(x) >= prec.
   */
  void (*cos)(tra_itype_t x, tra_itype_t a, tra_prec_t prec);

  /**
   * Ensures:  exp(γ(a)) ⊆ γ(x);
   *           (A):  w(exp(γ(a))) <= 2^-(prec+1) ==> p(x) >= prec.
   */
  void (*exp)(tra_itype_t x, tra_itype_t a, tra_prec_t prec);

  /* ==========
   *  Querying
   * ========== */

  /**
   * Ensures:  0 ∈ γ(x) ==> result;
   *           (A):  result ==> 0 ∈ γ(x).
   */
  bool (*zero_in)(tra_itype_t x);

  /**
   * Requires: a is finite.
   * Ensures:  result ==> a ∉ γ(x);
   *           (A):  a ∉ γ(x) ==> result.
   */
  bool (*avoids_mpfr)(tra_itype_t x, mpfr_t a);

  /**
   * Requires: v is real-valued.
   * Ensures:  result = TV_INSIDE ==> v ∈ γ(x);
   *           result = TV_BELOW  ==> v < γ(x);
   *           result = TV_ABOVE  ==> v > γ(x);
   *           (A):  v < γ(x) ==> result = TV_BELOW;
   *           (A):  v > γ(x) ==> result = TV_ABOVE.
   */
  tra_tv_position_t (*trail_value_position)(tra_itype_t x, const mcsat_value_t* v);

  /**
   * Ensures:  result ==> γ(x) ⊆ [0, +oo);
   *           (A):  γ(x) ⊆ [0, +oo) ==> result.
   */
  bool (*is_nonnegative)(tra_itype_t x);

  /**
   * Ensures:  result ==> γ(x) ⊆ (-oo, 0];
   *           (A):  γ(x) ⊆ (-oo, 0] ==> result.
   */
  bool (*is_nonpositive)(tra_itype_t x);

  /**
   * Ensures:  result ==> γ(x) is bounded;
   *           (A):  γ(x) is bounded ==> result.
   */
  bool (*is_bounded)(tra_itype_t x);

  /* ========
   *  Output
   * ======== */

  /**
   * Ensures:  γ(x) is printed to fp, with 'digits' significant decimal digits.
   */
  void (*fprintd)(FILE *fp, tra_itype_t x, long digits);

} tra_ilib_t;

/* ======================
    Some helpful getters
   ====================== */

/**
 * Let A(s) be the atom s = 0 if eq, and s >= 0 otherwise.
 * Requires: γ(r) ≠ ∅.
 * Ensures:  result ∈ {1, -1, 0};
 *           result = 1 ==> A(s) holds for every s ∈ γ(r);
 *           result = -1 ==> A(s) holds for no s ∈ γ(r);
 *           (A):  result != 0 if A(s) holds for every s ∈ γ(r) or for none, provided lib
 *                 meets the (A) clauses of is_nonnegative, is_nonpositive and zero_in on r.
 */
int tra_get_atom_truth(const tra_ilib_t* lib, tra_itype_t r, bool eq);

/**
 * Requires: x is not NaN.
 * Ensures:  result = min { n >= 0 : |x| < 2^n } if x is finite, and result = 0 if x is
 *           infinite. Callers should use the result only to choose working precisions.
 */
tra_prec_t tra_mpfr_magnitude_bits(const mpfr_t x);

/**
 * Requires: out was allocated by lib.
 * Modifies: the isolating interval of val, as in set_algebraic_lp.
 * Ensures:  result <==> val is real-valued;
 *           result ==> val ∈ γ(out);
 *           !result ==> out is unchanged;
 *           (A):  result ==> p(out) >= precision;
 *           (A):  result and val is an integer ==> γ(out) = {val} and p(out) = INF_PREC.
 */
bool tra_interval_around_value(const tra_ilib_t* lib, const mcsat_value_t* val, tra_prec_t precision, tra_itype_t out);

/**
 * Requires: lo and hi are initialised.
 * Modifies: the mpfr precisions of lo and hi.
 * Ensures:  γ(J) ⊆ [lo, hi];
 *           result <==> lo and hi are finite;
 *           result ==> γ(J) = [lo, hi],
 *                      or γ(J) is a singleton, precision != NO_PREC and hi - lo <= 2^-precision,
 *                      or the mpfr precisions of lo and hi are 2^20.
 */
bool tra_interval_to_mpfr(const tra_ilib_t* lib, tra_itype_t J, tra_prec_t precision, mpfr_t lo, mpfr_t hi);

/**
 * Requires: op is lib->sin, lib->cos or lib->exp, and f is sin, cos or exp accordingly;
 *           lo and hi are initialised.
 * Let K and J be the elements set by set_mpz_div(K, num(c), den(c), NO_PREC) and by
 * op(J, K, precision).
 * Ensures:  the contract of tra_interval_to_mpfr(lib, J, extract_precision, lo, hi);
 *           in particular, f(c) ∈ [lo, hi].
 */
bool tra_enclose_unop_at(const tra_ilib_t* lib, void (*op)(tra_itype_t, tra_itype_t, tra_prec_t),
                         const mpq_t c, tra_prec_t precision, tra_prec_t extract_precision,
                         mpfr_t lo, mpfr_t hi);

#endif /* TRA_ABSTRACT_VALUES_H_ */
