/*
 * This file is part of the mountdaemon_10micron project.
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

#pragma once

// commands
#define CMD_SHUTDOWN        ":shutdown#"  // TODO!
#define CMD_TRKSTART        ":AP#"
#define CMD_TRKSTOP         ":AL#"
#define CMD_GOTOAZ          ":MA#"
#define CMD_SLEWRADEC       ":MS#"
#define CMD_BAUDRATE        ":SB0#"
#define CMD_DUALTRK         ":Sdat1#"
#define CMD_REFCORR_ON      ":SREF1#"
#define CMD_STOP            ":STOP#"
#define CMD_HIGHPREC        ":U2#"

// getters
#define CMD_GETALT          ":GA#"
#define CMD_GETAZIM         ":GZ#"
#define CMD_GETDEC          ":GD#"
#define CMD_GETSTAT         ":Gstat#"
#define CMD_GETRA           ":GR#"
#define CMD_GETPS           ":pS#"

// setters
#define CMD_SETALT          ":Sa%s#"
#define CMD_SETDEC          ":Sd%s#"
#define CMD_SETTIME         ":SLDT%04d-%02d-%02d,%02d:%02d:%02d.%02ld#"
#define CMD_SETMINALT       ":So%d#"
#define CMD_SETRA           ":Sr%s#"
#define CMD_SETPRESSURE     ":SRPRS%.1f#"
#define CMD_SETTEMPER       ":SRTMP%.1f#"
#define CMD_SETZD           ":Sz%s#"
