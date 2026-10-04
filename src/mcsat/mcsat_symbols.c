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

#include <assert.h>
#include <string.h>

#include "mcsat/mcsat_symbols.h"
#include "mcsat/tra/tra_plugin.h"

/*
 * The symbols each logic's plugins interpret. A plugin attaches to the
 * mechanism by adding one row here and exposing the two accessors over its own
 * registration table.
 */
typedef struct {
  smt_logic_t logic;
  uint32_t (*num_symbols)(void);
  const mcsat_symbol_t* (*symbol)(uint32_t i);
} mcsat_theory_symbols_t;

static const mcsat_theory_symbols_t mcsat_theories[] = {
  { QF_TRA, tra_num_symbols, tra_symbol },
};

#define NUM_MCSAT_THEORIES (sizeof(mcsat_theories) / sizeof(mcsat_theories[0]))


// The row of the given logic, or NULL if it has no interpreted symbols
static const mcsat_theory_symbols_t* mcsat_get_theory(smt_logic_t logic) {
  for (uint32_t i = 0; i < NUM_MCSAT_THEORIES; i ++) {
    if (mcsat_theories[i].logic == logic) {
      return &mcsat_theories[i];
    }
  }
  return NULL;
}

uint32_t mcsat_num_interpreted_symbols(smt_logic_t logic) {
  const mcsat_theory_symbols_t* theory = mcsat_get_theory(logic);
  return theory == NULL ? 0 : theory->num_symbols();
}

const mcsat_symbol_t* mcsat_interpreted_symbol(smt_logic_t logic, uint32_t i) {
  const mcsat_theory_symbols_t* theory = mcsat_get_theory(logic);
  assert(theory != NULL && i < theory->num_symbols());
  return theory->symbol(i);
}

bool mcsat_is_interpreted_term(smt_logic_t logic, term_table_t* terms, term_t t) {
  const mcsat_theory_symbols_t* theory = mcsat_get_theory(logic);
  const char* name;

  // A symbol of another logic's plugins means nothing here: in QF_NRA, say, pi is
  // an ordinary variable
  if (theory == NULL) {
    return false;
  }

  // The symbol to look up is t itself when t is a constant such as pi, and the
  // function of the application when t is a term such as (sin 3)
  switch (term_kind(terms, t)) {
  case UNINTERPRETED_TERM:
    name = is_pos_term(t) ? term_name(terms, t) : NULL;
    break;
  case APP_TERM:
    name = term_name(terms, composite_term_arg(terms, t, 0));
    break;
  default:
    return false;
  }
  if (name == NULL) {
    return false;
  }

  // Interpreted symbols are recognized by name, as everywhere else in the plugins
  uint32_t n = theory->num_symbols();
  for (uint32_t i = 0; i < n; i ++) {
    if (strcmp(name, theory->symbol(i)->name) == 0) {
      return true;
    }
  }
  return false;
}
