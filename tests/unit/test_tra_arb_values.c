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
 * TEST OF THE ARB-BASED ABSTRACT DOMAIN (tra_ilib_t)
 *
 * Written against the tra_ilib_t interface rather than against ARB, so the
 * same checks apply to any future backend: point &lib at another instance.
 * The lattice tests are also run on the reduced product of the ARB and sign
 * domains, which is the configuration used by the TRA plugin.
 *
 * Everything asserted here is a contract from tra_abstract_values.h, in
 * particular the two ends of the precision scale, which are easy to get wrong:
 *   - exact inputs must give an INF_PREC result whenever the operation is exact,
 *     whatever precision is requested (INF_PREC itself is never requested: the
 *     domains assert against it, see tra_abstract_values.h);
 *   - NO_PREC must be accepted everywhere ("any precision will do").
 */

#include <stdio.h>
#include <stdbool.h>

#if HAVE_MCSAT

#include <gmp.h>
#include <mpfr.h>
#include <poly/algebraic_number.h>
#include <poly/dyadic_interval.h>
#include <poly/upolynomial.h>

#include "mcsat/tra/abstractions/tra_arb_domain.h"
#include "mcsat/tra/abstractions/tra_sign_domain.h"
#include "mcsat/tra/abstractions/tra_product_domain.h"

static tra_ilib_t* lib;
static uint32_t failed;

static void check(const char* what, bool ok) {
  printf("%-52s %s\n", what, ok ? "ok" : "FAILED");
  if (!ok) failed++;
}

// Working precision for the enclosure comparisons below: only needs to be wide
// enough to keep the endpoints of the intervals under test apart.
#define ENCL_PREC 512

// True when the enclosure of x sits inside the enclosure of y. Both are
// over-approximations, so this witnesses "x is the tighter enclosure" only when
// the two are known to enclose the same value.
static bool encl_inside(tra_itype_t x, tra_itype_t y) {
  mpfr_t xlo, xhi, ylo, yhi;
  mpfr_inits2(ENCL_PREC, xlo, xhi, ylo, yhi, (mpfr_ptr) 0);
  lib->get_interval_overapproximation_mpfr(xlo, xhi, x);
  lib->get_interval_overapproximation_mpfr(ylo, yhi, y);
  bool inside = mpfr_cmp(ylo, xlo) <= 0 && mpfr_cmp(xhi, yhi) <= 0;
  mpfr_clears(xlo, xhi, ylo, yhi, (mpfr_ptr) 0);
  return inside;
}

// True unless the enclosures of x and y are provably disjoint.
static bool encl_overlap(tra_itype_t x, tra_itype_t y) {
  mpfr_t xlo, xhi, ylo, yhi;
  mpfr_inits2(ENCL_PREC, xlo, xhi, ylo, yhi, (mpfr_ptr) 0);
  lib->get_interval_overapproximation_mpfr(xlo, xhi, x);
  lib->get_interval_overapproximation_mpfr(ylo, yhi, y);
  bool disjoint = mpfr_cmp(xhi, ylo) < 0 || mpfr_cmp(yhi, xlo) < 0;
  mpfr_clears(xlo, xhi, ylo, yhi, (mpfr_ptr) 0);
  return !disjoint;
}

/*
 * Exactness: an exact input stays exact, and so does any exact operation on
 * exact inputs, even when no precision is requested (NO_PREC).
 */
