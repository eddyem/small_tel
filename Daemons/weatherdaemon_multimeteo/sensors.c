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

#include <dlfcn.h>
#include <string.h>
#include <usefull_macros.h>

#include "mainweather.h"
#include "sensors.h"
#include "weathlib.h"

#define WARNXL(...) do{ LOGWARN(__VA_ARGS__); WARNX(__VA_ARGS__); } while(0)
#define WARNL(...) do{ LOGWARN(__VA_ARGS__); WARN(__VA_ARGS__); } while(0)
#define ERRXL(...) do{ LOGERR(__VA_ARGS__); ERRX(__VA_ARGS__); } while(0)
#define ERRL(...) do{ LOGERR(__VA_ARGS__); ERR(__VA_ARGS__); } while(0)

// poll each `poll_interval` seconds
static time_t poll_interval = 15;

static int nplugins = 0;
static sensordata_t **allplugins = NULL;

int get_nplugins(){
    return nplugins;
}

// set polling interval
int set_pollT(time_t t){
    if(t == 0 || t > MAX_POLLT) return FALSE;
    poll_interval = t;
    return TRUE;
}

time_t get_pollT(){ return poll_interval;}

/**
 * @brief get_plugin - get link to opened plugin
 * @param o (o) - plugin with given index
 * @param N - index in `allplugins`
 * @return NULL if failed or pointer
 */
sensordata_t *get_plugin(int N){
    if(N < 0 || N >= nplugins) return NULL;
    return allplugins[N];
}

// TODO: fix for usage with several identical meteostations
void *open_plugin(const char *name){
    DBG("try to open lib %s", name);
    void* dlh = dlopen(name, RTLD_NOW | RTLD_NOLOAD); // library may be already opened
    if(!dlh){
        DBG("Not loaded - load");
        dlh = dlopen(name, RTLD_NOW);
    }else{
        WARNX("Library %s already opened", name);
        //return NULL;
    }
    if(!dlh){
        char *e = dlerror();
        WARNXL("Can't find plugin! %s", (e) ? e : "");
        return NULL;
    }
    return dlh;
}

/**
 * @brief dumpsensors - this function called each time some `station` got new data
 *
 * @param station - pointer to N'th station opened
 */
static void dumpsensors(struct sensordata_t* station){
    //FNAME();
    if(!station || !station->get_value || station->Nvalues < 1 || station->IsMuted) return;
    refresh_sensval(station);
#if 0
    DBG("New values...");
#ifdef EBUG
    char buf[FULL_LEN+1];
    uint64_t Tsum = 0; int nsum = 0;
    int N = (nplugins > 1) ? station->PluginNo : -1;
    time_t oldest = time(NULL) - 100;
    for(int i = 0; i < station->Nvalues; ++i){
        val_t v;
        if(!station->get_value(station, &v, i)) continue;
        if(v.time < oldest) continue;
        if(0 < format_sensval(&v, buf, FULL_LEN+1, N)){
            printf("%s\n", buf);
            ++nsum; Tsum += v.time;
        }
    }
    if(nsum > 0){
        time_t last = (time_t)(Tsum / nsum);
        if(0 < format_msrmttm(last, buf, FULL_LEN+1)){
            printf("%s\n\n", buf);
        }
    }
#endif
#endif
}

/**
 * @brief openplugins - open sensors' plugin and init it
 * @param paths - paths to plugins
 * @param N - amount of plugins
 * @return amount of opened and inited plugins
 * This function should be runned only once at start
 */
