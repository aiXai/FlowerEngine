#include "asset_texture.h"
#include "../ui/ui.h"

#include <rttr/registration.h>
#include <nameof/nameof.hpp>
#include "asset_manager.h"
#include <stb/stb_image.h>
#include <stb/stb_image_resize.h>
#include <renderer/render_scene.h>

namespace engine
{
	template<typename T> 
	inline float getQuantifySize() 
	{ 
		CHECK(false);
		return 0.0f; 
	}

	template<> 
	inline float getQuantifySize<uint8_t>() 
	{ 
		return float(1 << 8) - 1.0f; 
	}

	template<> 
	inline float getQuantifySize<uint16_t>() 
	{ 
		return float(1 << 16) - 1.0f; 
	}

	template<> 
	inline float getQuantifySize<float>() 
	{
		return 1.0f; 
	}

	// Genric mipmap data generation.
	template<typename T>
	void buildMipmapData(
		T* srcPixels, 
		const AssetTexture& meta, 
		AssetTextureBin& outBinData, 
		uint32_t channelCount, 
		uint32_t channelOffset)
	{
		const float kQuantitySize = getQuantifySize<T>();

		if (meta.getAlphaMipmapCutOff() < 1.0f)
		{
			LOG_WARN("Texture {0} build mipmap with alpha cutoff {1}, but don't support alpha scale yet.", 
				meta.getName(), meta.getAlphaMipmapCutOff());
		}

		if (meta.isSRGB())
		{
			LOG_WARN("Texture {0} build mipmap require srgb convert, but don't support here!",
				meta.getName());
		}

		// Prepare mipmap datas.
		outBinData.mipmapDatas.resize(meta.getMipmapCount());

		// Get strip size.
		const auto kStripSize = sizeof(T) * channelCount;

		// Now build each mipmap.
		for (size_t mip = 0; mip < outBinData.mipmapDatas.size(); mip++)
		{
			auto& destMipData = outBinData.mipmapDatas[mip];

			// Allocate memory.
			uint32_t destWidth  = math::max<uint32_t>(meta.getDimension().x >> mip, 1);
			uint32_t destHeight = math::max<uint32_t>(meta.getDimension().y >> mip, 1);
			destMipData.resize(destWidth * destHeight * kStripSize);

			T* pDestData = (T*)destMipData.data();

			if (mip == 0)
			{
				for (size_t i = 0; i < destWidth * destHeight; i++)
				{
					for (size_t j = 0; j < channelCount; j++)
					{
						pDestData[i * channelCount + j] = srcPixels[i * 4 + j + channelOffset];
					}
				}
			}
			else
			{
				const size_t srcMip = mip - 1;
				const auto& srcMipData = outBinData.mipmapDatas[srcMip];
				for (size_t y = 0; y < destHeight; y++)
				{
					for (size_t x = 0; x < destWidth; x++)
					{
						// Get src data.
						uint32_t srcWidth  = std::max<uint32_t>(meta.getDimension().x >> srcMip, 1);
						uint32_t srcHeight = std::max<uint32_t>(meta.getDimension().y >> srcMip, 1);

						// Clamp src data fetech edge.
						size_t srcX0 = (size_t)std::min<uint32_t>(uint32_t(x * 2 + 0), srcWidth  - 1);
						size_t srcX1 = (size_t)std::min<uint32_t>(uint32_t(x * 2 + 1), srcWidth  - 1);
						size_t srcY0 = (size_t)std::min<uint32_t>(uint32_t(y * 2 + 0), srcHeight - 1);
						size_t srcY1 = (size_t)std::min<uint32_t>(uint32_t(y * 2 + 1), srcHeight - 1);
						size_t srcPixelStart[] =
						{
							(srcY0 * srcWidth + srcX0) * channelCount, // X0Y0
							(srcY0 * srcWidth + srcX1) * channelCount, // X1Y0
							(srcY1 * srcWidth + srcX0) * channelCount, // X0Y1
							(srcY1 * srcWidth + srcX1) * channelCount, // X1Y1
						};

						const T* pSrcData = (const T*)srcMipData.data();
						size_t destPixelPosStart = (y * destWidth + x) * channelCount;

						for (size_t channelId = 0; channelId < channelCount; channelId++)
						{
							float sumValue = 0.0f;
							for (size_t srcPixelId = 0; srcPixelId < 4; srcPixelId++)
							{
								const T& valueLoad = pSrcData[srcPixelStart[srcPixelId] + channelId];
								const float v = float(valueLoad) / kQuantitySize;

								sumValue += v;
							}
							sumValue *= 0.25f;

							pDestData[destPixelPosStart + channelId] = T(sumValue * kQuantitySize);
						}
					}
				}
			}
			CHECK(destHeight * destWidth * kStripSize == destMipData.size());
		}
	}

