/*
 * This file is part of the meteologger project.
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

#include <dirent.h>
#include <fcntl.h>
#include <regex.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "server.h"

// max available delta for timestamp if it's in future to current time
// USE NTP everywhere!!11111
#define TIME_IN_FUTURE_MAX  (60)

// some "standard" keys from server
static const char *key_pluginname = "PLUGIN";
static const char *key_nvalues = "NVALUES";
static const char *key_timestamp = "TWEATH";

// sensor's db filename mask: DBpath/weatherXX.log
static const char *dbfilename_mask = "%s/weather%02d.log";
// and search regex
static const char *dbfilename_regex = "weather[[:digit:]]{2}\\.log";

static volatile atomic_bool isrunning = true;
static volatile atomic_bool logreinit = false;
static double req_interval = 0.5;   // request interval, s
static double net_timeout = 1.;     // timeout for server's answer, s
static char *DBpath = NULL; // path to storage

typedef struct{
    int fd;             // log file descriptor
    int idx;            // index of station for requests
    int nvalues;        // maximal amount of values
    bool initialized;   // `sensor` is fully initialized
    char path[PATH_MAX];// path to file
    char *sensname;     // sensor's name
    char **keys;        // keyword names for header and index in string
    int *levels;        // array of `weather level` for each keyword
    time_t lasttimestamp;// timestamp of last received data
    char buf[BUFSIZ];   // string buffer for file writing operations
    int buflen;         // current length of `buf`
    int lastvalidx;     // index of last value stored in `buf`
} senslog_t;

static int Nsensors = 0; // amount of sensors for logging
static senslog_t *sensors = NULL; // array of files for logging

// commands for communication with server
typedef enum{
    CMD_LIST,
    CMD_GET,
    CMD_CHKLEVEL,
    CMD_TIME,
    CMD_AMOUNT
} req_commands;
static const char *commands[CMD_AMOUNT] = {
    [CMD_LIST] = "list",
    [CMD_GET] = "get",
    [CMD_CHKLEVEL] = "chklevel",
    [CMD_TIME] = "time",
};

static void delete_senskeys(senslog_t *sensor){
    if(!sensor) return;
    for(int j = 0; j < sensor->nvalues; ++j)
        FREE(sensor->keys[j]);
    FREE(sensor->keys);
    sensor->nvalues = 0;
}

static void sensors_delete(){
    if(!sensors) return;
    for(int i = 0; i < Nsensors; ++i){
        senslog_t *sensor = &sensors[i];
        if(sensor->fd > -1){
            close(sensor->fd);
            sensor->fd = -1;
        }
        delete_senskeys(sensor);
        FREE(sensor->levels);
        FREE(sensor->sensname);
    }
    FREE(sensors);
}

// stop server process
void stop_server(){
    FNAME();
    isrunning = false;
}

// set request interval to this value
void set_reqinterval(double dt){
    req_interval = dt; // all checking is in `parseargs.c`
}

// set network timeout
void set_nettimeout(double dt){
    net_timeout = dt;
}

/**
 * @brief write2fd - tries to write into file; in case of error close this file and set `sensor->fd=-1`
 * @param sensor - "sensor" to write
 * @param str - input string
 * @param len - length of string
 * @return true if all OK
 */
static bool write2fd(senslog_t *sensor, const char *str, size_t len){
    if(!sensor || !str || len < 1) return false;
    if(write(sensor->fd, str, len) != (ssize_t)len){
        LOGERR("Can't write '%s' to file %s: %s", str, sensor->path, strerror(errno));
        WARNX("Can't write data to file");
        close(sensor->fd);
        sensor->fd = -1;
        return false;
    }
    return true;
}

/**
 * @brief send_request - send request to server
 * @param cmd - command index
 * @return true if OK, false if disconnected
 */
static bool send_request(sl_sock_t *sock, const char *req){
    if(sl_sock_sendstrmessage(sock, req) < 1){
        WARNX("Can't send request '%s'", req);
        LOGERR("Can't send request '%s'", req);
        return false;
    }
    return true;
}

