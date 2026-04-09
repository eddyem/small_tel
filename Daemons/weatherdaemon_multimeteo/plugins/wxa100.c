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

#include <math.h>
#include <string.h>

#include "weathlib.h"

#define SENSOR_NAME  "WXA100-06 ultrasonic meteostation"

// static const char *emultemplate = "0R0,S=1.9,D=217.2,P=787.7,T=10.8,H=69.0,R=31.0,Ri=0.0,Rs=Y";

enum{
    NWIND,
    NWINDDIR,
    NHUMIDITY,
    NAMB_TEMP,
    NPRESSURE,
    NPRECIP,
    NPRECIPLVL,
    NPRECIPINT,
    NAMOUNT
};

static const val_t values[NAMOUNT] = {
    [NWIND]     = {.sense = VAL_OBLIGATORY, .type = VALT_FLOAT, .meaning = IS_WIND},
    [NWINDDIR]  = {.sense = VAL_RECOMMENDED,.type = VALT_FLOAT, .meaning = IS_WINDDIR},
    [NHUMIDITY] = {.sense = VAL_OBLIGATORY, .type = VALT_FLOAT, .meaning = IS_HUMIDITY},
    [NAMB_TEMP] = {.sense = VAL_OBLIGATORY, .type = VALT_FLOAT, .meaning = IS_AMB_TEMP},
    [NPRESSURE] = {.sense = VAL_OBLIGATORY, .type = VALT_FLOAT, .meaning = IS_PRESSURE},
    [NPRECIP]   = {.sense = VAL_OBLIGATORY, .type = VALT_UINT,  .meaning = IS_PRECIP},
    [NPRECIPLVL]= {.sense = VAL_RECOMMENDED,.type = VALT_FLOAT, .meaning = IS_PRECIP_LEVEL},
    [NPRECIPINT]= {.sense = VAL_RECOMMENDED,.type = VALT_FLOAT, .meaning = IS_OTHER, .name = "PRECRATE", .comment = "Precipitation rate, mm/h"},
};

typedef struct{
    double windspeed;   // speed in m/s
    double winddir;     // direction from north
    double pressure;    // pressure in hPa
    double temperature; // outern temperature in degC
    double humidity;    // humidity in percents
    double rainfall;    // cumulative rain level (mm)
    double rainrate;    // rain rate, mm/h
    double israin;      // ==1 if it's raining
} weather_t;

typedef struct{
    const char *parname;    // parameter started value
    int isboolean;          // ==1 if answer Y/N
    int parlen;             // parameter length in bytes
    double *weatherpar;     // data target
} wpair_t;

static weather_t lastweather;

static const wpair_t wpairs[] = {
    {"S=", 0, 2, &lastweather.windspeed},
    {"D=", 0, 2, &lastweather.winddir},
    {"P=", 0, 2, &lastweather.pressure},
    {"T=", 0, 2, &lastweather.temperature},
    {"H=", 0, 2, &lastweather.humidity},
    {"R=", 0, 2, &lastweather.rainfall},
    {"Ri=",0, 3, &lastweather.rainrate},
    {"Rs=",1, 3, &lastweather.israin},
    {NULL, 0, 0, NULL}
};

static int parseans(char *str){
    int ncollected = 0;
    if(strncmp(str, "0R0,", 4)){
        WARNX("Wrong answer");
        LOGWARN("poll_device() get wrong answer: %s", str);
        return 0;
    }
    // init with NaNs
    const wpair_t *el = wpairs;
    while(el->parname){
        *el->weatherpar = NAN;
        ++el;
    }
    char *token = strtok(str, ",");
    while(token){
        el = wpairs;
        while(el->parname){
            if(strncmp(token, el->parname, el->parlen) == 0){ // found next parameter
                token += el->parlen;
                char *endptr;
                if(el->isboolean){
                    *el->weatherpar = (*token == 'Y') ? 1. : 0.;
                    ++ncollected;
                }else{
                    *el->weatherpar = strtod(token, &endptr);
                    if(endptr == token){
                        DBG("Wrong double value %s", token);
                    }else ++ncollected;
                }
                break;
            }
            ++el;
        }
        token = strtok(NULL, ",");
    }
    DBG("Got %d values", ncollected);
    return ncollected;
}

