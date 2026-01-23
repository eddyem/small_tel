/*
 * This file is part of the libsidservo project.
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

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "main.h"
#include "serial.h"
#include "ssii.h"

int X_ENC_ZERO, Y_ENC_ZERO;
double X_MOT_STEPSPERREV = 1., Y_MOT_STEPSPERREV = 1., X_ENC_STEPSPERREV = 1., Y_ENC_STEPSPERREV = 1.;

uint16_t SScalcChecksum(uint8_t *buf, int len){
    uint16_t checksum = 0;
    for(int i = 0; i < len; i++){
        //DBG("data[%d]=0x%X", i, *buf);
        checksum += *buf++;
    }
    checksum ^= 0xFF00; // invert high byte
    //DBG("Checksum of %d bytes: 0x%04x", len, checksum);
    return checksum;
}

// Next three functions runs under locked mountdata_t mutex and shouldn't call locked it again!!
static void chkstopstat(int32_t *prev, int32_t cur, int *nstopped, axis_status_t *stat){
    if(*prev == INT32_MAX){
        *stat = AXIS_STOPPED;
        DBG("START");
    }else if(*stat != AXIS_STOPPED){
        if(*prev == cur && ++(*nstopped) > MOTOR_STOPPED_CNT){
            *stat = AXIS_STOPPED;
            DBG("AXIS stopped");
        }
    }else if(*prev != cur){
        DBG("AXIS moving");
        *nstopped = 0;
    }
    *prev = cur;
}
// check for stopped/pointing states
static void ChkStopped(const SSstat *s, mountdata_t *m){
    static int32_t Xmot_prev = INT32_MAX, Ymot_prev = INT32_MAX; // previous coordinates
    static int Xnstopped = 0, Ynstopped = 0; // counters to get STOPPED state
    chkstopstat(&Xmot_prev, s->Xmot, &Xnstopped, &m->Xstate);
    chkstopstat(&Ymot_prev, s->Ymot, &Ynstopped, &m->Ystate);
}

/**
 * @brief SSconvstat - convert stat from SSII format to human
 * @param s (i) - just read data
 * @param m (o) - output
 * @param t - measurement time
 */
void SSconvstat(const SSstat *s, mountdata_t *m, struct timespec *t){
    if(!s || !m || !t) return;
    m->motXposition.val = X_MOT2RAD(s->Xmot);
    m->motYposition.val = Y_MOT2RAD(s->Ymot);
    ChkStopped(s, m);
    m->motXposition.t = m->motYposition.t = *t;
    // fill encoder data from here, as there's no separate enc thread
    if(!Conf.SepEncoder){
        m->encXposition.val = Xenc2rad(s->Xenc);
        DBG("encx: %g", m->encXposition.val);
        m->encYposition.val = Yenc2rad(s->Yenc);
        m->encXposition.t = m->encYposition.t = *t;
        getXspeed(); getYspeed();
    }
    m->keypad = s->keypad;
    m->extradata.ExtraBits = s->ExtraBits;
    m->extradata.ain0 = s->ain0;
    m->extradata.ain1 = s->ain1;
    m->extradata.XBits = s->XBits;
    m->extradata.YBits = s->YBits;
    m->millis = s->millis;
    m->voltage = (double)s->voltage / 10.;
    m->temperature = ((double)s->tF - 32.) * 5. / 9.;
}

/**
 * @brief SStextcmd - send simple text command to mount and return answer
 * @param cmd (i) - command to send
 * @param answer (o) - answer (or NULL)
 * @return
 */
int SStextcmd(const char *cmd, data_t *answer){
    if(!cmd){
        DBG("try to send empty command");
        return FALSE;
    }
    data_t d;
    d.buf = (uint8_t*) cmd;
    d.len = d.maxlen = strlen(cmd);
    //DBG("send %zd bytes: %s", d.len, d.buf);
    return MountWriteRead(&d, answer);
}

// the same as SStextcmd, but not adding EOL - send raw 'cmd'
int SSrawcmd(const char *cmd, data_t *answer){
    if(!cmd){
        DBG("try to send empty command");
        return FALSE;
    }
    data_t d;
    d.buf = (uint8_t*) cmd;
    d.len = d.maxlen = strlen(cmd);
    //DBG("send %zd bytes: %s", d.len, d.buf);
    return MountWriteReadRaw(&d, answer);
}