static void test_exact(void) {
  printf("\n--- exact values ---\n");

  tra_itype_t a = lib->alloc();
  tra_itype_t b = lib->alloc();
  tra_itype_t c = lib->alloc();
  mpz_t big, one;
  mpz_init(big);
  mpz_init_set_ui(one, 1);
  mpz_ui_pow_ui(big, 2, 100);

  lib->set_mpz(a, big);
  lib->set_mpz(b, one);
  check("set_mpz is exact", lib->get_precision(a) == INF_PREC);

  // NO_PREC only promises soundness: 2^100 + 1 needs 101 bits, more than the
  // minimal working precision, so add and sub may round (but must enclose).
  mpfr_t f;
  mpfr_init2(f, 128);
  mpfr_set_z(f, big, MPFR_RNDN);
  mpfr_add_ui(f, f, 1, MPFR_RNDN);
  lib->add(c, a, b, NO_PREC);
  check("add at NO_PREC encloses 2^100 + 1", !lib->avoids_mpfr(c, f));
  mpfr_sub_ui(f, f, 2, MPFR_RNDN);
  lib->sub(c, a, b, NO_PREC);
  check("sub at NO_PREC encloses 2^100 - 1", !lib->avoids_mpfr(c, f));
  mpfr_clear(f);
  // Products of exact values are computed exactly whatever the request
  lib->mul(c, a, a, NO_PREC);
  check("mul of exact integers at NO_PREC is exact", lib->get_precision(c) == INF_PREC);
  lib->pow_ui(c, a, 3, NO_PREC);
  check("pow_ui of an exact integer at NO_PREC is exact", lib->get_precision(c) == INF_PREC);

  // A finite request is for ABSOLUTE accuracy, so add asks ARB for
  // 64 + magnitude bits -- 165 here, well past the 101 that 2^100 + 1 needs.
  // Integer arithmetic is therefore lossless even at a modest request, and
  // this check fails if the magnitude adjustment is ever dropped.
  lib->add(c, a, b, 64);
  check("add at 64 bits is exact for integers", lib->get_precision(c) == INF_PREC);

  // Rationals are exact, and so are the points computed from them
  mpz_t two, three;
  mpz_init_set_ui(two, 2);
  mpz_init_set_ui(three, 3);
  mpfr_init2(f, 64);
  mpfr_set_ui(f, 1, MPFR_RNDN);
  lib->set_mpz_div(a, one, three, 64);
  lib->set_mpz_div(b, two, three, 64);
  lib->add(c, a, b, 64);
  check("1/3 + 2/3 is exactly 1", lib->get_precision(c) == INF_PREC && !lib->avoids_mpfr(c, f));
  lib->set_z(a, 0);
  lib->exp(c, a, 64);
  check("exp(0) is exactly 1", lib->get_precision(c) == INF_PREC && !lib->avoids_mpfr(c, f));
  lib->set_R(b);
  lib->mul(c, a, b, 64);
  check("0 * R is exactly 0", lib->is_nonnegative(c) && lib->is_nonpositive(c));
  mpfr_clear(f);
  mpz_clear(two);
  mpz_clear(three);

  lib->free(a);
  lib->free(b);
  lib->free(c);
  mpz_clear(big);
  mpz_clear(one);
}

/*
 * A finite request must be met, and NO_PREC must never be rejected: it reaches
 * tra_add_precisions, which asserts on negative input, so a debug build would
 * abort if an operation forwarded it unfiltered.
 */
static void test_precision_requests(void) {
  printf("\n--- finite precisions and NO_PREC ---\n");

  tra_itype_t a = lib->alloc();
  tra_itype_t b = lib->alloc();
  tra_itype_t c = lib->alloc();
  mpz_t one, three;
  mpz_init_set_ui(one, 1);
  mpz_init_set_ui(three, 3);

  lib->set_mpz_div(a, one, three, 100);
  check("1/3 at 100 bits meets the request", lib->get_precision(a) >= 100);
  check("1/3 is exact", lib->get_precision(a) == INF_PREC);
  check("1/3 does not contain zero", !lib->zero_in(a));

  lib->set_mpz_div(b, one, three, 200);
  check("1/3 at 200 bits is tighter", encl_inside(b, a));

  lib->set_mpz(b, one);
  lib->add(c, a, b, 100);
  check("add at 100 bits meets the request", lib->get_precision(c) >= 100);
  lib->mul(c, a, b, 100);
  check("mul at 100 bits meets the request", lib->get_precision(c) >= 100);

  lib->set_mpz(c, three);
  lib->div(c, b, c, 100);
  check("div of exact values meets the request", lib->get_precision(c) >= 100);
  lib->div(c, b, a, 100);
  mpfr_t f;
  mpfr_init2(f, 64);
  mpfr_set_ui(f, 3, MPFR_RNDN);
  check("div by 1/3 encloses 3", !lib->avoids_mpfr(c, f));
  check("div of exact rationals is exact", lib->get_precision(c) == INF_PREC);
  mpfr_clear(f);

  // No result is checked here: the point is that none of these abort.
  lib->add(c, a, b, NO_PREC);
  lib->sub(c, a, b, NO_PREC);
  lib->mul(c, a, b, NO_PREC);
  lib->div(c, b, a, NO_PREC);
  lib->pow_ui(c, a, 3, NO_PREC);
  lib->const_pi(c, NO_PREC);
  lib->sin(c, a, NO_PREC);
  lib->exp(c, a, NO_PREC);
  check("NO_PREC is accepted by every operation", true);

  lib->free(a);
  lib->free(b);
  lib->free(c);
  mpz_clear(one);
  mpz_clear(three);
}

