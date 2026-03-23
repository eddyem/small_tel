/*
 * This file is part of the libsidservo project.
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

typedef struct{
    double x[3];        // [theta, omega, alpha]
    double P[3][3];     // covariance
    double Q[3][3];     // process noise
    double R;           // measurement noise
    double dt;
} Kalman3;


double encoder_noise(int counts);
void kalman3_update(Kalman3 *kf, double z);
void kalman3_predict(Kalman3 *kf);
void kalman3_set_jerk_noise(Kalman3 *kf, double sigma_j);
void kalman3_init(Kalman3 *kf, double dt, double enc_var);