int openplugins(char **paths, int N){
    char buf[PATH_MAX+1];
    if(!paths || !*paths || N < 1) return 0;
    if(allplugins || nplugins){
        WARNXL("Plugins already opened"); return 0;
    }
    allplugins = MALLOC(sensordata_t*, N);
    green("Try to open plugins:\n");
    for(int i = 0; i < N; ++i){
        printf("\tplugin[%d]=%s\n", i, paths[i]);
        snprintf(buf, PATH_MAX, "%s", paths[i]);
        char *colon = strchr(buf, ':');
        if(colon) *colon++ = 0;
        void* dlh = open_plugin(buf);
        if(!dlh) continue;
        DBG("OPENED");
        sensor_new_t sensnew = (sensor_new_t) dlsym(dlh, "sensor_new");
        if(sensnew){
            sensordata_t *S = sensnew(nplugins, poll_interval, colon); // here nplugins is index in array
            if(!S) WARNXL("Can't init plugin %s", paths[i]);
            else{
                if(!S->onrefresh || !S->onrefresh(S, dumpsensors)) WARNXL("Can't init refresh funtion");
                LOGMSG("Plugin %s nave %d sensors", paths[i], S->Nvalues);
                allplugins[nplugins++] = S;
            }
        }else WARNXL("Can't find initing function in plugin %s: %s", paths[i], dlerror());
    }
    return nplugins;
}

/**
 * @brief closeplugins - call `die` function for all sensors
 * This function should be runned at exit
 */
void closeplugins(){
    if(!allplugins || nplugins < 1) return;
    for(int i = 0; i < nplugins; ++i){
        if(allplugins[i]->kill) allplugins[i]->kill(allplugins[i]);
    }
    FREE(allplugins);
    nplugins = 0;
}

static const char* const NM[IS_OTHER] = { // names of standard fields
    [IS_WIND]       = "WIND",
    [IS_WINDDIR]    = "WINDDIR",
    [IS_HUMIDITY]   = "HUMIDITY",
    [IS_AMB_TEMP]   = "EXTTEMP",
    [IS_INNER_TEMP] = "INTTEMP",
    [IS_HW_TEMP]    = "HWTEMP", // mirror?
    [IS_PRESSURE]   = "PRESSURE",
    [IS_PRECIP]     = "PRECIP",
    [IS_PRECIP_LEVEL]="PRECIPLV",
    [IS_MIST]       = "MIST",
    [IS_CLOUDS]     = "CLOUDS",
    [IS_SKYTEMP]    = "SKYTEMP",
    [IS_LIGTDIST]   = "LIGTDIST",
};

// format "sense" of sensor, like "[WIND][1]=2"
int format_senssense(const val_t *v, char *buf, int buflen, int Np){
    if(!v || !buf || buflen < 1) return -1;
    int idx = v->meaning;
    const char *name = (idx < IS_OTHER) ? NM[idx] : v->name;
    int got;
    if(Np > -1) got = snprintf(buf, buflen, "[%s][%d]=%d", name, Np, v->sense);
    else got = snprintf(buf, buflen, "[%s]=%d", name, v->sense);
    return (got < buflen) ? got : buflen; // full or truncated
}

void get_fieldname(const val_t *v, char buf[KEY_LEN+1]){
    if(!v || !buf) return;
    int idx = v->meaning;
    const char *name = NULL;
    if(idx < IS_OTHER){
        name = NM[idx];
    }else{
        name = v->name;
    }
    if(name) snprintf(buf, KEY_LEN+1, "%s", name);
    else buf[0] = 0; // empty
}

/**
 * @brief format_sensval - snprintf sensor's value into buffer
 * @param v - value to get
 * @param buf - buffer
 * @param buflen - full length of `buf`
 * @param Np - if Np>-1, show it as plugin number (added to field name in square brackets, like WIND[1]);
 * @return amount of symbols printed or -1 if error
 */
