#pragma once

#include <bit>
#include <vector>

namespace vk
{
// The query manager has a fixed number of slots. Preserve FIFO reuse order
// without allocating/deallocating deque blocks during steady-state rendering.
class query_slot_queue
{
	std::vector<u32> m_slots;
	u32 m_head = 0;
	u32 m_tail = 0;
	u32 m_mask = 0;
	u32 m_capacity = 0;

public:
	void set_capacity(u32 capacity)
	{
		ensure(m_slots.empty() && capacity);
		ensure(capacity <= 0x80000000u);
		m_capacity = capacity;
		m_slots.resize(std::bit_ceil(capacity));
		m_mask = static_cast<u32>(m_slots.size() - 1);
	}

	bool empty() const { return m_head == m_tail; }
	u32 front() const { return m_slots[m_head & m_mask]; }

	void pop_front()
	{
		ensure(!empty());
		++m_head;
	}

	void push_back(u32 index)
	{
		ensure(m_tail - m_head < m_capacity);
		m_slots[m_tail++ & m_mask] = index;
	}
};
}
