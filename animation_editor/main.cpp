// Simple animation editor prototype using SDL, ImGui and stb_image
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <gif_lib.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <vector>
#include <string>
#include <iostream>
#include <memory>
#include "stb_image.h"
#include "timeline_data.h"
#include "timeline_layer.h"
#include "timeline_mgmt.h"
#include "asset_manager.h"
#include "asset_browser.h"

#undef main

// ============================================================================
// SECTION 1: DATA STRUCTURES - 数据结构定义
// ============================================================================

// 单个帧的数据（纹理、尺寸、持续时间、偏移）
struct Frame {
	unsigned int texture = 0;
	int width = 0;
	int height = 0;
	int duration_ms = 100;
	float offset_x = 0.0f;   // 帧在剪辑本地坐标系中的偏移
	float offset_y = 0.0f;
};

// ============================================================================
// SECTION 2: CLIPPLAYER CLASS - Handles loading image sequences / animated clips
// ============================================================================

class ClipPlayer {
public:
	ClipPlayer() {}
	~ClipPlayer() { clear(); }

	void clear() {
		for (auto& f : frames) {
			if (f.texture) glDeleteTextures(1, &f.texture);
		}
		frames.clear();
		current = 0;
		source_path.clear();
	}

	// Keep track of source path for assets window
	std::string source_path;

	// Load single image
	bool loadImage(const std::string& path) {
		int w, h, n;
		unsigned char* data = stbi_load(path.c_str(), &w, &h, &n, 4);
		if (!data) {
			std::cerr << "stb_image failed to load: " << path << "\n";
			return false;
		}

		clear();
		GLuint tex = 0;
		glGenTextures(1, &tex);
		glBindTexture(GL_TEXTURE_2D, tex);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glBindTexture(GL_TEXTURE_2D, 0);
		stbi_image_free(data);

		frames.push_back({ tex, w, h, 100 });
		playing = false;
		current = 0;
		last_time = glfwGetTime() * 1000.0;
		source_path = path;
		return true;
	}

	// Load animated GIF or image sequence
	bool loadAnimated(const std::string& path) {
		int error = 0;
		GifFileType* gif = DGifOpenFileName(path.c_str(), &error);
		if (!gif) {
			// Not a GIF or failed to open, try as single image
			return loadImage(path);
		}
		if (DGifSlurp(gif) != GIF_OK) {
			DGifCloseFile(gif, &error);
			return false;
		}

		clear();

		int w = gif->SWidth;
		int h = gif->SHeight;
		ColorMapObject* globalMap = gif->SColorMap;

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

			int ms = delay_cs > 0 ? delay_cs * 10 : 100;
			frames.push_back({ tex, w, h, ms });

			if (disposal == 2) {
				for (int ry = 0; ry < fh; ++ry) {
					for (int rx = 0; rx < fw; ++rx) {
						int cx = left + rx;
						int cy = top + ry;
						if (cx >= 0 && cx < w && cy >= 0 && cy < h) canvas[cy * w + cx] = 0;
					}
				}
			} else if (disposal == 3) {
				canvas = prevCanvas;
			}
		}

