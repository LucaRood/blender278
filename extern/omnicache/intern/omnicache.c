/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "omnicache.h"

#include "omni_utils.h"
#include "omni_serial.h"

static OmniSample *sample_get(OmniCache *cache, sample_time stime, bool create,
                              OmniSample **prev, OmniSample **next)
{
#define ASS_PREV(cache, index) (index > 0 ? sample_last(&cache->samples[index - 1]) : NULL)
#define ASS_NEXT(next, cache, nindex) (next ? next : (nindex < cache->def.num_samples_array ? &cache->samples[nindex] : NULL))

	OmniSample *sample = NULL;

	if (prev) {
		*prev = NULL;
	}

	if (next) {
		*next = NULL;
	}

	if (!TTYPE_VALID(stime.ttype)) {
		return NULL;
	}

	if (stime.index >= cache->num_samples_alloc) {
		if (create) {
			resize_sample_array(cache, min_array_size(stime.index));

			update_block_parents(cache);
		}
		else {
			if (prev) {
				*prev = ASS_PREV(cache, cache->def.num_samples_array);
			}

			return NULL;
		}
	}

	/* Increment array sample count until required sample, initializing all samples along the way. */
	if (cache->def.num_samples_array <= stime.index) {
		if (create) {
			for (; cache->def.num_samples_array <= stime.index; cache->def.num_samples_array++) {
				OmniSample *samp = &cache->samples[cache->def.num_samples_array];

				samp->parent = cache;
				samp->tindex = cache->def.num_samples_array;
				sample_set_status(samp, OMNI_SAMPLE_STATUS_SKIP);
			}
		}
		else {
			if (prev) {
				*prev = ASS_PREV(cache, cache->def.num_samples_array);
			}

			return NULL;
		}
	}

	/* Find or add sample. */
	{
		bool new = false;

		if (FU_FL_EQ(stime.offset, 0.0f)) {
			/* Sample is at time zero (i.e. sits directly in the array). */
			sample = &cache->samples[stime.index];

			if (SAMPLE_IS_SKIPPED(sample)) {
				new = true;
			}

			if (prev) {
				*prev = ASS_PREV(cache, stime.index);
			}
		}
		else {
			OmniSample *p = &cache->samples[stime.index];
			OmniSample *n = p->next;

			while (n && FU_LT(n->toffset, stime.offset)) {
				p = n;
				n = n->next;
			}

			if (prev) {
				*prev = p;
			}

			if (n && FU_EQ(n->toffset, stime.offset)) {
				/* Sample already exists. */
				sample = n;
			}
			else if (create) {
				/* New sample should be created. */
				sample = calloc(1, sizeof(OmniSample));
				sample->toffset = stime.offset;

				p->next = sample;
				sample->next = n;

				new = true;
			}
			else {
				if (next) {
					*next = ASS_NEXT(n, cache, stime.index + 1);
				}

				return NULL;
			}
		}

		if (next) {
			*next = ASS_NEXT(sample->next, cache, stime.index + 1);
		}

		if (new) {
			sample->parent = cache;
			sample->tindex = stime.index;

			init_sample_blocks(sample);

			sample_set_status(sample, OMNI_STATUS_INITED);
			sample_unset_status(sample, OMNI_SAMPLE_STATUS_SKIP);

			cache->def.num_samples_tot++;
		}
	}

	return sample;

#undef ASS_PREV
#undef ASS_NEXT
}

/* Utility to get sample without manually generating `sample_time`. */
static OmniSample *sample_get_from_time(OmniCache *cache, float_or_uint time, bool create,
                                        OmniSample **prev, OmniSample **next)
{
	sample_time stime = gen_sample_time(cache, time);

	return sample_get(cache, stime, create, prev, next);
}

/* Free all blocks in a sample (also frees metadata) */
static void blocks_free(OmniSample *sample)
{
	OmniCache *cache = sample->parent;

	if (sample->blocks) {
		for (uint i = 0; i < cache->def.num_blocks; i++) {
			free(sample->blocks[i].data);
		}

		free(sample->blocks);
		sample->blocks = NULL;
	}

	if (sample->meta.data) {
		free(sample->meta.data);
		sample->meta.data = NULL;
	}

	meta_unset_status(sample, OMNI_STATUS_VALID);
	sample_unset_status(sample, OMNI_STATUS_VALID);
}

