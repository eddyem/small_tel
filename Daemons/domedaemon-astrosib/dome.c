/*
 * This file is part of the domedaemon-astrosib project.
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

#include <stdio.h>
#include <string.h>

#include "astrosib_proto.h"
#include "dome.h"

// number of relay turning on/off motors power
#define MOTRELAY_NO     1
// state of relay to turn power on/off
#define MOTRELAY_ON     1
#define MOTRELAY_OFF    0

// time interval for requiring current dome status in idle state (e.g. trigger watchdog)
#define STATUSREQ_IDLE    10.
// update interval in moving state
#define STATUSREQ_MOVE    0.5

// dome status by polling (each STATUSREQ_IDLE seconds)
static dome_status_t dome_status = {0};
// finite state machine state
static dome_state_t state = DOME_S_IDLE;
// last time dome_status was updated
static double last_status_time = 0.;
// current update interval
static double status_req_interval = STATUSREQ_MOVE;
// serial device
static sl_tty_t *serialdev = NULL;
static pthread_mutex_t serialmutex = PTHREAD_MUTEX_INITIALIZER;

void dome_serialdev(sl_tty_t *serial){
    serialdev = serial;
}
/**
 * @brief serial_write - write buffer without "\r" on end to port
 * @param cmd - data to send
 * @param answer - buffer for answer (or NULL if not need)
 * @param anslen - length of `answer`
 * @return error code
 */
static int serial_write(const char *cmd, char *answer, int anslen){
    if(!serialdev || !cmd || !answer || anslen < 2) return FALSE;
    static char *buf = NULL;
    static size_t buflen = 0;
    DBG("Write %s", cmd);
    size_t cmdlen = strlen(cmd);
    if(buflen < cmdlen + 2){
        buflen = (cmdlen + 4096) / 4096;
        buflen *= 4096;
        if(!(buf = realloc(buf, buflen))){
            LOGERR("serial_write(): realloc() failed!");
            ERRX("serial_write(): realloc() failed!");
        }
    }
    size_t _2write = snprintf(buf, buflen-1, "%s\r", cmd);
    DBG("try to send %zd bytes", _2write);
    if(sl_tty_write(serialdev->comfd, buf, _2write)) return FALSE;
    int got = 0, totlen = 0;
    --anslen; // for /0
    do{
        got = sl_tty_read(serialdev);
        if(got > 0){
            if(got > anslen) return FALSE; // buffer overflow
            memcpy(answer, serialdev->buf, got);
            totlen += got;
            answer += got;
            anslen -= got;
            answer[0] = 0;
        }
        DBG("got = %d", got);
    }while(got > 0 && anslen);
    if(got < 0){
        LOGERR("serial_write(): serial device disconnected!");
        ERRX("serial_write(): serial device disconnected!");
    }
    if(totlen < 1) return FALSE; // no answer received
    if(answer[-1] == '\r') answer[-1] = 0; // remove trailing trash
    return TRUE;
}

// return TRUE if can parse status
static int parsestatus(const char *buf){
    if(!buf) return FALSE;
    DBG("buf=%s", buf);
    int n = sscanf(buf, ASIB_CMD_STATUS "%d,%d,%d,%d,%f,%f,%f,%f,%f,%f,%d,%d,%d,%d,%d",
                   &dome_status.coverstate[0], &dome_status.coverstate[1],
                   &dome_status.encoder[0], &dome_status.encoder[1],
                   &dome_status.Tin, &dome_status.Tout,
                   &dome_status.Imot[0], &dome_status.Imot[1], &dome_status.Imot[2], &dome_status.Imot[3],
                   &dome_status.relay[0], &dome_status.relay[1], &dome_status.relay[2],
                   &dome_status.rainArmed, &dome_status.israin);
    DBG("n=%d", n);
    if(n != 15){
        WARNX("Something wrong with STATUS answer");
        LOGWARN("Something wrong with STATUS answer");
        LOGWARNADD("%s", buf);
        return FALSE;
    }
    return TRUE;
}

// check status; return FALSE if failed
static int check_status(){
    char buf[BUFSIZ];
    // clear input buffers
    int got = sl_tty_read(serialdev);
    if(got > 0) printf("Got from serial %zd bytes of trash: `%s`\n", serialdev->buflen, serialdev->buf);
    else if(got < 0){
        LOGERR("Serial device disconnected?");
        ERRX("Serial device disconnected?");
    }
    DBG("Require status");
    if(!serial_write(ASIB_CMD_STATUS, buf, BUFSIZ)) return FALSE;
    int ret = FALSE;
    if(parsestatus(buf)){
        last_status_time = sl_dtime();
        ret = TRUE;
    }
    return ret;
}

// run naked command or command with parameters
static int runcmd(const char *cmd, const char *par){
    char buf[128];
    if(!cmd) return FALSE;
    DBG("Send command %s with par %s", cmd, par);
    if(!par) snprintf(buf, 127, "%s", cmd);
    else snprintf(buf, 127, "%s%s", cmd, par);
    if(!serial_write(buf, buf, 128)) return FALSE;
    if(strncmp(buf, "OK", 2)) return FALSE;
    return TRUE;
}

