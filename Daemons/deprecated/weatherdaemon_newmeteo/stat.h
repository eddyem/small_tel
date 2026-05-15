/*
 * This file is part of the weatherdaemon project.
 * Copyright 2023 Edward V. Emelianov <edward.emelianoff@gmail.com>.
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

#include "term.h"

// maximal time difference for records in statbuf - one hour
#define STATMAXT    (3600.)

// main statistical data
typedef struct{
    double min;
    double max;
    double mean;
    double rms;
} stat_t;

typedef struct{
    stat_t windspeed;
    stat_t winddir;
    stat_t pressure;
    stat_t temperature;
    stat_t humidity;
    stat_t rainfall;
    stat_t tmeasure;
} weatherstat_t;

double stat_for(double Tsec, weatherstat_t *wstat);
void addtobuf(weather_t *record);
double get_tmax();
