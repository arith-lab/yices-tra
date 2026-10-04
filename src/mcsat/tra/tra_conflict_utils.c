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

#include "mcsat/tra/tra_conflict_utils.h"

#include <assert.h>
#include <stdint.h>

#include <poly/algebraic_number.h>
#include <poly/upolynomial.h>
#include <poly/value.h>

#include "api/yices_api_lock_free.h"
#include "mcsat/plugin.h"
#include "mcsat/trail.h"
#include "mcsat/value.h"
#include "mcsat/variable_db.h"
#include "terms/term_manager.h"
#include "terms/terms.h"
#include "utils/memalloc.h"

#include "mcsat/tra/utils/tra_exact_values.h"

/* The conflict literals created by the methods below are built starting from mpfr 
 * approximations of tra_itype_t. Since Yices only support rationals in mpq encoding, 
 * the conversion from mpfr (mpfr_get_q) might a priori lead to overflows.  
 * TRA_CONFLICT_PREC_LIMIT (tra_conflict_utils.h) caps the precision of mpfr so that the conversion
 * is sound and not too expensive. (TRA_CONFLICT_PREC_INIT is instead the initial precision.)
 *
 * We measured that converting an mpfr of precision TRA_CONFLICT_PREC_LIMIT costs ~0.2 ms and
 * yields a rational of ~1 MiB. Doubling it doubles the size of the constants and roughly 
 * quadruples the cost of the exact arithmetic later performed on them.
 */

#define TRA_CONFLICT_PREC_INIT  128

bool tra_mpfr_to_q_is_safe(const mpfr_t x) {
  if (!mpfr_number_p(x)) return false;
  if (mpfr_zero_p(x)) return true;
  mpfr_exp_t e = mpfr_get_exp(x);
  return -TRA_CONFLICT_PREC_LIMIT <= e && e <= TRA_CONFLICT_PREC_LIMIT;
}

term_t tra_conflict_escape_literal(const plugin_context_t* ctx, const tra_ilib_t* lib, term_t t,
                                   tra_itype_t X, bool* below) {
  mcsat_value_t v;
  if (!term_evaluate(ctx, t, &v)) return NULL_TERM;

  tra_tv_position_t position = lib->trail_value_position(X, &v);
  if (position != TV_BELOW && position != TV_ABOVE) {
    mcsat_value_destruct(&v);
    return NULL_TERM;
  }
  bool is_below = (position == TV_BELOW);
  if (below != NULL) *below = is_below;

  mpfr_t lo, hi;
  mpq_t q;
  mpfr_inits2(TRA_CONFLICT_PREC_INIT, lo, hi, (mpfr_ptr) 0);
  mpq_init(q);

  // c is the endpoint of an enclosure of X on the side of v. It converges to the
  // endpoint of X as the precision grows, and v lies strictly beyond that one.
  term_t literal = NULL_TERM;
  for (mpfr_prec_t prec = TRA_CONFLICT_PREC_INIT; literal == NULL_TERM && prec <= TRA_CONFLICT_PREC_LIMIT; prec *= 2) {
    mpfr_set_prec(lo, prec);
    mpfr_set_prec(hi, prec);
    lib->get_interval_overapproximation_mpfr(lo, hi, X);
    mpfr_ptr c = is_below ? lo : hi;
    if (!tra_mpfr_to_q_is_safe(c)) break;

    mpfr_get_q(q, c);
    int cmp = tra_value_cmp_mpq(&v, q);
    if (is_below ? cmp > 0 : cmp < 0) continue;

    // The non-strict literal also excludes c, so it needs c outside X. It is the only
    // choice when v = c, e.g. v = 0 and X = (0, 1].
    bool strict = !lib->avoids_mpfr(X, c);
    if (strict && cmp == 0) continue;

    term_t bound = _o_yices_mpq(q);
    if (is_below) literal = strict ? _o_yices_arith_lt_atom(t, bound) : _o_yices_arith_leq_atom(t, bound);
    else          literal = strict ? _o_yices_arith_gt_atom(t, bound) : _o_yices_arith_geq_atom(t, bound);
  }

  mpq_clear(q);
  mpfr_clears(lo, hi, (mpfr_ptr) 0);
  mcsat_value_destruct(&v);
  return literal;
}