static void test_queries(void) {
  printf("\n--- zero_in / avoids_mpfr / enclosures ---\n");

  tra_itype_t a = lib->alloc();
  tra_itype_t b = lib->alloc();
  tra_itype_t c = lib->alloc();
  mpz_t zero, one, three;
  mpz_init_set_ui(zero, 0);
  mpz_init_set_ui(one, 1);
  mpz_init_set_ui(three, 3);

  lib->set_mpz(a, zero);
  check("zero_in(0)", lib->zero_in(a));
  lib->set_mpz(a, one);
  check("!zero_in(1)", !lib->zero_in(a));

  // avoids_mpfr decides membership of a point exactly.
  mpfr_t f;
  mpfr_init2(f, 128);
  mpfr_set_ui(f, 1, MPFR_RNDN);
  check("!avoids_mpfr(1, 1)", !lib->avoids_mpfr(a, f));
  mpfr_set_ui(f, 2, MPFR_RNDN);
  check("avoids_mpfr(1, 2)", lib->avoids_mpfr(a, f));

  lib->set_mpz_div(b, one, three, 100);   /* 1/3 */
  check("avoids_mpfr(1/3, 1)", lib->avoids_mpfr(b, f));
  mpfr_set_ui(f, 1, MPFR_RNDN);
  mpfr_div_ui(f, f, 3, MPFR_RNDN);
  // 1/3 is exact: its 128-bit rounding is another number
  check("avoids_mpfr(1/3, 1/3 to 128 bits)", lib->avoids_mpfr(b, f));

  check("!encl_overlap(1, 1/3)", !encl_overlap(a, b));
  check("encl_overlap(1/3, 1/3)", encl_overlap(b, b));

  check("encl_inside is reflexive", encl_inside(b, b));
  lib->add(c, b, b, 100);                 /* 2/3 */
  check("!encl_inside(1/3, 2/3)", !encl_inside(b, c));

  // A tighter enclosure of the same value sits inside a looser one.
  lib->set_mpz_div(a, one, three, 30);
  lib->set_mpz_div(b, one, three, 300);
  check("encl_inside(tight 1/3, loose 1/3)", encl_inside(b, a));

  mpfr_clear(f);

  lib->free(a);
  lib->free(b);
  lib->free(c);
  mpz_clear(zero);
  mpz_clear(one);
  mpz_clear(three);
}

/*
 * Real algebraic numbers. sqrt(2) is the root of x^2 - 2 in (1, 2).
 * set_algebraic_lp refines the isolating interval in place, so the same
 * number is used twice on purpose: the second call must still be correct.
 */
static void test_algebraic(void) {
  printf("\n--- set_algebraic_lp ---\n");

  int coeff[3] = { -2, 0, 1 };            /* x^2 - 2 */
  lp_upolynomial_t* f = lp_upolynomial_construct_from_int(lp_Z, 2, coeff);
  lp_dyadic_interval_t I;
  lp_dyadic_interval_construct_from_int(&I, 1, 1, 2, 1);   /* (1, 2) */
  lp_algebraic_number_t sqrt2;
  lp_algebraic_number_construct(&sqrt2, f, &I);

  tra_itype_t x = lib->alloc();
  tra_itype_t sq = lib->alloc();
  mpfr_t two;
  mpfr_init2(two, 64);
  mpfr_set_ui(two, 2, MPFR_RNDN);

  lib->set_algebraic_lp(x, &sqrt2, 64);
  check("sqrt(2) at 64 bits meets the request", lib->get_precision(x) >= 64);
  lib->mul(sq, x, x, 64);
  check("its square encloses 2", !lib->avoids_mpfr(sq, two));

  // Same number again, now refined further: the enclosure must tighten, and
  // must still be an enclosure -- i.e. sit inside the previous one.
  tra_itype_t x2 = lib->alloc();
  lib->set_algebraic_lp(x2, &sqrt2, 200);
  check("sqrt(2) at 200 bits meets the request", lib->get_precision(x2) >= 200);
  check("...and refines the 64-bit enclosure", encl_inside(x2, x));

  // Exactness is unattainable for an irrational: the result must be finite
  // precision, and still a valid enclosure.
  lib->set_algebraic_lp(x, &sqrt2, NO_PREC);
  check("sqrt(2) at NO_PREC is finite precision", lib->get_precision(x) != INF_PREC);
  lib->mul(sq, x, x, 200);
  check("...and its square still encloses 2", !lib->avoids_mpfr(sq, two));

  lib->free(x);
  lib->free(x2);
  lib->free(sq);
  mpfr_clear(two);
  lp_algebraic_number_destruct(&sqrt2);
  lp_dyadic_interval_destruct(&I);
}

