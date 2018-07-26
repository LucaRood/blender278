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
 * The Original Code is Copyright (C) 2007 by Janne Karhu.
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): Luca Rood
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/editors/physics/physics_omnicache.c
 *  \ingroup edphys
 */

#include "DNA_object_types.h"

#include "BKE_animsys.h"
#include "BKE_context.h"
#include "BKE_nla.h"
#include "BKE_omnicache.h"

#include "WM_api.h"
#include "WM_types.h"

#include "RNA_access.h"

#include "physics_intern.h"

static bool omnicache_poll(bContext *C)
{
	PointerRNA ptr = CTX_data_pointer_get_type(C, "omnicache", &RNA_OmniCache);

	return (ptr.data && ptr.id.data);
}

static int omnicache_push_nla_exec(bContext *C, wmOperator *UNUSED(op))
{
#ifdef WITH_OMNICACHE
	PointerRNA ptr = CTX_data_pointer_get_type(C, "omnicache", &RNA_OmniCache);
	Object *ob = ptr.id.data;
	BOmniCache *cache = ptr.data;

	/* TODO (luca): Perhaps NLA tracks here shouldn't be in animdata.
	 * Evaluate if there is a better option, or if they should be kept here. */
	struct AnimData *adt = BKE_animdata_add_id(&ob->id);

	BKE_nla_omnicache_pushdown(adt, cache);

	WM_event_add_notifier(C, NC_ANIMATION | ND_NLA_ACTCHANGE, NULL);
	return OPERATOR_FINISHED;
#else
	UNUSED_VARS(C);
	return OPERATOR_CANCELLED;
#endif
}

void OMNICACHE_OT_push_nla(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Push to NLA";
	ot->description = "Push this OmniCache to the NLA";
	ot->idname = "OMNICACHE_OT_push_nla";

	/* api callbacks */
	ot->exec = omnicache_push_nla_exec;
	ot->poll = omnicache_poll;

	/* flags */
	ot->flag = OPTYPE_REGISTER|OPTYPE_UNDO;
}
