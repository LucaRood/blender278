/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "utils.h"

float_or_uint fu_add(float_or_uint fu1, float_or_uint fu2)
{
	float_or_uint result;

	assert(fu1.isf == fu2.isf);

	result.isf = fu1.isf;

	if (result.isf) {
		result.f = fu1.f + fu2.f;
	}
	else {
		result.u = fu1.u + fu2.u;
	}

	return result;
}

float_or_uint fu_sub(float_or_uint fu1, float_or_uint fu2)
{
	float_or_uint result;

	assert(fu1.isf == fu2.isf);

	result.isf = fu1.isf;

	if (result.isf) {
		result.f = fu1.f - fu2.f;
	}
	else {
		result.u = fu1.u - fu2.u;
	}

	return result;
}

float_or_uint fu_mul(float_or_uint fu1, float_or_uint fu2)
{
	float_or_uint result;

	assert(fu1.isf == fu2.isf);

	result.isf = fu1.isf;

	if (result.isf) {
		result.f = fu1.f * fu2.f;
	}
	else {
		result.u = fu1.u * fu2.u;
	}

	return result;
}

float_or_uint fu_div(float_or_uint fu1, float_or_uint fu2)
{
	float_or_uint result;

	assert(fu1.isf == fu2.isf);

	result.isf = fu1.isf;

	if (result.isf) {
		result.f = fu1.f / fu2.f;
	}
	else {
		result.u = fu1.u / fu2.u;
	}

	return result;
}

float_or_uint fu_mod(float_or_uint fu1, float_or_uint fu2)
{
	float_or_uint result;

	assert(fu1.isf == fu2.isf);

	result.isf = fu1.isf;

	if (result.isf) {
		result.f = fmod(fu1.f, fu2.f);
	}
	else {
		result.u = fu1.u % fu2.u;
	}

	return result;
}

float fu_float(float_or_uint fu)
{
	return (float)(fu.isf ? fu.f : fu.u);
}

uint fu_uint(float_or_uint fu)
{
	return (uint)(fu.isf ? fu.f : fu.u);
}

uint pow_u(uint base, uint exp)
{
	uint result = 1;

	while (exp) {
		if (exp & 1) {
			result *= base;
		}
		exp >>= 1;
		base *= base;
	}

	return result;
}

uint min_array_size(uint index)
{
	if (MIN_ARRAY > index) {
		return MIN_ARRAY;
	}

	return pow_u(2, (uint)ceil(log2((double)(index + 1))));
}

void *dupalloc(const void *source, const size_t size)
{
	if (!source) {
		return NULL;
	}

	void *target = malloc(size);
	memcpy(target, source, size);

	return target;
}