		DGifCloseFile(gif, &error);
		current = 0;
		last_time = glfwGetTime() * 1000.0;
		playing = true;
		source_path = path;
		return !frames.empty();
	}

	// Generic loader (choose animated or single image)
	bool load(const std::string& path) {
		std::string lower = path;
		for (auto& c : lower) c = (char)tolower(c);
		if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".gif")
			return loadAnimated(path);
		return loadImage(path);
	}

	// === Playback control ===
	void update() {
		if (!playing || frames.empty()) return;
		double now = glfwGetTime() * 1000.0;
		if (now - last_time >= (double)frames[current].duration_ms) {
			current = (current + 1) % frames.size();
			last_time = now;
		}
	}

	void setPlaying(bool p) { playing = p; }
	bool isPlaying() const { return playing; }
	void gotoFrame(int idx) {
		if (idx >= 0 && idx < (int)frames.size()) {
			current = idx;
			last_time = glfwGetTime() * 1000.0;
		}
	}

	// === Frame queries ===
	int frameCount() const { return (int)frames.size(); }
	int currentFrame() const { return current; }
	int frameDuration(int idx) const {
		if (idx >= 0 && idx < (int)frames.size())
			return frames[idx].duration_ms;
		return 0;
	}

	Frame* getCurrentFrame() {
		return frames.empty() ? nullptr : &frames[current];
	}

	const Frame* getCurrentFrame() const {
		return frames.empty() ? nullptr : &frames[current];
	}

	// === Frame offset management ===
	void setFrameOffset(int idx, float ox, float oy) {
		if (idx >= 0 && idx < (int)frames.size()) {
			frames[idx].offset_x = ox;
			frames[idx].offset_y = oy;
		}
	}

	void getFrameOffset(int idx, float& ox, float& oy) const {
		if (idx >= 0 && idx < (int)frames.size()) {
			ox = frames[idx].offset_x;
			oy = frames[idx].offset_y;
		} else {
			ox = oy = 0.0f;
		}
	}

private:
	std::vector<Frame> frames;
	int current = 0;
	double last_time = 0.0;
	bool playing = false;
};

// ============================================================================
// SECTION 3: DISPLAY TREE - 显示树结构（参考AS3 DisplayObject）
// ============================================================================

// 前向声明
class DisplayObjectContainer;

// === DisplayObject 基类 - 所有可显示对象的基类 ===
class DisplayObject {
public:
	virtual ~DisplayObject() {}

	// === 基础属性 ===
	int id = -1;
	float x = 0.0f;
	float y = 0.0f;
	float scaleX = 1.0f;
	float scaleY = 1.0f;

	// === 层级关系 ===
	int z_order = 0;
	DisplayObjectContainer* parent = nullptr;

	// === 交互状态 ===
	bool selected = false;
	double last_click_time = 0.0;

	// === 虚方法 ===
	virtual void update(int parent_frame = 0) = 0;  // 更新逻辑（播放同步等）
	virtual void render(ImDrawList* draw_list, ImVec2 canvas_pos, ImVec2 canvas_size,
		float stage_pan_x, float stage_pan_y, float stage_zoom) = 0;  // 绘制
	virtual int hitTest(float mx, float my) { return -1; }  // 碰撞检测，返回最上层对象的ID或-1
	virtual bool isContainer() const { return false; }  // 是否为容器
};

// === DisplayObjectContainer - 可包含其他DisplayObject的容器 ===
class DisplayObjectContainer : public DisplayObject {
public:
	virtual ~DisplayObjectContainer() {}

	// === 子对象管理 ===
	std::vector<DisplayObject*> children;

	// 添加子对象
	void addChild(DisplayObject* child) {
		if (!child) return;
		child->parent = this;
		children.push_back(child);
	}

	// 移除子对象
	void removeChild(DisplayObject* child) {
		if (!child) return;
		auto it = std::find(children.begin(), children.end(), child);
		if (it != children.end()) {
			children.erase(it);
			child->parent = nullptr;
		}
	}

	// 获取子对象数量
	int numChildren() const { return (int)children.size(); }

	// 递归更新所有子对象
	virtual void update(int parent_frame = 0) override {
		// 先更新自己
		updateSelf(parent_frame);
		
		// 再递归更新所有子对象
		for (auto child : children) {
			if (child) {
				child->update(parent_frame);
			}
		}
	}

	// 递归渲染所有子对象
	virtual void render(ImDrawList* draw_list, ImVec2 canvas_pos, ImVec2 canvas_size,
		float stage_pan_x, float stage_pan_y, float stage_zoom) override {
		// 先渲染自己（如果有内容）
		renderSelf(draw_list, canvas_pos, canvas_size, stage_pan_x, stage_pan_y, stage_zoom);
		
		// 再递归渲染所有子对象
		for (auto child : children) {
			if (child) {
				child->render(draw_list, canvas_pos, canvas_size, stage_pan_x, stage_pan_y, stage_zoom);
			}
		}
	}

