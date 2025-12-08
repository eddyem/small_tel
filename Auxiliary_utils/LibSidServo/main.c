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

/*
 * main functions to fill struct `mount_t`
 */

#include <inttypes.h>
#include <strings.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>

#include "main.h"
#include "movingmodel.h"
#include "serial.h"
#include "ssii.h"
#include "PID.h"

// adder for monotonic time by realtime: inited any call of init()
static struct timespec timeadder = {0}, // adder of CLOCK_REALTIME to CLOCK_MONOTONIC
    t0 = {0},           // curtime() for initstarttime() call
    starttime = {0};    // starting time by monotonic (for timefromstart())

conf_t Conf = {0};
// parameters for model
static movemodel_t *Xmodel, *Ymodel;
// limits for model and/or real mount (in latter case data should be read from mount on init)
// radians, rad/sec, rad/sec^2
// max speeds (rad/s): xs=10 deg/s, ys=8 deg/s
// accelerations: xa=12.6 deg/s^2, ya= 9.5 deg/s^2
limits_t
    Xlimits = {
        .min = {.coord = -3.1241, .speed = 1e-10, .accel = 1e-6},
        .max = {.coord = 3.1241, .speed = 0.174533, .accel = 0.219911}},
    Ylimits = {
        .min = {.coord = -3.1241, .speed = 1e-10, .accel = 1e-6},
        .max = {.coord = 3.1241, .speed = 0.139626, .accel = 0.165806}}
;
static mcc_errcodes_t shortcmd(short_command_t *cmd);

/**
 * @brief curtime - monotonic time from first run
 * @param t - struct timespec by CLOCK_MONOTONIC but with setpoint by CLOCK_REALTIME on observations start
 * @return TRUE if all OK
 * FIXME: double -> struct timespec; on init: init t0 by CLOCK_REALTIME
 */
int curtime(struct timespec *t){
    struct timespec now;
    if(clock_gettime(CLOCK_MONOTONIC, &now)) return FALSE;
    now.tv_sec += timeadder.tv_sec;
    now.tv_nsec += timeadder.tv_nsec;
    if(now.tv_nsec > 999999999L){
        ++now.tv_sec;
        now.tv_nsec -= 1000000000L;
    }
    if(t) *t = now;
    return TRUE;
}

// init starttime; @return TRUE if all OK
static int initstarttime(){
    struct timespec start;
    if(clock_gettime(CLOCK_MONOTONIC, &starttime)) return FALSE;
    if(clock_gettime(CLOCK_REALTIME, &start)) return FALSE;
    timeadder.tv_sec = start.tv_sec - starttime.tv_sec;
    timeadder.tv_nsec = start.tv_nsec - starttime.tv_nsec;
    if(timeadder.tv_nsec < 0){
        --timeadder.tv_sec;
        timeadder.tv_nsec += 1000000000L;
    }
    curtime(&t0);
    return TRUE;
}
// return difference (in seconds) between time1 and time0
double timediff(const struct timespec *time1, const struct timespec *time0){
    if(!time1 || !time0) return -1.;
    return (time1->tv_sec - time0->tv_sec) + (time1->tv_nsec - time0->tv_nsec) / 1e9;
}
// difference between given time and  last initstarttime() call
double timediff0(const struct timespec *time1){
    return timediff(time1, &t0);
}
// time from last initstarttime() call
double timefromstart(){
    struct timespec now;
    if(clock_gettime(CLOCK_MONOTONIC, &now)) return -1.;
    return (now.tv_sec - starttime.tv_sec) + (now.tv_nsec - starttime.tv_nsec) / 1e9;
}

/**
 * @brief quit - close all opened and return to default state
 * TODO: close serial devices even in "model" mode
 */
static void quit(){
    if(Conf.RunModel) return;
    for(int i = 0; i < 10; ++i) if(SSstop(TRUE)) break;
    DBG("Close all serial devices");
    closeSerial();
    DBG("Exit");
}

void getModData(coordpair_t *c, movestate_t *xst, movestate_t *yst){
    if(!c || !Xmodel || !Ymodel) return;
    double tnow = timefromstart();
    moveparam_t Xp, Yp;
    movestate_t Xst = Xmodel->get_state(Xmodel, &Xp);
    //DBG("Xstate = %d", Xst);
    if(Xst == ST_MOVE) Xst = Xmodel->proc_move(Xmodel, &Xp, tnow);
    movestate_t Yst = Ymodel->get_state(Ymodel, &Yp);
    if(Yst == ST_MOVE) Yst = Ymodel->proc_move(Ymodel, &Yp, tnow);
    c->X = Xp.coord;
    c->Y = Yp.coord;
    if(xst) *xst = Xst;
    if(yst) *yst = Yst;
}

