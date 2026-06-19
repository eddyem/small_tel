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

#include <string.h>
#include <usefull_macros.h>

#include "args.h"
#include "server.h"

#define DEFAULT_COMDEV      "/dev/ttyUSB0"
// TCP socket port
#define DEFAULT_PORT        ":10000"
#define DEFAULT_CMDNODE     "localhost:10001"
// default PID filename:
#define DEFAULT_PIDFILE     "/tmp/mountdaemon_10micron.pid"
// default file with headers
#define DEFAULT_FITSHDR     "/tmp/10micron.fitsheader"
// baudrate - 115200
#define DEFAULT_SERSPEED    115200
// serial polling timeout - 100ms
#define DEFAULT_SERTMOUT    100000
// mount name
#define DEFAULT_MOUNT_NAME  "10Micron GM4000HPS"

// non-config file options
static int help = 0;
static char *conffile = NULL;

// default command line options
static parameters_t G = {0};
// default config options
static parameters_t Conf = {0};

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

#define OPTIONS(Stor) \
    {"device",  NEED_ARG,   NULL,   'd',    arg_string, APTR(&Stor.device),    "serial device name"}, \
    {"emulation",NO_ARGS,   NULL,   'e',    arg_int,    APTR(&Stor.emulation), "run in emulation mode"},  \
    {"logfile", NEED_ARG,   NULL,   'l',    arg_string, APTR(&Stor.logfile),   "file to save logs"}, \
    {"hdrfile", NEED_ARG,   NULL,   'o',    arg_string, APTR(&Stor.crdsfile),  "file to save FITS-header with coordinates and time (default: " DEFAULT_FITSHDR ")"}, \
    {"pidfile", NEED_ARG,   NULL,     0,    arg_string, APTR(&Stor.pidfile),   "pidfile (default: " DEFAULT_PIDFILE ")"}, \
    {"port",    NEED_ARG,   NULL,   'p',    arg_string, APTR(&Stor.port),      "port for stellaruim's connect (default: " DEFAULT_PORT ")"}, \
    {"cmdport", NEED_ARG,   NULL,   'P',    arg_string, APTR(&Stor.cmdnode),   "port or UNIX-socket path to connect for command console (default: " DEFAULT_CMDNODE ")"}, \
    {"sleept",  NEED_ARG,   NULL,   't',    arg_int,    APTR(&Stor.sleept),    "time of servers' sleeping in main cycle, us (default: " STR(DEFAULT_SLEEP_T) " us)"}, \
    {"isunix",  NO_ARGS,    NULL,   'U',    arg_int,    APTR(&Stor.isunix),    "use UNIX-socket for cmdport"}, \
    {"sertmout",NEED_ARG,   NULL,   'T',    arg_int,    APTR(&Stor.sertmout),  "serial timeout, us (default: " STR(DEFAULT_SERTMOUT) " us)"}, \
    {"serspeed",NEED_ARG,   NULL,   'S',    arg_int,    APTR(&Stor.serspeed),  "serial speed (default: " STR(DEFAULT_SERSPEED) ")"}, \
    {"maxclients",NEED_ARG, NULL,   0,      arg_int,    APTR(&Stor.maxclients),"max amount of clients connected to one socket (default: " STR(DEFAULT_MAXCLIENTS) ")"}, \
    {"mountname",NEED_ARG,  NULL,   0,      arg_string, APTR(&Stor.mountname), "mount name (default: " DEFAULT_MOUNT_NAME ")"},  \
    {"verbose", NO_ARGS,    NULL,   'v',    arg_none,   APTR(&Stor.verbose),   "verbose level (each -v increases it)"}, \


static sl_option_t cmdlnopts[] = {
    {"help",    NO_ARGS,    NULL,   'h',    arg_int,    APTR(&help),        "show this help"},
    {"config",  NEED_ARG,   NULL,   'c',    arg_string, APTR(&conffile),    "configuration file (command line options are in advantage)"},
    OPTIONS(G)
    end_option
};

// --help and --conf - options that should be parsed first
static sl_option_t confopts[] = {
    OPTIONS(Conf)
    end_option
};

static void chkstr(char **conf, char **cmd, const char *dflt){
    if(!*conf){
        if(!*cmd && dflt){
            DBG("Set default parameter: %s", dflt);
            *cmd = strdup(dflt);
        }
        return;
    }
    if(!*cmd){
        DBG("Conf-only parameter %s", *conf);
        *cmd = strdup(*conf);
    }
    FREE(*conf); // don't forget to clear
}

static void chkint(int *conf, int *cmd, int dflt){
    if(!*conf){
        if(!*cmd) *cmd = dflt;
        return;
    }
    if(!*cmd){
        DBG("Conf-only parameter %d", *conf);
        *cmd = *conf;
    }
}


#define STRCHK(field)  chkstr(&Conf.field, &G.field, NULL)
#define STRCHKD(field, def) chkstr(&Conf.field, &G.field, def)
#define INTCHK(field)  chkint(&Conf.field, &G.field, 0)
#define INTCHKD(field, def)  chkint(&Conf.field, &G.field, def)

parameters_t *parse_cmdline(int *argc, char ***argv){
    sl_parseargs(argc, argv, cmdlnopts);
    if(help) sl_showhelp(-1, cmdlnopts);
    if(!conffile) return &G;
    // fix for conffile
    if(!sl_conf_readopts(conffile, confopts)){ // show conf help
        sl_conf_showhelp(-1, confopts);
        return NULL;
    }
    // now fix for command line and check all:
    DBG("Check all args");
    STRCHKD(device, DEFAULT_COMDEV);
    STRCHKD(port, DEFAULT_PORT);
    STRCHKD(cmdnode, DEFAULT_CMDNODE);
    STRCHKD(pidfile, DEFAULT_PIDFILE);
    STRCHK(logfile);
    STRCHKD(crdsfile, DEFAULT_FITSHDR);
    STRCHKD(mountname, DEFAULT_MOUNT_NAME);
    INTCHK(emulation);
    INTCHK(verbose);
    INTCHKD(sleept, DEFAULT_SLEEP_T);
    INTCHK(isunix);
    INTCHKD(sertmout, DEFAULT_SERTMOUT);
    INTCHKD(serspeed, DEFAULT_SERSPEED);
    INTCHKD(maxclients, DEFAULT_MAXCLIENTS);
    return &G;
}
