//
//  test.cpp
//  RPCS3
//
//  Created by Даниил Виноградов on 13.01.2025.
//


//template <typename T>
//class named_thread;

#include <Utilities/Thread.h>
#include <Emu/Io/pad_config.h>
#include <Emu/Cell/Modules/cellGem.h>
#include <Utilities/cheat_info.h>
#include "Input/product_info.h"
#include "Utilities/mutex.h"
#include "rpcs3/util/atomic.hpp"
#include "pad_thread.h"
#include "ps_move_tracker.h"
#include "test.hpp"

cfg_input_configurations g_cfg_input_configs;

std::string g_input_config_override;

namespace rpcs3
{
std::string get_version_and_branch()
{
	return "IOS-FORK";
}

std::string get_verbose_version()
{
	return "version";
}
}

bool is_input_allowed()
{
	return true;
}

namespace input
{
std::vector<product_info> get_products_by_class(int class_id)
{
	std::vector<product_info> ret;
//	for (const auto& [type, product] : input_products)
//	{
//		if (product.class_id == class_id)
//			ret.push_back(product);
//	}
	return ret;
}
}


void enable_display_sleep()
{

}


void disable_display_sleep()
{

}

extern void check_microphone_permissions() {}

extern void qt_events_aware_op(int repeat_duration_ms, std::function<bool()> wrapped_op)
{
}

void pad_thread::SetIntercepted(bool intercepted)
{
	if (intercepted)
	{
		m_info.system_info |= CELL_PAD_INFO_INTERCEPTED;
		m_info.ignore_input = true;
	}
	else
	{
		m_info.system_info &= ~CELL_PAD_INFO_INTERCEPTED;
	}
}

s32 pad_thread::AddLddPad()
{
	// Look for first null pad
//	for (u32 i = 0; i < CELL_PAD_MAX_PORT_NUM; i++)
//	{
//		if (g_cfg_input.player[i]->handler == pad_handler::null && !m_pads[i]->ldd)
//		{
//			InitLddPad(i, nullptr);
//			return i;
//		}
//	}

	return -1;
}

void pad_thread::UnregisterLddPad(u32 handle)
{
//	ensure(handle < m_pads.size());
//
//	m_pads[handle]->ldd = false;
//
//	num_ldd_pad--;
}

void pad_thread::SetRumble(const u32 pad, u8 large_motor, bool small_motor)
{
	if (pad >= m_pads.size())
		return;

	m_pads[pad]->m_vibrateMotors[0].m_value = large_motor;
	m_pads[pad]->m_vibrateMotors[1].m_value = small_motor ? 255 : 0;
}

[[noreturn]] extern void report_fatal_error(std::string_view _text, bool is_html = false, bool include_help_text = true)
{
	std::abort();
}

namespace pad
{
	atomic_t<pad_thread*> g_pad_thread = nullptr;
	shared_mutex g_pad_mutex;
	std::string g_title_id;
	atomic_t<bool> g_started{false};
	atomic_t<bool> g_reset{false};
	atomic_t<bool> g_enabled{true};
	atomic_t<bool> g_home_menu_requested{false};
}

template <bool DiagnosticsEnabled>
std::tuple<u8, u8, u8> ps_move_tracker<DiagnosticsEnabled>::hsv_to_rgb(u16 hue, f32 saturation, f32 value)
{
	const f32 h = hue / 60.0f;
	const f32 chroma = value * saturation;
	const f32 x = chroma * (1.0f - std::abs(std::fmod(h, 2.0f) - 1.0f));
	const f32 m = value - chroma;

	f32 r = 0.0f;
	f32 g = 0.0f;
	f32 b = 0.0f;

	switch (static_cast<int>(std::ceil(h)))
	{
	case 0:
	case 1:
		r = chroma + m;
		g = x + m;
		b = 0 + m;
		break;
	case 2:
		r = x + m;
		g = chroma + m;
		b = 0 + m;
		break;
	case 3:
		r = 0 + m;
		g = chroma + m;
		b = x + m;
		break;
	case 4:
		r = 0 + m;
		g = x + m;
		b = chroma + m;
		break;
	case 5:
		r = x + m;
		g = 0 + m;
		b = chroma + m;
		break;
	case 6:
		r = chroma + m;
		g = 0 + m;
		b = x + m;
		break;
	default:
		break;
	}

	const u8 red   = static_cast<u8>(std::clamp(std::round(r * 255.0f), 0.0f, 255.0f));
	const u8 green = static_cast<u8>(std::clamp(std::round(g * 255.0f), 0.0f, 255.0f));
	const u8 blue  = static_cast<u8>(std::clamp(std::round(b * 255.0f), 0.0f, 255.0f));

	return { red, green, blue };
}