// check current state and turn on/off relay if need
static void chkrelay(){
    static dome_state_t oldstate = DOME_S_MOVING;
    static double t0 = 0.;
    if(state != DOME_S_MOVING) return;
    if(dome_status.coverstate[0] == COVER_INTERMEDIATE ||
        dome_status.coverstate[1] == COVER_INTERMEDIATE){ // still moving
            oldstate = DOME_S_MOVING;
            return;
    }
    DBG("state=%d, oldstate=%d", state, oldstate);
    // OK, we are on place - turn off motors' power after 5 seconds
    if(oldstate == DOME_S_MOVING){ // just stopped - fire pause
        t0 = sl_dtime();
        oldstate = DOME_S_IDLE;
        DBG("START 5s pause");
        return;
    }else{ // check pause
        if(sl_dtime() - t0 < POWER_STOP_TIMEOUT) return;
    }
    DBG("5s out -> turn off power");
    char buf[128];
    snprintf(buf, 127, "%s%d,%d", ASIB_CMD_RELAY, MOTRELAY_NO, MOTRELAY_OFF);
    if(serial_write(buf, buf, 128) && check_status()){
        DBG("Check are motors really off");
        if(dome_status.relay[MOTRELAY_NO-1] == MOTRELAY_OFF){
            DBG("OK state->IDLE");
            state = DOME_S_IDLE;
            oldstate = DOME_S_MOVING;
        }
    }
}

// turn ON motors' relay
static int motors_on(){
    char buf[128];
    snprintf(buf, 127, "%s%d,%d", ASIB_CMD_RELAY, MOTRELAY_NO, MOTRELAY_ON);
    if(serial_write(buf, buf, 128) && check_status()){
        DBG("Check are motors really on");
        if(dome_status.relay[MOTRELAY_NO-1] == MOTRELAY_ON){
            DBG("OK state->MOVING");
            state = DOME_S_MOVING;
            return TRUE;
        }
    }
    return FALSE;
}

// just get current status
double get_dome_status(dome_status_t *s){
    if(s){
        pthread_mutex_lock(&serialmutex);
        *s = dome_status;
        pthread_mutex_unlock(&serialmutex);
    }
    return last_status_time;
}

dome_state_t get_dome_state(){return state;}

dome_state_t dome_poll(dome_cmd_t cmd, int par){
    char buf[128];
    int st = DOME_S_ERROR;
    double curtime = sl_dtime();
    //DBG("curtime-lasttime=%g", curtime - last_status_time);
    // simple polling and there's a lot of time until next serial device poll
    if(cmd == DOME_POLL && curtime - last_status_time < status_req_interval)
        return state;
    pthread_mutex_lock(&serialmutex);
    // check if we need to turn ON motors' relay
    switch(cmd){
        case DOME_OPEN:
        case DOME_CLOSE:
        case DOME_OPEN_ONE:
        case DOME_CLOSE_ONE:
            if(!motors_on()) goto ret;
            break;
        default:
            break;
    }
    switch(cmd){
        case DOME_STOP:
            if(!runcmd(ASIB_CMD_STOP, NULL)) goto ret;
            break;
        case DOME_OPEN:
            if(!runcmd(ASIB_CMD_OPEN, NULL)) goto ret;
            break;
        case DOME_CLOSE:
            if(!runcmd(ASIB_CMD_CLOSE, NULL)) goto ret;
            break;
        case DOME_OPEN_ONE: // due to bug in documentation, 0 - OPEN
            if(par < 1 || par > 2) goto ret;
            snprintf(buf, 127, "%d,0", par-1);
            if(!runcmd(ASIB_CMD_MOVEONE, buf)) goto ret;
            break;
        case DOME_CLOSE_ONE: // due to bug in documentation, 90 - CLOSE
            if(par < 1 || par > 2) goto ret;
            snprintf(buf, 127, "%d,90", par-1);
            if(!runcmd(ASIB_CMD_MOVEONE, buf)) goto ret;
            break;
        case DOME_RELAY_ON:
            if(par < NRELAY_MIN || par > NRELAY_MAX) goto ret;
            snprintf(buf, 127, "%d,1", par);
            if(!runcmd(ASIB_CMD_RELAY, buf)) goto ret;
            break;
        case DOME_RELAY_OFF:
            if(par < NRELAY_MIN || par > NRELAY_MAX) goto ret;
            snprintf(buf, 127, "%d,0", par);
            if(!runcmd(ASIB_CMD_RELAY, buf)) goto ret;
            break;
        default:
            break;
    }
ret:
    if(check_status()){
        chkrelay();
        st = state;
    }
    if(state == DOME_S_IDLE) status_req_interval = STATUSREQ_IDLE;
    else status_req_interval = STATUSREQ_MOVE;
    pthread_mutex_unlock(&serialmutex);
    return st;
}
