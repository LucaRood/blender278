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
 * Contributor(s):
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/modifiers/intern/MOD_openvdb.c
 *  \ingroup modifiers
 */

#include <stdio.h>

#include "DNA_scene_types.h"
#include "DNA_object_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"

#include "BLI_utildefines.h"
#include "BLI_string.h"
#include "BLI_path_util.h"
#include "BLI_math.h"

#include "BKE_DerivedMesh.h"
#include "BKE_scene.h"
#include "BKE_global.h"
#include "BKE_mesh.h"
#include "BKE_main.h"

#include "MEM_guardedalloc.h"

#include "MOD_modifiertypes.h"

static void copyData(ModifierData *md, ModifierData *target)
{
	modifier_copyData_generic(md, target);
}

static bool dependsOnTime(ModifierData *UNUSED(md))
{
	return true;
}

static bool isDisabled(ModifierData *md, int UNUSED(useRenderParams))
{
	OpenVDBModifierData *vdbmd = (OpenVDBModifierData *) md;

	/* leave it up to the modifier to check the file is valid on calculation */
	return vdbmd->filepath[0] == '\0';
}

static DerivedMesh *applyModifier(ModifierData *md, Object *ob,
                                  DerivedMesh *dm,
                                  ModifierApplyFlag flag)
{
	OpenVDBModifierData *vdbmd = (OpenVDBModifierData*) md;

	if (flag & MOD_APPLY_ORCO)
		return dm;

	return dm;
}


ModifierTypeInfo modifierType_OpenVDB = {
	/* name */              "OpenVDB",
	/* structName */        "OpenVDBModifierData",
	/* structSize */        sizeof(OpenVDBModifierData),
	/* type */              eModifierTypeType_Constructive,
    /* flags */             eModifierTypeFlag_AcceptsMesh |
	                        eModifierTypeFlag_UsesPointCache |
	                        eModifierTypeFlag_Single,

	/* copyData */          copyData,
	/* deformVerts */       NULL,
	/* deformMatrices */    NULL,
	/* deformVertsEM */     NULL,
	/* deformMatricesEM */  NULL,
	/* applyModifier */     applyModifier,
	/* applyModifierEM */   NULL,
	/* initData */          NULL,
	/* requiredDataMask */  NULL,
	/* freeData */          NULL,
	/* isDisabled */        isDisabled,
	/* updateDepgraph */    NULL,
	/* updateDepsgraph */   NULL,
	/* dependsOnTime */     dependsOnTime,
	/* dependsOnNormals */  NULL,
	/* foreachObjectLink */ NULL,
	/* foreachIDLink */     NULL,
	/* foreachTexLink */    NULL,
};