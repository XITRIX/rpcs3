#pragma once

#include "Emu/Cell/Modules/cellSaveData.h"

namespace rpcs3::ios
{
	class save_data_dialog final : public SaveDialogBase
	{
	public:
		s32 ShowSaveDataList(const std::string& base_dir, std::vector<SaveDataEntry>& save_entries, s32 focused, u32 op, vm::ptr<CellSaveDataListSet> list_set, bool enable_overlay) override;
	};
}
