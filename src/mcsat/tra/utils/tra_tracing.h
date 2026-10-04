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
 * Tracing of the TRA plugin, enabled by --trace=<tag> in devel builds (compiled out under NDEBUG).
 * tra::any enables every tag below except tra::interactive. A function f prints "[+ (level) tra::f]"
 * on entry and "[- (level)]" on exit under the tag tra::f (see tra_trace_enter_function); the other
 * lines are messages in brackets.
 *
 * Methods of the MCSAT plugin (tra_plugin.c), one tag each: tra::notify, tra::lemma_notify,
 * tra::event, tra::propagate, tra::decide, tra::decide_assignment, tra::learn, tra::get_conflict,
 * tra::explain_propagation, tra::explain_evaluation, tra::simplify_conflict_literal, tra::push,
 * tra::pop, tra::gc_mark, tra::gc_sweep, tra::set_exception_handler. Under tra::notify,
 * tra::lemma_notify, tra::propagate, tra::decide, tra::decide_assignment and tra::learn, the
 * additions to the trail, conflicts and lemmas of NA (and, for tra::notify, the lemmas of the
 * function plugins) are printed as they are made.
 *   tra::na_ctx                    NA's calls to the plugin context (restarts, delta triggers, ...)
 * Analysis and consistency (tra_analyze_term.c, tra_consistency.c):
 *   tra::analyze_term              the attributes of each analyzed term (range, variables, ...)
 *   tra::consistency               the check of each atom, and TRA's delta triggers
 *   tra::abstraction               the abstraction of each term, and the value of ite guards
 *   tra::trail_vs_interval         the position of a trail value relative to an abstraction
 *   tra::extract_conflicts         the terms of the conflict of an inconsistent atom
 * Conflict strategies (tra_conflict_strategies.c):
 *   tra::get_range_conflict, tra::get_constant_term_conflict, tra::get_function_plugin_conflict
 *                                  entry and exit of each strategy
 *   tra::conflict_strategies       the conflicts they build, the function plugin strategy that built
 *                                  each (e.g. [sin: pair], or [exp: default] for the default conflict),
 *                                  and their requests to the function plugins
 * Function plugins:
 *   tra::sin, tra::exp             the parameters and failures of their conflict strategies
 * Strategy managers (tra_func_set_strategy_profile):
 *   tra::strategy_manager          each profile that a manager sets (none before the tracer is set)
 * tra::interactive waits for input after each printed exit line (Enter: next; :f: run to the exit
 * of f; :n: stop waiting; :q: quit). tra::any does not enable it.
 */

#ifndef MCSAT_TRA_UTIL_TRACING_H_
#define MCSAT_TRA_UTIL_TRACING_H_

#include <stdbool.h>

#include "yices_types.h"
#include "mcsat/mcsat_types.h"
#include "mcsat/plugin.h"
#include "mcsat/tracing.h"
#include "mcsat/variable_db.h"

#include "mcsat/tra/tra_abstract_values.h"
#include "mcsat/tra/tra_func_plugin.h"

static inline bool tra_trace_enabled(const struct tra_plugin_s* tra, const char* tag) {
#ifndef NDEBUG
  const plugin_context_t* ctx = tra_get_context(tra);
  return ctx_trace_enabled(ctx, tag) || ctx_trace_enabled(ctx, "tra::any");
#else
  return false; // tracing is compiled out (see ctx_trace_enabled)
#endif
}

void tra_trace_printf(const struct tra_plugin_s* tra, const char* text, ...);

void tra_trace_iprintf(const struct tra_plugin_s* tra, const char* text, ...);

// tra_trace_printf and tra_trace_iprintf if tag is enabled; compiled out under NDEBUG, where
// tra_trace_enabled is false
#define tra_trace_printf_if(tra, tag, ...) \
  do { if (tra_trace_enabled(tra, tag)) tra_trace_printf(tra, __VA_ARGS__); } while (0)
#define tra_trace_iprintf_if(tra, tag, ...) \
  do { if (tra_trace_enabled(tra, tag)) tra_trace_iprintf(tra, __VA_ARGS__); } while (0)

void tra_trace_enter(const struct tra_plugin_s* tra, const char* function_name);

void tra_trace_exit(const struct tra_plugin_s* tra, const char* function_name);

// The enter and exit lines of the function name, under the tag "tra::" name (name is a string literal)
#define tra_trace_enter_function(tra, name) \
  do { if (tra_trace_enabled(tra, "tra::" name)) tra_trace_enter(tra, name); } while (0)
#define tra_trace_exit_function(tra, name) \
  do { if (tra_trace_enabled(tra, "tra::" name)) tra_trace_exit(tra, name); } while (0)

// Only the two macros above may call tra_trace_enter and tra_trace_exit (tra_tracing.c defines them)
#ifndef TRA_TRACING_DEFINITIONS
#pragma GCC poison tra_trace_enter tra_trace_exit
#endif

void tra_trace_print_variable_assignment(const struct tra_plugin_s* tra, variable_t x, const mcsat_value_t* value);

void tra_trail_print(const struct tra_plugin_s* tra);

void tra_trace_print_term(const struct tra_plugin_s* tra, const term_t t);

void tra_trace_print_term_infos(const struct tra_plugin_s* tra, const term_t t);

void tra_trace_print_interval(const struct tra_plugin_s* tra, tra_itype_t I, long digits);

void tra_trace_print_abstraction(const struct tra_plugin_s* tra, const term_t t);

const char* tra_notify_kind_to_string(plugin_notify_kind_t kind);

const char* tra_tv_position_to_string(tra_tv_position_t p);

#endif /* MCSAT_TRA_UTIL_TRACING_H_ */