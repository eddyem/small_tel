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

// collect here weather from all weatherstations sorted by importance

#include <usefull_macros.h>

#include "mainweather.h"
#include "sensors.h"
#include "weathlib.h"

// wind speed history array size (not less than for one hour)
#define MAX_HISTORY     3600
// throw out data older than 24 hours
#define TOO_OLD_DATA    86400
// one hour
#define T_ONE_HOUR      3600

static pthread_mutex_t datamutex = PTHREAD_MUTEX_INITIALIZER;
static int Forbidden = 0;

// index of meteodata in array
enum{
    NWIND,
    NWINDMAX,
    NWINDMAX1,
    NWINDDIR,
    NWINDDIR1,
    NWINDDIR2,
    NHUMIDITY,
    NAMB_TEMP,
    NPRESSURE,
    NPRECIP,
    NPRECIP_LEVEL,
    NMIST,
    NCLOUDS,
    NSKYTEMP,
    NCOMMWEATH,
    NLASTAHTUNG,
    NAMOUNT_OF_DATA
};

// starting sense values are VAL_BROKEN except of calculated values
// they would be changed later in `fix_new_data` to lowest level
static val_t collected_data[NAMOUNT_OF_DATA] = {
    [NWIND] = {.sense = VAL_BROKEN, .type = VALT_FLOAT, .meaning = IS_WIND},
    [NWINDMAX] = {.sense = VAL_RECOMMENDED, .type = VALT_FLOAT, .meaning = IS_OTHER, .name = "WINDMAX", .comment = "Maximal wind speed for last 24 hours"},
    [NWINDMAX1] = {.sense = VAL_RECOMMENDED, .type = VALT_FLOAT, .meaning = IS_OTHER, .name = "WINDMAX1", .comment = "Maximal wind speed for last hour"},
    [NWINDDIR] = {.sense = VAL_BROKEN, .type = VALT_FLOAT, .meaning = IS_WINDDIR},
    [NWINDDIR1] = {.sense = VAL_RECOMMENDED, .type = VALT_FLOAT, .meaning = IS_OTHER, .name = "WINDDIR1", .comment = "Mean wind speed direction for last hour"},
    [NWINDDIR2] = {.sense = VAL_RECOMMENDED, .type = VALT_FLOAT, .meaning = IS_OTHER, .name = "WINDDIR2", .comment = "Mean wind speed^2 direction for last hour"},
    [NHUMIDITY] = {.sense = VAL_BROKEN, .type = VALT_FLOAT, .meaning = IS_HUMIDITY},
    [NAMB_TEMP] = {.sense = VAL_BROKEN, .type = VALT_FLOAT, .meaning = IS_AMB_TEMP},
    [NPRESSURE] = {.sense = VAL_BROKEN, .type = VALT_FLOAT, .meaning = IS_PRESSURE},
    [NPRECIP] = {.sense = VAL_BROKEN, .type = VALT_UINT, .meaning = IS_PRECIP},
    [NPRECIP_LEVEL] = {.sense = VAL_BROKEN, .type = VALT_FLOAT,   .meaning = IS_PRECIP_LEVEL},
    [NMIST] = {.sense = VAL_BROKEN, .type = VALT_UINT,   .meaning = IS_MIST},
    [NCLOUDS] = {.sense = VAL_BROKEN, .type = VALT_FLOAT,   .meaning = IS_CLOUDS},
    [NSKYTEMP] = {.sense = VAL_BROKEN, .type = VALT_FLOAT, .meaning = IS_SKYTEMP},
    // these are calculated values
    [NCOMMWEATH] = {.value.i = 0, .sense = VAL_OBLIGATORY, .type = VALT_UINT,   .meaning = IS_OTHER, .name = "WEATHER", .comment = "Weather level (0 - good, 3 - obs. prohibited)"},
    [NLASTAHTUNG] = {.value.i = 0, .sense = VAL_RECOMMENDED, .type = VALT_UINT, .meaning = IS_OTHER, .name = "EVTTIME", .comment = "UNIX-time of last weather level increasing"},
//    {.sense = VAL_OBLIGATORY, .type = VALT_FLOAT, .meaning = IS_OTHER},
};