static void test_transcendental(void) {
  printf("\n--- pi / sin / cos / exp ---\n");

  tra_itype_t x = lib->alloc();
  tra_itype_t y = lib->alloc();
  tra_itype_t z = lib->alloc();
  mpz_t zero;
  mpz_init_set_ui(zero, 0);

  lib->const_pi(x, 100);
  check("pi at 100 bits meets the request", lib->get_precision(x) >= 100);
  check("pi does not contain zero", !lib->zero_in(x));

  lib->set_mpz(y, zero);
  lib->sin(z, y, 100);
  check("sin(0) contains zero", lib->zero_in(z));
  lib->sin(z, x, 100);
  check("sin(pi) contains zero", lib->zero_in(z));

  // cos, the companion of sin needed by the Taylor coefficients of sine.
  lib->cos(z, y, 100);
  check("cos(0) meets the request", lib->get_precision(z) >= 100);
  check("cos(0) does not contain zero", !lib->zero_in(z));
  check("cos(0) is positive", lib->is_nonnegative(z));
  lib->cos(z, x, 100);
  check("cos(pi) does not contain zero", !lib->zero_in(z));
  check("cos(pi) is negative", lib->is_nonpositive(z));

  // The Pythagorean identity at a point that is neither 0 nor pi: sin^2 + cos^2 = 1.
  {
    mpz_t num, den;
    mpz_init_set_ui(num, 3);
    mpz_init_set_ui(den, 8); // 3/8, dyadic hence exact, as the centres will be
    tra_itype_t c = lib->alloc();
    tra_itype_t s2 = lib->alloc();
    tra_itype_t c2 = lib->alloc();
    mpfr_t one;
    mpfr_init2(one, 64);
    mpfr_set_ui(one, 1, MPFR_RNDN);

    lib->set_mpz_div(c, num, den, 100);
    lib->sin(s2, c, 200);
    lib->cos(c2, c, 200);
    check("sin(3/8) meets the request", lib->get_precision(s2) >= 200);
    check("cos(3/8) meets the request", lib->get_precision(c2) >= 200);
    lib->mul(s2, s2, s2, 200);
    lib->mul(c2, c2, c2, 200);
    lib->add(s2, s2, c2, 200);
    check("sin^2(3/8) + cos^2(3/8) contains 1", !lib->avoids_mpfr(s2, one));

    mpfr_clear(one);
    lib->free(c);
    lib->free(s2);
    lib->free(c2);
    mpz_clear(num);
    mpz_clear(den);
  }

  // NO_PREC must be accepted, as it is everywhere else in the interface.
  lib->cos(z, y, NO_PREC);
  check("cos(0) at NO_PREC does not contain zero", !lib->zero_in(z));

  lib->exp(z, y, 100);
  check("exp(0) meets the request", lib->get_precision(z) >= 100);
  check("exp(0) does not contain zero", !lib->zero_in(z));

  lib->free(x);
  lib->free(y);
  lib->free(z);
  mpz_clear(zero);
}

/*
 * Join: soundness (the result contains the union), exactness when a copy
 * suffices, the precision contract, commutativity, sign information and
 * in-place aliasing.
 */
// contains_interval_mpfr on the box [lo, hi], given as doubles for readability.
static void check_contains(const char* what, tra_itype_t x, double lo, double hi, bool expected) {
  mpfr_t l, h;
  mpfr_inits2(ENCL_PREC, l, h, (mpfr_ptr) 0);
  mpfr_set_d(l, lo, MPFR_RNDN);
  mpfr_set_d(h, hi, MPFR_RNDN);
  check(what, lib->contains_interval_mpfr(x, l, h) == expected);
  mpfr_clears(l, h, (mpfr_ptr) 0);
}