/**
 * @brief SSgetint - send text command and return integer answer
 * @param cmd (i) - command to send
 * @param ans (o) - intval (INT64_MAX if error)
 * @return FALSE if failed
 */
int SSgetint(const char *cmd, int64_t *ans){
    if(!cmd || !ans) return FALSE;
    uint8_t buf[64];
    data_t d = {.buf = buf, .len = 0, .maxlen = 64};
    if(!SStextcmd(cmd, &d)) return FALSE;
    int64_t retval = INT64_MAX;
    if(d.len > 1){
        char *ptr = (char*) buf;
        size_t i = 0;
        for(; i < d.len; ++i){
            if(isdigit(*ptr)) break;
            ++ptr;
        }
        if(i < d.len) retval = atol(ptr);
    }
    DBG("read int: %" PRIi64, retval);
    *ans = retval;
    return TRUE;
}

/**
 * @brief SSsetterI - integer setter
 * @param cmd - command to send
 * @param ival - value
 * @return false if failed
 */
int SSsetterI(const char *cmd, int32_t ival){
    char buf[128];
    snprintf(buf, 127, "%s%" PRIi32, cmd, ival);
    return SStextcmd(buf, NULL);
}

int SSstop(int emerg){
    int i = 0;
    const char *cmdx = (emerg) ? CMD_EMSTOPX : CMD_STOPX;
    const char *cmdy = (emerg) ? CMD_EMSTOPY : CMD_STOPY;
    for(; i < 10; ++i){
        if(!SStextcmd(cmdx, NULL)) continue;
        if(SStextcmd(cmdy, NULL)) break;
    }
    if(i == 10) return FALSE;
    return TRUE;
}

// update motors' positions due to encoders'
mcc_errcodes_t updateMotorPos(){
    mountdata_t md = {0};
    if(Conf.RunModel) return MCC_E_OK;
    double t0 = timefromstart(), t = 0.;
    struct timespec curt;
    DBG("start @ %g", t0);
    do{
        t = timefromstart();
        if(!curtime(&curt)){
            usleep(10000);
            continue;
        }
        //DBG("XENC2RAD: %g (xez=%d, xesr=%.10g)", Xenc2rad(32424842), X_ENC_ZERO, X_ENC_STEPSPERREV);
        if(MCC_E_OK == getMD(&md)){
            if(md.encXposition.t.tv_sec == 0 || md.encYposition.t.tv_sec == 0){
                DBG("Just started? t-t0 = %g!", t - t0);
                usleep(10000);
                continue;
            }
            if(md.Xstate != AXIS_STOPPED || md.Ystate != AXIS_STOPPED) return MCC_E_OK;
            DBG("got; t pos x/y: %ld/%ld; tnow: %ld", md.encXposition.t.tv_sec, md.encYposition.t.tv_sec, curt.tv_sec);
            mcc_errcodes_t OK = MCC_E_OK;
            if(fabs(md.motXposition.val - md.encXposition.val) > Conf.EncodersDisagreement && md.Xstate == AXIS_STOPPED){
                DBG("NEED to sync X: motors=%g, axis=%g", md.motXposition.val, md.encXposition.val);
                DBG("new motsteps: %d", X_RAD2MOT(md.encXposition.val));
                if(!SSsetterI(CMD_MOTXSET, X_RAD2MOT(md.encXposition.val))){
                    DBG("Xpos sync failed!");
                    OK = MCC_E_FAILED;
                }else DBG("Xpos sync OK, Dt=%g", t - t0);
            }
            if(fabs(md.motYposition.val - md.encYposition.val) > Conf.EncodersDisagreement && md.Ystate == AXIS_STOPPED){
                DBG("NEED to sync Y: motors=%g, axis=%g", md.motYposition.val, md.encYposition.val);
                if(!SSsetterI(CMD_MOTYSET, Y_RAD2MOT(md.encYposition.val))){
                    DBG("Ypos sync failed!");
                    OK = MCC_E_FAILED;
                }else DBG("Ypos sync OK, Dt=%g", t - t0);
            }
            if(MCC_E_OK == OK){
                DBG("Encoders synced");
                return OK;
            }
        }
        DBG("NO DATA; dt = %g", t - t0);
    }while(t - t0 < 2.);
    return MCC_E_FATAL;
}