typedef struct{
    double array[MAX_HISTORY];
    double sum;
} sumval_t;

typedef struct{
    int write_idx;
    sumval_t C;        // (speed * cos(dir))
    sumval_t C2;       // (speed^2 * cos(dir))
    sumval_t S;        // (speed * sin(dir))
    sumval_t S2;       // (speed^2 * sin(dir))
} meanwinddir_t;

static meanwinddir_t winddirs = {0};

static double update_sum(sumval_t *sv, double newval, int oldidx){
    double old = sv->array[oldidx];
    sv->array[oldidx] = newval;
    return (sv->sum += newval - old);
}

// add current value into floating array and recalculate mean speeds
// data mutex should be locked outside this function
static void wind_dir_add(double curspeed, double curdir, double *dir, double *dir2){
    double C, S;
    sincos(curdir * M_PI / 180., &S, &C);
    double vS = curspeed * S, vC = curspeed * C, v2S = curspeed * vS, v2C = curspeed * vC;
    int idx = winddirs.write_idx;
    winddirs.write_idx = (idx + 1) % MAX_HISTORY;
    // calculate sums
    vS  = update_sum(&winddirs.S, vS, idx);
    vC  = update_sum(&winddirs.C, vC, idx);
    v2S = update_sum(&winddirs.S2, v2S, idx);
    v2C = update_sum(&winddirs.C2, v2C, idx);
    *dir = atan2(vS, vC) * 180. / M_PI;
    *dir2 = atan2(v2S, v2C) * 180. / M_PI;
    if(*dir < 0.) *dir += 360.;
    if(*dir2 < 0.) *dir2 += 360.;

}

typedef struct{
    double speeds[MAX_HISTORY];
    time_t timestamps[MAX_HISTORY];
    int write_idx;          // index in `speeds` and `timestamps` for new value

    int deq[MAX_HISTORY];   // array of indexes in queue
    int deq_head, deq_tail; // queue's head and tail
} sliding_max_t;

static sliding_max_t windspeeds = {0};

static void add_windspeed(sliding_max_t *sm, double speed, time_t now) {
    // Write new data portion into queue
    int idx = sm->write_idx;
    sm->speeds[idx] = speed;
    sm->timestamps[idx] = now;
    sm->write_idx = (idx + 1) % MAX_HISTORY;

    // Remove values older than `TOO_OLD_DATA`
    time_t cutoff = now - TOO_OLD_DATA;
    while(sm->deq_head != sm->deq_tail && sm->timestamps[sm->deq[sm->deq_head]] < cutoff){
        sm->deq_head = (sm->deq_head + 1) % MAX_HISTORY;
    }

    // Remove small values less than current
    while(sm->deq_head != sm->deq_tail && sm->speeds[sm->deq[(sm->deq_tail - 1 + MAX_HISTORY) % MAX_HISTORY]] <= speed){
        sm->deq_tail = (sm->deq_tail - 1 + MAX_HISTORY) % MAX_HISTORY;
    }

    // Add new index into queue
    sm->deq[sm->deq_tail] = idx;
    sm->deq_tail = (sm->deq_tail + 1) % MAX_HISTORY;
}

static double get_current_max(sliding_max_t *sm){
    if(sm->deq_head == sm->deq_tail) return 0.0; // No data
    return sm->speeds[sm->deq[sm->deq_head]];
}

static double get_max_forT(sliding_max_t *sm, time_t tcutoff){
    if(sm->deq_head == sm->deq_tail) return 0.0; // No data
    int idx = sm->deq_head;
    while(idx != sm->deq_tail && sm->timestamps[sm->deq[sm->deq_head]] < tcutoff){
        idx = (idx + 1) % MAX_HISTORY;
    }
    if(idx == sm->deq_tail) return 0.0; // No fresh data
    return sm->speeds[sm->deq[idx]];
}

int collected_amount(){
    return NAMOUNT_OF_DATA;
}

