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

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "dbg.h"
#include "serial.h"
#include "ssii.h"

conf_t Conf = {0};

/**
 * @brief quit - close all opened and return to default state
 */
static void quit(){
    DBG("Close serial devices");
    for(int i = 0; i < 10; ++i) if(SSstop(TRUE)) break;
    closeSerial();
    DBG("Exit");
}

/**
 * @brief init - open serial devices and do other job
 * @param c - initial configuration
 * @return error code
 */
static mcc_errcodes_t init(conf_t *c){
    FNAME();
    if(!c) return MCC_E_BADFORMAT;
    Conf = *c;
    mcc_errcodes_t ret = MCC_E_OK;
    if(!Conf.MountDevPath || Conf.MountDevSpeed < 1200){
        DBG("Define mount device path and speed");
        ret = MCC_E_BADFORMAT;
    }else if(!openMount(Conf.MountDevPath, Conf.MountDevSpeed)){
        DBG("Can't open %s with speed %d", Conf.MountDevPath, Conf.MountDevSpeed);
        ret = MCC_E_MOUNTDEV;
    }
    if(Conf.SepEncoder){
        if(!Conf.EncoderDevPath || Conf.EncoderDevSpeed < 1200){
            DBG("Define encoder device path and speed");
            ret = MCC_E_BADFORMAT;
        }else if(!openEncoder(Conf.EncoderDevPath, Conf.EncoderDevSpeed)){
            DBG("Can't open %s with speed %d", Conf.EncoderDevPath, Conf.EncoderDevSpeed);
            ret = MCC_E_ENCODERDEV;
        }
    }
    if(Conf.MountReqInterval > 1. || Conf.MountReqInterval < 0.05){
        DBG("Bad value of MountReqInterval");
        ret = MCC_E_BADFORMAT;
    }
    uint8_t buf[1024];
    data_t d = {.buf = buf, .len = 0, .maxlen = 1024};
    // read input data as there may be some trash on start
    if(!SSrawcmd(CMD_EXITACM, &d)) ret = MCC_E_FAILED;
    if(ret != MCC_E_OK) quit();
    return ret;
}

// check coordinates and speeds; return FALSE if failed
// TODO fix to real limits!!!
static int chkX(double X){
    if(X > 2.*M_PI || X < -2.*M_PI) return FALSE;
    return TRUE;
}
static int chkY(double Y){
    if(Y > 2.*M_PI || Y < -2.*M_PI) return FALSE;
    return TRUE;
}
static int chkXs(double s){
    if(s < 0. || s > X_SPEED_MAX) return FALSE;
    return TRUE;
}
static int chkYs(double s){
    if(s < 0. || s > Y_SPEED_MAX) return FALSE;
    return TRUE;
}


/**
 * @brief move2 - simple move to given point and stop
 * @param X - new X coordinate (radians: -pi..pi) or NULL
 * @param Y - new Y coordinate (radians: -pi..pi) or NULL
 * @return error code
 */
static mcc_errcodes_t move2(const double *X, const double *Y){
    if(!X && !Y) return MCC_E_BADFORMAT;
    if(X){
        if(!chkX(*X)) return MCC_E_BADFORMAT;
        int32_t tag = X_RAD2MOT(*X);
        DBG("X: %g, tag: %d", *X, tag);
        if(!SSsetterI(CMD_MOTX, tag)) return MCC_E_FAILED;
    }
    if(Y){
        if(!chkY(*Y)) return MCC_E_BADFORMAT;
        int32_t tag = Y_RAD2MOT(*Y);
        DBG("Y: %g, tag: %d", *Y, tag);
        if(!SSsetterI(CMD_MOTY, tag)) return MCC_E_FAILED;
    }
    return MCC_E_OK;
}

/**
 * @brief setspeed - set maximal speed over axis
 * @param X (i) - max speed or NULL
 * @param Y (i) - -//-
 * @return errcode
 */
static mcc_errcodes_t setspeed(const double *X, const double *Y){
    if(!X && !Y) return MCC_E_BADFORMAT;
    if(X){
        if(!chkXs(*X)) return MCC_E_BADFORMAT;
        int32_t spd = X_RS2MOTSPD(*X);
        if(!SSsetterI(CMD_SPEEDX, spd)) return MCC_E_FAILED;
    }
    if(Y){
        if(!chkYs(*Y)) return MCC_E_BADFORMAT;
        int32_t spd = Y_RS2MOTSPD(*Y);
        if(!SSsetterI(CMD_SPEEDY, spd)) return MCC_E_FAILED;
    }
    return MCC_E_OK;
}

/**
 * @brief move2s - move to target with given max speed
 * @param target (i) - target or NULL
 * @param speed (i) - speed or NULL
 * @return
 */
