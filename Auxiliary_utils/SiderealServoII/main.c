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

#include <signal.h>         // signal
#include <stdio.h>          // printf
#include <stdlib.h>         // exit, free
#include <string.h>         // strdup
#include <unistd.h>         // sleep

#include "cmdlnopts.h"
#include "emulator.h"
#include "motlog.h"
#include "sidservo.h"

static glob_pars *GP = NULL;  // for GP->pidfile need in `signals`

/**
 * We REDEFINE the default WEAK function of signal processing
 */
void signals(int sig){
    DBG("Quit");
    if(sig > 0){
        signal(sig, SIG_IGN);
        DBG("Get signal %d, quit.\n", sig);
    }
    LOGERR("Exit with status %d", sig);
    DBG("unlink");
    if(GP && GP->pidfile) // remove unnesessary PID file
        unlink(GP->pidfile);
    SSwritecmd(CMD_STOPHA);
    SSwritecmd(CMD_STOPDEC);
    DBG("close");
    SSclose();
    DBG("exit");
    exit(sig);
}

void iffound_default(pid_t pid){
    ERRX("Another copy of this process found, pid=%d. Exit.", pid);
}

int main(int argc, char *argv[]){
    initial_setup();
    char *self = strdup(argv[0]);
    GP = parse_args(argc, argv);
    check4running(self, GP->pidfile);
    green("%s started, snippets library version is %s\n", self, sl_libversion());
    free(self);
    if(GP->logfile){
        if(!OPENLOG(GP->logfile, LOGLEVEL_ANY, 1)) ERRX("Can't open logfile %s", GP->logfile);
    }
    if(GP->motorslog){
        if(!open_mot_log(GP->motorslog)) ERRX("Can't open motors' log file %s", GP->motorslog);
        mot_log(0, "# Motor's data\n#time\tX\tXenc\tVx\tY\tYenc\tVy");
    }
    signal(SIGTERM, signals); // kill (-15) - quit
    signal(SIGHUP, SIG_IGN);  // hup - ignore
    signal(SIGINT, signals);  // ctrl+C - quit
    signal(SIGQUIT, signals); // ctrl+\ - quit
    signal(SIGTSTP, SIG_IGN); // ignore ctrl+Z
    LOGMSG("Start application...");
    if(!SSinit(GP->device, GP->speed)) signals(-2);
    mot_log(0, "Starting of emulation");
    SSstart_emulation(0., 0.);
    mot_log(0, "Return to zero");
    SSgoto(0., 0.);
    SSwaitmoving();
    signals(0);
    // try to move for 45 degrees by both axes
    if(!SSgoto(45., 45.)) signals(-3);
    SSwaitmoving();
    signals(0);
    DBG("Try to send short command");
    SSscmd sc = {
        .DECmot = 500000,
        .DECspeed = 1000000,
        .HAmot = 600000,
        .HAspeed = 2000000
    };
    mot_log(0, "Send short command");
    while(SScmds(&sc) != sizeof(SSstat)) WARNX("SSCMDshort bad answer!");
    SSmotor_monitoring((SSstat*)SSread(NULL)); // monitor
    SSlcmd lc = {
        .DECmot = 0,
        .DECspeed = 2000000, // steps per sec * 65536 / 1953
        .HAmot = 0, //-427643
        .HAspeed = 2000000,
        .DECadder = 100,
        .HAadder = 40,
        .DECatime = 1953*3,
        .HAatime = 1953*4
    };
    DBG("Try to send long command");
    mot_log(0, "Send long command");
    while(SScmdl(&lc) != sizeof(SSstat)) WARNX("SSCMDlong bad answer!");
    SSmotor_monitoring((SSstat*)SSread(NULL)); // monitor
    mot_log(0, "Stop motors");
    SSwritecmd(CMD_STOPHA);
    SSwritecmd(CMD_STOPDEC);
    SSmotor_monitoring(NULL); // monitor stopping
    /*
    double t0 = dtime();
    while(1){ // read data from port and print in into terminal
        ;
    }*/
    // clean everything
    signals(0);
    // never reached
    return 0;
}
