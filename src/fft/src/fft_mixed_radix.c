/*
 * Copyright (c) 2007, 2008, 2009, 2010, 2011, 2012 Joseph Gaeddert
 * Copyright (c) 2007, 2008, 2009, 2010, 2011, 2012 Virginia Polytechnic
 *                                      Institute & State University
 *
 * This file is part of liquid.
 *
 * liquid is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * liquid is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with liquid.  If not, see <http://www.gnu.org/licenses/>.
 */

//
// fft_mixed_radix.c : definitions for mixed-radix transforms using
//                     the Cooley-Tukey algorithm
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "liquid.internal.h"

#define FFT_DEBUG_MIXED_RADIX 0

// create FFT plan for regular DFT
//  _nfft   :   FFT size
//  _x      :   input array [size: _nfft x 1]
//  _y      :   output array [size: _nfft x 1]
//  _dir    :   fft direction: {FFT_FORWARD, FFT_REVERSE}
//  _method :   fft method
FFT(plan) FFT(_create_plan_mixed_radix)(unsigned int _nfft,
                                        TC *         _x,
                                        TC *         _y,
                                        int          _dir,
                                        int          _flags)
{
    // allocate plan and initialize all internal arrays to NULL
    FFT(plan) q = (FFT(plan)) malloc(sizeof(struct FFT(plan_s)));

    q->nfft      = _nfft;
    q->x         = _x;
    q->y         = _y;
    q->flags     = _flags;
    q->kind      = LIQUID_FFT_DFT_1D;
    q->direction = (_dir == FFT_FORWARD) ? FFT_FORWARD : FFT_REVERSE;
    q->method    = LIQUID_FFT_METHOD_MIXED_RADIX;

    q->execute   = FFT(_execute_mixed_radix);

    // find first 'prime' factor of _nfft
    unsigned int i;
    unsigned int Q=0;
    for (i=2; i<q->nfft; i++) {
        if ( (q->nfft % i)==0 ) {
            Q = i;
            break;
        }
    }
    if (Q==0) {
        fprintf(stderr,"error: fft_create_plan_mixed_radix(), _nfft=%u is prime\n", _nfft);
        exit(1);
    }

    // set mixed-radix data
    unsigned int P = q->nfft / Q;
    q->data.mixedradix.Q = Q;
    q->data.mixedradix.P = P;

    // allocate memory for buffers
    unsigned int t_len = Q > P ? Q : P;
    q->data.mixedradix.t0 = (TC *) malloc(t_len * sizeof(TC));
    q->data.mixedradix.t1 = (TC *) malloc(t_len * sizeof(TC));

    // allocate memory for input buffers
    q->data.mixedradix.x = (TC *) malloc(q->nfft * sizeof(TC));

    // create sub-transforms
    q->num_subplans = 2;
    q->subplans = (FFT(plan)*) malloc(q->num_subplans*sizeof(FFT(plan)));

    // P-point FFT
    q->subplans[0] = FFT(_create_plan)(q->data.mixedradix.P,
                                       q->data.mixedradix.t0,
                                       q->data.mixedradix.t1,
                                       q->direction,
                                       q->flags);

    // Q-point FFT
    q->subplans[1] = FFT(_create_plan)(q->data.mixedradix.Q,
                                       q->data.mixedradix.t0,
                                       q->data.mixedradix.t1,
                                       q->direction,
                                       q->flags);

    // initialize twiddle factors, indices for mixed-radix transforms
    // TODO : only allocate necessary twiddle factors
    q->twiddle = (TC *) malloc(q->nfft * sizeof(TC));
    
    T d = (q->direction == FFT_FORWARD) ? -1.0 : 1.0;
    for (i=0; i<q->nfft; i++)
        q->twiddle[i] = cexpf(_Complex_I*d*2*M_PI*(T)i / (T)(q->nfft));

    return q;
}

// destroy FFT plan
void FFT(_destroy_plan_mixed_radix)(FFT(plan) _q)
{
    // destroy sub-plans
    FFT(_destroy_plan)(_q->subplans[0]);
    FFT(_destroy_plan)(_q->subplans[1]);
    free(_q->subplans);

    // free data specific to mixed-radix transforms
    free(_q->data.mixedradix.t0);
    free(_q->data.mixedradix.t1);
    free(_q->data.mixedradix.x);

    // free twiddle factors
    free(_q->twiddle);

    // free main object memory
    free(_q);
}

// execute mixed-radix FFT
void FFT(_execute_mixed_radix)(FFT(plan) _q)
{
    // set internal constants
    unsigned int P = _q->data.mixedradix.P; // first FFT size
    unsigned int Q = _q->data.mixedradix.Q; // second FFT size

    // set pointers
    TC * t0      = _q->data.mixedradix.t0;
    TC * t1      = _q->data.mixedradix.t1;
    TC * x       = _q->data.mixedradix.x;
    TC * twiddle = _q->twiddle;

    // copy input to internal buffer
    memmove(x, _q->x, _q->nfft*sizeof(TC));

    unsigned int i;
    unsigned int k;

    // compute 'Q' DFTs of size 'P'
#if FFT_DEBUG_MIXED_RADIX
    printf("computing %u DFTs of size %u\n", Q, P);
#endif
    for (i=0; i<Q; i++) {
        // copy to temporary buffer
        for (k=0; k<P; k++)
            t0[k] = x[Q*k+i];

        // run internal P-point DFT
        FFT(_execute)(_q->subplans[0]);

        // copy back to input, applying twiddle factors
        for (k=0; k<P; k++)
            x[Q*k+i] = t1[k] * twiddle[i*k];

#if FFT_DEBUG_MIXED_RADIX
        printf("i=%3u/%3u\n", i, Q);
        for (k=0; k<P; k++)
            printf("  %12.6f %12.6f\n", crealf(x[Q*k+i]), cimagf(x[Q*k+i]));
#endif
    }

    // compute 'P' DFTs of size 'Q' and transpose
#if DEBUG
    printf("computing %u DFTs of size %u\n", P, Q);
#endif
    for (i=0; i<P; i++) {
        // copy to temporary buffer
        for (k=0; k<Q; k++)
            t0[k] = x[Q*i+k];

        // run internal Q-point DFT
        FFT(_execute)(_q->subplans[1]);

        // copy and transpose
        for (k=0; k<Q; k++)
            _q->y[k*P+i] = t1[k];
        
#if DEBUG
        printf("i=%3u/%3u\n", i, P);
        for (k=0; k<Q; k++)
            printf("  %12.6f %12.6f\n", crealf(_q->y[k*P+i]), cimagf(_q->y[k*P+i]));
#endif
    }
}

