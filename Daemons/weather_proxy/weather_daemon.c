/*
 * This file is part of the weather_proxy project.
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

#include <fcntl.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <usefull_macros.h>

#include "weather_data.h"

#define DEFAULT_PID     "/tmp/weather_proxy.pid"

// if we have no fresh data more than `RECONN_TMOUT`, try to reconect
#define RECONN_TMOUT    5
// don't ask new data less than `WEAT_TMOUT` seconds
#define WEAT_TMOUT      1

typedef struct{
    char *node;             // node of server
    int isunix;             // use UNIX-sockets instead of net
    char *logfile;          // logfile name
    int verb;               // verbocity level
    char *pidfile;          // pidfile name
    char *fitsheader;       // FITS-header with collected weather data
} glob_pars;

static pid_t childpid;
static int shm_fd = -1;
static int forbidden = 0;
static sem_t *sem = NULL;
static weather_data_t *shared_data = NULL;
static volatile int running = 1;
static glob_pars G = {.pidfile = DEFAULT_PID};

static sl_option_t opts[] = {
    {"node",    NEED_ARG,   NULL,   'n',    arg_string, APTR(&G.node),      "node to connect (host:port or UNIX socket name)"},
    {"logfile", NEED_ARG,   NULL,   'l',    arg_string, APTR(&G.logfile),   "save logs to file"},
    {"pidfile", NEED_ARG,   NULL,   'p',    arg_string, APTR(&G.pidfile),   "pidfile name (default: " DEFAULT_PID ")"},
    {"isunix",  NO_ARGS,    NULL,   'u',    arg_string, APTR(&G.isunix),    "use UNIX socket instead of network"},
    {"verbose", NO_ARGS,    NULL,   'v',    arg_none,    APTR(&G.verb),     "verbose level (each -v increases)"},
    {"fitsheader",NEED_ARG, NULL,   'f',    arg_string, APTR(&G.fitsheader),"fits-header for weather data"},
    end_option
};

void signals(int signo){
    if(signo){
        if(signals != signal(signo, SIG_IGN)) exit(signo); // function called "as is", before sig registration
        if(childpid == 0){ // child -> test USR1/USR2
            LOGDBG("Child gotta signal %d", signo);
            if(signo == SIGUSR1){
                forbidden = 1;
                LOGMSG("Got signal `observations forbidden`");
                signal(signo, signals);
                return;
            }else if(signo == SIGUSR2){
                forbidden = 0;
                LOGMSG("Got signal `observations permitted`");
                signal(signo, signals);
                return;
            }
        }
    }
    if(childpid){ // master
        LOGERR("Main process exits with status %d", signo);
        if(G.pidfile) unlink(G.pidfile);
        if(G.fitsheader) unlink(G.fitsheader);
        exit(1);
    }else{ // child
        if(running){
            LOGERR("Stop running");
            running = 0; // let make cleanup
        }
    }
}

static int init_ipc(void){
    umask(0); // for read-write semaphore
    shm_fd = shm_open(SHM_NAME, O_RDWR, 0644); // try to open existant SHM
    if(shm_fd == -1){
        printf("Create new shared memory\n");
        // no - create new
        shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0644);
        if(shm_fd == -1){
            LOGERR("shm_open (create) failed: %s", strerror(errno));
            return -1;
        }
        if(ftruncate(shm_fd, sizeof(weather_data_t)) == -1){
            LOGERR("ftruncate failed: %s", strerror(errno));
            return -1;
        }
        shared_data = mmap(NULL, sizeof(weather_data_t), PROT_READ | PROT_WRITE,
                           MAP_SHARED, shm_fd, 0);
        if(shared_data == MAP_FAILED){
            LOGERR("mmap failed: %s", strerror(errno));
            return -1;
        }
        // default values to data
    }else{
        DBG("Use existant SHM\n");
        shared_data = mmap(NULL, sizeof(weather_data_t), PROT_READ | PROT_WRITE,
                           MAP_SHARED, shm_fd, 0);
        if(shared_data == MAP_FAILED){
            LOGERR("mmap failed: %s", strerror(errno));
            return -1;
        }
    }
    memset(shared_data, 0, sizeof(weather_data_t));
    // create samaphore if no
    sem = sem_open(SEM_NAME, O_CREAT, 0666, 1);
    if(sem == SEM_FAILED){
        LOGERR("sem_open failed: %s", strerror(errno));
        return -1;
    }
    return 0;
}

// free IPC
static void cleanup_ipc(void){
    if (sem != NULL) {
        sem_close(sem);
        DBG("semaphore closed\n");
        if(-1 == sem_unlink(SEM_NAME)) LOGERR("Can't delete semaphore");
    }
    if(shared_data != NULL){
        DBG("memory unmapped\n");
        munmap(shared_data, sizeof(weather_data_t));
    }
    if(shm_fd != -1){
        close(shm_fd);
        DBG("close shared mem\n");
        if(shm_unlink(SHM_NAME) == -1){
            LOGERR("can't unlink SHM");
        }
    }
}

// update record of FITS header
static void FITS_update(const char *line, int finish){
    static FILE *tmp = NULL;
    static char templ[32]; // temporary file name
    if(!tmp){ // try to create new temporary file
        sprintf(templ, "/tmp/fitshdrXXXXXX");
        int fd = mkstemp(templ);
        if(fd < 0){
            WARN("mkstemp()");
            LOGERR("Can't create temporary file!");
            return;
        }
        tmp = fdopen(fd, "w");
        if(!tmp){
            WARN("fdopen()");
            LOGERR("Error in fdopen()");
            return;
        }
    }
    fprintf(tmp, "%s\n", line);
    if(finish){ // move temporary file into new location
        fclose(tmp);
        tmp = NULL;
        chmod(templ, 0644);
        if(rename(templ, G.fitsheader) < 0){
            WARN("rename(%s, %s)", templ, G.fitsheader);
            LOGERR("Error in rename()");
        }
    }
}

static void update_shm(weather_data_t *data){
    if(sem_wait(sem) == -1){
        LOGWARN("sem_wait failed: %s", strerror(errno));
    }else{
        memcpy(shared_data, data, sizeof(weather_data_t));
        sem_post(sem);
        LOGDBG("Weather data updated");
    }
}

static void parse_line(const char *line, weather_data_t *data) {
    char key[SL_KEY_LEN];
    char value[SL_VAL_LEN];

    int update = 0; // 0 for updating, 1 for finishing, -1 for error

    if(sl_get_keyval(line, key, value)){
        if(strcmp(key, "WEATHER") == 0){
            data->weather = (weather_condition_t) atoi(value);
            printf("got weather: %d\n", data->weather);
        }else if (strcmp(key, "WINDMAX1") == 0){
            data->windmax = atof(value);
            printf("got windmax: %g\n", data->windmax);
        }else if (strcmp(key, "PRECIP") == 0){
            data->rain = atoi(value);
            printf("got rain: %d\n", data->rain);
        }else if (strcmp(key, "CLOUDS") == 0){
            data->clouds = atof(value);
            printf("got clouds: %g\n", data->clouds);
        }else if (strcmp(key, "WIND") == 0){
            data->wind = atof(value);
            printf("got wind: %g\n", data->wind);
        }else if (strcmp(key, "EXTTEMP") == 0){
            data->exttemp = atof(value);
            printf("got temp: %g\n", data->exttemp);
        }else if (strcmp(key, "PRESSURE") == 0){
            data->pressure = atof(value);
            printf("got pressure: %g\n", data->pressure);
        }else if (strcmp(key, "HUMIDITY") == 0){
            data->humidity = atof(value);
            printf("got humidity: %g\n", data->humidity);
        }else if (strcmp(key, "PROHIBIT") == 0){
            data->prohibited = atoi(value);
        }else if (strcmp(key, "TMEAS") == 0){ // last line in message -> update
            data->last_update = atof(value);
            if(data->weather == WEATHER_PROHIBITED || forbidden) data->prohibited = 1;
            else if(data->weather < WEATHER_PROHIBITED) data->prohibited = 0;
            // update all
            update_shm(data);
            update = 1;
        }else update = -1;
        if(update > -1 && G.fitsheader){
            FITS_update(line, update);
        }
    }
}

static int request_weather_data(sl_sock_t *sock){
    static time_t tcur = 0;
    char *request = "get\n";
    time_t tnow = time(NULL);
    if(tnow - tcur >= WEAT_TMOUT){
        tcur = tnow;
    }else return 1; // not now

    DBG("try to send request: '%s", request);
    if(sl_sock_sendstrmessage(sock, request) < 1){
        LOGWARN("Can't poll new data");
        return -1;
    }
    return 0;
}

static void run_daemon(){
    char line[256];
    weather_data_t new_data;
    sl_socktype_e stype = (G.isunix) ? SOCKT_UNIX : SOCKT_NET;
    DBG("Try to connect to %s", G.node);
    sl_sock_t *sock = sl_sock_run_client(stype, G.node, 4096);
    if(!sock){
        DBG("Can't connect");
        LOGERR("Can't connect to meteodaemon over socket with node %s", G.node);
        return;
    }
    LOGMSG("Connected to meteodaemon %s", G.node);
    memcpy(&new_data, shared_data, sizeof(weather_data_t));
    time_t lastert = time(NULL);

    while(running){
        time_t tnow = time(NULL);
        int req = -1;
        if(sock) req = request_weather_data(sock);
        if(req == -1){
            int diff = tnow - lastert;
            DBG("diff = %d", diff);
            if(diff > RECONN_TMOUT){ // try to reconnect
                LOGERR("Failed to request weather data, retry");
                if(sock) sl_sock_delete(&sock);
                if(!(sock = sl_sock_run_client(stype, G.node, 4096))){
                    new_data.weather = WEATHER_TERRIBLE; // no connection to weather server, don't allow to open
                    update_shm(&new_data);
                    lastert += RECONN_TMOUT;
                }else{
                    LOGMSG("Reconnected to %s", G.node);
                    lastert = tnow;
                }
            }
        }else if(req == 0) lastert = tnow;

        while(sl_sock_readline(sock, line, 255) > 0){
            DBG("Parse '%s'", line);
            parse_line(line, &new_data);
        }
        usleep(500000);
    }
    sl_sock_delete(&sock); // disconnect and clear memory
    DBG("run_daemon() exited");
}

int main(int argc, char *argv[]){
    sl_init();
    sl_parseargs(&argc, &argv, opts);
    if(!G.node) ERRX("Point node to connect");
    if(G.fitsheader){
        FILE *fitsfile = fopen(G.fitsheader, "w");
        if(!fitsfile){
            WARN("Can't create FITS header %s", G.fitsheader);
            FREE(G.fitsheader);
        }else fclose(fitsfile);
    }
    sl_check4running(NULL, G.pidfile);
    if(G.logfile){
        sl_loglevel_e lvl = LOGLEVEL_ERR + G.verb;
        if(lvl >= LOGLEVEL_AMOUNT) lvl = LOGLEVEL_AMOUNT - 1;
        DBG("Loglevel: %d", lvl);
        if(!OPENLOG(G.logfile, lvl, 1)) ERRX("Can't open log file %s", G.logfile);
        LOGMSG("Started");
    }
    signal(SIGTERM, signals);
    signal(SIGINT, signals);
    signal(SIGUSR1, SIG_IGN);
    signal(SIGUSR2, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
#ifndef EBUG
    sl_daemonize();
    while(1){ // guard for dead processes
        childpid = fork();
        if(childpid){
            LOGDBG("create child with PID %d\n", childpid);
            DBG("Created child with PID %d\n", childpid);
            wait(NULL);
            WARNX("Child %d died\n", childpid);
            LOGWARN("Child %d died\n", childpid);
            sleep(1);
        }else{
            prctl(PR_SET_PDEATHSIG, SIGTERM); // send SIGTERM to child when parent dies
            break; // go out to normal functional
        }
    }
#endif
    // react for USRx only in child
    signal(SIGUSR1, signals);
    signal(SIGUSR2, signals);

    if(init_ipc() != 0){
        LOGERR("IPC initialization failed");
        exit(EXIT_FAILURE);
    }
    run_daemon();
    LOGDBG("Daemon is dead");
    cleanup_ipc();
    LOGDBG("IPC cleaned");
    return 0;
}
