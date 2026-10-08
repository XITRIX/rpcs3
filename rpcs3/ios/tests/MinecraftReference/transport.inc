struct transport_packet
		{
			op type{};
			std::vector<u8> opt_storage{};
			void* src{};
			void* dst{};
			u32 length{};
			u32 aux_param0{};
			u32 aux_param1{};

			transport_packet(void *_dst, void *_src, u32 len)
				: type(op::raw_copy), src(_src), dst(_dst), length(len)
			{}

			transport_packet(void *_dst, std::vector<u8>& _src, u32 len)
				: type(op::vector_copy), opt_storage(std::move(_src)), dst(_dst), length(len)
			{}

			transport_packet(void *_dst, rsx::primitive_type prim, u32 len)
				: type(op::index_emulate), dst(_dst), length(len), aux_param0(static_cast<u8>(prim))
			{}

			transport_packet(u32 command, void* args)
				: type(op::callback), src(args), aux_param0(command)
			{}

			transport_packet(const transport_packet&) = delete;
			transport_packet& operator=(const transport_packet&) = delete;
		};