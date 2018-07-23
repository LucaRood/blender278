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
 * Contributor(s): Daniel Genrich
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/blenkernel/intern/cloth.c
 *  \ingroup bke
 */


#include "MEM_guardedalloc.h"

#include "DNA_cloth_types.h"
#include "DNA_scene_types.h"
#include "DNA_object_types.h"
#include "DNA_meshdata_types.h"

#include "BLI_alloca.h"
#include "BLI_utildefines.h"
#include "BLI_math.h"
#include "BLI_edgehash.h"
#include "BLI_linklist.h"

#include "BKE_cdderivedmesh.h"
#include "BKE_cloth.h"
#include "BKE_editmesh.h"
#include "BKE_deform.h"
#include "BKE_effect.h"
#include "BKE_global.h"
#include "BKE_modifier.h"

#ifndef WITH_OMNICACHE
#  include "BKE_pointcache.h"
#endif

#include "BPH_mass_spring.h"

#ifdef WITH_OMNICACHE
#  include "omnicache.h"
#endif

// #include "PIL_time.h"  /* timing for debug prints */

/* ********** cloth engine ******* */
/* Prototypes for internal functions.
 */
static void cloth_to_object (Object *ob,  ClothModifierData *clmd, float (*vertexCos)[3]);
static void cloth_from_mesh ( ClothModifierData *clmd, DerivedMesh *dm );
static int cloth_from_object(Object *ob, ClothModifierData *clmd, DerivedMesh *dm, float framenr, int first);
static void cloth_update_springs( ClothModifierData *clmd );
static void cloth_update_verts( Object *ob, ClothModifierData *clmd, DerivedMesh *dm );
static void cloth_update_spring_lengths( ClothModifierData *clmd, DerivedMesh *dm );
static int cloth_build_springs ( ClothModifierData *clmd, DerivedMesh *dm );
static void cloth_apply_vgroup ( ClothModifierData *clmd, DerivedMesh *dm, Object *ob );

typedef struct BendSpringRef {
	int index, polys;
	ClothSpring *spring;
} BendSpringRef;

/********** OmniCache **********/
#ifdef WITH_OMNICACHE
/* Just for convenience. */
typedef float float3[3];

/* OmniCache callbacks */

static uint cache_count_vert(void *data)
{
	ClothModifierData *clmd = (ClothModifierData *)data;

	return clmd->clothObject->mvert_num;
}

static uint cache_count_spring(void *data)
{
	ClothModifierData *clmd = (ClothModifierData *)data;

	return clmd->clothObject->numsprings;
}

#define CACHE_RW_VERT(target, source) {                  \
	ClothModifierData *clmd = (ClothModifierData *)data; \
	Cloth *cloth = clmd->clothObject;                    \
	float3 *array = (float3 *)omni_data->data;           \
	if (omni_data->dcount != cloth->mvert_num) {         \
		return false;                                    \
	}                                                    \
	for (uint i = 0; i < cloth->mvert_num; i++) {        \
		ClothVertex *vert = &cloth->verts[i];            \
		memcpy(target, source, sizeof(float3));          \
		array++;                                         \
	}                                                    \
	return true;                                         \
}

#define CACHE_READ_VERT(prop) CACHE_RW_VERT(vert->prop, array)
#define CACHE_WRITE_VERT(prop) CACHE_RW_VERT(array, vert->prop)

static bool cache_read_x(OmniData *omni_data, void *data)
{
	CACHE_READ_VERT(x);
}

static bool cache_read_v(OmniData *omni_data, void *data)
{
	CACHE_READ_VERT(v);
}

static bool cache_read_xconst(OmniData *omni_data, void *data)
{
	CACHE_READ_VERT(xconst);
}

static bool cache_write_x(OmniData *omni_data, void *data)
{
	CACHE_WRITE_VERT(x);
}

static bool cache_write_v(OmniData *omni_data, void *data)
{
	CACHE_WRITE_VERT(v);
}

static bool cache_write_xconst(OmniData *omni_data, void *data)
{
	CACHE_WRITE_VERT(xconst);
}

#define CACHE_RW_SPRING(target, source) {                                    \
	ClothModifierData *clmd = (ClothModifierData *)data;                     \
	Cloth *cloth = clmd->clothObject;                                        \
	float *array = (float *)omni_data->data;                                 \
	if (omni_data->dcount != cloth->numsprings) {                            \
		return false;                                                        \
	}                                                                        \
	for (LinkNode *search = cloth->springs; search; search = search->next) { \
		ClothSpring *spring = search->link;                                  \
		target = source;                                                     \
		array++;                                                             \
	}                                                                        \
	return true;                                                             \
}

#define CACHE_READ_SPRING(prop) CACHE_RW_SPRING(spring->prop, *array)
#define CACHE_WRITE_SPRING(prop) CACHE_RW_SPRING(*array, spring->prop)

static bool cache_read_lenfact(OmniData *omni_data, void *data)
{
	CACHE_READ_SPRING(lenfact);
}

static bool cache_read_angoffset(OmniData *omni_data, void *data)
{
	CACHE_READ_SPRING(angoffset);
}

static bool cache_write_lenfact(OmniData *omni_data, void *data)
{
	CACHE_WRITE_SPRING(lenfact);
}

static bool cache_write_angoffset(OmniData *omni_data, void *data)
{
	CACHE_WRITE_SPRING(angoffset);
}

/* OmniCache templates */

static const OmniCacheTemplate cache_template = {
    .id = "blender_cloth",
    .time_type = OMNI_TIME_INT,
    .time_initial = OMNI_U_TO_FU(1),
    .time_final = OMNI_U_TO_FU(250),
    .time_step = OMNI_U_TO_FU(1),
    .flags = OMNICACHE_FLAG_FRAMED | OMNICACHE_FLAG_INTERP_SUB,
    .num_blocks = 5,
    .blocks = {
        {
            .id = "x",
            .data_type = OMNI_DATA_FLOAT3,
            .flags = OMNI_BLOCK_FLAG_CONTINUOUS | OMNI_BLOCK_FLAG_CONST_COUNT,
            .count = cache_count_vert,
            .read = cache_read_x,
            .write = cache_write_x,
        },
        {
            .id = "v",
            .data_type = OMNI_DATA_FLOAT3,
            .flags = OMNI_BLOCK_FLAG_CONTINUOUS | OMNI_BLOCK_FLAG_CONST_COUNT,
            .count = cache_count_vert,
            .read = cache_read_v,
            .write = cache_write_v,
        },
        {
            .id = "xconst",
            .data_type = OMNI_DATA_FLOAT3,
            .flags = OMNI_BLOCK_FLAG_CONTINUOUS | OMNI_BLOCK_FLAG_CONST_COUNT,
            .count = cache_count_vert,
            .read = cache_read_xconst,
            .write = cache_write_xconst,
        },
        {
            .id = "lenfact",
            .data_type = OMNI_DATA_FLOAT,
            .flags = OMNI_BLOCK_FLAG_CONTINUOUS | OMNI_BLOCK_FLAG_CONST_COUNT,
            .count = cache_count_spring,
            .read = cache_read_lenfact,
            .write = cache_write_lenfact,
        },
        {
            .id = "angoffset",
            .data_type = OMNI_DATA_FLOAT,
            .flags = OMNI_BLOCK_FLAG_CONTINUOUS | OMNI_BLOCK_FLAG_CONST_COUNT,
            .count = cache_count_spring,
            .read = cache_read_angoffset,
            .write = cache_write_angoffset,
        },
    },
};
#endif

/******************************************************************************
 *
 * External interface called by modifier.c clothModifier functions.
 *
 ******************************************************************************/
/**
 * cloth_init - creates a new cloth simulation.
 *
 * 1. create object
 * 2. fill object with standard values or with the GUI settings if given
 */
