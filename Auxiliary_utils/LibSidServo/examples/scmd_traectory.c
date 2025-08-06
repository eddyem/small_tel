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

#include <pthread.h>
#include <signal.h>
#include <usefull_macros.h>

#include "conf.h"
#include "dump.h"
#include "sidservo.h"
#include "simpleconv.h"
#include "traectories.h"

// calculate some traectory and try to run over it

typedef struct{
    int help;
    int dumpconf;
    int Ncycles;        // n cycles to wait stop
    double reqint;      // requests interval (seconds)
    double Xmax;        // maximal X to stop
    double Ymax;        // maximal Y to stop
    double tmax;        // maximal time of emulation
    double X0;          // starting point of traectory (-30..30 degr)
    double Y0;          // -//-
    char *coordsoutput; // dump file
    char *errlog;       // log with position errors
    char *tfn;          // traectory function name
    char *conffile;
} parameters;

static FILE *fcoords = NULL, *errlog = NULL;
static pthread_t dthr;
static parameters G = {
    .Ncycles = 40,
    .reqint = 0.1,
    .tfn = "sincos",
    .Xmax = 45.,
    .Ymax = 45.,
    .tmax = 300., // 5 minutes
    .X0 = 10.,
    .Y0 = 10.,
};

static sl_option_t cmdlnopts[] = {
    {"help",        NO_ARGS,    NULL,   'h',    arg_int,    APTR(&G.help),      "show this help"},
    {"ncycles",     NEED_ARG,   NULL,   'n',    arg_int,    APTR(&G.Ncycles),   "N cycles in stopped state (default: 40)"},
    {"coordsfile",  NEED_ARG,   NULL,   'o',    arg_string, APTR(&G.coordsoutput),"output file with coordinates log"},
    {"reqinterval", NEED_ARG,   NULL,   'i',    arg_double, APTR(&G.reqint),    "mount requests interval (default: 0.1 second)"},
    {"traectory",   NEED_ARG,   NULL,   't',    arg_string, APTR(&G.tfn),       "used traectory function (default: sincos)"},
    {"xmax",        NEED_ARG,   NULL,   'X',    arg_double, APTR(&G.Xmax),      "maximal abs X coordinate for traectory (default: 45 degrees)"},
    {"ymax",        NEED_ARG,   NULL,   'Y',    arg_double, APTR(&G.Ymax),      "maximal abs Y coordinate for traectory (default: 45 degrees)"},
    {"tmax",        NEED_ARG,   NULL,   'T',    arg_double, APTR(&G.tmax),      "maximal duration time of emulation (default: 300 seconds)"},
    {"x0",          NEED_ARG,   NULL,   '0',    arg_double, APTR(&G.X0),        "starting X-coordinate of traectory (default: 10 degrees)"},
    {"y0",          NEED_ARG,   NULL,   '1',    arg_double, APTR(&G.Y0),        "starting Y-coordinate of traectory (default: 10 degrees)"},
    {"conffile",    NEED_ARG,   NULL,   'C',    arg_string, APTR(&G.conffile),  "configuration file name"},
    {"errlog",      NEED_ARG,   NULL,   'e',    arg_string, APTR(&G.errlog),    "file with errors log"},
    {"dumpconf",    NO_ARGS,    NULL,   'D',    arg_int,    APTR(&G.dumpconf),  "dump current configuration"},
    end_option
};

void signals(int sig){
    pthread_cancel(dthr);
    if(sig){
        signal(sig, SIG_IGN);
        DBG("Get signal %d, quit.\n", sig);
    }
    Mount.stop();
    sleep(1);
    Mount.quit();
    if(fcoords) fclose(fcoords);
    exit(sig);
}

static void *dumping(void _U_ *u){
    dumpmoving(fcoords, 3600., G.Ncycles);
    return NULL;
}

