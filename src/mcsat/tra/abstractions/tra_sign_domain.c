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
 * tra_sign_domain.c
 *
 * Implementation of tra_ilib_t using a simple sign domain.
 * By default, this abstract domain is unsound with respect to the specification
 * given in tra_abstract_values.h. It only becomes sound if combined with
 * a sound domain through tra_product_domain.h.
 *
 * An element of the sign domain is a uint8_t bitmask of the SIGN_ZERO / SIGN_POS /
 * SIGN_NEG flags below. Unions of the corresponding sets are obtained by taking a
 * bitwise or. E.g., the set [-infty,0] is represented by SIGN_ZERO | SIGN_NEG.
 */

#include "mcsat/tra/abstractions/tra_sign_domain.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <gmp.h>
#include <mpfr.h>
#include <poly/algebraic_number.h>
#include <poly/poly.h>

#include "mcsat/mcsat_types.h"
#include "mcsat/value.h"
#include "utils/memalloc.h"

#include "mcsat/tra/utils/tra_exact_values.h"

/* =================
 *  Internal helpers
 * ================= */

enum {
  SIGN_ZERO = 1, // the set {0}
  SIGN_POS  = 2, // the set (0,+infty)
  SIGN_NEG  = 4, // the set (-infty,0)
};

/* Cast opaque pointer to the concrete uint8_t* storage. */
static inline uint8_t* as_sign(tra_itype_t x) {
  return (uint8_t*) x;
}

/* signs are stored in a uint8_t. If sign = 0, the sign is uninitialized. */
static bool is_initialized(tra_itype_t x){
  return *as_sign(x) > 0;
}

/* ==========
 *  Querying
 * ========== */

static bool sign_ilib_zero_in(tra_itype_t x) {
  return *as_sign(x) & SIGN_ZERO;
}

static bool sign_ilib_avoids_mpfr(tra_itype_t x, mpfr_t a) {
  int s = mpfr_sgn(a);
  uint8_t sa = (s == 0) ? SIGN_ZERO : (s > 0 ? SIGN_POS : SIGN_NEG);
  return (*as_sign(x) & sa) == 0;
}

static tra_tv_position_t sign_ilib_trail_value_position(tra_itype_t x, const mcsat_value_t* v) {
  uint8_t sx = *as_sign(x);
  int s = tra_value_sign(v);
  uint8_t sv = (s == 0) ? SIGN_ZERO : (s > 0 ? SIGN_POS : SIGN_NEG);

  if (sx & sv) return TV_INSIDE;
  if (sv == SIGN_POS) return TV_ABOVE;
  if (sv == SIGN_NEG) return TV_BELOW;
  return TV_UNKNOWN;
}

static bool sign_ilib_is_nonnegative(tra_itype_t x) {
  return (*as_sign(x) & SIGN_NEG) == 0;
}

static bool sign_ilib_is_nonpositive(tra_itype_t x) {
  return (*as_sign(x) & SIGN_POS) == 0;
}

static bool sign_ilib_is_bounded(tra_itype_t x) {
  // treating 0 (no bits set) as emptyset. Check with is_initialized
  return (*as_sign(x) & (SIGN_POS | SIGN_NEG)) == 0;
}

/* ===========
 *  Lifecycle
 * =========== */

static tra_itype_t sign_ilib_alloc(void) {
  uint8_t* p = (uint8_t*) safe_malloc(sizeof(uint8_t));
  *p = SIGN_ZERO; // {0}, per the alloc contract in tra_abstract_values.h
  return (tra_itype_t) p;
}

static void sign_ilib_free(tra_itype_t x) {
  free(x);
}

/* ============
 *  Extraction
 * ============ */

