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

#include "mcsat/na/poly-backends/tra_libpoly_poly.h"

#include <poly/polynomial.h>

#include "utils/memalloc.h"

static void tra_libpoly_destruct(na_poly_backend_t* self) {
  safe_free(self);
}

na_poly_backend_t* tra_libpoly_backend_allocator(void) {
  na_poly_backend_t* backend = (na_poly_backend_t*) safe_malloc(sizeof(na_poly_backend_t));
  backend->factor_square_free = lp_polynomial_factor_square_free;
  backend->roots_isolate = lp_polynomial_roots_isolate;
  backend->resultant = NULL;
  backend->destruct = tra_libpoly_destruct;
  return backend;
}