static void *mainthread(void *s){
    FNAME();
    char buf[BUFSIZ];
    time_t tpoll = 0;
    sensordata_t *sensor = (sensordata_t *)s;
    while(sensor->fdes > -1){
        time_t tnow = time(NULL);
        if(tnow - tpoll > sensor->tpoll){
            if(sl_tty_write(sensor->fdes, "!0R0\r\n", 6)){
                WARN("Can't ask new data");
                break;
            }
            DBG("poll @%zd, pollt=%zd", tnow, sensor->tpoll);
            tpoll = tnow;
        }
        int canread = sl_canread(sensor->fdes);
        if(canread < 0){
            WARNX("Disconnected fd %d", sensor->fdes);
            break;
        }else if(canread == 1){
            ssize_t got = read(sensor->fdes, buf, BUFSIZ);
            if(got > 0){
                sl_RB_write(sensor->ringbuffer, (uint8_t*)buf, got);
            }else if(got < 0){
                DBG("Disconnected?");
                break;
            }
        }
        if(sl_RB_datalen(sensor->ringbuffer) > BUFSIZ-1){
            WARNX("Overfull? Clear data from ring buffer");
            sl_RB_clearbuf(sensor->ringbuffer);
        }
        if(sl_RB_readline(sensor->ringbuffer, buf, BUFSIZ-1) > 0 && parseans(buf) > 0){
            tnow = time(NULL);
            pthread_mutex_lock(&sensor->valmutex);
            if(!isnan(lastweather.rainfall)){
                sensor->values[NPRECIPLVL].value.f = (float) lastweather.rainfall;
                sensor->values[NPRECIPLVL].time = tnow;
            }
            if(!isnan(lastweather.rainrate)){
                sensor->values[NPRECIPINT].value.f = (float) lastweather.rainrate;
                sensor->values[NPRECIPINT].time = tnow;
            }
            if(!isnan(lastweather.israin)){
                sensor->values[NPRECIP].value.u = (lastweather.israin > 0.) ? 1 : 0;
                sensor->values[NPRECIP].time = tnow;
            }
            if(!isnan(lastweather.temperature)){
                sensor->values[NAMB_TEMP].value.f = (float) lastweather.temperature;
                sensor->values[NAMB_TEMP].time = tnow;
            }
            if(!isnan(lastweather.windspeed)){
                sensor->values[NWIND].value.f = (float) lastweather.windspeed;
                sensor->values[NWIND].time = tnow;
            }
            if(!isnan(lastweather.winddir)){
                sensor->values[NWINDDIR].value.f = (float) lastweather.winddir;
                sensor->values[NWINDDIR].time = tnow;
            }
            if(!isnan(lastweather.pressure)){
                sensor->values[NPRESSURE].value.f = (float) (lastweather.pressure * 0.7500616); // mmHg instead of hPa!
                sensor->values[NPRESSURE].time = tnow;
            }
            if(!isnan(lastweather.humidity)){
                sensor->values[NHUMIDITY].value.f = (float) lastweather.humidity;
                sensor->values[NHUMIDITY].time = tnow;
            }
            pthread_mutex_unlock(&sensor->valmutex);
            if(sensor->freshdatahandler) sensor->freshdatahandler(sensor);
        }
    }
    sensor->kill(sensor);
    return NULL;
}

sensordata_t *sensor_new(int N, time_t pollt, int fd){
    FNAME();
    if(fd < 0) return NULL;
    sensordata_t *s = common_new();
    if(!s) return NULL;
    strncpy(s->name, SENSOR_NAME, NAME_LEN);
    s->PluginNo = N;
    s->fdes = fd;
    s->Nvalues = NAMOUNT;
    if(pollt) s->tpoll = pollt;
    s->values = MALLOC(val_t, NAMOUNT);
    for(int i = 0; i < NAMOUNT; ++i) s->values[i] = values[i];
    if(!(s->ringbuffer = sl_RB_new(BUFSIZ)) ||
        pthread_create(&s->thread, NULL, mainthread,  (void*)s)){
        s->kill(s);
        return NULL;
    }
    return s;
}