static void sign_ilib_get_interval_overapproximation_mpfr(mpfr_t lo, mpfr_t hi, tra_itype_t x) {
  uint8_t sign = *as_sign(x);

  // Negative values present => unbounded below, else bounded by 0 from below.
  if (sign & SIGN_NEG) mpfr_set_inf(lo, -1);
  else mpfr_set_zero(lo, 1);

  // Positive values present => unbounded above, else bounded by 0 from above.
  if (sign & SIGN_POS) mpfr_set_inf(hi, 1);
  else mpfr_set_zero(hi, 1);
}

// Exact: [lo,hi] is inside x iff every sign class it meets is one of x's.
static bool sign_ilib_contains_interval_mpfr(tra_itype_t x, const mpfr_t lo, const mpfr_t hi) {
  assert(!mpfr_nan_p(lo) && !mpfr_nan_p(hi));
  if (mpfr_cmp(lo, hi) > 0) return true; // empty box, vacuously inside

  uint8_t required = 0;
  if (mpfr_sgn(lo) < 0) required |= SIGN_NEG;
  if (mpfr_sgn(lo) <= 0 && mpfr_sgn(hi) >= 0) required |= SIGN_ZERO;
  if (mpfr_sgn(hi) > 0) required |= SIGN_POS;

  // An uninitialized x denotes the empty set, which certifies nothing.
  return is_initialized(x) && (required & ~*as_sign(x)) == 0;
}

static tra_prec_t sign_ilib_get_precision(tra_itype_t x) {
  if (*as_sign(x) == SIGN_ZERO) return INF_PREC;
  return NO_PREC;
}

/* ============
 *  Assignment
 * ============ */

static void sign_ilib_set(tra_itype_t dst, tra_itype_t src) {
  *as_sign(dst) = *as_sign(src);
}

static void sign_ilib_set_z(tra_itype_t x, long n) {
  if (n == 0) *as_sign(x) = SIGN_ZERO;
  else if (n > 0) *as_sign(x) = SIGN_POS;
  else *as_sign(x) = SIGN_NEG;
}

static void sign_ilib_set_mpz(tra_itype_t x, const mpz_t n) {
  int s = mpz_sgn(n);
  if (s == 0) *as_sign(x) = SIGN_ZERO;
  else if (s > 0) *as_sign(x) = SIGN_POS;
  else *as_sign(x) = SIGN_NEG;
}

static void sign_ilib_set_mpz_div(tra_itype_t x,
                                  const mpz_t a, const mpz_t b,
                                  tra_prec_t prec) {
  assert(mpz_sgn(b) > 0);
  assert(prec != INF_PREC);

  // b > 0, so sign(a/b) = sign(a)
  int s = mpz_sgn(a);
  if (s == 0) *as_sign(x) = SIGN_ZERO;
  else if (s > 0) *as_sign(x) = SIGN_POS;
  else *as_sign(x) = SIGN_NEG;
}

static void sign_ilib_set_mpfr(tra_itype_t x, const mpfr_t a, tra_prec_t prec) {
  assert(prec != INF_PREC);

  int s = mpfr_sgn(a);
  if (s == 0) *as_sign(x) = SIGN_ZERO;
  else if (s > 0) *as_sign(x) = SIGN_POS;
  else *as_sign(x) = SIGN_NEG;
}

static void sign_ilib_set_algebraic_lp(tra_itype_t out, const lp_algebraic_number_t* a, tra_prec_t prec) {
  assert(prec != INF_PREC);

  int s = lp_algebraic_number_sgn(a);
  if (s == 0) *as_sign(out) = SIGN_ZERO;
  else if (s > 0) *as_sign(out) = SIGN_POS;
  else *as_sign(out) = SIGN_NEG;
}

static void sign_ilib_set_R(tra_itype_t x) {
  *as_sign(x) = SIGN_ZERO | SIGN_POS | SIGN_NEG;
}

/* ===========================
 *  Arithmetic on tra_itype_t
 * =========================== */

