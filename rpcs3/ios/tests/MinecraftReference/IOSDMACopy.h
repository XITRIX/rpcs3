#pragma once

#include <cstddef>

namespace rpcs3::ios
{
// Keep the original forward, one-vector-at-a-time copy semantics, including
// overlapping ranges. Do not replace this with a whole-range memcpy.
template <typename Vector, std::size_t Size>
inline void copy_dma_fixed(unsigned char* dst, const unsigned char* src)
{
	static_assert(sizeof(Vector) == 16 && Size % 16 == 0);
	for (std::size_t offset = 0; offset < Size; offset += 16)
	{
		*reinterpret_cast<Vector*>(dst + offset) = *reinterpret_cast<const Vector*>(src + offset);
	}
}

template <typename Vector>
inline void copy_dma_vectors(unsigned char* dst, const unsigned char* src, std::size_t size)
{
	static_assert(sizeof(Vector) == 16);
	switch (size)
	{
	case 64: copy_dma_fixed<Vector, 64>(dst, src); return;
	case 80: copy_dma_fixed<Vector, 80>(dst, src); return;
	case 96: copy_dma_fixed<Vector, 96>(dst, src); return;
	case 112: copy_dma_fixed<Vector, 112>(dst, src); return;
	case 128: copy_dma_fixed<Vector, 128>(dst, src); return;
	default:
		// Callers supply whole aligned vectors, as required by the old loop.
		for (std::size_t offset = 0; offset < size; offset += 16)
		{
			*reinterpret_cast<Vector*>(dst + offset) = *reinterpret_cast<const Vector*>(src + offset);
		}
	}
}
}
