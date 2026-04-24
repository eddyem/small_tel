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
#include <errno.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <semaphore.h>
#include <unistd.h>

#include "weather_data.h"

int get_weather_data(weather_data_t *data) {
    int shm_fd;
    sem_t *sem;
    weather_data_t *shared_data;
    int ret = 0;

    shm_fd = shm_open(SHM_NAME, O_RDONLY, 0600);
    if (shm_fd == -1) {
        perror("shm_open");
        return -1;
    }

    shared_data = mmap(NULL, sizeof(weather_data_t), PROT_READ, MAP_SHARED, shm_fd, 0);
    if (shared_data == MAP_FAILED) {
        perror("mmap");
        close(shm_fd);
        return -1;
    }

    sem = sem_open(SEM_NAME, 0);
    if (sem == SEM_FAILED) {
        perror("sem_open");
        munmap(shared_data, sizeof(weather_data_t));
        close(shm_fd);
        return -1;
    }

    if (sem_wait(sem) == -1) {
        perror("sem_wait");
        ret = -1;
        goto cleanup;
    }

    memcpy(data, shared_data, sizeof(weather_data_t));

    sem_post(sem);

cleanup:
    sem_close(sem);
    munmap(shared_data, sizeof(weather_data_t));
    close(shm_fd);
    return ret;
}