	// 递归碰撞检测
	virtual int hitTest(float mx, float my) override {
		int result = -1;
		
		// 先检测所有子对象（从后往前，确保顶层优先）
		for (int i = (int)children.size() - 1; i >= 0; --i) {
			if (children[i]) {
				int hit = children[i]->hitTest(mx, my);
				if (hit >= 0) {
					return hit;  // 返回最先碰撞的（最上层）
				}
			}
		}
		
		// 再检测自己
		result = hitTestSelf(mx, my);
		return result;
	}

	virtual bool isContainer() const override { return true; }

protected:
	// 子类可覆盖这些方法来实现自定义逻辑
	virtual void updateSelf(int parent_frame) {}
	virtual void renderSelf(ImDrawList* draw_list, ImVec2 canvas_pos, ImVec2 canvas_size,
		float stage_pan_x, float stage_pan_y, float stage_zoom) {}
	virtual int hitTestSelf(float mx, float my) { return -1; }
};

// === Clip - 影片剪辑（DisplayObjectContainer 的具体实现）===
class Clip : public DisplayObjectContainer {
public:
	// === 媒体和播放 ===
	ClipPlayer* player = nullptr;

	// === 帧编辑模式 ===
	bool editing_frames = false;
	int editing_frame_idx = -1;

	virtual ~Clip() {
		if (player) delete player;
	}

protected:
	virtual void updateSelf(int parent_frame) override {
		if (!player) return;
		
		// 如果有父容器且父容器也是 Clip，同步到父帧
		if (parent) {
			Clip* parent_clip = dynamic_cast<Clip*>(parent);
			if (parent_clip && parent_clip->player) {
				int parent_frame_idx = parent_clip->player->currentFrame();
				if (player->frameCount() > 0) {
					player->gotoFrame(parent_frame_idx % player->frameCount());
				}
			}
		} else {
			// 否则更新自己的播放
			player->update();
		}
	}

	virtual void renderSelf(ImDrawList* draw_list, ImVec2 canvas_pos, ImVec2 canvas_size,
		float stage_pan_x, float stage_pan_y, float stage_zoom) override {
		if (!player || player->frameCount() == 0) return;

		Frame* f = player->getCurrentFrame();
		if (!f) return;

		// 计算缩放尺寸
		float scaled_w = f->width * scaleX;
		float scaled_h = f->height * scaleY;

		// 计算屏幕坐标
		float screen_x = canvas_pos.x + stage_pan_x + (x + f->offset_x) * stage_zoom;
		float screen_y = canvas_pos.y + stage_pan_y + (y + f->offset_y) * stage_zoom;
		float screen_w = scaled_w * stage_zoom;
		float screen_h = scaled_h * stage_zoom;

		ImGui::SetCursorScreenPos(ImVec2(screen_x, screen_y));
		
		// 绘制边框
		if (selected) {
			ImU32 border_color = editing_frames ? IM_COL32(0, 150, 255, 255) : IM_COL32(0, 255, 0, 255);
			float border_thickness = editing_frames ? 3.0f : 2.0f;
			draw_list->AddRect(ImVec2(screen_x, screen_y), ImVec2(screen_x + screen_w, screen_y + screen_h),
				border_color, 0.0f, 15, border_thickness);
			
			if (editing_frames) {
				draw_list->AddText(ImVec2(screen_x + 5, screen_y + 5), IM_COL32(0, 150, 255, 255), "[EDIT MODE]");
			}
		}

		ImGui::Image((ImTextureID)(intptr_t)f->texture, ImVec2(screen_w, screen_h));
	}

	virtual int hitTestSelf(float mx, float my) override {
		if (!player || player->frameCount() == 0) return -1;

		Frame* f = player->getCurrentFrame();
		if (!f) return -1;

		float scaled_w = f->width * scaleX;
		float scaled_h = f->height * scaleY;
		float x1 = x;
		float y1 = y;
		float x2 = x1 + scaled_w;
		float y2 = y1 + scaled_h;

		if (mx >= x1 && mx < x2 && my >= y1 && my < y2) {
			return id;  // 返回自己的ID
		}
		return -1;
	}
};