	static inline void getChannelCountOffset(uint32_t& channelCount, uint32_t& pixelSampleOffset, ETextureFormat format)
	{
		switch (format)
		{
		case engine::ETextureFormat::RGBA16Unorm:
		case engine::ETextureFormat::R8G8B8A8:
		case engine::ETextureFormat::BC3:
		case engine::ETextureFormat::BC1:
		{ 
			channelCount = 4; 
			pixelSampleOffset = 0; 
			return;
		}
		case engine::ETextureFormat::BC5:
		case engine::ETextureFormat::R8G8:
		{
			channelCount = 2;
			pixelSampleOffset = 0;
			return;
		}
		case engine::ETextureFormat::Greyscale:
		case engine::ETextureFormat::BC4Greyscale:
		case engine::ETextureFormat::R8:
		case engine::ETextureFormat::BC4R8:
		case engine::ETextureFormat::R16Unorm:
		{
			channelCount = 1;
			pixelSampleOffset = 0;
			return;
		}
		case engine::ETextureFormat::G8:
		case engine::ETextureFormat::BC4G8:
		{
			channelCount = 1;
			pixelSampleOffset = 1;
			return;
		}
		case engine::ETextureFormat::B8:
		case engine::ETextureFormat::BC4B8:
		{
			channelCount = 1;
			pixelSampleOffset = 2;
			return;
		}
		case engine::ETextureFormat::A8:
		case engine::ETextureFormat::BC4A8:
		{
			channelCount = 1;
			pixelSampleOffset = 3;
			return;
		}
		}

		CHECK_ENTRY();
	}

	static void drawTextureImportConfig(
		std::shared_ptr<AssetImportConfigInterface> ptr)
	{
		auto config = std::static_pointer_cast<AssetTextureImportConfig>(ptr);

		ImGui::Spacing();
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
		ImGui::PushID(std::hash<std::string>{}(config->path.second.string()));
		ImGui::Indent();
		{
			std::string utf8Name = utf8::utf16to8(config->path.first.u16string());
			std::string saveUtf8 = utf8::utf16to8(config->path.second.u16string());

			ImGui::TextDisabled(std::format("Load from: {}", utf8Name).c_str());
			ImGui::TextDisabled(std::format("Save to: {}", saveUtf8).c_str());
			ImGui::Spacing();

			const float sizeLable = ImGui::GetFontSize();

			if (ImGui::BeginTable("##ConfigTable", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_Borders))
			{
				ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);

				ImGui::TableNextRow(); 
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("Is sRGB");      
				ImGui::TableSetColumnIndex(1);
				ImGui::Checkbox("##SRGB", &config->bSRGB);

				ImGui::TableNextRow(); 
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("Build Mipmap"); 
				ImGui::TableSetColumnIndex(1);
				ImGui::Checkbox("##MipMap", &config->bGenerateMipmap);

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("Alpha Cutoff");
				ImGui::TableSetColumnIndex(1);
				ImGui::SliderFloat("##AlphaCutoff", &config->alphaMipmapCutoff, 0.0f, 1.0f, "%.2f");

				int formatValue = (int)config->format;

				std::array<std::string, (size_t)ETextureFormat::Max> formatList { };
				std::array<const char*, (size_t)ETextureFormat::Max> formatListChar{ };
				for (size_t i = 0; i < formatList.size(); i++)
				{
					std::string prefix = (formatValue == i) ? "  * " : "    ";
					formatList[i] = std::format("{0} {1}", prefix, nameof::nameof_enum(ETextureFormat(i)));
					formatListChar[i] = formatList[i].c_str();
				}

				ImGui::TableNextRow(); 
				ImGui::TableSetColumnIndex(0); 
				ImGui::Text("Format");
				ImGui::TableSetColumnIndex(1); 
				ImGui::Combo("##Format", &formatValue, formatListChar.data(), formatListChar.size());
				config->format = ETextureFormat(formatValue);

				ImGui::EndTable();
			}
		}
		ImGui::Unindent();
		ImGui::PopStyleVar();
		ImGui::PopID();

