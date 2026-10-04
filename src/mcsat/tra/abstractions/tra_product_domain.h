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
 * tra_product_domain.h
 *
 * The product of the domains D_1, ..., D_n (see abstractions/tra_product_domain.c). An element
 * x of the product is a tuple (x_1, ..., x_n), with x_i an element of D_i, and it denotes
 * γ(x) = γ(x_1) ∩ ... ∩ γ(x_n). In this way a domain that meets only the soundness clauses
 * of tra_abstract_values.h, such as the sign domain, can be combined with one that meets the
 * (A) clauses too, such as ARB.
 *
 * Contracts of tra_abstract_values.h met by the product:
 *  - every soundness clause, if D_1, ..., D_n meet them and the div of each D_i also accepts
 *    b with 0 ∈ γ(b), meeting its soundness clause for γ(b) \ {0} in place of γ(b)
 *    (0 ∉ γ(x) does not imply 0 ∉ γ(x_i));
 *  - the (A) clauses only in part. For instance, p(x) is the largest precision of x_1, ...,
 *    x_n, so an (A) clause of an operation holds when one D_i meets it on its own arguments;
 *    it may fail when only the intersection of the arguments meets its hypothesis.
 */

#ifndef TRA_PRODUCT_DOMAIN_H_
#define TRA_PRODUCT_DOMAIN_H_

#include <stdint.h>

#include "mcsat/tra/tra_abstract_values.h"

/**
 * Let P be the product library, the same at every call.
 * Requires: n > 0; libs[0], ..., libs[n-1] remain valid while P is used; libs and n are those of
 *           the first call.
 * Ensures:  result = P, the product of libs[0], ..., libs[n-1]. Only the first call writes the
 *           configuration of P.
 */
tra_ilib_t* tra_init_product_domain(tra_ilib_t* const* libs, uint32_t n);

#endif /* TRA_PRODUCT_DOMAIN_H_ */
