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
 * tra_product_domain.c
 *
 * Implementation of tra_ilib_t as a "reduced product" of n other tra_ilib_t
 * backends (configured once via tra_init_product_domain). An element of the
 * product denotes the INTERSECTION D = D_1 /\ ... /\ D_n of what each
 * component denotes: a tra_itype_t here is just n component tra_itype_t
 * slots, one per configured library.
 *
 * Soundness recipe. Considering that D is a subset of every D_i:
 *  - Operations (add ... exp, and every set_*) broadcast to all components: each
 *    computes its own sound result, and intersecting them only tightens.
 *  - is_nonnegative / is_nonpositive / is_bounded hold as soon as ANY component
 *    proves them. The function zero_in instead needs EVERY component to agree.
 *  - join is componentwise, which over-approximates:
 *    (A_1 /\ ... /\ A_n) \/ (B_1 /\ ... /\ B_n) is a subset of (A_1 \/ B_1) /\ ...
 *  - get_interval_overapproximation_mpfr intersects the component enclosures.
 *  - contains_interval_mpfr is a conjunction: [lo,hi] is inside an intersection 
 *    when it is inside every factor.
 */

#include "mcsat/tra/abstractions/tra_product_domain.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <gmp.h>
#include <mpfr.h>
#include <poly/poly.h>

#include "mcsat/mcsat_types.h"
#include "utils/memalloc.h"

/* =================
 *  Configuration
 * ================= */

static tra_ilib_t* const* g_libs = NULL;
static uint32_t g_n = 0;

static tra_ilib_t tra_product_ilib;

tra_ilib_t* tra_init_product_domain(tra_ilib_t* const* libs, uint32_t n) {
  assert(n > 0);
  // Written at the first call only: a later call may come from yices_reset_context, which does
  // not take the global lock of the thread-safe build, while another thread reads the configuration
  if (g_libs == NULL) {
    g_libs = libs;
    g_n = n;
  }
  assert(g_libs == libs && g_n == n);
  return &tra_product_ilib;
}

/* =================
 *  Internal helpers
 * ================= */

/* A product value is just an array of g_n component tra_itype_t slots,
 * one per configured library (see tra_init_product_domain). */
static inline tra_itype_t* as_product(tra_itype_t x) {
  return (tra_itype_t*) x;
}

/* ===========
 *  Lifecycle
 * =========== */

static tra_itype_t product_ilib_alloc(void) {
  assert(g_libs != NULL); // tra_init_product_domain must be called first
  tra_itype_t* p = (tra_itype_t*) safe_malloc(g_n * sizeof(tra_itype_t));
  for (uint32_t i = 0; i < g_n; i++) p[i] = g_libs[i]->alloc();
  return (tra_itype_t) p;
}

static void product_ilib_free(tra_itype_t x) {
  tra_itype_t* p = as_product(x);
  for (uint32_t i = 0; i < g_n; i++) g_libs[i]->free(p[i]);
  free(p);
}

/* ==========
 *  Querying
 * ========== */

// Only needed as a working precision for the boundedness probe in
// product_ilib_is_bounded below, not to produce a tight result.
#define TRA_PRODUCT_OVERLAP_PREC 64

// Forward declaration: defined in the Extraction section below, but needed
// here too by product_ilib_is_bounded.
static void product_ilib_get_interval_overapproximation_mpfr(mpfr_t lo, mpfr_t hi, tra_itype_t x);

static bool product_ilib_zero_in(tra_itype_t x) {
  tra_itype_t* p = as_product(x);
  // 0 is in the intersection iff every component contains it
  for (uint32_t i = 0; i < g_n; i++) {
    if (!g_libs[i]->zero_in(p[i])) return false;
  }
  return true;
}

static bool product_ilib_avoids_mpfr(tra_itype_t x, mpfr_t a) {
  tra_itype_t* p = as_product(x);
  // a is excluded from the interval as soon as ANY domain excludes it
  for (uint32_t i = 0; i < g_n; i++) {
    if (g_libs[i]->avoids_mpfr(p[i], a)) return true;
  }
  return false;
}

