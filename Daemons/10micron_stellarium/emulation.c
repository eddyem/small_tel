/*
 * This file is part of the mountdaemon_10micron project.
 * Copyright 2026 Edward V. Emelianov <edward.emelianoff@gmail.com>.
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

#include <erfa.h>
#include <erfam.h>
#include <math.h>
#include <stdatomic.h>
#include <usefull_macros.h>

#include "angles.h"
#include "emulation.h"

// emulation speed over axis
#define RASPEED         (DEG2RAD(5.))
#define DECSPEED        (DEG2RAD(8.))
// limiting Zen.d.
#define ZD_LIMIT        (DEG2RAD(80.))
// pointing tolerance: ~1''
#define POINTING_TOL    (DEG2RAD(0.0003))

// current emulation status (only MNT_S_STOPPED, MNT_S_TRACKING and MNT_S_SLEWING supported)
static atomic_int emul_status = MNT_S_STOPPED;
// current & target coordinates for stationary process
static horizCrds_t CurAZ = {.az = DEG2RAD(180.), .zd = DEG2RAD(80.)}, TagAZ = {0};
// pointing to AZ and stop
static bool pointAZ = false;
// current and target Ra/Dec
static polarCrds_t CurRD = {0}, TagRD = {0};
// last time user asks for coordinates or change something
static double tlast = -1.;
// flipping emulation
static bool flip_in_progress = false;
static polarCrds_t flip_target = {0}; // flipping target when dec>90deg, ra+=12h

/**
 * send coordinates to telescope emulation
 * @param ra - right ascention (hours)
 * @param decl - declination (degrees)
 */
bool point_emulation(double ra, double decl){
    norm_RADEC(&ra, &decl);
    DBG("(emul) Send ra=%ghrs, decl=%gdeg", ra, decl);
    LOGMSG("(emul) Send ra=%ghrs, decl=%gdeg", ra, decl);
    double LST;
    if(!get_LST(NULL, &LST)){
        DBG("Time error");
        return false;
    }
    // refresh coordinates
    get_emul_coords(NULL, NULL);
    TagRD.ra = HRS2RAD(ra); TagRD.dec = DEG2RAD(decl);
    horizCrds_t hc;
    eq2hor(&TagRD, &hc, LST);
    if(hc.zd > ZD_LIMIT){
        LOGWARN("User asks to point to object with ZD=%g", RAD2DEG(hc.zd));
        DBG("Bad zd: %g", RAD2DEG(hc.zd));
        return false;
    }
    tlast = sl_dtime();
    pointAZ = false;
    flip_in_progress = false;
    atomic_store(&emul_status, MNT_S_SLEWING);
    return true;
}

bool pointAZ_emulation(double a, double z){
    if(!normAZ(&a, &z)) return false;
    get_emul_coords(NULL, NULL);
    TagAZ.az = DEG2RAD(a);
    TagAZ.zd = DEG2RAD(z);
    pointAZ = true;
    tlast = sl_dtime();
    flip_in_progress = false;
    atomic_store(&emul_status, MNT_S_SLEWING);
    return true;
}

static void chk_zlim(double LST){
    if(CurAZ.zd > ZD_LIMIT){
        CurAZ.zd = ZD_LIMIT;
        emulation_stop();
        hor2eq(&CurAZ, &CurRD, LST);
    }
}

#if 0
// distance between `a` and `b` on sphere
static double angular_distance(const polarCrds_t *a, const polarCrds_t *b){
    double dRA = a->ra - b->ra;
    double sa, sb, ca, cb;
    sincos(a->dec, &sa, &ca);
    sincos(b->dec, &sb, &cb);
    double cd = sa*sb + ca*cb*cos(dRA);
    return acos(cd);
}
#endif
// time to reach position b from position a
static double reacht(const polarCrds_t *a, const polarCrds_t *b){
    // convert to [-pi, pi)
    double tra = fabs(eraAnpm(b->ra - a->ra)) / RASPEED, tdec = fabs(eraAnpm(b->dec - a->dec)) / DECSPEED;
    if(tra > tdec) return tra;
    return tdec;
}

// calculate flip target (dec>90deg)
static void get_flip_target(const polarCrds_t *src, polarCrds_t *dst){
    dst->ra = src->ra + ERFA_DPI;
    if(dst->ra >= ERFA_D2PI) dst->ra -= ERFA_D2PI;
    if(src->dec > 0.0)
        dst->dec = ERFA_DPI - src->dec;
    else
        dst->dec = -ERFA_DPI - src->dec;
}

/**
 * get coordinates (emulation)
 */