// === Stage - 舞台（特殊的 DisplayObjectContainer，是显示树的根）===
class Stage : public DisplayObjectContainer {
public:
	// === 舞台显示属性 ===
	float pan_x = 0.0f;
	float pan_y = 0.0f;
	float zoom = 1.0f;

	// === 对象管理 ===
	int nextClipId = 0;
	int nextZOrder = 0;
	int selectedClipId = -1;

	// === 时间轴系统 ===
	TimelineSystem timeline_system;

	Stage() {
		is_stage_root = true;
		id = -1;
		timeline_system.initialize(this);
	}

	~Stage() {
		timeline_system.clear();
	}

	// 创建并添加新剪辑
	Clip* createClip() {
		Clip* clip = new Clip();
		clip->id = nextClipId++;
		clip->z_order = nextZOrder++;
		clip->player = new ClipPlayer();
		addChild(clip);

		// 创建对应的时间轴
		Timeline* timeline = timeline_system.createTimeline(120);
		if (timeline) {
			std::string layer_name = "Clip_" + std::to_string(clip->id);
			timeline_system.createLayer(clip->id, timeline->getId(), layer_name);
			timeline_system.setCurrentTimelineId(timeline->getId());
		}

		return clip;
	}

	// 删除剪辑
	void deleteClip(int clip_id) {
		for (auto it = children.begin(); it != children.end(); ++it) {
			DisplayObject* obj = *it;
			if (obj && obj->id == clip_id) {
				// 删除对应的时间轴
				TimelineLayer* layer = timeline_system.getLayer(clip_id);
				if (layer) {
					timeline_system.deleteTimeline(layer->getTimelineId());
					timeline_system.deleteLayer(clip_id);
				}

				delete obj;
				removeChild(obj);
				break;
			}
		}
	}

	// 选择剪辑
	void selectClip(int clip_id) {
		for (auto child : children) {
			if (child) {
				child->selected = (child->id == clip_id);
				if (child->selected) {
					selectedClipId = clip_id;
				}
			}
		}
	}

	// 获取选中的剪辑
	Clip* getSelectedClip() {
		for (auto child : children) {
			if (child && child->id == selectedClipId) {
				Clip* clip = dynamic_cast<Clip*>(child);
				return clip;
			}
		}
		return nullptr;
	}

	// 碰撞检测（转换坐标后递归调用）
	int hitTestStage(float screen_mx, float screen_my) {
		// 转换到舞台坐标系
		float stage_mx = (screen_mx - pan_x) / zoom;
		float stage_my = (screen_my - pan_y) / zoom;
		
		// 递归碰撞检测
		return hitTest(stage_mx, stage_my);
	}

	// 层级操作
	void raiseClip(int clip_id) {
		for (auto child : children) {
			if (child && child->id == clip_id) {
				child->z_order++;
				break;
			}
		}
	}

	void lowerClip(int clip_id) {
		for (auto child : children) {
			if (child && child->id == clip_id) {
				child->z_order--;
				break;
			}
		}
	}

	void raiseClipToTop(int clip_id) {
		for (auto child : children) {
			if (child && child->id == clip_id) {
				child->z_order = nextZOrder++;
				break;
			}
		}
	}

	void lowerClipToBottom(int clip_id) {
		for (auto child : children) {
			if (child && child->id == clip_id) {
				child->z_order = -nextZOrder;
				nextZOrder++;
				break;
			}
		}
	}

