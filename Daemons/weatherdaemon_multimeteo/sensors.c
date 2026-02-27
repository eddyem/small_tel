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
#include <usefull_macros.h>

#include "sensors.h"

#define WARNXL(...) do{ LOGWARN(__VA_ARGS__); WARNX(__VA_ARGS__); } while(0)
#define WARNL(...) do{ LOGWARN(__VA_ARGS__); WARN(__VA_ARGS__); } while(0)
#define ERRXL(...) do{ LOGERR(__VA_ARGS__); ERRX(__VA_ARGS__); } while(0)
#define ERRL(...) do{ LOGERR(__VA_ARGS__); ERR(__VA_ARGS__); } while(0)

static int nplugins = 0;
static sensordata_t **allplugins = NULL;

int get_nplugins(){
    return nplugins;
}

/**
 * @brief get_plugin - get copy of opened plugin
 * @param o (o) - plugin with given index
 * @param N - index in `allplugins`
 * @return TRUE if OK
 */
int get_plugin(sensordata_t *o, int N){
    if(!o || N < 0 || N >= nplugins) return FALSE;
    *o = *allplugins[N];
    return TRUE;
}

void *open_plugin(const char *name){
    DBG("try to open lib %s", name);
    void* dlh = dlopen(name, RTLD_NOLOAD); // library may be already opened
    if(!dlh){
        DBG("Not loaded - load");
        dlh = dlopen(name, RTLD_NOW);
    }
    if(!dlh){
        char *e = dlerror();
        WARNXL("Can't find plugin! %s", (e) ? e : "");
        return NULL;
    }
    return dlh;
}

#ifdef EBUG
// in release this function can be used for meteo logging
static void dumpsensors(const struct sensordata_t* const station){
    FNAME();
    if(!station || !station->get_value || station->Nvalues < 1) return;
    char buf[FULL_LEN+1];
    int N = (nplugins > 1) ? station->PluginNo : -1;
    for(int i = 0; i < station->Nvalues; ++i){
        val_t v;
        if(!station->get_value(&v, i)) continue;
        if(0 < format_sensval(&v, buf, FULL_LEN+1, N)){
            printf("%s\n", buf);
        }
    }
}
#endif

/**
 * @brief openplugins - open sensors' plugin and init it
 * @param paths - paths to plugins
 * @param N - amount of plugins
 * @return amount of opened and inited plugins
 * This function should be runned only once at start
 */
int openplugins(char **paths, int N){
    if(!paths || !*paths || N < 1) return 0;
    if(allplugins || nplugins){
        WARNXL("Plugins already opened"); return 0;
    }
    allplugins = MALLOC(sensordata_t*, N);
    green("Try to open plugins:\n");
    for(int i = 0; i < N; ++i){
        printf("\tplugin[%d]=%s\n", i, paths[i]);
        void* dlh = open_plugin(paths[i]);
        if(!dlh) continue;
        DBG("OPENED");
        void *s = dlsym(dlh, "sensor");
        if(s){
            sensordata_t *S = (sensordata_t*) s;
            if(!S->get_value) WARNXL("Sensor library %s have no values' getter!", paths[i]);
            if(!S->init){
                WARNXL("Sensor library %s have no init funtion");
                continue;
            }
            int ns = S->init(nplugins);
            if(ns < 1) WARNXL("Can't init plugin %s", paths[i]);
            else{
#ifdef EBUG
                if(!S->onrefresh(dumpsensors)) WARNXL("Can't init refresh funtion");
#endif
                LOGMSG("Plugin %s nave %d sensors", paths[i], ns);
                allplugins[nplugins++] = S;
            }
        }else WARNXL("Can't find field `sensor` in plugin %s: %s", paths[i], dlerror());
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
        if(allplugins[i]->die) allplugins[i]->die();
    }
    FREE(allplugins);
    nplugins = 0;
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
    --buflen; // for trailing zero
    if(!v || !buf || buflen < FULL_LEN) return -1;
    char strval[VAL_LEN+1];
    switch(v->type){
        case VALT_UINT:  snprintf(strval, VAL_LEN, "%u", v->value.u); break;
        case VALT_INT:   snprintf(strval, VAL_LEN, "%d", v->value.i); break;
        case VALT_FLOAT: snprintf(strval, VAL_LEN, "%g", v->value.f); break;
        default: sprintf(strval, "'ERROR'");
    }
    const char* const NM[] = { // names of standard fields
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
        [IS_SKYTEMP]    = "SKYTEMP"
    };
    const char* const CMT[] = { // comments for standard fields
        [IS_WIND]       = "Wind, m/s",
        [IS_WINDDIR]    = "Wind direction, degr (CW from north to FROM)",
        [IS_HUMIDITY]   = "Humidity, percent",
        [IS_AMB_TEMP]   = "Ambient temperature, degC",
        [IS_INNER_TEMP] = "In-dome temperature, degC",
        [IS_HW_TEMP]    = "Hardware (mirror?) termperature, degC",
        [IS_PRESSURE]   = "Atmospheric pressure, mmHg",
        [IS_PRECIP]     = "Precipitation (1 - yes, 0 - no)",
        [IS_PRECIP_LEVEL]="Precipitation level (mm)",
        [IS_MIST]       = "Mist (1 - yes, 0 - no)",
        [IS_CLOUDS]     = "Integral clouds value (bigger - better)",
        [IS_SKYTEMP]    = "Mean sky temperatyre"
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
    int got;
    if(Np > -1) got = snprintf(buf, buflen, "%s[%d]=%s / %s", name, Np, strval, comment);
    else got = snprintf(buf, buflen, "%s=%s / %s", name, strval, comment);
    return got;
}

// the same for measurement time formatting
int format_msrmttm(time_t t, char *buf, int buflen){
    --buflen; // for trailing zero
    if(!buf || buflen < FULL_LEN) return -1;
    char cmt[COMMENT_LEN+1];
    struct tm *T = localtime(&t);
    strftime(cmt, COMMENT_LEN, "%F %T", T);
    return snprintf(buf, buflen, "TMEAS=%zd / Last measurement time: %s", t, cmt);
}