void get_emul_coords(double *ra, double *decl){
    mount_status_t curstat = emulation_status();
    double LST, tcur = sl_dtime();
    if(!get_LST(NULL, &LST)) return;
    if(curstat == MNT_S_STOPPED){ // just show constant A/Z
        DBG("Stopped -> get from a=%gdeg, z=%gdeg", RAD2DEG(CurAZ.az), RAD2DEG(CurAZ.zd));
        hor2eq(&CurAZ, &CurRD, LST);
    }else if(curstat == MNT_S_SLEWING){ // slew to target (RA/Dec or ZD/Az)
        if(pointAZ){
            hor2eq(&TagAZ, &TagRD, LST); // refresh target coordinates
            DBG("Slewing to A/Z: a=%g, z=%g", RAD2DEG(TagAZ.az), RAD2DEG(TagAZ.zd));
        }
        if(!flip_in_progress){ // check if we need to flip
            // distance for direct moving
            double time_direct = reacht(&CurRD, &TagRD);
            // new point after flipping
            polarCrds_t flip_candidate;
            get_flip_target(&TagRD, &flip_candidate);
            // distance for moving with flip
            double time_flip = reacht(&CurRD, &flip_candidate);

            if(time_flip < time_direct){
                // need to make flip: it's shorter
                flip_target = flip_candidate;
                flip_in_progress = true;
                DBG("Target: ra=%gdeg, dec=%gdeg; flip target: ra=%gdeg, dec=%gdec", RAD2DEG(TagRD.ra), RAD2DEG(TagRD.dec), RAD2DEG(flip_candidate.ra), RAD2DEG(flip_candidate.dec));
                DBG("Flip chosen: direct=%.3fsec, flip=%.3fsec\n\n\n", RAD2DEG(time_direct), RAD2DEG(time_flip));
            }else{
                flip_in_progress = false;
                DBG("Move directly: direct=%.3fsec, flip=%.3fsec\n\n\n", RAD2DEG(time_direct), RAD2DEG(time_flip));
            }
        }
        // flip target is the same as TagRD, but with RA+=12h and dec > 90deg
        polarCrds_t *target = flip_in_progress ? &flip_target : &TagRD;
        // RA difference over target and last position: [-pi, pi)
        double dRA = eraAnpm(target->ra - CurRD.ra);
        DBG("RA difference: %gdegr (tag: %g, cur: %g)", RAD2DEG(dRA), RAD2DEG(target->ra), RAD2DEG(CurRD.ra));
        double dDec = eraAnpm(target->dec - CurRD.dec);
        DBG("DEC difference: %gdegr (tag: %g, cur: %g)", RAD2DEG(dDec), RAD2DEG(target->dec), RAD2DEG(CurRD.dec));
        double dist = sqrt(dRA*dRA + dDec*dDec);
        double tra = fabs(dRA) / RASPEED, tdec = fabs(dDec) / DECSPEED;
        double time_to_reach = (tra > tdec) ? tra : tdec;
        double dt = tcur - tlast;
        if(dist < POINTING_TOL || time_to_reach < dt){ // on position
            DBG("ON Position, time to reach=%gs, dt=%gs\n\n\n", time_to_reach, dt);
            if(pointAZ) emulation_stop(); // got A/Z position, stop
            else atomic_store(&emul_status, MNT_S_TRACKING);
            if(flip_in_progress) flip_in_progress = false;
            CurRD = TagRD; // fix to dec <=90
        }else{
            DBG("dRA=%gdeg, dDEC=%gdegh\n\n\n", RAD2DEG(dRA), RAD2DEG(dDec));
            double sign = (dRA > 0.) ? 1. : -1.;
            if(tra < dt) CurRD.ra = target->ra;
            else CurRD.ra += dt * RASPEED * sign;
            sign = (dDec > 0.) ? 1. : -1.;
            if(tdec < dt) CurRD.dec = target->dec;
            else CurRD.dec += dt * DECSPEED * sign;
            if(CurRD.ra < 0.) CurRD.ra += ERFA_D2PI;
            else if(CurRD.ra >= ERFA_D2PI) CurRD.ra -= ERFA_D2PI;
        }
    }else if(curstat == MNT_S_TRACKING){ // tracking -> just show static RA/Dec until Z<ZD_LIMIT
        DBG("Tracking...");
        eq2hor(&CurRD, &CurAZ, LST);
    }
    chk_zlim(LST);
    tlast = tcur;
    if(ra || decl){
        // in flipping mode dec could be greater than 90
        double curra = CurRD.ra, curdec = CurRD.dec;
        norm_RADECr(&curra, &curdec);
        if(ra) *ra = curra;
        if(decl) *decl = curdec;
    }
}

// stop telescope
void emulation_stop(){
    atomic_store(&emul_status, MNT_S_STOPPED);
    pointAZ = 0;
}

// start tracking from current position
void emul_start_tracking(){
    if(MNT_S_TRACKING == emulation_status()) return;
    atomic_store(&emul_status, MNT_S_TRACKING);
    // TODO: convert current A/Z into RA/Dec and set zero speeds by both axes
}

mount_status_t emulation_status(){
    mount_status_t curstate = (mount_status_t) atomic_load(&emul_status);
    return curstate;
}