/**
 * less square calculations of speed
 */
less_square_t *LS_init(size_t Ndata){
    if(Ndata < 5){
        DBG("Ndata=%zd - TOO SMALL", Ndata);
        return NULL;
    }
    DBG("Init less squares: %zd", Ndata);
    less_square_t *l = calloc(1, sizeof(less_square_t));
    l->x = calloc(Ndata, sizeof(double));
    l->t2 = calloc(Ndata, sizeof(double));
    l->t = calloc(Ndata, sizeof(double));
    l->xt = calloc(Ndata, sizeof(double));
    l->arraysz = Ndata;
    return l;
}
void LS_delete(less_square_t **l){
    if(!l || !*l) return;
    free((*l)->x); free((*l)->t2); free((*l)->t); free((*l)->xt);
    free(*l);
    *l = NULL;
}
// add next data portion and calculate current slope
double LS_calc_slope(less_square_t *l, double x, double t){
    if(!l) return 0.;
    size_t idx = l->idx;
    double oldx = l->x[idx], oldt = l->t[idx], oldt2 = l->t2[idx], oldxt = l->xt[idx];
    double t2 = t * t, xt = x * t;
    l->x[idx] = x; l->t2[idx] = t2;
    l->t[idx] = t; l->xt[idx] = xt;
    ++idx;
    l->idx = (idx >= l->arraysz) ? 0 : idx;
    l->xsum += x - oldx;
    l->t2sum += t2 - oldt2;
    l->tsum += t - oldt;
    l->xtsum += xt - oldxt;
    double n = (double)l->arraysz;
    double denominator = n * l->t2sum - l->tsum * l->tsum;
    //DBG("idx=%zd, arrsz=%zd, den=%g", l->idx, l->arraysz, denominator);
    if(fabs(denominator) < 1e-7) return 0.;
    double numerator = n * l->xtsum - l->xsum * l->tsum;
    // point: (sum_x  - slope * sum_t) / n;
    return (numerator / denominator);
}

/**
 * @brief init - open serial devices and do other job
 * @param c - initial configuration
 * @return error code
 */
static mcc_errcodes_t init(conf_t *c){
    FNAME();
    if(!c) return MCC_E_BADFORMAT;
    if(!initstarttime()) return MCC_E_FAILED;
    Conf = *c;
    mcc_errcodes_t ret = MCC_E_OK;
    Xmodel = model_init(&Xlimits);
    Ymodel = model_init(&Ylimits);
    if(Conf.MountReqInterval > 1. || Conf.MountReqInterval < 0.05){
        DBG("Bad value of MountReqInterval");
        ret = MCC_E_BADFORMAT;
    }
    if(Conf.RunModel){
        if(!Xmodel || !Ymodel || !openMount()) return MCC_E_FAILED;
        return MCC_E_OK;
    }
    if(!Conf.MountDevPath || Conf.MountDevSpeed < MOUNT_BAUDRATE_MIN){
        DBG("Define mount device path and speed");
        ret = MCC_E_BADFORMAT;
    }else if(!openMount()){
        DBG("Can't open %s with speed %d", Conf.MountDevPath, Conf.MountDevSpeed);
        ret = MCC_E_MOUNTDEV;
    }
    if(Conf.SepEncoder){
        if(!Conf.EncoderDevPath && !Conf.EncoderXDevPath){
            DBG("Define encoder device path");
            ret = MCC_E_BADFORMAT;
        }else if(!openEncoder()){
            DBG("Can't open encoder device");
            ret = MCC_E_ENCODERDEV;
        }
    }
    // TODO: read hardware configuration on init
    if(Conf.EncoderSpeedInterval < Conf.EncoderReqInterval * MCC_CONF_MIN_SPEEDC || Conf.EncoderSpeedInterval > MCC_CONF_MAX_SPEEDINT){
        DBG("Wrong speed interval");
        ret = MCC_E_BADFORMAT;
    }
    if(!SSrawcmd(CMD_EXITACM, NULL)) ret = MCC_E_FAILED;
    if(ret != MCC_E_OK) return ret;
    return updateMotorPos();
}

