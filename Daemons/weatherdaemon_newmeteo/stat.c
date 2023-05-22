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

#include <math.h>
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <usefull_macros.h>

#include "stat.h"

// add BUFSZ_INCR records to buffer each time when no free space left
#define BUFSZ_INCR  (2048)
static weather_t *buf = NULL;
// current size of `buf`
static size_t buflen = 0;
// indexes of first and last element in buffer
static size_t firstidx = 0, lastidx = 0;
// maximal current time delta between last and first items of `buf`
static double tdiffmax = 0.;

// mutex for working with buffer
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

double get_tmax(){
    return tdiffmax;
}

// add new record to buffer
void addtobuf(weather_t *record){
    if(!record) return;
    pthread_mutex_lock(&mutex);
    if(!buf){ // first run
        buf = MALLOC(weather_t, BUFSZ_INCR);
        buflen = BUFSZ_INCR;
        memcpy(&buf[0], record, sizeof(weather_t));
        //DBG("init buff to %zd", buflen);
        pthread_mutex_unlock(&mutex);
        return;
    }
    if(++lastidx == buflen){ // end of buffer reached: decide wether to increase buffer or not
        if(tdiffmax < STATMAXT){
            buflen += BUFSZ_INCR;
            buf = realloc(buf, sizeof(weather_t)*buflen);
            DBG("realloc buf to %zd", buflen);
        }else lastidx = 0;
    }
    if(lastidx == firstidx){
        if(++firstidx == buflen) firstidx = 0;
    }
    memcpy(&buf[lastidx], record, sizeof(weather_t));
    tdiffmax = buf[lastidx].tmeasure - buf[firstidx].tmeasure;
    //DBG("add record, last=%zd, first=%zd, dT=%.3f", lastidx, firstidx, tdiffmax);
    pthread_mutex_unlock(&mutex);
}

// get statistics for last `Tsec` seconds; @return real dT for given interval
double stat_for(double Tsec/*, weather_t *w*/){
    double dt = 0., tlast = buf[lastidx].tmeasure;
    size_t startfrom = lastidx;
    pthread_mutex_lock(&mutex);
    if(tdiffmax <= Tsec) startfrom = firstidx; // use all data
    else while(dt < Tsec && startfrom != firstidx){ // search from which index we should start
        if(startfrom == 0) startfrom = buflen - 1;
        else --startfrom;
    }
    dt = tlast - buf[startfrom].tmeasure;
    DBG("got indexes: start=%zd, end=%zd, dt=%.2f", startfrom, lastidx, dt);
    weather_t min = {0}, max = {0}, sum = {0}, sum2 = {0};
    size_t amount = 0;
    memcpy(&min, &buf[lastidx], sizeof(weather_t));
    while(startfrom != lastidx){
        weather_t *curw = &buf[startfrom];
#define CHK(field)  do{register double d = curw->field; if(d > max.field) max.field = d; else if(d < min.field) min.field = d; \
                        sum.field += d; sum2.field += d*d;}while(0)
        CHK(windspeed);
        CHK(pressure);
        CHK(temperature);
        CHK(humidity);
        CHK(rainfall);
        ++amount;
        if(++startfrom == buflen) startfrom = 0;
    }
    DBG("Got %zd records", amount);
    double wmean = sum.windspeed/amount;
    DBG("wind min/max/mean/rms=%.1f/%.1f/%.1f/%.1f", min.windspeed, max.windspeed,
        wmean, sqrt(sum2.windspeed/amount - wmean*wmean));
    pthread_mutex_unlock(&mutex);
    return dt;
}
