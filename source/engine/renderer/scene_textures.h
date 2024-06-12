#pragma once
#include "../graphics/context.h"

namespace engine
{
	// 128 * 128 blue noise.
	struct TemporalBlueNoise : NonCopyable
	{
		struct BufferMisc
		{
			// The Sobol sequence buffer.
			std::unique_ptr<VulkanBuffer> sobolBuffer = nullptr;

			// The ranking tile buffer for sampling.
			std::unique_ptr<VulkanBuffer> rankingTileBuffer = nullptr;

			// The scrambling tile buffer for sampling.
			std::unique_ptr<VulkanBuffer> scramblingTileBuffer = nullptr;

			VkDescriptorSet set;
			VkDescriptorSetLayout setLayouts;

			void buildSet();
		};

		BufferMisc spp_1_buffer;
		BufferMisc spp_2_buffer;
		BufferMisc spp_4_buffer;
		BufferMisc spp_8_buffer;
		BufferMisc spp_16_buffer;
		BufferMisc spp_32_buffer;
		BufferMisc spp_64_buffer;
		BufferMisc spp_128_buffer;
		BufferMisc spp_256_buffer;

		explicit TemporalBlueNoise();
	};

	class SharedTextures : NonCopyable
	{
	public:
		std::unique_ptr<VulkanImage> brdfLut = nullptr;
		std::unique_ptr<VulkanImage> cloudBasicNoise = nullptr;
		std::unique_ptr<VulkanImage> cloudDetailNoise = nullptr;
		std::unique_ptr<VulkanImage> imageFallback_RGBA16f = nullptr;

		std::unique_ptr<VulkanBuffer> zeroBuffer = nullptr;

		static const uint32_t kUniformGridVertices16x16Count = 16 * 16 * 3 * 2;
		std::unique_ptr<VulkanBuffer> uniformGridVertices16x16 = nullptr;

		explicit SharedTextures();
	private:
		void compute(VkCommandBuffer cmd);
	};

	struct AtmosphereTextures
	{
		PoolImageSharedRef transmittance = nullptr;
		PoolImageSharedRef skyView       = nullptr;
		PoolImageSharedRef multiScatter  = nullptr;
		PoolImageSharedRef froxelScatter = nullptr;
		PoolImageSharedRef envCapture    = nullptr;

		PoolImageSharedRef distant       = nullptr;
		PoolImageSharedRef distantGrid   = nullptr;

		bool isValid()
		{
			return
				transmittance != nullptr &&
				skyView       != nullptr &&
				multiScatter  != nullptr &&
				froxelScatter != nullptr &&
				envCapture    != nullptr && 
				distant       != nullptr &&
				distantGrid   != nullptr;
		}
	};


	struct GBufferTextures
	{
	public:
		// Scene hdr color.
		PoolImageSharedRef hdrSceneColor = nullptr;
		PoolImageSharedRef hdrSceneColorUpscale = nullptr;
		static auto hdrSceneColorFormat() { return VK_FORMAT_R16G16B16A16_SFLOAT; }

		// Scene depth texutre.
		PoolImageSharedRef depthTexture = nullptr; 
		static auto depthTextureFormat() { return VK_FORMAT_D32_SFLOAT; }

		PoolImageSharedRef gbufferA = nullptr; // GBuffer A: r8g8b8a8 unorm, .rgb store base color.
		inline static auto gbufferAFormat() { return VK_FORMAT_B8G8R8A8_SRGB; }

		PoolImageSharedRef gbufferB = nullptr; // GBuffer B : r10g10b10a2 .rgb store worldspace normal pack.
		inline static auto gbufferBFormat() { return VK_FORMAT_A2B10G10R10_UNORM_PACK32; }

		PoolImageSharedRef gbufferS = nullptr; // GBuffer S: r8g8b8a8 unorm, .r is metal, .g is roughness, .b is mesh ao.
		inline static auto gbufferSFormat() { return VK_FORMAT_R8G8B8A8_UNORM; }

