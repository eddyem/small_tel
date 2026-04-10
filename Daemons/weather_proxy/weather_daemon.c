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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <semaphore.h>
#include <signal.h>
#include <time.h>
#include <stdarg.h>

#include "weather_data.h"

#define DEAD_TMOUT      15
#define RECONN_TMOUT    5
#define WEAT_TMOUT      1

#define SHM_NAME "/weather_shm"
#define SEM_NAME "/weather_sem"

static int shm_fd = -1;
static sem_t *sem = NULL;
static weather_data_t *shared_data = NULL;
static volatile int running = 1;

#define log_message(...)    do{printf("message: "); printf(__VA_ARGS__); printf("\n");}while(0)
#define log_error(...)      do{printf("error: "); printf(__VA_ARGS__); printf("\n");}while(0)

static void signal_handler(int sig) {
    if (sig == SIGTERM || sig == SIGINT) {
        running = 0;
        log_message("Received signal %d, shutting down", sig);
    }
}

static int init_ipc(void) {
    umask(0); // for read-write semaphore
    shm_fd = shm_open(SHM_NAME, O_RDWR, 0644); // try to open existant SHM
    if (shm_fd == -1) {
        printf("Create new shared memory\n");
        // no - create new
        shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0644);
        if (shm_fd == -1) {
            log_error("shm_open (create) failed: %s", strerror(errno));
            return -1;
        }
        if (ftruncate(shm_fd, sizeof(weather_data_t)) == -1) {
            log_error("ftruncate failed: %s", strerror(errno));
            return -1;
        }
        shared_data = mmap(NULL, sizeof(weather_data_t), PROT_READ | PROT_WRITE,
                           MAP_SHARED, shm_fd, 0);
        if (shared_data == MAP_FAILED) {
            log_error("mmap failed: %s", strerror(errno));
            return -1;
        }
        // default values to data
    } else {
        printf("Use existant SHM\n");
        // use existant SHM
        shared_data = mmap(NULL, sizeof(weather_data_t), PROT_READ | PROT_WRITE,
                           MAP_SHARED, shm_fd, 0);
        if (shared_data == MAP_FAILED) {
            log_error("mmap failed: %s", strerror(errno));
            return -1;
        }
    }

    memset(shared_data, 0, sizeof(weather_data_t));

    // create samaphore if no
    sem = sem_open(SEM_NAME, O_CREAT, 0666, 1);
    if (sem == SEM_FAILED) {
        log_error("sem_open failed: %s", strerror(errno));
        return -1;
    }
    return 0;
}

// free IPC
static void cleanup_ipc(void) {
    if (sem != NULL) {
        sem_close(sem);
        printf("semaphore closed\n");
        if(-1 == sem_unlink(SEM_NAME)) perror("Can't delete semaphore");
    }
    if (shared_data != NULL) {
        printf("memory unmapped\n");
        munmap(shared_data, sizeof(weather_data_t));
    }
    if (shm_fd != -1) {
        close(shm_fd);
        printf("close shared mem\n");
        if (shm_unlink(SHM_NAME) == -1) {
            perror("can't unlink SHM");
        }
    }
}

