#include "stdafx.h"
#include "metalfx_spatial.h"

#include "../VKResourceManager.h"
#include "../vkutils/device.h"
#include "../vkutils/image.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#pragma clang diagnostic ignored "-Wunguarded-availability-new"
#import <Metal/Metal.h>
#import <MetalFX/MetalFX.h>
#include <vulkan/vulkan_metal.h>
#pragma clang diagnostic pop

namespace vk
{
	struct metal_fx_spatial_upscaler::implementation
	{
		struct configuration
		{
			std::uint32_t input_width = 0;
			std::uint32_t input_height = 0;
			std::uint32_t output_width = 0;
			std::uint32_t output_height = 0;

			bool operator==(const configuration&) const = default;
		};

		VkDevice vk_device = VK_NULL_HANDLE;
		VkQueue vk_queue = VK_NULL_HANDLE;
		PFN_vkExportMetalObjectsEXT export_metal_objects = nullptr;

		id<MTLDevice> metal_device = nil;
		id<MTLCommandQueue> metal_queue = nil;
		id<MTLTexture> input_texture = nil;
		id<MTLTexture> output_texture = nil;
		id<MTLFXSpatialScaler> scaler = nil;

		std::unique_ptr<viewable_image> input;
		std::unique_ptr<viewable_image> output;
		configuration active_configuration{};
		configuration failed_configuration{};
		bool has_failed_configuration = false;
		bool unavailable = false;

		void release_resources(bool defer_images = false)
		{
			if (scaler)
			{
				scaler.colorTexture = nil;
				scaler.outputTexture = nil;
				[scaler release];
				scaler = nil;
			}

			input_texture = nil;
			output_texture = nil;

			if (defer_images)
			{
				auto defer_image = [](std::unique_ptr<viewable_image>& image)
				{
					if (image && image->value)
					{
						vk::get_resource_manager()->dispose(image);
					}
					else
					{
						image.reset();
					}
				};

				defer_image(output);
				defer_image(input);
			}
			else
			{
				output.reset();
				input.reset();
			}

			active_configuration = {};
		}

		void release_all()
		{
			release_resources();
			metal_queue = nil;
			metal_device = nil;
			export_metal_objects = nullptr;
			vk_queue = VK_NULL_HANDLE;
			vk_device = VK_NULL_HANDLE;
			failed_configuration = {};
			has_failed_configuration = false;
			unavailable = false;
		}

		bool prepare_device(render_device& device)
		{
			const VkDevice requested_device = device;
			const VkQueue requested_queue = device.get_graphics_queue();
			if (vk_device == requested_device && vk_queue == requested_queue && metal_device && metal_queue)
			{
				return true;
			}

			release_all();
			vk_device = requested_device;
			vk_queue = requested_queue;

			if (!device.get_metal_objects_support())
			{
				rsx_log.error("MetalFX Spatial is unavailable because VK_EXT_metal_objects is not supported.");
				unavailable = true;
				return false;
			}

			export_metal_objects = reinterpret_cast<PFN_vkExportMetalObjectsEXT>(
				vkGetDeviceProcAddr(vk_device, "vkExportMetalObjectsEXT"));
			if (!export_metal_objects)
			{
				rsx_log.error("MetalFX Spatial could not load vkExportMetalObjectsEXT.");
				unavailable = true;
				return false;
			}

			VkExportMetalCommandQueueInfoEXT queue_info{
				.sType = VK_STRUCTURE_TYPE_EXPORT_METAL_COMMAND_QUEUE_INFO_EXT,
				.pNext = nullptr,
				.queue = vk_queue,
				.mtlCommandQueue = nil,
			};
			VkExportMetalDeviceInfoEXT device_info{
				.sType = VK_STRUCTURE_TYPE_EXPORT_METAL_DEVICE_INFO_EXT,
				.pNext = &queue_info,
				.mtlDevice = nil,
			};
			VkExportMetalObjectsInfoEXT objects_info{
				.sType = VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECTS_INFO_EXT,
				.pNext = &device_info,
			};
			export_metal_objects(vk_device, &objects_info);

			metal_device = device_info.mtlDevice;
			metal_queue = queue_info.mtlCommandQueue;
			if (!metal_device || !metal_queue || ![MTLFXSpatialScalerDescriptor supportsDevice:metal_device])
			{
				rsx_log.error("MetalFX Spatial is not supported by this Metal device.");
				unavailable = true;
				return false;
			}

			return true;
		}

