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

#define TRA_TRACING_DEFINITIONS // see the poison pragma in tra_tracing.h
#include "mcsat/tra/utils/tra_tracing.h"

#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "yices_types.h"
#include "mcsat/model.h"
#include "mcsat/tracing.h"
#include "mcsat/trail.h"
#include "mcsat/value.h"
#include "terms/terms.h"
#include "utils/memalloc.h"
#include "utils/ptr_hash_map.h"

#include "mcsat/tra/tra_plugin_internal.h"
#include "mcsat/tra/utils/tra_term_explorer.h"

#define TRA_TRACE_COLOR_ENTER  "\033[1;32m"  /* bold green  */
#define TRA_TRACE_COLOR_EXIT   "\033[1;31m"  /* bold red    */
#define TRA_TRACE_COLOR_INFO   "\033[1;36m"  /* bold cyan   */
#define TRA_TRACE_COLOR_RESET  "\033[0m"

static int indent_c = 0;
static char* stopping_label = NULL;

static inline void indent(const plugin_context_t* ctx) {
  for (int i = 0; i < indent_c; i++) {
    ctx_trace_printf(ctx, "  ");
  }
}

void tra_trace_printf(const tra_plugin_t* tra, const char* text, ...) {
  va_list args;
  va_start(args, text);
  ctx_trace_vprintf(tra->ctx, text, args);
  va_end(args);
}

void tra_trace_iprintf(const tra_plugin_t* tra, const char* text, ...) {
  indent(tra->ctx);
  va_list args;
  va_start(args, text);
  ctx_trace_vprintf(tra->ctx, text, args);
  va_end(args);
}

void tra_trace_print_term(const tra_plugin_t* tra, const term_t t) {
  FILE* out = ctx_trace_out(tra->ctx);
  term_table_t* terms = tra->ctx->terms;

  ctx_trace_printf(tra->ctx, TRA_TRACE_COLOR_INFO);
  term_print_to_file(out, terms, t);
  ctx_trace_printf(tra->ctx, TRA_TRACE_COLOR_RESET);
}

static inline const char* mcsat_value_type_to_string(const mcsat_value_t* value) {
  switch (value->type) {
    case VALUE_NONE: return "NONE";
    case VALUE_BOOLEAN: return "BOOLEAN";
    case VALUE_RATIONAL: return "RATIONAL";
    case VALUE_LIBPOLY: return "LIBPOLY";
    case VALUE_BV: return "BV";
    default: return "UNKNOWN";
  }
}

void tra_trace_print_variable_assignment(const tra_plugin_t* tra, variable_t x, const mcsat_value_t* value) {
  FILE* out = ctx_trace_out(tra->ctx);
  term_table_t* terms = tra->ctx->terms;
  term_t t = variable_db_get_term(tra->ctx->var_db, x);

  indent(tra->ctx);

  tra_trace_print_term(tra, t);
  tra_trace_printf(tra, TRA_TRACE_COLOR_INFO " == ");
  mcsat_value_print(value, out);
  tra_trace_printf(tra, TRA_TRACE_COLOR_RESET " : (%s, %s, %s)\n", 
      kind_to_string(term_kind(terms, t)), 
      type_to_string(term_type_kind(terms, t)),
      mcsat_value_type_to_string(value));
}

void tra_trace_print_term_infos(const tra_plugin_t* tra, const term_t t) {
  term_table_t* terms = tra->ctx->terms;

  indent(tra->ctx);

  tra_trace_print_term(tra, t);
  tra_trace_printf(tra, " : (%s, %s)\n",
    kind_to_string(term_kind(terms, t)),
    type_to_string(term_type_kind(terms, t)));

  term_t def = preprocessor_purification_definition(tra->ctx->preprocessor, t);
  if (def != NULL_TERM) {
    indent(tra->ctx);
    tra_trace_printf(tra, "[proxy for: ");
    tra_trace_print_term(tra, def);
    tra_trace_printf(tra, "]\n");
  }

  indent(tra->ctx);
  tra_trace_printf(tra, "[depurified: ");
  tra_trace_print_term(tra, tra_depurify_term(tra->ctx->preprocessor, t));
  tra_trace_printf(tra, "]\n");
}

void tra_trace_enter(const tra_plugin_t* tra, const char* function_name) {
  indent(tra->ctx);
  indent_c += 1;
  tra_trace_printf(tra, TRA_TRACE_COLOR_ENTER "[+ (%u) tra::%s]" TRA_TRACE_COLOR_RESET "\n", tra->ctx->trail->decision_level, function_name);
}

static void tra_wait_for_instruction(const plugin_context_t *ctx) {
  static bool still_interactive = true;
  if(!still_interactive || stopping_label != NULL) {
    return;
  }

  ctx_trace_printf(ctx, "Waiting for input. Enter = next, :label = jump to next tra::label, :n = switch to non-interactive, :q = quit.\n");
  fflush(ctx_trace_out(ctx));

  char input[256];
  while (1) {
    if (fgets(input, sizeof(input), stdin) == NULL) {
      break; // Handle EOF or error
    }

    // Remove trailing newline
    input[strcspn(input, "\n")] = 0;

    if (strcmp(input, "") == 0) {
      // Enter key pressed
      break;
    } else if (strcmp(input, ":q") == 0) {
      // Quit
      ctx_trace_printf(ctx, "Exiting...\n");
      exit(0);
    } else if (strcmp(input, ":n") == 0) {
      // Switch to non-interactive
      still_interactive = false;
      break;
    } else if (input[0] == ':' && strlen(input) > 1) {
      // Handle :label
      printf("Jumping to label: %s\n", input + 1);
      stopping_label = safe_strdup(input + 1);
      break;
    }
    
    ctx_trace_printf(ctx, "\033[1A\033[2K");
    ctx_trace_printf(ctx, "\033[1A\033[2K");
    fflush(ctx_trace_out(ctx));
  }

  ctx_trace_printf(ctx, "\033[1A\033[2K");
  ctx_trace_printf(ctx, "\033[1A\033[2K");
  fflush(ctx_trace_out(ctx));
}