/* Sample iterator helpers */

static void sample_mark_outdated(OmniSample *sample)
{
	sample_unset_status(sample, OMNI_STATUS_CURRENT);
}

static void sample_mark_invalid(OmniSample *sample)
{
	sample_unset_status(sample, OMNI_STATUS_VALID);
}

static void sample_clear_ref(OmniSample *sample)
{
	if (!SAMPLE_IS_ROOT(sample)) {
		OmniSample *prev = sample_prev(sample);

		prev->next = NULL;
	}
}

static void sample_remove_list(OmniSample *sample)
{
	blocks_free(sample);

	sample->parent->def.num_samples_tot--;

	free(sample);
}

static void sample_remove_root(OmniSample *sample)
{
	blocks_free(sample);

	if (!SAMPLE_IS_SKIPPED(sample)) {
		sample->parent->def.num_samples_tot--;

		sample_set_status(sample, OMNI_SAMPLE_STATUS_SKIP);
	}
}

static void sample_remove(OmniSample *sample)
{
	if (sample) {
		if (SAMPLE_IS_ROOT(sample)) {
			sample_remove_root(sample);
		}
		else {
			OmniSample *prev = sample_prev(sample);
			prev->next = sample->next;

			sample_remove_list(sample);
		}
	}
}

static void sample_remove_invalid(OmniSample *sample)
{
	if (!SAMPLE_IS_VALID(sample)) {
		sample_remove(sample);
	}
}

static void sample_remove_outdated(OmniSample *sample)
{
	if (!SAMPLE_IS_CURRENT(sample))
	{
		sample_remove(sample);
	}
}

static void samples_free(OmniCache *cache)
{
	if (cache->samples) {
		samples_iterate(cache->samples, sample_remove_list, blocks_free, NULL);

		free(cache->samples);
		cache->samples = NULL;
	}

	cache->num_samples_alloc = 0;
	cache->def.num_samples_array = 0;
	cache->def.num_samples_tot = 0;

	cache_set_status(cache, OMNI_STATUS_CURRENT);
}


/* Public API functions */

float_or_uint OMNI_f_to_fu(float val)
{
	float_or_uint fou = {
	    .isf = true,
	    .f = val,
	};

	return fou;
}

float_or_uint OMNI_u_to_fu(uint val)
{
	float_or_uint fou = {
	    .isf = false,
	    .u = val,
	};

	return fou;
}

OmniCache *OMNI_new(const OmniCacheTemplate *cache_temp, const char blocks[])
{
	OmniCache *cache = calloc(1, sizeof(OmniCache));

	assert(FU_FL_GT(cache_temp->time_step, 0.0f));
	assert(TTYPE_FLOAT(cache_temp->time_type) == cache_temp->time_initial.isf);
	assert(TTYPE_FLOAT(cache_temp->time_type) == cache_temp->time_final.isf);
	assert(TTYPE_FLOAT(cache_temp->time_type) == cache_temp->time_step.isf);
	assert(FU_LE(cache_temp->time_initial, cache_temp->time_final));

	strncpy(cache->def.id, cache_temp->id, MAX_NAME);

	cache->def.tinitial = cache_temp->time_initial;
	cache->def.tfinal = cache_temp->time_final;
	cache->def.tstep = cache_temp->time_step;

	cache->def.ttype = cache_temp->time_type;
	cache->def.flags = cache_temp->flags;
	cache->def.msize = cache_temp->meta_size;

	cache->meta_gen = cache_temp->meta_gen;

	/* Blocks */
	if (cache_temp->num_blocks) {
		bool *mask = block_id_mask(cache_temp, blocks, &cache->def.num_blocks);

		block_info_array_init(cache, cache_temp, mask);

		free(mask);
	}

	cache_set_status(cache, OMNI_STATUS_CURRENT);

	return cache;
}

