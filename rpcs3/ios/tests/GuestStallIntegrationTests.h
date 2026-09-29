// Executes production compilation entry points with slow/failing host compiler
// stubs. Clock, guard, gating and cleanup are the actual production code.
namespace stall_integration
{
static bool expect_hold = true;
static bool fail_compile = false;

static void host_work()
{
	const u64 before = get_guest_system_time();
	host_us += 25'000'000;
	check(get_guest_system_time() == before + (expect_hold ? 0 : 25'000'000));
	if (fail_compile) throw 1;
}

static void resumed()
{
	const u64 before = get_guest_system_time();
	host_us += 100;
	check(get_guest_system_time() == before + 100);
}

namespace glsl { struct program {}; struct program_input {}; }
struct VkComputePipelineCreateInfo {};
struct VkGraphicsPipelineCreateInfo {};
using VkShaderModule = int;
namespace vk
{
struct pipeline_props {};
struct pipe_compiler
{
	using op_flags = unsigned;
	static constexpr op_flags COMPILE_INLINE = 1, COMPILE_DEFERRED = 2;
	using callback_t = std::function<void(std::unique_ptr<glsl::program>&)>;
	using graphics_pipe_create_callback_t = std::function<VkGraphicsPipelineCreateInfo()>;
	struct queue
	{
		template <typename... Args> void push(Args&&...) { host_work(); }
	} m_work_queue;
	template <typename... Args> std::unique_ptr<glsl::program> int_compile_compute_pipe(Args&&...)
	{
		host_work(); return std::make_unique<glsl::program>();
	}
	template <typename... Args> std::unique_ptr<glsl::program> int_compile_graphics_pipe(Args&&...)
	{
		host_work(); return std::make_unique<glsl::program>();
	}
#include "StallPipelineDeclarations.inc"
};
#include "StallPipelineDefinitions.inc"
}

struct shader { unsigned id = 0; };
struct RSXVertexProgram
{
	int key = 1;
	bool operator<(const RSXVertexProgram& other) const { return key < other.key; }
};
struct RSXFragmentProgram
{
	int key = 1;
	void clone_data() const {}
	bool operator<(const RSXFragmentProgram& other) const { return key < other.key; }
};
struct reader_lock { reader_lock(int) {} void upgrade() {} };
struct { void trace(const char*) {} } rsx_log;
namespace rsx
{
struct program_cache_hint_t
{
	shader* vp = nullptr;
	shader* fp = nullptr;
	bool has_vertex_program() const { return vp; }
	bool has_fragment_program() const { return fp; }
	template <typename T> T* get_vertex_program() const { return vp; }
	template <typename T> T* get_fragment_program() const { return fp; }
	static void cache_vertex_program(program_cache_hint_t* hint, const RSXVertexProgram&, shader* value)
	{
		if (hint) hint->vp = value;
	}
	static void cache_fragment_program(program_cache_hint_t* hint, const RSXFragmentProgram&, shader* value)
	{
		if (hint) hint->fp = value;
	}
};
}
struct backend_traits
{
	template <typename... Args> static void recompile_vertex_program(Args&&...) { host_work(); }
	template <typename... Args> static void recompile_fragment_program(Args&&...) { host_work(); }
};
struct shader_cache
{
	using vertex_program_type = shader;
	using fragment_program_type = shader;
	using binary_to_fragment_program = std::map<RSXFragmentProgram, shader>;
	std::map<RSXVertexProgram, shader> m_vertex_shader_cache;
	binary_to_fragment_program m_fragment_shader_cache;
	int m_vertex_mutex = 0, m_fragment_mutex = 0;
	unsigned m_next_id = 0;
#include "StallShaderMethods.inc"
};

struct spu_program { std::vector<u32> data; };
using spu_function_t = void (*)();
static bool empty_analysis = false, null_compilation = false;
struct spu_compiler
{
	spu_program analyse(u32*, u32) { host_work(); return {empty_analysis ? std::vector<u32>{} : std::vector<u32>{1}}; }
};
static spu_function_t compile_spu_llvm_with_retry(spu_compiler*, const spu_program&)
{
	host_work();
	return null_compilation ? nullptr : +[] {};
}
struct spu_thread_stub
{
	spu_compiler* jit;
	u32 pc = 0;
	template <typename T> T* _ptr(u32) { return nullptr; }
};
static void dispatch_compile()
{
	spu_compiler compiler;
	spu_thread_stub spu{&compiler};
#include "StallSPUCompile.inc"
	check(program.data.empty() == empty_analysis);
	check((func == nullptr) == (empty_analysis || null_compilation));
	resumed(); // the real dispatcher takes its non-C++ escape after this scope
}

static void run()
{
	for (bool background : {false, true})
	{
		for (bool failure : {false, true})
		{
			for (unsigned overload = 0; overload < 4; ++overload)
			{
				reset(); emulated_thread = !background; expect_hold = !background; fail_compile = failure;
				vk::pipe_compiler compiler;
				try
				{
					switch (overload)
					{
					case 0: compiler.compile(VkComputePipelineCreateInfo{}, 1, {}, {}); break;
					case 1: compiler.compile(VkGraphicsPipelineCreateInfo{}, 1, {}, {}, {}); break;
					case 2: compiler.compile(vk::pipeline_props{}, 1, 2, 1, {}, {}, {}); break;
					case 3: compiler.compile([] { return VkGraphicsPipelineCreateInfo{}; }, 1, {}, {}, {}); break;
					}
					check(!failure);
				}
				catch (int) { check(failure); }
				resumed();
			}
		}
	}
	fail_compile = false;
	for (unsigned overload : {0, 2, 3})
	{
		reset(); expect_hold = false;
		vk::pipe_compiler compiler;
		switch (overload)
		{
		case 0: check(!compiler.compile(VkComputePipelineCreateInfo{}, 2, {}, {})); break;
		case 2: check(!compiler.compile(vk::pipeline_props{}, 1, 2, 2, {}, {}, {})); break;
		case 3: check(!compiler.compile([] { return VkGraphicsPipelineCreateInfo{}; }, 2, {}, {}, {})); break;
		}
		resumed();
	}
	for (bool background : {false, true})
	{
		reset(); emulated_thread = !background; expect_hold = !background;
		shader_cache cache;
		rsx::program_cache_hint_t hint;
		check(!std::get<1>(cache.search_vertex_program(&hint, {})));
		check(!std::get<1>(cache.search_fragment_program(&hint, {})));
		resumed();
		check(std::get<1>(cache.search_vertex_program(&hint, {})));
		check(std::get<1>(cache.search_fragment_program(&hint, {})));
		check(std::get<1>(cache.search_vertex_program(nullptr, {})));
		check(std::get<1>(cache.search_fragment_program(nullptr, {})));
		resumed();
	}
	for (bool check_only : {false, true})
	{
		for (bool is_being_used_in_emulation : {false, true})
		{
			reset(); expect_hold = !check_only && is_being_used_in_emulation;
			{
#include "StallPPUHold.inc"
				host_work();
			}
			resumed();
		}
	}
	for (bool empty : {false, true})
	{
		for (bool null : {false, true})
		{
			for (bool failure : {false, true})
			{
				reset(); expect_hold = true; fail_compile = failure;
				empty_analysis = empty; null_compilation = null;
				try { dispatch_compile(); check(!failure); }
				catch (int) { check(failure); }
				resumed();
			}
		}
	}
	fail_compile = false;
}
}
