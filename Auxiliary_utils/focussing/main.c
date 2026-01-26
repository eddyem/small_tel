/*
 * Copyright 2024 Edward V. Emelianov <edward.emelianoff@gmail.com>.
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
#include <signal.h>
#include <stdio.h>
#include <strings.h>
#include <usefull_macros.h>

// minimal amount of elements
#define MIN_ELEMENTS_AMOUNT (5)

typedef struct{
    int help;
    double tolerance;  // max std of data from fit
    char *input;       // input file
} glob_pars;

static glob_pars G = {.tolerance = 10.};

static sl_option_t cmdlnopts[] = {
    // common options
    {"help",    NO_ARGS,    NULL,   'h',    arg_none,   APTR(&G.help),      _("show this help")},
    {"infile",  NEED_ARG,   NULL,   'i',    arg_string, APTR(&G.input),     _("input file name")},
    {"tolerance",NEED_ARG,  NULL,   't',    arg_double, APTR(&G.tolerance), _("max STD of data from fit (default: 10.)")},
    end_option
};

static double *x = NULL, *y = NULL;
static int nelems = 0;
static uint8_t *badmask = NULL;

/**
 * @brief parabolic_fit - fit y = a + bx + cx^2
 * @param coeffs (o) - coefficients a,b,c
 * @return n good elements
 */
static int parabolic_fit(double coeffs[3]){
    if(nelems < MIN_ELEMENTS_AMOUNT || !x || !y) return 0;
    double matrix[3][4];
    double sum_x = 0., sum_y = 0., sum_x2 = 0., sum_x3 = 0., sum_x4 = 0., sum_xy = 0., sum_x2y = 0.;
    int goodelems = 0;
    for(int i = 0; i < nelems; ++i){
        if(badmask[i]) continue;
        ++goodelems;
        double xx = x[i], yy = y[i], x2 = xx * xx, x3 = x2 * xx, x4 = x3 * xx;
        sum_x += xx;
        sum_y += yy;
        sum_x2 += x2;
        sum_x3 += x3;
        sum_x4 += x4;
        sum_xy += xx*yy;
        sum_x2y += x2*yy;
    }
    printf("%d good from %d\n", goodelems, nelems);
    if(goodelems < MIN_ELEMENTS_AMOUNT) return 0;
    matrix[0][0] = goodelems;
    matrix[0][1] = sum_x;
    matrix[0][2] = sum_x2;
    matrix[0][3] = sum_y;
    matrix[1][0] = sum_x;
    matrix[1][1] = sum_x2;
    matrix[1][2] = sum_x3;
    matrix[1][3] = sum_xy;
    matrix[2][0] = sum_x2;
    matrix[2][1] = sum_x3;
    matrix[2][2] = sum_x4;
    matrix[2][3] = sum_x2y;
    for(int i = 0; i < 3; i++){
        for(int j = 0; j < 3; j++){
            if(i != j){
                if(matrix[i][i] == 0.) return 0;
                double r = matrix[j][i]/matrix[i][i];
                for(int k = 0; k < 4; k++){
                    matrix[j][k] -= r * matrix[i][k];
                }
            }
        }
    }
    for(int i = 0; i < 3; i++){
        double a = matrix[i][i];
        if(a == 0.) return 0;
        for(int j = 0; j < 4; j++){
            matrix[i][j] /= a;
        }
    }
    for(int i = 0; i < 3; i++){
        printf("%c => %.4f\n", 97+i, matrix[i][3]);
        coeffs[i] = matrix[i][3];
    }
    return goodelems;
}

/**
 * @brief check - check points for tolerance & mark bad (t*2)
 * @param coeffs - fit coefficients
 * @return amount of points in tolerance*2 region
 */
static int check(double coeffs[3]){
    int ngood = 0;
    double a = coeffs[0], b = coeffs[1], c = coeffs[2];
    double lim = 2*G.tolerance;
    for(int i = 0; i < nelems; ++i){
        if(badmask[i]) continue;
        double yy = a + b*x[i] + c*x[i]*x[i];
        double delta = fabs(y[i] - yy);
        printf("delta[%d]=%g\n", i, delta);
        if(delta > lim) badmask[i] = 1;
        else ++ngood;
    }
    printf("found %d good elements\n", ngood);
    return ngood;
}

static int readfile(FILE *f){
    if(!f) return 0;
    int n = 0;
    while(1){
        double xx, yy;
        if(2 != fscanf(f, "%lg %lg\n", &xx, &yy)) break;
        if(nelems >= n){
            n += 256;
            x = realloc(x, n * sizeof(double));
            y = realloc(y, n * sizeof(double));
            badmask = realloc(badmask, n);
            if(!x || !y) ERR("realloc failed");
        }
        x[nelems] = xx; y[nelems] = yy;
        ++nelems;
    }
    for(int i = 0; i < nelems; ++i){
        printf("x[%d]=%g, y[%d]=%g\n", i, x[i], i, y[i]);
    }
    bzero(badmask, sizeof(int) * nelems);
    return nelems;
}

/* calculate best focus by parabolic fit:
 * y = a + bx + cx^2 -> 0 = b + 2cx -> x = -b/(2c)
 */
static double calcfocus(double coeffs[3]){
    return -coeffs[1]/2./coeffs[2];
}

int main(int argc, char **argv){
    char helpstring[256];
    sl_init();
    snprintf(helpstring, 255, "Usage: `cat file | %%s` or with args; file format \"x y\\n..\"\n\tArgs:\n");
    sl_helpstring(helpstring);
    sl_parseargs(&argc, &argv, cmdlnopts);
    if(G.help) sl_showhelp(-1, cmdlnopts);
    if(G.tolerance <= 0.) ERRX("Tolerance should be > 0");
    FILE *f = stdin;
    if(G.input){
        f = fopen(G.input, "r");
        DBG("open %s - %d\n", G.input, f ? 1 : 0);
        if(!f) ERR("Can't open %s", G.input);
    }
    if(!readfile(f)) ERRX("Can't read data");
    double coeffs[3];
    int goodelems = 0, now = 0;
    do{
        goodelems = parabolic_fit(coeffs);
        red("focnow: %g\n", calcfocus(coeffs));
        if(!goodelems) ERRX("Can't fit");
        now = check(coeffs);
        if(MIN_ELEMENTS_AMOUNT > now) ERRX("All elements are too wrong");
    }while(now != goodelems);
    green("focus = %.2f\n", calcfocus(coeffs));
    return 0;
#if 0

    for(i = 0; i < 3; i++){
        printf("\n%c => %.2f", 97+i, matrix[i][3]);
    }
#endif
}