// Helper function to combine signs
// op is the operation (see e.g., sign_class_add).
static uint8_t sign_combine(uint8_t sa, uint8_t sb, uint8_t (*op)(uint8_t, uint8_t)) {
  static const uint8_t sign_classes[3] = {SIGN_ZERO, SIGN_POS, SIGN_NEG};
  uint8_t result = 0;
  for (int i = 0; i < 3; i++) {
    if (!(sa & sign_classes[i])) continue;
    for (int j = 0; j < 3; j++) {
      if (sb & sign_classes[j]) result |= op(sign_classes[i], sign_classes[j]);
    }
  }
  return result;
}

static uint8_t sign_negate(uint8_t s) {
  uint8_t r = s & SIGN_ZERO;
  if (s & SIGN_POS) r |= SIGN_NEG;
  if (s & SIGN_NEG) r |= SIGN_POS;
  return r;
}

static uint8_t sign_class_add(uint8_t x, uint8_t y) {
  if (x == SIGN_ZERO) return y;   // 0 + y = y
  if (y == SIGN_ZERO) return x;   // x + 0 = x
  if (x == y) return x;           // P+P=P, N+N=N
  return SIGN_ZERO | SIGN_POS | SIGN_NEG; // P+N or N+P: sign undetermined without magnitudes
}

/* Also the correct table for division, since b never contains zero */
static uint8_t sign_class_mul(uint8_t x, uint8_t y) {
  if (x == SIGN_ZERO || y == SIGN_ZERO) return SIGN_ZERO;
  return (x == y) ? SIGN_POS : SIGN_NEG;
}

static void sign_ilib_add(tra_itype_t c, tra_itype_t a, tra_itype_t b, tra_prec_t prec) {
  assert(prec != INF_PREC);
  *as_sign(c) = sign_combine(*as_sign(a), *as_sign(b), sign_class_add);
}

static void sign_ilib_sub(tra_itype_t c, tra_itype_t a, tra_itype_t b, tra_prec_t prec) {
  assert(prec != INF_PREC);
  *as_sign(c) = sign_combine(*as_sign(a), sign_negate(*as_sign(b)), sign_class_add);
}

static void sign_ilib_mul(tra_itype_t c, tra_itype_t a, tra_itype_t b, tra_prec_t prec) {
  assert(prec != INF_PREC);
  *as_sign(c) = sign_combine(*as_sign(a), *as_sign(b), sign_class_mul);
}

static void sign_ilib_div(tra_itype_t c, tra_itype_t a, tra_itype_t b, tra_prec_t prec) {
  assert(prec != INF_PREC);
  // do not assert that b contains zero: sign is too coarse for this
  *as_sign(c) = sign_combine(*as_sign(a), *as_sign(b), sign_class_mul);
}

static void sign_ilib_abs(tra_itype_t b, tra_itype_t a) {
  uint8_t sa = *as_sign(a);
  uint8_t result = sa & SIGN_ZERO;                  // 0 stays 0
  if (sa & (SIGN_POS | SIGN_NEG)) result |= SIGN_POS; // any nonzero value becomes positive
  *as_sign(b) = result;
}

static void sign_ilib_pow_ui(tra_itype_t c, tra_itype_t a, unsigned long n, tra_prec_t prec) {
  assert(prec != INF_PREC);

  if (n == 0) { *as_sign(c) = SIGN_POS; return; } // a^0 = 1, for every a (including 0)

  uint8_t sa = *as_sign(a);
  bool even = (n % 2 == 0);
  uint8_t result = sa & SIGN_ZERO;                         // 0^n = 0
  if (sa & SIGN_POS) result |= SIGN_POS;                   // P^n = P
  if (sa & SIGN_NEG) result |= even ? SIGN_POS : SIGN_NEG; // N^n = P if n even, N if n odd
  *as_sign(c) = result;
}

/* ====================
 *  Lattice operations
 * ==================== */

// Sign sets are closed under union: the join is the bitwise or, exactly.
static void sign_ilib_join(tra_itype_t c, tra_itype_t a, tra_itype_t b, tra_prec_t prec) {
  assert(prec != INF_PREC);
  *as_sign(c) = *as_sign(a) | *as_sign(b);
}

