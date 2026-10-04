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

#ifndef MCSAT_TRA_UTIL_TERM_EXPLORER_H_
#define MCSAT_TRA_UTIL_TERM_EXPLORER_H_

#include "yices_types.h"
#include "mcsat/preprocessor.h"

/*
 * In the contracts below, σ is the substitution that maps every purification variable of
 * the preprocessor to the term it stands for.
 */

/**
 * Ensures:  result = t if t is an ARITH_ROOT_ATOM;
 *           otherwise, result = σ^k(t) for the least k >= 0 such that σ^(k+1)(t) = σ^k(t).
 */
term_t tra_depurify_term(const preprocessor_t* preprocessor, term_t t);

/**
 * Requires: t is not an ARITH_ROOT_ATOM (the substitution may break its degree assertions).
 * Ensures:  result = σ(t).
 * Note:     for a positive UNINTERPRETED_TERM t, σ(t) is preprocessor_purification_definition(t)
 *           when t is a purification variable, and t otherwise. Unlike that function, this one
 *           accepts any term t, and replaces every purification variable that occurs in t.
 */
term_t tra_depurify_term_step(const preprocessor_t* preprocessor, term_t t);

#endif /* MCSAT_TRA_UTIL_TERM_EXPLORER_H_ */