/**
 * @brief get_answer_line - read one line of server's answer with timeout
 * @param sock - socket to read
 * @param buf - input buffer
 * @param buflen - its length
 * @return length of receiving data, 0 if nothing received, -1 in case of error (and clear all sensor's list)
 */
static ssize_t get_answer_line(sl_sock_t *sock, char *buf, size_t buflen){
    FNAME();
    double t0 = sl_dtime();
    DBG("tstart=%g", t0);
    ssize_t len = -1;
    while(sl_dtime() - t0 < net_timeout){
        len = sl_sock_readline(sock, buf, buflen-1);
        if(len > 0) break;
        else if(len < 0){
            WARNX("Seems like server disconnected");
            LOGWARN("Error reading server answer, disconnected?");
            sensors_delete();
        }
    }
    DBG("len=%zd, tend=%g", len, sl_dtime());
    return len;
}

// reinit logs at start or after rotating
// @return false if failed
bool reinit_logs(){
    FNAME();
    logreinit = true;
    return true;
}

bool checkDBpath(const char *path){
    struct stat path_stat;
    if(stat(path, &path_stat)){
        WARNX("Can't stat() %s", path);
        return false; // `stat` failed
    }
    if(!S_ISDIR(path_stat.st_mode)){
        WARNX("%s isn't a directory", path);
        return false; // not a directory
    }
    // now check if we can write there
    if(access(path, W_OK)){
        WARNX("Can't write to %s", path);
        return false;
    }
    return true;
}

// find if this sensor already have opened BD;
// if have, modify sensor->fd and sensor->path, open file for appending and return `true`
static bool find_old_file(senslog_t *sensor){
    if(sensor->fd > -1){
        DBG("found opened file %s with fd %d -> close", sensor->path, sensor->fd);
        close(sensor->fd);
    }

    regex_t regex;
    // Compile regex
    if(regcomp(&regex, dbfilename_regex, REG_EXTENDED | REG_NOSUB) != 0){
        LOGERR("find_old_file(): error in regcomp(), %s", strerror(errno));
        WARNX("regcomp()");
        return false;
    }

    bool ret = false;
    DIR *d = opendir(DBpath);
    if(d){
        struct dirent *dir;
        while((dir = readdir(d))){
            // Check if filename matches regex
            if(regexec(&regex, dir->d_name, 0, NULL, 0) == 0){
                DBG("Found: %s", dir->d_name);
                char fname[PATH_MAX], line[BUFSIZ];
                snprintf(fname, PATH_MAX, "%s/%s", DBpath, dir->d_name);
                FILE *fp = fopen(fname, "r");
                if(!fp){
                    LOGERR("Cannot open %s for reading: %s", fname, strerror(errno));
                    DBG("Can't open");
                    continue;
                }
                if(fgets(line, sizeof(line), fp) == NULL){
                    LOGWARN("Found empty BD file %s - WTF???", fname);
                    fclose(fp);
                    continue;
                }
                fclose(fp);
                int len = strlen(line);
                if(len > 0 && line[len-1] == '\n') line[len-1] = 0; // remove trailing newline
                if(line[0] != '#' || line[1] != ' '){ // should starts from comment with station's name
                    LOGWARN("Found broken database file: %s", fname);
                    continue;
                }
                const char *stname = line + 2; // station name from comment
                DBG("Check name: '%s' and '%s'", stname, sensor->sensname);
                if(0 == strcmp(stname, sensor->sensname)){ // good, we found this file!
                    DBG("Found existant file %s -> append to it", fname);
                    int newfd = open(fname, O_WRONLY | O_APPEND);
                    if(newfd < 0){
                        LOGERR("Can't open existant BD file %s for append, try to create new", fname);
                        continue;
                    }
                    sensor->fd = newfd;
                    ret = true;
                    break;
                }
            }
        }
        closedir(d);
    }else{
        LOGERR("Can't open %s: %s", DBpath, strerror(errno));
    }
    regfree(&regex);
    return ret;
}

