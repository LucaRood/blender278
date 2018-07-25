/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "omni_serial.h"

#include "omnicache.h"
#include "omni_utils.h"


#define INCREMENT_SERIAL() s = (OmniSerial *)(++temp)

uint serial_calc_size(const OmniCache *cache, bool UNUSED(serialize_data))
{
	return sizeof(OmniCacheDef) + (sizeof(OmniBlockInfoDef) * cache->def.num_blocks);
}

/* TODO: Data serialization. */
void serialize(OmniSerial *serial, const OmniCache *cache, bool UNUSED(serialize_data))
{
	OmniSerial *s = serial;

	/* cache */
	{
		OmniCacheDef *temp = (OmniCacheDef *)s;

		memcpy(temp, cache, sizeof(OmniCacheDef));

		/* TODO: Respect serialize_data. */
		temp->num_samples_array = 0;
		temp->num_samples_tot = 0;

		INCREMENT_SERIAL();
	}

	/* block_index */
	{
		OmniBlockInfoDef *temp = (OmniBlockInfoDef *)s;

		for (uint i = 0; i < cache->def.num_blocks; i++) {
			OmniBlockInfo *block = &cache->block_index[i];

			memcpy(temp, block, sizeof(OmniBlockInfoDef));

			INCREMENT_SERIAL();
		}
	}
}

OmniCache *deserialize(OmniSerial *serial, const OmniCacheTemplate *cache_temp)
{
	OmniSerial *s = (OmniSerial *)serial;
	OmniCache *cache;

	/* cache */
	{
		OmniCacheDef *temp = (OmniCacheDef *)s;

		if (cache_temp &&
		    strncmp(temp->id, cache_temp->id, MAX_NAME) != 0)
		{
			fprintf(stderr, "OmniCache: Deserialization falied, cache type mismatch.\n");

			return NULL;
		}

		cache = malloc(sizeof(OmniCache));

		memcpy(cache, temp, sizeof(OmniCacheDef));

		cache_set_status(cache, OMNI_STATUS_CURRENT);

		/* TODO: Data deserialization. */
		cache->num_samples_alloc = 0;

		cache->samples = NULL;

		if (cache_temp) {
			cache->meta_gen = cache_temp->meta_gen;
		}
		else {
			cache->meta_gen = NULL;
		}

		INCREMENT_SERIAL();
	}

	/* block_index */
	{
		OmniBlockInfoDef *temp = (OmniBlockInfoDef *)s;

		cache->block_index = malloc(sizeof(OmniBlockInfo) * cache->def.num_blocks);

		for (uint i = 0; i < cache->def.num_blocks; i++) {
			OmniBlockInfo *b_info = &cache->block_index[i];

			memcpy(b_info, temp, sizeof(OmniBlockInfoDef));

			b_info->parent = cache;

			if (cache_temp) {
				const OmniBlockTemplate *b_temp = &cache_temp->blocks[temp->index];

				b_info->count = b_temp->count;
				b_info->read = b_temp->read;
				b_info->write = b_temp->write;
				b_info->interp = b_temp->interp;
			}

			INCREMENT_SERIAL();
		}
	}

	return cache;
}