template <bool DiagnosticsEnabled>
std::tuple<s16, f32, f32> ps_move_tracker<DiagnosticsEnabled>::rgb_to_hsv(f32 r, f32 g, f32 b)
{
	const f32 cmax = std::max({r, g, b}); // V (of HSV)
	const f32 cmin = std::min({r, g, b});
	const f32 delta = cmax - cmin;
	const f32 saturation = cmax ? (delta / cmax) : 0.0f; // S (of HSV)

	s16 hue; // H (of HSV)

	if (!delta)
	{
		hue = 0;
	}
	else if (cmax == r)
	{
		hue = (static_cast<s16>(60.0f * (g - b) / delta) + 360) % 360;
	}
	else if (cmax == g)
	{
		hue = (static_cast<s16>(60.0f * (b - r) / delta) + 120 + 360) % 360;
	}
	else
	{
		hue = (static_cast<s16>(60.0f * (r - g) / delta) + 240 + 360) % 360;
	}

	return { hue, saturation, cmax };
}

template <bool DiagnosticsEnabled>
void ps_move_tracker<DiagnosticsEnabled>::set_hue(u32 index, u16 hue)
{
	ps_move_config& config = ::at32(m_config, index);
	config.hue = hue;
	config.calculate_values();
}

template <bool DiagnosticsEnabled>
void ps_move_tracker<DiagnosticsEnabled>::set_hue_threshold(u32 index, u16 threshold)
{
	ps_move_config& config = ::at32(m_config, index);
	config.hue_threshold = threshold;
	config.calculate_values();
}

template <bool DiagnosticsEnabled>
void ps_move_tracker<DiagnosticsEnabled>::set_saturation_threshold(u32 index, u16 threshold)
{
	ps_move_config& config = ::at32(m_config, index);
	config.saturation_threshold_u8 = threshold;
	config.calculate_values();
}

template <bool DiagnosticsEnabled>
void ps_move_tracker<DiagnosticsEnabled>::process_image()
{
}

template <bool DiagnosticsEnabled>
void ps_move_tracker<DiagnosticsEnabled>::set_active(u32 index, bool active)
{
	ps_move_config& config = ::at32(m_config, index);
	config.active = active;
}

template <bool DiagnosticsEnabled>
void ps_move_tracker<DiagnosticsEnabled>::set_image_data(const void* buf, u64 size, u32 width, u32 height, s32 format)
{
	if (!buf || !size || !width || !height || !format)
	{
//		ps_move.error("ps_move_tracker got unexpected input: buf=*0x%x, size=%d, width=%d, height=%d, format=%d", buf, size, width, height, format);
		return;
	}

	m_width = width;
	m_height = height;
	m_format = format;
	m_image_data.resize(size);

	std::memcpy(m_image_data.data(), buf, size);
}

template <bool DiagnosticsEnabled>
ps_move_tracker<DiagnosticsEnabled>::ps_move_tracker()
{
//	init_workers();
}

template <bool DiagnosticsEnabled>
ps_move_tracker<DiagnosticsEnabled>::~ps_move_tracker()
{
//	for (u32 index = 0; index < CELL_GEM_MAX_NUM; index++)
//	{
//		if (auto& worker = m_workers[index])
//		{
//			auto& thread = *worker;
//			thread = thread_state::aborting;
//			m_wake_up_workers[index].release(1);
//			m_wake_up_workers[index].notify_one();
//			thread();
//		}
//	}
}