int get_collected(val_t *val, int N){
    if(!val || N < 0 || N >= NAMOUNT_OF_DATA){
        DBG("Wrong number (%d) requested or no place for data", N);
        return FALSE;
    }
    pthread_mutex_lock(&datamutex);
    DBG("Copied data of %d", N);
    *val = collected_data[N];
    pthread_mutex_unlock(&datamutex);
    return TRUE;
}

// take only data with `sense value` less than collected have
static void fix_new_data(val_t *collected, val_t *fresh){
    if(!collected || !fresh) return;
    if(collected->time > fresh->time) return;
    // lower `collected` level if data is too old
    if(fresh->time - collected->time > 60) collected->sense = VAL_UNNECESSARY;
    if(collected->sense < fresh->sense) return;
    if(collected->sense != fresh->sense) collected->sense = fresh->sense; // take new lower level
    //DBG("Refresh collected value");
    collected->time = fresh->time;
    if(collected->type == fresh->type){ // good case
        //DBG("Types are the same");
        collected->value = fresh->value;
        return;
    }
    // bad case: have different types
    switch(collected->type){
    case VALT_UINT:
        switch(fresh->type){
        case VALT_INT:
            collected->value.u = (uint32_t) fresh->value.i;
            //DBG("i->u");
            break;
        case VALT_FLOAT:
            collected->value.u = (uint32_t) fresh->value.f;
            //DBG("f->u");
        default: break;
        }
        break;
    case VALT_INT:
        switch(fresh->type){
        case VALT_UINT:
            collected->value.i = (int32_t) fresh->value.u;
            //DBG("u->i");
            break;
        case VALT_FLOAT:
            collected->value.i = (int32_t) fresh->value.f;
            //DBG("f->i");
        default: break;
        }
        break;
    case VALT_FLOAT:
        switch(fresh->type){
        case VALT_UINT:
            collected->value.f = (float) fresh->value.u;
            //DBG("u->f");
            break;
        case VALT_INT:
            collected->value.f = (float) fresh->value.i;
            //DBG("i->f");
        default: break;
        }
        break;
    }
}

static void chkweatherlevel(int *curlevel, double curvalue, weather_cond_t *curcond, int *ahtungtime){
    double good = curcond->good, bad = curcond->bad, terrible = curcond->terrible;
    if(curcond->negflag){ // negate
        curvalue = -curvalue;
        good = -good;
        bad = -bad;
        terrible = -terrible;
    }
    int newlevel = -1;
    if(curvalue > terrible) newlevel = WEATHER_TERRIBLE;
    else if(curvalue > bad) newlevel = WEATHER_BAD;
    else if(curvalue < good) newlevel = WEATHER_GOOD;
    if(newlevel == -1) return;
    time_t curt = time(NULL);
    if(newlevel > *curlevel){
        DBG("newlevel: %d, current: %d  INCREASED", newlevel, *curlevel);
        *curlevel = newlevel;
        if(*ahtungtime < curt) *ahtungtime = (int) curt; // refresh event time
    }else if(newlevel < *curlevel){ // check timeout to make level lower
        if(curt - *ahtungtime > WeatherConf.ahtung_delay){
            DBG("newlevel: %d, current: %d  DECREASED", newlevel, *curlevel);
            *curlevel = newlevel;
        }
    }
}

