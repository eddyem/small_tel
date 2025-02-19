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
    for(int i = 0; i < 10; ++i) if(SSemergStop()) break;
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
    if(Conf.MountReqInterval > 1. || Conf.MountReqInterval < 0.001){
        DBG("Bad value of MountReqInterval");
        ret = MCC_E_BADFORMAT;
    }
    if(ret != MCC_E_OK) quit();
    return ret;
}

/**
 * @brief move2 - simple move to given point and stop
 * @param X - new X coordinate (radians: -pi..pi)
 * @param Y - new Y coordinate (radians: -pi..pi)
 * @return error code
 */
static mcc_errcodes_t move2(double X, double Y){
    if(X > M_PI || X < -M_PI || Y > M_PI || Y < -M_PI){
        DBG("Wrong coords: X=%g, Y=%g", X, Y);
        return MCC_E_BADFORMAT;
    }
    if(!SSXmoveto(X) || !SSYmoveto(Y)) return MCC_E_FAILED;
    return MCC_E_OK;
}

/**
 * @brief emstop - emergency stop
 * @return errcode
 */
static mcc_errcodes_t emstop(){
    if(!SSemergStop()) return MCC_E_FAILED;
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

// init mount class
mount_t Mount = {
    .init = init,
    .quit = quit,
    .getMountData = getMD,
    .moveTo = move2,
    .emergStop = emstop,
    .shortCmd = shortcmd,
    .longCmd = longcmd,
};