// check coordinates (rad) and speeds (rad/s); return FALSE if failed
// TODO fix to real limits!!!
static int chkX(double X){
    if(X > Xlimits.max.coord || X < Xlimits.min.coord) return FALSE;
    return TRUE;
}
static int chkY(double Y){
    if(Y > Ylimits.max.coord || Y < Ylimits.min.coord) return FALSE;
    return TRUE;
}
static int chkXs(double s){
    if(s < Xlimits.min.speed || s > Xlimits.max.speed) return FALSE;
    return TRUE;
}
static int chkYs(double s){
    if(s < Ylimits.min.speed || s > Ylimits.max.speed) return FALSE;
    return TRUE;
}

// set SLEWING state if axis was stopped later
static void setslewingstate(){
    //FNAME();
    mountdata_t d;
    if(MCC_E_OK == getMD(&d)){
        axis_status_t newx = d.Xstate, newy = d.Ystate;
        //DBG("old state: %d/%d", d.Xstate, d.Ystate);
        if(d.Xstate == AXIS_STOPPED) newx = AXIS_SLEWING;
        if(d.Ystate == AXIS_STOPPED) newy = AXIS_SLEWING;
        if(newx != d.Xstate || newy != d.Ystate){
            DBG("Started moving -> slew");
            setStat(newx, newy);
        }
    }else DBG("CAN't GET MOUNT DATA!");
}

/*
static mcc_errcodes_t slew2(const coordpair_t *target, slewflags_t flags){
    (void)target;
    (void)flags;
    //if(Conf.RunModel) return ... ;
    if(MCC_E_OK != updateMotorPos()) return MCC_E_FAILED;
    //...
    setStat(AXIS_SLEWING, AXIS_SLEWING);
    //...
    return MCC_E_FAILED;
}
*/

/**
 * @brief move2 - simple move to given point and stop
 * @param X - new X coordinate (radians: -pi..pi) or NULL
 * @param Y - new Y coordinate (radians: -pi..pi) or NULL
 * @return error code
 */
static mcc_errcodes_t move2(const coordpair_t *target){
    if(!target) return MCC_E_BADFORMAT;
    if(!chkX(target->X) || !chkY(target->Y)) return MCC_E_BADFORMAT;
    if(MCC_E_OK != updateMotorPos()) return MCC_E_FAILED;
    short_command_t cmd = {0};
    DBG("x,y: %g, %g", target->X, target->Y);
    cmd.Xmot = target->X;
    cmd.Ymot = target->Y;
    cmd.Xspeed = Xlimits.max.speed;
    cmd.Yspeed = Ylimits.max.speed;
    mcc_errcodes_t r = shortcmd(&cmd);
    if(r != MCC_E_OK) return r;
    setslewingstate();
    return MCC_E_OK;
}

/**
 * @brief setspeed - set maximal speed over axis by text command
 * @param X (i) - max speed or NULL
 * @param Y (i) - -//-
 * @return errcode
 */
static mcc_errcodes_t setspeed(const coordpair_t *tagspeed){
    if(!tagspeed || !chkXs(tagspeed->X) || !chkYs(tagspeed->Y)) return MCC_E_BADFORMAT;
    if(Conf.RunModel) return MCC_E_FAILED;
    int32_t spd = X_RS2MOTSPD(tagspeed->X);
    if(!SSsetterI(CMD_SPEEDX, spd)) return MCC_E_FAILED;
    spd = Y_RS2MOTSPD(tagspeed->Y);
    if(!SSsetterI(CMD_SPEEDY, spd)) return MCC_E_FAILED;
    return MCC_E_OK;
}

/**
 * @brief move2s - move to target with given max speed
 * @param target (i) - target or NULL
 * @param speed (i) - speed or NULL
 * @return
 */
static mcc_errcodes_t move2s(const coordpair_t *target, const coordpair_t *speed){
    if(!target || !speed) return MCC_E_BADFORMAT;
    if(!chkX(target->X) || !chkY(target->Y)) return MCC_E_BADFORMAT;
    if(!chkXs(speed->X) || !chkYs(speed->Y)) return MCC_E_BADFORMAT;
    // updateMotorPos() here can make a problem; TODO: remove?
    if(MCC_E_OK != updateMotorPos()) return MCC_E_FAILED;
    short_command_t cmd = {0};
    cmd.Xmot = target->X;
    cmd.Ymot = target->Y;
    cmd.Xspeed = speed->X;
    cmd.Yspeed = speed->Y;
    mcc_errcodes_t r = shortcmd(&cmd);
    if(r != MCC_E_OK) return r;
    setslewingstate();
    return MCC_E_OK;
}