		ImGui::NewLine();
		ImGui::Separator();
	}

	VkFormat getFormatFromConfig(const AssetTextureImportConfig& config, bool bCanCompressed)
	{
		if (config.format == ETextureFormat::R16Unorm)
		{
			return VK_FORMAT_R16_UNORM;
		}

		if (config.format == ETextureFormat::RGBA16Unorm)
		{
			return VK_FORMAT_R16G16B16A16_UNORM;
		}

		if (config.format == ETextureFormat::R8G8B8A8)
		{
			return config.bSRGB ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
		}
		if (ETextureFormat::R8G8 == config.format) return VK_FORMAT_R8G8_UNORM;
		if (ETextureFormat::Greyscale == config.format || 
			ETextureFormat::R8 == config.format ||
			ETextureFormat::B8 == config.format ||
			ETextureFormat::G8 == config.format ||
			ETextureFormat::A8 == config.format) return VK_FORMAT_R8_UNORM;

		if (ETextureFormat::BC3 == config.format)
		{
			if (bCanCompressed)
			{
				return config.bSRGB ? VK_FORMAT_BC3_SRGB_BLOCK : VK_FORMAT_BC3_UNORM_BLOCK;
			}
			return config.bSRGB ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
		}

		if (ETextureFormat::BC1 == config.format)
		{
			if (bCanCompressed)
			{
				// Don't care BC1's alpha which quality poorly.
				return config.bSRGB ? VK_FORMAT_BC1_RGB_SRGB_BLOCK : VK_FORMAT_BC1_RGB_UNORM_BLOCK;
			}
			return config.bSRGB ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
		}

		if (ETextureFormat::BC4Greyscale == config.format ||
			ETextureFormat::BC4R8 == config.format ||
			ETextureFormat::BC4G8 == config.format ||
			ETextureFormat::BC4B8 == config.format ||
			ETextureFormat::BC4A8 == config.format)
		{
			if (bCanCompressed)
			{
				return VK_FORMAT_BC4_UNORM_BLOCK;
			}
			return VK_FORMAT_R8_UNORM;
		}

		if (ETextureFormat::BC5 == config.format)
		{
			if (bCanCompressed)
			{
				return VK_FORMAT_BC5_UNORM_BLOCK;
			}
			return VK_FORMAT_R8G8_UNORM;
		}

		CHECK_ENTRY();
		return VK_FORMAT_R8_UNORM;
	}

	static bool loadLdrTexture(std::shared_ptr<AssetImportConfigInterface> ptr)
	{
		const std::filesystem::path& srcPath = ptr->path.first;
		const std::filesystem::path& savePath = ptr->path.second;
		const auto texPath = utf8::utf16to8(srcPath.u16string());

		std::shared_ptr<AssetTexture> texturePtr;

		auto config = std::static_pointer_cast<AssetTextureImportConfig>(ptr);

		// Load texture.
		int32_t texWidth, texHeight, texChannels;

		uint32_t channelCount;
		uint32_t pixelSampleOffset;
		getChannelCountOffset(channelCount, pixelSampleOffset, config->format);

		stbi_uc* pixels = stbi_load(srcPath.string().c_str(), &texWidth, &texHeight, &texChannels, 4);
		{
			if (!pixels)
			{
				return false;
			}

			// Texture is power of two.
			const bool bPOT = isPOT(texWidth) && isPOT(texHeight);
			const int rawDataSize = texWidth * texHeight * channelCount;

			// Build texture ptr.
			const auto name = savePath.filename().u16string() + utf8::utf8to16(AssetTexture::getCDO()->getSuffix());

			const auto relativePathUtf8 =
				buildRelativePathUtf8(getAssetManager()->getProjectConfig().assetPath, savePath.parent_path());

			AssetSaveInfo saveInfo(utf8::utf16to8(name), relativePathUtf8);

			texturePtr = getAssetManager()->createAsset<AssetTexture>(saveInfo).lock();
			texturePtr->markDirty();

			// Copy raw asset to project asset.
			{
				auto copyDest = savePath.u16string() + srcPath.filename().extension().u16string();
				ASSERT(!std::filesystem::exists(copyDest), "Can't copy same resource multi times.");
				std::filesystem::copy(srcPath, copyDest);
				std::filesystem::path copyDestPath = copyDest;

				texturePtr->m_rawAssetPath = buildRelativePathUtf8(getAssetManager()->getProjectConfig().assetPath, copyDestPath);
			}

			// Override mipmap state by texture dimension.
			config->bGenerateMipmap = bPOT ? config->bGenerateMipmap : false;

			// Only compressed bc when src image all dimension is small than 4.
			const bool bCanCompressed = bPOT && (texWidth >= 4) && (texHeight >= 4);

			texturePtr->initBasicInfo(
				config->bSRGB,
				config->bGenerateMipmap ? getMipLevelsCount(texWidth, texHeight) : 1U,
				getFormatFromConfig(*config, bCanCompressed),
				{ texWidth, texHeight, 1 },
				config->alphaMipmapCutoff);

			// Build snapshot.
			{
				std::vector<uint8_t> data{};
				texturePtr->buildSnapshot(data, pixels, 4);
				saveAsset(data, texturePtr->getSnapshotPath(), false);
			}

			// Build mipmap.
			{
				AssetTextureBin bin{};
				buildMipmapData8Bit(channelCount, pixelSampleOffset, pixels, bin,
					texturePtr->m_bSRGB,
					texturePtr->m_mipmapCount,
					texturePtr->m_dimension.x,
					texturePtr->m_dimension.y,
					texturePtr->m_alphaMipmapCutoff);

				switch (texturePtr->getFormat())
				{
				case VK_FORMAT_R8G8B8A8_UNORM:
				case VK_FORMAT_R8G8B8A8_SRGB:
				case VK_FORMAT_R8G8_UNORM:
				{
					// Do nothing.
				}
				break;
				case VK_FORMAT_BC3_UNORM_BLOCK:
				case VK_FORMAT_BC3_SRGB_BLOCK:
				case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
				case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
				case VK_FORMAT_BC5_UNORM_BLOCK:
				case VK_FORMAT_BC4_UNORM_BLOCK:
				{
					CHECK(bCanCompressed);
					mipmapCompressBC(bin, *texturePtr);
				}
				break;
				default: UN_IMPLEMENT();
				}

				saveAsset(bin, texturePtr->getBinPath(), false);
			}
		}
		stbi_image_free(pixels);

		return texturePtr->save();
	}

	static bool importTextureFromConfigThreadSafe(
		std::shared_ptr<AssetImportConfigInterface> ptr)
	{
		const std::filesystem::path& srcPath = ptr->path.first;
		const std::filesystem::path& savePath = ptr->path.second;
		const auto texPath = utf8::utf16to8(srcPath.u16string());

		auto config = std::static_pointer_cast<AssetTextureImportConfig>(ptr);

		// Build texture ptr.
		const auto name = savePath.filename().u16string() + utf8::utf8to16(AssetTexture::getCDO()->getSuffix());
		const auto relativePathUtf8 =
			buildRelativePathUtf8(getAssetManager()->getProjectConfig().assetPath, savePath.parent_path());

		// Create asset texture.
		AssetSaveInfo saveInfo(utf8::utf16to8(name), relativePathUtf8);
		std::shared_ptr<AssetTexture> texturePtr = getAssetManager()->createAsset<AssetTexture>(saveInfo).lock();
		texturePtr->markDirty();

		uint32_t channelCount;
		uint32_t pixelSampleOffset;
		getChannelCountOffset(channelCount, pixelSampleOffset, config->format);

		auto importLdrTexture = [&]() -> bool
		{
			int32_t texWidth, texHeight, texChannels;
			stbi_uc* pixels = stbi_load(srcPath.string().c_str(), &texWidth, &texHeight, &texChannels, 4);
			{
				if (!pixels)
				{
					return false;
				}

				// Texture is power of two.
				const bool bPOT = isPOT(texWidth) && isPOT(texHeight);

				// Override mipmap state by texture dimension.
				config->bGenerateMipmap = bPOT ? config->bGenerateMipmap : false;

				// Only compressed bc when src image all dimension is small than 4.
				const bool bCanCompressed = bPOT && (texWidth >= 4) && (texHeight >= 4);

				texturePtr->initBasicInfo(
					config->bSRGB,
					config->bGenerateMipmap ? getMipLevelsCount(texWidth, texHeight) : 1U,
					getFormatFromConfig(*config, bCanCompressed),
					{ texWidth, texHeight, 1 },
					config->alphaMipmapCutoff);

				// Build snapshot.
				{
					std::vector<uint8_t> data{};
					texturePtr->buildSnapshot(data, pixels, 4);
					saveAsset(data, texturePtr->getSnapshotPath(), false);
				}

				// Build mipmap.
				{
					AssetTextureBin bin{};
					buildMipmapData8Bit(channelCount, pixelSampleOffset, pixels, bin,
						texturePtr->m_bSRGB,
						texturePtr->m_mipmapCount,
						texturePtr->m_dimension.x,
						texturePtr->m_dimension.y,
						texturePtr->m_alphaMipmapCutoff);

					switch (texturePtr->getFormat())
					{
					case VK_FORMAT_R8G8B8A8_UNORM:
					case VK_FORMAT_R8G8B8A8_SRGB:
					case VK_FORMAT_R8G8_UNORM:
					{
						// Do nothing.
					}
					break;
					case VK_FORMAT_BC3_UNORM_BLOCK:
					case VK_FORMAT_BC3_SRGB_BLOCK:
					case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
					case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
					case VK_FORMAT_BC5_UNORM_BLOCK:
					case VK_FORMAT_BC4_UNORM_BLOCK:
					{
						CHECK(bCanCompressed);
						mipmapCompressBC(bin, *texturePtr);
					}
					break;
					default: UN_IMPLEMENT();
					}

					saveAsset(bin, texturePtr->getBinPath(), false);
				}
			}
			stbi_image_free(pixels);

			return true;
		};

		auto importHalfTexture = [&]() -> bool
		{
			int32_t texWidth, texHeight, texChannels;
			stbi_us* pixels = stbi_load_16(srcPath.string().c_str(), &texWidth, &texHeight, &texChannels, 4);
			if (!pixels)
			{
				return false;
			}

			// Texture is power of two.
			const bool bPOT = isPOT(texWidth) && isPOT(texHeight);

			// Override mipmap state by texture dimension.
			config->bGenerateMipmap = bPOT ? config->bGenerateMipmap : false;

			texturePtr->initBasicInfo(
				false, // No srgb for half texture.
				config->bGenerateMipmap ? getMipLevelsCount(texWidth, texHeight) : 1U,
				getFormatFromConfig(*config, false),
				{ texWidth, texHeight, 1 },
				1.0f); // No alpha cutoff for mipmap.

			// Build snapshot.
			{
				uint32_t widthSnapShot;
				uint32_t heightSnapShot;
				AssetSnapshot::quantifySnapshotDim(widthSnapShot, heightSnapShot, texWidth, texHeight);

				std::vector<uint16_t> snapshotData;
				snapshotData.resize(widthSnapShot * heightSnapShot * 4);

				// Do srgb convert for all texture, so they will looks same with browser editor.
				stbir_resize_uint16_generic(
					pixels,
					texWidth,
					texHeight,
					0,
					snapshotData.data(),
					widthSnapShot,
					heightSnapShot,
					0,
					4,
					3,
					0,
					STBIR_EDGE_CLAMP,
					STBIR_FILTER_DEFAULT,
					STBIR_COLORSPACE_LINEAR,
					nullptr
				);

				std::vector<uint8_t> ldrDatas;
				ldrDatas.resize(snapshotData.size());
				for (size_t i = 0; i < ldrDatas.size(); i++)
				{
					ldrDatas[i] = uint8_t(float(snapshotData[i]) / 65535.0f * 255.0f);
				}

				saveAsset(ldrDatas, texturePtr->getSnapshotPath(), false);
			}

			// Build mipmap.
			{
				AssetTextureBin bin{};
				buildMipmapData<uint16_t>(pixels, *texturePtr, bin, channelCount, pixelSampleOffset);
				saveAsset(bin, texturePtr->getBinPath(), false);
			}

			stbi_image_free(pixels);
			return true;
		};

		bool bImportSucceed = false;
		switch (config->format)
		{
		case ETextureFormat::R8G8B8A8:
		case ETextureFormat::BC3:
		case ETextureFormat::BC1:
		case ETextureFormat::BC5:
		case ETextureFormat::R8G8:
		case ETextureFormat::Greyscale:
		case ETextureFormat::R8:
		case ETextureFormat::G8:
		case ETextureFormat::B8:
		case ETextureFormat::A8:
		case ETextureFormat::BC4Greyscale:
		case ETextureFormat::BC4R8:
		case ETextureFormat::BC4G8:
		case ETextureFormat::BC4B8:
		case ETextureFormat::BC4A8:
		{
			bImportSucceed = importLdrTexture();
			break;
		}
		case ETextureFormat::RGBA16Unorm:
		case ETextureFormat::R16Unorm:
		{
			bImportSucceed = importHalfTexture();
			break;
		}
		default:
		{
			CHECK_ENTRY();
			break;
		}
		}

		// Copy raw asset to project asset.
		if (bImportSucceed)
		{
			auto copyDest = savePath.u16string() + srcPath.filename().extension().u16string();
			ASSERT(!std::filesystem::exists(copyDest), "Can't copy same resource multi times.");
			std::filesystem::copy(srcPath, copyDest);
			std::filesystem::path copyDestPath = copyDest;

			texturePtr->m_rawAssetPath = buildRelativePathUtf8(getAssetManager()->getProjectConfig().assetPath, copyDestPath);
		}

		return texturePtr->save();
	}


	AssetTexture::AssetTexture(const AssetSaveInfo& saveInfo)
		: AssetInterface(saveInfo)
	{

	}

	void AssetTexture::onPostAssetConstruct()
	{
		
	}

	VulkanImage* AssetTexture::getImage()
	{
		return getGPUImage().lock()->getReadyImage();
	}

	std::weak_ptr<GPUImageAsset> AssetTexture::getGPUImage()
	{
		if (!m_cacheImage.lock())
		{
			if (getSaveInfo().isBuiltin())
			{
				m_cacheImage = getContext()->getBuiltinTexture(getSaveInfo().getName());
			}
			else
			{
				if (!getContext()->isLRUAssetExist(getBinUUID()))
				{
					auto newTask = buildTextureLoadTask();
					getContext()->getAsyncUploader().addTask(newTask);
				}

				m_cacheImage = std::dynamic_pointer_cast<GPUImageAsset>(getContext()->getLRU()->tryGet(getBinUUID()));
			}
		}

		return m_cacheImage;
	}

	VulkanImage* AssetTexture::getSnapshotImage()
	{
		if (!m_cacheSnapshotImage.lock())
		{
			if (!getContext()->isLRUAssetExist(getSnapshotUUID()))
			{
				auto newTask = buildSnapShotLoadTask();
				getContext()->getAsyncUploader().addTask(newTask);
			}

			m_cacheSnapshotImage = std::dynamic_pointer_cast<GPUImageAsset>(getContext()->getLRU()->tryGet(getSnapshotUUID()));
		}

		return m_cacheSnapshotImage.lock()->getReadyImage();
	}

	const AssetReflectionInfo& AssetTexture::uiGetAssetReflectionInfo()
	{
		const static AssetReflectionInfo kInfo =
		{
			.name = "Texture",
			.icon = ICON_FA_IMAGE,
			.decoratedName = std::string("  ") + ICON_FA_IMAGE + std::string("    Texture"),
			.importConfig =
			{
				.bImportable = true,
				.importRawAssetExtension = "jpg,jpeg,png,tga,exr;jpg,jpeg;png;tga;exr",
				.buildAssetImportConfig = []() { return std::make_shared<AssetTextureImportConfig>(); },
				.drawAssetImportConfig = [](AssetReflectionInfo::ImportConfigPtr ptr)
				{
					drawTextureImportConfig(ptr);
				},
				.importAssetFromConfigThreadSafe = [](AssetReflectionInfo::ImportConfigPtr ptr)
				{
					return importTextureFromConfigThreadSafe(ptr);
				},
			}
		};
		return kInfo;
	}

	const AssetTexture* AssetTexture::getCDO()
	{
		static AssetTexture texture { };
		return &texture;
	}

	bool AssetTexture::saveImpl()
	{
		// Only save meta data.
		std::shared_ptr<AssetInterface> asset = getptr<AssetTexture>();
		return saveAsset(asset, getSavePath(), false);
	}

	void AssetTexture::unloadImpl()
	{

	}

	void SnapshotAssetTextureLoadTask::uploadFunction(
		uint32_t stageBufferOffset,
		void* bufferPtrStart,
		RHICommandBufferBase& commandBuffer,
		VulkanBuffer& stageBuffer)
	{
		std::vector<uint8_t> snapshotData { };
		if (!std::filesystem::exists(cacheAsset->getSnapshotPath()))
		{
			// TODO:
			UN_IMPLEMENT();
		}
		else
		{
			LOG_TRACE("Found snapshot for asset {} cache in disk so just load.", 
				utf8::utf16to8(cacheAsset->getSaveInfo().getStorePath()));

			// Just load from cache file.
			loadAsset(snapshotData, cacheAsset->getSnapshotPath());
		}
		memcpy(bufferPtrStart, snapshotData.data(), snapshotData.size());

		imageAssetGPU->prepareToUpload(commandBuffer, buildBasicImageSubresource());

		VkBufferImageCopy region{};
		region.bufferOffset = stageBufferOffset;
		region.bufferRowLength = 0;
		region.bufferImageHeight = 0;
		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.mipLevel = 0;
		region.imageSubresource.baseArrayLayer = 0;
		region.imageSubresource.layerCount = 1;
		region.imageOffset = { 0, 0, 0 };
		region.imageExtent = imageAssetGPU->getSelfImage().getExtent();

		vkCmdCopyBufferToImage(
			commandBuffer.cmd,
			stageBuffer,
			imageAssetGPU->getSelfImage().getImage(),
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1,
			&region);
		imageAssetGPU->finishUpload(commandBuffer, buildBasicImageSubresource());
	}

	std::shared_ptr<SnapshotAssetTextureLoadTask>
		AssetTexture::buildSnapShotLoadTask()
	{
		auto* fallbackWhite = getContext()->getBuiltinTextureWhite().get();
		ASSERT(fallbackWhite, "Fallback texture must be valid, you forget init engine texture before init.");

		uint32_t widthSnapShot;
		uint32_t heightSnapShot;
		AssetSnapshot::quantifySnapshotDim(widthSnapShot, heightSnapShot, m_dimension.x, m_dimension.y);

		auto newAsset = std::make_shared<GPUImageAsset>(
			fallbackWhite,
			VK_FORMAT_R8G8B8A8_UNORM,
			this->getSaveInfo().getName(),
			1,
			math::uvec3{ widthSnapShot, heightSnapShot, 1 }
		);

		getContext()->insertLRUAsset(this->getSnapshotUUID(), newAsset);

		auto newTask = std::make_shared<SnapshotAssetTextureLoadTask>();
		newTask->imageAssetGPU = newAsset;
		newTask->cacheAsset = getptr<AssetTexture>();

		return newTask;
	}

	std::shared_ptr<AssetTextureCacheLoadTask> AssetTexture::buildTextureLoadTask()
	{
		auto* fallbackWhite = getContext()->getBuiltinTextureWhite().get();

		auto newAsset = std::make_shared<GPUImageAsset>(
			fallbackWhite,
			this->getFormat(),
			this->getSaveInfo().getName(),
			this->getMipmapCount(),
			this->getDimension()
		);

		getContext()->insertLRUAsset(this->getBinUUID(), newAsset);

		auto newTask = std::make_shared<AssetTextureCacheLoadTask>();
		newTask->imageAssetGPU = newAsset;
		newTask->cacheAsset = getptr<AssetTexture>();

		return newTask;
	}

	void AssetTexture::buildSnapshot(
		std::vector<uint8_t>& snapshotData, 
		unsigned char* pixels,
		int numChannel)
	{
		uint32_t widthSnapShot;
		uint32_t heightSnapShot;
		AssetSnapshot::quantifySnapshotDim(widthSnapShot, heightSnapShot, m_dimension.x, m_dimension.y);

		snapshotData.resize(widthSnapShot * heightSnapShot * numChannel);

		stbir_resize_uint8_srgb_edgemode(
			pixels,
			m_dimension.x,
			m_dimension.y,
			0,
			snapshotData.data(),
			widthSnapShot,
			heightSnapShot,
			0,
			numChannel,
			numChannel == 4 ? 3 : -1,
			STBIR_FLAG_ALPHA_PREMULTIPLIED,
			STBIR_EDGE_CLAMP
		);
	}

	void AssetTextureCacheLoadTask::uploadFunction(
		uint32_t stageBufferOffset, 
		void* bufferPtrStart, 
		RHICommandBufferBase& commandBuffer, 
		VulkanBuffer& stageBuffer)
	{

		AssetTextureBin textureBin{};
		if (!std::filesystem::exists(cacheAsset->getBinPath()))
		{
			UN_IMPLEMENT();
		}
		else
		{
			LOG_TRACE("Found bin for asset {} cache in disk so just load.",
				utf8::utf16to8(cacheAsset->getSaveInfo().getStorePath()));

			// Just load from cache file.
			loadAsset(textureBin, cacheAsset->getBinPath());
		}
		

		VkImageSubresourceRange rangeAllMips = buildBasicImageSubresource();
		rangeAllMips.levelCount = cacheAsset->getMipmapCount();

		imageAssetGPU->prepareToUpload(commandBuffer, rangeAllMips);

		uint32_t bufferOffset = 0;
		uint32_t bufferSize = 0;

		VkBufferImageCopy region{};
		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.baseArrayLayer = 0;
		region.imageSubresource.layerCount = 1;
		region.imageOffset = { 0, 0, 0 };
		region.bufferRowLength = 0;
		region.bufferImageHeight = 0;

		std::vector<VkBufferImageCopy> copyRegions{};

		const auto& mipmapDatas = textureBin.mipmapDatas;
		for (uint32_t level = 0; level < cacheAsset->getMipmapCount(); level++)
		{
			const auto& currentMip = mipmapDatas.at(level);
			const uint32_t currentMipSize = (uint32_t)currentMip.size();

			uint32_t mipWidth  = std::max<uint32_t>(cacheAsset->getDimension().x >> level, 1);
			uint32_t mipHeight = std::max<uint32_t>(cacheAsset->getDimension().y >> level, 1);

			memcpy((void*)((char*)bufferPtrStart + bufferOffset), currentMip.data(), currentMipSize);

			region.bufferOffset = stageBufferOffset + bufferOffset;
			region.imageSubresource.mipLevel = level;
			region.imageExtent = { mipWidth, mipHeight, 1 };

			copyRegions.push_back(region);

			bufferOffset += currentMipSize;
			bufferSize += currentMipSize;
		}
		ASSERT(uploadSize() >= bufferSize, "Upload size must bigger than buffer size!");

		vkCmdCopyBufferToImage(
			commandBuffer.cmd, 
			stageBuffer, 
			imageAssetGPU->getSelfImage().getImage(),
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
			(uint32_t)copyRegions.size(), 
			copyRegions.data());

		imageAssetGPU->finishUpload(commandBuffer, rangeAllMips);
	}
}