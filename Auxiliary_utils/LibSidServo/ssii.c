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
#include <string.h>

#include "dbg.h"
#include "serial.h"
#include "ssii.h"

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

/**
 * @brief SSconvstat - convert stat from SSII format to human
 * @param s (i) - just read data
 * @param m (o) - output
 * @param t - measurement time
 */
void SSconvstat(const SSstat *s, mountdata_t *m, double t){
    if(!s || !m) return;
/*
#ifdef EBUG
    static double t0 = -1.;
    if(t0 < 0.) t0 = dtime();
#endif
    DBG("Convert, t=%g", dtime()-t0);
*/
    m->motXposition.val = X_MOT2RAD(s->Xmot);
    m->motYposition.val = Y_MOT2RAD(s->Ymot);
    m->motXposition.t = m->motYposition.t = t;
    // fill encoder data from here, as there's no separate enc thread
    if(!Conf.SepEncoder){
        m->encXposition.val = X_ENC2RAD(s->Xenc);
        m->encYposition.val = Y_ENC2RAD(s->Yenc);
        m->encXposition.t = m->encYposition.t = t;
    }
    //m->lastmotposition.X = X_MOT2RAD(s->XLast);
    //m->lastmotposition.Y = Y_MOT2RAD(s->YLast);
    //m->lastmotposition.msrtime = *tdat;
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
    DBG("send %zd bytes: %s", d.len, d.buf);
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
    DBG("send %zd bytes: %s", d.len, d.buf);
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

