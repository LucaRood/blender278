/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef __OMNI_OMNICACHE_H__
#define __OMNI_OMNICACHE_H__

#include <assert.h>

#include "types.h"

#define MAX_NAME 64

/*********
 * Enums *
 *********/

typedef enum OmniTimeType {
	OMNI_TIME_INT		= 1, /* Discrete integer time. */
	OMNI_TIME_FLOAT		= 2, /* Continuous floating point time. */
} OmniTimeType;

typedef enum OmniDataType {
	OMNI_DATA_GENERIC	= 0, /* Black box data not manipulated by OmniCache. */
	OMNI_DATA_META		= 1,
	OMNI_DATA_FLOAT		= 2,
	OMNI_DATA_FLOAT3	= 3,
	OMNI_DATA_INT		= 4,
	OMNI_DATA_INT3		= 5,
	OMNI_DATA_MAT3		= 6,
	OMNI_DATA_MAT4		= 7,
	OMNI_DATA_REF		= 8, /* Reference to a constant OnmiCache library block. */
	OMNI_DATA_TREF		= 9, /* Transformed reference to a constant OmniCache library block (includes MAT4). */

	OMNI_NUM_DTYPES	= 10, /* Number of data types (should always be the last entry). */
} OmniDataType;

/*********
 * Types *
 *********/

typedef struct OmniCache OmniCache;
typedef struct OmniSerial OmniSerial;

/* Transformed reference. */
typedef struct OmniTRef {
	uint index;
	float mat[4][4];
} OmniTRef;

/*************
 * Callbacks *
 *************/

typedef struct OmniData {
	OmniDataType dtype;
	uint dsize;
	uint dcount;
	void *data;
} OmniData;

typedef struct OmniInterpData {
	OmniData *target;
	OmniData *prev;
	OmniData *next;
	float_or_uint ttarget;
	float_or_uint tprev;
	float_or_uint tnext;
} OmniInterpData;

typedef uint (*OmniCountCallback)(void *user_data);
typedef bool (*OmniReadCallback)(OmniData *omni_data, void *user_data);
typedef bool (*OmniWriteCallback)(OmniData *omni_data, void *user_data);
typedef bool (*OmniInterpCallback)(OmniInterpData *interp_data);

typedef bool (*OmniMetaGenCallback)(void *user_data, void *result);

/*********
 * Flags *
 *********/

typedef enum OmniWriteResult {
	OMNI_WRITE_SUCCESS	= 0,
	OMNI_WRITE_INVALID	= (1 << 1), /* Leaving bit 0 clear in case it is decided to use it for success. */
	OMNI_WRITE_FAILED	= (1 << 2),
} OmniWriteResult;

typedef enum OmniReadResult {
	OMNI_READ_EXACT		= 0,
	OMNI_READ_INTERP	= (1 << 1), /* Leaving bit 0 clear in case it is decided to use it for exact. */
	OMNI_READ_OUTDATED	= (1 << 2),
	OMNI_READ_INVALID	= (1 << 3),
} OmniReadResult;

typedef enum OmniBlockFlags {
	OMNI_BLOCK_FLAG_CONTINUOUS	= (1 << 0), /* Continuous data that can be interpolated. */
	OMNI_BLOCK_FLAG_CONST_COUNT	= (1 << 1), /* Element count does not change between samples. (TODO: Check constness when writing) */
	OMNI_BLOCK_FLAG_MANDATORY	= (1 << 2), /* This block is always present in the cache, and can't be removed. (TODO: Respect this when removing blocks) */
} OmniBlockFlags;

typedef enum OmniCacheFlags {
	OMNICACHE_FLAG_FRAMED		= (1 << 0), /* Time in frames instead of seconds. */
	OMNICACHE_FLAG_INTERP_ANY	= (1 << 1), /* Interpolate when reading any inexistant sample is enabled. */
	OMNICACHE_FLAG_INTERP_SUB	= (1 << 2), /* Interpolate only when reading between `time_step` increments. */
} OmniCacheFlags;

typedef enum OmniConsolidationFlags {
	OMNI_CONSOL_CONSOLIDATE		= (1 << 0),
	OMNI_CONSOL_FREE_INVALID	= (1 << 1),
	OMNI_CONSOL_FREE_OUTDATED	= (1 << 2),
} OmniConsolidationFlags;

