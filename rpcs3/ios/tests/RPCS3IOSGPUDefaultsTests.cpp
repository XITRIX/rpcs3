#include "../RPCS3IOSGPUDefaults.h"

#include <type_traits>

int main()
{
	using namespace rpcs3::ios;

	static_assert(std::is_same_v<std::remove_cv_t<decltype(default_shader_mode)>, shader_mode>);
	static_assert(default_shader_mode == shader_mode::async_recompiler);
	static_assert(!default_precise_zcull);
	static_assert(!default_relaxed_zcull);

	return 0;
}
