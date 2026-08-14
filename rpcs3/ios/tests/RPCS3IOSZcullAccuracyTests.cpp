#include "../RPCS3IOSZcullAccuracy.h"

int main()
{
	using namespace rpcs3::ios;

	static_assert(zcull_accuracy_name({true, false}) == "Precise");
	static_assert(zcull_accuracy_name({false, false}) == "Approximate");
	static_assert(zcull_accuracy_name({false, true}) == "Relaxed");
	static_assert(zcull_accuracy_name({true, true}) == "Relaxed");

	constexpr auto precise = parse_zcull_accuracy("Precise");
	static_assert(precise && precise->precise && !precise->relaxed);
	constexpr auto approximate = parse_zcull_accuracy("Approximate");
	static_assert(approximate && !approximate->precise && !approximate->relaxed);
	constexpr auto relaxed = parse_zcull_accuracy("Relaxed");
	static_assert(relaxed && !relaxed->precise && relaxed->relaxed);
	static_assert(!parse_zcull_accuracy("Fast"));

	return 0;
}
