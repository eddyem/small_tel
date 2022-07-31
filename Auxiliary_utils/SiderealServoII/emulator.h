/*
 * This file is part of the SSII project.
 * Copyright 2022 Edward V. Emelianov <edward.emelianoff@gmail.com>.
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

#include "sidservo.h"

#define SECPERRAD   (206264.802)

// convert angles
#define ARCSEC2DEG(x)   (x/3600.)
#define DEG2ARCSEC(x)   (x*3600.)
#define ARCSEC2RAD(x)   (x/SECPERRAD)
#define RAD2ARCSEC(x)   (x*SECPERRAD)


void SSmotor_monitoring(SSstat *inistat);
int SSlog_motor_data(SSstat *old, double *told);

void SSstart_emulation(double ha_start, double dec_start);