static void parse_line(const char *line, weather_data_t *data) {
    char key[64];
    char value[256];
    if (sscanf(line, "%63[^=]=%255s", key, value) == 2) {
        if (strcmp(key, "WEATHER") == 0) {
            data->weather = (weather_condition_t) atoi(value);
            printf("got weather: %d\n", data->weather);
        } else if (strcmp(key, "WINDMAX1") == 0) {
            data->windmax = atof(value);
            printf("got windmax: %g\n", data->windmax);
        } else if (strcmp(key, "PRECIP") == 0) {
            data->rain = atoi(value);
            printf("got rain: %d\n", data->rain);
        } else if (strcmp(key, "CLOUDS") == 0) {
            data->clouds = atof(value);
            printf("got clouds: %g\n", data->clouds);
        } else if (strcmp(key, "WIND") == 0) {
            data->wind = atof(value);
            printf("got wind: %g\n", data->wind);
        } else if (strcmp(key, "EXTTEMP") == 0) {
            data->exttemp = atof(value);
            printf("got temp: %g\n", data->exttemp);
        } else if (strcmp(key, "PRESSURE") == 0) {
            data->pressure = atof(value);
            printf("got pressure: %g\n", data->pressure);
        } else if (strcmp(key, "HUMIDITY") == 0) {
            data->humidity = atof(value);
            printf("got humidity: %g\n", data->humidity);
        } else if (strcmp(key, "PROHIBIT") == 0) {
            data->prohibited = atoi(value);
        } else if (strcmp(key, "TMEAS") == 0) {
            data->last_update = atof(value);
            if(data->weather == WEATHER_PROHIBITED) data->prohibited = 1;
            else if(data->weather < WEATHER_TERRIBLE) data->prohibited = 0;
            // update all
            if (sem_wait(sem) == -1) {
                log_error("sem_wait failed: %s", strerror(errno));
            } else {
                memcpy(shared_data, data, sizeof(weather_data_t));
                sem_post(sem);
                log_message("Weather data updated");
            }
        }
    }
}

static int sock = -1;
static FILE *sock_file = NULL;

static int opensock(const char *server_ip, int port){
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        log_error("socket creation failed: %s", strerror(errno));
        return -1;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        log_error("invalid address: %s", server_ip);
        close(sock);
        sock = -1;
        return -1;
    }

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        log_error("connect failed: %s", strerror(errno));
        close(sock);
        sock = -1;
        return -1;
    }

    int flags = fcntl(sock, F_GETFL, 0);
    if (flags == -1){
        perror("fcntl F_GETFL");
    }else{
        if (fcntl(sock, F_SETFL, flags | O_NONBLOCK) == -1)  perror("fcntl F_SETFL");
    }

    sock_file = fdopen(sock, "r");
    if (!sock_file) {
        log_error("fdopen failed: %s", strerror(errno));
        close(sock);
        sock = -1;
        return -1;
    }

    return 0;
}

static int request_weather_data() {
    static time_t tcur = 0;

    char *request = "get\n";
    time_t tnow = time(NULL);
    if(tnow - tcur >= WEAT_TMOUT){
        tcur = tnow;
    }else return 1; // not now

    printf("try to send request: '%s", request);
    if (send(sock, request, strlen(request), 0) < 0) {
        log_error("send failed: %s", strerror(errno));
        close(sock);
        sock = -1;
        return -1;
    }

    return 0;
}

static void run_daemon(const char *server_ip, int port) {
    char line[256];
    weather_data_t new_data;
    opensock(server_ip, port); // just try: in case of error reopen next time
    memcpy(&new_data, shared_data, sizeof(weather_data_t));
    time_t lastert = time(NULL);
    while (running) {
        time_t tnow = time(NULL);
        if (-1 == sock || request_weather_data() == -1) {
            if(tnow - lastert > RECONN_TMOUT){ // try to reconnect
                log_error("Failed to request weather data, retry");
                if(-1 == opensock(server_ip, port)) lastert += 5;
                else lastert = tnow;
            }
        }else lastert = tnow;

        while (fgets(line, sizeof(line), sock_file)) {
            line[strcspn(line, "\r\n")] = '\0';
            printf("parse '%s'\n", line);
            parse_line(line, &new_data);
        }
    }
    close(sock);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <server_ip> <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *server_ip = argv[1];
    int port = atoi(argv[2]);
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Invalid port\n");
        exit(EXIT_FAILURE);
    }

    log_message("Starting weather daemon, server %s:%d", server_ip, port);

    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);
    signal(SIGPIPE, SIG_IGN);

    if (init_ipc() != 0) {
        log_error("IPC initialization failed");
        exit(EXIT_FAILURE);
    }

    run_daemon(server_ip, port);

    cleanup_ipc();
    log_message("Weather daemon stopped");
    return 0;
}