/**
 * @brief emstop - emergency stop
 * @return errcode
 */
static mcc_errcodes_t emstop(){
    FNAME();
    if(Conf.RunModel){
        double curt = timefromstart();
        Xmodel->emergency_stop(Xmodel, curt);
        Ymodel->emergency_stop(Ymodel, curt);
        return MCC_E_OK;
    }
    if(!SSstop(TRUE)) return MCC_E_FAILED;
    return MCC_E_OK;
}
// normal stop
static mcc_errcodes_t stop(){
    FNAME();
    if(Conf.RunModel){
        double curt = timefromstart();
        Xmodel->stop(Xmodel, curt);
        Ymodel->stop(Ymodel,curt);
        return MCC_E_OK;
    }
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
    if(Conf.RunModel){
        double curt = timefromstart();
        moveparam_t param = {0};
        param.coord = cmd->Xmot; param.speed = cmd->Xspeed;
        if(!model_move2(Xmodel, &param, curt)) return MCC_E_FAILED;
        param.coord = cmd->Ymot; param.speed = cmd->Yspeed;
        if(!model_move2(Ymodel, &param, curt)) return MCC_E_FAILED;
        setslewingstate();
        return MCC_E_OK;
    }
    SSscmd s = {0};
    DBG("tag: xmot=%g rad, ymot=%g rad", cmd->Xmot, cmd->Ymot);
    s.Xmot = X_RAD2MOT(cmd->Xmot);
    s.Ymot = Y_RAD2MOT(cmd->Ymot);
    s.Xspeed = X_RS2MOTSPD(cmd->Xspeed);
    s.Yspeed = Y_RS2MOTSPD(cmd->Yspeed);
    s.xychange = cmd->xychange;
    s.XBits = cmd->XBits;
    s.YBits = cmd->YBits;
    DBG("X->%d, Y->%d, Xs->%d, Ys->%d", s.Xmot, s.Ymot, s.Xspeed, s.Yspeed);
    if(!cmdS(&s)) return MCC_E_FAILED;
    setslewingstate();
    return MCC_E_OK;
}

/**
 * @brief longcmd - send and receive long binary command
 * @param cmd (io) - command
 * @return errcode
 */
static mcc_errcodes_t longcmd(long_command_t *cmd){
    if(!cmd) return MCC_E_BADFORMAT;
    if(Conf.RunModel){
        double curt = timefromstart();
        moveparam_t param = {0};
        param.coord = cmd->Xmot; param.speed = cmd->Xspeed;
        if(!model_move2(Xmodel, &param, curt)) return MCC_E_FAILED;
        param.coord = cmd->Ymot; param.speed = cmd->Yspeed;
        if(!model_move2(Ymodel, &param, curt)) return MCC_E_FAILED;
        setslewingstate();
        return MCC_E_OK;
    }
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
    setslewingstate();
    return MCC_E_OK;
}