void cloth_init(ClothModifierData *clmd )
{	
	/* Initialize our new data structure to reasonable values. */
	clmd->sim_parms->gravity[0] = 0.0;
	clmd->sim_parms->gravity[1] = 0.0;
	clmd->sim_parms->gravity[2] = -9.81;
	clmd->sim_parms->tension = 50.0;
	clmd->sim_parms->compression = 50.0;
	clmd->sim_parms->max_tension = 50.0;
	clmd->sim_parms->max_compression = 50.0;
	clmd->sim_parms->shear = 0.1;
	clmd->sim_parms->max_shear = 0.1;
	clmd->sim_parms->bending = 0.05;
	clmd->sim_parms->max_bend = 0.05;
	clmd->sim_parms->bending_damping = 0.5;
	clmd->sim_parms->tension_damp = 5.0;
	clmd->sim_parms->compression_damp = 5.0;
	clmd->sim_parms->shear_damp = 1.0;
	clmd->sim_parms->Cvi = 1.0;
	clmd->sim_parms->mass = 0.3f;
	clmd->sim_parms->stepsPerFrame = 5;
	clmd->sim_parms->flags = 0;
	clmd->sim_parms->solver_type = 0;
	clmd->sim_parms->maxspringlen = 10;
	clmd->sim_parms->vgroup_mass = 0;
	clmd->sim_parms->vgroup_shrink = 0;
	clmd->sim_parms->shrink = 0.0f; /* min amount the fabric will shrink by 0.0 = no shrinking, 1.0 = shrink to nothing*/
	clmd->sim_parms->max_shrink = 0.0f;
	clmd->sim_parms->avg_spring_len = 0.0;
	clmd->sim_parms->presets = 2; /* cotton as start setting */
	clmd->sim_parms->timescale = 1.0f; /* speed factor, describes how fast cloth moves */
	clmd->sim_parms->time_scale = 1.0f; /* multiplies cloth speed */
	clmd->sim_parms->reset = 0;
	clmd->sim_parms->vel_damping = 1.0f; /* 1.0 = no damping, 0.0 = fully dampened */
	clmd->sim_parms->struct_plasticity = 1.0f;
	clmd->sim_parms->struct_yield_fact = 1.5f;
	clmd->sim_parms->bend_plasticity = 1.0f;
	clmd->sim_parms->bend_yield_fact = DEG2RAD(10.0);

	/* Adaptive subframes */
	clmd->sim_parms->max_subframes = 50;
	clmd->sim_parms->max_vel = 0.04f;
	clmd->sim_parms->adjustment_factor = 0.8f;
	clmd->sim_parms->max_imp = 0.04f;
	clmd->sim_parms->imp_adj_factor = 0.8f;
	
	clmd->coll_parms->self_friction = 5.0;
	clmd->coll_parms->friction = 5.0;
	clmd->coll_parms->loop_count = 2;
	clmd->coll_parms->epsilon = 0.015f;
	clmd->coll_parms->flags = CLOTH_COLLSETTINGS_FLAG_ENABLED;
	clmd->coll_parms->collision_list = NULL;
	clmd->coll_parms->selfepsilon = 0.015;
	clmd->coll_parms->vgroup_selfcol = 0;
	clmd->coll_parms->objcol_resp_iter = 2;
	clmd->coll_parms->selfcol_resp_iter = 3;

	/* These defaults are copied from softbody.c's
	 * softbody_calc_forces() function.
	 */
	clmd->sim_parms->eff_force_scale = 1000.0;
	clmd->sim_parms->eff_wind_scale = 250.0;

	// also from softbodies
	clmd->sim_parms->maxgoal = 1.0f;
	clmd->sim_parms->mingoal = 0.0f;
	clmd->sim_parms->defgoal = 0.0f;
	clmd->sim_parms->goalspring = 1.0f;
	clmd->sim_parms->goalfrict = 0.0f;
	clmd->sim_parms->velocity_smooth = 0.0f;

	clmd->sim_parms->voxel_cell_size = 0.1f;

	if (!clmd->sim_parms->effector_weights)
		clmd->sim_parms->effector_weights = BKE_add_effector_weights(NULL);

#ifdef WITH_OMNICACHE
	clmd->cache = OMNI_new(&cache_template, "x;v;xconst;");

	cloth_serialize_omnicache(clmd);
#else
	if (clmd->point_cache)
		clmd->point_cache->step = 1;
#endif
}

static BVHTree *bvhtree_build_from_cloth (ClothModifierData *clmd, float epsilon)
{
	unsigned int i;
	BVHTree *bvhtree;
	Cloth *cloth;
	ClothVertex *verts;
	const MVertTri *vt;

	if (!clmd)
		return NULL;

	cloth = clmd->clothObject;

	if (!cloth)
		return NULL;
	
	verts = cloth->verts;
	vt = cloth->tri;
	
	/* in the moment, return zero if no faces there */
	if (!cloth->tri_num)
		return NULL;

	/* create quadtree with k=26 */
	bvhtree = BLI_bvhtree_new(cloth->tri_num, epsilon, 4, 26);

	/* fill tree */
	for (i = 0; i < cloth->tri_num; i++, vt++) {
		float co[3][3];

		copy_v3_v3(co[0], verts[vt->tri[0]].xold);
		copy_v3_v3(co[1], verts[vt->tri[1]].xold);
		copy_v3_v3(co[2], verts[vt->tri[2]].xold);

		BLI_bvhtree_insert(bvhtree, i, co[0], 3);
	}

	/* balance tree */
	BLI_bvhtree_balance(bvhtree);
	
	return bvhtree;
}

void bvhtree_update_from_cloth(ClothModifierData *clmd, bool moving, bool self)
{	
	unsigned int i = 0;
	Cloth *cloth = clmd->clothObject;
	BVHTree *bvhtree;
	ClothVertex *verts = cloth->verts;
	const MVertTri *vt;

	if (self) {
		bvhtree = cloth->bvhselftree;
	}
	else {
		bvhtree = cloth->bvhtree;
	}

	if (!bvhtree)
		return;
	
	vt = cloth->tri;
	
	/* update vertex position in bvh tree */
	if (verts && vt) {
		for (i = 0; i < cloth->tri_num; i++, vt++) {
			float co[3][3], co_moving[3][3];
			bool ret;

			/* copy new locations into array */
			if (moving) {
				copy_v3_v3(co[0], verts[vt->tri[0]].txold);
				copy_v3_v3(co[1], verts[vt->tri[1]].txold);
				copy_v3_v3(co[2], verts[vt->tri[2]].txold);

				/* update moving positions */
				copy_v3_v3(co_moving[0], verts[vt->tri[0]].tx);
				copy_v3_v3(co_moving[1], verts[vt->tri[1]].tx);
				copy_v3_v3(co_moving[2], verts[vt->tri[2]].tx);

				ret = BLI_bvhtree_update_node(bvhtree, i, co[0], co_moving[0], 3);
			}
			else {
				copy_v3_v3(co[0], verts[vt->tri[0]].tx);
				copy_v3_v3(co[1], verts[vt->tri[1]].tx);
				copy_v3_v3(co[2], verts[vt->tri[2]].tx);

				ret = BLI_bvhtree_update_node(bvhtree, i, co[0], NULL, 3);
			}
			
			/* check if tree is already full */
			if (ret == false) {
				break;
			}
		}
		
		BLI_bvhtree_update_tree(bvhtree);
	}
}

#ifndef WITH_OMNICACHE
void cloth_clear_cache(Object *ob, ClothModifierData *clmd, float framenr)
{
	PTCacheID pid;
	
	BKE_ptcache_id_from_cloth(&pid, ob, clmd);

	// don't do anything as long as we're in editmode!
	if (pid.cache->edit && ob->mode & OB_MODE_PARTICLE_EDIT)
		return;
	
	BKE_ptcache_id_clear(&pid, PTCACHE_CLEAR_AFTER, framenr);
}
#endif

static int do_init_cloth(Object *ob, ClothModifierData *clmd, DerivedMesh *result, int framenr)
{
#ifdef WITH_OMNICACHE
	OmniCache *cache = clmd->cache;
#else
	PointCache *cache = clmd->point_cache;
#endif

	/* initialize simulation data if it didn't exist already */
	if (clmd->clothObject == NULL) {
		if (!cloth_from_object(ob, clmd, result, framenr, 1)) {
#ifdef WITH_OMNICACHE
			OMNI_clear(cache);
#else
			BKE_ptcache_invalidate(cache);
#endif
			modifier_setError(&(clmd->modifier), "Can't initialize cloth");
			return 0;
		}
	
		if (clmd->clothObject == NULL) {
#ifdef WITH_OMNICACHE
			OMNI_clear(cache);
#else
			BKE_ptcache_invalidate(cache);
#endif
			modifier_setError(&(clmd->modifier), "Null cloth object");
			return 0;
		}
	
		BKE_cloth_solver_set_positions(clmd);

		clmd->clothObject->last_frame= MINFRAME-1;
		clmd->clothObject->adapt_fact = 1.0f;
		clmd->sim_parms->dt = 1.0f / clmd->sim_parms->stepsPerFrame;
	}

	return 1;
}

