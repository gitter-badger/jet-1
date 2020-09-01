/*
 * zlib License
 *
 * Regularized Incomplete Beta Function
 *
 * Copyright (c) 2016, 2017 Lewis Van Winkle
 * http://CodePlea.com
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgement in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 */

#include <math.h>

#define STOP 1.0e-8
#define TINY 1.0e-30

double incbeta(double a, double b, double x)
{
    if (x < 0.0 || x > 1.0) return 1.0 / 0.0;

    /*The continued fraction converges nicely for x < (a+1)/(a+b+2)*/
    if (x > (a + 1.0) / (a + b + 2.0)) {
        return (1.0 - incbeta(b, a, 1.0 - x));
        /*Use the fact that beta is symmetrical.*/
    }

    /*Find the first part before the continued fraction.*/
    const double lbeta_ab = lgamma(a) + lgamma(b) - lgamma(a + b);
    const double front = exp(log(x) * a + log(1.0 - x) * b - lbeta_ab) / a;

    /*Use Lentz's algorithm to evaluate the continued fraction.*/
    double f = 1.0, c = 1.0, d = 0.0;

    int i, m;
    for (i = 0; i <= 200; ++i) {
        m = i / 2;

        double numerator;
        if (i == 0) {
            numerator = 1.0; /*First numerator is 1.0.*/
        } else if (i % 2 == 0) {
            numerator = (m * (b - m) * x)
                / ((a + 2.0 * m - 1.0) * (a + 2.0 * m)); /*Even term.*/
        } else {
            numerator = -((a + m) * (a + b + m) * x)
                / ((a + 2.0 * m) * (a + 2.0 * m + 1)); /*Odd term.*/
        }

        /*Do an iteration of Lentz's algorithm.*/
        d = 1.0 + numerator * d;
        if (fabs(d) < TINY) d = TINY;
        d = 1.0 / d;

        c = 1.0 + numerator / c;
        if (fabs(c) < TINY) c = TINY;

        const double cd = c * d;
        f *= cd;

        /*Check for stop.*/
        if (fabs(1.0 - cd) < STOP) {
            return front * (f - 1.0);
        }
    }

    return 1.0 / 0.0; /*Needed more loops, did not converge.*/
}
#include <stdio.h>

#define PEAKRATE 9000 // 8000
#define PEAKDAY 120 // 107
#define NX 2 // ratio of drop duration to grow duration
#define NM 1001 // array size
#define NP (PEAKDAY * (NX + 1)) // number of days + 1

#define countof(x) (sizeof(x) / sizeof(x[0]))
#define foreach(idx, arr) for (int idx = 0; idx < NP; idx++)

#define max(x, y) ((y > x) ? (y) : (x))
int main()
{
    double x[NM] = {};
    x[0] = 0;
    for (int i = 1; i < NP; i++) x[i] = 1.0 * i / (NP - 1);
    double y[NM] = {};

    foreach (i, y)
        y[i] = incbeta(2, NX, x[i]);

    foreach (i, x)
        x[i] *= (NP - 1);

    for (int i = countof(y) - 1; i > 0; i--) y[i] -= y[i - 1];

    // double maxy = 0.0;
    // foreach (i, y)
    //     maxy = max(maxy, y[i]);
    double factor = PEAKRATE / y[PEAKDAY];

    foreach (i, y) {
        y[i] *= factor;
        // y[i] *= PEAKRATE;
    }

    for (int i = 1; i < NP; i++) printf("%f %f\n", x[i], y[i]);

    for (int i = 1; i < NP; i++)
        if (y[i] < y[i - 1]) {
            fprintf(stderr, "peak at %.1f%%\n", i * 100.0 / NP);
            break;
        }
}