// create new database file in `DBpath`
// `sensor` obligate to be fully initialized
static bool create_db_file(senslog_t *sensor){
    FNAME();
    if(!sensor || !sensor->initialized){
        WARNX("create_db_file() should be called only with fully initialized `sensor`");
        return false;
    }
    int num = sensor->idx; // try to start from sensor's index
    char path[PATH_MAX];
    for(; num <= 99; ++num){
        snprintf(path, PATH_MAX, dbfilename_mask, DBpath, num);
        DBG("Try to create %s", path);
        if(access(path, F_OK) != 0) break;   // no such file
    }
    if(num > 99){
        LOGERR("Can't find free filename for station '%s', all numbers from 0 to 99 are busy! WTF???",
               sensor->sensname);
        WARNX("No free numbers for sensors");
        return false;
    }
    // create and open write-only
    int newfd = open(path, O_WRONLY | O_CREAT, 0644);
    if(newfd < 0){
        LOGERR("Can't open file %s for station %s: %s", path, sensor->sensname, strerror(errno));
        WARNX("Can't open %s", path);
        return false;
    }
    DBG("OK, %s opened, try to write header", path);
    sensor->fd = newfd;
    ssize_t len = snprintf(path, PATH_MAX, "# %s\n", sensor->sensname);
    if(!write2fd(sensor, path, len)) return false;
    len = snprintf(path, PATH_MAX, "# Station #%d, format: KEYWORD[level],...\n# TIMESTAMP, ", sensor->idx);
    if(!write2fd(sensor, path, len)) return false;
    char *ptr = path;
    len = 0;
    for(int i = 0; i < sensor->nvalues; ++i){
        ssize_t L = snprintf(ptr, PATH_MAX-len, "%s%s[%d]", i ? ", " : "", sensor->keys[i], sensor->levels[i]);
        len += L;
        ptr += L;
    }
    len += snprintf(ptr, PATH_MAX-len, "\n");
    if(!write2fd(sensor, path, len)) return false;
    DBG("%s now have descriptor %d: %s", sensor->sensname, newfd, path);

    return true;
}

/**
 * @brief get_sensor_keys - fill `sensor->keys` and `sensor->levels` using `chklevel` request
 * @param sensor - "sensor" with inited `nvalues`, `idx` and `sensname`; allocated `keys` and `levels`
 * @param sock - socket for request
 * @return
 */
static bool get_sensor_keys(senslog_t *sensor, sl_sock_t *sock){
    if(!sensor || !sock) return false;
    char str[BUFSIZ], key[SL_KEY_LEN], value[SL_VAL_LEN];
    snprintf(str, BUFSIZ, "%s=%d\n", commands[CMD_CHKLEVEL], sensor->idx);
    if(!send_request(sock, str)){
        LOGERR("Can't send request '%s'", commands[CMD_CHKLEVEL]);
        return false;
    }
    ssize_t got = -1;
    int valueidx = 0;
    while(valueidx < sensor->nvalues && (got = get_answer_line(sock, str, BUFSIZ)) > 0 ){
        DBG("valueidx=%d, nvalues=%d, got answer: %s", valueidx, sensor->nvalues, str);
        if(2 != sl_get_keyval(str, key, value)){
            LOGWARN("Wrong answer for `chklevel`: %s", str);
            continue;
        }
        DBG("key=%s", key);
        int sensidx;
        if(2 != sscanf(key, "[%[^]]][%d]", str, &sensidx)){
            DBG("header? '%s' (str=%s)", key, str);
            continue; // omit two heading lines: `PLUGIN` and `NVALUES`
        }
        DBG("str=%s, sensidx=%d", str, sensidx);
        if(sensor->idx != sensidx){
            LOGWARN("Got wrong key answer for sensor with idx=%d: '%s'", sensor->idx, key);
            continue;
        }
        // OK - it's our value, increment number and fill name
        sensor->keys[valueidx] = strdup(str);
        sensor->levels[valueidx] = atoi(value);
        DBG("now sensor->keys[%d]=%s, levels=%d", valueidx, sensor->keys[valueidx], sensor->levels[valueidx]);
        ++valueidx;
    }
    if(got < 0 || valueidx != sensor->nvalues) return false;
    sensor->initialized = true;
    sensor->lastvalidx = -1;
    return true;
}

