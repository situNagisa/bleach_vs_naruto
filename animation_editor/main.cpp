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
#include <string_view>
#include "stb_image.h"
#include "timeline_data.h"
#include "timeline_layer.h"
#include "timeline_mgmt.h"
#include "asset_manager.h"
#include "asset_browser.h"

#include <entt/entt.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "aned/loader/picture.h"
#include "aned/loader/gif.h"

#include "aned/ui/select_timeline_layer.h"

#include "aned/timeline/system.h"
#include "aned/timeline/timeline.h"
#include "aned/movie/play_data.h"
#include "aned/image/image.h"

#include "aned/render_canvas.h"
#include "aned/timeline/render_ui.h"

#include "aned/hit_test/box.h"
#include "aned/hit_test/hit_test.h"

#undef main

struct entt_raii_handle : ::entt::handle
{
	using ::entt::handle::handle;
	~entt_raii_handle() noexcept
	{
		destroy();
	}
};



int main(int argc, char** argv) {
	{
		// Initialize GLFW and create window with OpenGL context
		if (!glfwInit()) {
			std::cerr << "glfwInit failed\n";
			return 1;
		}
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
		glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
	}
	GLFWwindow* window = glfwCreateWindow(1600, 1000, "Animation Editor", NULL, NULL);
	{
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
	}
	
	{
		// Setup ImGui
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
	}
	ImGuiIO& io = ImGui::GetIO();
	{
		// Enable docking
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		ImGui_ImplGlfw_InitForOpenGL(window, true);
		ImGui_ImplOpenGL3_Init("#version 130");
		ImGui::StyleColorsDark();
	}
	::entt::registry display_world{};
	// Initialize stage
	auto stage = ::entt::handle(display_world, display_world.create());
	{
		auto&& system = stage.emplace<::aned::component::timeline_system>();
		auto&& layer = system._layers.emplace_back("Main Timeline", ::aned::timeline_system::timeline());
		layer.timeline.emplace_back();
	}
	stage.emplace<::aned::component::play_data>();
	stage.emplace<::aned::component::select_timeline_layer>();

	// Asset library and browser
	AssetLibrary asset_library;
	// Initialize to current working directory
	asset_library.scanDirectory(".");
	AssetBrowser asset_browser;
	
	// Asset manager for object lifecycle management
	auto asset_manager = ::std::vector<entt_raii_handle>();
	
	// Simple application context to access both Stage, AssetLibrary, and AssetManager from GLFW callbacks
	struct AppContext { 
		::entt::handle root;
		::entt::registry& display_world;
		::entt::handle* stage = &root;
		AssetLibrary& assets;
		decltype(asset_manager)& asset_manager;
		float zoom = 1.0f;
		ImVec2 offset{};
	} app_ctx{
		.root = stage,
		.display_world = display_world,
		.assets = asset_library,
		.asset_manager =  asset_manager,
	};

#if 0
	asset_browser.setOnAssetSelected([&](const std::string& path) {
		// Create or update asset entry
		int asset_idx = asset_library.addOrUpdateAsset(path);

		// Use AssetManager to create appropriate object (MovieClip or Image)
		DisplayObject* obj = asset_manager.createFromPath(path);
		if (obj) {
			// Update asset library with thumbnail and frame count
			int tex = asset_library.generateThumbnail(path);
			if (tex > 0) asset_library.setAssetTexture(asset_idx, tex);

			// Set frame count if it's a MovieClip
			MovieClip* movie_clip = dynamic_cast<MovieClip*>(obj);
			if (movie_clip && movie_clip->player) {
				asset_library.setAssetFrameCount(asset_idx, movie_clip->player->frameCount());
			} else {
				asset_library.setAssetFrameCount(asset_idx, 1);
			}

			// stage.selectObject(obj->id);
		}
	});
#endif

	// Setup drag-drop callback - use AppContext as window user pointer
	glfwSetWindowUserPointer(window, &app_ctx);
	glfwSetDropCallback(window, [](GLFWwindow* w, int count, const char** paths) {
		if (count <= 0 || !paths) return;
		auto ctx = static_cast<AppContext*>(glfwGetWindowUserPointer(w));
		if (!ctx) return;

		std::string path = paths[0];
		int asset_idx = ctx->assets.addOrUpdateAsset(path);

		auto entity = ctx->display_world.create();
		auto&& handle = ctx->asset_manager.emplace_back(ctx->display_world, entity);
		if (path.ends_with(".gif"))
		{
			auto&& system = handle.emplace<::aned::component::timeline_system>();
			auto&& layer = system._layers.emplace_back("hhh", ::aned::loader::gif(ctx->display_world, path));
			handle.emplace<::aned::component::play_data>();
			handle.emplace<::aned::component::select_timeline_layer>();

			for (auto&& frame : layer.timeline._data)
			{
				for (auto&& h : frame.keyframe.displays)
				{
					auto&& img = h.get<::aned::component::image>();
					namespace bg = ::boost::geometry;
					h.emplace<::aned::component::hit_box>(
						::aned::component::hit_box{
							{bg::model::d2::point_xy<float>{}, bg::model::d2::point_xy<float>(img.width, img.height)}
						}
					);
				}
			}
		}
		else
		{
			auto&& img = handle.emplace<::aned::component::image>(::aned::loader::picture(path.c_str()));
			namespace bg = ::boost::geometry;
			handle.emplace<::aned::component::hit_box>(
				::aned::component::hit_box{
					{bg::model::d2::point_xy<float>{}, bg::model::d2::point_xy<float>(img.width, img.height)}
				}
			);
		}
		{
			auto&& system = ctx->stage->get<::aned::component::timeline_system>();
			auto&& layer_selector = ctx->stage->get<::aned::component::select_timeline_layer>();
			auto&& layer = system._layers.at(layer_selector.index);
			auto&& frame = layer.timeline[ctx->stage->get<::aned::component::play_data>().current_frame];
			frame.keyframe->displays.push_back(handle);
		}

		// select handle
	});

	constexpr auto timeline_theme = ::aned::timeline_system::theme::visual_studio_dark();
	::aned::controller::render_timeline_context render_timeline_context{
		.system = ::std::addressof(app_ctx.stage->get<::aned::component::timeline_system>()),
		.select_layer = ::std::addressof(app_ctx.stage->get<::aned::component::select_timeline_layer>()),
		.play_data = ::std::addressof(app_ctx.stage->get<::aned::component::play_data>()),
		.theme = ::std::addressof(timeline_theme),
	};

	using clock_type = ::std::chrono::steady_clock;
	auto frame_rate_record = clock_type::now();

	auto stage_transform_cache = ::glm::mat3x3(1.f);

	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();
#if 0
		::dfs(stage, [&](::std::size_t deep, ::std::size_t index, ::entt::handle handle) noexcept
			{
				if (handle.any_of<movie_clip>())
				{
					auto&& mc = handle.get<movie_clip>();
					mc.current_frame++;
				}
			});
#endif

		// === PHASE 2: IMGUI FRAME - ImGui帧 ===
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		// Update timeline system (advance if playing) and sync clip players
		ImGuiIO& io_main = ImGui::GetIO();
		auto dt = ::std::chrono::milliseconds{ static_cast<int>(io_main.DeltaTime) * 1000 };

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

		{
			// Make Canvas a dockable window (do not force position/size)
			ImGui::Begin("Canvas", nullptr, ImGuiWindowFlags_HorizontalScrollbar);

			ImVec2 canvas_size = ImGui::GetContentRegionAvail();
			
			// Ensure minimum size to avoid assertion failure
			if (canvas_size.x < 1.0f) canvas_size.x = 1.0f;
			if (canvas_size.y < 1.0f) canvas_size.y = 1.0f;
			
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
				// 拖动
				if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
					DisplayObject* obj = nullptr;
					// if (hovered_clip_id >= 0) {
					// 	for (auto child : stage.children) {
					// 		if (child && child->id == hovered_clip_id) {
					// 			obj = child;
					// 			break;
					// 		}
					// 	}
					// }

					// MovieClip* clip = dynamic_cast<MovieClip*>(obj);
					// if (clip && !clip->editing_frames) {
					// 	clip->x += io_local.MouseDelta.x / stage.zoom;
					// 	clip->y += io_local.MouseDelta.y / stage.zoom;
					// 	dragging_clip = true;
					// 	stage.selectObject(hovered_clip_id);
					// } else if (hovered_clip_id < 0) {
					// 	stage.pan_x += io_local.MouseDelta.x;
					// 	stage.pan_y += io_local.MouseDelta.y;
					// }
					if (io_local.KeyAlt)
					{
						app_ctx.offset.x += io_local.MouseDelta.x;
						app_ctx.offset.y += io_local.MouseDelta.y;
					}
				}

				// 缩放
				if (io_local.MouseWheel != 0.0f) {
					if (io_local.KeyCtrl)
					{
						float zoom_factor = io_local.MouseWheel > 0.0f ? 1.1f : 0.9f;
						app_ctx.zoom *= zoom_factor;
						app_ctx.zoom = ::std::clamp(app_ctx.zoom, 0.1f, 10.0f);
					}
					else
					{
						if (io_local.KeyShift)
						{
							app_ctx.offset.x += io_local.MouseWheel * 20.0f;
						}
						else
						{
							app_ctx.offset.y += io_local.MouseWheel * 20.0f;
						}
					}
				}

				// 单击选择
				// if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !dragging_clip) {
				// 	stage.selectObject(hovered_clip_id);
				// }

				{
					auto mat4 = ::glm::mat4x4(1.0f);
					mat4 = ::glm::translate(mat4, ::glm::vec3(app_ctx.offset.x, app_ctx.offset.y, 0.0f));
					mat4 = ::glm::scale(mat4, ::glm::vec3(app_ctx.zoom, app_ctx.zoom, 1.0f));
					stage_transform_cache = ::glm::mat3(1.0f);
					stage_transform_cache[0][0] = mat4[0][0];
					stage_transform_cache[0][1] = mat4[0][1];
					stage_transform_cache[1][0] = mat4[1][0];
					stage_transform_cache[1][1] = mat4[1][1];
					stage_transform_cache[2][0] = mat4[3][0];
					stage_transform_cache[2][1] = mat4[3][1];
					stage_transform_cache[2][2] = 1.0f;
				}

				
				if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
				{

					auto hits = ::aned::controller::hit_test(
						*app_ctx.stage
						, app_ctx.stage->get<::aned::component::play_data>().current_frame
						, ::boost::geometry::model::d2::point_xy<float>{canvas_mx, canvas_my}
					, stage_transform_cache
						);

					for (auto&& hit : hits)
					{
						if (hit.any_of<::aned::component::timeline_system>())
						{
							app_ctx.stage = &hit;
						}
						break;
					}
				}
				/*
				// 碰撞检测
				hovered_clip_id = stage.hitTestStage(canvas_mx, canvas_my);

				// 双击进入帧编辑模式
				if(ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hovered_clip_id >= 0) {
					DisplayObject* obj = nullptr;
					for (auto child : stage.children) {
						if (child && child->id == hovered_clip_id) {
							obj = child;
							break;
						}
					}

					MovieClip* clip = dynamic_cast<MovieClip*>(obj);
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
						stage.selectObject(hovered_clip_id);
						dragging_clip = true;
					} else {
						stage.selectObject(hovered_clip_id);
					}
				}*/
			}

			// === 绘制 ===
			ImDrawList* draw_list = ImGui::GetWindowDrawList();
			// stage.renderStageAxes(draw_list, canvas_pos, canvas_size);
			::aned::controller::render_canvas(*app_ctx.stage, app_ctx.stage->get<::aned::component::play_data>().current_frame, stage_transform_cache);
			
			// stage.render(draw_list, canvas_pos, canvas_size, stage.pan_x, stage.pan_y, stage.zoom);

			ImGui::End();
		}
		::ImGui::ShowDemoWindow();
		// === PHASE 4: TIMELINE UI - Timeline面板 ===
		// Make Timeline dockable and movable
		ImGui::Begin("Timeline");

		// Toolbar controls: first, previous, play/pause, next, last, stop, loop toggle
		{
			auto&& play_data = app_ctx.stage->get<::aned::component::play_data>();
			auto&& system = app_ctx.stage->get<::aned::component::timeline_system>();
			auto total_frames = system.frames().size();

			if (ImGui::Button("|<<")) 
			{
				play_data.play = false;
				play_data.current_frame = 0;
			}
			ImGui::SameLine();
			if (ImGui::Button("<<")) {
				play_data.play = false;
				if (play_data.current_frame)
					play_data.current_frame--;
			}
			ImGui::SameLine();
			if (ImGui::Button(play_data.play ? "Pause" : "Play")) 
			{
				play_data.play = !play_data.play;
				if (play_data.play)
				{
					play_data.current_frame = 0;
				}
			}
			ImGui::SameLine();
			if (ImGui::Button(">>")) 
			{
				play_data.play = false;
				if (play_data.current_frame < total_frames - 1)
					play_data.current_frame++;
			}
			ImGui::SameLine();
			if (ImGui::Button(">|")) 
			{
				play_data.play = false;
				play_data.current_frame = total_frames - 1;
			}

			ImGui::SameLine();
			// Frame display and scrubber
			ImGui::Text(::std::format("frame: {} total frames: {} frame rate: {}", play_data.current_frame + 1, total_frames, play_data.frame_rate).c_str());

			if (play_data.play) 
			{
				auto now = clock_type::now();
				auto elapsed = now - frame_rate_record;
				if (elapsed >= ::std::chrono::milliseconds(1000 / play_data.frame_rate)) 
				{
					play_data.current_frame++;
					if (play_data.current_frame >= total_frames) {
						play_data.current_frame = total_frames - 1;
						play_data.play = false;
					}
					frame_rate_record = now;
				}
			}
		}

		{
			// Render timeline system
			// ImDrawList* timeline_draw_list = ImGui::GetWindowDrawList();
			// ImVec2 timeline_pos = ImGui::GetCursorScreenPos();
			// ImVec2 timeline_size = ImGui::GetContentRegionAvail();
			// 
			// // Ensure minimum size to avoid assertion failure
			// if (timeline_size.x < 1.0f) timeline_size.x = 1.0f;
			// if (timeline_size.y < 1.0f) timeline_size.y = 1.0f;
			// 
			// // Create invisible button for mouse input
			// ImGui::InvisibleButton("timeline_area", timeline_size);
			// 
			// // Handle mouse input for timeline
			// if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
			// 	ImGuiIO& io2 = ImGui::GetIO();
			// 	//stage.timeline_system.handleMouseInput(timeline_pos, timeline_size, io2.MousePos);
			// }

			
			render_timeline_context.system = ::std::addressof(app_ctx.stage->get<::aned::component::timeline_system>());
			render_timeline_context.select_layer = ::std::addressof(app_ctx.stage->get<::aned::component::select_timeline_layer>());
			render_timeline_context.play_data = ::std::addressof(app_ctx.stage->get<::aned::component::play_data>());
			::aned::controller::render_timeline_ui(render_timeline_context);
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