static mcc_errcodes_t move2s(const coords_t *target, const coords_t *speed){
    if(!target && !speed) return MCC_E_BADFORMAT;
    if(!target) return setspeed(&speed->X, &speed->Y);
    if(!speed) return move2(&target->X, &target->Y);
    if(!chkX(target->X) || !chkY(target->Y) || !chkXs(speed->X) || !chkYs(speed->Y))
        return MCC_E_BADFORMAT;
    char buf[128];
    int32_t spd = X_RS2MOTSPD(speed->X), tag = X_RAD2MOT(target->X);
    snprintf(buf, 127, "%s%" PRIi64 "%s%" PRIi64, CMD_MOTX, tag, CMD_MOTXYS, spd);
    if(!SStextcmd(buf, NULL)) return MCC_E_FAILED;
    spd = Y_RS2MOTSPD(speed->Y); tag = Y_RAD2MOT(target->Y);
    snprintf(buf, 127, "%s%" PRIi64 "%s%" PRIi64, CMD_MOTY, tag, CMD_MOTXYS, spd);
    if(!SStextcmd(buf, NULL)) return MCC_E_FAILED;
    return MCC_E_OK;
}

/**
 * @brief emstop - emergency stop
 * @return errcode
 */
static mcc_errcodes_t emstop(){
    if(!SSstop(TRUE)) return MCC_E_FAILED;
    return MCC_E_OK;
}
// normal stop
static mcc_errcodes_t stop(){
    if(!SSstop(FALSE)) return MCC_E_FAILED;
    return MCC_E_OK;
}

/**
 * @brief shortcmd - send and receive short binary command
 * @param cmd (io) - command
 * @return errcode
 */
static mcc_errcodes_t shortcmd(short_command_t *cmd){
    if(!cmd) return MCC_E_BADFORMAT;
    SSscmd s = {0};
    DBG("xmot=%g, ymot=%g", cmd->Xmot, cmd->Ymot);
    s.Xmot = X_RAD2MOT(cmd->Xmot);
    s.Ymot = Y_RAD2MOT(cmd->Ymot);
    s.Xspeed = X_RS2MOTSPD(cmd->Xspeed);
    s.Yspeed = Y_RS2MOTSPD(cmd->Yspeed);
    s.xychange = cmd->xychange;
    s.XBits = cmd->XBits;
    s.YBits = cmd->YBits;
    DBG("X->%d, Y->%d, Xs->%d, Ys->%d", s.Xmot, s.Ymot, s.Xspeed, s.Yspeed);
    if(!cmdS(&s)) return MCC_E_FAILED;
    cmd->Xmot = X_MOT2RAD(s.Xmot);
    cmd->Ymot = Y_MOT2RAD(s.Ymot);
    cmd->Xspeed = X_MOTSPD2RS(s.Xspeed);
    cmd->Yspeed = Y_MOTSPD2RS(s.Yspeed);
    cmd->xychange = s.xychange;
    cmd->XBits = s.XBits;
    cmd->YBits = s.YBits;
    return MCC_E_OK;
}

/**
 * @brief shortcmd - send and receive long binary command
 * @param cmd (io) - command
 * @return errcode
 */
static mcc_errcodes_t longcmd(long_command_t *cmd){
    if(!cmd) return MCC_E_BADFORMAT;
    SSlcmd l = {0};
    l.Xmot = X_RAD2MOT(cmd->Xmot);
    l.Ymot = Y_RAD2MOT(cmd->Ymot);
    l.Xspeed = X_RS2MOTSPD(cmd->Xspeed);
    l.Yspeed = Y_RS2MOTSPD(cmd->Yspeed);
    l.Xadder = X_RS2MOTSPD(cmd->Xadder);
    l.Yadder = Y_RS2MOTSPD(cmd->Yadder);
    l.Xatime = S2ADDER(cmd->Xatime);
    l.Yatime = S2ADDER(cmd->Yatime);
    if(!cmdL(&l)) return MCC_E_FAILED;
    cmd->Xmot = X_MOT2RAD(l.Xmot);
    cmd->Ymot = Y_MOT2RAD(l.Ymot);
    cmd->Xspeed = X_MOTSPD2RS(l.Xspeed);
    cmd->Yspeed = Y_MOTSPD2RS(l.Yspeed);
    cmd->Xadder = X_MOTSPD2RS(l.Xadder);
    cmd->Yadder = Y_MOTSPD2RS(l.Yadder);
    cmd->Xatime = ADDER2S(l.Xatime);
    cmd->Yatime = ADDER2S(l.Yatime);
    return MCC_E_OK;
}

mcc_errcodes_t get_hwconf(hardware_configuration_t *c){
    if(!c) return MCC_E_BADFORMAT;
    SSconfig conf;
    if(!cmdC(&conf, FALSE)) return MCC_E_FAILED;
    // and bored transformations
    DBG("Xacc=%u", conf.Xconf.accel);
    DBG("Yacc=%u", conf.Yconf.accel);
    c->Xconf.accel = X_MOTACC2RS(conf.Xconf.accel);
    DBG("cacc: %g", c->Xconf.accel);
    c->Xconf.backlash = conf.Xconf.backlash;
    // ...
    c->Yconf.accel = X_MOTACC2RS(conf.Yconf.accel);
    c->Xconf.backlash = conf.Xconf.backlash;
    // ...
    return MCC_E_OK;
}

// init mount class
mount_t Mount = {
    .init = init,
    .quit = quit,
    .getMountData = getMD,
    .moveTo = move2,
    .moveWspeed = move2s,
    .setSpeed = setspeed,
    .emergStop = emstop,
    .stop = stop,
    .shortCmd = shortcmd,
    .longCmd = longcmd,
    .getHWconfig = get_hwconf,
};