static mcc_errcodes_t get_hwconf(hardware_configuration_t *hwConfig){
    if(!hwConfig) return MCC_E_BADFORMAT;
    if(Conf.RunModel) return MCC_E_FAILED;
    SSconfig config;
    if(!cmdC(&config, FALSE)) return MCC_E_FAILED;
    // Convert acceleration (ticks per loop^2 to rad/s^2)
    hwConfig->Xconf.accel = X_MOTACC2RS(config.Xconf.accel);
    hwConfig->Yconf.accel = Y_MOTACC2RS(config.Yconf.accel);
    // Convert backlash (ticks to radians)
    hwConfig->Xconf.backlash = X_MOT2RAD(config.Xconf.backlash);
    hwConfig->Yconf.backlash = Y_MOT2RAD(config.Yconf.backlash);
    // Convert error limit (ticks to radians)
    hwConfig->Xconf.errlimit = X_MOT2RAD(config.Xconf.errlimit);
    hwConfig->Yconf.errlimit = Y_MOT2RAD(config.Yconf.errlimit);
    // Proportional, integral, and derivative gains are unitless, so no conversion needed
    hwConfig->Xconf.propgain = (double)config.Xconf.propgain;
    hwConfig->Yconf.propgain = (double)config.Yconf.propgain;
    hwConfig->Xconf.intgain = (double)config.Xconf.intgain;
    hwConfig->Yconf.intgain = (double)config.Yconf.intgain;
    hwConfig->Xconf.derivgain = (double)config.Xconf.derivgain;
    hwConfig->Yconf.derivgain = (double)config.Yconf.derivgain;
    // Output limit is a percentage (0-100)
    hwConfig->Xconf.outplimit = (double)config.Xconf.outplimit / 255.0 * 100.0;
    hwConfig->Yconf.outplimit = (double)config.Yconf.outplimit / 255.0 * 100.0;
    // Current limit in amps
    hwConfig->Xconf.currlimit = (double)config.Xconf.currlimit / 100.0;
    hwConfig->Yconf.currlimit = (double)config.Yconf.currlimit / 100.0;
    // Integral limit is unitless
    hwConfig->Xconf.intlimit = (double)config.Xconf.intlimit;
    hwConfig->Yconf.intlimit = (double)config.Yconf.intlimit;
    // Copy XBits and YBits (no conversion needed)
    hwConfig->xbits = config.xbits;
    hwConfig->ybits = config.ybits;
    // Copy address
    hwConfig->address = config.address;

    // TODO: What to do with eqrate, eqadj and trackgoal?
    config.latitude = __bswap_16(config.latitude);
    // Convert latitude (degrees * 100 to radians)
    hwConfig->latitude = ((double)config.latitude) / 100.0 * M_PI / 180.0;
    // Copy ticks per revolution
    hwConfig->Xsetpr = __bswap_32(config.Xsetpr);
    hwConfig->Ysetpr = __bswap_32(config.Ysetpr);
    hwConfig->Xmetpr = __bswap_32(config.Xmetpr) / 4; // as documentation said, real ticks are 4 times less
    hwConfig->Ymetpr = __bswap_32(config.Ymetpr) / 4;
    // Convert slew rates (ticks per loop to rad/s)
    hwConfig->Xslewrate = X_MOTSPD2RS(config.Xslewrate);
    hwConfig->Yslewrate = Y_MOTSPD2RS(config.Yslewrate);
    // Convert pan rates (ticks per loop to rad/s)
    hwConfig->Xpanrate = X_MOTSPD2RS(config.Xpanrate);
    hwConfig->Ypanrate = Y_MOTSPD2RS(config.Ypanrate);
    // Convert guide rates (ticks per loop to rad/s)
    hwConfig->Xguiderate = X_MOTSPD2RS(config.Xguiderate);
    hwConfig->Yguiderate = Y_MOTSPD2RS(config.Yguiderate);
    // copy baudrate
    hwConfig->baudrate = (uint32_t) config.baudrate;
    // Convert local search degrees (degrees * 100 to radians)
    hwConfig->locsdeg = (double)config.locsdeg / 100.0 * M_PI / 180.0;
    // Convert local search speed (arcsec per second to rad/s)
    hwConfig->locsspeed = (double)config.locsspeed * M_PI / (180.0 * 3600.0);
    // Convert backlash speed (ticks per loop to rad/s)
    hwConfig->backlspd = X_MOTSPD2RS(config.backlspd);
    return MCC_E_OK;
}

