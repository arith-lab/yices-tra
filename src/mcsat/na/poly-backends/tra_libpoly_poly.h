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

#ifndef TRA_LIBPOLY_POLY_H_
#define TRA_LIBPOLY_POLY_H_

#include "mcsat/na/na_poly.h"

/**
 * Ensures:  result is a new backend whose operations are libpoly's and meet the contracts of
 *           na_poly_backend_t (na_poly.h). resultant is NULL: libpoly's resultant costs as much as
 *           its principal subresultant coefficients, which the projection then computes directly.
 */
na_poly_backend_t* tra_libpoly_backend_allocator(void);

#endif /* TRA_LIBPOLY_POLY_H_ */