// calculate
static void runtraectory(traectory_fn tfn){
    if(!tfn) return;
    coordval_pair_t telXY;
    coordval_pair_t target;
    coordpair_t traectXY, endpoint;
    endpoint.X = G.Xmax, endpoint.Y = G.Ymax;
    double t0 = Mount.currentT(), tlast = 0.;
    double tlastX = 0., tlastY = 0.;
    while(1){
        if(!telpos(&telXY)){
            WARNX("No next telescope position");
            return;
        }
        if(telXY.X.t == tlastX && telXY.Y.t == tlastY) continue; // last measure - don't mind
        DBG("\n\nTELPOS: %g'/%g' measured @ %g/%g", RAD2AMIN(telXY.X.val), RAD2AMIN(telXY.Y.val), telXY.X.t, telXY.Y.t);
        tlastX = telXY.X.t; tlastY = telXY.Y.t;
        double t = Mount.currentT();
        if(fabs(telXY.X.val) > G.Xmax || fabs(telXY.Y.val) > G.Ymax || t - t0 > G.tmax) break;
        if(!traectory_point(&traectXY, t)) break;
        target.X.val = traectXY.X; target.Y.val = traectXY.Y;
        target.X.t = target.Y.t = t;
        // check whether we should change direction
        if(telXY.X.val > traectXY.X) endpoint.X = -G.Xmax;
        else if(telXY.X.val < traectXY.X) endpoint.X = G.Xmax;
        if(telXY.Y.val > traectXY.Y) endpoint.Y = -G.Ymax;
        else if(telXY.Y.val < traectXY.Y) endpoint.Y = G.Ymax;
        DBG("target: %g'/%g'", RAD2AMIN(traectXY.X), RAD2AMIN(traectXY.Y));
        DBG("%g: dX=%.4f'', dY=%.4f''", t-t0, RAD2ASEC(traectXY.X-telXY.X.val), RAD2ASEC(traectXY.Y-telXY.Y.val));
        DBG("Correct to: %g/%g with EP %g/%g", RAD2DEG(target.X.val), RAD2DEG(target.Y.val), RAD2DEG(endpoint.X), RAD2DEG(endpoint.Y));
        if(errlog)
            fprintf(errlog, "%10.4g  %10.4g  %10.4g\n", t, RAD2ASEC(traectXY.X-telXY.X.val), RAD2ASEC(traectXY.Y-telXY.Y.val));
        if(MCC_E_OK != Mount.correctTo(&target, &endpoint)) WARNX("Error of correction!");
        while((t = Mount.currentT()) - tlast < MCC_PID_REFRESH_DT) usleep(50);
        tlast = t;
    }
    WARNX("No next traectory point or emulation ends");
}

int main(int argc, char **argv){
    sl_init();
    sl_parseargs(&argc, &argv, cmdlnopts);
    if(G.help) sl_showhelp(-1, cmdlnopts);
    if(G.Xmax < 1. || G.Xmax > 90.) ERRX("Xmax should be 1..90 degrees");
    if(G.Ymax < 1. || G.Ymax > 90.) ERRX("Ymax should be 1..90 degrees");
    // convert to radians
    G.Xmax = DEG2RAD(G.Xmax); G.Ymax = DEG2RAD(G.Ymax);
    if(G.X0 < -30. || G.X0 > 30. || G.Y0 < -30. || G.Y0 > 30.)
        ERRX("X0 and Y0 should be -30..30 degrees");
    if(G.errlog){
        if(!(errlog = fopen(G.errlog, "w")))
            ERRX("Can't open error log %s", G.errlog);
        else
            fprintf(errlog, "#    time      Xerr''      Yerr''   // target - real\n");
    }
    if(G.coordsoutput){
        if(!(fcoords = fopen(G.coordsoutput, "w")))
            ERRX("Can't open %s", G.coordsoutput);
    }else fcoords = stdout;
    conf_t *Config = readServoConf(G.conffile);
    if(!Config || G.dumpconf){
        dumpConf();
        return 1;
    }
    Config->MountReqInterval = G.reqint;
    traectory_fn tfn = traectory_by_name(G.tfn);
    if(!tfn){
        WARNX("Bad traectory name %s, should be one of", G.tfn);
        print_tr_names();
        return 1;
    }
    coordpair_t c = {.X = DEG2RAD(G.X0), .Y = DEG2RAD(G.Y0)};
    if(!init_traectory(tfn, &c)){
        ERRX("Can't init traectory");
        return 1;
    }
    mcc_errcodes_t e = Mount.init(Config);
    if(e != MCC_E_OK){
        WARNX("Can't init devices");
        return 1;
    }
    signal(SIGTERM, signals); // kill (-15) - quit
    signal(SIGHUP, SIG_IGN);  // hup - ignore
    signal(SIGINT, signals);  // ctrl+C - quit
    signal(SIGQUIT, signals); // ctrl+\ - quit
    signal(SIGTSTP, SIG_IGN); // ignore ctrl+Z
    chk0(G.Ncycles);
    logmnt(fcoords, NULL);
    if(pthread_create(&dthr, NULL, dumping, NULL)) ERRX("Can't run dump thread");
    ;
    runtraectory(tfn);
    signals(0);
    return 0;
}
