/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef __OMNI_OMNI_SERIAL_H__
#define __OMNI_OMNI_SERIAL_H__

#include "omni_types.h"

uint serial_calc_size(const OmniCache *cache, bool serialize_data);
void serialize(OmniSerial *serial, const OmniCache *cache, bool serialize_data);
OmniCache *deserialize(OmniSerial *serial, const OmniCacheTemplate *cache_temp);

#endif /* __OMNI_OMNI_SERIAL_H__ */
