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

// SAO coordinates: longitude 41degr 26' 29.175'', latitude 43degr 39' 12.7''

#define LONG_OBS        41.44143375
#define LAT_OBS         43.6535278
#define COS_LAT_OBS     0.723527278
#define SIN_LAT_OBS     0.690295790

typedef struct{
    double A;
    double Z;
    double HA;
    double Dec;
} point;


point *getPoints(int *N);
int set_sorting(char *param, char *coord);
int set_Zlimits(double minz, double maxz);
void show_sorting_help();
