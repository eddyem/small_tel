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

#include <pthread.h>
#include <signal.h> // pthread_kill
#include <string.h>
#include <time.h>
#include <usefull_macros.h>

#include "weathlib.h"

// dummy example of file descriptors usage

#define NS (4)

static time_t Tpoll = 10;
extern sensordata_t sensor;
static int fdes = -1;
static sl_ringbuffer_t *rb;
static pthread_t thread;
static void (*freshdatahandler)(const struct sensordata_t* const) = NULL;
static void die();

static val_t values[NS] = { // fields `name` and `comment` have no sense until value meaning is `IS_OTHER`
    {.name = "WIND",     .sense = VAL_OBLIGATORY,  .type = VALT_FLOAT, .meaning = IS_WIND},
    {.name = "EXTTEMP",  .sense = VAL_OBLIGATORY,  .type = VALT_FLOAT, .meaning = IS_AMB_TEMP},
    {.name = "PRESSURE", .sense = VAL_RECOMMENDED, .type = VALT_FLOAT, .meaning = IS_PRESSURE},
    {.name = "HUMIDITY", .sense = VAL_RECOMMENDED, .type = VALT_FLOAT, .meaning = IS_HUMIDITY},
};

static int format_values(char *buf){
    int gotvals = 0;
    char *token = strtok(buf, ",");
    time_t tnow = time(NULL);
    while(token && gotvals < NS){
        double v;
        DBG("TOKEN: %s", token);
        if(sl_str2d(&v, token)){
            DBG("next value: %g", v);
            values[gotvals].value.f = (float) v;
            values[gotvals].time = tnow;
            ++gotvals;
        }
        token = strtok(NULL, ",");
    }
    DBG("GOT: %d", gotvals);
    return gotvals;
}

static ssize_t writedata(int fd, const char *str, size_t size){
    ssize_t sent = 0;
    do{
        DBG("try to write %zd bytes", size);
        int canwrite = sl_canwrite(fd);
        if(canwrite < 0){
            WARNX("Disconnected?!");
            return -1;
        }else if(canwrite){
            ssize_t r = write(fd, str+sent, size);
            if(r < 0){
                sent = -1;
                WARNX("Disconnected??");
                break;
            }else{
                sent += r;
                size -= r;
            }
            DBG("sent %zd bytes; total send %zd, leave %zd", r, sent, size);
        }
    }while(size);
    return sent;
}

static void *mainthread(void _U_ *U){
    FNAME();
    time_t task = 0;
    const char begging[] = "Enter comma-separated data: wind, exttemp, pressure, humidity\n";
    char buf[128];
    while(fdes > -1){
        time_t tnow = time(NULL);
        int canread = sl_canread(fdes);
        if(canread < 0){
            WARNX("Disconnected fd %d", fdes);
            break;
        }else if(canread == 1){
            ssize_t got = read(fdes, buf, 128);
            if(got > 0){
                sl_RB_write(rb, (uint8_t*)buf, got);
            }else if(got < 0){
                DBG("Disconnected?");
                break;
            }
        }
        if(sl_RB_readline(rb, buf, 127) > 0){
            if(NS == format_values(buf) && freshdatahandler)
                freshdatahandler(&sensor);
        }
        if(tnow >= task){
            DBG("write %s", begging);
            ssize_t got = writedata(fdes, begging, sizeof(begging)-1);
            if(got > 0) task = tnow + Tpoll;
            else if(got < 0){
                close(fdes);
                fdes = -1;
            }
        }
    }
    DBG("OOOOps!");
    return NULL;
}

static int init(int N, time_t pollt, int fd){
    FNAME();
    if(!(rb = sl_RB_new(BUFSIZ))){
        WARNX("Can't init ringbuffer!");
        return 0;
    }
    if(pthread_create(&thread, NULL, mainthread, NULL)) return 0;
    sensor.PluginNo = N;
    if(pollt) Tpoll = pollt;
    fdes = fd;
    if(fdes < 0) return -1;
    return NS;
}

static int onrefresh(void (*handler)(const struct sensordata_t* const)){
    FNAME();
    if(!handler) return FALSE;
    freshdatahandler = handler;
    return TRUE;
}


static int getval(val_t *o, int N){
    if(N < 0 || N >= NS) return FALSE;
    if(o) *o = values[N];
    return TRUE;
}

static void die(){
    FNAME();
    if(0 == pthread_kill(thread, -9)){
        DBG("Killed, join");
        pthread_join(thread, NULL);
        DBG("Done");
    }
    DBG("Delete RB");
    sl_RB_delete(&rb);
    if(fdes > -1){
        close(fdes);
        DBG("FD closed");
    }
    DBG("FDEXAMPLE: died");
}

sensordata_t sensor = {
    .name = "Dummy socket or serial device weatherstation",
    .Nvalues = NS,
    .init = init,
    .onrefresh = onrefresh,
    .get_value = getval,
    .die = die,
};
