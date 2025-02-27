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
        fprintf(fcoords, "# time Xmot(deg) Ymot(deg) Xenc(deg) Yenc(deg) millis T V\n");
        return;
    }
    if(t0 < 0.) t0 = m->encposition.msrtime.tv_sec + (double)(m->encposition.msrtime.tv_usec) / 1e6;
    double t = m->encposition.msrtime.tv_sec + (double)(m->encposition.msrtime.tv_usec) / 1e6 - t0;
    // write data
    fprintf(fcoords, "%12.6f %10.6f %10.6f %10.6f %10.6f %10u %6.1f %4.1f\n",
            t, RAD2DEG(m->motposition.X), RAD2DEG(m->motposition.Y),
            RAD2DEG(m->encposition.X), RAD2DEG(m->encposition.Y),
            m->millis, m->temperature, m->voltage);
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
    uint32_t millis = mdata.encposition.msrtime.tv_usec;
    int ctr = -1;
    double xlast = mdata.motposition.X, ylast = mdata.motposition.Y;
    double t0 = sl_dtime();
    //DBG("millis = %u", millis);
    while(sl_dtime() - t0 < t && ctr < N){
        usleep(1000);
        if(MCC_E_OK != Mount.getMountData(&mdata)){ WARNX("Can't get data"); continue;}
        if(mdata.encposition.msrtime.tv_usec == millis) continue;
        //DBG("Got new data, posX=%g, posY=%g", mdata.motposition.X, mdata.motposition.Y);
        millis = mdata.encposition.msrtime.tv_usec;
        if(fcoords) logmnt(fcoords, &mdata);
        if(mdata.motposition.X != xlast || mdata.motposition.Y != ylast){
            xlast = mdata.motposition.X;
            ylast = mdata.motposition.Y;
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
        if(mdata.motposition.X != xlast || mdata.motposition.Y != ylast){
            xlast = mdata.motposition.X;
            ylast = mdata.motposition.Y;
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
int getPos(coords_t *mot, coords_t *enc){
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
    if(mot) *mot = mdata.motposition;
    if(enc) *enc = mdata.encposition;
    return TRUE;
}

// check current position and go to 0 if non-zero
void chk0(int ncycles){
    coords_t M;
    if(!getPos(&M, NULL)) signals(2);
    if(M.X || M.Y){
        WARNX("Mount position isn't @ zero; moving");
        double zero = 0.;
        Mount.moveTo(&zero, &zero);
        waitmoving(ncycles);
        green("Now mount @ zero\n");
    }
}
