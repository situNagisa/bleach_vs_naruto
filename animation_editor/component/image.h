#pragma once

#include <stdexcept>
#include <format>
#include <memory>

#include <glad/glad.h>
#include <stb_image.h>


struct image
{
	constexpr static auto texture_deleter = [](GLuint texture) noexcept { glDeleteTextures(1, &texture); };
	::std::unique_ptr<GLuint, decltype(texture_deleter)> texture{};
	int width = 0;
	int height = 0;

protected:
	virtual void update(int parent_frame = 0) override {}

	virtual void render(ImDrawList* draw_list, ImVec2 canvas_pos, ImVec2 canvas_size,
		float stage_pan_x, float stage_pan_y, float stage_zoom) override {
		if (!texture_id || width == 0 || height == 0) return;

		float scaled_w = width * scaleX;
		float scaled_h = height * scaleY;

		float screen_x = canvas_pos.x + stage_pan_x + x * stage_zoom;
		float screen_y = canvas_pos.y + stage_pan_y + y * stage_zoom;
		float screen_w = scaled_w * stage_zoom;
		float screen_h = scaled_h * stage_zoom;

		ImGui::SetCursorScreenPos(ImVec2(screen_x, screen_y));

		if (selected) {
			ImU32 border_color = IM_COL32(0, 255, 0, 255);
			float border_thickness = 2.0f;
			draw_list->AddRect(ImVec2(screen_x, screen_y), ImVec2(screen_x + screen_w, screen_y + screen_h),
				border_color, 0.0f, 15, border_thickness);
		}

		ImGui::Image((ImTextureID)(intptr_t)texture_id, ImVec2(screen_w, screen_h));
	}

	virtual int hitTest(float mx, float my) override {
		float scaled_w = width * scaleX;
		float scaled_h = height * scaleY;
		float x1 = x;
		float y1 = y;
		float x2 = x1 + scaled_w;
		float y2 = y1 + scaled_h;

		if (mx >= x1 && mx < x2 && my >= y1 && my < y2) {
			return id;
		}
		return -1;
	}
};

inline auto load_image(char const* path)
{
	int n, w, h;
	unsigned char* data = stbi_load(path, &w, &h, &n, 4);
	if (!data)
		throw ::std::runtime_error(::std::format("stb_image failed to load: {}\n", path));

	GLuint tex = 0;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glBindTexture(GL_TEXTURE_2D, 0);
	stbi_image_free(data);

	return image{
		.texture{tex},
		.width = w,
		.height = h,
	};
}