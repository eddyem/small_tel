/*
 * This file is part of the libsidservo project.
 * Copyright 2025 Edward V. Emelianov <edward.emelianoff@gmail.com>.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "sidservo.h"

// traectory
typedef int (*traectory_fn)(coordpair_t *, double);

int init_traectory(traectory_fn f, coordpair_t *XY0);
traectory_fn traectory_by_name(const char *name);
void print_tr_names();
int traectory_point(coordpair_t *nextpt, double t);
int telpos(coordval_pair_t *curpos);
int Linear(coordpair_t *nextpt, double t);
int SinCos(coordpair_t *nextpt, double t);
