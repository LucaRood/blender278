/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef __OMNI_OMNI_TYPES_H__
#define __OMNI_OMNI_TYPES_H__

#include "types.h"
#include "omnicache.h"

/* enum OmniTimeType */
#define OMNI_TIME_INVALID 0

/* Size of the data types in `omnicache.h` (keep in sync). */
static const uint OMNI_DATA_TYPE_SIZE[] = {
	0,						/* OMNI_DATA_GENERIC */
	0,						/* OMNI_DATA_META */
	sizeof(float),			/* OMNI_DATA_FLOAT */
	sizeof(float[3]),		/* OMNI_DATA_FLOAT3 */
	sizeof(int),			/* OMNI_DATA_INT */
	sizeof(int[3]),			/* OMNI_DATA_INT3 */
	sizeof(float[3][3]),	/* OMNI_DATA_MAT3 */
	sizeof(float[4][4]),	/* OMNI_DATA_MAT4 */
	sizeof(uint),			/* OMNI_DATA_REF */
	sizeof(OmniTRef),		/* OMNI_DATA_TREF */
};

static_assert((sizeof(OMNI_DATA_TYPE_SIZE) / sizeof(*OMNI_DATA_TYPE_SIZE)) == OMNI_NUM_DTYPES, "OmniCache: data type mismatch");

typedef struct sample_time {
	OmniTimeType ttype;
	uint index;
	float_or_uint offset;
} sample_time;

/* Only bits 0-15 used here.
 * Bits 16-31 are reserved for exclusive object flags. */
typedef enum OmniStatusFlags {
	OMNI_STATUS_INITED	= (1 << 0),
	OMNI_STATUS_VALID	= (1 << 1),
	OMNI_STATUS_CURRENT	= (1 << 2),
} OmniStatusFlags;

/* Block */

/* Block definition data. */
typedef struct OmniBlockInfoDef {
	char id[MAX_NAME];
	uint index;

	OmniDataType dtype;
	uint dsize;

	OmniBlockFlags flags;
} OmniBlockInfoDef;

/* Block runtime data. */
typedef struct OmniBlockInfo {
	OmniBlockInfoDef def;

	struct OmniCache *parent;

	OmniCountCallback count;
	OmniReadCallback read;
	OmniWriteCallback write;
	OmniInterpCallback interp;
} OmniBlockInfo;

/* Bits 0-15 are used for OmniStatusFlags. */
typedef enum OmniBlockStatusFlags {
	OMNI_BLOCK_STATUS_FLAGS	= (1 << 15), /* End of range reserved by OmniStatusFlags. */
} OmniBlockStatusFlags;

typedef struct OmniBlock {
	struct OmniSample *parent;

	OmniBlockStatusFlags status;
	uint dcount;

	void *data;
} OmniBlock;

typedef struct OmniMetaBlock {
	OmniBlockStatusFlags status;

	void *data;
} OmniMetaBlock;


/* Sample */

/* Bits 0-15 are used for OmniStatusFlags. */
typedef enum OmniSampleStatusFlags {
	OMNI_SAMPLE_STATUS_FLAGS	= (1 << 15), /* End of range reserved by OmniStatusFlags. */
	OMNI_SAMPLE_STATUS_SKIP		= (1 << 16), /* Unused sample. */
} OmniSampleStatusFlags;

typedef struct OmniSample {
	struct OmniSample *next;
	struct OmniCache *parent;
	OmniMetaBlock meta;

	OmniSampleStatusFlags status;

	uint tindex;
	float_or_uint toffset;

	uint num_blocks_invalid;
	uint num_blocks_outdated;

	OmniBlock *blocks;
} OmniSample;


/* Cache */

/* Bits 0-15 are used for OmniStatusFlags. */
typedef enum OmniCacheStatusFlags {
	OMNI_CACHE_STATUS_FLAGS		= (1 << 15), /* End of range reserved by OmniStatusFlags. */
	OMNI_CACHE_STATUS_COMPLETE	= (1 << 16), /* Set if the whole frame range is cached (valid). */
} OmniCacheStatusFlags;

/* Cache definition data. */
typedef struct OmniCacheDef {
	char id[MAX_NAME];

	OmniTimeType ttype;
	float_or_uint tinitial;
	float_or_uint tfinal;
	float_or_uint tstep;

	OmniCacheFlags flags;

	uint num_blocks;
	uint num_samples_array; /* Number of samples initialized in the array. */
	uint num_samples_tot; /* Total number of non-skipped initialized samples (including sub-samples) */

	uint msize;
} OmniCacheDef;

/* Cache runtime data. */
typedef struct OmniCache {
	OmniCacheDef def;

	OmniCacheStatusFlags status;

	uint num_samples_alloc; /* Number of samples allocated in the array. */

	OmniBlockInfo *block_index;
	OmniSample *samples;

	OmniMetaGenCallback meta_gen;
} OmniCache;

#endif /* __OMNI_OMNI_TYPES_H__ */
