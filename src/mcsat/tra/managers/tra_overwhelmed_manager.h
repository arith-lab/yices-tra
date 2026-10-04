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

#ifndef MCSAT_TRA_OVERWHELMED_MANAGER_H_
#define MCSAT_TRA_OVERWHELMED_MANAGER_H_

#include <stdint.h>

#include "mcsat/tra/tra_func_plugin.h"
#include "mcsat/tra/tra_strategy_manager.h"

/* The overwhelmed manager enables every strategy at the first event after this many events. */
#define TRA_OVERWHELMED_EVENTS 2

/**
 * The allocator of the overwhelmed manager (contract in tra_strategy_manager.h). Let c be the
 * number of calls of event_notify so far.
 * Ensures:  the profile of every function plugin lists its initially enabled strategies while
 *           c <= TRA_OVERWHELMED_EVENTS, and all its strategies once c > TRA_OVERWHELMED_EVENTS,
 *           in the order of its table.
 */
tra_strategy_manager_t* tra_overwhelmed_manager_allocator(const struct tra_plugin_s* tra,
                                                          tra_func_plugin_t* const* fps, uint32_t n);

#endif /* MCSAT_TRA_OVERWHELMED_MANAGER_H_ */
