#pragma once

#include <stdint.h>
#include <stdbool.h>

// Shared by the signed module and the core. All process-owned pointers arrive
// as data at invocation time; no generated trampoline or code patch is needed.
#define RPCS3_AOT_RUNTIME_VERSION 4u
#define RPCS3_AOT_SPARSE_SPU 1u

struct rpcs3_aot_runtime_context_v2
{
	uint32_t structure_size;
	uint32_t version;
	uint64_t* ppu_gpr;
	uint32_t* spu_gpr_words;
	const void* runtime_functions;
	void* cpu;
	void* memory_base;
	void* local_store;
	uint32_t guest_pc;
	uint32_t next_pc;
	bool (*matches_code)(const rpcs3_aot_runtime_context_v2*, const uint32_t*, uint32_t);
	uint32_t runtime_function_count;
	uint32_t (*read_code)(const rpcs3_aot_runtime_context_v2*, uint32_t*, uint32_t);
	uint32_t remaining_words;    // bounded native execution before servicing CPU work
	uint32_t executed_words;     // dynamically executed words, not static program size
	uint32_t code_base_pc;       // runtime address corresponding to words[0]
	uint32_t verification_flags; // dispatcher-only code matching policy
};

struct rpcs3_aot_entry_v2
{
	uint32_t guest_kind; // 1 PPU, 2 SPU
	uint32_t opcode;     // first word; the complete code unit is checked before dispatch
	uint32_t word_count;
	uint32_t entry_offset;
	const uint32_t* words;
	void (*invoke)(rpcs3_aot_runtime_context_v2*);
	uint32_t alternate_count;
	uint32_t verification_flags;
	const uint32_t* alternate_offsets; // resumable native block/chunk entries (word offsets)
};

struct rpcs3_aot_module_v2
{
	uint32_t structure_size;
	uint32_t version;
	uint32_t entry_count;
	uint32_t reserved;
	const rpcs3_aot_entry_v2* entries;
	uint32_t import_count;
	uint32_t import_reserved;
	const char* const* imports;
};
