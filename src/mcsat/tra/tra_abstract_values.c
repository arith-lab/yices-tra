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

#include "mcsat/tra/tra_abstract_values.h"

#include <assert.h>

#include <poly/rational.h>
#include <poly/value.h>

#include "mcsat/value.h"
#include "terms/rationals.h"

bool tra_interval_around_value(const tra_ilib_t* lib, const mcsat_value_t* val, tra_prec_t precision, tra_itype_t out) {
  switch (val->type) {

  case VALUE_RATIONAL: {
    // Exact rational q = num/den: build the point interval.
    mpq_t q;
    mpq_init(q);
    q_get_mpq(&val->q, q);
    lib->set_mpz_div(out, mpq_numref(q), mpq_denref(q), precision);
    mpq_clear(q);
    return true;
  }

  case VALUE_LIBPOLY: {
    const lp_value_t* v = &val->lp_value;

    if (lp_value_is_rational(v)) {
      // Same point interval as in the VALUE_RATIONAL case
      lp_rational_t q;
      lp_rational_construct(&q);
      lp_value_get_rational(v, &q);
      lib->set_mpz_div(out, mpq_numref(&q), mpq_denref(&q), precision);
      lp_rational_destruct(&q);
      return true;
    }

    // Irrational algebraic number: isolating dyadic interval
    assert(v->type == LP_VALUE_ALGEBRAIC);
    lib->set_algebraic_lp(out, &v->value.a, precision);
    return true;
  }

  default:
    return false;
  }
}

int tra_get_atom_truth(const tra_ilib_t* lib, tra_itype_t r, bool eq) {
  bool nonneg = lib->is_nonnegative(r);
  bool nonpos = lib->is_nonpositive(r);
  bool zero_in = lib->zero_in(r);
  // By the soundness clauses of the three, nonneg && nonpos && !zero_in implies γ(r) = ∅
  assert(zero_in || !(nonneg && nonpos));
  if (eq ? nonneg && nonpos : nonneg) return 1;
  return !zero_in && (eq || nonpos) ? -1 : 0;
}

tra_prec_t tra_mpfr_magnitude_bits(const mpfr_t x) {
  assert(!mpfr_nan_p(x));
  if (mpfr_zero_p(x) || mpfr_inf_p(x)) return 0; // mpfr_get_exp is undefined on both
  // mpfr's convention is x = m * 2^e with 1/2 <= |m| < 1, so |x| < 2^e and the
  // bound is e. Testing e > 1 would report 0 for x in (1,2), claiming |x| <= 1.
  mpfr_exp_t e = mpfr_get_exp(x);
  return (e > 0) ? (tra_prec_t) e : 0;
}

/* Initial and maximal mpfr precision tried by tra_interval_to_mpfr */
#define TRA_MPFR_ENCLOSURE_PREC_INIT 64
#define TRA_MPFR_ENCLOSURE_PREC_MAX  (1L << 20)

bool tra_interval_to_mpfr(const tra_ilib_t* lib, tra_itype_t J, tra_prec_t precision, mpfr_t lo, mpfr_t hi) {
  bool finite = false;
  bool point = lib->get_precision(J) == INF_PREC;

  for (mpfr_prec_t p = TRA_MPFR_ENCLOSURE_PREC_INIT; ; p *= 2) {
    mpfr_set_prec(lo, p);
    mpfr_set_prec(hi, p);

    lib->get_interval_overapproximation_mpfr(lo, hi, J);
    finite = mpfr_number_p(lo) && mpfr_number_p(hi);
    if (!finite) break; // J is unbounded: no finite enclosure at any precision

    // J is inside [lo, hi] already, so the reverse inclusion means J = [lo, hi]:
    // nothing was rounded away and a wider precision cannot improve the enclosure.
    if (lib->contains_interval_mpfr(J, lo, hi) || p >= TRA_MPFR_ENCLOSURE_PREC_MAX) break;

    // A point that is not a dyadic rational cannot equal [lo, hi]
    // so we stop at the requested width
    if (point && precision != NO_PREC) {
      mpfr_t w;
      mpfr_init2(w, TRA_MPFR_ENCLOSURE_PREC_INIT);
      mpfr_sub(w, hi, lo, MPFR_RNDU);
      bool narrow = mpfr_zero_p(w) || mpfr_get_exp(w) <= -precision; // w < 2^exp(w)
      mpfr_clear(w);
      if (narrow) break;
    }
  }

  return finite;
}

bool tra_enclose_unop_at(const tra_ilib_t* lib, void (*op)(tra_itype_t, tra_itype_t, tra_prec_t),
                         const mpq_t c, tra_prec_t precision, tra_prec_t extract_precision,
                         mpfr_t lo, mpfr_t hi) {
  tra_itype_t I_c = lib->alloc();
  tra_itype_t I_r = lib->alloc();
  lib->set_mpz_div(I_c, mpq_numref(c), mpq_denref(c), NO_PREC);
  op(I_r, I_c, precision);
  bool bounded = tra_interval_to_mpfr(lib, I_r, extract_precision, lo, hi);
  lib->free(I_c);
  lib->free(I_r);
  return bounded;
}
