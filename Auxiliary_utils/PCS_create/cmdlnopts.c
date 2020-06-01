/*
 * This file is part of the ttyterm project.
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
     .pressure = 780.   // 580mmHg
    ,.temperature = 5.  // @5degrC
};

/*
 * Define command line options by filling structure:
 *  name    has_arg flag    val     type        argptr          help
*/
static myoption cmdlnopts[] = {
    // set 1 to param despite of its repeating number:
    {"help",    NO_ARGS,    NULL,   'h',    arg_int,    APTR(&help),        _("show this help")},
    {"10m",     NO_ARGS,    NULL,   't',    arg_int,    APTR(&G.for10m),    _("make output suitable for 10-micron mount")},
    {"header",  NO_ARGS,    NULL,   'H',    arg_int,    APTR(&G.printhdr),  _("print header for output file")},
    {"raindeg", NO_ARGS,    NULL,   'd',    arg_int,    APTR(&G.raindeg),   _("RA in degrees instead of hours")},
    {"strings", NO_ARGS,    NULL,   's',    arg_int,    APTR(&G.crdstrings),_("coordinates in string form")},
    {"ha",      NO_ARGS,    NULL,   0,      arg_int,    APTR(&G.ha),        _("print HA instead of RA")},
    {"stdeg",   NO_ARGS,    NULL,   0,      arg_int,    APTR(&G.stindegr),  _("sidereal time in degrees instead of hours")},
    {"delta",   NO_ARGS,    NULL,   'D',    arg_int,    APTR(&G.delta),     _("show delta: apparent-encoder instead of apparent coordinates")},
    {"pressure",NEED_ARG,   NULL,   'P',    arg_double, APTR(&G.pressure),  _("atmospheric pressure (hPa)")},
    {"pinmm",   NO_ARGS,    NULL,   'm',    arg_int,    APTR(&G.pmm),       _("pressure in mmHg instead of hPa")},
    {"temperature",NEED_ARG,NULL,   'T',    arg_double, APTR(&G.temperature),_("temperature, degrC")},
    {"horcoords",NO_ARGS,   NULL,   'A',    arg_int,    APTR(&G.horcoords), _("show horizontal coordinates instead of equatorial")},
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
    change_helpstring(_("Usage: %s [args] FITS_files\nMake PCS list for equatorial mount\n\tWhere args are:\n"));
    // parse arguments
    parseargs(&argc, &argv, cmdlnopts);
    if(help) showhelp(-1, cmdlnopts);
    G.nfiles = argc;
    G.infiles = MALLOC(char*, argc);
    for(int i = 0; i < argc; i++){
        G.infiles[i] = strdup(argv[i]);
    }
    return &G;
}

