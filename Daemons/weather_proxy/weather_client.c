#include "weather_data.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <semaphore.h>
#include <unistd.h>

#define SHM_NAME "/weather_shm"
#define SEM_NAME "/weather_sem"

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