		PoolImageSharedRef gbufferV = nullptr; // GBuffer V: r16g16 sfloat.
		inline static auto gbufferVFormat() { return VK_FORMAT_R16G16_SFLOAT; }

		PoolImageSharedRef gbufferId = nullptr; // Gbuffer Id: r16
		inline static auto gbufferIdFormat() { return VK_FORMAT_R16_UNORM; }

		void clearValue(VkCommandBuffer graphicsCmd);

		PoolImageSharedRef vertexNormal = nullptr;
		PoolImageSharedRef chessboardHalfDepth = nullptr;
		PoolImageSharedRef hzbClosest = nullptr;
		PoolImageSharedRef hzbFurthest = nullptr;

		BufferParameterHandle lensBuffer = nullptr;

		BufferParameterHandle terrainPathDispatchBuffer = nullptr;
		BufferParameterHandle terrainLodCountBuffer = nullptr;
		BufferParameterHandle terrainLodNodeBuffer = nullptr;
		BufferParameterHandle terrainPatchBufferMainView = nullptr;
		BufferParameterHandle terrainDrawArgsMainView = nullptr;

		PoolImageSharedRef terrainLODPatchMap = nullptr;

		/*
			In the context of FSR2, the term "reactivity" means how much influence the samples rendered for the current frame have over the production of the final upscaled image. Typically,
			samples rendered for the current frame contribute a relatively modest amount to the result computed by FSR2; however, there are exceptions. To produce the best results for fast moving,
			alpha-blended objects, FSR2 requires the Reproject & accumulate stage to become more reactive for such pixels. As there is no good way to determine from either color,
			depth or motion vectors which pixels have been rendered using alpha blending, FSR2 performs best when applications explicitly mark such areas.

			Therefore, it is strongly encouraged that applications provide a reactive mask to FSR2.
			The reactive mask guides FSR2 on where it should reduce its reliance on historical information when compositing the current pixel,
			and instead allow the current frame's samples to contribute more to the final result.
			The reactive mask allows the application to provide a value from [0..1] where 0 indicates that the pixel is not at all reactive (and should use the default FSR2 composition strategy),
			and a value of 1 indicates the pixel should be fully reactive.

			While there are other applications for the reactive mask,
			the primary application for the reactive mask is producing better results of upscaling images which include alpha-blended objects.
			A good proxy for reactiveness is actually the alpha value used when compositing an alpha-blended object into the scene,
			therefore, applications should write alpha to the reactive mask.
			It should be noted that it is unlikely that a reactive value of close to 1 will ever produce good results. Therefore, we recommend clamping the maximum reactive value to around 0.9.

			If a Reactive mask is not provided to FSR2 (by setting the reactive field of FfxFsr2DispatchDescription to NULL),
			then an internally generated 1x1 texture with a cleared reactive value will be used.
		*/
		PoolImageSharedRef gbufferUpscaleReactive = nullptr;
		inline static auto gbufferUpscaleReactiveFormat() { return VK_FORMAT_R8_UNORM; }

		/*
			In addition to the Reactive mask, FSR2 provides for the application to denote areas of other specialist rendering which should be accounted for during the upscaling process.
			Examples of such special rendering include areas of raytraced reflections or animated textures.

			While the Reactive mask adjusts the accumulation balance, the Transparency & composition mask adjusts the pixel locks created by FSR2.
			A pixel with a value of 0 in the Transparency & composition mask does not perform any additional modification to the lock for that pixel.
			Conversely, a value of 1 denotes that the lock for that pixel should be completely removed.

			If a Transparency & composition mask is not provided to FSR2 (by setting the transparencyAndComposition field of FfxFsr2DispatchDescription to NULL),
			then an internally generated 1x1 texture with a cleared transparency and composition value will be used.
		*/
		PoolImageSharedRef gbufferUpscaleTranslucencyAndComposition = nullptr;
		inline static auto gbufferUpscaleTranslucencyAndCompositionFormat() { return VK_FORMAT_R8_UNORM; }

	public:
		static GBufferTextures build(
			uint renderWidth, 
			uint renderHeight, 
			uint postWidth, 
			uint postHeight);
	};
}