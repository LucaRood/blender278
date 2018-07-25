/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef __OMNI_OMNI_UTILS_H__
#define __OMNI_OMNI_UTILS_H__

#include "omni_types.h"
#include "utils.h"

#define DATA_SIZE(dtype, dsize) (dtype == OMNI_DATA_GENERIC) ? dsize : OMNI_DATA_TYPE_SIZE[dtype];

#define IS_VALID(object) (object && (object->status & OMNI_STATUS_VALID))
#define IS_CURRENT(object) (IS_VALID(object) && (object->status & OMNI_STATUS_CURRENT))

#define SAMPLE_IS_ROOT(sample) FU_FL_EQ(sample->toffset, 0.0f)
#define SAMPLE_IS_SKIPPED(sample) (sample->status & OMNI_SAMPLE_STATUS_SKIP)
#define SAMPLE_IS_VALID(sample) (IS_VALID(sample) && !(sample->status & OMNI_SAMPLE_STATUS_SKIP) && (sample->num_blocks_invalid == 0))
#define SAMPLE_IS_CURRENT(sample) (SAMPLE_IS_VALID(sample) && (sample->status & OMNI_STATUS_CURRENT) && (sample->num_blocks_outdated == 0))

#define TTYPE_VALID(ttype) (ttype != OMNI_TIME_INVALID)
#define TTYPE_FLOAT(ttype) (ttype == OMNI_TIME_FLOAT)
#define TTYPE_INT(ttype) (ttype == OMNI_TIME_INT)

typedef void (*iter_callback)(OmniSample *sample);

void block_set_status(OmniBlock *block, OmniBlockStatusFlags status);
void block_unset_status(OmniBlock *block, OmniBlockStatusFlags status);

void meta_set_status(OmniSample *sample, OmniBlockStatusFlags status);
void meta_unset_status(OmniSample *sample, OmniBlockStatusFlags status);

void sample_set_status(OmniSample *sample, OmniSampleStatusFlags status);
void sample_unset_status(OmniSample *sample, OmniSampleStatusFlags status);

void cache_set_status(OmniCache *cache, OmniCacheStatusFlags status);
void cache_unset_status(OmniCache *cache, OmniCacheStatusFlags status);

sample_time gen_sample_time(OmniCache *cache, float_or_uint time);

void samples_iterate(OmniSample *start, iter_callback list, iter_callback root, iter_callback first);
OmniSample *sample_prev(OmniSample *sample);
OmniSample *sample_last(OmniSample *sample);

void resize_sample_array(OmniCache *cache, uint size);
void init_sample_blocks(OmniSample *sample);

void block_info_init(OmniCache *cache, const OmniCacheTemplate *cache_temp, const uint target_index, const uint source_index);
void block_info_array_init(OmniCache *cache, const OmniCacheTemplate *cache_temp, bool *mask);
void update_block_parents(OmniCache *cache);

bool block_id_in_str(const char id_str[], const char id[]);
bool *block_id_mask(const OmniCacheTemplate *cache_temp, const char id_str[], uint *num_blocks);

#endif /* __OMNI_OMNI_UTILS_H__ */