static int do_step_cloth(Object *ob, ClothModifierData *clmd, DerivedMesh *result, int framenr)
{
	ClothVertex *verts = NULL;
	Cloth *cloth;
	ListBase *effectors = NULL;
	MVert *mvert;
	unsigned int i = 0;
	int ret = 0;

	/* simulate 1 frame forward */
	cloth = clmd->clothObject;
	verts = cloth->verts;
	mvert = result->getVertArray(result);

	/* force any pinned verts to their constrained location. */
	for (i = 0; i < clmd->clothObject->mvert_num; i++, verts++) {
		/* save the previous position. */
		copy_v3_v3(verts->xold, verts->xconst);
		copy_v3_v3(verts->txold, verts->x);

		/* Get the current position. */
		copy_v3_v3(verts->xconst, mvert[i].co);
		mul_m4_v3(ob->obmat, verts->xconst);
	}

	effectors = pdInitEffectors(clmd->scene, ob, NULL, clmd->sim_parms->effector_weights, true);

	if (clmd->sim_parms->flags & CLOTH_SIMSETTINGS_FLAG_DYNAMIC_BASEMESH )
		cloth_update_verts ( ob, clmd, result );

	/* Support for dynamic vertex groups, changing from frame to frame */
	cloth_apply_vgroup(clmd, result, ob);

	if ((clmd->sim_parms->flags & CLOTH_SIMSETTINGS_FLAG_DYNAMIC_BASEMESH) ||
	    (clmd->sim_parms->vgroup_shrink > 0) || (clmd->sim_parms->shrink > 0.0f))
	{
		cloth_update_spring_lengths ( clmd, result );
	}

	cloth_update_springs( clmd );
	
	// TIMEIT_START(cloth_step)

	/* call the solver. */
	ret = BPH_cloth_solve(ob, framenr, clmd, effectors, result);

	// TIMEIT_END(cloth_step)

	pdEndEffectors(&effectors);

	// printf ( "%f\n", ( float ) tval() );
	
	return ret;
}

/************************************************
 * clothModifier_do - main simulation function
 ************************************************/
void clothModifier_do(ClothModifierData *clmd, Scene *scene, Object *ob, DerivedMesh *dm, float (*vertexCos)[3])
{
#ifdef WITH_OMNICACHE
	OmniCache *cache = clmd->cache;
	float_or_uint omni_framenr;
#else
	PointCache *cache = clmd->point_cache;;
	PTCacheID pid;
#endif

	float timescale;
	int framenr, startframe, endframe;
	int cache_result;

	clmd->scene= scene;	/* nice to pass on later :) */
	framenr= (int)scene->r.cfra;

#ifdef WITH_OMNICACHE
	if (!cache) {
		cache = clmd->cache = OMNI_deserialize(clmd->cache_serial, &cache_template);
		OMNI_mark_outdated(clmd->cache);
	}

	omni_framenr = OMNI_u_to_fu(framenr);

	{
		float_or_uint start, end;

		OMNI_get_range(cache, &start, &end, NULL);

		startframe = OMNI_FU_GET(start);
		endframe = OMNI_FU_GET(end);

		timescale = scene->r.framelen;
	}
#else
	BKE_ptcache_id_from_cloth(&pid, ob, clmd);
	BKE_ptcache_id_time(&pid, scene, framenr, &startframe, &endframe, &timescale);
#endif

	clmd->sim_parms->timescale= timescale * clmd->sim_parms->time_scale;

	if (clmd->sim_parms->reset || (clmd->clothObject && dm->getNumVerts(dm) != clmd->clothObject->mvert_num)) {
		clmd->sim_parms->reset = 0;

#ifdef WITH_OMNICACHE
		OMNI_clear(cache);
#else
		cache->flag |= PTCACHE_OUTDATED;
		BKE_ptcache_id_reset(scene, &pid, PTCACHE_RESET_OUTDATED);
		BKE_ptcache_validate(cache, 0);
		cache->last_exact= 0;
		cache->flag &= ~PTCACHE_REDO_NEEDED;
#endif
	}

	/* simulation is only active during a specific period */
	if (framenr < startframe) {
		return;
	}
	else if (framenr > endframe) {
		framenr= endframe;
	}

	/* initialize simulation data if it didn't exist already */
	if (!do_init_cloth(ob, clmd, dm, framenr))
		return;

#ifdef WITH_OMNICACHE
	if (framenr == startframe && !OMNI_is_current(cache)) {
		OMNI_clear(cache);
		cloth_free_modifier(clmd);
		do_init_cloth(ob, clmd, dm, framenr);
		clmd->clothObject->last_frame= framenr;
		OMNI_sample_write(cache, omni_framenr, clmd);
		return;
	}
#else
	if (framenr == startframe && ((cache->flag & PTCACHE_OUTDATED) || (cache->last_exact < startframe))) {
		BKE_ptcache_id_reset(scene, &pid, PTCACHE_RESET_OUTDATED);
		do_init_cloth(ob, clmd, dm, framenr);
		BKE_ptcache_validate(cache, framenr);
		cache->flag &= ~PTCACHE_REDO_NEEDED;
		clmd->clothObject->last_frame= framenr;
		BKE_ptcache_write(&pid, startframe);
		return;
	}
#endif

	/* try to read from cache */
#ifdef WITH_OMNICACHE
	bool can_simulate = (framenr == clmd->clothObject->last_frame + 1);

	/* TODO (luca): Should respect subframe here, and interpolate between frames. */
	cache_result = OMNI_sample_read(cache, omni_framenr, clmd);

	BKE_cloth_solver_set_positions(clmd);

	if (!(cache_result & OMNI_READ_INVALID))
	{
		cloth_to_object(ob, clmd, vertexCos);

		/* TODO (luca): Might want to write interpolated result to cache... Or not. */

		clmd->clothObject->last_frame = framenr;

		return;
	}
#else
	bool can_simulate = (framenr == clmd->clothObject->last_frame + 1) &&
	                    (framenr == cache->last_exact + 1) &&
	                    !(cache->flag & PTCACHE_BAKED);

	cache_result = BKE_ptcache_read(&pid, (float)framenr+scene->r.subframe, can_simulate);

	if (cache_result == PTCACHE_READ_EXACT || cache_result == PTCACHE_READ_INTERPOLATED ||
	    (!can_simulate && cache_result == PTCACHE_READ_OLD))
	{
		BKE_cloth_solver_set_positions(clmd);
		cloth_to_object (ob, clmd, vertexCos);

		BKE_ptcache_validate(cache, framenr);

		if (cache_result == PTCACHE_READ_INTERPOLATED && cache->flag & PTCACHE_REDO_NEEDED)
			BKE_ptcache_write(&pid, framenr);

		clmd->clothObject->last_frame= framenr;

		return;
	}
	else if (cache_result==PTCACHE_READ_OLD) {
		BKE_cloth_solver_set_positions(clmd);
	}
	else if ( /*ob->id.lib ||*/ (cache->flag & PTCACHE_BAKED)) { /* 2.4x disabled lib, but this can be used in some cases, testing further - campbell */
		/* if baked and nothing in cache, do nothing */
		BKE_ptcache_invalidate(cache);
		return;
	}
#endif

	if (!can_simulate)
		return;

	/* TODO (luca): get last frame number (not strictly necessary,
	 * since we checked that the time is always one frame ahead of the last frame anyway) */
#ifndef WITH_OMNICACHE
	clmd->sim_parms->timescale *= framenr - cache->simframe;
#endif

	/* do simulation */
#ifdef WITH_OMNICACHE
	if (!do_step_cloth(ob, clmd, dm, framenr)) {
		OMNI_sample_mark_invalid_from(cache, omni_framenr);
	}
	else {
		OMNI_sample_write(cache, omni_framenr, clmd);
	}
#else
	BKE_ptcache_validate(cache, framenr);

	if (!do_step_cloth(ob, clmd, dm, framenr)) {
		BKE_ptcache_invalidate(cache);
	}
	else
		BKE_ptcache_write(&pid, framenr);
#endif

	cloth_to_object (ob, clmd, vertexCos);
	clmd->clothObject->last_frame= framenr;
}

/* frees all */
void cloth_free_modifier(ClothModifierData *clmd )
{
	Cloth	*cloth = NULL;

	if ( !clmd )
		return;

	cloth = clmd->clothObject;


	if ( cloth ) {
		BPH_cloth_solver_free(clmd);

		// Free the verts.
		if ( cloth->verts != NULL )
			MEM_freeN ( cloth->verts );

		cloth->verts = NULL;
		cloth->mvert_num = 0;

		// Free the springs.
		if ( cloth->springs != NULL ) {
			LinkNode *search = cloth->springs;
			while (search) {
				ClothSpring *spring = search->link;

				if (spring->pa != NULL) {
					MEM_freeN(spring->pa);
				}
				if (spring->pb != NULL) {
					MEM_freeN(spring->pb);
				}

				MEM_freeN ( spring );
				search = search->next;
			}
			BLI_linklist_free(cloth->springs, NULL);

			cloth->springs = NULL;
		}

		cloth->springs = NULL;
		cloth->numsprings = 0;

		// free BVH collision tree
		if ( cloth->bvhtree )
			BLI_bvhtree_free ( cloth->bvhtree );

		if ( cloth->bvhselftree )
			BLI_bvhtree_free ( cloth->bvhselftree );

		// we save our faces for collision objects
		if (cloth->tri)
			MEM_freeN(cloth->tri);

		/*
		if (clmd->clothObject->facemarks)
		MEM_freeN(clmd->clothObject->facemarks);
		*/
		MEM_freeN ( cloth );
		clmd->clothObject = NULL;
	}
}

