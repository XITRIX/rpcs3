bool fragment_program_utils::is_any_src_constant(v128 sourceOperand)
{
#ifdef ARCH_ARM64
	// A source is constant when its two-bit register type is 10. Test the
	// two upper words together without extracting and comparing each lane.
	const u64 pairs = sourceOperand._u64[1];
	return ((pairs & ~(pairs << 1)) & 0x20000000200) || (sourceOperand._u32[1] & 0x300) == 0x200;
#else
	const u64 masked = sourceOperand._u64[1] & 0x30000000300;
	return (sourceOperand._u32[1] & 0x300) == 0x200 || (static_cast<u32>(masked) == 0x200 || static_cast<u32>(masked >> 32) == 0x200);
#endif
}
bool fragment_program_compare::operator()(const RSXFragmentProgram& binary1, const RSXFragmentProgram& binary2) const
{
	if (!compare_properties(binary1, binary2))
	{
		return false;
	}

	const void* instBuffer1 = binary1.get_data();
	const void* instBuffer2 = binary2.get_data();
	for (usz instIndex = 0; instIndex < (binary1.ucode_length / 16); instIndex++)
	{
		const auto inst1 = v128::loadu(instBuffer1, instIndex);
		const auto inst2 = v128::loadu(instBuffer2, instIndex);

		if (inst1._u ^ inst2._u)
		{
			return false;
		}

		// Skip constants
		if (fragment_program_utils::is_any_src_constant(inst1))
			instIndex++;
	}
	
	return true;
}
bool fragment_program_compare::compare_properties(const RSXFragmentProgram& binary1, const RSXFragmentProgram& binary2)
{
	return binary1.ucode_length == binary2.ucode_length &&
		binary1.ctrl == binary2.ctrl &&
		binary1.texture_state == binary2.texture_state &&
		binary1.texcoord_control_mask == binary2.texcoord_control_mask &&
		binary1.two_sided_lighting == binary2.two_sided_lighting &&
		binary1.mrt_buffers_count == binary2.mrt_buffers_count;
}