// prepare BD files and fill `sensors[i].fd` fields; make header in new file or move write pointer to the end of existant
// `sensors` should be prepared already
// this function called at start and on any logs reinit
static bool prepare_files(sl_sock_t *sock){
    if(!sock || !DBpath || Nsensors < 1) return false;
    for(int i = 0; i < Nsensors; ++i){
        senslog_t *sensor = &sensors[i];
        if(!sensor->initialized && !get_sensor_keys(sensor, sock)) return false;
        DBG("Check if there's something for sensor[%d]", i);
        if(!find_old_file(sensor)){ // create new file
            DBG("Nothing found, try to create new");
            if(!create_db_file(sensor)){
                DBG("Oops: can't create");
                return false;
            }
        }
    }
    DBG("All ready!");
    return true;
}

static bool analyse_list(sl_sock_t *sock){
    FNAME();
    char str[BUFSIZ], key[SL_KEY_LEN], value[SL_VAL_LEN];
    ssize_t got;
    while((got = get_answer_line(sock, str, BUFSIZ)) > 0){
        DBG("Got answer: %s", str);
        if(2 != sl_get_keyval(str, key, value)){
            LOGWARN("Wrong answer from meteodaemon for 'list' request: %s", str);
            continue;
        }
        DBG("key: '%s', value: '%s'", key, value);
        int idx;
        // we don't need `str` now and can use it again
        if(2 != sscanf(key, "%[^[][%d]", str, &idx)){
            LOGWARN("Wrong key format: '%s'", key);
            continue;
        }
        DBG("Got key '%s' with idx=%d", str, idx);
        if(Nsensors <= idx){
            int oldN = Nsensors;
            Nsensors = idx + 1;
            sensors = realloc(sensors, Nsensors * sizeof(senslog_t));
            if(!sensors){
                sensors_delete();
                LOGERR("analyse_list(): error in realloc()");
                WARNX("analyse_list(): error in realloc()");
                return false;
            }
            bzero(&sensors[oldN], sizeof(senslog_t)*(Nsensors - oldN));
        }
        senslog_t *sensor = &sensors[idx];
        if(0 == strcmp(str, key_pluginname)){ // found plugin name -> fill this field
            if(sensor->sensname) free(sensor->sensname);
            sensor->sensname = strdup(value);
            sensor->idx = idx;
            sensor->fd = -1; // not inited yet
            DBG("name[%d]=%s", idx, sensor->sensname);
        }else if(0 == strcmp(str, key_nvalues)){
            int nvalues = atoi(value);
            if(nvalues < 1){
                LOGWARN("wrong server's responce for sensor[%d] values amount: %d", idx, nvalues);
                continue;
            }
            if(sensor->nvalues) delete_senskeys(sensor);
            sensor->nvalues = nvalues;
            DBG("sensor[%d] have %d values", idx, sensor->nvalues);
            sensor->keys = MALLOC(char*, nvalues); // prepare empty array for keywords
            sensor->levels = MALLOC(int, nvalues);
        }
    }
    if(got < 0) return false; // disconnection?
    // now check all we got
    if(Nsensors < 1){
        LOGWARN("Found 0 sensors in server's answer");
        WARNX("Found 0 sensors in server's answer");
        return false;
    }
    bool ans = true;
    for(int i = 0; i < Nsensors; ++i){
        senslog_t *sensor = &sensors[i];
        DBG("Check sensor %s with values %d", sensor->sensname, sensor->nvalues);
        if(sensor->nvalues < 1){
            LOGWARN("Zero keys for station %s", sensor->sensname);
            ans = false; break;
        }
    }
    if(ans) ans = prepare_files(sock);
    if(ans == false) sensors_delete();
    return ans;
}