/* frees all */
void cloth_free_modifier_extern(ClothModifierData *clmd )
{
	Cloth	*cloth = NULL;
	if (G.debug_value > 0)
		printf("cloth_free_modifier_extern\n");

	if ( !clmd )
		return;

	cloth = clmd->clothObject;

	if ( cloth ) {
		if (G.debug_value > 0)
			printf("cloth_free_modifier_extern in\n");

		BPH_cloth_solver_free(clmd);

		// Free the verts.
		if ( cloth->verts != NULL )
			MEM_freeN ( cloth->verts );

		cloth->verts = NULL;
		cloth->mvert_num = 0;

		// Free the springs.
		if ( cloth->springs != NULL ) {
			LinkNode *search = cloth->springs;
			while (search) {
				ClothSpring *spring = search->link;

				if (spring->type & CLOTH_SPRING_TYPE_BENDING) {
					MEM_freeN(spring->pa);
					MEM_freeN(spring->pb);
				}

				MEM_freeN ( spring );
				search = search->next;
			}
			BLI_linklist_free(cloth->springs, NULL);

			cloth->springs = NULL;
		}

		cloth->springs = NULL;
		cloth->numsprings = 0;

		// free BVH collision tree
		if ( cloth->bvhtree )
			BLI_bvhtree_free ( cloth->bvhtree );

		if ( cloth->bvhselftree )
			BLI_bvhtree_free ( cloth->bvhselftree );

		// we save our faces for collision objects
		if (cloth->tri)
			MEM_freeN(cloth->tri);

		/*
		if (clmd->clothObject->facemarks)
		MEM_freeN(clmd->clothObject->facemarks);
		*/
		MEM_freeN ( cloth );
		clmd->clothObject = NULL;
	}
}

bool is_basemesh_valid(Object *ob, Object *basemesh, ClothModifierData *clmd)
{
	DerivedMesh *basedm;
	ModifierData *md;

	if (!basemesh || (ob == basemesh)) {
		return true;
	}
	else if (basemesh->type != OB_MESH) {
		return false;
	}

	if (clmd) {
		md = (ModifierData *)clmd;
	}
	else {
		md = modifiers_findByType(ob, eModifierType_Cloth);
		clmd = (ClothModifierData *)md;
	}

	basedm = mesh_get_derived_final(md->scene, basemesh, 0);

	return clmd->clothObject->mvert_num == basedm->getNumVerts(basedm);
}

#ifdef WITH_OMNICACHE
void cloth_serialize_omnicache(ClothModifierData *clmd)
{
	clmd->cache_serial_size = OMNI_serial_get_size(clmd->cache, false);
	clmd->cache_serial = MEM_mallocN(clmd->cache_serial_size, "OmniCache serial");
	OMNI_serialize_to_buffer(clmd->cache_serial, clmd->cache, false);
}

void cloth_update_omnicache_blocks(ClothModifierData *clmd) {
	if ((clmd->sim_parms->flags & CLOTH_SIMSETTINGS_FLAG_STRUCT_PLASTICITY) &&
	    (clmd->sim_parms->flags & CLOTH_SIMSETTINGS_FLAG_BEND_PLASTICITY))
	{
		OMNI_blocks_set(clmd->cache, &cache_template, "x;v;xconst;lenfact;angoffset;");
	}
	else if ((clmd->sim_parms->flags & CLOTH_SIMSETTINGS_FLAG_STRUCT_PLASTICITY)) {
		OMNI_blocks_set(clmd->cache, &cache_template, "x;v;xconst;lenfact;");
	}
	else if ((clmd->sim_parms->flags & CLOTH_SIMSETTINGS_FLAG_BEND_PLASTICITY)) {
		OMNI_blocks_set(clmd->cache, &cache_template, "x;v;xconst;angoffset;");
	}
	else {
		OMNI_blocks_set(clmd->cache, &cache_template, "x;v;xconst;");
	}
}
#endif

/******************************************************************************
 *
 * Internal functions.
 *
 ******************************************************************************/

/**
 * cloth_to_object - copies the deformed vertices to the object.
 *
 **/
static void cloth_to_object (Object *ob,  ClothModifierData *clmd, float (*vertexCos)[3])
{
	unsigned int i = 0;
	Cloth *cloth = clmd->clothObject;

	if (clmd->clothObject) {
		/* inverse matrix is not uptodate... */
		invert_m4_m4(ob->imat, ob->obmat);

		for (i = 0; i < cloth->mvert_num; i++) {
			copy_v3_v3 (vertexCos[i], cloth->verts[i].x);
			mul_m4_v3(ob->imat, vertexCos[i]);	/* cloth is in global coords */
		}
	}
}


int cloth_uses_vgroup(ClothModifierData *clmd)
{
	return (((clmd->coll_parms->flags & CLOTH_COLLSETTINGS_FLAG_SELF) && (clmd->coll_parms->vgroup_selfcol>0)) ||
		(clmd->sim_parms->vgroup_struct>0)||
		(clmd->sim_parms->vgroup_bend>0) ||
		(clmd->sim_parms->vgroup_shrink>0) ||
		(clmd->sim_parms->vgroup_mass>0) ||
		(clmd->sim_parms->vgroup_planar>0) ||
		(clmd->sim_parms->flags & CLOTH_SIMSETTINGS_FLAG_COMB_GOAL));
}

/**
 * cloth_apply_vgroup - applies a vertex group as specified by type
 *
 **/
/* can be optimized to do all groups in one loop */
static void cloth_apply_vgroup(ClothModifierData *clmd, DerivedMesh *dm, Object *ob)
{
	int i = 0;
	int j = 0;
	MDeformVert *dvert = NULL;
	Cloth *clothObj = NULL;
	int mvert_num;
	/* float goalfac = 0; */ /* UNUSED */
	ClothVertex *verts = NULL;

	if (!clmd || !dm) return;

	clothObj = clmd->clothObject;

	mvert_num = dm->getNumVerts(dm);

	verts = clothObj->verts;

	if (cloth_uses_vgroup(clmd)) {
		for (i = 0; i < mvert_num; i++, verts++) {

			/* Reset Goal values to standard */
			if ( clmd->sim_parms->vgroup_mass>0 )
				verts->goal= clmd->sim_parms->defgoal;
			else
				verts->goal= 0.0f;

			/* Compute base cloth shrink weight */
			verts->shrink_factor = 0.0f;

			/* Reset vertex flags */
			verts->flags &= ~CLOTH_VERT_FLAG_PINNED;
			verts->flags &= ~CLOTH_VERT_FLAG_NOSELFCOLL;

			dvert = dm->getVertData ( dm, i, CD_MDEFORMVERT );

			if ( dvert ) {
				if (clmd->sim_parms->flags & CLOTH_SIMSETTINGS_FLAG_COMB_GOAL)
				{
					verts->goal = BKE_defvert_combined_weight(ob, dvert, DVERT_COMBINED_MODE_ADD);

					verts->goal  = pow4f(verts->goal);
					if ( verts->goal >= SOFTGOALSNAP )
						verts->flags |= CLOTH_VERT_FLAG_PINNED;
				}

				for ( j = 0; j < dvert->totweight; j++ ) {
					if ((dvert->dw[j].def_nr == (clmd->sim_parms->vgroup_mass-1)) &&
					    !(clmd->sim_parms->flags & CLOTH_SIMSETTINGS_FLAG_COMB_GOAL))
					{
						verts->goal = dvert->dw [j].weight;

						/* goalfac= 1.0f; */ /* UNUSED */

						// Kicking goal factor to simplify things...who uses that anyway?
						// ABS ( clmd->sim_parms->maxgoal - clmd->sim_parms->mingoal );

						verts->goal  = pow4f(verts->goal);
						if ( verts->goal >= SOFTGOALSNAP )
							verts->flags |= CLOTH_VERT_FLAG_PINNED;
					}

					if ( dvert->dw[j].def_nr == (clmd->sim_parms->vgroup_struct-1)) {
						verts->struct_stiff = dvert->dw [j].weight;
					}

					if ( dvert->dw[j].def_nr == (clmd->sim_parms->vgroup_shear-1)) {
						verts->shear_stiff = dvert->dw [j].weight;
					}

					if ( dvert->dw[j].def_nr == (clmd->sim_parms->vgroup_bend-1)) {
						verts->bend_stiff = dvert->dw [j].weight;
					}

					if ( dvert->dw[j].def_nr == (clmd->sim_parms->vgroup_planar - 1)) {
						verts->planarity = dvert->dw[j].weight;
					}

					if (clmd->coll_parms->flags & CLOTH_COLLSETTINGS_FLAG_SELF ) {
						if ( dvert->dw[j].def_nr == (clmd->coll_parms->vgroup_selfcol-1)) {
							if (dvert->dw [j].weight > 0.0f) {
								verts->flags |= CLOTH_VERT_FLAG_NOSELFCOLL;
							}
						}
					}
					if (clmd->sim_parms->vgroup_shrink > 0) {
						if (dvert->dw[j].def_nr == (clmd->sim_parms->vgroup_shrink - 1)) {
							/* used for linear interpolation between min and max shrink factor based on weight */
							verts->shrink_factor = dvert->dw[j].weight;
						}
					}
				}
			}
		}
	}
}

