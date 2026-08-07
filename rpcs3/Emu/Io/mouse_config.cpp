#include "stdafx.h"
#include "mouse_config.h"
#include "MouseHandler.h"
#include "Utilities/File.h"

mouse_config::mouse_config()
#ifdef RPCS3_IOS
	: cfg_name()
#else
	: cfg_name(fs::get_config_dir(true) + "config_mouse.yml")
#endif
{
}

bool mouse_config::exist() const
{
	const std::string config_path = cfg_name.empty() ? fs::get_config_dir(true) + "config_mouse.yml" : cfg_name;
	return fs::is_file(config_path);
}

bool mouse_config::load()
{
	g_cfg_mouse.from_default();

	const std::string config_path = cfg_name.empty() ? fs::get_config_dir(true) + "config_mouse.yml" : cfg_name;
	if (fs::file cfg_file{config_path, fs::read})
	{
		if (const std::string content = cfg_file.to_string(); !content.empty())
		{
			return from_string(content);
		}
	}

	return false;
}

void mouse_config::save()
{
	const std::string config_path = cfg_name.empty() ? fs::get_config_dir(true) + "config_mouse.yml" : cfg_name;
	fs::pending_file file(config_path);

	if (file.file)
	{
		file.file.write(to_string());
		file.commit();
	}

	reload_requested = true;
}

cfg::string& mouse_config::get_button(int code)
{
	switch (code)
	{
	case CELL_MOUSE_BUTTON_1: return mouse_button_1;
	case CELL_MOUSE_BUTTON_2: return mouse_button_2;
	case CELL_MOUSE_BUTTON_3: return mouse_button_3;
	case CELL_MOUSE_BUTTON_4: return mouse_button_4;
	case CELL_MOUSE_BUTTON_5: return mouse_button_5;
	case CELL_MOUSE_BUTTON_6: return mouse_button_6;
	case CELL_MOUSE_BUTTON_7: return mouse_button_7;
	case CELL_MOUSE_BUTTON_8: return mouse_button_8;
	default: fmt::throw_exception("Invalid code %d", code);
	}
}