OmniCache *OMNI_duplicate(const OmniCache *source, bool copy_data)
{
	OmniCache *cache = dupalloc(source, sizeof(OmniCache));

	if (cache->def.num_blocks) {
		cache->block_index = dupalloc(cache->block_index, sizeof(OmniBlockInfo) * cache->def.num_blocks);

		for (uint i = 0; i < cache->def.num_blocks; i++) {
			cache->block_index[i].parent = cache;
		}
	}

	if (copy_data) {
		cache->samples = dupalloc(cache->samples, sizeof(OmniSample) * cache->num_samples_alloc);

		for (uint i = 0; i < cache->def.num_samples_array; i++) {
			OmniSample *sample = &cache->samples[i];

			do {
				sample->parent = cache;

				sample->blocks = dupalloc(sample->blocks, sizeof(OmniBlock) * cache->def.num_blocks);

				for (uint j = 0; j < cache->def.num_blocks; j++) {
					OmniBlock *block = &sample->blocks[j];

					block->parent = sample;
					block->data = dupalloc(block->data, cache->block_index[j].def.dsize * block->dcount);
				}

				sample->next = dupalloc(sample->next, sizeof(OmniSample));

				sample = sample->next;
			} while (sample);
		}
	}
	else {
		cache_set_status(cache, OMNI_STATUS_CURRENT);
		cache_unset_status(cache, OMNI_CACHE_STATUS_COMPLETE);

		cache->num_samples_alloc = 0;
		cache->def.num_samples_array = 0;
		cache->def.num_samples_tot = 0;

		cache->samples = NULL;
	}

	return cache;
}

void OMNI_free(OmniCache *cache)
{
	samples_free(cache);

	free(cache->block_index);
	free(cache);
}

/* TODO: Preserve settings from existing blocks. */
void OMNI_blocks_add(OmniCache *cache, const OmniCacheTemplate *cache_temp, const char blocks[])
{
	uint count = 0;
	bool *mask = block_id_mask(cache_temp, blocks, &count);

	for (uint i = 0; i < cache->def.num_blocks; i++) {
		uint index = cache->block_index[i].def.index;

		if (mask[index] == false) {
			count++;
			mask[index] = true;
		}
	}

	samples_free(cache);

	cache->def.num_blocks = count;
	free(cache->block_index);

	block_info_array_init(cache, cache_temp, mask);

	free(mask);
}

void OMNI_blocks_remove(OmniCache *cache, const char blocks[])
{
	bool *mask = malloc(sizeof(bool) * cache->def.num_blocks);
	OmniBlockInfo *block_index;
	uint num_blocks = 0;

	for (uint i = 0; i < cache->def.num_blocks; i++) {
		mask[i] = !block_id_in_str(blocks, cache->block_index[i].def.id);

		if (mask[i]) {
			num_blocks++;
		}
	}

	samples_free(cache);

	block_index = malloc(sizeof(OmniBlockInfo) * num_blocks);

	for (uint i = 0, j = 0; i < cache->def.num_blocks; i++) {
		if (mask[i]) {
			memcpy(&block_index[j++], &cache->block_index[i], sizeof(OmniBlockInfo));
		}
	}

	free(cache->block_index);
	cache->block_index = block_index;
	cache->def.num_blocks = num_blocks;
}

/* TODO: Preserve settings from existing blocks. */
void OMNI_blocks_set(OmniCache *cache, const OmniCacheTemplate *cache_temp, const char blocks[])
{
	bool *mask;

	samples_free(cache);
	free(cache->block_index);

	mask = block_id_mask(cache_temp, blocks, &cache->def.num_blocks);

	block_info_array_init(cache, cache_temp, mask);

	free(mask);
}