	// 舞台特有：渲染坐标系
	void renderStageAxes(ImDrawList* draw_list, ImVec2 canvas_pos, ImVec2 canvas_size) {
		float grid_step = 50.0f;
		ImU32 grid_color = IM_COL32(50, 50, 50, 100);
		
		float screen_grid_step = grid_step * zoom;
		int start_x = (int)std::floor((0 - pan_x) / screen_grid_step);
		int end_x = (int)std::ceil((canvas_size.x - pan_x) / screen_grid_step);
		int start_y = (int)std::floor((0 - pan_y) / screen_grid_step);
		int end_y = (int)std::ceil((canvas_size.y - pan_y) / screen_grid_step);
		
		for (int i = start_x; i <= end_x; ++i) {
			float screen_x = canvas_pos.x + pan_x + i * screen_grid_step;
			if (screen_x >= canvas_pos.x && screen_x <= canvas_pos.x + canvas_size.x) {
				draw_list->AddLine(ImVec2(screen_x, canvas_pos.y), ImVec2(screen_x, canvas_pos.y + canvas_size.y),
					grid_color, 1.0f);
			}
		}
		
		for (int i = start_y; i <= end_y; ++i) {
			float screen_y = canvas_pos.y + pan_y + i * screen_grid_step;
			if (screen_y >= canvas_pos.y && screen_y <= canvas_pos.y + canvas_size.y) {
				draw_list->AddLine(ImVec2(canvas_pos.x, screen_y), ImVec2(canvas_pos.x + canvas_size.x, screen_y),
					grid_color, 1.0f);
			}
		}
		
		float origin_screen_x = canvas_pos.x + pan_x;
		float origin_screen_y = canvas_pos.y + pan_y;
		const float axis_length = 200.0f * zoom;
		
		if (origin_screen_x >= canvas_pos.x - axis_length && origin_screen_x <= canvas_pos.x + canvas_size.x) {
			draw_list->AddLine(ImVec2(origin_screen_x, origin_screen_y),
				ImVec2(origin_screen_x + axis_length, origin_screen_y), IM_COL32(255, 0, 0, 255), 2.0f);
			draw_list->AddText(ImVec2(origin_screen_x + axis_length + 5, origin_screen_y - 10),
				IM_COL32(255, 0, 0, 255), "X");
		}
		
		if (origin_screen_y >= canvas_pos.y - axis_length && origin_screen_y <= canvas_pos.y + canvas_size.y) {
			draw_list->AddLine(ImVec2(origin_screen_x, origin_screen_y),
				ImVec2(origin_screen_x, origin_screen_y + axis_length), IM_COL32(0, 255, 0, 255), 2.0f);
			draw_list->AddText(ImVec2(origin_screen_x - 15, origin_screen_y + axis_length + 5),
				IM_COL32(0, 255, 0, 255), "Y");
		}
		
		draw_list->AddCircleFilled(ImVec2(origin_screen_x, origin_screen_y), 4.0f, IM_COL32(255, 255, 0, 255));
	}

protected:
	bool is_stage_root = false;

	virtual void updateSelf(int parent_frame) override {
		// 舞台本身没有播放逻辑，仅更新子对象
	}

	virtual void renderSelf(ImDrawList* draw_list, ImVec2 canvas_pos, ImVec2 canvas_size,
		float stage_pan_x, float stage_pan_y, float stage_zoom) override {
		// 舞台本身不需要渲染，坐标轴在外部单独渲染
	}
};

