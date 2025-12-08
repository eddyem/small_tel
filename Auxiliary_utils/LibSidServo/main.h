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

/*
 * Almost all here used for debug purposes
 */

#pragma once

#include <stdlib.h>

#include "movingmodel.h"
#include "sidservo.h"

extern conf_t Conf;
extern limits_t Xlimits, Ylimits;
int curtime(struct timespec *t);
double timediff(const struct timespec *time1, const struct timespec *time0);
double timediff0(const struct timespec *time1);
double timefromstart();
void getModData(coordpair_t *c, movestate_t *xst, movestate_t *yst);
typedef struct{
    double *x, *t, *t2, *xt; // arrays of coord/time and multiply
    double xsum, tsum, t2sum, xtsum; // sums of coord/time and their multiply
    size_t idx; // index of current data in array
    size_t arraysz; // size of arrays
} less_square_t;

less_square_t *LS_init(size_t Ndata);
void LS_delete(less_square_t **ls);
double LS_calc_slope(less_square_t *l, double x, double t);

// unused arguments of functions
#define _U_         __attribute__((__unused__))
// weak functions
#define WEAK        __attribute__ ((weak))

#ifndef DBL_EPSILON
#define DBL_EPSILON        (2.2204460492503131e-16)
#endif

#ifndef FALSE
#define FALSE (0)
#endif

#ifndef TRUE
#define TRUE (1)
#endif


#ifdef EBUG
#include <stdio.h>
    #define COLOR_RED           "\033[1;31;40m"
    #define COLOR_GREEN         "\033[1;32;40m"
    #define COLOR_OLD           "\033[0;0;0m"
    #define FNAME() do{ fprintf(stderr, COLOR_GREEN "\n%s " COLOR_OLD, __func__); \
    fprintf(stderr, "(%s, line %d)\n", __FILE__, __LINE__);} while(0)
    #define DBG(...) do{ fprintf(stderr, COLOR_RED "%s " COLOR_OLD, __func__); \
        fprintf(stderr, "(%s, line %d): ", __FILE__, __LINE__); \
        fprintf(stderr, __VA_ARGS__);           \
        fprintf(stderr, "\n");} while(0)

#else  // EBUG
    #define FNAME()
    #define DBG(...)
#endif // EBUG
