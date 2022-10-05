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

#pragma once

/*
 * here are some typedef's for global data
 */
typedef struct{
    char *outfile;          // output file name
    char *delimeter;        // coordinates delimeter: HH:MM:SS.S, HH/MM/SS.S etc
    char *sorting;          // soring function (none, increasing, decreasing)
    char *sortcoord;        // coordinate to sort by
    int rest_pars_num;      // number of rest parameters
    int Npts;               // max amount of points
    int hide[4];            // hide n-th column
    double Zmin;            // minZ (degr)
    double Zmax;            // maxZ (degr)
    char** rest_pars;       // the rest parameters: array of char*
} glob_pars;


glob_pars *parse_args(int argc, char **argv);