static tra_tv_position_t product_ilib_trail_value_position(tra_itype_t x, const mcsat_value_t* v) {
  tra_itype_t* p = as_product(x);
  bool unknown = false;
  for (uint32_t i = 0; i < g_n; i++) {
    tra_tv_position_t pi = g_libs[i]->trail_value_position(p[i], v);
    if (pi == TV_ABOVE || pi == TV_BELOW) return pi;
    if (pi == TV_UNKNOWN) unknown = true;
    // if pi == TV_INSIDE, we continue to check the other components
  }
  return unknown ? TV_UNKNOWN : TV_INSIDE;
}

static bool product_ilib_is_nonnegative(tra_itype_t x) {
  tra_itype_t* p = as_product(x);
  // Sound as soon as ANY single component proves it
  for (uint32_t i = 0; i < g_n; i++) {
    if (g_libs[i]->is_nonnegative(p[i])) return true;
  }
  return false;
}

static bool product_ilib_is_nonpositive(tra_itype_t x) {
  tra_itype_t* p = as_product(x);
  // Sound as soon as ANY single component proves it
  for (uint32_t i = 0; i < g_n; i++) {
    if (g_libs[i]->is_nonpositive(p[i])) return true;
  }
  return false;
}

static bool product_ilib_is_bounded(tra_itype_t x) {
  tra_itype_t* p = as_product(x);
  bool bounded = false;
  for (uint32_t i = 0; i < g_n; i++) {
    if (g_libs[i]->is_bounded(p[i])) { bounded = true; break; }
  }
  if (bounded || g_n <= 1) return bounded;

  // Combine overapproximations: one component's finite lower bound plus another's finite
  // upper bound can prove boundedness neither proves alone.
  mpfr_t lo, hi;
  mpfr_init2(lo, TRA_PRODUCT_OVERLAP_PREC);
  mpfr_init2(hi, TRA_PRODUCT_OVERLAP_PREC);
  product_ilib_get_interval_overapproximation_mpfr(lo, hi, x);
  bounded = mpfr_number_p(lo) && mpfr_number_p(hi);
  mpfr_clear(lo);
  mpfr_clear(hi);
  return bounded;
}

/* ============
 *  Extraction
 * ============ */

/* Intersection of overapproximations */
static void product_ilib_get_interval_overapproximation_mpfr(mpfr_t lo, mpfr_t hi, tra_itype_t x) {
  tra_itype_t* p = as_product(x);
  g_libs[0]->get_interval_overapproximation_mpfr(lo, hi, p[0]);

  mpfr_t clo, chi;
  mpfr_init2(clo, mpfr_get_prec(lo));
  mpfr_init2(chi, mpfr_get_prec(hi));
  for (uint32_t i = 1; i < g_n; i++) {
    g_libs[i]->get_interval_overapproximation_mpfr(clo, chi, p[i]);
    // Intersecting n sound enclosures is itself sound, and generally tighter.
    if (mpfr_cmp(clo, lo) > 0) mpfr_swap(lo, clo);
    if (mpfr_cmp(chi, hi) < 0) mpfr_swap(hi, chi);
  }
  mpfr_clear(clo);
  mpfr_clear(chi);
}

/* [lo,hi] is inside the intersection iff it is inside every component. */
static bool product_ilib_contains_interval_mpfr(tra_itype_t x, const mpfr_t lo, const mpfr_t hi) {
  tra_itype_t* p = as_product(x);
  for (uint32_t i = 0; i < g_n; i++) {
    if (!g_libs[i]->contains_interval_mpfr(p[i], lo, hi)) return false;
  }
  return true;
}

static tra_prec_t product_ilib_get_precision(tra_itype_t x) {
  tra_itype_t* p = as_product(x);
  tra_prec_t best = NO_PREC;
  for (uint32_t i = 0; i < g_n; i++) {
    // A d-bit precision witness for one component's own (wider) set is
    // still a valid witness for the (narrower) intersection, so taking the
    // best individual answer is sound -- it may just not be maximally tight
    // (a witness combining several components' bounds could do better).
    tra_prec_t pi = g_libs[i]->get_precision(p[i]);
    if (pi == INF_PREC) return INF_PREC;
    best = tra_max_precisions(best, pi);
  }
  return best;
}