// Bisection steps spent pushing a certified endpoint outwards.
#define TRA_CONFLICT_WIDEN_STEPS 32

// Move the endpoint(s) the search is pushing to c.
static inline void tra_box_set(mpfr_t lo, mpfr_t hi, bool move_lo, bool degenerate, const mpfr_t c) {
  if (degenerate) {
    mpfr_set(lo, c, MPFR_RNDN);
    mpfr_set(hi, c, MPFR_RNDN);
  } else if (move_lo) {
    mpfr_set(lo, c, MPFR_RNDN);
  } else {
    mpfr_set(hi, c, MPFR_RNDN);
  }
}

/*
 * Push one endpoint of the certified box [lo,hi] out towards 'target', as far as the
 * domain still certifies. Bisection is valid because containment is downward closed:
 * shrinking a certified box keeps it certified, whatever shape I has.
 */
static void tra_widen_endpoint(const tra_ilib_t* lib, tra_itype_t I, mpfr_t lo, mpfr_t hi,
                               bool move_lo, bool degenerate, const mpfr_t target) {
  // An infinite or oversized target is left alone: we do not invent a bound here.
  if (!mpfr_number_p(target) || !tra_mpfr_to_q_is_safe(target)) return;

  mpfr_t good, bad, mid;
  mpfr_inits2(mpfr_get_prec(lo), good, bad, mid, (mpfr_ptr) 0);
  mpfr_set(good, move_lo ? lo : hi, MPFR_RNDN);
  mpfr_set(bad, target, MPFR_RNDN);

  // I is inside its own enclosure, so reaching the target is the widest possible answer.
  tra_box_set(lo, hi, move_lo, degenerate, target);
  if (lib->contains_interval_mpfr(I, lo, hi)) goto done;

  for (int k = 0; k < TRA_CONFLICT_WIDEN_STEPS; k++) {
    mpfr_add(mid, good, bad, MPFR_RNDN);
    mpfr_div_2ui(mid, mid, 1, MPFR_RNDN);
    if (mpfr_equal_p(mid, good) || mpfr_equal_p(mid, bad)) break; // grid exhausted
    tra_box_set(lo, hi, move_lo, degenerate, mid);
    mpfr_set(lib->contains_interval_mpfr(I, lo, hi) ? good : bad, mid, MPFR_RNDN);
  }
  tra_box_set(lo, hi, move_lo, degenerate, good);

done:
  mpfr_clears(good, bad, mid, (mpfr_ptr) 0);
}

/*
 * A box [lo,hi] holding the trail value v and certified inside I, as wide as the domain
 * will confirm; false if no box is certified. W is scratch owned by the caller.
 * With whole_box false only the endpoint on the escape side is emitted, so the box is
 * kept degenerate and only that point is certified.
 * The box is seeded from the enclosure of v itself, which brackets v by construction, so
 * no exact comparison against v is needed here even when v is an algebraic number.
 * Only a box the domain accepted is ever returned: the widening is a heuristic for width
 * and cannot affect soundness.
 */
static bool tra_conflict_box(const tra_ilib_t* lib, tra_itype_t I, tra_itype_t W,
                             const mcsat_value_t* v, bool whole_box, bool below,
                             mpfr_t lo, mpfr_t hi) {
  bool seeded = false;
  for (mpfr_prec_t prec = TRA_CONFLICT_PREC_INIT; prec <= TRA_CONFLICT_PREC_LIMIT; prec *= 2) {
    mpfr_set_prec(lo, prec);
    mpfr_set_prec(hi, prec);
    if (!tra_interval_around_value(lib, v, (tra_prec_t) prec, W)) break;
    lib->get_interval_overapproximation_mpfr(lo, hi, W);
    if (!tra_mpfr_to_q_is_safe(lo) || !tra_mpfr_to_q_is_safe(hi)) break;
    if (!whole_box) mpfr_set(below ? hi : lo, below ? lo : hi, MPFR_RNDN);

    if (lib->contains_interval_mpfr(I, lo, hi)) { seeded = true; break; }
  }
  if (!seeded) return false;

  mpfr_t A, B;
  mpfr_inits2(mpfr_get_prec(lo), A, B, (mpfr_ptr) 0);
  lib->get_interval_overapproximation_mpfr(A, B, I);
  if (whole_box || below) tra_widen_endpoint(lib, I, lo, hi, true, !whole_box, A);
  if (whole_box || !below) tra_widen_endpoint(lib, I, lo, hi, false, !whole_box, B);
  mpfr_clears(A, B, (mpfr_ptr) 0);
  return true;
}