/*
 * contains_interval_mpfr. Accepting a box that is not inside the element would let
 * tra_default_get_conflict emit an invalid clause, so false positives are the thing
 * to guard against; a false negative only costs conflict strength.
 */
static void test_containment(void) {
  printf("\n--- containment ---\n");

  tra_itype_t a = lib->alloc();
  tra_itype_t b = lib->alloc();
  tra_itype_t c = lib->alloc();

  // c is exactly [1, 2]
  lib->set_z(a, 1);
  lib->set_z(b, 2);
  lib->join(c, a, b, 100);
  check_contains("[1,2] is in [1,2]", c, 1.0, 2.0, true);
  check_contains("[1.5,1.5] is in [1,2]", c, 1.5, 1.5, true);
  check_contains("[1.5,1.75] is in [1,2]", c, 1.5, 1.75, true);
  check_contains("[0.5,2] is not in [1,2]", c, 0.5, 2.0, false);
  check_contains("[1,3] is not in [1,2]", c, 1.0, 3.0, false);
  check_contains("[0,0] is not in [1,2]", c, 0.0, 0.0, false);

  // A point contains only itself
  lib->set_z(a, 7);
  check_contains("[7,7] is in {7}", a, 7.0, 7.0, true);
  check_contains("[6,7] is not in {7}", a, 6.0, 7.0, false);

  // R contains every box, including the unbounded one
  lib->set_R(a);
  check_contains("[-5,5] is in R", a, -5.0, 5.0, true);
  mpfr_t ninf, pinf;
  mpfr_inits2(ENCL_PREC, ninf, pinf, (mpfr_ptr) 0);
  mpfr_set_inf(ninf, -1);
  mpfr_set_inf(pinf, 1);
  check("[-inf,+inf] is in R", lib->contains_interval_mpfr(a, ninf, pinf));
  lib->set_z(b, 0);
  check("[-inf,+inf] is not in {0}", !lib->contains_interval_mpfr(b, ninf, pinf));
  mpfr_clears(ninf, pinf, (mpfr_ptr) 0);

  lib->free(a);
  lib->free(b);
  lib->free(c);
}

/*
 * The product must reject a box as soon as ANY component rejects it, since it denotes
 * the intersection. join(1, -1) is the sharp case: the arb component is the whole ball
 * [-1,1], but the sign component is {positive} union {negative}, so the product denotes
 * [-1,1] minus {0} and must refuse every box straddling zero. The arb component alone
 * accepts [-1,1], so this also witnesses that the product is strictly stronger.
 */
static void test_product_containment(void) {
  printf("\n--- containment, intersection of components ---\n");

  tra_itype_t a = lib->alloc();
  tra_itype_t b = lib->alloc();
  tra_itype_t c = lib->alloc();

  lib->set_z(a, 1);
  lib->set_z(b, -1);
  lib->join(c, a, b, 100);

  check_contains("[-1,1] is not in [-1,1]\\{0}", c, -1.0, 1.0, false);
  check_contains("[-0.5,0.5] is not in [-1,1]\\{0}", c, -0.5, 0.5, false);
  check_contains("[0,1] is not in [-1,1]\\{0}", c, 0.0, 1.0, false);
  check_contains("[0,0] is not in [-1,1]\\{0}", c, 0.0, 0.0, false);
  check_contains("[0.5,1] is in [-1,1]\\{0}", c, 0.5, 1.0, true);
  check_contains("[-1,-0.5] is in [-1,1]\\{0}", c, -1.0, -0.5, true);
  check_contains("[0.5,2] is not in [-1,1]\\{0}", c, 0.5, 2.0, false);

  lib->free(a);
  lib->free(b);
  lib->free(c);
}

