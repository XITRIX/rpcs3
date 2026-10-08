class weak_vertex_cache : public default_vertex_cache<uploaded_range>
		{
			using storage_type = uploaded_range;

		private:
			rsx::unordered_map<uptr, storage_type> vertex_ranges;

			FORCE_INLINE u64 hash(u32 local_addr, u32 data_length) const
			{
				return u64(local_addr) | (u64(data_length) << 32);
			}

		public:

			const storage_type* find_vertex_range(u32 local_addr, u32 data_length) override
			{
				const auto key = hash(local_addr, data_length);
				const auto found = vertex_ranges.find(key);

				if (found == vertex_ranges.end())
				{
					return nullptr;
				}

				// Check if data in local_address changed vs what was stored in the vertex_cache
				if (auto sudo_ptr = vm::get_super_ptr<char>(local_addr);
					data_length >= 8 && found->second.fingerprint != *utils::bless<u64>(sudo_ptr))
				{
					vertex_ranges.erase(key);
					return nullptr;
				}

				return std::addressof(found->second);
			}

			void store_range(u32 local_addr, u32 data_length, u32 offset_in_heap) override
			{
				storage_type v = {};
				v.data_length = data_length;
				v.local_address = local_addr;
				v.offset_in_heap = offset_in_heap;

				if (auto sudo_ptr = vm::get_super_ptr<char>(local_addr); data_length >= 8)
				{
					v.fingerprint = *utils::bless<u64>(sudo_ptr);
				}

				const auto key = hash(local_addr, data_length);
				vertex_ranges[key] = v;
			}

			void purge() override
			{
				vertex_ranges.clear();
			}
		};
