/*
 * This file is part of the teldaemon project.
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

#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#include <usefull_macros.h>

#include "header.h"
#include "socket.h"

static char *headername = NULL;
static char *telescope_name = NULL;
static header_mask_t header_mask = {0};

static int printhdr(int fd, const char *key, const char *val, const char *cmnt){
    char tmp[81];
    char tk[9];
    if(strlen(key) > 8){
        snprintf(tk, 8, "%s", key);
        key = tk;
    }
    if(cmnt){
        snprintf(tmp, 81, "%-8s= %-21s / %s", key,  val, cmnt);
    }else{
        snprintf(tmp, 81, "%-8s= %s", key, val);
    }
    size_t l = strlen(tmp);
    tmp[l] = '\n';
    ++l;
    if(write(fd, tmp, l) != (ssize_t)l){
        WARN("write()");
        return 1;
    }
    return 0;
}

// return TRUE if can write to given header file
int header_create(const char *file, int flags){
    if(!file || flags < 0 || flags > 0xff) return FALSE;
    FILE *hf = fopen(file, "w");
    if(!hf) return FALSE;
    unlink(file);
    if(headername) FREE(headername);
    headername = strdup(file);
    header_mask.flags = (uint8_t)flags;
    return TRUE;
}

// set given `name` for telescope name
void telname(const char *name){
    if(telescope_name) FREE(telescope_name);
    if(name){
        int l = strlen(name) + 3;
        telescope_name = MALLOC(char, l);
        snprintf(telescope_name, l-1, "'%s'", name);
    }
}

// return static buffer with help
const char *getheadermaskhelp(){
    return
        "0 - telescope name\n"
        "1 - focuser status\n"
        "2 - cooler status\n"
        "3 - heater status\n"
        "4 - external temperature\n"
        "5 - mirror temperature\n"
        "6 - measured time\n"
    ;
}

void write_header(){
    if(!headername || !header_mask.flags) return;
    char aname[PATH_MAX];
    char val[22];
    telstatus_t st;
#define WRHDR(k, v, c)  do{if(printhdr(fd, k, v, c)){goto returning;}}while(0)
    snprintf(aname, PATH_MAX-1, "%sXXXXXX", headername);
    int fd = mkstemp(aname);
    if(fd < 0){
        LOGWARN("Can't write header file: mkstemp()");
        return;
    }
    fchmod(fd, 0644);

    if(get_telescope_data(&st)) WRHDR("OPERATIO", "'FORBIDDEN'", "Observations are forbidden");
    if(header_mask.telname && telescope_name) WRHDR("TELESCOP", telescope_name, "Telescope name");
    if(header_mask.fosuser){
        snprintf(val, 21, "%d", st.focuserpos);
        WRHDR("FOCUS", val, "Current focuser position");
    }
    if(header_mask.cooler){
        snprintf(val, 21, "%d", st.cooler);
        WRHDR("COOLER", val, "Primary mirror cooler status: 0/1 (off/on)");
    }
    if(header_mask.heater){
        snprintf(val, 21, "%d", st.heater);
        WRHDR("HEATER", val, "Secondary mirror heater status: 0/1 (off/on)");
    }
    if(header_mask.exttemp){
        snprintf(val, 21, "%.1f", st.ambienttemp);
        WRHDR("TDOME", val, "In-dome temperature, degC");
    }
    if(header_mask.mirtemp){
        snprintf(val, 21, "%.1f", st.mirrortemp);
        WRHDR("TMIRROR", val, "Mirror temperature, degC");
    }
    if(header_mask.meastime){
        snprintf(val, 21, "%.3f", st.stattime);
        char timebuf[BUFSIZ];
        time_t t = (time_t) st.stattime;
        struct tm *tmp;
        tmp = localtime(&t);
        if(!tmp || 0 == strftime(timebuf, BUFSIZ, "Measurement time: %F %T", tmp)){
            LOGERR("localtime() returned NULL");
            goto returning;
        }
        WRHDR("TMEAS", val, timebuf);

    }
#undef WRHDR
returning:
    close(fd);
    rename(aname, headername);
}