static void test_lattice(void) {
  printf("\n--- join ---\n");

  tra_itype_t a = lib->alloc();
  tra_itype_t b = lib->alloc();
  tra_itype_t c = lib->alloc();
  tra_itype_t d = lib->alloc();
  mpfr_t f;
  mpfr_init2(f, 64);
  mpz_t one, three;
  mpz_init_set_ui(one, 1);
  mpz_init_set_ui(three, 3);

  // join of a point with itself is that point
  lib->set_z(a, 1);
  lib->join(c, a, a, 100);
  check("join(1, 1) is exact", lib->get_precision(c) == INF_PREC);
  mpfr_set_ui(f, 1, MPFR_RNDN);
  check("join(1, 1) contains 1", !lib->avoids_mpfr(c, f));

  // join of two points contains their hull
  lib->set_z(b, 3);
  lib->join(c, a, b, 100);
  check("join(1, 3) contains 1", !lib->avoids_mpfr(c, f));
  mpfr_set_ui(f, 3, MPFR_RNDN);
  check("join(1, 3) contains 3", !lib->avoids_mpfr(c, f));
  mpfr_set_ui(f, 2, MPFR_RNDN);
  check("join(1, 3) contains 2", !lib->avoids_mpfr(c, f));
  check("join(1, 3) does not contain 0", !lib->zero_in(c));
  check("join(1, 3) is nonnegative", lib->is_nonnegative(c));
  check("join(1, 3) is bounded", lib->is_bounded(c));
  lib->join(d, b, a, 100);
  check("join is commutative", encl_inside(c, d) && encl_inside(d, c));

  // join with R is R
  lib->set_R(d);
  lib->join(c, a, d, 100);
  check("join(1, R) is unbounded", !lib->is_bounded(c));

  // precision: joining enclosures of 1/3 keeps what the hull allows
  lib->set_mpz_div(a, one, three, 200);
  lib->join(c, a, a, 100);
  check("join(x, x) keeps the precision of x", lib->get_precision(c) >= 200);
  lib->set_mpz_div(b, one, three, 150);
  lib->join(c, a, b, 100);
  check("join of two 1/3 enclosures meets 100 bits", lib->get_precision(c) >= 100);
  check("join of two 1/3 enclosures contains both", encl_inside(a, c) && encl_inside(b, c));

  // in-place: a := join(a, b)
  lib->set_z(a, -1);
  lib->set_z(b, 3);
  lib->join(a, a, b, 100);
  mpfr_set_si(f, -1, MPFR_RNDN);
  check("in-place join(-1, 3) contains -1", !lib->avoids_mpfr(a, f));
  mpfr_set_ui(f, 3, MPFR_RNDN);
  check("in-place join(-1, 3) contains 3", !lib->avoids_mpfr(a, f));
  check("in-place join(-1, 3) has no definite sign", !lib->is_nonnegative(a) && !lib->is_nonpositive(a));

  // a point on the boundary of a sign class must not be lost to rounding
  lib->set_z(a, 0);
  lib->set_z(b, 2);
  lib->join(c, a, b, 100);
  check("join(0, 2) contains 0", lib->zero_in(c));
  check("join(0, 2) is nonnegative", lib->is_nonnegative(c));
  mpfr_set_d(f, -0.5, MPFR_RNDN);
  check("join(0, 2) avoids -1/2", lib->avoids_mpfr(c, f));
  lib->set_z(b, -2);
  lib->join(c, a, b, 100);
  check("join(0, -2) is nonpositive", lib->is_nonpositive(c));

  lib->free(a);
  lib->free(b);
  lib->free(c);
  lib->free(d);
  mpfr_clear(f);
  mpz_clear(one);
  mpz_clear(three);
}

int main(void) {
  lib = &tra_arb_ilib;
  failed = 0;

  test_exact();
  test_precision_requests();
  test_queries();
  test_algebraic();
  test_transcendental();
  test_lattice();
  test_containment();

  // The plugin's actual configuration: reduced product of the ARB and sign domains
  printf("\n=== reduced product of arb and sign ===\n");
  static tra_ilib_t* const components[] = { &tra_arb_ilib, &tra_sign_ilib };
  lib = tra_init_product_domain(components, 2);
  test_lattice();
  test_containment();
  test_product_containment();

  printf("\n%s\n", failed == 0 ? "All tests passed" : "SOME TESTS FAILED");
  return failed == 0 ? 0 : 1;
}

#else /* HAVE_MCSAT */

int main(void) {
  printf("Compiled without mcsat support: nothing to test\n");
  return 0;
}

#endif /* HAVE_MCSAT */
