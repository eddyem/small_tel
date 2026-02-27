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

#include <pthread.h>
#include <signal.h>
#include <usefull_macros.h>

#include "weathlib.h"

#define NS (6)

extern sensordata_t sensor;

static void (*freshdatahandler)(const struct sensordata_t* const) = NULL; // do nothing with fresh data
static pthread_t thread;

static val_t values[NS] = { // fields `name` and `comment` have no sense until value meaning is `IS_OTHER`
    {.name = "WIND", .comment = "wind speed, m/s", .sense = VAL_OBLIGATORY, .type = VALT_FLOAT, .meaning = IS_WIND},
    {.name = "WINDDIR", .comment = "wind direction azimuth (from south over west), deg", .sense = VAL_RECOMMENDED, .type = VALT_FLOAT, .meaning = IS_WINDDIR},
    {.name = "EXTTEMP", .comment = "external temperature, degC", .sense = VAL_OBLIGATORY, .type = VALT_FLOAT, .meaning = IS_AMB_TEMP},
    {.name = "PRESSURE", .comment = "atmospheric pressure, hPa", .sense = VAL_RECOMMENDED, .type = VALT_FLOAT, .meaning = IS_PRESSURE},
    {.name = "HUMIDITY", .comment = "air relative humidity, %%", .sense = VAL_RECOMMENDED, .type = VALT_FLOAT, .meaning = IS_HUMIDITY},
    {.name = "PRECIP", .comment = "precipitations flag (0 - no, 1 - yes)", .sense = VAL_OBLIGATORY, .type = VALT_UINT, .meaning = IS_PRECIP},
};

void *mainthread(void _U_ *U){
    FNAME();
    double t0 = sl_dtime();
    while(1){
        float f = values[0].value.f + (drand48() - 0.5) / 2.;
        if(f >= 0.) values[0].value.f = f;
        f = values[1].value.f + (drand48() - 0.5) * 4.;
        if(f > 160. && f < 200.) values[1].value.f = f;
        f = values[2].value.f + (drand48() - 0.5) / 20.;
        if(f > 13. && f < 21.) values[2].value.f = f;
        f = values[3].value.f + (drand48() - 0.5) / 100.;
        if(f > 585. && f < 615.) values[3].value.f = f;
        f = values[4].value.f + (drand48() - 0.5) / 10.;
        if(f > 60. && f <= 100.) values[4].value.f = f;
        values[5].value.u = (f > 98.) ? 1 : 0;
        time_t cur = time(NULL);
        for(int i = 0; i < NS; ++i) values[i].time = cur;
        if(freshdatahandler) freshdatahandler(&sensor);
        while(sl_dtime() - t0 < 1.) usleep(500);
        t0 = sl_dtime();
    }
    return NULL;
}

static int init(int N){
    FNAME();
    values[0].value.f = 1.;
    values[1].value.f = 180.;
    values[2].value.f = 17.;
    values[3].value.f = 600.;
    values[4].value.f = 80.;
    values[5].value.u = 0;
    if(pthread_create(&thread, NULL, mainthread, NULL)) return 0;
    sensor.PluginNo = N;
    return NS;
}

static int onrefresh(void (*handler)(const struct sensordata_t* const)){
    FNAME();
    if(!handler) return FALSE;
    freshdatahandler = handler;
    return TRUE;
}

static void die(){
    FNAME();
    if(0 == pthread_kill(thread, 9)){
        DBG("Killed, join");
        pthread_join(thread, NULL);
        DBG("Done");
    }
}

/**
 * @brief getval - value's getter
 * @param o (o) - value
 * @param N - it's index
 * @return FALSE if failed
 */
static int getval(val_t *o, int N){
    if(N < 0 || N >= NS) return FALSE;
    if(o) *o = values[N];
    return TRUE;
}

sensordata_t sensor = {
    .name = "Dummy weatherstation",
    .Nvalues = NS,
    .init = init,
    .onrefresh = onrefresh,
    .get_value = getval,
    .die = die,
};