static float cloth_shrink_factor(ClothModifierData *clmd, ClothVertex *verts, int i1, int i2)
{
	/* linear interpolation between min and max shrink factor based on weight */
	float base = 1.0f - clmd->sim_parms->shrink;
	float delta = clmd->sim_parms->shrink - clmd->sim_parms->max_shrink;

	float k1 = base + delta * verts[i1].shrink_factor;
	float k2 = base + delta * verts[i2].shrink_factor;

	/* Use geometrical mean to average two factors since it behaves better
	   for diagonals when a rectangle transforms into a trapezoid. */
	return sqrtf(k1 * k2);
}

static int cloth_from_object(Object *ob, ClothModifierData *clmd, DerivedMesh *dm, float UNUSED(framenr), int first)
{
	int i = 0;
	MVert *mvert = NULL;
	MVert *basevert = NULL;
	ClothVertex *verts = NULL;
	float (*shapekey_rest)[3] = NULL;
	float tnull[3] = {0, 0, 0};

	// If we have a clothObject, free it.
	if ( clmd->clothObject != NULL ) {
		cloth_free_modifier ( clmd );
		if (G.debug_value > 0)
			printf("cloth_free_modifier cloth_from_object\n");
	}

	// Allocate a new cloth object.
	clmd->clothObject = MEM_callocN ( sizeof ( Cloth ), "cloth" );
	if ( clmd->clothObject ) {
		clmd->clothObject->old_solver_type = 255;
		// clmd->clothObject->old_collision_type = 255;
	}
	else if (!clmd->clothObject) {
		modifier_setError(&(clmd->modifier), "Out of memory on allocating clmd->clothObject");
		return 0;
	}

	// mesh input objects need DerivedMesh
	if ( !dm )
		return 0;

	cloth_from_mesh ( clmd, dm );

	// create springs
	clmd->clothObject->springs = NULL;
	clmd->clothObject->numsprings = -1;

	if (clmd->sim_parms->basemesh_target) {
		if ((clmd->sim_parms->basemesh_target != ob) && is_basemesh_valid(ob, clmd->sim_parms->basemesh_target, clmd)) {
			ModifierData *md = (ModifierData *)clmd;
			DerivedMesh *basedm;

			if (clmd->sim_parms->basemesh_target == md->scene->obedit) {
				BMEditMesh *em = BKE_editmesh_from_object(clmd->sim_parms->basemesh_target);
				basedm = em->derivedFinal;
			}
			else {
				basedm = clmd->sim_parms->basemesh_target->derivedFinal;
			}

			basevert = basedm->getVertArray(basedm);
		}
	}
	else if (clmd->sim_parms->shapekey_rest && !(clmd->sim_parms->flags & CLOTH_SIMSETTINGS_FLAG_DYNAMIC_BASEMESH)) {
		shapekey_rest = dm->getVertDataArray ( dm, CD_CLOTH_ORCO );
	}

	mvert = dm->getVertArray(dm);

	verts = clmd->clothObject->verts;

	// set initial values
	for ( i = 0; i < dm->getNumVerts(dm); i++, verts++ ) {
		if (clmd->sim_parms->vgroup_trouble > 0) {
			MDeformVert *dvert = (MDeformVert *)dm->getVertData(dm, i, CD_MDEFORMVERT);
			MDeformWeight *weight = defvert_verify_index(dvert, (clmd->sim_parms->vgroup_trouble - 1));
			weight->weight = 0.0f;
		}

		if (first) {
			copy_v3_v3(verts->x, mvert[i].co);

			mul_m4_v3(ob->obmat, verts->x);

			if (basevert) {
				copy_v3_v3(verts->xrest, basevert[i].co);
				mul_m4_v3(clmd->sim_parms->basemesh_target->obmat, verts->xrest);
			}
			else if (shapekey_rest) {
				copy_v3_v3(verts->xrest, shapekey_rest[i]);
				mul_m4_v3(ob->obmat, verts->xrest);
			}
			else {
				copy_v3_v3(verts->xrest, verts->x);
			}
		}

		/* no GUI interface yet */
		verts->mass = clmd->sim_parms->mass;
		verts->impulse_count = 0;

		if ( clmd->sim_parms->vgroup_mass>0 )
			verts->goal= clmd->sim_parms->defgoal;
		else
			verts->goal= 0.0f;

		verts->shrink_factor = 0.0f;

		verts->flags = 0;
		copy_v3_v3 ( verts->xold, verts->x );
		copy_v3_v3 ( verts->xconst, verts->x );
		copy_v3_v3 ( verts->txold, verts->x );
		copy_v3_v3 ( verts->tx, verts->x );
		mul_v3_fl(verts->v, 0.0f);

		verts->impulse_count = 0;
		copy_v3_v3 ( verts->impulse, tnull );

		verts->col_trouble = 0.0f;
	}

	// apply / set vertex groups
	// has to be happen before springs are build!
	cloth_apply_vgroup(clmd, dm, ob);

	if ( !cloth_build_springs ( clmd, dm ) ) {
		cloth_free_modifier ( clmd );
		modifier_setError(&(clmd->modifier), "Cannot build springs");
		printf("cloth_free_modifier cloth_build_springs\n");
		return 0;
	}

	// init our solver
	BPH_cloth_solver_init(ob, clmd);

	if (!first)
		BKE_cloth_solver_set_positions(clmd);

	clmd->clothObject->bvhtree = bvhtree_build_from_cloth ( clmd, clmd->coll_parms->epsilon );

	clmd->clothObject->bvhselftree = bvhtree_build_from_cloth ( clmd, clmd->coll_parms->selfepsilon );

	return 1;
}

static void cloth_from_mesh ( ClothModifierData *clmd, DerivedMesh *dm )
{
	const MLoop *mloop = dm->getLoopArray(dm);
	const MLoopTri *looptri = dm->getLoopTriArray(dm);
	const unsigned int mvert_num = dm->getNumVerts(dm);
	const unsigned int looptri_num = dm->getNumLoopTri(dm);

	/* Allocate our vertices. */
	clmd->clothObject->mvert_num = mvert_num;
	clmd->clothObject->verts = MEM_callocN(sizeof(ClothVertex) * clmd->clothObject->mvert_num, "clothVertex");
	if (clmd->clothObject->verts == NULL) {
		cloth_free_modifier(clmd);
		modifier_setError(&(clmd->modifier), "Out of memory on allocating clmd->clothObject->verts");
		printf("cloth_free_modifier clmd->clothObject->verts\n");
		return;
	}

	/* save face information */
	clmd->clothObject->tri_num = looptri_num;
	clmd->clothObject->tri = MEM_mallocN(sizeof(MVertTri) * looptri_num, "clothLoopTris");
	if (clmd->clothObject->tri == NULL) {
		cloth_free_modifier(clmd);
		modifier_setError(&(clmd->modifier), "Out of memory on allocating clmd->clothObject->looptri");
		printf("cloth_free_modifier clmd->clothObject->looptri\n");
		return;
	}
	DM_verttri_from_looptri(clmd->clothObject->tri, mloop, looptri, looptri_num);

	/* Free the springs since they can't be correct if the vertices
	 * changed.
	 */
	if ( clmd->clothObject->springs != NULL )
		MEM_freeN ( clmd->clothObject->springs );

}

/***************************************************************************************
 * SPRING NETWORK BUILDING IMPLEMENTATION BEGIN
 ***************************************************************************************/

BLI_INLINE void spring_verts_ordered_set(ClothSpring *spring, int v0, int v1)
{
	if (v0 < v1) {
		spring->ij = v0;
		spring->kl = v1;
	}
	else {
		spring->ij = v1;
		spring->kl = v0;
	}
}

// be careful: implicit solver has to be resettet when using this one!
// --> only for implicit handling of this spring!
#if 0 // Unused for now, but might come in handy when implementing something with dynamic spring count
static int cloth_add_spring(ClothModifierData *clmd, unsigned int indexA, unsigned int indexB, float restlength, int spring_type)
{
	Cloth *cloth = clmd->clothObject;
	ClothSpring *spring = NULL;

	if (cloth && spring_type != CLOTH_SPRING_TYPE_BENDING) {
		// TODO: look if this spring is already there

		spring = (ClothSpring *)MEM_callocN ( sizeof ( ClothSpring ), "cloth spring" );

		if (!spring)
			return 0;

		spring->ij = indexA;
		spring->kl = indexB;
		spring->restlen =  restlength;
		spring->type = spring_type;
		spring->flags = 0;
		spring->lin_stiffness = 0.0f;

		cloth->numsprings++;

		BLI_linklist_prepend ( &cloth->springs, spring );

		return 1;
	}
	return 0;
}
#endif

