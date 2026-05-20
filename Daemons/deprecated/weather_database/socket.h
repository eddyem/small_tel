/*
 * This file is part of the sqlite project.
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

// timeout of answer waiting - 1s
#define ANS_TIMEOUT         (1.)
// get data each 1 minute
#define POLLING_INTERVAL    (60.)
// command to get stat for last 60s
#define SERVER_COMMAND      "statsimple60"

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

int open_socket(const char *server, const char *port);
void run_socket(int fd);
