#include "RPCS3IOSSaveDialog.h"

#include "Emu/Cell/Modules/cellSysutil.h"
#include "Emu/IdManager.h"
#include "Emu/RSX/Overlays/overlay_manager.h"
#include "Emu/RSX/Overlays/overlay_save_dialog.h"

#include "util/logs.hpp"

LOG_CHANNEL(cellSaveData);

namespace rpcs3::ios
{
	s32 save_data_dialog::ShowSaveDataList(const std::string& base_dir, std::vector<SaveDataEntry>& save_entries, s32 focused, u32 op, vm::ptr<CellSaveDataListSet> list_set, bool enable_overlay)
	{
		cellSaveData.notice("ShowSaveDataList(save_entries=%d, focused=%d, op=0x%x, listSet=*0x%x, enable_overlay=%d)", save_entries.size(), focused, op, list_set, enable_overlay);

		const bool send_drawing_end = sysutil_send_system_cmd(CELL_SYSUTIL_DRAWING_BEGIN, 0) >= 0;

		if (!send_drawing_end)
		{
			cellSaveData.error("ShowSaveDataList(): Not able to notify DRAWING_BEGIN callback because one has already been sent!");
		}

		s32 result = rsx::overlays::user_interface::selection_code::error;

		if (auto manager = g_fxo->try_get<rsx::overlays::display_manager>())
		{
			cellSaveData.notice("ShowSaveDataList(): Showing native UI dialog");
			result = manager->create<rsx::overlays::save_dialog>()->show(base_dir, save_entries, focused, op, list_set, enable_overlay);
		}
		else
		{
			cellSaveData.error("ShowSaveDataList(): Native overlay manager is unavailable");
		}

		if (send_drawing_end)
		{
			sysutil_send_system_cmd(CELL_SYSUTIL_DRAWING_END, 0);
		}

		if (result == rsx::overlays::user_interface::selection_code::error)
		{
			cellSaveData.error("ShowSaveDataList(): Native UI dialog returned an error");
			return rsx::overlays::user_interface::selection_code::canceled;
		}

		cellSaveData.notice("ShowSaveDataList(): Native UI dialog returned with selection %d", result);
		return result;
	}
}
