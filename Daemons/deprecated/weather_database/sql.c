/*
 * This file is part of the sqlite project.
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

#include <stdio.h>
#include <sqlite3.h>
#include <usefull_macros.h>

#include "sql.h"

static sqlite3 *db = NULL;
static sqlite3_stmt *res = NULL;
static char *errmsg = NULL;
static int rc = 0;

#define SQL(str, callback, arg)     do{if(SQLITE_OK != (rc = sqlite3_exec(db, str, callback, arg, &errmsg))){ WARNX("SQL exec error: %s", errmsg); LOGERR("SQL exec error: %s", errmsg);}}while(0)

void closedb(){
    if(res) sqlite3_finalize(res);
    if(db) sqlite3_close(db);
    res = NULL;
    db = NULL;
}

/**
 * @brief opendb - try to open sqlite3 database
 * @param name - filename of db
 * @return TRUE if all OK
 */
int opendb(const char *name){
    closedb();
    rc = sqlite3_open(name, &db);
    if(rc != SQLITE_OK){
        WARNX("Can't open database file %s: %s", name, sqlite3_errmsg(db));
        sqlite3_close(db);
        db = NULL;
        return FALSE;
    }
    // time with milliseconds from UNIX time with them
    // select strftime('%Y-%m-%d %H:%M:%f', 1092941466.123, 'unixepoch');
    //
    // get data by timestamp
    // select * from weatherdata where timestamp < unixepoch('now','-10 minutes') and timestamp > 1685617000;
    char *sql = "create table if not exists weatherdata(timestamp real primary key asc, windmax real, wind real, windrms real, dir real, dirrms real, pressure real, temperature real, humidiy real, rain real);";
    SQL(sql, NULL, 0);
    return TRUE;
}

// add data to DB
void addtodb(weatherstat_t *w){
    if(!w) return;
    char buf[BUFSIZ];
    int restlen = BUFSIZ-1;
    snprintf(buf, restlen, "insert into weatherdata values(%.1f, %.1f, %.1f, %.1f, %.1f, %.1f, %.1f, %.1f, %.1f, %.1f);",
             w->tmeasure.mean, w->windspeed.max, w->windspeed.mean, w->windspeed.rms, w->winddir.mean, w->winddir.rms,
             w->pressure.mean, w->temperature.mean, w->humidity.mean, w->rainfall.max);
    SQL(buf, NULL, 0);
}