void OMNI_block_add_by_index(OmniCache *cache, const OmniCacheTemplate *cache_temp, const uint block)
{
	uint index = cache->def.num_blocks;
	OmniBlockInfo *block_index;

	/* Find where to insert the new block. */
	while (index > 0 && block <= cache->block_index[index - 1].def.index) {
		index--;
	}

	/* Block already exists. */
	if (index < cache->def.num_blocks && block == cache->block_index[index].def.index) {
		return;
	}

	samples_free(cache);

	block_index = malloc(sizeof(OmniBlockInfo) * (cache->def.num_blocks + 1));

	/* Copy blocks before currently inserted block. */
	memcpy(block_index, cache->block_index, sizeof(OmniBlockInfo) * index);

	/* Copy blocks after currently inserted block. */
	if (index < cache->def.num_blocks) {
		memcpy(&block_index[index + 1], &cache->block_index[index], sizeof(OmniBlockInfo) * (cache->def.num_blocks - index));
	}

	free(cache->block_index);
	cache->block_index = block_index;
	cache->def.num_blocks++;

	block_info_init(cache, cache_temp, index, block);
}

void OMNI_block_remove_by_index(OmniCache *cache, const uint block)
{
	uint index = cache->def.num_blocks - 1;
	OmniBlockInfo *block_index;

	/* Find block to remove. */
	while (index > 0 && block != cache->block_index[index].def.index) {
		index--;
	}

	/* Block does not exist. */
	if (block != cache->block_index[index].def.index) {
		return;
	}

	samples_free(cache);

	cache->def.num_blocks--;

	block_index = malloc(sizeof(OmniBlockInfo) * (cache->def.num_blocks));

	/* Copy blocks before removed block. */
	memcpy(block_index, cache->block_index, sizeof(OmniBlockInfo) * index);

	if (index < cache->def.num_blocks) {
		memcpy(&block_index[index], &cache->block_index[index + 1], sizeof(OmniBlockInfo) * (cache->def.num_blocks - index));
	}

	free(cache->block_index);
	cache->block_index = block_index;
}

OmniWriteResult OMNI_sample_write(OmniCache *cache, float_or_uint time, void *data)
{
	OmniSample *sample = sample_get_from_time(cache, time, true, NULL, NULL);

	if (!sample) {
		return OMNI_WRITE_INVALID;
	}

	for (uint i = 0; i < cache->def.num_blocks; i++) {
		OmniBlockInfo *b_info = &cache->block_index[i];
		OmniBlock *block = &sample->blocks[i];
		OmniData omni_data;
		uint dcount = b_info->count(data);

		if (block->data && block->dcount != dcount) {
			free(block->data);
			block->data = NULL;
		}

		block->dcount = dcount;

		if (!block->data) {
			block->data = malloc(b_info->def.dsize * block->dcount);
		}

		omni_data.dtype = b_info->def.dtype;
		omni_data.dsize = b_info->def.dsize;
		omni_data.dcount = block->dcount;
		omni_data.data = block->data;

		if (b_info->write(&omni_data, data)) {
			block_set_status(block, OMNI_STATUS_CURRENT);
		}
		else {
			block_unset_status(block, OMNI_STATUS_VALID);
			sample_unset_status(sample, OMNI_STATUS_VALID);

			return OMNI_WRITE_FAILED;
		}

		/* Ensure the user did not reallocate the data pointer. */
		assert(omni_data.data == block->data);
	}

	if (cache->meta_gen) {
		if (!sample->meta.data) {
			sample->meta.data = malloc(cache->def.msize);
		}

		if (cache->meta_gen(data, sample->meta.data)) {
			meta_set_status(sample, OMNI_STATUS_CURRENT);
		}
		else {
			meta_unset_status(sample, OMNI_STATUS_VALID);
			sample_unset_status(sample, OMNI_STATUS_VALID);

			return OMNI_WRITE_FAILED;
		}
	}

	sample_set_status(sample, OMNI_STATUS_CURRENT);

	return OMNI_WRITE_SUCCESS;
}

