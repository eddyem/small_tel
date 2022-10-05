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

#include <stdarg.h>
#include <stdio.h>
#include <usefull_macros.h>

#include "cmdlnopts.h"
#include "uniform.h"

// return sign of angle (+1 - positive, -1 - negative, 0 - error)
static int deg2dms(double deg, int *d, int *m, int *s){
    if(!d || !m || !s) return 0;
    int sign = 1;
    if(deg < 0.){
        sign = -1;
        deg = -deg;
    }
    // add 0.5'' for right roundness
    deg += 0.5/3600.;
    *d = (int) deg;
    deg = (deg - *d) * 60.; // minutes
    *m = (int) deg;
    deg = (deg - *m) * 60.;
    *s = (int)deg;
    return sign;

}
/*
// for hour angle in hour format
static int deg2hms(double deg, int *d, int *m, int *s){
  //  if(deg < 0.) deg += 360.;
    deg /= 15.;
    return deg2dms(deg, d, m, s);
}*/

static void tee(FILE *f, const char *fmt, ...){
    va_list ar;
    va_start(ar, fmt);
    vprintf(fmt, ar);
    va_end(ar);
    if(f){
        va_start(ar, fmt);
        vfprintf(f, fmt, ar);
        va_end(ar);
    }
}

/**
 * @brief savepoints - print points and save to file (if f!=NULL)
 * @param f - file
 * @param pts - points
 * @param N - amount
 * @param delim - angle delimeter string
 * @param mask - bit mask of columns (e.g. 1100 - show A/Z and hile HA/Dec)
 */
static void savepoints(FILE *f, point *pts, int N, char *delim, int mask){
    char str[4][32];
    if(!pts || !delim || N < 1) return;
    const char *headers[4] = {"A", "Z", "HA", "Dec"};
    int show[4] = {0};
    for(int i = 0; i < 4; ++i) if(mask & (1<<i)) show[i] = 1;
    tee(f, "%-15s", "#");
    for(int i = 0; i < 4; ++i){
        // check which columns we need
        if(show[i]) tee(f, "%-14s", headers[i]);
    }
    tee(f, "\n");
    for(int i = 0; i < N; ++i, ++pts){
        char *sgn[4] = {"", "", "", ""};
        int d[4], m[4], s[4];
        if(deg2dms(pts->A, &d[0], &m[0], &s[0]) < 0) sgn[0] = "-";
        if(deg2dms(pts->Z, &d[1], &m[1], &s[1]) < 0) sgn[1] = "-";
        if(deg2dms(pts->HA/15., &d[2], &m[2], &s[2]) < 0) sgn[2] = "-";
        if(deg2dms(pts->Dec, &d[3], &m[3], &s[3]) < 0) sgn[3] = "-";
        for(int i = 0; i < 4; ++i)
            if(show[i]) snprintf(str[i], 31, "%s%02d%s%02d%s%02d",
                     sgn[i], d[i], delim, m[i], delim, s[i]);
        tee(f, "%-6d", i);
        for(int i = 0; i < 4; ++i){
            if(show[i]) tee(f, "%14s", str[i]);
        }
        tee(f, "\n");
    }
}

int main(int argc, char **argv){
    initial_setup();
    glob_pars *G = parse_args(argc, argv);
    FILE *f = NULL;
    if(G->outfile){
        f = fopen(G->outfile, "w");
        if(!f) ERR("Can't open %s", G->outfile);
    }
    if(G->Npts < 10) ERRX("Need at least 10 points");
    if(!set_Zlimits(G->Zmin, G->Zmax)) ERRX("Wrong Z limits");
    if(G->sorting && !set_sorting(G->sorting, G->sortcoord)){
        show_sorting_help();
        return 1;
    }
    int mask = 0x0f;
    for(int i = 0; i < 4; ++i){
        if(G->hide[i]) mask &= ~(1<<i);
    }
    if(mask == 0) ERRX("You can't hide ALL columns");
    int N = G->Npts;
    point *points = getPoints(&N);
    if(!points) ERRX("Can't calculate");
    green("%d -> %d\n", G->Npts, N);
    savepoints(f, points, N, G->delimeter, mask);
    if(f) fclose(f);
    return 0;
}