void tra_trace_exit(const tra_plugin_t* tra, const char* function_name) {
  indent_c -= 1;
  indent(tra->ctx);

  tra_trace_printf(tra, TRA_TRACE_COLOR_EXIT "[- (%u)]" TRA_TRACE_COLOR_RESET "\n", tra->ctx->trail->decision_level);
  if(stopping_label != NULL && strcmp(function_name, stopping_label) == 0) {
    safe_free(stopping_label);
    stopping_label = NULL;
  }
  if(ctx_trace_enabled(tra->ctx, "tra::interactive")) tra_wait_for_instruction(tra->ctx);
}

void tra_trail_print(const tra_plugin_t* tra) {
  const mcsat_trail_t* trail = tra->ctx->trail;
  uint32_t i;
  variable_t var;
  assignment_type_t var_type;

  indent(tra->ctx);
  tra_trace_printf(tra, TRA_TRACE_COLOR_RESET "[" TRA_TRACE_COLOR_INFO);
  for (i = 0; i < trail->elements.size; ++ i) {
    if (i) {
      tra_trace_printf(tra, TRA_TRACE_COLOR_RESET ",\n" TRA_TRACE_COLOR_INFO);
      indent(tra->ctx);
    }
    var = trail->elements.data[i];
    var_type = trail_get_assignment_type(trail, var);

    if (var_type == DECISION) {
      tra_trace_printf(tra, TRA_TRACE_COLOR_RESET "[%u]\n" TRA_TRACE_COLOR_INFO, trail_get_level(trail, var));
      indent(tra->ctx);
    }

    variable_db_print_variable(trail->var_db, var, tra->ctx->tracer->file);

    switch (var_type) {
    case DECISION:
      tra_trace_printf(tra, " *= ");
      break;
    case PROPAGATION:
      tra_trace_printf(tra, " == ");
      break;
    default:
      assert(false);
    }
    mcsat_value_print(trail->model.values + var, tra->ctx->tracer->file);
  }
  tra_trace_printf(tra, TRA_TRACE_COLOR_RESET "]\n");
}

const char* tra_notify_kind_to_string(plugin_notify_kind_t kind) {
  switch (kind) {
  case MCSAT_SOLVER_START:
    return "MCSAT_SOLVER_START";
  case MCSAT_SOLVER_RESTART:
    return "MCSAT_SOLVER_RESTART";
  case MCSAT_SOLVER_CONFLICT:
    return "MCSAT_SOLVER_CONFLICT";
  case MCSAT_SOLVER_POP:
    return "MCSAT_SOLVER_POP";
  case MCSAT_DELTA_BUMP:
    return "MCSAT_DELTA_BUMP";
  default:
    return "UNKNOWN_NOTIFY_KIND";
  }
}

const char* tra_tv_position_to_string(tra_tv_position_t p) {
  switch (p) {
  case TV_INSIDE:
    return "INSIDE";
  case TV_ABOVE:
    return "ABOVE";
  case TV_BELOW:
    return "BELOW";
  case TV_UNKNOWN:
    return "UNKNOWN";
  default:
    return "?";
  }
}

void tra_trace_print_interval(const tra_plugin_t* tra, tra_itype_t I, long digits) {
  tra_ilib_t* lib = tra->abstract_domain;
  FILE* out = ctx_trace_out(tra->ctx);

  tra_trace_printf(tra, TRA_TRACE_COLOR_INFO);
  lib->fprintd(out, I, digits);
  tra_trace_printf(tra, TRA_TRACE_COLOR_RESET);
}

void tra_trace_print_abstraction(const tra_plugin_t* tra, const term_t t) {
  tra_ilib_t* lib = tra->abstract_domain;
  ptr_hmap_pair_t* cached = ptr_hmap_find(&tra->cache_abstractions, t);

  if(cached == NULL || cached->val == NULL || !((tra_cache_abstraction_t*) cached->val)->valid) {
    tra_trace_iprintf(tra, "[no abstraction found for ");
    tra_trace_print_term(tra, t);
    tra_trace_printf(tra, "]\n");

    return;
  }

  tra_cache_abstraction_t* cached_val = (tra_cache_abstraction_t*) cached->val;
  tra_prec_t precision = lib->get_precision(cached_val->interval);

  tra_trace_iprintf(tra, "[abstraction of ");
  tra_trace_print_term(tra, t);
  if (precision == INF_PREC) {
    tra_trace_printf(tra, ", precision +oo");
  } else if (precision == NO_PREC) {
    tra_trace_printf(tra, ", NO precision");
  } else {
    tra_trace_printf(tra, ", precision %ld", precision);
  }
  if (cached_val->trail_trusted) tra_trace_printf(tra, ", trail trusted");
  tra_trace_printf(tra, ": ");
  tra_trace_print_interval(tra, cached_val->interval, 15);
  tra_trace_printf(tra, "]\n");
}