		bool prepare_resources(render_device& device, const configuration& requested)
		{
			if (scaler && requested == active_configuration)
			{
				return true;
			}

			if (has_failed_configuration && requested == failed_configuration)
			{
				return false;
			}

			release_resources(scaler != nil);
			failed_configuration = requested;
			has_failed_configuration = true;

			const VkImageUsageFlags image_usage =
				VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
				VK_IMAGE_USAGE_TRANSFER_DST_BIT |
				VK_IMAGE_USAGE_SAMPLED_BIT |
				VK_IMAGE_USAGE_STORAGE_BIT |
				VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

			input = std::make_unique<viewable_image>(
				device,
				device.get_memory_mapping().device_local,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				VK_IMAGE_TYPE_2D,
				VK_FORMAT_B8G8R8A8_UNORM,
				requested.input_width,
				requested.input_height,
				1, 1, 1,
				VK_SAMPLE_COUNT_1_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_TILING_OPTIMAL,
				image_usage,
				0,
				VMM_ALLOCATION_POOL_SYSTEM);

			output = std::make_unique<viewable_image>(
				device,
				device.get_memory_mapping().device_local,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				VK_IMAGE_TYPE_2D,
				VK_FORMAT_B8G8R8A8_UNORM,
				requested.output_width,
				requested.output_height,
				1, 1, 1,
				VK_SAMPLE_COUNT_1_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_TILING_OPTIMAL,
				image_usage,
				0,
				VMM_ALLOCATION_POOL_SYSTEM);

			input->set_debug_name("MetalFX Spatial input");
			output->set_debug_name("MetalFX Spatial output");

			VkExportMetalTextureInfoEXT output_info{
				.sType = VK_STRUCTURE_TYPE_EXPORT_METAL_TEXTURE_INFO_EXT,
				.pNext = nullptr,
				.image = output->value,
				.imageView = VK_NULL_HANDLE,
				.bufferView = VK_NULL_HANDLE,
				.plane = VK_IMAGE_ASPECT_COLOR_BIT,
				.mtlTexture = nil,
			};
			VkExportMetalTextureInfoEXT input_info{
				.sType = VK_STRUCTURE_TYPE_EXPORT_METAL_TEXTURE_INFO_EXT,
				.pNext = &output_info,
				.image = input->value,
				.imageView = VK_NULL_HANDLE,
				.bufferView = VK_NULL_HANDLE,
				.plane = VK_IMAGE_ASPECT_COLOR_BIT,
				.mtlTexture = nil,
			};
			VkExportMetalObjectsInfoEXT objects_info{
				.sType = VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECTS_INFO_EXT,
				.pNext = &input_info,
			};
			export_metal_objects(vk_device, &objects_info);

			input_texture = input_info.mtlTexture;
			output_texture = output_info.mtlTexture;
			if (!input_texture || !output_texture ||
				input_texture.storageMode != MTLStorageModePrivate ||
				output_texture.storageMode != MTLStorageModePrivate)
			{
				rsx_log.error("MetalFX Spatial could not export private Metal textures from MoltenVK.");
				release_resources();
				return false;
			}

			@autoreleasepool
			{
				MTLFXSpatialScalerDescriptor* descriptor = [[MTLFXSpatialScalerDescriptor alloc] init];
				descriptor.colorTextureFormat = input_texture.pixelFormat;
				descriptor.outputTextureFormat = output_texture.pixelFormat;
				descriptor.inputWidth = requested.input_width;
				descriptor.inputHeight = requested.input_height;
				descriptor.outputWidth = requested.output_width;
				descriptor.outputHeight = requested.output_height;
				descriptor.colorProcessingMode = MTLFXSpatialScalerColorProcessingModePerceptual;
				scaler = [descriptor newSpatialScalerWithDevice:metal_device];
				[descriptor release];
			}

			if (!scaler ||
				(input_texture.usage & scaler.colorTextureUsage) != scaler.colorTextureUsage ||
				(output_texture.usage & scaler.outputTextureUsage) != scaler.outputTextureUsage)
			{
				rsx_log.error("MetalFX Spatial rejected the requested texture format or usage.");
				release_resources();
				return false;
			}

			scaler.inputContentWidth = requested.input_width;
			scaler.inputContentHeight = requested.input_height;
			scaler.colorTexture = input_texture;
			scaler.outputTexture = output_texture;
			active_configuration = requested;
			has_failed_configuration = false;

			rsx_log.notice(
				"MetalFX Spatial initialized at %ux%u -> %ux%u.",
				requested.input_width,
				requested.input_height,
				requested.output_width,
				requested.output_height);
			return true;
		}
	};

	metal_fx_spatial_upscaler::metal_fx_spatial_upscaler()
		: m_impl(std::make_unique<implementation>())
	{
	}

	metal_fx_spatial_upscaler::~metal_fx_spatial_upscaler()
	{
		reset();
	}

	bool metal_fx_spatial_upscaler::prepare(
		render_device& device,
		std::uint32_t input_width,
		std::uint32_t input_height,
		std::uint32_t output_width,
		std::uint32_t output_height)
	{
		if (!input_width || !input_height || !output_width || !output_height ||
			output_width < input_width || output_height < input_height)
		{
			return false;
		}

		if (m_impl->unavailable || !m_impl->prepare_device(device))
		{
			return false;
		}

		return m_impl->prepare_resources(device, {
			.input_width = input_width,
			.input_height = input_height,
			.output_width = output_width,
			.output_height = output_height,
		});
	}

	bool metal_fx_spatial_upscaler::encode()
	{
		if (!m_impl->scaler || !m_impl->metal_queue || !m_impl->input_texture || !m_impl->output_texture)
		{
			return false;
		}

		@autoreleasepool
		{
			id<MTLCommandBuffer> command_buffer = [m_impl->metal_queue commandBuffer];
			if (!command_buffer)
			{
				rsx_log.error("MetalFX Spatial could not allocate a Metal command buffer.");
				return false;
			}

			command_buffer.label = @"RPCS3 MetalFX Spatial";
			[m_impl->scaler encodeToCommandBuffer:command_buffer];
			[command_buffer commit];
		}

		return true;
	}

	viewable_image* metal_fx_spatial_upscaler::input_image() const
	{
		return m_impl->input.get();
	}

	viewable_image* metal_fx_spatial_upscaler::output_image() const
	{
		return m_impl->output.get();
	}

	void metal_fx_spatial_upscaler::reset()
	{
		if (m_impl)
		{
			m_impl->release_all();
		}
	}
}
