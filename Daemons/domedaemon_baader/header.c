/*
 * This file is part of the baader_dome project.
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
static char *dome_name = NULL;

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
int header_create(const char *file){
    if(!file) return FALSE;
    FILE *hf = fopen(file, "w");
    if(!hf) return FALSE;
    unlink(file);
    if(headername) FREE(headername);
    headername = strdup(file);
    return TRUE;
}

// set given `name` for telescope name
void domename(const char *name){
    if(dome_name) FREE(dome_name);
    if(name){
        int l = strlen(name) + 3;
        dome_name = MALLOC(char, l);
        snprintf(dome_name, l-1, "'%s'", name);
    }
}

void write_header(){
    if(!headername) return;
    char aname[PATH_MAX];
    char val[22];
#define WRHDR(k, v, c)  do{if(printhdr(fd, k, v, c)){goto returning;}}while(0)
    snprintf(aname, PATH_MAX-1, "%sXXXXXX", headername);
    int fd = mkstemp(aname);
    if(fd < 0){
        LOGWARN("Can't write header file: mkstemp()");
        return;
    }
    fchmod(fd, 0644);

    dome_data_t st;
    if(get_dome_data(&st)) WRHDR("OPERATIO", "'FORBIDDEN'", "Observations are forbidden");
    if(dome_name) WRHDR("DOME", dome_name, "Dome manufacturer/name");
    WRHDR("DOMESTAT", st.status, "Dome status");
    WRHDR("DOMEWEAT", st.weather, "Dome weather sensor status");
    snprintf(val, 21, "%d", st.errcode);
    WRHDR("DOMEECOD", val, "Dome error code: Rain|Watchdog|Power");
    snprintf(val, 21, "%.3f", st.stattime);
    char timebuf[BUFSIZ];
    time_t t = (time_t) st.stattime;
    struct tm *tmp;
    tmp = localtime(&t);
    if(!tmp || 0 == strftime(timebuf, BUFSIZ, "Measurement time: %F %T", tmp)){
        LOGERR("localtime() returned NULL");
        goto returning;
    }
    WRHDR("TDOMMEAS", val, timebuf);
#undef WRHDR
returning:
    close(fd);
    rename(aname, headername);
}
