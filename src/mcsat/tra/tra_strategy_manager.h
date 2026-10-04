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
 * tra_strategy_manager.h
 *
 * Interface (tra_strategy_manager_t) of the strategy managers. A manager chooses the profiles of
 * the function plugins (tra_func_profile_t in tra_func_plugin.h). The TRA plugin owns one
 * manager, chosen in tra_config.h. A manager reads a function plugin fp only through fp->name,
 * fp->strategies, fp->num_strategies and fp->profile; it changes fp->profile only through
 * tra_func_set_strategy_profile; it reads the TRA plugin only through the accessors of
 * tra_func_plugin.h. It keeps no term_t or variable_t across calls.
 * A manager cannot affect soundness: every strategy and every default conflict is sound. It can
 * affect progress: a term that only a disabled strategy explains stays unexplained.
 */

#ifndef MCSAT_TRA_STRATEGY_MANAGER_H_
#define MCSAT_TRA_STRATEGY_MANAGER_H_

#include <stdint.h>

#include "mcsat/mcsat_types.h"
#include "mcsat/plugin.h"

#include "mcsat/tra/tra_func_plugin.h"

typedef struct tra_strategy_manager_s {
  /**
   * Called:   after the TRA plugin has processed an event of kind MCSAT_SOLVER_RESTART or
   *           MCSAT_DELTA_BUMP. A delta bump gives MCSAT_DELTA_BUMP
   *           (delta already doubled), then MCSAT_SOLVER_RESTART.
   * Requires: trail is at the base level (the solver has backtracked).
   * Modifies: self, and the profiles of the function plugins passed to its allocator.
   */
  void (*event_notify)(struct tra_strategy_manager_s* self, plugin_notify_kind_t kind,
                       const mcsat_trail_t* trail);

  /**
   * Called:   once, when the TRA plugin is destructed, before the function plugins.
   * Ensures:  the memory of self, and of what self owns, is released.
   */
  void (*destruct)(struct tra_strategy_manager_s* self);
} tra_strategy_manager_t;

/**
 * The allocator of a strategy manager.
 * Called:   once, at the end of the construction of the TRA plugin tra; the options and the
 *           tracer are not available yet.
 * Requires: fps[0], ..., fps[n-1] are the function plugins of tra, with empty profiles; they
 *           outlive the result.
 * Modifies: the profiles of fps[0], ..., fps[n-1].
 * Ensures:  result is a new manager.
 */
typedef tra_strategy_manager_t* (*tra_strategy_manager_allocator_t)(const struct tra_plugin_s* tra,
                                                                    tra_func_plugin_t* const* fps, uint32_t n);

#endif /* MCSAT_TRA_STRATEGY_MANAGER_H_ */
