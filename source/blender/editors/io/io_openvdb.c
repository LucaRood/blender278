#ifdef WITH_OPENVDB

/* needed for directory lookup */
#ifndef WIN32
#  include <dirent.h>
#else
#  include "BLI_winstuff.h"
#endif

#include "MEM_guardedalloc.h"

#include "DNA_modifier_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_space_types.h"

#include "BKE_context.h"
#include "BKE_global.h"
#include "BKE_main.h"
#include "BKE_report.h"

#include "BLI_listbase.h"
#include "BLI_math_vector.h"
#include "BLI_path_util.h"
#include "BLI_string.h"
#include "BLI_utildefines.h"

#include "BLT_translation.h"

#include "RNA_access.h"
#include "RNA_define.h"
#include "RNA_enum_types.h"

#include "UI_interface.h"
#include "UI_resources.h"

#include "WM_api.h"
#include "WM_types.h"

#include "io_openvdb.h"

#ifdef WITH_OPENVDB
#include "openvdb_capi.h"
#endif

static void wm_openvdb_import_draw(bContext *UNUSED(C), wmOperator *op)
{
	PointerRNA ptr;

	RNA_pointer_create(NULL, op->type->srna, op->properties, &ptr);
	//ui_openvdb_import_settings(op->layout, &ptr);
}

static int wm_openvdb_import_exec(bContext *C, wmOperator *op)
{
	if (!RNA_struct_property_is_set(op->ptr, "filepath")) {
		BKE_report(op->reports, RPT_ERROR, "No filename given");
		return OPERATOR_CANCELLED;
	}

	char filename[FILE_MAX];
	RNA_string_get(op->ptr, "filepath", filename);

	struct OpenVDBReader *reader = OpenVDBReader_create();
	OpenVDBReader_open(reader, filename);

	OpenVDB_print_grids(reader);

	return OPERATOR_FINISHED;
}

void WM_OT_openvdb_import(wmOperatorType *ot)
{
	ot->name = "Import OpenVDB";
	ot->description = "Load an OpenVDB cache";
	ot->idname = "WM_OT_openvdb_import";

	ot->invoke = WM_operator_filesel;
	ot->exec = wm_openvdb_import_exec;
	ot->poll = WM_operator_winactive;
	ot->ui = wm_openvdb_import_draw;

	WM_operator_properties_filesel(ot, FILE_TYPE_FOLDER | FILE_TYPE_OPENVDB,
	                               FILE_BLENDER, FILE_SAVE, WM_FILESEL_FILEPATH,
	                               FILE_DEFAULTDISPLAY, FILE_SORT_ALPHA);
}

#endif