void tra_conflict_push_bounds(term_t x, const mcsat_value_t* x_val, const mpq_t lo, const mpq_t hi,
                              ivector_t* conflict) {
  if (tra_value_cmp_mpq(x_val, lo) <= 0 || tra_value_cmp_mpq(x_val, hi) >= 0) return;

  term_t lo_literal = _o_yices_arith_geq_atom(x, _o_yices_mpq(lo));
  term_t hi_literal = _o_yices_arith_leq_atom(x, _o_yices_mpq(hi));
  if (lo_literal == false_term || hi_literal == false_term) return;
  if (lo_literal != true_term) ivector_push(conflict, lo_literal);
  if (hi_literal != true_term) ivector_push(conflict, hi_literal);
}

term_t tra_conflict_cutting_literal(const plugin_context_t* ctx, term_t t, term_t bound, bool above) {
  if (bound == NULL_TERM) return NULL_TERM;
  int cmp = tra_value_cmp_terms(ctx, t, bound);
  if (above ? cmp <= 0 : cmp >= 0) return NULL_TERM;

  term_t literal = above ? _o_yices_arith_gt_atom(t, bound) : _o_yices_arith_lt_atom(t, bound);
  return (literal == true_term || literal == false_term) ? NULL_TERM : literal;
}

bool tra_conflict_escape_endpoint(const mcsat_value_t* t_val, const mpfr_t lo, const mpfr_t hi, mpq_t H) {
  mpfr_get_q(H, hi);
  if (tra_value_cmp_mpq(t_val, H) > 0) return true;
  mpfr_get_q(H, lo);
  return false;
}

/*
 * Classical interval-to-interval conflicts.
 * If the function is increasing, only "half-intervals" are needed.
 */
static bool tra_default_get_conflict(const tra_func_plugin_t* fp, term_t t,
                                     ivector_t* conflict, bool increasing) {
  if (!tra_application_is_of_the_plugin(fp, t)) return false;

  assert(conflict->size == 0);
  
  const plugin_context_t* ctx = tra_get_context(fp->tra);
  const tra_ilib_t* lib = fp->abstract_domain;
  term_table_t* terms = ctx->terms;

  // The arguments of an application sit at 1..arity; a nullary symbol is the term itself.
  uint32_t arity = (term_kind(terms, t) == APP_TERM) ? composite_term_arity(terms, t) - 1 : 0;

  tra_itype_t J = tra_get_abstraction(fp->tra, t);
  bool below;
  term_t literal = (J == NULL) ? NULL_TERM : tra_conflict_escape_literal(ctx, lib, t, J, &below);
  if (literal == NULL_TERM) return false;

  bool conflict_generated = false;
  mpfr_t lo, hi;
  mpq_t lo_q, hi_q;
  mpfr_inits2(TRA_CONFLICT_PREC_INIT, lo, hi, (mpfr_ptr) 0);
  mpq_inits(lo_q, hi_q, NULL);
  tra_itype_t W = lib->alloc();

  for (uint32_t i = 1; i <= arity; i++) {
    term_t x = composite_term_arg(terms, t, i);
    tra_itype_t I = tra_get_argument_abstraction(fp->tra, t, i - 1);
    mcsat_value_t v;
    bool has_value = term_evaluate(ctx, x, &v);

    // A monotone f only uses the endpoint on the escape side, so only that point has
    // to lie in I: below J, x >= l gives f(x) >= f(l) as soon as f(l) is in J.
    bool found = I != NULL && has_value && lib->trail_value_position(I, &v) == TV_INSIDE
              && tra_conflict_box(lib, I, W, &v, !increasing, below, lo, hi);
    if (has_value) mcsat_value_destruct(&v);
    if (!found) {
      ivector_reset(conflict);
      goto done;
    }
    assert(mpfr_cmp(lo, hi) <= 0 && lib->contains_interval_mpfr(I, lo, hi));

    mpfr_get_q(lo_q, lo);
    mpfr_get_q(hi_q, hi);
    if (!increasing || below) ivector_push(conflict, _o_yices_arith_geq_atom(x, _o_yices_mpq(lo_q)));
    if (!increasing || !below) ivector_push(conflict, _o_yices_arith_leq_atom(x, _o_yices_mpq(hi_q)));
  }
  // Last: the order of the literals steers the conflict analysis
  ivector_push(conflict, literal);
  conflict_generated = true;

done:
  lib->free(W);
  mpq_clears(lo_q, hi_q, NULL);
  mpfr_clears(lo, hi, (mpfr_ptr) 0);

  assert(!conflict_generated  || conflict->size > 0);
  return conflict_generated;
}

