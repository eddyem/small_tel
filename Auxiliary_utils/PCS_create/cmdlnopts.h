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

#pragma once
#ifndef CMDLNOPTS_H__
#define CMDLNOPTS_H__

/*
 * here are some typedef's for global data
 */
typedef struct{
    int for10m;             // output suitable for 10-micron mount
    int printhdr;           // print header
    int raindeg;            // RA in degrees
    int crdstrings;         // coordinates in string form
    int ha;                 // print HA instead of RA
    int stindegr;           // sidereal time in degrees instead of hours
    int delta;              // show delta: apparent-encoder instead of apparent coordinates
    double pressure;        // atmospheric pressure (HPa or mmHg if pmm==1)
    int pmm;                // pressure in mmHg
    double temperature;     // temperature, degrC
    int nfiles;             // amount of input files (amount of free arguments)
    char **infiles;         // input file[s] name[s]
} glob_pars;

glob_pars *parse_args(int argc, char **argv);
#endif // CMDLNOPTS_H__