/* ==========================
 *  Transcendental functions
 * ========================== */

static void sign_ilib_const_pi(tra_itype_t x, tra_prec_t prec) {
  assert(prec != INF_PREC);
  *as_sign(x) = SIGN_POS; // pi > 0
}

static void sign_ilib_sin(tra_itype_t x, tra_itype_t a, tra_prec_t prec) {
  assert(prec != INF_PREC);
  *as_sign(x) = (*as_sign(a) == SIGN_ZERO) ? SIGN_ZERO : (SIGN_ZERO | SIGN_POS | SIGN_NEG);
}

static void sign_ilib_cos(tra_itype_t x, tra_itype_t a, tra_prec_t prec) {
  assert(prec != INF_PREC);
  // Unlike sin, the zero argument is not a fixed point: cos(0) = 1 > 0.
  *as_sign(x) = (*as_sign(a) == SIGN_ZERO) ? SIGN_POS : (SIGN_ZERO | SIGN_POS | SIGN_NEG);
}

static void sign_ilib_exp(tra_itype_t x, tra_itype_t a, tra_prec_t prec) {
  assert(prec != INF_PREC);
  *as_sign(x) = SIGN_POS; // exp is always strictly positive
}

/* ========
 *  Output
 * ======== */

static void sign_ilib_fprintd(FILE *fp, tra_itype_t x, long digits) {
  switch (*as_sign(x)) {
    case 0: fprintf(fp, "{}"); break;
    case SIGN_ZERO: fprintf(fp, "{0}"); break;
    case SIGN_POS: fprintf(fp, "(0,+inf)"); break;
    case SIGN_ZERO | SIGN_POS: fprintf(fp, "[0,+inf)"); break;
    case SIGN_NEG: fprintf(fp, "(-inf,0)"); break;
    case SIGN_ZERO | SIGN_NEG: fprintf(fp, "(-inf,0]"); break;
    case SIGN_POS | SIGN_NEG: fprintf(fp, "R\\{0}"); break;
    case SIGN_ZERO | SIGN_POS | SIGN_NEG: fprintf(fp, "R"); break;
  }
}

/* =========================
 *  Global library instance
 * ========================= */

tra_ilib_t tra_sign_ilib = {
  .alloc             = sign_ilib_alloc,
  .free              = sign_ilib_free,
  .set               = sign_ilib_set,
  .set_z             = sign_ilib_set_z,
  .set_mpz           = sign_ilib_set_mpz,
  .set_mpz_div       = sign_ilib_set_mpz_div,
  .set_mpfr          = sign_ilib_set_mpfr,
  .set_algebraic_lp  = sign_ilib_set_algebraic_lp,
  .set_R             = sign_ilib_set_R,
  .const_pi          = sign_ilib_const_pi,
  .get_interval_overapproximation_mpfr = sign_ilib_get_interval_overapproximation_mpfr,
  .contains_interval_mpfr = sign_ilib_contains_interval_mpfr,
  .get_precision     = sign_ilib_get_precision,
  .zero_in           = sign_ilib_zero_in,
  .avoids_mpfr       = sign_ilib_avoids_mpfr,
  .trail_value_position = sign_ilib_trail_value_position,
  .is_nonnegative    = sign_ilib_is_nonnegative,
  .is_nonpositive    = sign_ilib_is_nonpositive,
  .is_bounded        = sign_ilib_is_bounded,
  .add               = sign_ilib_add,
  .sub               = sign_ilib_sub,
  .mul               = sign_ilib_mul,
  .div               = sign_ilib_div,
  .abs               = sign_ilib_abs,
  .pow_ui            = sign_ilib_pow_ui,
  .join              = sign_ilib_join,
  .sin               = sign_ilib_sin,
  .cos               = sign_ilib_cos,
  .exp               = sign_ilib_exp,
  .fprintd           = sign_ilib_fprintd,
};