void refresh_sensval(sensordata_t *s){
    //FNAME();
    static time_t poll_time = 0;
    val_t value;
    if(!s || !s->get_value) return;
    if(poll_time == 0) poll_time = get_pollT();
    int curlevel = collected_data[NCOMMWEATH].value.i;
    int curahtungtime = collected_data[NLASTAHTUNG].value.i;
    time_t curtime = time(NULL);
    double dir = -100., dir2 = -100.; // mean wind directions
    //DBG("%d meteo values", s->Nvalues);
    for(int i = 0; i < s->Nvalues; ++i){
        //DBG("\nTry to get %dth value", i);
        if(!s->get_value(s, &value, i) || value.sense > VAL_RECOMMENDED) continue;
        //DBG("got value");
        int idx = -1;
        double curvalue;
        weather_cond_t *curcond = NULL;
        switch(value.meaning){
            case IS_WIND:
                idx = NWIND;
                curvalue = (double) value.value.f;
                curcond = &WeatherConf.wind;
                add_windspeed(&windspeeds, curvalue, curtime);
                break;
            case IS_WINDDIR:
                idx = NWINDDIR;
                wind_dir_add(collected_data[NWIND].value.f, value.value.f, &dir, &dir2);
                break;
            case IS_HUMIDITY:
                idx = NHUMIDITY;
                curvalue = (double) value.value.f;
                curcond = &WeatherConf.humidity;
                break;
            case IS_AMB_TEMP:
                idx = NAMB_TEMP;
                break;
            case IS_PRESSURE:
                idx = NPRESSURE;
                break;
            case IS_PRECIP:
                idx = NPRECIP;
                if(value.value.i && curlevel < WEATHER_TERRIBLE){
                    curlevel = WEATHER_TERRIBLE;
                    curahtungtime = curtime;
                }
                break;
            case IS_PRECIP_LEVEL:
                idx = NPRECIP_LEVEL;
                break;
            case IS_MIST:
                idx = NMIST;
                if(value.value.i && curlevel < WEATHER_TERRIBLE){
                    curahtungtime = curtime;
                    curlevel = WEATHER_TERRIBLE;
                }
                break;
            case IS_CLOUDS:
                idx = NCLOUDS;
                curvalue = (double) value.value.f;
                curcond = &WeatherConf.clouds;
                break;
            case IS_SKYTEMP:
                idx = NSKYTEMP;
                curvalue = (double) value.value.f;
                curcond = &WeatherConf.sky;
                break;
            default : break;
        }
        if(idx < 0 || idx >= NAMOUNT_OF_DATA) continue;
        //DBG("IDX=%d", idx);
        pthread_mutex_lock(&datamutex);
        fix_new_data(&collected_data[idx], &value);
        pthread_mutex_unlock(&datamutex);
        if(!Forbidden && curcond) chkweatherlevel(&curlevel, curvalue, curcond, &curahtungtime);
    }
    pthread_mutex_lock(&datamutex);
    // refresh max
    if(dir >= 0.){
        collected_data[NWINDDIR1].value.f = (float) dir;
        collected_data[NWINDDIR1].time = curtime;
    }
    if(dir2 >= 0.){
        collected_data[NWINDDIR2].value.f = (float) dir2;
        collected_data[NWINDDIR2].time = curtime;
    }
    collected_data[NWINDMAX].value.f = (float) get_current_max(&windspeeds);
    collected_data[NWINDMAX].time = curtime;
    collected_data[NWINDMAX1].value.f = (float) get_max_forT(&windspeeds, curtime - T_ONE_HOUR);
    collected_data[NWINDMAX1].time = curtime;
    //DBG("check ahtung");
    if(Forbidden) collected_data[NCOMMWEATH].value.i = WEATHER_PROHIBITED;
    else collected_data[NCOMMWEATH].value.i = curlevel;
    if(collected_data[NLASTAHTUNG].value.i < curahtungtime) collected_data[NLASTAHTUNG].value.i = curahtungtime;
    collected_data[NCOMMWEATH].time = curtime;
    collected_data[NLASTAHTUNG].time = curtime;
    pthread_mutex_unlock(&datamutex);
    //DBG("Refreshed");
}

void forbid_observations(int f){
    if(f) Forbidden = 1;
    else Forbidden = 0;
}

#if 0
// main cycle
void run_mainweather(){
    int N = get_nplugins();
    if(N < 1) return;
    poll_time = get_pollT();
    while(1){
        int nactive = 0;
        pthread_mutex_lock(&datamutex);
        for(int i = N-1; i > -1; --i){ // the most important is the last
            sensordata_t *s = get_plugin(i);
            if(!s || !sensor_alive(s)) continue;
            ++nactive;
        }
        pthread_mutex_unlock(&datamutex);
        if(nactive == 0) break; // no active sensors
        usleep(10000);
    }
    LOGERR("Main weather collector died: all sensors lost");
}
#endif
