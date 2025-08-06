/*
 * This file is part of the moving_model project.
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

#include <stddef.h>

typedef struct{
    double P, I, D;
} PIDpar_t;

typedef struct {
    PIDpar_t gain;      // PID gains
    double prev_error;  // Previous error
    double integral;    // Integral term
    double *pidIarray;  // array for Integral
    size_t pidIarrSize; // it's size
    size_t curIidx;     // and index of current element
} PIDController_t;

PIDController_t *pid_create(PIDpar_t *gain, size_t Iarrsz);
void pid_clear(PIDController_t *pid);
void pid_delete(PIDController_t **pid);
double pid_calculate(PIDController_t *pid, double error, double dt);