OmniReadResult OMNI_sample_read(OmniCache *cache, float_or_uint time, void *data)
{
	OmniSample *sample = NULL;
	OmniReadResult result = OMNI_READ_EXACT;

	if (!IS_VALID(cache)) {
		return OMNI_READ_INVALID;
	}

	if (!IS_CURRENT(cache)) {
		result |= OMNI_READ_OUTDATED;
	}

	sample = sample_get_from_time(cache, time, false, NULL, NULL);

	/* TODO: Interpolation. */
	if (!SAMPLE_IS_VALID(sample)) {
		return OMNI_READ_INVALID;
	}

	if (!IS_CURRENT(sample)) {
		result |= OMNI_READ_OUTDATED;
	}

	for (uint i = 0; i < cache->def.num_blocks; i++) {
		OmniBlockInfo *b_info = &cache->block_index[i];
		OmniBlock *block = &sample->blocks[i];
		OmniData omni_data;

		if (!IS_VALID(block)) {
			return OMNI_READ_INVALID;
		}

		omni_data.dtype = b_info->def.dtype;
		omni_data.dsize = b_info->def.dsize;
		omni_data.dcount = block->dcount;
		omni_data.data = block->data;

		if (!b_info->read(&omni_data, data)) {
			return OMNI_READ_INVALID;
		}

		if (!IS_CURRENT(block)) {
			result |= OMNI_READ_OUTDATED;
		}
	}

	return result;
}

void OMNI_set_range(OmniCache *cache, float_or_uint time_initial, float_or_uint time_final, float_or_uint time_step)
{
	bool changed = false;

	assert(FU_FL_GT(time_step, 0.0f));
	assert(TTYPE_FLOAT(cache->def.ttype) == time_initial.isf);
	assert(TTYPE_FLOAT(cache->def.ttype) == time_final.isf);
	assert(TTYPE_FLOAT(cache->def.ttype) == time_step.isf);
	assert(FU_LE(time_initial, time_final));

	if (!FU_EQ(time_initial, cache->def.tinitial)) {
		changed = true;

		cache->def.tinitial = time_initial;
	}

	if (!FU_EQ(time_final, cache->def.tfinal)) {
		changed = true;

		cache->def.tfinal = time_final;
	}

	if (!FU_EQ(time_step, cache->def.tstep)) {
		changed = true;

		cache->def.tstep = time_step;
	}

	/* TODO: Optionally clip/extend cache instead of freeing. */
	if (changed) {
		samples_free(cache);
	}
}

void OMNI_get_range(OmniCache *cache, float_or_uint *time_initial, float_or_uint *time_final, float_or_uint *time_step)
{
	if (time_initial) {
		*time_initial = cache->def.tinitial;
	}

	if (time_final) {
		*time_final = cache->def.tfinal;
	}

	if (time_step) {
		*time_initial = cache->def.tstep;
	}
}

uint OMNI_get_num_cached(OmniCache *cache)
{
	return cache->def.num_samples_tot;
}

bool OMNI_is_valid(OmniCache *cache)
{
	return IS_VALID(cache);
}

bool OMNI_is_current(OmniCache *cache)
{
	return IS_CURRENT(cache);
}

bool OMNI_sample_is_valid(OmniCache *cache, float_or_uint time)
{
	if (!IS_VALID(cache)) {
		return false;
	}

	return SAMPLE_IS_VALID(sample_get_from_time(cache, time, false, NULL, NULL));
}

bool OMNI_sample_is_current(OmniCache *cache, float_or_uint time)
{
	if (!IS_CURRENT(cache)) {
		return false;
	}

	return SAMPLE_IS_CURRENT(sample_get_from_time(cache, time, false, NULL, NULL));
}

/* TODO: Consolidation should set the num_samples_array as to ignore trailing skipped samples (without children).
 * (same applies to sample_clear_from and such) */
