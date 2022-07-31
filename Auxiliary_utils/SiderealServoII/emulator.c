/*
 * This file is part of the SSII project.
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
#include <usefull_macros.h>

#include "emulator.h"
#include "motlog.h"

// HA/DEC of starting point, starting time
static double Eha0 = 0., Edec0 = 0., Et0 = 0.;

// monitor each 100ms motor's motion; stop when coords are the same for 3 times in a row
// inistat - returning of binary command (if len equal to sizeof SSstat)
void SSmotor_monitoring(SSstat *inistat){
    DBG("Start monitoring");
    //int32_t oldDec = 0, oldRA = 0; // old positions in encoder values (also for speed calculations)
    SSstat prevstat = {0};
    uint8_t start = 1, repeat = 0, errctr = 0;
    int32_t oldRAm = 0, oldDm = 0;
    double tlast = dtime();
    if(inistat){ // get initial values
        start = 0;
        prevstat = *inistat;
        oldRAm = prevstat.HAmot;
        oldDm = prevstat.DECmot;
    }
    while(1){
        if(start){ // didn't get initial values -> need to know first
            if(!SSgetPartialstat(&prevstat)) continue;
            start = 0;
            tlast = dtime();
            oldRAm = prevstat.HAmot;
            oldDm = prevstat.DECmot;
        }else{
            if(!SSlog_motor_data(&prevstat, &tlast)){
                WARNX("Hmmm... in %dth time", ++errctr);
                if(++errctr > 10) break;
                else continue;
            }else errctr = 0;
        }
        if(prevstat.HAmot == oldRAm && prevstat.DECmot == oldDm){
            if(++repeat > 2) break;
        }else repeat = 0;
        oldRAm = prevstat.HAmot;
        oldDm = prevstat.DECmot;
    }
    DBG("End of monitoring");
}


/**
 * @brief log_motor_data - get current config & put to log difference between values
 * @param old (io) - old parameters value: as it was -> as it become
 * @param told (io) - previous time (`dtime()` output) -> current time
 * @return TRUE if all OK (if return FALSE, the `old` and `told` won't be changed!
 */
int SSlog_motor_data(SSstat *old, double *told){
    SSstat s;
    if(!old || !told) return FALSE;
    if(!SSgetPartialstat(&s)) return FALSE;
    double tnow = dtime(), tdif = tnow - *told;
    mot_log(1, "%d\t%d\t%.1f\t%d\t%d\t%.1f", s.DECmot, s.DECenc, (s.DECmot - old->DECmot) / tdif,
            s.HAmot, s.HAenc, (s.HAmot - old->HAmot) / tdif);
    *old = s;
    *told = tnow;
    return TRUE;
}

// calculate next `target` position in current moment of time
// ha changed as 15''/s + 15*sin(t*2pi/600s);
// dec changed as 30*cos(t*2pi/600s)
static void emul_next_pos(double *ha, double *dec, double tnew){
    if(!ha || !dec) return;
    double s1, c1, t = tnew - Et0;
    sincos(t*2.*M_PI/600., &s1, &c1);
    *ha = Eha0 + ARCSEC2DEG(15) * (t + s1);
    *dec = Edec0 + ARCSEC2DEG(30) * c1;
}

// init emulated variables and start target's searching
void SSstart_emulation(double ha_start, double dec_start){
    Eha0 = ha_start;
    Edec0 = dec_start;
    Et0 = dtime();
    DBG("Try to send short command");
    SSscmd sc = {0};
    // now calculate first starting position and try to catch it
    double ha, dec;
    emul_next_pos(&ha, &dec, Et0 + 1.);
    mot_log(0, "goto ha=%d, dec=%d");
    SSgoto(ha, dec);
    SSwaitmoving();
    // now we are near of the target: start to catch it
    ;
    while(SScmds(&sc) != sizeof(SSstat)) WARNX("SSCMDshort bad answer!");

}