template <bool DiagnosticsEnabled>
void ps_move_tracker<DiagnosticsEnabled>::ps_move_config::calculate_values()
{
	// The hue is a "circle", so we use modulo 360.
	max_hue = (hue + hue_threshold) % 360;
	min_hue = hue - hue_threshold;

	if (min_hue < 0)
	{
		min_hue += 360;
	}
	else
	{
		min_hue %= 360;
	}

	saturation_threshold = saturation_threshold_u8 / 255.0f;
}


template class ps_move_tracker<false>;
template class ps_move_tracker<true>;


//"fmt_class_string<std::__1::chrono::time_point<std::__1::chrono::system_clock, std::__1::chrono::duration<long long, std::__1::ratio<1l, 1000000l>>>, void>::format(std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>&, unsigned long long)", referenced from:
//  fmt::type_info_v<std::__1::chrono::time_point<std::__1::chrono::system_clock, std::__1::chrono::duration<long long, std::__1::ratio<1l, 1000000l>>>> in librpcs3_emu.a[440](System.o)


template <>
void fmt_class_string<std::chrono::sys_time<typename std::chrono::system_clock::duration>>::format(std::string& out, u64 arg)
{
//	const std::time_t dateTime = std::chrono::system_clock::to_time_t(get_object(arg));
//	out += date_time::fmt_time("%Y-%m-%dT%H:%M:%S", dateTime);
}


//"fmt_class_string<cheat_type, void>::format(std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>&, unsigned long long)", referenced from:
//	  YAML::convert<cheat_info>::decode(YAML::Node const&, cheat_info&) in librpcs3_emu.a[473](yaml.o)


template <>
void fmt_class_string<std::string>::format(std::string& out, u64 arg)
{
	out += get_object(arg);
}

template <>
void fmt_class_string<std::string_view>::format(std::string& out, u64 arg)
{
	out += get_object(arg);
}

template <>
void fmt_class_string<cheat_type>::format(std::string& out, u64 arg)
{
	format_enum(out, arg, [](cheat_type value)
	{
		switch (value)
		{
		case cheat_type::unsigned_8_cheat: return "Unsigned 8 bits";
		case cheat_type::unsigned_16_cheat: return "Unsigned 16 bits";
		case cheat_type::unsigned_32_cheat: return "Unsigned 32 bits";
		case cheat_type::unsigned_64_cheat: return "Unsigned 64 bits";
		case cheat_type::signed_8_cheat: return "Signed 8 bits";
		case cheat_type::signed_16_cheat: return "Signed 16 bits";
		case cheat_type::signed_32_cheat: return "Signed 32 bits";
		case cheat_type::signed_64_cheat: return "Signed 64 bits";
		case cheat_type::float_32_cheat: return "Float 32 bits";
		case cheat_type::max: break;
		}

		return unknown;
	});
}

/// LLVM
//#include <llvm/IR/IRBuilder.h>
//
//using namespace llvm;
//CallInst *IRBuilderBase::CreateConstrainedFPBinOp(
//	Intrinsic::ID ID, Value *L, Value *R, FMFSource FMFSource,
//	const Twine &Name, MDNode *FPMathTag, std::optional<RoundingMode> Rounding,
//	std::optional<fp::ExceptionBehavior> Except) {
//  Value *RoundingV = getConstrainedFPRounding(Rounding);
//  Value *ExceptV = getConstrainedFPExcept(Except);
//
//  FastMathFlags UseFMF = FMFSource.get(FMF);
//
//  CallInst *C = CreateIntrinsic(ID, {L->getType()},
//								{L, R, RoundingV, ExceptV}, nullptr, Name);
//  setConstrainedFPCallAttr(C);
//  setFPAttrs(C, FPMathTag, UseFMF);
//  return C;
//}

//fmt_class_string<cheat_type, void>::format(std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>&, unsigned long long)
