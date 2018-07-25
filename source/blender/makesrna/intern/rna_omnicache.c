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
 * Contributor(s): Luca Rood
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/makesrna/intern/rna_omnicache.c
 *  \ingroup RNA
 */

#include "DNA_scene_types.h"

#include "RNA_define.h"
#include "rna_internal.h"

#ifdef RNA_RUNTIME

#include "DNA_object_types.h"
#include "DNA_omnicache_types.h"

#include "BKE_context.h"
#include "BKE_depsgraph.h"
#include "BKE_omnicache.h"

#include "WM_api.h"
#include "WM_types.h"

static void rna_omnicache_update(Main *UNUSED(bmain), Scene *UNUSED(scene), PointerRNA *ptr)
{
	Object *ob = (Object *)ptr->id.data;

	DAG_id_tag_update(&ob->id, OB_RECALC_DATA);
	WM_main_add_notifier(NC_OBJECT | ND_MODIFIER, ob);
}

static void rna_OmniCache_time_start_set(struct PointerRNA *ptr, int value)
{
	BOmniCache *cache = (BOmniCache *)ptr->data;

#ifdef WITH_OMNICACHE
	BKE_omnicache_setRange(cache, (unsigned int)value, cache->time_end);
#else
	cache->time_start = value;
#endif
}

static void rna_OmniCache_time_end_set(struct PointerRNA *ptr, float value)
{
	BOmniCache *cache = (BOmniCache *)ptr->data;

#ifdef WITH_OMNICACHE
	BKE_omnicache_setRange(cache, cache->time_start, (unsigned int)value);
#else
	cache->time_end = value;
#endif
}

#else

void RNA_def_omnicache(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	srna = RNA_def_struct(brna, "OmniCache", NULL);
	RNA_def_struct_sdna(srna, "BOmniCache");
	RNA_def_struct_ui_text(srna, "OmniCache", "OmniCache settings");
	RNA_def_struct_ui_icon(srna, ICON_PHYSICS);

	prop = RNA_def_property(srna, "time_start", PROP_INT, PROP_TIME);
	RNA_def_property_range(prop, 1, MAXFRAME);
	RNA_def_property_int_funcs(prop, NULL, "rna_OmniCache_time_start_set", NULL);
	RNA_def_property_ui_text(prop, "Start", "Time at which the simulation starts");
	RNA_def_property_update(prop, 0, "rna_omnicache_update");

	prop = RNA_def_property(srna, "time_end", PROP_INT, PROP_TIME);
	RNA_def_property_range(prop, 1, MAXFRAME);
	RNA_def_property_int_funcs(prop, NULL, "rna_OmniCache_time_end_set", NULL);
	RNA_def_property_ui_text(prop, "End", "Time at which the simulation stops");
	RNA_def_property_update(prop, 0, "rna_omnicache_update");
}

#endif
