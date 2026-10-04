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

#ifndef TRA_PLUGIN_H_
#define TRA_PLUGIN_H_

#include <stdint.h>

#include "mcsat/mcsat_symbols.h"
#include "mcsat/mcsat_types.h"

/**
 * Ensures:  result is the number of symbols that the TRA plugin interprets, i.e. of the
 *           functions registered in tra_config.h (see mcsat/mcsat_symbols.h).
 */
uint32_t tra_num_symbols(void);

/**
 * Requires: i < tra_num_symbols().
 * Ensures:  result is the symbol of the i-th function registered in tra_config.h.
 */
const mcsat_symbol_t* tra_symbol(uint32_t i);

/**
 * Ensures:  result is a new TRA plugin, not yet constructed, whose plugin interface is set.
 */
plugin_t* tra_plugin_allocator(void);

#endif /* TRA_PLUGIN_H_ */