static void cloth_free_edgelist(LinkNodePair *edgelist, unsigned int mvert_num)
{
	if (edgelist) {
		unsigned int i;
		for (i = 0; i < mvert_num; i++) {
			BLI_linklist_free(edgelist[i].list, NULL);
		}

		MEM_freeN(edgelist);
	}
}

static void cloth_free_errorsprings(Cloth *cloth, LinkNodePair *edgelist)
{
	if ( cloth->springs != NULL ) {
		LinkNode *search = cloth->springs;
		while (search) {
			ClothSpring *spring = search->link;

			if (spring->type & CLOTH_SPRING_TYPE_BENDING) {
				MEM_freeN(spring->pa);
				MEM_freeN(spring->pb);
			}

			MEM_freeN ( spring );
			search = search->next;
		}
		BLI_linklist_free(cloth->springs, NULL);

		cloth->springs = NULL;
	}

	cloth_free_edgelist(edgelist, cloth->mvert_num);
}

BLI_INLINE float spring_angle(ClothVertex *verts, int i, int j, int *i_a, int *i_b, int len_a, int len_b)
{
	float co_i[3], co_j[3], co_a[3], co_b[3];
	float dir_a[3], dir_b[3];
	float tmp1[3], tmp2[3], vec_e[3];
	float sin, cos;
	float fact_a = 1.0f / len_a;
	float fact_b = 1.0f / len_b;
	int x;

	zero_v3(co_a);
	zero_v3(co_b);

	/* assign poly vert coords to arrays */
	for (x = 0; x < len_a; x++) {
		madd_v3_v3fl(co_a, verts[i_a[x]].xrest, fact_a);
	}

	for (x = 0; x < len_b; x++) {
		madd_v3_v3fl(co_b, verts[i_b[x]].xrest, fact_b);
	}

	/* get edge vert coords and poly centroid coords. */
	copy_v3_v3(co_i, verts[i].xrest);
	copy_v3_v3(co_j, verts[j].xrest);

	/* find dir for poly a */
	sub_v3_v3v3(tmp1, co_j, co_a);
	sub_v3_v3v3(tmp2, co_i, co_a);

	cross_v3_v3v3(dir_a, tmp1, tmp2);
	normalize_v3(dir_a);

	/* find dir for poly b */
	sub_v3_v3v3(tmp1, co_i, co_b);
	sub_v3_v3v3(tmp2, co_j, co_b);

	cross_v3_v3v3(dir_b, tmp1, tmp2);
	normalize_v3(dir_b);

	/* find parallel and perpendicular directions to edge */
	sub_v3_v3v3(vec_e, co_i, co_j);
	normalize_v3(vec_e);

	/* calculate angle between polys */
	cos = dot_v3v3(dir_a, dir_b);

	cross_v3_v3v3(tmp1, dir_a, dir_b);
	sin = dot_v3v3(tmp1, vec_e);

	return atan2(sin, cos);
}

static void cloth_hair_update_bending_targets(ClothModifierData *clmd)
{
	Cloth *cloth = clmd->clothObject;
	LinkNode *search = NULL;
	float hair_frame[3][3], dir_old[3], dir_new[3];
	int prev_mn; /* to find hair chains */

	if (!clmd->hairdata)
		return;

	/* XXX Note: we need to propagate frames from the root up,
	 * but structural hair springs are stored in reverse order.
	 * The bending springs however are then inserted in the same
	 * order as vertices again ...
	 * This messy situation can be resolved when solver data is
	 * generated directly from a dedicated hair system.
	 */

	prev_mn = -1;
	for (search = cloth->springs; search; search = search->next) {
		ClothSpring *spring = search->link;
		ClothHairData *hair_ij, *hair_kl;
		bool is_root = spring->kl != prev_mn;

		if (spring->type != CLOTH_SPRING_TYPE_BENDING_HAIR) {
			continue;
		}

		hair_ij = &clmd->hairdata[spring->ij];
		hair_kl = &clmd->hairdata[spring->kl];
		if (is_root) {
			/* initial hair frame from root orientation */
			copy_m3_m3(hair_frame, hair_ij->rot);
			/* surface normal is the initial direction,
			 * parallel transport then keeps it aligned to the hair direction
			 */
			copy_v3_v3(dir_new, hair_frame[2]);
		}

		copy_v3_v3(dir_old, dir_new);
		sub_v3_v3v3(dir_new, cloth->verts[spring->mn].x, cloth->verts[spring->kl].x);
		normalize_v3(dir_new);

#if 0
		if (clmd->debug_data && (spring->ij == 0 || spring->ij == 1)) {
			float a[3], b[3];

			copy_v3_v3(a, cloth->verts[spring->kl].x);
//			BKE_sim_debug_data_add_dot(clmd->debug_data, cloth_vert ? cloth_vert->x : key->co, 1, 1, 0, "frames", 8246, p, k);

			mul_v3_v3fl(b, hair_frame[0], clmd->sim_parms->avg_spring_len);
			BKE_sim_debug_data_add_vector(clmd->debug_data, a, b, 1, 0, 0, "frames", 8247, spring->kl, spring->mn);

			mul_v3_v3fl(b, hair_frame[1], clmd->sim_parms->avg_spring_len);
			BKE_sim_debug_data_add_vector(clmd->debug_data, a, b, 0, 1, 0, "frames", 8248, spring->kl, spring->mn);

			mul_v3_v3fl(b, hair_frame[2], clmd->sim_parms->avg_spring_len);
			BKE_sim_debug_data_add_vector(clmd->debug_data, a, b, 0, 0, 1, "frames", 8249, spring->kl, spring->mn);
		}
#endif

		/* get local targets for kl/mn vertices by putting rest targets into the current frame,
		 * then multiply with the rest length to get the actual goals
		 */

		mul_v3_m3v3(spring->target, hair_frame, hair_kl->rest_target);
		mul_v3_fl(spring->target, spring->restlen);

		/* move frame to next hair segment */
		cloth_parallel_transport_hair_frame(hair_frame, dir_old, dir_new);

		prev_mn = spring->mn;
	}
}

static void cloth_hair_update_bending_rest_targets(ClothModifierData *clmd)
{
	Cloth *cloth = clmd->clothObject;
	LinkNode *search = NULL;
	float hair_frame[3][3], dir_old[3], dir_new[3];
	int prev_mn; /* to find hair roots */

	if (!clmd->hairdata)
		return;

	/* XXX Note: we need to propagate frames from the root up,
	 * but structural hair springs are stored in reverse order.
	 * The bending springs however are then inserted in the same
	 * order as vertices again ...
	 * This messy situation can be resolved when solver data is
	 * generated directly from a dedicated hair system.
	 */

	prev_mn = -1;
	for (search = cloth->springs; search; search = search->next) {
		ClothSpring *spring = search->link;
		ClothHairData *hair_ij, *hair_kl;
		bool is_root = spring->kl != prev_mn;

		if (spring->type != CLOTH_SPRING_TYPE_BENDING_HAIR) {
			continue;
		}

		hair_ij = &clmd->hairdata[spring->ij];
		hair_kl = &clmd->hairdata[spring->kl];
		if (is_root) {
			/* initial hair frame from root orientation */
			copy_m3_m3(hair_frame, hair_ij->rot);
			/* surface normal is the initial direction,
			 * parallel transport then keeps it aligned to the hair direction
			 */
			copy_v3_v3(dir_new, hair_frame[2]);
		}

		copy_v3_v3(dir_old, dir_new);
		sub_v3_v3v3(dir_new, cloth->verts[spring->mn].xrest, cloth->verts[spring->kl].xrest);
		normalize_v3(dir_new);

		/* dir expressed in the hair frame defines the rest target direction */
		copy_v3_v3(hair_kl->rest_target, dir_new);
		mul_transposed_m3_v3(hair_frame, hair_kl->rest_target);

		/* move frame to next hair segment */
		cloth_parallel_transport_hair_frame(hair_frame, dir_old, dir_new);

		prev_mn = spring->mn;
	}
}

