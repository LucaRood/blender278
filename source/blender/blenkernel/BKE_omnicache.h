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
 * The Original Code is Copyright (C) Blender Foundation.
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): Luca Rood
 *
 * ***** END GPL LICENSE BLOCK *****
 */

#ifndef __BKE_OMNICACHE_H__
#define __BKE_OMNICACHE_H__

/** \file BKE_omnicache.h
 *  \ingroup bke
 *  \author Luca Rood
 */

#ifdef WITH_OMNICACHE

#include "DNA_omnicache_types.h"

struct Scene;

extern struct OmniCacheTemplate bOmnicacheTemplate_Cloth;

void BKE_omnicache_templatesInit(void);

BOmniCache *BKE_omnicache_new(BOmniCacheType type);
BOmniCache *BKE_omnicache_duplicate(BOmniCache *cache);
void BKE_omnicache_free(BOmniCache *cache);

void BKE_omnicache_blockAdd(BOmniCache *cache, unsigned int block);
void BKE_omnicache_blockRemove(BOmniCache *cache, unsigned int block);

void BKE_omnicache_reset(BOmniCache *cache, struct Scene *scene);
void BKE_omnicache_clear(BOmniCache *cache);

void BKE_omnicache_invalidate(BOmniCache *cache);
void BKE_omnicache_invalidateFromTime(BOmniCache *cache, unsigned int time);

bool BKE_omnicache_isCurrent(BOmniCache *cache);
bool BKE_omnicache_isValidAtTime(BOmniCache *cache, unsigned int time);

void BKE_omnicache_write(BOmniCache *cache, unsigned int time, void *data);
bool BKE_omnicache_read(BOmniCache *cache, unsigned int time, void *data);

void BKE_omnicache_getRange(BOmniCache *cache, unsigned int *start, unsigned int *end);

#endif /* WITH_OMNICACHE */
#endif /* __BKE_OMNICACHE_H__ */
