/*
 * This file is part of the SSII project.
 * Copyright 2022 Edward V. Emelianov <edward.emelianoff@gmail.com>.
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

#include <usefull_macros.h>
#include <stdio.h>
#include <stdarg.h>

#include "motlog.h"

static FILE *flog = NULL;
static double time0 = 0.;

int open_mot_log(const char *path){
    if(!path) return FALSE;
    flog = fopen(path, "a+");
    if(!flog){
        WARN("Can't open %s", path);
        return FALSE;
    }
    time0 = dtime();
    return TRUE;
}

void refresh_t0(){
    time0 = dtime();
}

void close_mot_log(){
    if(!flog) return;
    fclose(flog);
    flog = NULL;
}

int mot_log(int timestamp, const char *fmt, ...){
    if(!flog) return 0;
    int i = 0;
    // timestamp in milliseconds
#ifdef EBUG
    red("Time: %10.2f\n", 1000.*(dtime() - time0));
#endif
    if(timestamp) i += fprintf(flog, "%10.2f\t", 1000.*(dtime() - time0));
    va_list ar;
    va_start(ar, fmt);
    i += vfprintf(flog, fmt, ar);
    va_end(ar);
    fseek(flog, -1, SEEK_CUR);
    char c;
    ssize_t r = fread(&c, 1, 1, flog);
    if(1 == r){ // add '\n' if there was no newline
        if(c != '\n') i += fprintf(flog, "\n");
    }
    fflush(flog);
    return i;
}