/* update stiffness if vertex group values are changing from frame to frame */
static void cloth_update_springs( ClothModifierData *clmd )
{
	Cloth *cloth = clmd->clothObject;
	LinkNode *search = NULL;

	search = cloth->springs;
	while (search) {
		ClothSpring *spring = search->link;

		spring->lin_stiffness = 0.0f;

		if (spring->type & CLOTH_SPRING_TYPE_BENDING) {
			spring->ang_stiffness = (cloth->verts[spring->kl].bend_stiff + cloth->verts[spring->ij].bend_stiff) / 2.0f;
			spring->planarity = (cloth->verts[spring->kl].planarity + cloth->verts[spring->ij].planarity) / 2.0f;
		}

		if (spring->type & CLOTH_SPRING_TYPE_STRUCTURAL) {
			spring->lin_stiffness = (cloth->verts[spring->kl].struct_stiff + cloth->verts[spring->ij].struct_stiff) / 2.0f;
		}
		else if (spring->type & CLOTH_SPRING_TYPE_SHEAR) {
			spring->lin_stiffness = (cloth->verts[spring->kl].shear_stiff + cloth->verts[spring->ij].shear_stiff) / 2.0f;
		}
		else if (spring->type == CLOTH_SPRING_TYPE_BENDING_HAIR) {
			ClothVertex *v1 = &cloth->verts[spring->ij];
			ClothVertex *v2 = &cloth->verts[spring->kl];
			if (clmd->hairdata) {
				/* copy extra hair data to generic cloth vertices */
				v1->bend_stiff = clmd->hairdata[spring->ij].bending_stiffness;
				v2->bend_stiff = clmd->hairdata[spring->kl].bending_stiffness;
			}
			spring->lin_stiffness = (v1->bend_stiff + v2->bend_stiff) / 2.0f;
		}
		else if (spring->type == CLOTH_SPRING_TYPE_GOAL) {
			/* Warning: Appending NEW goal springs does not work because implicit solver would need reset! */

			/* Activate / Deactivate existing springs */
			if ((!(cloth->verts[spring->ij].flags & CLOTH_VERT_FLAG_PINNED)) &&
			    (cloth->verts[spring->ij].goal > ALMOST_ZERO))
			{
				spring->flags &= ~CLOTH_SPRING_FLAG_DEACTIVATE;
			}
			else {
				spring->flags |= CLOTH_SPRING_FLAG_DEACTIVATE;
			}
		}

		search = search->next;
	}

	cloth_hair_update_bending_targets(clmd);
}

/* Update rest verts, for dynamically deformable cloth */
static void cloth_update_verts(Object *ob, ClothModifierData *clmd, DerivedMesh *dm)
{
	unsigned int i = 0;
	MVert *mvert;
	ClothVertex *verts = clmd->clothObject->verts;

	if (clmd->sim_parms->basemesh_target && (clmd->sim_parms->basemesh_target != ob) &&
	    is_basemesh_valid(ob, clmd->sim_parms->basemesh_target, clmd))
	{
		ModifierData *md = (ModifierData *)clmd;

		ob = clmd->sim_parms->basemesh_target;

		if (ob == md->scene->obedit) {
			BMEditMesh *em = BKE_editmesh_from_object(ob);
			dm = em->derivedFinal;
		}
		else {
			dm = ob->derivedFinal;
		}
	}

	mvert = dm->getVertArray(dm);

	/* vertex count is already ensured to match */
	for ( i = 0; i < dm->getNumVerts(dm); i++, verts++ ) {
		copy_v3_v3(verts->xrest, mvert[i].co);
		mul_m4_v3(ob->obmat, verts->xrest);
	}
}

/* Update spring rest lenght, for dynamically deformable cloth */
static void cloth_update_spring_lengths( ClothModifierData *clmd, DerivedMesh *dm )
{
	Cloth *cloth = clmd->clothObject;
	LinkNode *search = cloth->springs;
	unsigned int struct_springs = 0;
	unsigned int i = 0;
	unsigned int mvert_num = (unsigned int)dm->getNumVerts(dm);
	float shrink_factor;

	clmd->sim_parms->avg_spring_len = 0.0f;

	for (i = 0; i < mvert_num; i++) {
		cloth->verts[i].avg_spring_len = 0.0f;
	}

	while (search) {
		ClothSpring *spring = search->link;

		if ( spring->type != CLOTH_SPRING_TYPE_SEWING ) {
			if ( spring->type & (CLOTH_SPRING_TYPE_STRUCTURAL | CLOTH_SPRING_TYPE_SHEAR | CLOTH_SPRING_TYPE_BENDING) ) {
				shrink_factor = cloth_shrink_factor(clmd, cloth->verts, spring->ij, spring->kl);
			}
			else {
				shrink_factor = 1.0f;
			}

			spring->restlen = len_v3v3(cloth->verts[spring->kl].xrest, cloth->verts[spring->ij].xrest) * shrink_factor;

			if ( spring->type & CLOTH_SPRING_TYPE_BENDING ) {
				spring->restang = spring_angle(cloth->verts, spring->ij, spring->kl, spring->pa, spring->pb, spring->la, spring->lb);
			}
		}

		if ( spring->type & CLOTH_SPRING_TYPE_STRUCTURAL ) {
			clmd->sim_parms->avg_spring_len += spring->restlen;
			cloth->verts[spring->ij].avg_spring_len += spring->restlen;
			cloth->verts[spring->kl].avg_spring_len += spring->restlen;
			struct_springs++;
		}

		search = search->next;
	}

	if (struct_springs > 0) {
		clmd->sim_parms->avg_spring_len /= struct_springs;
	}

	for (i = 0; i < mvert_num; i++) {
		if (cloth->verts[i].spring_count > 0) {
			cloth->verts[i].avg_spring_len = cloth->verts[i].avg_spring_len * 0.49f / ((float)cloth->verts[i].spring_count);
		}
	}
}

BLI_INLINE void cross_identity_v3(float r[3][3], const float v[3])
{
	zero_m3(r);
	r[0][1] = v[2];
	r[0][2] = -v[1];
	r[1][0] = -v[2];
	r[1][2] = v[0];
	r[2][0] = v[1];
	r[2][1] = -v[0];
}

BLI_INLINE void madd_m3_m3fl(float r[3][3], float m[3][3], float f)
{
	r[0][0] += m[0][0] * f;
	r[0][1] += m[0][1] * f;
	r[0][2] += m[0][2] * f;
	r[1][0] += m[1][0] * f;
	r[1][1] += m[1][1] * f;
	r[1][2] += m[1][2] * f;
	r[2][0] += m[2][0] * f;
	r[2][1] += m[2][1] * f;
	r[2][2] += m[2][2] * f;
}

void cloth_parallel_transport_hair_frame(float mat[3][3], const float dir_old[3], const float dir_new[3])
{
	float rot[3][3];

	/* rotation between segments */
	rotation_between_vecs_to_mat3(rot, dir_old, dir_new);

	/* rotate the frame */
	mul_m3_m3m3(mat, rot, mat);
}

/* add a shear and a bend spring between two verts within a poly */
BLI_INLINE bool add_shear_bend_spring(ClothModifierData *clmd, LinkNodePair *edgelist,
                                      const MLoop *mloop, const MPoly *mpoly, int i, int j, int k)
{
	Cloth *cloth = clmd->clothObject;
	ClothSpring *spring;
	const MLoop *tmp_loop;
	float shrink_factor;
	int x, y;

	/* Combined shear/bend properties */

	spring = (ClothSpring *)MEM_callocN(sizeof(ClothSpring), "cloth spring");

	if (!spring) {
		return false;
	}

	spring_verts_ordered_set(
			spring,
			mloop[mpoly[i].loopstart + j].v,
			mloop[mpoly[i].loopstart + k].v);

	shrink_factor = cloth_shrink_factor(clmd, cloth->verts, spring->ij, spring->kl);
	spring->restlen = len_v3v3(cloth->verts[spring->kl].xrest, cloth->verts[spring->ij].xrest) * shrink_factor;
	spring->lenfact = 1.0f;
	spring->type |= CLOTH_SPRING_TYPE_SHEAR;
	spring->lin_stiffness = (cloth->verts[spring->kl].shear_stiff + cloth->verts[spring->ij].shear_stiff) / 2.0f;

	BLI_linklist_append(&edgelist[spring->ij], spring);
	BLI_linklist_append(&edgelist[spring->kl], spring);

	/* Bending specific spring */

	spring->type |= CLOTH_SPRING_TYPE_BENDING;

	spring->la = k - j + 1;
	spring->lb = mpoly[i].totloop - k + j + 1;

	spring->pa = MEM_mallocN(sizeof(*spring->pa) * spring->la, "spring poly");
	if (!spring->pa) {
		return false;
	}

	spring->pb = MEM_mallocN(sizeof(*spring->pb) * spring->lb, "spring poly");
	if (!spring->pb) {
		return false;
	}

	tmp_loop = mloop + mpoly[i].loopstart;

	for (x = 0; x < spring->la; x++) {
		spring->pa[x] = tmp_loop[j + x].v;
	}

	for (x = 0; x <= j; x++) {
		spring->pb[x] = tmp_loop[x].v;
	}

	for (y = k; y < mpoly[i].totloop; x++, y++) {
		spring->pb[x] = tmp_loop[y].v;
	}

	spring->mn = -1;

	spring->restang = spring_angle(cloth->verts, spring->ij, spring->kl, spring->pa, spring->pb, spring->la, spring->lb);

	spring->ang_stiffness = (cloth->verts[spring->ij].bend_stiff + cloth->verts[spring->kl].bend_stiff) / 2.0f;

	BLI_linklist_prepend(&cloth->springs, spring);

	return true;
}

