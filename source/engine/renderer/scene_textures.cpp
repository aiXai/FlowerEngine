#include "scene_textures.h"
#include "renderer.h"
#include "render_scene.h"
#include "deferred_renderer.h"

namespace engine
{
	static const auto kGBufferVkImageUsage =
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
		VK_IMAGE_USAGE_STORAGE_BIT |
		VK_IMAGE_USAGE_SAMPLED_BIT |
		VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
		VK_IMAGE_USAGE_TRANSFER_DST_BIT;

	static const auto kDepthVkImageUsage =
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
		VK_IMAGE_USAGE_SAMPLED_BIT |
		VK_IMAGE_USAGE_TRANSFER_DST_BIT;

	GBufferTextures GBufferTextures::build(
		uint renderWidth, 
		uint renderHeight, 
		uint postWidth, 
		uint postHeight)
	{
		auto& pool = getContext()->getRenderTargetPools();

		GBufferTextures result{ };

		// Render scene hdr color texture.
		result.hdrSceneColor = pool.createPoolImage(
			"HdrSceneColor_RGBA16F", 
			renderWidth, 
			renderHeight, 
			hdrSceneColorFormat(), 
			kGBufferVkImageUsage);

		// Create hdr scene color upscale.
		result.hdrSceneColorUpscale = pool.createPoolImage(
			"HdrSceneColor_Upscale_RGBA16F",
			postWidth,
			postHeight,
			hdrSceneColorFormat(),
			kGBufferVkImageUsage);

		// Create depth texture.
		result.depthTexture = pool.createPoolImage(
			"DepthTexture", 
			renderWidth, 
			renderHeight, 
			depthTextureFormat(), 
			kDepthVkImageUsage);

		result.gbufferA  = pool.createPoolImage("GBufferA", renderWidth, renderHeight, gbufferAFormat(), 
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
			VK_IMAGE_USAGE_SAMPLED_BIT |
			VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
			VK_IMAGE_USAGE_TRANSFER_DST_BIT);
		result.gbufferB  = pool.createPoolImage("GBufferB", renderWidth, renderHeight, gbufferBFormat(), kGBufferVkImageUsage);
		result.gbufferS  = pool.createPoolImage("GBufferS", renderWidth, renderHeight, gbufferSFormat(), kGBufferVkImageUsage);
		result.gbufferV  = pool.createPoolImage("GBufferV", renderWidth, renderHeight, gbufferVFormat(), kGBufferVkImageUsage);
		result.gbufferId = pool.createPoolImage("GBufferId", renderWidth, renderHeight, gbufferIdFormat(), kGBufferVkImageUsage);


		result.gbufferUpscaleTranslucencyAndComposition = 
			pool.createPoolImage("gbufferUpscaleTranslucencyAndComposition", renderWidth, renderHeight, gbufferUpscaleTranslucencyAndCompositionFormat(), kGBufferVkImageUsage);
		result.gbufferUpscaleReactive = 
			pool.createPoolImage("gbufferUpscaleReactive", renderWidth, renderHeight, gbufferUpscaleReactiveFormat(), kGBufferVkImageUsage);

		return result;
	}

