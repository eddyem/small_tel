/*
 * This file is part of the weatherdaemon project.
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

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <usefull_macros.h>
#include "cmdlnopts.h"

/*
 * here are global parameters initialisation
 */
int help;

// default values for Gdefault & help
#define DEFAULT_PORT    "12345"
#define DEFAULT_PID		"/tmp/weatherdaemon.pid"

//            DEFAULTS
// default global parameters
static glob_pars defpars = {
    .port = DEFAULT_PORT,
    .logfile = NULL,
    .verb = 0,
    .pidfile = DEFAULT_PID
};
// default config: all values should be wrong or empty to understand than user change them
static glob_pars defconf = {
    .verb = -1,
};

static glob_pars G;

/*
 * Define command line options by filling structure:
 *  name        has_arg     flag    val     type        argptr              help
*/
#define COMMON_OPTS \
    {"port",    NEED_ARG,   NULL,   0,      arg_string, APTR(&G.port),      "network port to connect (default: " DEFAULT_PORT "); hint: use \"localhost:port\" to make local net socket"}, \
    {"logfile", NEED_ARG,   NULL,   'l',    arg_string, APTR(&G.logfile),   "save logs to file (default: none)"}, \
    {"pidfile", NEED_ARG,   NULL,   'P',    arg_string, APTR(&G.pidfile),   "pidfile name (default: " DEFAULT_PID ")"}, \
    {"sockpath",NEED_ARG,   NULL,   0,      arg_string, APTR(&G.sockname),  "UNIX socket path (starting from '\\0' for anonimous) of command socket"}, \
    {"plugin",  MULT_PAR,   NULL,   'p',    arg_string, APTR(&G.plugins),   "add this weather plugin (may be a lot of); FORMAT: \"dlpath:l:dev\", where `dlpath` - path of plugin library; `l` - 'D' for device, 'U' for UNIX-socket or 'N' for INET socket; dev - path to device and speed (like /dev/ttyS0:9600), UNIX socket name or host:port for INET"}, \
    {"pollt",   NEED_ARG,   NULL,   'T',    arg_int,    APTR(&G.pollt),     "set maximal polling interval (seconds, integer)"},

sl_option_t cmdlnopts[] = {
    {"help",    NO_ARGS,    NULL,   'h',    arg_int,    APTR(&help),        "show this help"},
    {"conffile",NEED_ARG,   NULL,   'c',    arg_string, APTR(&G.conffile),  "configuration file name (consists all or a part of long-named parameters and their values (e.g. plugin=liboldweather.so:D:/dev/ttyS0:115200)"},
    {"verb",    NO_ARGS,    NULL,   'v',    arg_none,   APTR(&G.verb),      "logfile verbocity level (each -v increased)"}, \
    COMMON_OPTS
    end_option
};

sl_option_t confopts[] = {
    {"verbose",  NEED_ARG,  NULL,   'v',    arg_int,    APTR(&G.verb),      "logfile verbocity level"}, \
    COMMON_OPTS
    end_option
};

static int sortstrings(const void *v1, const void *v2){
    const char **s1 = (const char **)v1, **s2 = (const char **)v2;
    return strcmp(*s1, *s2);
}

// compare plugins from configuration and command line; add to command line plugins all new
static void compplugins(glob_pars *cmdline, glob_pars *conf){
    if(!cmdline) return;
    char **p;
    int nconf = 0;
    if(conf){
        p = conf->plugins;
        if(p && *p) while(*p++) ++nconf;
    }
    int ncmd = 0;
    p = cmdline->plugins;
    if(p && *p) while(*p++) ++ncmd;
    DBG("Got %d plugins in conf and %d in cmdline", nconf, ncmd);
    // compare plugins and rebuild new list
    int newsize = ncmd + nconf;
    if(newsize == 0) return; // no plugins in both
    char **newarray = MALLOC(char*, newsize + 1); // +1 for ending NULL
    for(int i = 0; i < ncmd; ++i){ newarray[i] = cmdline->plugins[i]; }
    FREE(cmdline->plugins);
    if(conf){
        for(int i = 0; i < nconf; ++i){ newarray[i+ncmd] = conf->plugins[i]; }
        FREE(conf->plugins);
    }
    qsort(newarray, newsize, sizeof(char*), sortstrings);
    DBG("NOW together:"); p = newarray; while(*p) printf("\t%s\n", *p++);
    p = newarray;
    int nondobuleidx = 0;
    for(int i = 0; i < newsize;){
        int j = i + 1;
        for(; j < newsize; ++j){
            if(strcmp(newarray[i], newarray[j])) break;
            FREE(newarray[j]);
        }
        if(nondobuleidx != i){
            newarray[nondobuleidx] = newarray[i];
            newarray[i] = NULL;
        }
        ++nondobuleidx;
        i = j;
    }
    DBG("Result:"); p = newarray; while(*p) printf("\t%s\n", *p++);
    cmdline->plugins = newarray;
    cmdline->nplugins = nondobuleidx;
}

/**
 * Parse command line options and return dynamically allocated structure
 *      to global parameters
 * @param argc - copy of argc from main
 * @param argv - copy of argv from main
 * @return allocated structure with global parameters
 */
glob_pars *parse_args(int argc, char **argv){
    G = defpars; // copy defaults
    // format of help: "Usage: progname [args]\n"
    sl_helpstring("Usage: %s [args]\n\t" COLOR_RED "Be careful: command line options have priority over config" COLOR_OLD "\n\tWhere args are:\n");
    // parse arguments
    sl_parseargs(&argc, &argv, cmdlnopts);
    DBG("verb: %d", G.verb);
    if(help) sl_showhelp(-1, cmdlnopts);
    if(argc > 0){
        WARNX("You give %d unused parameters:", argc);
        while(argc) printf("\t%s\n", argv[--argc]);
    }
    DBG("PARS: \n-------------\n%s-------------\n\n", sl_print_opts(cmdlnopts, 1));
    if(G.conffile){ // read conffile and fix parameters (cmdline args are in advantage)
        glob_pars oldpars = G; // save cmdline opts
        G = defconf;
        if(!sl_conf_readopts(oldpars.conffile, confopts)) ERRX("Can't get options from %s", G.conffile);
        DBG("CONF: \n-------------\n%s-------------\n\n", sl_print_opts(confopts, 1));
        if((0 == strcmp(oldpars.port, DEFAULT_PORT)) && G.port) oldpars.port = G.port;
        if(!oldpars.logfile && G.logfile) oldpars.logfile = G.logfile;
        if(!oldpars.verb && G.verb > -1) oldpars.verb = G.verb;
        if((0 == strcmp(oldpars.pidfile, DEFAULT_PID)) && G.pidfile) oldpars.pidfile = G.pidfile;
        if(!oldpars.sockname && G.sockname) oldpars.sockname = G.sockname;
        // now check plugins
        compplugins(&oldpars, &G);
        G = oldpars;
    }else compplugins(&G, NULL);
    DBG("RESULT: \n-------------\n%s-------------\n\n", sl_print_opts(cmdlnopts, 1));
    DBG("Nplugins = %d", G.nplugins);
    return &G;
}