static int cloth_build_springs ( ClothModifierData *clmd, DerivedMesh *dm )
{
	Cloth *cloth = clmd->clothObject;
	ClothSpring *spring = NULL, *tspring = NULL, *tspring2 = NULL;
	unsigned int struct_springs = 0, shear_springs=0, bend_springs = 0, struct_springs_real = 0;
	unsigned int i = 0;
	unsigned int mvert_num = (unsigned int)dm->getNumVerts(dm);
	unsigned int numedges = (unsigned int)dm->getNumEdges (dm);
	unsigned int numpolys = (unsigned int)dm->getNumPolys(dm);
	float shrink_factor;
	const MEdge *medge = dm->getEdgeArray(dm);
	const MPoly *mpoly = dm->getPolyArray(dm);
	const MLoop *mloop = dm->getLoopArray(dm);
	const MLoop *ml;
	LinkNodePair *edgelist;
	LinkNode *search = NULL, *search2 = NULL;
	BendSpringRef *spring_ref;
	BendSpringRef *curr_ref;

	// error handling
	if ( numedges==0 )
		return 0;

	/* NOTE: handling ownership of springs is quite sloppy
	 * currently they are never initialized but assert just to be sure */
	BLI_assert(cloth->springs == NULL);

	cloth->springs = NULL;

	spring_ref = MEM_callocN(sizeof(*spring_ref) * numedges, "temp bend spring reference");

	if (!spring_ref) {
		return 0;
	}

	edgelist = MEM_callocN(sizeof(*edgelist) * mvert_num, "cloth_edgelist_alloc" );

	if (!edgelist) {
		MEM_freeN(spring_ref);
		return 0;
	}

	/* structural springs */
	for ( i = 0; i < numedges; i++ ) {
		spring = MEM_callocN ( sizeof ( ClothSpring ), "cloth spring" );

		if ( spring ) {
			spring_verts_ordered_set(spring, medge[i].v1, medge[i].v2);
			if (clmd->sim_parms->flags & CLOTH_SIMSETTINGS_FLAG_SEW && medge[i].flag & ME_LOOSEEDGE) {
				// handle sewing (loose edges will be pulled together)
				spring->restlen = 0.0f;
				spring->lin_stiffness = 1.0f;
				spring->type = CLOTH_SPRING_TYPE_SEWING;
			}
			else {
				shrink_factor = cloth_shrink_factor(clmd, cloth->verts, spring->ij, spring->kl);
				spring->restlen = len_v3v3(cloth->verts[spring->kl].xrest, cloth->verts[spring->ij].xrest) * shrink_factor;
				spring->lenfact = 1.0f;
				spring->lin_stiffness = (cloth->verts[spring->kl].struct_stiff + cloth->verts[spring->ij].struct_stiff) / 2.0f;
				spring->type |= CLOTH_SPRING_TYPE_STRUCTURAL;

				clmd->sim_parms->avg_spring_len += spring->restlen;
				cloth->verts[spring->ij].avg_spring_len += spring->restlen;
				cloth->verts[spring->kl].avg_spring_len += spring->restlen;
				cloth->verts[spring->ij].spring_count++;
				cloth->verts[spring->kl].spring_count++;
				struct_springs_real++;
			}

			spring->flags = 0;
			struct_springs++;

			BLI_linklist_prepend ( &cloth->springs, spring );
			spring_ref[i].spring = spring;
		}
		else {
			MEM_freeN(spring_ref);
			cloth_free_errorsprings(cloth, edgelist);
			return 0;
		}
	}

	if (struct_springs_real > 0)
		clmd->sim_parms->avg_spring_len /= struct_springs_real;

	for (i = 0; i < mvert_num; i++) {
		if (cloth->verts[i].spring_count > 0)
			cloth->verts[i].avg_spring_len = cloth->verts[i].avg_spring_len * 0.49f / ((float)cloth->verts[i].spring_count);
	}

	/* shear and bend springs */
	if (numpolys) {
		int j, k;

		for (i = 0; i < numpolys; i++) {
			/* shear/bend spring */
			/* triangle faces already have shear springs due to structural geometry */
			if (mpoly[i].totloop > 3) {
				for (j = 1; j < mpoly[i].totloop - 1; j++) {
					if (j > 1) {
						if (add_shear_bend_spring(clmd, edgelist, mloop, mpoly, i, 0, j)) {
							shear_springs++;
							bend_springs++;
						}
						else {
							MEM_freeN(spring_ref);
							cloth_free_errorsprings(cloth, edgelist);
							return 0;
						}
					}

					for (k = j + 2; k < mpoly[i].totloop; k++) {
						if (add_shear_bend_spring(clmd, edgelist, mloop, mpoly, i, j, k)) {
							shear_springs++;
							bend_springs++;
						}
						else {
							MEM_freeN(spring_ref);
							cloth_free_errorsprings(cloth, edgelist);
							return 0;
						}
					}
				}
			}

			/* struct/bending springs */
			ml = mloop + mpoly[i].loopstart;

			for (j = 0; j < mpoly[i].totloop; j++, ml++) {
				curr_ref = &spring_ref[ml->e];
				curr_ref->polys++;

				/* First poly found for this edge, store poly index */
				if (curr_ref->polys == 1) {
					curr_ref->index = i;
				}
				/* Second poly found for this egde, add bending data */
				if (curr_ref->polys == 2) {
					const MLoop *tmp_loop;

					spring = curr_ref->spring;

					spring->type |= CLOTH_SPRING_TYPE_BENDING;

					spring->la = mpoly[curr_ref->index].totloop;
					spring->lb = mpoly[i].totloop;

					spring->pa = MEM_mallocN(sizeof(*spring->pa) * spring->la, "spring poly");
					if (!spring->pa) {
						MEM_freeN(spring_ref);
						cloth_free_errorsprings(cloth, edgelist);
						return 0;
					}

					spring->pb = MEM_mallocN(sizeof(*spring->pb) * spring->lb, "spring poly");
					if (!spring->pb) {
						MEM_freeN(spring_ref);
						cloth_free_errorsprings(cloth, edgelist);
						return 0;
					}

					tmp_loop = mloop + mpoly[curr_ref->index].loopstart;

					for (k = 0; k < spring->la; k++, tmp_loop++) {
						spring->pa[k] = tmp_loop->v;
					}

					tmp_loop = mloop + mpoly[i].loopstart;

					for (k = 0; k < spring->lb; k++, tmp_loop++) {
						spring->pb[k] = tmp_loop->v;
					}

					spring->mn = ml->e;

					spring->restang = spring_angle(cloth->verts, spring->ij, spring->kl, spring->pa, spring->pb, spring->la, spring->lb);
					spring->angoffset = 0.0f;

					spring->ang_stiffness = (cloth->verts[spring->ij].bend_stiff + cloth->verts[spring->kl].bend_stiff) / 2.0f;

					bend_springs++;
				}
				/* Third poly found for this egde, remove bending data */
				else if (curr_ref->polys == 3) {
					spring = curr_ref->spring;

					spring->type &= ~CLOTH_SPRING_TYPE_BENDING;
					MEM_freeN(spring->pa);
					MEM_freeN(spring->pb);
					spring->pa = NULL;
					spring->pb = NULL;

					bend_springs--;
				}
			}
		}
	}

	/* hair springs */
	else if (struct_springs > 2) {
		search = cloth->springs;
		search2 = search->next;
		while (search && search2) {
			tspring = search->link;
			tspring2 = search2->link;

			if (tspring->ij == tspring2->kl) {
				spring = MEM_callocN ( sizeof ( ClothSpring ), "cloth spring" );

				if (!spring) {
					cloth_free_errorsprings(cloth, edgelist);
					return 0;
				}

				spring->ij = tspring2->ij;
				spring->kl = tspring->ij;
				spring->mn = tspring->kl;
				spring->restlen = len_v3v3(cloth->verts[spring->kl].xrest, cloth->verts[spring->ij].xrest);
				spring->lenfact = 1.0f;
				spring->type = CLOTH_SPRING_TYPE_BENDING_HAIR;
				spring->lin_stiffness = (cloth->verts[spring->kl].bend_stiff + cloth->verts[spring->ij].bend_stiff) / 2.0f;
				bend_springs++;

				BLI_linklist_prepend ( &cloth->springs, spring );
			}

			search = search->next;
			search2 = search2->next;
		}

		cloth_hair_update_bending_rest_targets(clmd);
	}

	MEM_freeN(spring_ref);

	cloth->numsprings = struct_springs + shear_springs + bend_springs;

	cloth_free_edgelist(edgelist, mvert_num);

#if 0
	if (G.debug_value > 0)
		printf("avg_len: %f\n", clmd->sim_parms->avg_spring_len);
#endif

	return 1;

} /* cloth_build_springs */
/***************************************************************************************
 * SPRING NETWORK BUILDING IMPLEMENTATION END
 ***************************************************************************************/
