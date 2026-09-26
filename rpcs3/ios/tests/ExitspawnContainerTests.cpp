#include <cstdio>
#include <functional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using u32 = unsigned;
struct lv2_memory_container {};
namespace id_manager { template <typename> struct id_map {}; }

static void ensure(bool condition)
{
	if (!condition) throw std::runtime_error("container lifecycle assertion failed");
}

struct fake_fxo
{
	bool map = false;
	bool default_container = false;
	u32 size = 0;

	template <typename T> bool is_init() const
	{
		if constexpr (std::is_same_v<T, lv2_memory_container>) return default_container;
		else return map;
	}
	template <typename T> bool init(u32 memory_size = 0)
	{
		ensure(!is_init<T>());
		if constexpr (std::is_same_v<T, lv2_memory_container>)
		{
			default_container = true;
			size = memory_size;
		}
		else map = true;
		return true;
	}
} fxo;
static auto* g_fxo = &fxo;

enum class game_boot_result { no_errors, currently_restricted, invalid_file };
enum class cfg_mode { continuous };
static void select_containers(void* ar, u32 mem_size);

struct fake_emulator
{
	std::function<void(u32)> init_mem_containers;
	std::vector<std::string> argv, envp;
	std::vector<unsigned> data, klic;
	std::string disc, hdd1;
	bool force = false, continuous = true;
	unsigned kills = 0;
	game_boot_result result = game_boot_result::no_errors;
	void SetForceBoot(bool value) { force = value; }
	void SetContinuousMode(bool value) { continuous = value; }
	void Kill(bool autoexit)
	{
		ensure(!autoexit);
		++kills;
		// Stand-in for the already-stopped branch of Emulator::Kill.
		argv.clear(); envp.clear(); data.clear(); disc.clear(); hdd1.clear(); klic.clear();
		init_mem_containers = nullptr;
	}
	game_boot_result BootGame(const std::string&, const char*, bool, cfg_mode, int, int)
	{
		if (result == game_boot_result::no_errors) select_containers(nullptr, 256);
		return result;
	}
} Emu;

struct fake_log
{
	unsigned failures = 0;
	template <typename... Args> void fatal(const char*, Args...) { ++failures; }
} sys_process;

static void select_containers(void* ar, u32 mem_size)
{
#include "ContainerSelectionUnderTest.inc"
}

static void handoff(std::function<void(u32)> func)
{
	std::vector<std::string> argv{"game.self"}, envp{"ENV=1"};
	std::vector<unsigned> data{1};
	std::string disc="disc", path="game.self", hdd1="cache";
	int old_config=0, old_db_config=0;
	unsigned klic=1;
#include "ExitspawnHandoffUnderTest.inc"
}

int main()
{
	try
	{
		unsigned calls = 0;
		auto callback = [&](u32 size)
		{
			ensure(!Emu.init_mem_containers); // Clear before invoking it.
			++calls;
			ensure(g_fxo->init<id_manager::id_map<lv2_memory_container>>());
			ensure(g_fxo->init<lv2_memory_container>(size - 32));
		};

		// Successful exitspawn retains the native handoff, exactly once.
		handoff(callback);
		ensure(calls == 1 && fxo.map && fxo.size == 224 && Emu.kills == 0);
		ensure(!Emu.init_mem_containers && sys_process.failures == 0);

		// A stale callback must never pre-initialize containers for a state.
		for (unsigned repeat = 0; repeat < 100; ++repeat)
		{
			fxo = {};
			Emu.init_mem_containers = callback;
			int serialized_state = 1;
			select_containers(&serialized_state, 256);
			ensure(calls == 1 && !Emu.init_mem_containers && !fxo.map && !fxo.default_container);
			// Serialization owns both initializations after this selection step.
			ensure(g_fxo->init<id_manager::id_map<lv2_memory_container>>());
			ensure(g_fxo->init<lv2_memory_container>(192));
		}

		// Both early rejection and later boot failure discard pending setup.
		for (const auto result : {game_boot_result::currently_restricted, game_boot_result::invalid_file})
		{
			Emu = {};
			fxo = {};
			Emu.result = result;
			handoff(callback);
			ensure(calls == 1 && Emu.kills == 1 && !Emu.init_mem_containers);
			ensure(!Emu.force && !Emu.continuous && Emu.argv.empty() && Emu.disc.empty() && Emu.klic.empty());
			select_containers(nullptr, 256);
			ensure(fxo.map && fxo.size == 256 && calls == 1);
		}

		// A normal boot still initializes both default container structures.
		fxo = {};
		select_containers(nullptr, 128);
		ensure(fxo.map && fxo.size == 128 && !Emu.init_mem_containers);
		std::puts("Exitspawn containers: success, 100 stale-callback state loads, rejected/failed handoffs and fresh boots passed");
	}
	catch (const std::exception& error)
	{
		std::fprintf(stderr, "Exitspawn container test failed: %s\n", error.what());
		return 1;
	}
}
