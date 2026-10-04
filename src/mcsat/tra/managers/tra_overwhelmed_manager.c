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

#include "mcsat/tra/managers/tra_overwhelmed_manager.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "mcsat/mcsat_types.h"
#include "mcsat/plugin.h"
#include "utils/memalloc.h"

#include "mcsat/tra/tra_func_plugin.h"
#include "mcsat/tra/tra_strategy_manager.h"

typedef struct {
  tra_strategy_manager_t base;

  // The function plugins passed to the allocator, and their number
  tra_func_plugin_t* const* fps;
  uint32_t n;

  // Invariant: num_events = min(c, TRA_OVERWHELMED_EVENTS + 1), where c is the number of calls of
  // event_notify so far
  uint32_t num_events;
} tra_overwhelmed_manager_t;

// Sets the profile of every plugin to its strategies that are initially enabled, or to all of
// them if all holds, in the order of its table
static void tra_overwhelmed_set_profiles(tra_overwhelmed_manager_t* m, bool all) {
  for (uint32_t i = 0; i < m->n; i++) {
    tra_func_plugin_t* fp = m->fps[i];
    // A plugin without strategies keeps its empty profile (and a VLA must not be empty)
    if (fp->num_strategies == 0) continue;
    uint32_t order[fp->num_strategies], size = 0;
    for (uint32_t s = 0; s < fp->num_strategies; s++) {
      if (all || fp->strategies[s].initially_enabled) order[size++] = s;
    }
    bool ok = tra_func_set_strategy_profile(fp, order, size);
    assert(ok);
    (void) ok;
  }
}

static void tra_overwhelmed_event_notify(tra_strategy_manager_t* self, plugin_notify_kind_t kind,
                                         const mcsat_trail_t* trail) {
  tra_overwhelmed_manager_t* m = (tra_overwhelmed_manager_t*) self;
  assert(kind == MCSAT_SOLVER_RESTART || kind == MCSAT_DELTA_BUMP);
  if (m->num_events > TRA_OVERWHELMED_EVENTS) return;
  if (++m->num_events > TRA_OVERWHELMED_EVENTS) tra_overwhelmed_set_profiles(m, true);
}

static void tra_overwhelmed_destruct(tra_strategy_manager_t* self) {
  safe_free(self);
}

tra_strategy_manager_t* tra_overwhelmed_manager_allocator(const struct tra_plugin_s* tra,
                                                          tra_func_plugin_t* const* fps, uint32_t n) {
  tra_overwhelmed_manager_t* m = (tra_overwhelmed_manager_t*) safe_malloc(sizeof(tra_overwhelmed_manager_t));
  m->base.event_notify = tra_overwhelmed_event_notify;
  m->base.destruct = tra_overwhelmed_destruct;
  m->fps = fps;
  m->n = n;
  m->num_events = 0;
  tra_overwhelmed_set_profiles(m, false);
  return (tra_strategy_manager_t*) m;
}
