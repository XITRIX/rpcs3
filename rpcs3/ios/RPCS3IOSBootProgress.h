#pragma once

#include <mutex>
#include <string>
#include <utility>

namespace rpcs3::ios
{
// One serialized boot writer, independent observational readers. Never hold
// this mutex across filesystem, renderer, driver, or lifecycle operations.
class boot_stage_registry final
{
public:
	void update(std::string stage)
	{
		std::lock_guard lock(m_mutex);
		m_stage = std::move(stage);
	}

	std::string snapshot() const
	{
		std::lock_guard lock(m_mutex);
		return m_stage;
	}

private:
	mutable std::mutex m_mutex;
	std::string m_stage;
};

inline boot_stage_registry& boot_stages()
{
	static boot_stage_registry registry;
	return registry;
}

class scoped_boot_stage final
{
public:
	explicit scoped_boot_stage(std::string stage, boot_stage_registry& registry = boot_stages())
		: m_registry(registry)
	{
		m_registry.update(std::move(stage));
	}

	scoped_boot_stage(const scoped_boot_stage&) = delete;
	scoped_boot_stage& operator=(const scoped_boot_stage&) = delete;

	void update(std::string stage)
	{
		m_registry.update(std::move(stage));
	}

	~scoped_boot_stage()
	{
		m_registry.update({});
	}

private:
	boot_stage_registry& m_registry;
};
}
