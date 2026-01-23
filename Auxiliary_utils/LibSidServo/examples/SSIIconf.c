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

#include <math.h>
#include <stdio.h>
#include <usefull_macros.h>

#include "conf.h"
#include "sidservo.h"
#include "simpleconv.h"

typedef struct{
    int help;
    int helpargs;
    int writeconf;
    char *conffile;
    char *hwconffile;
} parameters;

static hardware_configuration_t HW = {0};

static parameters G = {
    .conffile = "servo.conf",
};

static sl_option_t cmdlnopts[] = {
    {"help",        NO_ARGS,    NULL,   'h',    arg_int,    APTR(&G.help),      "show this help"},
    {"help-opts",   NO_ARGS,    NULL,   'H',    arg_int,    APTR(&G.helpargs),  "configuration help"},
    {"serconf",     NEED_ARG,   NULL,   'C',    arg_string, APTR(&G.conffile),  "serial configuration file name"},
    {"hwconf",      NEED_ARG,   NULL,   'i',    arg_string, APTR(&G.hwconffile),"SSII configuration file name"},
    {"writeconf",   NO_ARGS,    NULL,   0,      arg_int,    APTR(&G.writeconf), "write configuration (BE CAREFUL!)"},
    end_option
};

static sl_option_t confopts[] = {
    {"Xaccel",      NEED_ARG,   NULL,   0,      arg_double, APTR(&HW.Xconf.accel),  "X Default Acceleration, rad/s^2"},
    {"Yaccel",      NEED_ARG,   NULL,   0,      arg_double, APTR(&HW.Yconf.accel),  "Y Default Acceleration, rad/s^2"},
    end_option
};

static void dumpaxis(char axis, axis_config_t *c){
#define STRUCTPAR(p)    (c)->p
#define DUMP(par) do{printf("%c%s=%.10g\n", axis, #par, STRUCTPAR(par));}while(0)
#define DUMPD(par) do{printf("%c%s=%g\n", axis, #par, RAD2DEG(STRUCTPAR(par)));}while(0)
    DUMPD(accel);
    DUMPD(backlash);
    DUMPD(errlimit);
    DUMP(propgain);
    DUMP(intgain);
    DUMP(derivgain);
    DUMP(outplimit);
    DUMP(currlimit);
    DUMP(intlimit);
    DUMP(motor_stepsperrev);
    DUMP(axis_stepsperrev);
#undef DUMP
#undef DUMPD
}

static void dumpxbits(xbits_t *c){
#define DUMPBIT(f) do{printf("X%s=%d\n", #f, STRUCTPAR(f));}while(0)
    DUMPBIT(motrev);
    DUMPBIT(motpolarity);
    DUMPBIT(encrev);
    DUMPBIT(dragtrack);
    DUMPBIT(trackplat);
    DUMPBIT(handpaden);
    DUMPBIT(newpad);
    DUMPBIT(guidemode);
#undef DUMPBIT
}

static void dumpybits(ybits_t *c){
#define DUMPBIT(f) do{printf("Y%s=%d\n", #f, STRUCTPAR(f));}while(0)
    DUMPBIT(motrev);
    DUMPBIT(motpolarity);
    DUMPBIT(encrev);
    DUMPBIT(slewtrack);
    DUMPBIT(digin_sens);
    printf("Ydigin=%d\n", c->digin);
#undef DUMPBIT
}

static void dumpHWconf(){
#undef STRUCTPAR
#define STRUCTPAR(p)    (HW).p
#define DUMP(par) do{printf("%s=%g\n", #par, STRUCTPAR(par));}while(0)
#define DUMPD(par) do{printf("%s=%g\n", #par, RAD2DEG(STRUCTPAR(par)));}while(0)
#define DUMPU8(par) do{printf("%s=%u\n", #par, (uint8_t)STRUCTPAR(par));}while(0)
#define DUMPU32(par) do{printf("%s=%u\n", #par, (uint32_t)STRUCTPAR(par));}while(0)
    green("X axis configuration:\n");
    dumpaxis('X', &HW.Xconf);
    green("X bits:\n");
    dumpxbits(&HW.xbits);
    green("Y axis configuration:\n");
    dumpaxis('Y', &HW.Yconf);
    green("Y bits:\n");
    dumpybits(&HW.ybits);
    green("Other:\n");
    printf("address=%d\n", HW.address);
    DUMP(eqrate);
    DUMP(eqadj);
    DUMP(trackgoal);
    DUMPD(latitude);
    DUMPU32(Xsetpr);
    DUMPU32(Ysetpr);
    DUMPU32(Xmetpr);
    DUMPU32(Ymetpr);
    DUMPD(Xslewrate);
    DUMPD(Yslewrate);
    DUMPD(Xpanrate);
    DUMPD(Ypanrate);
    DUMPD(Xguiderate);
    DUMPD(Yguiderate);
    DUMPU32(baudrate);
    DUMPD(locsdeg);
    DUMPD(locsspeed);
    DUMPD(backlspd);
}

int main(int argc, char** argv){
    sl_init();
    sl_parseargs(&argc, &argv, cmdlnopts);
    if(G.help)
        sl_showhelp(-1, cmdlnopts);
    if(G.helpargs)
        sl_showhelp(-1, confopts);
    conf_t *sconf = readServoConf(G.conffile);
    if(!sconf){
        dumpConf();
        return 1;
    }
    if(MCC_E_OK != Mount.init(sconf)) ERRX("Can't init mount");
    if(MCC_E_OK != Mount.getHWconfig(&HW)) ERRX("Can't read configuration");
    /*
    char *c = sl_print_opts(confopts, TRUE);
    green("Got configuration:\n");
    printf("%s\n", c);
    FREE(c);
    */
    dumpHWconf();
    /*
    if(G.hwconffile && G.writeconf){
        ;
    }*/
    Mount.quit();
    return 0;
}
