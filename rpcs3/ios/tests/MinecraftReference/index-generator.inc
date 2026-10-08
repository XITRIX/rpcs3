void write_non_native_indices_naive(char* dst, rsx::primitive_type draw_mode, unsigned count)
{
	auto typedDst = reinterpret_cast<u32*>(dst);
	switch (draw_mode)
	{
	case rsx::primitive_type::line_loop:
		iota32(typedDst, count);
		typedDst[count] = 0;
		return;
	case rsx::primitive_type::triangle_fan:
	case rsx::primitive_type::polygon:
		for (unsigned i = 0; i < (count - 2); i++)
		{
			typedDst[3 * i] = 0;
			typedDst[3 * i + 1] = i + 2 - 1;
			typedDst[3 * i + 2] = i + 2;
		}
		return;
	case rsx::primitive_type::quads:
		for (unsigned i = 0; i < count / 4; i++)
		{
			// First triangle
			typedDst[6 * i] = 4 * i;
			typedDst[6 * i + 1] = 4 * i + 1;
			typedDst[6 * i + 2] = 4 * i + 2;
			// Second triangle
			typedDst[6 * i + 3] = 4 * i + 2;
			typedDst[6 * i + 4] = 4 * i + 3;
			typedDst[6 * i + 5] = 4 * i;
		}
		return;
	case rsx::primitive_type::quad_strip:
	case rsx::primitive_type::points:
	case rsx::primitive_type::lines:
	case rsx::primitive_type::line_strip:
	case rsx::primitive_type::triangles:
	case rsx::primitive_type::triangle_strip:
		fmt::throw_exception("Native primitive type doesn't require expansion");
	}

	fmt::throw_exception("Tried to load invalid primitive type");
}

constexpr u32 max_precomputed_vertices = 65536;

const std::vector<u32>& quad_index_table()
{
	static const std::vector<u32> table = []
	{
		std::vector<u32> result(max_precomputed_vertices / 4 * 6);
		for (u32 i = 0; i < max_precomputed_vertices / 4; i++)
		{
			result[6 * i + 0] = static_cast<u32>(4 * i + 0);
			result[6 * i + 1] = static_cast<u32>(4 * i + 1);
			result[6 * i + 2] = static_cast<u32>(4 * i + 2);
			result[6 * i + 3] = static_cast<u32>(4 * i + 2);
			result[6 * i + 4] = static_cast<u32>(4 * i + 3);
			result[6 * i + 5] = static_cast<u32>(4 * i + 0);
		}
		return result;
	}();

	return table;
}

const std::vector<u32>& fan_index_table()
{
	static const std::vector<u32> table = []
	{
		std::vector<u32> result((max_precomputed_vertices - 2) * 3);
		for (u32 i = 0; i < max_precomputed_vertices - 2; i++)
		{
			result[3 * i + 0] = 0;
			result[3 * i + 1] = static_cast<u32>(i + 1);
			result[3 * i + 2] = static_cast<u32>(i + 2);
		}
		return result;
	}();

	return table;
}

void write_non_native_indices_precomputed(char* dst, rsx::primitive_type draw_mode, unsigned count)
{
	auto* typed_dst = reinterpret_cast<u32*>(dst);

	switch (draw_mode)
	{
	case rsx::primitive_type::triangle_fan:
	case rsx::primitive_type::polygon:
		if (count >= 2 && count <= max_precomputed_vertices) [[likely]]
		{
			std::memcpy(typed_dst, fan_index_table().data(), (count - 2) * 3 * sizeof(u32));
			return;
		}
		break;
	case rsx::primitive_type::quads:
		if (count <= max_precomputed_vertices) [[likely]]
		{
			std::memcpy(typed_dst, quad_index_table().data(), (count / 4) * 6 * sizeof(u32));
			return;
		}
		break;
	default:
		break;
	}

	write_non_native_indices_naive(dst, draw_mode, count);
}