int format_sensval(const val_t *v, char *buf, int buflen, int Np){
    if(!v || !buf || buflen < 1) return -1;
    char strval[VAL_LEN];
    int fieldlen = 20; // minimal distance between '=' and '/' is 22 bytes
    switch(v->type){
        case VALT_UINT:  snprintf(strval, VAL_LEN, "%u", v->value.u); break;
        case VALT_INT:   snprintf(strval, VAL_LEN, "%d", v->value.i); break;
        case VALT_FLOAT: snprintf(strval, VAL_LEN, "%.2f", v->value.f); break;
        case VALT_STRING: sprintf(strval, "'%s'", v->value.str); fieldlen = -20; break;
        default: sprintf(strval, "'ERROR'");
    }
    const char* const CMT[IS_OTHER] = { // comments for standard fields
        [IS_WIND]       = "Wind, m/s",
        [IS_WINDDIR]    = "Wind direction, degr (CW from north to FROM)",
        [IS_HUMIDITY]   = "Humidity, percent",
        [IS_AMB_TEMP]   = "Ambient temperature, degC",
        [IS_INNER_TEMP] = "In-dome temperature, degC",
        [IS_HW_TEMP]    = "Hardware (mirror?) termperature, degC",
        [IS_PRESSURE]   = "Atmospheric pressure, mmHg",
        [IS_PRECIP]     = "Precipitation (1 - yes, 0 - no)",
        [IS_PRECIP_LEVEL]="Cumulative precipitation level (mm)",
        [IS_MIST]       = "Mist (1 - yes, 0 - no)",
        [IS_CLOUDS]     = "Integral sky quality value (bigger - better)",
        [IS_SKYTEMP]    = "Mean sky temperatyre",
        [IS_LIGTDIST]   = "Distance to last lightning, km",
    };
    const char *name = NULL, *comment = NULL;
    int idx = v->meaning;
    if(idx < IS_OTHER){
        name = NM[idx];
        comment = CMT[idx];
    }else{
        name = v->name;
        comment = v->comment;
    }
    if(!name) return 0; // no name pointed - don't show this value
    if(!comment) comment = "-";
    int got;
    if(Np > -1) got = snprintf(buf, buflen, "%s[%d] = %s / %s", name, Np, strval, comment);
    else got = snprintf(buf, buflen, "%-*s= %*s / %s", KEY_LEN, name, fieldlen, strval, comment);
    return (got < buflen) ? got : buflen; // full or truncated
}

// the same for measurement time formatting
int format_msrmttm(time_t t, char *buf, int buflen, int Np){
    if(!buf || buflen < 1) return -1;
    char cmt[COMMENT_LEN+1];
    struct tm *T = localtime(&t);
    strftime(cmt, COMMENT_LEN, "%F %T", T);
    int got;
    if(Np > -1) got = snprintf(buf, buflen, "TWEATH[%d] = %zd / Last weather time: %s", Np, t, cmt);
    else got = snprintf(buf, buflen, "TWEATH  = %20zd / Last weather time: %s", t, cmt);
    return (got < buflen) ? got : buflen;
}

// find sensor's value by its name; @return index or -1 if not found
int find_val_by_name(sensordata_t *s, const char *name){
    if(!s || !name)  return -1;
    if(s->Nvalues < 1) return -1;
    // check standard "meaning"
    valmeaning_t mnng = 0;
    for(; mnng < IS_OTHER; ++mnng){
        if(0 == strcmp(NM[mnng], name)) break; // found in standard
    }
    for(int i = 0; i < s->Nvalues; ++i){
        val_t val;
        if(!s->get_value(s, &val, i) || val.meaning != mnng) continue;
        if(mnng != IS_OTHER){ // found in standard values
            return i;
        }else{ // non-standard: check by name
            if(0 == strcmp(val.name, name)){ // found by name
                return i;
            }
        }
    }
    return -1; // not found
}

// chane 'sense' field of given meteostation for value with index=`idx`; @return FALSE if failed
int change_val_sense(sensordata_t *s, int idx, valsense_t sense){
    if(!s || sense < 0 || sense >= VAL_AMOUNT) return FALSE;
    int N = s->Nvalues;
    if(idx < 0 || idx >= N) return FALSE;
    s->values[idx].sense = sense;
    return TRUE;
}

// convert val_t into double
double val2d(const val_t *value){
    double curvalue;
    switch(value->type){
        case VALT_UINT: curvalue = (double) value->value.u; break;
        case VALT_INT: curvalue = (double) value->value.i; break;
        case VALT_FLOAT: curvalue = (double) value->value.f; break;
        default: curvalue = 0.;
    }
    return curvalue;
}

// pause and continue sensors refresh
int station_mute(sensordata_t *s){
    if(!s) return FALSE;
    s->IsMuted = TRUE;
    return TRUE;
}
int station_unmute(sensordata_t *s){
    if(!s) return FALSE;
    s->IsMuted = FALSE;
    return TRUE;
}
int station_is_muted(sensordata_t *s){
    if(s && s->IsMuted) return TRUE;
    return FALSE;
}