	void GBufferTextures::clearValue(VkCommandBuffer graphicsCmd)
	{
		{
			static const auto zeroClear  = VkClearColorValue{ .uint32 = { 0, 0, 0, 0} };
			static const auto rangeClear = buildBasicImageSubresource();

			auto& hdrSceneColor = this->hdrSceneColor->getImage();
			auto& hdrSceneColorUpscale = this->hdrSceneColorUpscale->getImage();
			auto& gbufferA = this->gbufferA->getImage();
			auto& gbufferB = this->gbufferB->getImage();
			auto& gbufferS = this->gbufferS->getImage();
			auto& gbufferV = this->gbufferV->getImage();
			auto& gbufferId = this->gbufferId->getImage();
			auto& gbufferComposition = this->gbufferUpscaleTranslucencyAndComposition->getImage();
			auto& gbufferUpscaleMask = this->gbufferUpscaleReactive->getImage();


			hdrSceneColor.transitionLayout(graphicsCmd, VK_IMAGE_LAYOUT_GENERAL, buildBasicImageSubresource());
			hdrSceneColorUpscale.transitionLayout(graphicsCmd, VK_IMAGE_LAYOUT_GENERAL, buildBasicImageSubresource());
			gbufferA.transitionLayout(graphicsCmd, VK_IMAGE_LAYOUT_GENERAL, buildBasicImageSubresource());
			gbufferB.transitionLayout(graphicsCmd, VK_IMAGE_LAYOUT_GENERAL, buildBasicImageSubresource());
			gbufferS.transitionLayout(graphicsCmd, VK_IMAGE_LAYOUT_GENERAL, buildBasicImageSubresource());
			gbufferV.transitionLayout(graphicsCmd, VK_IMAGE_LAYOUT_GENERAL, buildBasicImageSubresource());
			gbufferId.transitionLayout(graphicsCmd, VK_IMAGE_LAYOUT_GENERAL, buildBasicImageSubresource());
			gbufferComposition.transitionLayout(graphicsCmd, VK_IMAGE_LAYOUT_GENERAL, buildBasicImageSubresource());
			gbufferUpscaleMask.transitionLayout(graphicsCmd, VK_IMAGE_LAYOUT_GENERAL, buildBasicImageSubresource());


			vkCmdClearColorImage(graphicsCmd, hdrSceneColor.getImage(), VK_IMAGE_LAYOUT_GENERAL, &zeroClear, 1, &rangeClear);
			vkCmdClearColorImage(graphicsCmd, hdrSceneColorUpscale.getImage(), VK_IMAGE_LAYOUT_GENERAL, &zeroClear, 1, &rangeClear);
			vkCmdClearColorImage(graphicsCmd, gbufferA.getImage(), VK_IMAGE_LAYOUT_GENERAL, &zeroClear, 1, &rangeClear);
			vkCmdClearColorImage(graphicsCmd, gbufferB.getImage(), VK_IMAGE_LAYOUT_GENERAL, &zeroClear, 1, &rangeClear);
			vkCmdClearColorImage(graphicsCmd, gbufferS.getImage(), VK_IMAGE_LAYOUT_GENERAL, &zeroClear, 1, &rangeClear);
			vkCmdClearColorImage(graphicsCmd, gbufferV.getImage(), VK_IMAGE_LAYOUT_GENERAL, &zeroClear, 1, &rangeClear);
			vkCmdClearColorImage(graphicsCmd, gbufferId.getImage(), VK_IMAGE_LAYOUT_GENERAL, &zeroClear, 1, &rangeClear);
			vkCmdClearColorImage(graphicsCmd, gbufferComposition.getImage(), VK_IMAGE_LAYOUT_GENERAL, &zeroClear, 1, &rangeClear);
			vkCmdClearColorImage(graphicsCmd, gbufferUpscaleMask.getImage(), VK_IMAGE_LAYOUT_GENERAL, &zeroClear, 1, &rangeClear);

			gbufferA.transitionLayout(graphicsCmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, buildBasicImageSubresource());
			gbufferB.transitionLayout(graphicsCmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, buildBasicImageSubresource());
			gbufferS.transitionLayout(graphicsCmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, buildBasicImageSubresource());
			gbufferV.transitionLayout(graphicsCmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, buildBasicImageSubresource());
			gbufferId.transitionLayout(graphicsCmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, buildBasicImageSubresource());
			hdrSceneColor.transitionLayout(graphicsCmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, buildBasicImageSubresource());
			hdrSceneColorUpscale.transitionLayout(graphicsCmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, buildBasicImageSubresource());
			gbufferComposition.transitionLayout(graphicsCmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, buildBasicImageSubresource());
			gbufferUpscaleMask.transitionLayout(graphicsCmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, buildBasicImageSubresource());
		}


		// Clear depth texture.
		{
			static const auto depthClear = VkClearDepthStencilValue{ 0.0f, 1 }; // Reverse z.
			static const auto rangeClearDepth = RHIDefaultImageSubresourceRange(VK_IMAGE_ASPECT_DEPTH_BIT);

			auto& depthZ = this->depthTexture->getImage();
			depthZ.transitionLayoutDepth(graphicsCmd, VK_IMAGE_LAYOUT_GENERAL);

			vkCmdClearDepthStencilImage(
				graphicsCmd, 
				depthZ.getImage(), 
				VK_IMAGE_LAYOUT_GENERAL, 
				&depthClear, 
				1, 
				&rangeClearDepth);

			depthZ.transitionLayoutDepth(graphicsCmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		}

	}

	void TemporalBlueNoise::BufferMisc::buildSet()
	{
		VkDescriptorBufferInfo sobolInfo{};
		sobolInfo.buffer = sobolBuffer->getVkBuffer();
		sobolInfo.offset = 0;
		sobolInfo.range = sobolBuffer->getSize();

		VkDescriptorBufferInfo rankingTileInfo{};
		rankingTileInfo.buffer = rankingTileBuffer->getVkBuffer();
		rankingTileInfo.offset = 0;
		rankingTileInfo.range = rankingTileBuffer->getSize();

		VkDescriptorBufferInfo scramblingTileInfo{};
		scramblingTileInfo.buffer = scramblingTileBuffer->getVkBuffer();
		scramblingTileInfo.offset = 0;
		scramblingTileInfo.range = scramblingTileBuffer->getSize();

		getContext()->descriptorFactoryBegin()
			.bindBuffers(0, 1, &sobolInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kCommonShaderStage)
			.bindBuffers(1, 1, &rankingTileInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kCommonShaderStage)
			.bindBuffers(2, 1, &scramblingTileInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kCommonShaderStage)
			.build(set, setLayouts);
	}

	namespace blueNoise_256_Spp
	{
		// blue noise sampler 256spp.
		#include "samplerCPP/samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_256spp.cpp"
	}

	namespace blueNoise_128_Spp
	{
		// blue noise sampler 128spp.
		#include "samplerCPP/samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_128spp.cpp"
	}
	namespace blueNoise_64_Spp
	{
		// blue noise sampler 64spp.
		#include "samplerCPP/samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_64spp.cpp"
	}
	namespace blueNoise_32_Spp
	{
		// blue noise sampler 32spp.
		#include "samplerCPP/samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_32spp.cpp"
	}

	namespace blueNoise_16_Spp
	{
		// blue noise sampler 16spp.
		#include "samplerCPP/samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_16spp.cpp"
	}

	namespace blueNoise_8_Spp
	{
		// blue noise sampler 8spp.
		#include "samplerCPP/samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_8spp.cpp"
	}

	namespace blueNoise_4_Spp
	{
		// blue noise sampler 4spp.
		#include "samplerCPP/samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_4spp.cpp"
	}

	namespace blueNoise_2_Spp
	{
		// blue noise sampler 2spp.
		#include "samplerCPP/samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_2spp.cpp"
	}

	namespace blueNoise_1_Spp
	{
		// blue noise sampler 1spp.
		#include "samplerCPP/samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_1spp.cpp"
	}

	TemporalBlueNoise::TemporalBlueNoise()
	{
		auto buildBuffer = [](const char* name, void* ptr, VkDeviceSize size)
		{
			return std::make_unique<VulkanBuffer>(
				getContext()->getVMABuffer(),
				name,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				VulkanBuffer::getStageCopyForUploadBufferFlags(),
				size,
				ptr
			);
		};

		#define WORK_CODE \
		{\
			BlueNoiseWorkingBuffer.sobolBuffer = buildBuffer(BlueNoiseWorkingName, (void*)BlueNoiseWorkingSpace::sobol_256spp_256d, sizeof(BlueNoiseWorkingSpace::sobol_256spp_256d));\
			BlueNoiseWorkingBuffer.rankingTileBuffer = buildBuffer(BlueNoiseWorkingName, (void*)BlueNoiseWorkingSpace::rankingTile, sizeof(BlueNoiseWorkingSpace::rankingTile));\
			BlueNoiseWorkingBuffer.scramblingTileBuffer = buildBuffer(BlueNoiseWorkingName, (void*)BlueNoiseWorkingSpace::scramblingTile, sizeof(BlueNoiseWorkingSpace::scramblingTile));\
			BlueNoiseWorkingBuffer.buildSet();\
		}

		#define BlueNoiseWorkingBuffer spp_1_buffer
		#define BlueNoiseWorkingSpace blueNoise_1_Spp
		#define BlueNoiseWorkingName "Sobel_1_spp_buffer"
			WORK_CODE
		#undef BlueNoiseWorkingBuffer
		#undef BlueNoiseWorkingSpace 
		#undef BlueNoiseWorkingName

		#define BlueNoiseWorkingBuffer spp_2_buffer
		#define BlueNoiseWorkingSpace blueNoise_2_Spp
		#define BlueNoiseWorkingName "Sobel_2_spp_buffer"
			WORK_CODE
		#undef BlueNoiseWorkingBuffer
		#undef BlueNoiseWorkingSpace 
		#undef BlueNoiseWorkingName

		#define BlueNoiseWorkingBuffer spp_4_buffer
		#define BlueNoiseWorkingSpace blueNoise_4_Spp
		#define BlueNoiseWorkingName "Sobel_4_spp_buffer"
			WORK_CODE
		#undef BlueNoiseWorkingBuffer
		#undef BlueNoiseWorkingSpace 
		#undef BlueNoiseWorkingName

		#define BlueNoiseWorkingBuffer spp_8_buffer
		#define BlueNoiseWorkingSpace blueNoise_8_Spp
		#define BlueNoiseWorkingName "Sobel_8_spp_buffer"
			WORK_CODE
		#undef BlueNoiseWorkingBuffer
		#undef BlueNoiseWorkingSpace 
		#undef BlueNoiseWorkingName

		#define BlueNoiseWorkingBuffer spp_16_buffer
		#define BlueNoiseWorkingSpace blueNoise_16_Spp
		#define BlueNoiseWorkingName "Sobel_16_spp_buffer"
			WORK_CODE
		#undef BlueNoiseWorkingBuffer
		#undef BlueNoiseWorkingSpace 
		#undef BlueNoiseWorkingName

		#define BlueNoiseWorkingBuffer spp_32_buffer
		#define BlueNoiseWorkingSpace blueNoise_32_Spp
		#define BlueNoiseWorkingName "Sobel_32_spp_buffer"
			WORK_CODE
		#undef BlueNoiseWorkingBuffer
		#undef BlueNoiseWorkingSpace 
		#undef BlueNoiseWorkingName


		#define BlueNoiseWorkingBuffer spp_64_buffer
		#define BlueNoiseWorkingSpace blueNoise_64_Spp
		#define BlueNoiseWorkingName "Sobel_64_spp_buffer"
			WORK_CODE
		#undef BlueNoiseWorkingBuffer
		#undef BlueNoiseWorkingSpace 
		#undef BlueNoiseWorkingName

		#define BlueNoiseWorkingBuffer spp_128_buffer
		#define BlueNoiseWorkingSpace blueNoise_128_Spp
		#define BlueNoiseWorkingName "Sobel_128_spp_buffer"
			WORK_CODE
		#undef BlueNoiseWorkingBuffer
		#undef BlueNoiseWorkingSpace 
		#undef BlueNoiseWorkingName

		#define BlueNoiseWorkingBuffer spp_256_buffer
		#define BlueNoiseWorkingSpace blueNoise_256_Spp
		#define BlueNoiseWorkingName "Sobel_256_spp_buffer"
			WORK_CODE
		#undef BlueNoiseWorkingBuffer
		#undef BlueNoiseWorkingSpace 
		#undef BlueNoiseWorkingName

		#undef WORK_CODE
	}

	SharedTextures::SharedTextures()
	{
		auto cmd = getContext()->createMajorGraphicsCommandBuffer();
		RHICheck(vkResetCommandBuffer(cmd, 0));
		VkCommandBufferBeginInfo cmdBeginInfo = RHICommandbufferBeginInfo(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
		RHICheck(vkBeginCommandBuffer(cmd, &cmdBeginInfo));
		{
			compute(cmd);
		}
		RHICheck(vkEndCommandBuffer(cmd));
		VkPipelineStageFlags waitFlags = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
		RHISubmitInfo cmdSubmitInfo{};
		cmdSubmitInfo.setWaitStage(&waitFlags).setCommandBuffer(&cmd, 1);
		std::vector<VkSubmitInfo> infosRawSubmit{ cmdSubmitInfo };
		getContext()->submitNoFence((uint32_t)infosRawSubmit.size(), infosRawSubmit.data());

		getContext()->waitDeviceIdle();
	}
}