// prepare DB files at start
static bool prepare_logfiles(sl_sock_t *sock, const char *path){
    FNAME();
    char buf[PATH_MAX];
    if(DBpath) return false; // already inited??
    if(!checkDBpath(path)) return false;
    DBpath = strdup(path);
    if(!DBpath){
        WARN("strdup()");
        return false;
    }
    DBG("Store files in %s; send `list` request", DBpath);
    snprintf(buf, 255, "%s\n", commands[CMD_LIST]);
    if(!send_request(sock, buf)){
        LOGERR("Can't send inited request");
        return false;
    }
    // now we have an answer: 2*N strings a la "PLUGIN[i]=...\nNVALUES[i]=...\n" ->
    //     prepare `sensors` and try to find opened files like `sensorXX.log`
    if(!analyse_list(sock)) return false;
    return true;
}

// send `get=x` request for each sensor; return true if all sent OK
static bool send_get_req(sl_sock_t *sock){
    char str[128];
    if(!sock || !sensors || Nsensors < 1) return false;
    for(int i = 0; i < Nsensors; ++i){
        if(sensors[i].nvalues < 1) continue;
        snprintf(str, 128, "%s=%d\n", commands[CMD_GET], sensors[i].idx);
        if(!send_request(sock, str)) return false;
    }
    return true;
}

// get answers and fill database
// we consider that answers comes in sequence order, from least sensno to timestamp
// @return false only in case of error
static bool poll_server_answers(sl_sock_t *sock){
    char str[BUFSIZ], key[SL_KEY_LEN], value[SL_VAL_LEN];;
    ssize_t len = sl_sock_readline(sock, str, BUFSIZ-1);
    if(len < 0) return false; // error
    else if(len == 0) return true; // nothing to read
    DBG("Got line: '%s'", str);
    int sensidx;
    if(2 != sl_get_keyval(str, key, value) || 2 != sscanf(key, "%[^[][%d]", str, &sensidx)){
        WARNX("not key=value");
        LOGWARN("Wrong answer for `get`: %s", str);
        return false;
    }
    if(sensidx < 0 || sensidx >= Nsensors){
        WARNX("Wrong sensor index");
        LOGWARN("Got station with index (%d) out of bounds [0, %d]", sensidx, Nsensors-1);
        return false;
    }
    // omit header
    if(0 == strcmp(key_pluginname, str) || 0 == strcmp(key_nvalues, str)) return true;
    senslog_t *sensor = &sensors[sensidx];
    if(0 == strcmp(key_timestamp, str)){ // check timestamps and write data to disk
        time_t ts = (time_t)atol(value);
        DBG("Got timestamp: %zd (last timestamp: %zd)", ts, sensor->lasttimestamp);
        bool allOK = true;
        if(ts > sensor->lasttimestamp){ // don't store old data
            time_t curt = time(NULL);
            DBG("Timestamp minus current time = %zd", ts - curt);
            if(ts > curt && ts - curt > TIME_IN_FUTURE_MAX){
                WARNX("timestamp is in future");
                LOGWARN("got future timestamp for sensor %d: %ld", sensidx, ts);
                allOK = false;
            }else{ // all OK - we can save data
                if(sensor->lastvalidx > -1){ // only if at least one keyword received
                    int trailingcommas = sensor->nvalues - sensor->lastvalidx - 2; // amount of commas after our last record
                    DBG("trailing commas = %d", trailingcommas);
                    if(trailingcommas > 0){
                        if(sensor->buflen + 2*trailingcommas > BUFSIZ-1){
                            WARNX("Sensor's buffer overfull");
                            LOGWARN("Sensor %s: writing buffer overfull", sensor->idx);
                        }else{ // write trailing zeros to buffer
                            for(int i = 0; i < trailingcommas; ++i){
                                sprintf(sensor->buf + sensor->buflen, ", ");
                                sensor->buflen += 2;
                            }
                        }
                    }
                    // write timestamp without error checking
                    size_t l = snprintf(str, BUFSIZ, "%zd, ", ts);
                    write2fd(sensor, str, l);
                    // now throw data to disk
                    DBG("throw '%s' with length %d to disk", sensor->buf, sensor->buflen);
                    allOK = write2fd(sensor, sensor->buf, sensor->buflen);
                    write2fd(sensor, "\n", 1);
                    sensor->lasttimestamp = ts;
                    DBG("Data for %d is %swritten to disk", sensor->idx, allOK ? "" : "not ");
                }
            }
        }
        // clear gathred data
        sensor->buflen = 0;
        sensor->lastvalidx = -1;
        return allOK;
    }
    // now we need to find index of current keyword
    int idx = 0, nvalues = sensor->nvalues;
    for(; idx < nvalues; ++idx){
        if(0 == strcmp(str, sensor->keys[idx])) break;
    }
    DBG("key index=%d", idx);

    if(idx == nvalues){
        WARNX("Not found in keylist");
        LOGWARN("Weird keyword for station %d: '%s'", sensidx, str);
        return false;
    }
    if(sensor->lastvalidx >= idx){
        WARNX("Missed timestamp?");
        LOGWARN("Missed timestamp for station %d?", sensidx);
        sensor->lastvalidx = -1;
        sensor->buflen = 0;
        return false;
    }
    // add current raw value to sensor's buffer (there's could be a text fields, so don't make atof
    char *comment = strchr(value, '/');
    if(comment){
        *comment = 0; // remove comment mark
        if(comment != value && comment[-1] == ' ') comment[-1] = 0; // remove last space
    }
    DBG("VALUE: %s", value);
    // add spaces if need
    int trailingcommas = idx - sensor->lastvalidx - 1; // amount of commas after our last record before current
    DBG("Commas: %d", trailingcommas);
    if(trailingcommas > 0){
        if(sensor->buflen + 2*trailingcommas > BUFSIZ-1){
            WARNX("Buffer overfull");
            LOGWARN("Sensor %d: writing buffer overfull", sensor->idx);
        }else{ // write trailing zeros to buffer
            for(int i = 0; i < trailingcommas; ++i){
                sprintf(sensor->buf + sensor->buflen, ", ");
                sensor->buflen += 2;
            }
        }
    }
    sensor->buflen += snprintf(sensor->buf + sensor->buflen, BUFSIZ-sensor->buflen, "%s%s",
                               value, sensor->nvalues-1 != idx ? ", " : "");
    sensor->lastvalidx = idx;
    DBG("Now buf[%d]=%s", sensor->idx, sensor->buf);
    return true;
}

