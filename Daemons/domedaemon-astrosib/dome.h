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
#pragma once

#include <usefull_macros.h>

#define NRELAY_MIN  1
#define NRELAY_MAX  3

// pause to clear power after stop - 5 seconds
#define POWER_STOP_TIMEOUT      (5.)

// dome finite state machine state
typedef enum{
    DOME_S_IDLE,      // idle, motors disabled
    DOME_S_MOVING,    // moving, motors enabled
    DOME_S_ERROR      // some kind of error
} dome_state_t;

// commands through dome_poll interface
typedef enum{
    DOME_POLL,          // just poll state maching
    DOME_STOP,          // stop any moving
    DOME_OPEN,          // fully open dome
    DOME_CLOSE,         // fully close dome
    DOME_OPEN_ONE,      // open only one part # `par` (1-2)
    DOME_CLOSE_ONE,     // close only one part # `par`
    DOME_RELAY_ON,      // turn on relay # `par` (1-3)
    DOME_RELAY_OFF,     // turn off relay # `par`
} dome_cmd_t;

// cover states
enum{
    COVER_INTERMEDIATE = 0,
    COVER_OPENED = 2,
    COVER_CLOSED = 3
};

typedef struct{
    int coverstate[2];      // north/south covers state (3 - closed, 2 - opened, 0 - intermediate)
    int encoder[2];         // encoders values
    float Tin;              // temperatures (unavailable)
    float Tout;
    float Imot[4];          // motors' currents
    int relay[3];           // relays' state
    int rainArmed;          // rain sensor closes the dome
    int israin;             // arm sensor signal
} dome_status_t;

double get_dome_status(dome_status_t *s);
dome_state_t get_dome_state();
dome_state_t dome_poll(dome_cmd_t cmd, int par);
void dome_serialdev(sl_tty_t *serial);
