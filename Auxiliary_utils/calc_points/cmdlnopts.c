/*
 * This file is part of the uniformdistr project.
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

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include "cmdlnopts.h"
#include "usefull_macros.h"

/*
 * here are global parameters initialisation
 */
static int help;

// default global parameters
static glob_pars G = {
    .delimeter = ":",
    .Npts = 100,
    .Zmax = 75.,
    .sortcoord = "Z",
};

/*
 * Define command line options by filling structure:
 *  name        has_arg     flag    val     type        argptr              help
*/
static sl_option_t cmdlnopts[] = {
// common options
    {"help",    NO_ARGS,    NULL,   'h',    arg_int,    APTR(&help),        "show this help"},
    {"delimeter",NEED_ARG,  NULL,   'd',    arg_string, APTR(&G.delimeter), "coordinates delimeter string (default: ':')"},
    {"output",  NEED_ARG,   NULL,   'o',    arg_string, APTR(&G.outfile),   "output file name"},
    {"npts",    NEED_ARG,   NULL,   'n',    arg_int,    APTR(&G.Npts),      "max amount of points (default: 100)"},
    {"minz",    NEED_ARG,   NULL,   'z',    arg_double, APTR(&G.Zmin),      "minimal Z (degrees) (default: 0.)"},
    {"maxz",    NEED_ARG,   NULL,   'Z',    arg_double, APTR(&G.Zmax),      "maximal Z (degrees) (default: 75.)"},
    {"sorting", NEED_ARG,   NULL,   's',    arg_string, APTR(&G.sorting),   "sorting order (none, positive, negative)"},
    {"scoord",  NEED_ARG,   NULL,   'c',    arg_string, APTR(&G.sortcoord), "sort by this coordinate (A, Z, HA, Dec) (default: Z)"},
    {"hideA",   NO_ARGS,    NULL,   '1',    arg_none,   APTR(&G.hide[0]),    "hide first column (A)"},
    {"hideZ",   NO_ARGS,    NULL,   '2',    arg_none,   APTR(&G.hide[1]),    "hide second column (Z)"},
    {"hideHA",  NO_ARGS,    NULL,   '3',    arg_none,   APTR(&G.hide[2]),    "hide third column (HA)"},
    {"hideDec", NO_ARGS,    NULL,   '4',    arg_none,   APTR(&G.hide[3]),    "hide fourth column (Dec)"},
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
    int i;
    size_t hlen = 1024;
    char helpstring[1024], *hptr = helpstring;
    snprintf(hptr, hlen, "Usage: %%s [args]\n\n\tWhere args are:\n");
    // format of help: "Usage: progname [args]\n"
    sl_helpstring(helpstring);
    // parse arguments
    sl_parseargs(&argc, &argv, cmdlnopts);
    if(help) sl_showhelp(-1, cmdlnopts);
    if(argc > 0){
        G.rest_pars_num = argc;
        G.rest_pars = MALLOC(char *, argc);
        for(i = 0; i < argc; i++)
            G.rest_pars[i] = strdup(argv[i]);
    }
    return &G;
}

