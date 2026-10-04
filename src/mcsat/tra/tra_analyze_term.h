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

#ifndef MCSAT_TRA_ANALYZE_TERM_H_
#define MCSAT_TRA_ANALYZE_TERM_H_

#include "yices_types.h"

#include "mcsat/tra/tra_plugin_internal.h"

/**
 * Requires: t is a purified term; every term that the analysis reaches from t is analyzable
 *           (see tra_term_is_analyzable). tra_plugin_new_assertion_notify checks this for the
 *           original constraints, at assertion time.
 * Modifies: the attribute maps of tra (see tra_plugin_internal.h).
 * Ensures:  t and all its subterms are analyzed: they have an entry in every attribute map,
 *           which meets the invariant of that map. A term that was analyzed before is left
 *           unchanged.
 */
void tra_analyze_term(tra_plugin_t* tra, term_t t);

/**
 * Ensures:  result <==> the analysis handles t itself (not its subterms): a negative literal, or
 *           a Boolean term of kind CONSTANT_TERM, UNINTERPRETED_TERM, ARITH_EQ_ATOM, ARITH_GE_ATOM,
 *           ARITH_BINEQ_ATOM, OR_TERM, XOR_TERM, EQ_TERM, ITE_TERM or ITE_SPECIAL, or an arithmetic
 *           term of kind ARITH_CONSTANT, UNINTERPRETED_TERM, ARITH_RDIV, ARITH_ABS, ITE_TERM,
 *           ITE_SPECIAL, ARITH_POLY, POWER_PRODUCT, or APP_TERM of a registered function.
 *           These are exactly the terms that tra_analyze_term handles.
 */
bool tra_term_is_analyzable(tra_plugin_t* tra, term_t t);

#endif /* MCSAT_TRA_ANALYZE_TERM_H_ */