void OMNI_consolidate(OmniCache *cache, OmniConsolidationFlags flags)
{
	if ((!IS_VALID(cache) && (flags & (OMNI_CONSOL_FREE_INVALID | OMNI_CONSOL_FREE_OUTDATED))) ||
	    (!IS_CURRENT(cache) && (flags & OMNI_CONSOL_FREE_OUTDATED)))
	{
		samples_free(cache);
		return;
	}

	/* Frees outdated and invalid samples. */
	if (flags & OMNI_CONSOL_FREE_OUTDATED) {
		samples_iterate(cache->samples, sample_remove_outdated, NULL, NULL);
	}
	/* Frees invalid samples. */
	else if (flags & OMNI_CONSOL_FREE_INVALID) {
		samples_iterate(cache->samples, sample_remove_invalid, NULL, NULL);
	}

	if (flags & OMNI_CONSOL_CONSOLIDATE) {
		if (!IS_VALID(cache)) {
			samples_iterate(cache->samples, sample_mark_invalid, NULL, NULL);
		}
		else if (!IS_CURRENT(cache)) {
			samples_iterate(cache->samples, sample_mark_outdated, NULL, NULL);
		}

		cache_set_status(cache, OMNI_STATUS_CURRENT);
	}
}

void OMNI_mark_outdated(OmniCache *cache)
{
	cache_unset_status(cache, OMNI_STATUS_CURRENT);
}

void OMNI_mark_invalid(OmniCache *cache)
{
	cache_unset_status(cache, OMNI_STATUS_VALID);
}

void OMNI_clear(OmniCache *cache)
{
	samples_free(cache);
}

void OMNI_sample_mark_outdated(OmniCache *cache, float_or_uint time)
{
	OmniSample *sample = sample_get_from_time(cache, time, false, NULL, NULL);

	if (sample) {
		sample_mark_outdated(sample);
	}
}

void OMNI_sample_mark_invalid(OmniCache *cache, float_or_uint time)
{
	OmniSample *sample = sample_get_from_time(cache, time, false, NULL, NULL);

	if (sample) {
		sample_mark_invalid(sample);
	}
}

void OMNI_sample_clear(OmniCache *cache, float_or_uint time)
{
	OmniSample *sample = sample_get_from_time(cache, time, false, NULL, NULL);

	if (sample) {
		if (SAMPLE_IS_ROOT(sample)) {
			sample_remove_root(sample);
		}
		else {
			OmniSample *prev = sample_prev(sample);
			prev->next = sample->next;

			sample_remove_list(sample);
		}
	}
}

void OMNI_sample_mark_outdated_from(OmniCache *cache, float_or_uint time)
{
	OmniSample *next = NULL;
	OmniSample *sample = sample_get_from_time(cache, time, false, NULL, &next);

	sample = sample ? sample : next;

	if (sample) {
		samples_iterate(sample, sample_mark_outdated, sample_mark_outdated, NULL);
	}
}

void OMNI_sample_mark_invalid_from(OmniCache *cache, float_or_uint time)
{
	OmniSample *next = NULL;
	OmniSample *sample = sample_get_from_time(cache, time, false, NULL, &next);

	sample = sample ? sample : next;

	if (sample) {
		samples_iterate(sample, sample_mark_invalid, sample_mark_invalid, NULL);
	}
}

void OMNI_sample_clear_from(OmniCache *cache, float_or_uint time)
{
	OmniSample *next = NULL;
	OmniSample *sample = sample_get_from_time(cache, time, false, NULL, &next);

	sample = sample ? sample : next;

	if (sample) {
		samples_iterate(sample,
		                sample_remove_list,
		                sample_remove_root,
		                sample_clear_ref);
	}
}

#define INCREMENT_SERIAL(size) s = (OmniSerial *)(temp + size)

uint OMNI_serial_get_size(const OmniCache *cache, bool serialize_data)
{
	return serial_calc_size(cache, serialize_data);
}

/* TODO: Data serialization. */
OmniSerial *OMNI_serialize(const OmniCache *cache, bool serialize_data, uint *size)
{
	uint s = serial_calc_size(cache, serialize_data);
	OmniSerial *serial = malloc(s);

	if (size) {
		*size = s;
	}

	serialize(serial, cache, serialize_data);

	return serial;
}

void OMNI_serialize_to_buffer(OmniSerial *serial, const OmniCache *cache, bool serialize_data)
{
	serialize(serial, cache, serialize_data);
}

OmniCache *OMNI_deserialize(OmniSerial *serial, const OmniCacheTemplate *cache_temp)
{
	return deserialize(serial, cache_temp);
}

#undef INCREMENT_SERIAL
