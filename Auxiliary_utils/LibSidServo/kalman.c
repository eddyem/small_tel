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

#include <math.h>

#include "kalman.h"

void kalman3_init(Kalman3 *kf, double dt, double enc_var){
    kf->dt = dt;

    kf->x[0] = 0;
    kf->x[1] = 0;
    kf->x[2] = 0;

    for(int i=0;i<3;i++)
        for(int j=0;j<3;j++)
            kf->P[i][j] = 0;

    kf->P[0][0] = 1;
    kf->P[1][1] = 1;
    kf->P[2][2] = 1;

    // process noise
    kf->Q[0][0] = 1e-6;
    kf->Q[1][1] = 1e-4;
    kf->Q[2][2] = 1e-3;

    kf->R = enc_var; // encoder noise variance
}

void kalman3_set_jerk_noise(Kalman3 *kf, double sigma_j){
    double dt = kf->dt;

    double dt2 = dt*dt;
    double dt3 = dt2*dt;
    double dt4 = dt3*dt;
    double dt5 = dt4*dt;

    double q = sigma_j * sigma_j;

    kf->Q[0][0] = q * dt5 / 20.0;
    kf->Q[0][1] = q * dt4 / 8.0;
    kf->Q[0][2] = q * dt3 / 6.0;

    kf->Q[1][0] = q * dt4 / 8.0;
    kf->Q[1][1] = q * dt3 / 3.0;
    kf->Q[1][2] = q * dt2 / 2.0;

    kf->Q[2][0] = q * dt3 / 6.0;
    kf->Q[2][1] = q * dt2 / 2.0;
    kf->Q[2][2] = q * dt;
}

void kalman3_predict(Kalman3 *kf){
    double dt = kf->dt;
    double dt2 = 0.5 * dt * dt;

    double theta = kf->x[0];
    double omega = kf->x[1];
    double alpha = kf->x[2];

    // state prediction
    kf->x[0] = theta + omega*dt + alpha*dt2;
    kf->x[1] = omega + alpha*dt;
    kf->x[2] = alpha;

    // transition matrix
    double F[3][3] =
        {
            {1, dt, dt2},
            {0, 1,  dt},
            {0, 0,  1}
        };

    // P = FPF^T + Q

    double FP[3][3];

    for(int i=0;i<3;i++)
        for(int j=0;j<3;j++){
            FP[i][j] =
                F[i][0]*kf->P[0][j] +
                F[i][1]*kf->P[1][j] +
                F[i][2]*kf->P[2][j];
        }

    double Pnew[3][3];

    for(int i=0;i<3;i++)
        for(int j=0;j<3;j++){
            Pnew[i][j] =
                FP[i][0]*F[j][0] +
                FP[i][1]*F[j][1] +
                FP[i][2]*F[j][2] +
                kf->Q[i][j];
        }

    for(int i=0;i<3;i++)
        for(int j=0;j<3;j++)
            kf->P[i][j] = Pnew[i][j];
}

void kalman3_update(Kalman3 *kf, double z){
    // innovation
    double y = z - kf->x[0];

    // S = HPH^T + R
    double S = kf->P[0][0] + kf->R;

    // Kalman gain
    double K[3];

    K[0] = kf->P[0][0] / S;
    K[1] = kf->P[1][0] / S;
    K[2] = kf->P[2][0] / S;

    // state update
    kf->x[0] += K[0] * y;
    kf->x[1] += K[1] * y;
    kf->x[2] += K[2] * y;

    // covariance update
    double P00 = kf->P[0][0];
    double P01 = kf->P[0][1];
    double P02 = kf->P[0][2];

    double P10 = kf->P[1][0];
    double P11 = kf->P[1][1];
    double P12 = kf->P[1][2];

    double P20 = kf->P[2][0];
    double P21 = kf->P[2][1];
    double P22 = kf->P[2][2];

    kf->P[0][0] = P00 - K[0]*P00;
    kf->P[0][1] = P01 - K[0]*P01;
    kf->P[0][2] = P02 - K[0]*P02;

    kf->P[1][0] = P10 - K[1]*P00;
    kf->P[1][1] = P11 - K[1]*P01;
    kf->P[1][2] = P12 - K[1]*P02;

    kf->P[2][0] = P20 - K[2]*P00;
    kf->P[2][1] = P21 - K[2]*P01;
    kf->P[2][2] = P22 - K[2]*P02;
}


// estimation of the R
double encoder_noise(int counts){
    double d = 2.0*M_PI / counts;
    return d*d / 12.0;
}

