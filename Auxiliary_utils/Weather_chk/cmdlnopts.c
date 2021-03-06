/*
 * This file is part of the weatherchk project.
 * Copyright 2020 Edward V. Emelianov <edward.emelianoff@gmail.com>.
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


#include <assert.h> // assert
#include <stdio.h>  // printf
#include <string.h> // memcpy
#include <usefull_macros.h>
#include "cmdlnopts.h"

/*
 * here are global parameters initialisation
 */
static int help;
static glob_pars  G;

//            DEFAULTS
// default global parameters
glob_pars const Gdefault = {
    .speed = 9600,
    .ttyname = "/dev/ttyS3",
};

/*
 * Define command line options by filling structure:
 *  name    has_arg flag    val     type        argptr          help
*/
static myoption cmdlnopts[] = {
    // set 1 to param despite of its repeating number:
    {"help",    NO_ARGS,    NULL,   'h',    arg_int,    APTR(&help),        _("show this help")},
    {"speed",   NEED_ARG,   NULL,   's',    arg_int,    APTR(&G.speed),     _("baudrate (default: 9600)")},
    {"devname", NEED_ARG,   NULL,   'd',    arg_string, APTR(&G.ttyname),   _("serial device name")},
    {"raw",     NO_ARGS,    NULL,   'r',    arg_int,    APTR(&G.showraw),   _("show raw information from meteostation")},
    end_option
};

/**
 * Parse command line options and return dynamically allocated structure
 *      to global parameters
 * @param argc - copy of argc from main
 * @param argv - copy of argv from main
 * @return allocated structure with global parameters
 */
glob_pars *parse_args(int argc, char **argv){
    void *ptr = memcpy(&G, &Gdefault, sizeof(G)); assert(ptr);
    // format of help: "Usage: progname [args]\n"
    change_helpstring(_("Usage: %s [args]\n\n\tWhere args are:\n"));
    // parse arguments
    parseargs(&argc, &argv, cmdlnopts);
    if(help) showhelp(-1, cmdlnopts);
    if(argc > 0){
        WARNX("Wrong arguments:\n");
        for(int i = 0; i < argc; i++)
            fprintf(stderr, "\t%s\n", argv[i]);
        signals(9);
    }
    return &G;
}