static mcc_errcodes_t write_hwconf(hardware_configuration_t *hwConfig){
    SSconfig config;
    if(Conf.RunModel) return MCC_E_FAILED;
    // Convert acceleration (rad/s^2 to ticks per loop^2)
    config.Xconf.accel = X_RS2MOTACC(hwConfig->Xconf.accel);
    config.Yconf.accel = Y_RS2MOTACC(hwConfig->Yconf.accel);
    // Convert backlash (radians to ticks)
    config.Xconf.backlash = X_RAD2MOT(hwConfig->Xconf.backlash);
    config.Yconf.backlash = Y_RAD2MOT(hwConfig->Yconf.backlash);
    // Convert error limit (radians to ticks)
    config.Xconf.errlimit = X_RAD2MOT(hwConfig->Xconf.errlimit);
    config.Yconf.errlimit = Y_RAD2MOT(hwConfig->Yconf.errlimit);
    // Proportional, integral, and derivative gains are unitless, so no conversion needed
    config.Xconf.propgain = (uint16_t)hwConfig->Xconf.propgain;
    config.Yconf.propgain = (uint16_t)hwConfig->Yconf.propgain;
    config.Xconf.intgain = (uint16_t)hwConfig->Xconf.intgain;
    config.Yconf.intgain = (uint16_t)hwConfig->Yconf.intgain;
    config.Xconf.derivgain = (uint16_t)hwConfig->Xconf.derivgain;
    config.Yconf.derivgain = (uint16_t)hwConfig->Yconf.derivgain;
    // Output limit is a percentage (0-100), so convert back to 0-255
    config.Xconf.outplimit = (uint8_t)(hwConfig->Xconf.outplimit / 100.0 * 255.0);
    config.Yconf.outplimit = (uint8_t)(hwConfig->Yconf.outplimit / 100.0 * 255.0);
    // Current limit is in amps (convert back to *100)
    config.Xconf.currlimit = (uint16_t)(hwConfig->Xconf.currlimit * 100.0);
    config.Yconf.currlimit = (uint16_t)(hwConfig->Yconf.currlimit * 100.0);
    // Integral limit is unitless, so no conversion needed
    config.Xconf.intlimit = (uint16_t)hwConfig->Xconf.intlimit;
    config.Yconf.intlimit = (uint16_t)hwConfig->Yconf.intlimit;
    // Copy XBits and YBits (no conversion needed)
    config.xbits = hwConfig->xbits;
    config.ybits = hwConfig->ybits;
    // Convert latitude (radians to degrees * 100)
    config.latitude = __bswap_16((uint16_t)(hwConfig->latitude * 180.0 / M_PI * 100.0));
    // Convert slew rates (rad/s to ticks per loop)
    config.Xslewrate = X_RS2MOTSPD(hwConfig->Xslewrate);
    config.Yslewrate = Y_RS2MOTSPD(hwConfig->Yslewrate);
    // Convert pan rates (rad/s to ticks per loop)
    config.Xpanrate = X_RS2MOTSPD(hwConfig->Xpanrate);
    config.Ypanrate = Y_RS2MOTSPD(hwConfig->Ypanrate);
    // Convert guide rates (rad/s to ticks per loop)
    config.Xguiderate = X_RS2MOTSPD(hwConfig->Xguiderate);
    config.Yguiderate = Y_RS2MOTSPD(hwConfig->Yguiderate);
    // Convert local search degrees (radians to degrees * 100)
    config.locsdeg = (uint32_t)(hwConfig->locsdeg * 180.0 / M_PI * 100.0);
    // Convert local search speed (rad/s to arcsec per second)
    config.locsspeed = (uint32_t)(hwConfig->locsspeed * 180.0 * 3600.0 / M_PI);
    // Convert backlash speed (rad/s to ticks per loop)
    config.backlspd = X_RS2MOTSPD(hwConfig->backlspd);
    config.Xsetpr = __bswap_32(hwConfig->Xsetpr);
    config.Ysetpr = __bswap_32(hwConfig->Ysetpr);
    config.Xmetpr = __bswap_32(hwConfig->Xmetpr * 4);
    config.Ymetpr = __bswap_32(hwConfig->Ymetpr * 4);
    // TODO - next
    (void) config;
    return MCC_E_OK;
}

// getters of max/min speed and acceleration
mcc_errcodes_t maxspeed(coordpair_t *v){
    if(!v) return MCC_E_BADFORMAT;
    v->X = Xlimits.max.speed;
    v->Y = Ylimits.max.speed;
    return MCC_E_OK;
}
mcc_errcodes_t minspeed(coordpair_t *v){
    if(!v) return MCC_E_BADFORMAT;
    v->X = Xlimits.min.speed;
    v->Y = Ylimits.min.speed;
    return MCC_E_OK;
}
mcc_errcodes_t acceleration(coordpair_t *a){
    if(!a) return MCC_E_BADFORMAT;
    a->X = Xlimits.max.accel;
    a->Y = Ylimits.max.accel;
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
    .saveHWconfig = write_hwconf,
    .currentT = curtime,
    .timeFromStart = timefromstart,
    .timeDiff = timediff,
    .timeDiff0 = timediff0,
    .correctTo = correct2,
    .getMaxSpeed = maxspeed,
    .getMinSpeed = minspeed,
    .getAcceleration = acceleration,
};

