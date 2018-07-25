/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "omni_utils.h"

/* Flagging utils */

void block_set_status(OmniBlock *block, OmniBlockStatusFlags status)
{
	OmniSample *sample = block->parent;

	if (status & OMNI_STATUS_CURRENT) {
		status |= OMNI_STATUS_VALID;

		if (!(block->status & OMNI_STATUS_CURRENT)) {
			sample->num_blocks_outdated--;
		}
	}

	if (status & OMNI_STATUS_VALID) {
		if (!(block->status & OMNI_STATUS_VALID)) {
			sample->num_blocks_invalid--;
		}
	}

	block->status |= status;
}

void block_unset_status(OmniBlock *block, OmniBlockStatusFlags status)
{
	OmniSample *sample = block->parent;

	if (status & OMNI_STATUS_VALID) {
		status |= OMNI_STATUS_CURRENT;

		if (block->status & OMNI_STATUS_VALID) {
			sample->num_blocks_invalid++;
		}
	}

	if (status & OMNI_STATUS_CURRENT) {
		if (block->status & OMNI_STATUS_CURRENT) {
			sample->num_blocks_outdated++;
		}
	}

	block->status &= ~status;
}

void meta_set_status(OmniSample *sample, OmniBlockStatusFlags status)
{
	if (status & OMNI_STATUS_CURRENT) {
		status |= OMNI_STATUS_VALID;
	}

	sample->meta.status |= status;
}

void meta_unset_status(OmniSample *sample, OmniBlockStatusFlags status)
{
	if (status & OMNI_STATUS_VALID) {
		status |= OMNI_STATUS_CURRENT;
	}

	sample->meta.status &= ~status;
}

void sample_set_status(OmniSample *sample, OmniSampleStatusFlags status)
{
	if (status & OMNI_STATUS_CURRENT) {
		status |= OMNI_STATUS_VALID;
	}

	if (status & (OMNI_STATUS_VALID | OMNI_SAMPLE_STATUS_SKIP)) {
		status |= OMNI_STATUS_INITED;
	}

	sample->status |= status;
}

void sample_unset_status(OmniSample *sample, OmniSampleStatusFlags status)
{
	if (status & OMNI_STATUS_INITED) {
		status |= OMNI_STATUS_VALID;
	}

	if (status & OMNI_STATUS_VALID) {
		status |= OMNI_STATUS_CURRENT;
	}

	sample->status &= ~status;
}

void cache_set_status(OmniCache *cache, OmniCacheStatusFlags status)
{
	if (status & OMNI_STATUS_CURRENT) {
		status |= OMNI_STATUS_VALID;
	}

	if (status & OMNI_STATUS_VALID) {
		status |= OMNI_STATUS_INITED;
	}

	cache->status |= status;
}

void cache_unset_status(OmniCache *cache, OmniCacheStatusFlags status)
{
	if (status & OMNI_STATUS_INITED) {
		status |= OMNI_STATUS_VALID;
	}

	if (status & OMNI_STATUS_VALID) {
		status |= OMNI_STATUS_CURRENT;
	}

	cache->status &= ~status;
}

/* Sample utils */

sample_time gen_sample_time(OmniCache *cache, float_or_uint time)
{
	sample_time result = {0};

	assert(TTYPE_FLOAT(cache->def.ttype) == time.isf);

	if (FU_LT(time, cache->def.tinitial) || FU_GT(time, cache->def.tfinal)) {
		result.ttype = OMNI_TIME_INVALID;
		return result;
	}

	time = fu_sub(time, cache->def.tinitial);

	result.ttype = cache->def.ttype;
	result.index = fu_uint(fu_div(time, cache->def.tstep));
	result.offset = fu_mod(time, cache->def.tstep);

	return result;
}

/* Call a function for each sample in the cache, starting from an arbitrary sample.
 * start: sample at which to start iterating.
 * list: function called for all listed samples (non-root).
 * root: function called for all root samples.
 * first: function called for the `start` sample in addition to the `list` or `root` function. */
void samples_iterate(OmniSample *start, iter_callback list,
                     iter_callback root, iter_callback first)
{
	assert(list);

	if (start) {
		OmniCache *cache = start->parent;
		OmniSample *curr = start;
		OmniSample *next = curr->next;
		uint index = curr->tindex;

		if (first) first(curr);

		if (SAMPLE_IS_ROOT(curr)) {
			if (root) root(curr);
		}
		else {
			if (list) list(curr);
		}

		if (list) {
			for (curr = next; curr; curr = next) {
				next = curr->next;

				list(curr);
			}
		}

		for (uint i = index + 1; i < cache->def.num_samples_array; i++) {
			curr = &cache->samples[i];
			next = curr->next;

			if (root) root(curr);

			if (list) {
				for (curr = next; curr; curr = next) {
					next = curr->next;

					list(curr);
				}
			}
		}
	}
}

