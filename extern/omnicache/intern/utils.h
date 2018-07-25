/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef __OMNI_UTILS_H__
#define __OMNI_UTILS_H__

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <math.h>

#include "types.h"

#ifdef __GNUC__
#  define UNUSED(x) UNUSED_ ## x __attribute__((__unused__))
#else
#  define UNUSED(x) UNUSED_ ## x
#endif

#define MIN_ARRAY 32

#define MIN(val1, val2) (val1 < val2 ? val1 : val2)
#define MAX(val1, val2) (val1 > val2 ? val1 : val2)

#define FU_EQ(fu1, fu2) (fu1.isf ? (fu1.f == fu2.f) : (fu1.u == fu2.u))
#define FU_LT(fu1, fu2) (fu1.isf ? (fu1.f < fu2.f) : (fu1.u < fu2.u))
#define FU_GT(fu1, fu2) (fu1.isf ? (fu1.f > fu2.f) : (fu1.u > fu2.u))
#define FU_LE(fu1, fu2) (fu1.isf ? (fu1.f <= fu2.f) : (fu1.u <= fu2.u))
#define FU_GE(fu1, fu2) (fu1.isf ? (fu1.f >= fu2.f) : (fu1.u >= fu2.u))

#define FU_FL_EQ(fu, fl) (fu.isf ? (fu.f == fl) : (fu.u == (int)fl))
#define FU_FL_LT(fu, fl) (fu.isf ? (fu.f < fl) : (fu.u < (int)fl))
#define FU_FL_GT(fu, fl) (fu.isf ? (fu.f > fl) : (fu.u > (int)fl))
#define FU_FL_LE(fu, fl) (fu.isf ? (fu.f <= fl) : (fu.u <= (int)fl))
#define FU_FL_GE(fu, fl) (fu.isf ? (fu.f >= fl) : (fu.u >= (int)fl))

float_or_uint fu_add(float_or_uint fu1, float_or_uint fu2);
float_or_uint fu_sub(float_or_uint fu1, float_or_uint fu2);
float_or_uint fu_mul(float_or_uint fu1, float_or_uint fu2);
float_or_uint fu_div(float_or_uint fu1, float_or_uint fu2);
float_or_uint fu_mod(float_or_uint fu1, float_or_uint fu2);

float fu_float(float_or_uint fu);
uint fu_uint(float_or_uint fu);

uint pow_u(uint base, uint exp);

uint min_array_size(uint index);

void *dupalloc(const void *source, const size_t size);

#endif /* __OMNI_UTILS_H__ */
