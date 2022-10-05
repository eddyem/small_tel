/*
 * This file is part of the uniformdistr project.
 * Copyright 2022 Edward V. Emelianov <edward.emelianoff@gmail.com>.
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

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <usefull_macros.h>

#include "uniform.h"

static double Zmin, Zmax;

typedef enum{
    SORT_NONE,
    SORT_POS,
    SORT_NEG,
    SORT_AMOUNT
} t_sorting;

typedef struct{
    const char *alg;
    const char *help;
} strpair;

static const strpair sorthelp[SORT_AMOUNT] = {
    [SORT_NONE] = {"none", "don't sort"},
    [SORT_POS] = {"positive", "sort in increasing order"},
    [SORT_NEG] = {"negative", "sort in decreasing order"},
};

typedef enum{
    COORD_A,
    COORD_Z,
    COORD_HA,
    COORD_DEC,
    COORD_AMOUNT
} t_coord;

static const char *coordname[COORD_AMOUNT] = {
    [COORD_A] = "A",
    [COORD_Z] = "Z",
    [COORD_HA] = "HA",
    [COORD_DEC] = "DEC"
};

// convert angle in radians into 0..360 degrees
static double deg360(double rad){
    rad *= 180. / M_PI;
    int n = (int)(rad / 360.);
    rad -= 360. * n;
    if(rad < 0.) rad += 360.;
    return rad;
}

// convert angle in radians into -180..180 degrees
static double deg180(double rad){
    rad = deg360(rad);
    if(rad > 180.) rad -= 360.;
    return rad;
}

// compare coordinates - for sorting
static int spos(double a1, double a2){
    if(a1 > a2) return 1;
    else return -1;
}
static int sneg(double a1, double a2){
    if(a1 < a2) return 1;
    else return -1;
}
// default compare functions
static int (*compf)(const void *, const void *) = NULL;
// compare points - for sorting
static int compapos(const void *a1, const void *a2){
    return spos(((point*)a1)->A, ((point*)a2)->A);
}
static int companeg(const void *a1, const void *a2){
    return sneg(((point*)a1)->A, ((point*)a2)->A);
}
static int compzpos(const void *a1, const void *a2){
    return spos(((point*)a1)->Z, ((point*)a2)->Z);
}
static int compzneg(const void *a1, const void *a2){
    return sneg(((point*)a1)->Z, ((point*)a2)->Z);
}
static int comphapos(const void *a1, const void *a2){
    return spos(((point*)a1)->HA, ((point*)a2)->HA);
}
static int comphaneg(const void *a1, const void *a2){
    return sneg(((point*)a1)->HA, ((point*)a2)->HA);
}
static int compdecpos(const void *a1, const void *a2){
    return spos(((point*)a1)->Dec, ((point*)a2)->Dec);
}
static int compdecneg(const void *a1, const void *a2){
    return sneg(((point*)a1)->Dec, ((point*)a2)->Dec);
}

static int (*comparray[COORD_AMOUNT][SORT_AMOUNT])(const void *, const void *) = {
    [COORD_A] = {[SORT_NONE] = NULL, [SORT_POS] = compapos, [SORT_NEG] = companeg},
    [COORD_Z] = {[SORT_NONE] = NULL, [SORT_POS] = compzpos, [SORT_NEG] = compzneg},
    [COORD_HA] = {[SORT_NONE] = NULL, [SORT_POS] = comphapos, [SORT_NEG] = comphaneg},
    [COORD_DEC] = {[SORT_NONE] = NULL, [SORT_POS] = compdecpos, [SORT_NEG] = compdecneg},
};

// convert horizontal to equatorial
static void hor2eq(point *p){
    double alt_s, alt_c, A_s, A_c; // sin/cos of alt and Az
    sincos((90. - p->Z)*M_PI/180., &alt_s, &alt_c);
    sincos(p->A*M_PI/180., &A_s, &A_c);
    double sind = SIN_LAT_OBS * alt_s + COS_LAT_OBS * alt_c * A_c;
    double d = asin(sind), cosd = cos(d);
    p->Dec = d *180./M_PI;
    double x = (alt_s - sind * SIN_LAT_OBS) / (cosd * COS_LAT_OBS);
    double y = -A_s * alt_c / cosd;
    p->HA = atan2(y, x) *180./M_PI;
    if(p->HA < 0.) p->HA += 360.;
}

/**
 * @brief getHpoints - calculate equally distributed points A/Z (degrees)
 * @param N (io) - max number of points (i), actual number of points (o)
 * @return array of unsorted coordinates
 */
static point* getHpoints(int *N){
    if(!N || *N < 1) return NULL;
    int n = *N, total = 0;
    point* points = MALLOC(point, n), *pptr = points;
    double ang = M_PI * (1. + sqrt(5));
    for(int i = 0; i < n; ++i){
        // we need only one hemisphere, so instead of acos(1. - 2.*(i+0.5)/n) get
        double phi = acos(1. - (i+0.5)/n);
        double theta = ang * i;
        //DBG("phi = %g (%g), theta = %g (%g)", phi, deg360(phi), theta, deg360(theta));
        if(phi > M_PI_2) break;
        double Z = deg360(phi);
        if(Z < Zmin || Z > Zmax) continue;
        ++total;
        pptr->A = deg180(theta);
        pptr->Z = Z;
        //BG("%d:\t%g / %g", total, pptr->A, pptr->Z);
        ++pptr;
    }
    *N = total;
    return points;
}

int set_Zlimits(double minz, double maxz){
    if(minz > 80. || minz < 0.) return FALSE;
    if(maxz > 90. || maxz < 10.) return FALSE;
    Zmin = minz;
    Zmax = maxz;
    return TRUE;
}

point *getPoints(int *N){
    point *points = getHpoints(N);
    if(!points) return NULL;
    // convert A,Z -> Ha,Dec (if sorting by HA/Dec?)
    for(int i = 0; i < *N; ++i)
        hor2eq(&points[i]);
    if(compf){ // sort data
        DBG("sort");
        qsort(points, *N, sizeof(point), compf);
    }
    return points;
}

// set sorting parameters
int set_sorting(char *param, char *coord){
    DBG("Try to set sorting '%s' by '%s'", param, coord);
    int x = 0, c = 0;
    for(; c < COORD_AMOUNT; ++c){
        if(strcasecmp(coordname[c], coord) == 0) break;
    }
    if(c == COORD_AMOUNT) return FALSE;
    for(; x < SORT_AMOUNT; ++x){
        //DBG("x=%d, cmp %s %s", x, sorthelp[x].alg, param);
        if(strncmp(param, sorthelp[x].alg, strlen(param)) == 0) break;
    }
    if(x == SORT_AMOUNT) return FALSE;
    DBG("x=%d, c=%d", x,c);
    compf = comparray[c][x];
    return TRUE;
}

void show_sorting_help(){
    fprintf(stderr, "Sorting algorythms:\n");
    for(int i = 0; i < SORT_AMOUNT; ++i){
        fprintf(stderr, "\t%s - %s\n", sorthelp[i].alg, sorthelp[i].help);
    }
    fprintf(stderr, "Sorting coordinates:\n");
    for(int i = 0; i < COORD_AMOUNT; ++i){
        fprintf(stderr, "\t%s\n", coordname[i]);
    }
}
