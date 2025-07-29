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

// logging of mount position

#include <usefull_macros.h>

#include "dump.h"
#include "simpleconv.h"

#if 0
// amount of elements used for encoders' data filtering
#define NFILT	(10)

static double filterK[NFILT];
static double lastvals[2][NFILT] = {0};
static int need2buildFilter = 1;

static void buildFilter(){
    filterK[NFILT-1] = 1.;
    double sum = 1.;
    for(int i = NFILT-2; i > -1; --i){
        filterK[i] = (filterK[i+1] + 1.) * 1.1;
        sum += filterK[i];
    }
    for(int i = 0; i < NFILT; ++i) filterK[i] /= sum;
}

static double filter(double val, int idx){
    if(need2buildFilter){
        buildFilter();
        need2buildFilter = 0;
    }
    static int ctr[2] = {0};
    for(int i = NFILT-1; i > 0; --i) lastvals[idx][i] = lastvals[idx][i-1];
    lastvals[idx][0] = val;
    double r = 0.;
    if(ctr[idx] < NFILT){
        ++ctr[idx];
        return val;
    }
    for(int i = 0; i < NFILT; ++i) r += filterK[i] * lastvals[idx][i];
    return r;
}
#endif

/**
 * @brief logmnt - log mount data into file
 * @param fcoords - file to dump
 * @param m - mount data
 */
void logmnt(FILE *fcoords, mountdata_t *m){
    if(!fcoords) return;
    //DBG("LOG %s", m ? "data" : "header");
    static double t0 = -1.;
    if(!m){ // write header
        fprintf(fcoords, "#     time    Xmot(deg)   Ymot(deg) Xenc(deg)  Yenc(deg)   VX(d/s)    VY(d/s)     millis\n");
        return;
    }
    double tnow = (m->encXposition.t + m->encYposition.t) / 2.;
    if(t0 < 0.) t0 = tnow;
    double t = tnow - t0;
    // write data
    fprintf(fcoords, "%12.6f %10.6f %10.6f %10.6f %10.6f %10.6f %10.6f %10u\n",
            t, RAD2DEG(m->motXposition.val), RAD2DEG(m->motYposition.val),
            RAD2DEG(m->encXposition.val), RAD2DEG(m->encYposition.val),
            RAD2DEG(m->encXspeed.val), RAD2DEG(m->encYspeed.val),
            m->millis);
    fflush(fcoords);
}

/**
 * @brief dumpmoving - dump conf while moving
 * @param fcoords - dump file
 * @param t - max waiting time
 * @param N - number of cycles to wait while motors aren't moving
 */
void dumpmoving(FILE *fcoords, double t, int N){
    if(!fcoords) return;
    mountdata_t mdata;
    DBG("Start dump");
    int ntries = 0;
    for(; ntries < 10; ++ntries){
        if(MCC_E_OK == Mount.getMountData(&mdata)) break;
    }
    if(ntries == 10){
        WARNX("Can't get mount data");
        LOGWARN("Can't get mount data");
    }
    uint32_t mdmillis = mdata.millis;
    double enct = (mdata.encXposition.t + mdata.encYposition.t) / 2.;
    int ctr = -1;
    double xlast = mdata.motXposition.val, ylast = mdata.motYposition.val;
    double t0 = Mount.currentT();
    while(Mount.currentT() - t0 < t && ctr < N){
        usleep(1000);
        if(MCC_E_OK != Mount.getMountData(&mdata)){ WARNX("Can't get data"); continue;}
        double tmsr = (mdata.encXposition.t + mdata.encYposition.t) / 2.;
        if(tmsr == enct) continue;
        enct = tmsr;
        if(fcoords) logmnt(fcoords, &mdata);
        if(mdata.millis == mdmillis) continue;
        //DBG("ctr=%d", ctr);
        mdmillis = mdata.millis;
        if(mdata.motXposition.val != xlast || mdata.motYposition.val != ylast){
            xlast = mdata.motXposition.val;
            ylast = mdata.motYposition.val;
            ctr = 0;
        }else ++ctr;
    }
}

/**
 * @brief waitmoving - wait until moving by both axes stops at least for N cycles
 * @param N - amount of stopped cycles
 */
void waitmoving(int N){
    mountdata_t mdata;
    int ctr = -1;
    uint32_t millis = 0;
    double xlast = 0., ylast = 0.;
    while(ctr < N){
        usleep(10000);
        if(MCC_E_OK != Mount.getMountData(&mdata)){ WARNX("Can't get data"); continue;}
        if(mdata.millis == millis) continue;
        millis = mdata.millis;
        if(mdata.motXposition.val != xlast || mdata.motYposition.val != ylast){
            xlast = mdata.motXposition.val;
            ylast = mdata.motYposition.val;
            ctr = 0;
        }else ++ctr;
    }
}

/**
 * @brief getPos - get current position
 * @param mot (o) - motor position (or NULL)
 * @param Y (o) - encoder position (or NULL)
 * @return FALSE if failed
 */
int getPos(coordval_pair_t *mot, coordval_pair_t *enc){
    mountdata_t mdata = {0};
    int errcnt = 0;
    do{
        if(MCC_E_OK != Mount.getMountData(&mdata)) ++errcnt;
        else{
            errcnt = 0;
            if(mdata.millis) break;
        }
    }while(errcnt < 10);
    if(errcnt >= 10){
        WARNX("Can't read mount status");
        return FALSE;
    }
    if(mot){
        mot->X = mdata.motXposition;
        mot->Y = mdata.motYposition;
    }
    if(enc){
        enc->X = mdata.encXposition;
        enc->Y = mdata.encYposition;
    }
    return TRUE;
}

// check current position and go to 0 if non-zero
void chk0(int ncycles){
    coordval_pair_t M;
    if(!getPos(&M, NULL)) signals(2);
    if(M.X.val || M.Y.val){
        WARNX("Mount position isn't @ zero; moving");
        coordpair_t zero = {0., 0.};
        Mount.moveTo(&zero);
        waitmoving(ncycles);
        green("Now mount @ zero\n");
    }
}