bool tra_func_default_get_conflict(const tra_func_plugin_t* fp, term_t t, ivector_t* conflict) {
  return tra_default_get_conflict(fp, t, conflict, false);
}

bool tra_func_increasing_default_get_conflict(const tra_func_plugin_t* fp, term_t t, ivector_t* conflict) {
  return tra_default_get_conflict(fp, t, conflict, true);
}

term_t tra_conflict_boolean_literal(const plugin_context_t* ctx, term_t c) {
  assert(is_boolean_term(ctx->terms, c));
  variable_t var = variable_db_get_variable_if_exists(ctx->var_db, unsigned_term(c));
  if (var == variable_null || !trail_has_value(ctx->trail, var)) return NULL_TERM;
  return trail_get_boolean_value(ctx->trail, var) != is_neg_term(c) ? c : opposite_term(c);
}

term_t tra_conflict_value_literal(const plugin_context_t* ctx, term_t x) {
  term_kind_t kind = term_kind(ctx->terms, x);
  assert(kind != ARITH_CONSTANT && kind != ARITH_POLY && kind != POWER_PRODUCT);
  (void) kind;
  mcsat_value_t v;
  bool ok = term_evaluate(ctx, x, &v);
  assert(ok && v.type == VALUE_LIBPOLY);
  (void) ok;

  term_t literal;
  if (lp_value_is_rational(&v.lp_value)) {
    literal = mk_eq(ctx->tm, x, mcsat_value_to_term(&v, ctx->tm));
  } else {
    assert(v.lp_value.type == LP_VALUE_ALGEBRAIC);
    const lp_algebraic_number_t* a = &v.lp_value.value.a;
    size_t deg = lp_upolynomial_degree(a->f);

    // f = Σ_i coeff[i] x^i is the polynomial a->f, as a term in x
    lp_integer_t* coeff = safe_malloc((deg + 1) * sizeof(lp_integer_t));
    term_t* powers = safe_malloc((deg + 1) * sizeof(term_t));
    for (size_t i = 0; i <= deg; i++) {
      lp_integer_construct(&coeff[i]);
      powers[i] = _o_yices_power(x, i);
    }
    lp_upolynomial_unpack(a->f, coeff);
    term_t f = _o_yices_poly_mpz(deg + 1, (const mpz_t*) coeff, powers);
    for (size_t i = 0; i <= deg; i++) lp_integer_destruct(&coeff[i]);
    safe_free(powers);
    safe_free(coeff);

    lp_algebraic_number_t* roots = safe_malloc(deg * sizeof(lp_algebraic_number_t));
    size_t n_roots = 0;
    lp_upolynomial_roots_isolate(a->f, roots, &n_roots);
    uint32_t k = 0;
    for (size_t i = 0; i < n_roots; i++) {
      if (lp_algebraic_number_cmp(&roots[i], a) < 0) k++;
      lp_algebraic_number_destruct(&roots[i]);
    }
    safe_free(roots);

    literal = mk_arith_root_atom_eq(ctx->tm, k, x, f);
  }
  mcsat_value_destruct(&v);
  return literal;
}
