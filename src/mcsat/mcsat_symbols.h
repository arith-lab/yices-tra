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

#ifndef MCSAT_SYMBOLS_H_
#define MCSAT_SYMBOLS_H_

#include "api/smt_logic_codes.h"
#include "terms/terms.h"

/*
 * Symbols that Yices treats as uninterpreted, but to which an MCSAT plugin
 * gives a fixed meaning: pi, sin and exp for the TRA plugin of QF_TRA.
 *
 * The frontend declares them when the logic is set, so that the input may use
 * them without declaring them, and the preprocessor must not eliminate them:
 * substituting pi away would hide it from the plugin that interprets it.
 *
 * A plugin attaches to this mechanism by adding one row to the table in
 * mcsat_symbols.c.
 */

typedef struct {
  const char* name;
  uint32_t arity;            // 0 for a constant such as pi
  const type_t* signature;   // arity+1 types: the domain, then the range
} mcsat_symbol_t;

/** Number of symbols the plugins of this logic interpret (0 if it has none) */
uint32_t mcsat_num_interpreted_symbols(smt_logic_t logic);

/** The i-th interpreted symbol of this logic, i < mcsat_num_interpreted_symbols(logic) */
const mcsat_symbol_t* mcsat_interpreted_symbol(smt_logic_t logic, uint32_t i);

/** True if t is interpreted by some MCSAT plugin: one of its symbols itself, be it
 *  a constant such as pi or a function symbol such as sin, or an application of one
 *  of them such as (sin 3).
 */
bool mcsat_is_interpreted_term(smt_logic_t logic, term_table_t* terms, term_t t);

#endif /* MCSAT_SYMBOLS_H_ */
