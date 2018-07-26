/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) Blender Foundation
 * All rights reserved.
 *
 * Contributor(s): Luca Rood
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/blenkernel/intern/omnicache.c
 *  \ingroup bke
 */

#ifdef WITH_OMNICACHE

#include "MEM_guardedalloc.h"

#include "BKE_omnicache.h"

#include "DNA_omnicache_types.h"
#include "DNA_scene_types.h"

#include "omnicache.h"

static OmniCacheTemplate *get_template_from_type(BOmniCacheType type)
{
	switch (type) {
		case bOmnicacheType_Cloth:
			return &bOmnicacheTemplate_Cloth;
		default:
			return NULL;
	}
}

static OmniCache *ensure_cache(BOmniCache *cache)
{
	OmniCacheTemplate *temp = get_template_from_type(cache->type);

	if (!cache->omnicache) {
		OmniCache *omnic = cache->omnicache = OMNI_new(temp, "");

		OMNI_set_range(omnic,
		               OMNI_u_to_fu(cache->time_start),
		               OMNI_u_to_fu(cache->time_end),
		               OMNI_u_to_fu(cache->time_step));

		for (unsigned int i = 0; i < temp->num_blocks; i++) {
			if (cache->blocks_flag & (1 << i)) {
				OMNI_block_add_by_index(omnic, temp, i);
			}
		}
	}

	return cache->omnicache;
}

void BKE_omnicache_templatesInit(void)
{
	for (unsigned int i = 1; i < NUM_OMNICACHE_TYPES; i++) {
		OmniCacheTemplate *temp = get_template_from_type(i);

		temp->time_type = OMNI_TIME_INT;
		temp->time_step = OMNI_u_to_fu(1);

		temp->flags |= OMNICACHE_FLAG_FRAMED;
	}
}

BOmniCache *BKE_omnicache_new(BOmniCacheType type)
{
	BOmniCache *cache = MEM_mallocN(sizeof(BOmniCache), "OmniCache");

	cache->type = type;

	cache->time_start = 1;
	cache->time_end = 250;
	cache->time_step = 1;
	cache->blocks_flag = 0;

	cache->omnicache = NULL;

	return cache;
}

BOmniCache *BKE_omnicache_duplicate(BOmniCache *cache)
{
	BOmniCache *new_cache = MEM_dupallocN(cache);

	new_cache->omnicache = NULL;

	return new_cache;
}

void BKE_omnicache_free(BOmniCache *cache)
{
	if (cache->omnicache) {
		OMNI_free(cache->omnicache);
	}

	MEM_freeN(cache);
}

void BKE_omnicache_blockAdd(BOmniCache *cache, unsigned int block)
{
	cache->blocks_flag |= (1 << block);

	if (cache->omnicache) {
		OmniCacheTemplate *temp = get_template_from_type(cache->type);

		OMNI_block_add_by_index(cache->omnicache, temp, block);
	}
}

void BKE_omnicache_blockRemove(BOmniCache *cache, unsigned int block)
{
	cache->blocks_flag &= ~(1 << block);

	if (cache->omnicache) {
		OMNI_block_remove_by_index(cache->omnicache, block);
	}
}

void BKE_omnicache_reset(BOmniCache *cache, Scene *scene)
{
	if (cache->omnicache) {
		OMNI_sample_clear_from(cache->omnicache, OMNI_u_to_fu(CFRA + 1));
		OMNI_mark_outdated(cache->omnicache);
	}
}

void BKE_omnicache_clear(BOmniCache *cache)
{
	if (cache->omnicache) {
		OMNI_clear(cache->omnicache);
	}
}

void BKE_omnicache_invalidate(BOmniCache *cache)
{
	if (cache->omnicache) {
		OMNI_mark_invalid(cache->omnicache);
	}
}

void BKE_omnicache_invalidateFromTime(BOmniCache *cache, unsigned int time)
{
	if (cache->omnicache) {
		OMNI_sample_mark_invalid_from(cache->omnicache, OMNI_u_to_fu(time));
	}
}

bool BKE_omnicache_isCurrent(BOmniCache *cache)
{
	return (cache->omnicache &&
	        OMNI_is_current(cache->omnicache) &&
	        OMNI_get_num_cached(cache->omnicache));
}

bool BKE_omnicache_isValidAtTime(BOmniCache *cache, unsigned int time)
{
	return (cache->omnicache &&
	        OMNI_sample_is_valid(cache->omnicache, OMNI_u_to_fu(time)));
}

/* TODO (luca): Don't ignore return value here. */
void BKE_omnicache_write(BOmniCache *cache, unsigned int time, void *data)
{
	OmniCache *omnic = ensure_cache(cache);

	OMNI_sample_write(omnic, OMNI_u_to_fu(time), data);
}

bool BKE_omnicache_read(BOmniCache *cache, unsigned int time, void *data)
{
	return (cache->omnicache &&
	        OMNI_sample_read(cache->omnicache, OMNI_u_to_fu(time), data) != OMNI_READ_INVALID);
}

void BKE_omnicache_setRange(BOmniCache *cache, unsigned int start, unsigned int end)
{
	cache->time_start = start;
	cache->time_end = end;

	if (cache->omnicache) {
		OMNI_set_range(cache->omnicache,
		               OMNI_u_to_fu(cache->time_start),
		               OMNI_u_to_fu(cache->time_end),
		               OMNI_u_to_fu(cache->time_step));
	}
}

void BKE_omnicache_getRange(BOmniCache *cache, unsigned int *start, unsigned int *end)
{
	*start = cache->time_start;
	*end = cache->time_end;
}

#endif /* WITH_OMNICACHE */