OmniSample *sample_prev(OmniSample *sample)
{
	OmniCache *cache = sample->parent;
	OmniSample *prev = &cache->samples[sample->tindex];

	while (prev->next != sample) {
		prev = prev->next;
	}

	return prev;
}

/* Find last sample at certain index */
OmniSample *sample_last(OmniSample *sample)
{
	while (sample->next != NULL) {
		sample = sample->next;
	}

	return sample;
}

void resize_sample_array(OmniCache *cache, uint size)
{
	cache->samples = realloc(cache->samples, sizeof(OmniSample) * size);

	if (size > cache->num_samples_alloc) {
		uint start = cache->num_samples_alloc;
		uint length = size - start;
		memset(&cache->samples[start], 0, sizeof(OmniSample) * length);
	}

	cache->num_samples_alloc = size;
}

void init_sample_blocks(OmniSample *sample)
{
	if (!sample->blocks) {
		OmniCache *cache = sample->parent;

		sample->blocks = calloc(cache->def.num_blocks, sizeof(OmniBlock));
		sample->num_blocks_invalid = cache->def.num_blocks;
		sample->num_blocks_outdated = cache->def.num_blocks;

		for (uint i = 0; i < cache->def.num_blocks; i++) {
			OmniBlock *block = &sample->blocks[i];

			block->parent = sample;

			block_set_status(block, OMNI_STATUS_INITED);
		}
	}
}

void block_info_init(OmniCache *cache, const OmniCacheTemplate *cache_temp,
                     const uint target_index, const uint source_index)
{
	const OmniBlockTemplate *b_temp = &cache_temp->blocks[source_index];
	OmniBlockInfo *b_info = &cache->block_index[target_index];

	strncpy(b_info->def.id, b_temp->id, MAX_NAME);
	b_info->def.index = source_index;

	b_info->def.dtype = b_temp->data_type;
	b_info->def.flags = b_temp->flags;

	b_info->def.dsize = DATA_SIZE(b_temp->data_type, b_temp->data_size);

	b_info->parent = cache;

	assert(b_temp->count);
	assert(b_temp->read);
	assert(b_temp->write);

	b_info->count = b_temp->count;
	b_info->read = b_temp->read;
	b_info->write = b_temp->write;
	b_info->interp = b_temp->interp;
}

void block_info_array_init(OmniCache *cache, const OmniCacheTemplate *cache_temp, bool *mask)
{
	cache->block_index = malloc(sizeof(OmniBlockInfo) * cache->def.num_blocks);

	for (uint i = 0, j = 0; i < cache_temp->num_blocks; i++) {
		if (mask[i]) {
			block_info_init(cache, cache_temp, j++, i);
		}
	}
}

void update_block_parents(OmniCache *cache)
{
	for (uint i = 0; i < cache->def.num_samples_array; i++) {
		OmniSample *samp = &cache->samples[i];

		do {
			if (samp->blocks) {
				for (uint j = 0; j < cache->def.num_blocks; j++) {
					samp->blocks[j].parent = samp;
				}
			}

			samp = samp->next;
		} while (samp);
	}
}

static bool strcmp_delim(const char *str, const char *sub, char delim, uint *index)
{
	str += *index;

	while (*str == *sub &&
	       *str != '\0' &&
	       *str != delim &&
	       *sub != '\0')
	{
		str++;
		sub++;
		(*index)++;
	}

	{
		uint i = 0;

		while (str[i] != '\0' &&
		       str[i] != delim)
		{
			i++;
		}

		if (str[i] == '\0') {
			*index += i;
		}
		else {
			*index += i + 1;
		}
	}

	if (*sub == '\0' && (*str == '\0' || *str == delim)) {
		return true;
	}

	return false;
}

bool block_id_in_str(const char id_str[], const char id[])
{
	uint index = 0;

	while (id_str[index] != '\0') {
		if (strcmp_delim(id_str, id, ';', &index)) {
			return true;
		}
	}

	return false;
}

bool *block_id_mask(const OmniCacheTemplate *cache_temp, const char id_str[], uint *num_blocks)
{
	bool *m = malloc(sizeof(bool) * cache_temp->num_blocks);
	uint count = 0;

	for (uint i = 0; i < cache_temp->num_blocks; i++) {
		if (cache_temp->blocks[i].flags & OMNI_BLOCK_FLAG_MANDATORY) {
			m[i] = true;
		}
		else {
			m[i] = block_id_in_str(id_str, cache_temp->blocks[i].id);
		}

		if (m[i]) {
			count++;
		}
	}

	if (num_blocks) {
		*num_blocks = count;
	}

	return m;
}
