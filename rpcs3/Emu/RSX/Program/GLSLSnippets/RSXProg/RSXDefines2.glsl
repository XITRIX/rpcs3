R"(
// Small structures that should be defined before any backend logic
// Avoid arrays and sub-vec4 members because of std140 padding constraints
struct sampler_info
{
	float scale_x, scale_y, scale_z;  // 12
	float bias_x, bias_y, bias_z;     // 24
	float clamp_min_x, clamp_min_y;   // 32
	float clamp_max_x, clamp_max_y;   // 40
	uint remap;                       // 44
	uint flags;                       // 48
#ifdef _ENABLE_VK_COLOR_BORDER
	vec4 border_color;                // 64 (Vulkan only)
#endif
};

#ifdef _ENABLE_VK_COLOR_BORDER
vec4 _apply_color_border(vec4 texel, vec2 coord, vec2 size, sampler_info params)
{
	const uint axes = params.flags & 0x60000000u;
	if (axes == 0u) return texel;

	vec2 inside;
	if ((params.flags & 0x80000000u) != 0u)
	{
		// Sum the in-range bilinear taps along each clamped axis.
		inside = clamp(min(coord * size + 0.5, (1.0 - coord) * size + 0.5), 0.0, 1.0);
	}
	else
	{
		inside = vec2(greaterThanEqual(coord, vec2(0.0))) * vec2(lessThan(coord, vec2(1.0)));
	}
	if ((axes & 0x20000000u) == 0u) inside.x = 1.0;
	if ((axes & 0x40000000u) == 0u) inside.y = 1.0;
	return mix(params.border_color, texel, inside.x * inside.y);
}
#endif

struct vertex_context_t
{
	mat4 scale_offset_mat;
	uint user_clip_configuration_bits;
	uint transform_branch_bits;
	float point_size;
	float z_near;
	float z_far;
	float reserved[3];
};

struct draw_parameters_t
{
	uint vertex_base_index;
	uint vertex_index_offset;
	uint draw_id;
	uint xform_constants_offset;
	uint vs_context_offset;
	uint fs_constants_offset;
	uint fs_context_offset;
	uint fs_texture_base_index;
	uint fs_stipple_pattern_offset;
	uint reserved;
	uvec2 attrib_data[16];
};

struct fragment_context_t
{
	float fog_param0;
	float fog_param1;
	uint rop_control;
	float alpha_ref;
	uint fog_mode;
	float wpos_scale;
	vec2 wpos_bias;
};

)"
