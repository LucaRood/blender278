/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef __OMNI_TYPES_H__
#define __OMNI_TYPES_H__

#include <stdbool.h>

typedef unsigned int uint;

typedef struct float_or_uint {
	bool isf;
	union {
		uint u;
		float f;
	};
} float_or_uint;

#endif /* __OMNI_TYPES_H__ */
