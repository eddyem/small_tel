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

// simple conversion macros

#include <math.h>

#define DEG2RAD(d)  (d/180.*M_PI)
#define ASEC2RAD(d) (d/180.*M_PI/3600.)
#define AMIN2RAD(d) (d/180.*M_PI/60.)
#define RAD2DEG(r)  (r/M_PI*180.)
#define RAD2ASEC(r) (r/M_PI*180.*3600.)
#define RAD2AMIN(r) (r/M_PI*180.*60.)

