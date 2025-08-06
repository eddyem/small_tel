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

// some simplest traectories
// all traectories runs increasing X and Y from starting point

#include <stdio.h>
#include <string.h>
#include <usefull_macros.h>

#include "simpleconv.h"
#include "traectories.h"

static traectory_fn cur_traectory = NULL;
// starting point of traectory
static coordpair_t XYstart = {0};
static double tstart = 0.;

/**
 * @brief init_traectory - init traectory fn, sync starting positions of motor & encoders
 * @param f - function calculating next point
 * @param XY0 - starting point
 * @return FALSE if failed
 */
int init_traectory(traectory_fn f, coordpair_t *XY0){
    if(!f || !XY0) return FALSE;
    cur_traectory = f;
    XYstart = *XY0;
    tstart = Mount.currentT();
    mountdata_t mdata;
    int ntries = 0;
    for(; ntries < 10; ++ntries){
        if(MCC_E_OK == Mount.getMountData(&mdata)) break;
    }
    if(ntries == 10) return FALSE;
    return TRUE;
}

/**
 * @brief traectory_point - get traectory point for given time
 * @param nextpt (o) - next point coordinates
 * @param t - UNIX-time of event
 * @return FALSE if something wrong (e.g. X not in -90..90 or Y not in -180..180)
 */
int traectory_point(coordpair_t *nextpt, double t){
    if(t < 0. || !cur_traectory) return FALSE;
    coordpair_t pt;
    if(!cur_traectory(&pt, t)) return FALSE;
    if(nextpt) *nextpt = pt;
    if(pt.X < -M_PI_2 || pt.X > M_PI_2 || pt.Y < -M_PI || pt.Y > M_PI) return FALSE;
    return TRUE;
}

// current telescope position according to starting motor coordinates
// @return FALSE if failed to get current coordinates
int telpos(coordval_pair_t *curpos){
    mountdata_t mdata;
    int ntries = 0;
    for(; ntries < 10; ++ntries){
        if(MCC_E_OK == Mount.getMountData(&mdata)) break;
    }
    if(ntries == 10) return FALSE;
    coordval_pair_t pt;
    pt.X.val = mdata.encXposition.val;
    pt.Y.val = mdata.encYposition.val;
    pt.X.t = mdata.encXposition.t;
    pt.Y.t = mdata.encYposition.t;
    if(curpos) *curpos = pt;
    return TRUE;
}

// X=X0+1'/s, Y=Y0+15''/s
int Linear(coordpair_t *nextpt, double t){
    coordpair_t pt;
    pt.X = XYstart.X + ASEC2RAD(0.1) * (t - tstart);
    pt.Y = XYstart.Y + ASEC2RAD(15.)* (t - tstart);
    if(nextpt) *nextpt = pt;
    return TRUE;
}

// X=X0+5'*sin(t/30*2pi), Y=Y0+10'*cos(t/200*2pi)
int SinCos(coordpair_t *nextpt, double t){
    coordpair_t pt;
    pt.X = XYstart.X + ASEC2RAD(5.) * sin((t-tstart)/30.*2*M_PI);
    pt.Y = XYstart.Y + AMIN2RAD(10.)* cos((t-tstart)/200.*2*M_PI);
    if(nextpt) *nextpt = pt;
    return TRUE;
}

typedef struct{
    traectory_fn f;
    const char *name;
    const char *help;
} tr_names;

static tr_names names[] = {
    {Linear, "linear", "X=X0+0.1''/s, Y=Y0+15''/s"},
    {SinCos, "sincos", "X=X0+5''*sin(t/30*2pi), Y=Y0+10'*cos(t/200*2pi)"},
    {NULL, NULL, NULL}
};

traectory_fn traectory_by_name(const char *name){
    traectory_fn f = NULL;
    for(int i = 0; ; ++i){
        if(!names[i].f) break;
        if(strcmp(names[i].name, name) == 0){
            f = names[i].f;
            break;
        }
    }
    return f;
}

// print all acceptable traectories names with help
void print_tr_names(){
    for(int i = 0; ; ++i){
        if(!names[i].f) break;
        printf("%s: %s\n", names[i].name, names[i].help);
    }
}