/* ============
 *  Assignment
 * ============ */

static void product_ilib_set(tra_itype_t dst, tra_itype_t src) {
  tra_itype_t* pd = as_product(dst);
  tra_itype_t* ps = as_product(src);
  for (uint32_t i = 0; i < g_n; i++) g_libs[i]->set(pd[i], ps[i]);
}

static void product_ilib_set_z(tra_itype_t x, long n) {
  tra_itype_t* p = as_product(x);
  for (uint32_t i = 0; i < g_n; i++) g_libs[i]->set_z(p[i], n);
}

static void product_ilib_set_mpz(tra_itype_t x, const mpz_t n) {
  tra_itype_t* p = as_product(x);
  for (uint32_t i = 0; i < g_n; i++) g_libs[i]->set_mpz(p[i], n);
}

static void product_ilib_set_mpz_div(tra_itype_t x, const mpz_t a, const mpz_t b, tra_prec_t prec) {
  tra_itype_t* p = as_product(x);
  for (uint32_t i = 0; i < g_n; i++) g_libs[i]->set_mpz_div(p[i], a, b, prec);
}

static void product_ilib_set_mpfr(tra_itype_t x, const mpfr_t a, tra_prec_t prec) {
  tra_itype_t* p = as_product(x);
  for (uint32_t i = 0; i < g_n; i++) g_libs[i]->set_mpfr(p[i], a, prec);
}

static void product_ilib_set_algebraic_lp(tra_itype_t out, const lp_algebraic_number_t* a, tra_prec_t prec) {
  tra_itype_t* p = as_product(out);
  // set_algebraic_lp is allowed to refine 'a' in place; refining it further
  // before the next component only helps, so calling every component in
  // sequence on the same 'a' is safe.
  for (uint32_t i = 0; i < g_n; i++) g_libs[i]->set_algebraic_lp(p[i], a, prec);
}

static void product_ilib_set_R(tra_itype_t x) {
  tra_itype_t* p = as_product(x);
  for (uint32_t i = 0; i < g_n; i++) g_libs[i]->set_R(p[i]);
}

/* ===========================
 *  Arithmetic on tra_itype_t
 * =========================== */

static void product_ilib_add(tra_itype_t c, tra_itype_t a, tra_itype_t b, tra_prec_t prec) {
  tra_itype_t *pc = as_product(c), *pa = as_product(a), *pb = as_product(b);
  for (uint32_t i = 0; i < g_n; i++) g_libs[i]->add(pc[i], pa[i], pb[i], prec);
}

static void product_ilib_sub(tra_itype_t c, tra_itype_t a, tra_itype_t b, tra_prec_t prec) {
  tra_itype_t *pc = as_product(c), *pa = as_product(a), *pb = as_product(b);
  for (uint32_t i = 0; i < g_n; i++) g_libs[i]->sub(pc[i], pa[i], pb[i], prec);
}

static void product_ilib_mul(tra_itype_t c, tra_itype_t a, tra_itype_t b, tra_prec_t prec) {
  tra_itype_t *pc = as_product(c), *pa = as_product(a), *pb = as_product(b);
  for (uint32_t i = 0; i < g_n; i++) g_libs[i]->mul(pc[i], pa[i], pb[i], prec);
}

static void product_ilib_div(tra_itype_t c, tra_itype_t a, tra_itype_t b, tra_prec_t prec) {
  tra_itype_t *pc = as_product(c), *pa = as_product(a), *pb = as_product(b);
  for (uint32_t i = 0; i < g_n; i++) g_libs[i]->div(pc[i], pa[i], pb[i], prec);
}

static void product_ilib_abs(tra_itype_t b, tra_itype_t a) {
  tra_itype_t *pb = as_product(b), *pa = as_product(a);
  for (uint32_t i = 0; i < g_n; i++) g_libs[i]->abs(pb[i], pa[i]);
}

