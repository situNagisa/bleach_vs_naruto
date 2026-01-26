#pragma once

#include <string_view>
#include <string>
#include <stdexcept>
#include <format>
#include <chrono>

#include <gif_lib.h>

#include <glad/glad.h>

#include <entt/entt.hpp>

#include "../opengl.h"

#include "../image/image.h"

#include "../timeline/timeline.h"

namespace aned::loader
{
	inline auto gif(::entt::registry& registry, ::std::string_view path)
	{
		int error = 0;
		auto gif = ::DGifOpenFileName(::std::string(path).c_str(), &error);
		if (!gif)
			throw ::std::runtime_error(::std::format("{} Not a GIF or failed to open", path));
		if (::DGifSlurp(gif) != GIF_OK) {
			::DGifCloseFile(gif, &error);
			throw ::std::runtime_error(::std::format("{} Failed to parse GIF, error = {}", path, error));
		}

		auto result = timeline_system::timeline();

		auto w = gif->SWidth;
		auto h = gif->SHeight;
		auto globalMap = gif->SColorMap;

		std::vector<unsigned char> canvas(w * h * 4, 0);
		std::vector<unsigned char> prevCanvas;

		for (int i = 0; i < gif->ImageCount; ++i) {
			SavedImage& img = gif->SavedImages[i];
			ColorMapObject* cmap = img.ImageDesc.ColorMap ? img.ImageDesc.ColorMap : globalMap;

			int left = img.ImageDesc.Left;
			int top = img.ImageDesc.Top;
			int fw = img.ImageDesc.Width;
			int fh = img.ImageDesc.Height;
			bool interlaced = img.ImageDesc.Interlace != 0;

			int delay_cs = 10;
			int transparent_index = -1;
			int disposal = 0;

			for (int eb = 0; eb < img.ExtensionBlockCount; ++eb) {
				ExtensionBlock& ext = img.ExtensionBlocks[eb];
				if (ext.Function == 0xF9 && ext.ByteCount >= 4) {
					unsigned char* b = ext.Bytes;
					int packed = b[0];
					disposal = (packed >> 2) & 0x7;
					bool hasTransp = packed & 0x1;
					delay_cs = b[1] + (b[2] << 8);
					transparent_index = (int)(unsigned char)b[3];
					if (!hasTransp) transparent_index = -1;
				}
			}

			prevCanvas = canvas;

			auto setPixel = [&](int cx, int cy, unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
				if (cx < 0 || cx >= w || cy < 0 || cy >= h) return;
				size_t idx = (size_t)(cy * w + cx) * 4;
				canvas[idx + 0] = r;
				canvas[idx + 1] = g;
				canvas[idx + 2] = b;
				canvas[idx + 3] = a;
				};

			auto destRowFromInterlaced = [&](int row) {
				if (!interlaced) return row;
				std::vector<int> map;
				map.reserve(fh);
				for (int r = 0; r < fh; r += 8) map.push_back(r);
				for (int r = 4; r < fh; r += 8) map.push_back(r);
				for (int r = 2; r < fh; r += 4) map.push_back(r);
				for (int r = 1; r < fh; r += 2) map.push_back(r);
				if (row < (int)map.size()) return map[row];
				return row;
				};

			for (int fy = 0; fy < fh; ++fy) {
				int sy = interlaced ? destRowFromInterlaced(fy) : fy;
				for (int fx = 0; fx < fw; ++fx) {
					int srcIdx = fy * fw + fx;
					unsigned char colorIndex = img.RasterBits[srcIdx];
					if (!cmap || colorIndex >= cmap->ColorCount) continue;
					GifColorType gc = cmap->Colors[colorIndex];
					unsigned char a = 255;
					if (colorIndex == transparent_index) a = 0;
					int cx = left + fx;
					int cy = top + sy;
					if (a == 0) continue;
					setPixel(cx, cy, gc.Red, gc.Green, gc.Blue, a);
				}
			}

			GLuint tex = 0;
			glGenTextures(1, &tex);
			glBindTexture(GL_TEXTURE_2D, tex);
			glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, canvas.data());
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glBindTexture(GL_TEXTURE_2D, 0);

			auto ms = ::std::chrono::milliseconds(delay_cs > 0 ? delay_cs * 10 : 100);
			constexpr auto frame_duration = ::std::chrono::milliseconds(1000) / 60;
			auto range = result.emplace_back(::std::max(1, static_cast<int>(std::ceil(static_cast<float>(ms / frame_duration)))));
			auto entity = registry.create();
			auto handle = ::entt::handle(registry, entity);
			handle.emplace<component::image>(
				component::image{
					.texture{&tex, {}},
					.width = static_cast<::std::size_t>(w),
					.height = static_cast<::std::size_t>(h),
				});
			result._data.back().keyframe.displays.push_back(handle);

			if (disposal == 2) {
				for (int ry = 0; ry < fh; ++ry) {
					for (int rx = 0; rx < fw; ++rx) {
						int cx = left + rx;
						int cy = top + ry;
						if (cx >= 0 && cx < w && cy >= 0 && cy < h) canvas[cy * w + cx] = 0;
					}
				}
			}
			else if (disposal == 3) {
				canvas = prevCanvas;
			}
		}

		::DGifCloseFile(gif, &error);
		return result;
	}
}