/**
 * @brief run_server - run main server: send weather requests and store data
 * @param node - node to connect
 * @param type - socket type
 * @param path - directory where to store data logs
 */
void run_server(const char *node, sl_socktype_e type, const char *path){
    if(!node || !path) return;
    //char str[BUFSIZ];
    sl_sock_t *sock = sl_sock_run_client(type, node, BUFSIZ);
    if(!sock){
        DBG("Can't connect");
        LOGERR("Can't connect to %s", node);
        return;
    }
    if(!prepare_logfiles(sock, path)) return;

    DBG("Superloop");
    int errctr = 0;
    double tlast = 0.;
    while(isrunning){
        bool allOK = true;
        if(logreinit){
            if(!prepare_files(sock)) allOK = false;
            else logreinit = false;
        }
        double tnow = sl_dtime();
        if(tnow - tlast > req_interval){ // send next requests
            DBG("\n\n\nSend next request; deltaT=%g", tnow - tlast);
            if(!send_get_req(sock)) allOK = false;
            else tlast = tnow;
        }
        // now poll everything from server
        if(!poll_server_answers(sock)) allOK = false;
        ;
        if(!allOK){
            if(++errctr > 5){
                LOGERR("Too much errors -> exit");
                WARNX("Too much errors -> exit");
                break;
            }
        }else errctr = 0;
        usleep(1000);
    }
    if(sock) sl_sock_delete(&sock);
    sensors_delete();
}