static void product_ilib_pow_ui(tra_itype_t c, tra_itype_t a, unsigned long n, tra_prec_t prec) {
  tra_itype_t *pc = as_product(c), *pa = as_product(a);
  for (uint32_t i = 0; i < g_n; i++) g_libs[i]->pow_ui(pc[i], pa[i], n, prec);
}

/* ====================
 *  Lattice operations
 * ==================== */

// Componentwise join: sound, see the soundness recipe at the top of this file.
static void product_ilib_join(tra_itype_t c, tra_itype_t a, tra_itype_t b, tra_prec_t prec) {
  tra_itype_t *pc = as_product(c), *pa = as_product(a), *pb = as_product(b);
  for (uint32_t i = 0; i < g_n; i++) g_libs[i]->join(pc[i], pa[i], pb[i], prec);
}

/* ==========================
 *  Transcendental functions
 * ========================== */

static void product_ilib_const_pi(tra_itype_t x, tra_prec_t prec) {
  tra_itype_t* p = as_product(x);
  for (uint32_t i = 0; i < g_n; i++) g_libs[i]->const_pi(p[i], prec);
}

static void product_ilib_sin(tra_itype_t x, tra_itype_t a, tra_prec_t prec) {
  tra_itype_t *px = as_product(x), *pa = as_product(a);
  for (uint32_t i = 0; i < g_n; i++) g_libs[i]->sin(px[i], pa[i], prec);
}

static void product_ilib_cos(tra_itype_t x, tra_itype_t a, tra_prec_t prec) {
  tra_itype_t *px = as_product(x), *pa = as_product(a);
  for (uint32_t i = 0; i < g_n; i++) g_libs[i]->cos(px[i], pa[i], prec);
}

static void product_ilib_exp(tra_itype_t x, tra_itype_t a, tra_prec_t prec) {
  tra_itype_t *px = as_product(x), *pa = as_product(a);
  for (uint32_t i = 0; i < g_n; i++) g_libs[i]->exp(px[i], pa[i], prec);
}

/* ========
 *  Output
 * ======== */

static void product_ilib_fprintd(FILE *fp, tra_itype_t x, long digits) {
  tra_itype_t* p = as_product(x);
  for (uint32_t i = 0; i < g_n; i++) {
    if (i > 0) fprintf(fp, " & ");
    g_libs[i]->fprintd(fp, p[i], digits);
  }
}

/* =========================
 *  Global library instance
 * ========================= */

static tra_ilib_t tra_product_ilib = {
  .alloc             = product_ilib_alloc,
  .free              = product_ilib_free,
  .set               = product_ilib_set,
  .set_z             = product_ilib_set_z,
  .set_mpz           = product_ilib_set_mpz,
  .set_mpz_div       = product_ilib_set_mpz_div,
  .set_mpfr          = product_ilib_set_mpfr,
  .set_algebraic_lp  = product_ilib_set_algebraic_lp,
  .set_R             = product_ilib_set_R,
  .const_pi          = product_ilib_const_pi,
  .get_interval_overapproximation_mpfr = product_ilib_get_interval_overapproximation_mpfr,
  .contains_interval_mpfr = product_ilib_contains_interval_mpfr,
  .get_precision     = product_ilib_get_precision,
  .zero_in           = product_ilib_zero_in,
  .avoids_mpfr       = product_ilib_avoids_mpfr,
  .trail_value_position = product_ilib_trail_value_position,
  .is_nonnegative    = product_ilib_is_nonnegative,
  .is_nonpositive    = product_ilib_is_nonpositive,
  .is_bounded        = product_ilib_is_bounded,
  .add               = product_ilib_add,
  .sub               = product_ilib_sub,
  .mul               = product_ilib_mul,
  .div               = product_ilib_div,
  .abs               = product_ilib_abs,
  .pow_ui            = product_ilib_pow_ui,
  .join              = product_ilib_join,
  .sin               = product_ilib_sin,
  .cos               = product_ilib_cos,
  .exp               = product_ilib_exp,
  .fprintd           = product_ilib_fprintd,
};
