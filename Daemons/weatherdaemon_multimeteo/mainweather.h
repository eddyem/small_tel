/*
 * This file is part of the weatherdaemon project.
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

#pragma once

#include "weathlib.h"

// weather conditions
enum{
    WEATHER_GOOD,           // good to start observations
    WEATHER_BAD,            // bad for start, but can run
    WEATHER_TERRIBLE,       // need close the dome
    WEATHER_PROHIBITED,     // need close all, park and power off <-- by SIGUSR1/SIGUSR2
};

typedef struct{
    double good;        // if value less than this, weather is good
    double bad;         // if value greater than this, weather is bad
    double terrible;    // if value greater than this, weather is terrible
    int negflag;        // reversal flag (good if > val,  etc)
} weather_cond_t;

typedef struct{
    int ahtung_delay;       // delay to change "bad weather" to good after last "bad event"
    // wind, m/s
    weather_cond_t wind;
    // humidity, %%
    weather_cond_t humidity;
    // "clouds", > 2500 - OK -> should be negated when check!!!
    weather_cond_t clouds;
    // sky temperature minus ambient temperature, degC
    weather_cond_t sky;
} weather_conf_t;

// defined in cmdlnopts.c
extern weather_conf_t WeatherConf;

int collected_amount();
int get_collected(val_t *val, int N);

void forbid_observations(int f);
void refresh_sensval(sensordata_t *s);
//void run_mainweather();
