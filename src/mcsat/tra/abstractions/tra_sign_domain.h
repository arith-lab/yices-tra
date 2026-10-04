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
 * tra_sign_domain.h
 *
 * The sign domain (see abstractions/tra_sign_domain.c). An element denotes a union of the
 * sets {0}, (0, +oo) and (-oo, 0). It meets the soundness clauses of tra_abstract_values.h,
 * but not the (A) clauses: for instance, set_z(x, 5) gives γ(x) = (0, +oo). It is therefore
 * used as a component of a product (tra_product_domain.h) with a domain that meets them.
 */

#ifndef TRA_SIGN_DOMAIN_H_
#define TRA_SIGN_DOMAIN_H_

#include "mcsat/tra/tra_abstract_values.h"

/* The sign domain: pass &tra_sign_ilib wherever a tra_ilib_t* is expected. */
extern tra_ilib_t tra_sign_ilib;

#endif /* TRA_SIGN_DOMAIN_H_ */
