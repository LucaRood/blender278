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
 * The Original Code is Copyright (C) 2006 by NaN Holding BV.
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): Luca Rood
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file DNA_omnicache_types.h
 *  \ingroup DNA
 */

#ifndef __DNA_OMNICACHE_TYPES_H__
#define __DNA_OMNICACHE_TYPES_H__

#include "DNA_defs.h"

typedef enum BOmniCacheType {
	bOmnicacheType_None		= 0,
	bOmnicacheType_Cloth	= 1,
	NUM_OMNICACHE_TYPES
} BOmniCacheType;

/* TODO (luca): Move to float time. */
typedef struct BOmniCache {
	int type;

	unsigned int time_start;
	unsigned int time_end;
	unsigned int time_step;

	int blocks_flag;

	int pad;

	struct OmniCache *omnicache;
} BOmniCache;

#endif