/*************
 * Templates *
 *************/

typedef struct OmniBlockTemplate {
	char id[MAX_NAME];

	OmniDataType data_type;
	uint data_size; /* Only required if `dtype` == `OMNI_DATA_GENERIC` */

	OmniBlockFlags flags;

	OmniCountCallback count;
	OmniReadCallback read;
	OmniWriteCallback write;
	OmniInterpCallback interp;
} OmniBlockTemplate;

typedef struct OmniCacheTemplate {
	char id[MAX_NAME];

	OmniTimeType time_type;

	/* Initial time and default step size.
	 * - `float` if `ttype` is `OMNI_SAMPLING_FLOAT`;
	 * - `uint` if `ttype` is `OMNI_SAMPLING_INT`.*/
	float_or_uint time_initial;
	float_or_uint time_final;
	float_or_uint time_step;

	OmniCacheFlags flags;

	uint meta_size;
	OmniMetaGenCallback meta_gen;

	uint num_blocks;
	OmniBlockTemplate blocks[];
} OmniCacheTemplate;

/*****************
 * API Functions *
 *****************/

#define OMNI_F_TO_FU(val) {.isf = true, .f = val}
#define OMNI_U_TO_FU(val) {.isf = false, .u = val}
#define OMNI_FU_GET(val) (val.isf ? val.f : val.u)

float_or_uint OMNI_f_to_fu(float val);
float_or_uint OMNI_u_to_fu(uint val);

OmniCache *OMNI_new(const OmniCacheTemplate *c_temp, const char blocks[]);
OmniCache *OMNI_duplicate(const OmniCache *source, bool copy_data);
void OMNI_free(OmniCache *cache);

void OMNI_blocks_add(OmniCache *cache, const OmniCacheTemplate *cache_temp, const char blocks[]);
void OMNI_blocks_remove(OmniCache *cache, const char blocks[]);
void OMNI_blocks_set(OmniCache *cache, const OmniCacheTemplate *cache_temp, const char blocks[]);

void OMNI_block_add_by_index(OmniCache *cache, const OmniCacheTemplate *cache_temp, const uint block);
void OMNI_block_remove_by_index(OmniCache *cache, const uint block);

OmniWriteResult OMNI_sample_write(OmniCache *cache, float_or_uint time, void *data);
OmniReadResult OMNI_sample_read(OmniCache *cache, float_or_uint time, void *data);

void OMNI_set_range(OmniCache *cache, float_or_uint time_initial, float_or_uint time_final, float_or_uint time_step);
void OMNI_get_range(OmniCache *cache, float_or_uint *time_initial, float_or_uint *time_final, float_or_uint *time_step);
uint OMNI_get_num_cached(OmniCache *cache);

bool OMNI_is_valid(OmniCache *cache);
bool OMNI_is_current(OmniCache *cache);

bool OMNI_sample_is_valid(OmniCache *cache, float_or_uint time);
bool OMNI_sample_is_current(OmniCache *cache, float_or_uint time);

void OMNI_consolidate(OmniCache *cache, OmniConsolidationFlags flags);

void OMNI_mark_outdated(OmniCache *cache);
void OMNI_mark_invalid(OmniCache *cache);
void OMNI_clear(OmniCache *cache);

void OMNI_sample_mark_outdated(OmniCache *cache, float_or_uint time);
void OMNI_sample_mark_invalid(OmniCache *cache, float_or_uint time);
void OMNI_sample_clear(OmniCache *cache, float_or_uint time);

void OMNI_sample_mark_outdated_from(OmniCache *cache, float_or_uint time);
void OMNI_sample_mark_invalid_from(OmniCache *cache, float_or_uint time);
void OMNI_sample_clear_from(OmniCache *cache, float_or_uint time);

uint OMNI_serial_get_size(const OmniCache *cache, bool serialize_data);
OmniSerial *OMNI_serialize(const OmniCache *cache, bool serialize_data, uint *size);
void OMNI_serialize_to_buffer(OmniSerial *serial, const OmniCache *cache, bool serialize_data);
OmniCache *OMNI_deserialize(OmniSerial *serial, const OmniCacheTemplate *cache_temp);

#endif /* __OMNI_OMNICACHE_H__ */
