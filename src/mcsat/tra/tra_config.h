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

#ifndef MCSAT_TRA_CONFIG_H_
#define MCSAT_TRA_CONFIG_H_

#include "yices_types.h"
#include "mcsat/mcsat_symbols.h"
#include "terms/types.h"

#include "mcsat/tra/abstractions/tra_arb_domain.h"
#include "mcsat/tra/abstractions/tra_product_domain.h"
#include "mcsat/tra/abstractions/tra_sign_domain.h"
#include "mcsat/tra/functions/tra_exp.h"
#include "mcsat/tra/functions/tra_pi.h"
#include "mcsat/tra/functions/tra_sin.h"
#include "mcsat/tra/managers/tra_overwhelmed_manager.h"
#include "mcsat/tra/tra_abstract_values.h"
#include "mcsat/tra/tra_func_plugin.h"
#include "mcsat/tra/tra_strategy_manager.h"

/*
 * Registration of the transcendental functions and constants, of the abstract domain, and of
 * the strategy manager of the TRA plugin. This is the only file to update when adding a
 * function, a domain or a strategy manager.
 *
 * To add a function or constant f:
 *  1. implement a function plugin for f that meets the contracts of tra_func_plugin.h
 *     (functions/tra_exp.c is an example);
 *  2. add the pair {symbol of f, allocator of the plugin} to tra_registered_functions.
 *     The frontend then declares the symbol of f (see mcsat/mcsat_symbols.c).
 *
 * To change the abstract domain, list in tra_abstract_domain_components domains that meet
 * the soundness clauses of tra_abstract_values.h, at least one of which meets the (A)
 * clauses too.
 *
 * To change the strategy manager, set tra_strategy_manager_allocator to the allocator of a
 * manager that meets the contracts of tra_strategy_manager.h (managers/tra_overwhelmed_manager.c
 * is an example).
 */

typedef struct {
  mcsat_symbol_t symbol;   // name, arity and signature, as the frontend needs them to declare it

  /**
   * Ensures:  result is a new function plugin whose name and arity are those of symbol,
   *           and whose methods and strategies meet the contracts of tra_func_plugin.h.
   */
  tra_func_plugin_t* (*tra_func_allocator)(const struct tra_plugin_s* tra);
} tra_func_registration_t;

/* Signatures of the registered functions: the domain, then the range. */
static const type_t tra_real[]         = { real_id };
static const type_t tra_real_to_real[] = { real_id, real_id };

/* Registered transcendental functions. */
static const tra_func_registration_t tra_registered_functions[] = {
  { { "pi",  0, tra_real },         tra_pi_plugin_allocator },
  { { "sin", 1, tra_real_to_real }, tra_sin_plugin_allocator },
  { { "exp", 1, tra_real_to_real }, tra_exp_plugin_allocator },
};

/* Register abstract domains in a reduced product (see tra_product_domain.h). */
static tra_ilib_t* const tra_abstract_domain_components[] = { &tra_arb_ilib, &tra_sign_ilib };

/**
 * Called:   when the TRA plugin is constructed.
 * Ensures:  result is the product of tra_abstract_domain_components.
 */
static inline tra_ilib_t* tra_abstract_domain_configure(void) {
  return tra_init_product_domain(tra_abstract_domain_components,
                             sizeof(tra_abstract_domain_components) / sizeof(tra_abstract_domain_components[0]));
}

/* The strategy manager of the TRA plugin (see tra_strategy_manager.h). */
static const tra_strategy_manager_allocator_t tra_strategy_manager_allocator = tra_overwhelmed_manager_allocator;

#define NUM_TRA_FUNCS (sizeof(tra_registered_functions) / sizeof(tra_registered_functions[0]))

#endif /* MCSAT_TRA_CONFIG_H_ */