int main(int argc, char** argv) {
	// Initialize GLFW and create window with OpenGL context
	if (!glfwInit()) {
		std::cerr << "glfwInit failed\n";
		return 1;
	}
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
	GLFWwindow* window = glfwCreateWindow(1600, 1000, "Animation Editor", NULL, NULL);
	if (!window) {
		std::cerr << "glfwCreateWindow failed\n";
		glfwTerminate();
		return 1;
	}
	glfwMakeContextCurrent(window);
	glfwSwapInterval(1);
	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
		std::cerr << "Failed to initialize GLAD\n";
		return 1;
	}

	// Setup ImGui
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	// Enable docking
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL3_Init("#version 130");
	ImGui::StyleColorsDark();

	// Initialize stage
	Stage stage;

	// Asset library and browser
	AssetLibrary asset_library;
	// Initialize to current working directory
	asset_library.scanDirectory(".");
	AssetBrowser asset_browser;
	// Simple application context to access both Stage and AssetLibrary from GLFW callbacks
	struct AppContext { Stage* stage; AssetLibrary* assets; } app_ctx{ &stage, &asset_library };

	asset_browser.setOnAssetSelected([&](const std::string& path) {
		// Create or update asset entry
		int asset_idx = asset_library.addOrUpdateAsset(path);

		// Create a clip and load selected asset
		Clip* clip = stage.createClip();
		if (clip) {
			if (clip->player->load(path)) {
				// Populate timeline keyframes from clip frames
				TimelineLayer* layer = stage.timeline_system.getLayer(clip->id);
				if (layer) {
					int fc = clip->player->frameCount();
					std::vector<int> durs;
					for (int i = 0; i < fc; ++i) durs.push_back(clip->player->frameDuration(i));
					stage.timeline_system.populateTimelineFromPlayer(layer->getTimelineId(), fc, durs);
					stage.timeline_system.setCurrentTimelineId(layer->getTimelineId());
				}
				// Update asset library with thumbnail and frame count
				int tex = asset_library.generateThumbnail(path);
				if (tex > 0) asset_library.setAssetTexture(asset_idx, tex);
				asset_library.setAssetFrameCount(asset_idx, clip->player->frameCount());
				// Record source_path on player
				clip->player->source_path = path;
				stage.selectClip(clip->id);
			}
		}
	});

	// Setup drag-drop callback - use AppContext as window user pointer
	glfwSetWindowUserPointer(window, &app_ctx);
	glfwSetDropCallback(window, [](GLFWwindow* w, int count, const char** paths) {
		if (count <= 0 || !paths) return;
		AppContext* ctx = (AppContext*)glfwGetWindowUserPointer(w);
		if (!ctx || !ctx->stage || !ctx->assets) return;
		Stage* stage = ctx->stage;
		AssetLibrary* asset_library = ctx->assets;
		
		Clip* clip = stage->createClip();
		if (clip) {
			std::string path = paths[0];
			if (clip->player->load(path)) {
				int asset_idx = asset_library->addOrUpdateAsset(path);
				int tex = asset_library->generateThumbnail(path);
				if (tex > 0) asset_library->setAssetTexture(asset_idx, tex);
				asset_library->setAssetFrameCount(asset_idx, clip->player->frameCount());
				TimelineLayer* layer = stage->timeline_system.getLayer(clip->id);
				if (layer) {
					int fc = clip->player->frameCount();
					std::vector<int> durs;
					for (int i = 0; i < fc; ++i) durs.push_back(clip->player->frameDuration(i));
					stage->timeline_system.populateTimelineFromPlayer(layer->getTimelineId(), fc, durs);
					stage->timeline_system.setCurrentTimelineId(layer->getTimelineId());
				}
				clip->player->source_path = path;
				stage->selectClip(clip->id);
			}
		}
	});

	// ============================================================================
	// MAIN LOOP - 主循环
	// ============================================================================
	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();
		
		// === PHASE 1: UPDATE - 更新显示树 ===
		stage.update();

		// === PHASE 2: IMGUI FRAME - ImGui帧 ===
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		// Update timeline system (advance if playing) and sync clip players
		ImGuiIO& io_main = ImGui::GetIO();
		double dt_ms = io_main.DeltaTime * 1000.0;
		stage.timeline_system.update(dt_ms);
		if (stage.timeline_system.isPlaying()) {
			int cf = stage.timeline_system.getCurrentFrame();
			for (auto child : stage.children) {
				Clip* clip = dynamic_cast<Clip*>(child);
				if (clip && clip->player && clip->player->frameCount() > 0) {
					clip->player->gotoFrame(cf % clip->player->frameCount());
				}
			}
		}

		// Begin full-screen dockspace host
		ImGuiWindowFlags host_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground;
		ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->Pos);
		ImGui::SetNextWindowSize(viewport->Size);
		ImGui::SetNextWindowViewport(viewport->ID);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::Begin("DockSpaceHost", nullptr, host_flags);
		ImGui::PopStyleVar();

		ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
		ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

		// === PHASE 3: CANVAS RENDERING - Canvas窗口渲染 ===
		if (stage.numChildren() > 0) {
			// Make Canvas a dockable window (do not force position/size)
			ImGui::Begin("Canvas");

			ImVec2 canvas_size = ImGui::GetContentRegionAvail();
			ImVec2 canvas_pos = ImGui::GetCursorScreenPos();

			ImGui::InvisibleButton("canvas_drag", canvas_size);

			ImGuiIO& io_local = ImGui::GetIO();
			ImVec2 mouse_pos = io_local.MousePos;
			float canvas_mx = mouse_pos.x - canvas_pos.x;
			float canvas_my = mouse_pos.y - canvas_pos.y;

			bool dragging_clip = false;
			int hovered_clip_id = -1;

			// === 交互处理 ===
			if (ImGui::IsItemHovered()) {
				// 碰撞检测
				hovered_clip_id = stage.hitTestStage(canvas_mx, canvas_my);

				// 双击进入帧编辑模式
				if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hovered_clip_id >= 0) {
					Clip* clip = nullptr;
					for (auto child : stage.children) {
						if (child && child->id == hovered_clip_id) {
							clip = dynamic_cast<Clip*>(child);
							break;
						}
					}
					
					if (clip) {
						double now = glfwGetTime();
						double time_since_last = now - clip->last_click_time;
						
						if (time_since_last < 0.3) {
							clip->editing_frames = !clip->editing_frames;
							if (clip->editing_frames && clip->player) {
								clip->editing_frame_idx = clip->player->currentFrame();
							}
						}
						clip->last_click_time = now;
						stage.selectClip(hovered_clip_id);
						dragging_clip = true;
					}
				}

				// 拖动
				if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
					Clip* clip = nullptr;
					if (hovered_clip_id >= 0) {
						for (auto child : stage.children) {
							if (child && child->id == hovered_clip_id) {
								clip = dynamic_cast<Clip*>(child);
								break;
							}
						}
					}
					
					if (clip && !clip->editing_frames) {
						clip->x += io_local.MouseDelta.x / stage.zoom;
						clip->y += io_local.MouseDelta.y / stage.zoom;
						dragging_clip = true;
						stage.selectClip(hovered_clip_id);
					} else if (hovered_clip_id < 0) {
						stage.pan_x += io_local.MouseDelta.x;
						stage.pan_y += io_local.MouseDelta.y;
					}
				}

				// 缩放
				if (io_local.MouseWheel != 0.0f) {
					float zoom_factor = io_local.MouseWheel > 0.0f ? 1.1f : 0.9f;
					stage.zoom *= zoom_factor;
					stage.zoom = std::clamp(stage.zoom, 0.1f, 10.0f);
				}

				// 单击选择
				if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !dragging_clip) {
					stage.selectClip(hovered_clip_id);
				}
			}

			// === 绘制 ===
			ImDrawList* draw_list = ImGui::GetWindowDrawList();
			stage.renderStageAxes(draw_list, canvas_pos, canvas_size);
			stage.render(draw_list, canvas_pos, canvas_size, stage.pan_x, stage.pan_y, stage.zoom);

			ImGui::End();
		}

		// === PHASE 4: TIMELINE UI - Timeline面板 ===
		// Make Timeline dockable and movable
		ImGui::Begin("Timeline");

		// Toolbar controls: first, previous, play/pause, next, last, stop, loop toggle
		{
			int current_frame = stage.timeline_system.getCurrentFrame();
			int timeline_id = stage.timeline_system.getCurrentTimelineId();
			Timeline* cur_tl = stage.timeline_system.getTimeline(timeline_id);
			int total_frames = cur_tl ? cur_tl->getFrameCount() : 1;

			if (ImGui::Button("|<<")) {
				stage.timeline_system.setPlaying(false);
				stage.timeline_system.setCurrentFrame(0);
				// sync
				for (auto child : stage.children) {
					Clip* clip = dynamic_cast<Clip*>(child);
					if (clip && clip->player && clip->player->frameCount() > 0) {
						clip->player->gotoFrame(0);
					}
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("<<")) {
				stage.timeline_system.setPlaying(false);
				stage.timeline_system.setCurrentFrame(std::max(0, current_frame - 1));
				int cf = stage.timeline_system.getCurrentFrame();
				for (auto child : stage.children) {
					Clip* clip = dynamic_cast<Clip*>(child);
					if (clip && clip->player && clip->player->frameCount() > 0) {
						clip->player->gotoFrame(cf % clip->player->frameCount());
					}
				}
			}
			ImGui::SameLine();
			bool is_playing = stage.timeline_system.isPlaying();
			if (ImGui::Button(is_playing ? "Pause" : "Play")) {
				stage.timeline_system.setPlaying(!is_playing);
			}
			ImGui::SameLine();
			if (ImGui::Button(">>")) {
				stage.timeline_system.setPlaying(false);
				stage.timeline_system.setCurrentFrame(std::min(total_frames - 1, current_frame + 1));
				int cf = stage.timeline_system.getCurrentFrame();
				for (auto child : stage.children) {
					Clip* clip = dynamic_cast<Clip*>(child);
					if (clip && clip->player && clip->player->frameCount() > 0) {
						clip->player->gotoFrame(cf % clip->player->frameCount());
					}
				}
			}
			ImGui::SameLine();
			if (ImGui::Button(">|")) {
				stage.timeline_system.setPlaying(false);
				stage.timeline_system.setCurrentFrame(total_frames - 1);
				for (auto child : stage.children) {
					Clip* clip = dynamic_cast<Clip*>(child);
					if (clip && clip->player && clip->player->frameCount() > 0) {
						clip->player->gotoFrame((total_frames - 1) % clip->player->frameCount());
					}
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Stop")) {
				stage.timeline_system.setPlaying(false);
				stage.timeline_system.setCurrentFrame(0);
				for (auto child : stage.children) {
					Clip* clip = dynamic_cast<Clip*>(child);
					if (clip && clip->player && clip->player->frameCount() > 0) {
						clip->player->gotoFrame(0);
					}
				}
			}

			ImGui::SameLine();
			// Frame display and scrubber
			ImGui::Text("Frame:");
			ImGui::SameLine();
			int frame_before = current_frame;
			if (ImGui::SliderInt("##frame_scrub", &current_frame, 0, std::max(0, total_frames - 1))) {
				stage.timeline_system.setPlaying(false);
				stage.timeline_system.setCurrentFrame(current_frame);
				for (auto child : stage.children) {
					Clip* clip = dynamic_cast<Clip*>(child);
					if (clip && clip->player && clip->player->frameCount() > 0) {
						clip->player->gotoFrame(current_frame % clip->player->frameCount());
					}
				}
			}
			ImGui::SameLine();
			ImGui::Text("/%d", std::max(1, total_frames));
		}

		if (stage.numChildren() == 0) {
			ImGui::Text("No clip loaded. Drag and drop a GIF or image file into the window.");
		} else {
			// Render timeline system
			ImDrawList* timeline_draw_list = ImGui::GetWindowDrawList();
			ImVec2 timeline_pos = ImGui::GetCursorScreenPos();
			ImVec2 timeline_size = ImGui::GetContentRegionAvail();
			
			// Create invisible button for mouse input
			ImGui::InvisibleButton("timeline_area", timeline_size);
			
			// Handle mouse input for timeline
			if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
				ImGuiIO& io2 = ImGui::GetIO();
				stage.timeline_system.handleMouseInput(timeline_pos, timeline_size, io2.MousePos);
			}
			
			stage.timeline_system.render(timeline_draw_list, timeline_pos, timeline_size);
		}

		ImGui::End();

		// === PHASE 5: ASSETS WINDOW - Asset Browser ===
		asset_browser.render(asset_library);

		// End dockspace host
		ImGui::End();

		// === PHASE 6: RENDERING - 最终渲染 ===
		ImGui::Render();
		int display_w, display_h;
		glfwGetFramebufferSize(window, &display_w, &display_h);
		glViewport(0, 0, display_w, display_h);
		glClearColor(0.11f, 0.11f, 0.11f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);

		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		glfwSwapBuffers(window);
	}

	// Cleanup
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	glfwDestroyWindow(window);
	glfwTerminate();
	return 0;
}

