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

#ifndef MCSAT_TRA_PI_H_
#define MCSAT_TRA_PI_H_  

#include "mcsat/tra/tra_func_plugin.h"

/**
 * Ensures:  result is a new function plugin for the constant pi (name "pi", arity 0), whose
 *           methods and strategies meet the contracts of tra_func_plugin.h.
 */
tra_func_plugin_t* tra_pi_plugin_allocator(const struct tra_plugin_s* tra);

#endif /* MCSAT_TRA_PI_H_ */