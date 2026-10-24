#pragma once
#include <string>
namespace vk::apple
{
	struct backend_availability
	{
		bool available = false;
		std::string reason;
	};
	// Probing never changes the renderer's selected dispatch table.
	const backend_availability& kosmickrisp_availability();
	// Called while stopped, before constructing an instance or renderer workers.
	void select_backend(const std::string& requested);
	bool using_kosmickrisp();
} // namespace vk::apple
