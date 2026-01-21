// Asset Browser UI - ImGui-based resource browser window

#pragma once

#include <imgui.h>
#include "asset_manager.h"
#include <functional>

// ============================================================================
// ASSET BROWSER WINDOW
// ============================================================================

class AssetBrowser {
public:
	AssetBrowser() : show_window(true), selected_asset_index(-1), icon_size(64.0f) {}

	// Set callback when asset is selected for import
	using OnAssetSelected = std::function<void(const std::string& file_path)>;
	void setOnAssetSelected(OnAssetSelected callback) {
		on_asset_selected = callback;
	}

	// Render the asset browser window
	void render(AssetLibrary& library, bool* p_open = nullptr) {
		if (!show_window) return;

		ImGui::SetNextWindowSize(ImVec2(400, 500), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Asset Browser##window", p_open ? p_open : &show_window)) {
			ImGui::End();
			return;
		}

		// Toolbar
		ImGui::AlignTextToFramePadding();
		ImGui::Text("📁 %s", library.getCurrentDirectory().c_str());

		ImGui::SameLine();
		if (ImGui::SmallButton("🔄 Refresh")) {
			library.scanDirectory(library.getCurrentDirectory());
			selected_asset_index = -1;
		}

		ImGui::SameLine();
		if (ImGui::SmallButton("📤 Up")) {
			library.navigateToDirectory("..");
			selected_asset_index = -1;
		}

		// View options
		ImGui::SameLine();
		ImGui::SetNextItemWidth(100);
		ImGui::SliderFloat("Icon Size##asset_browser", &icon_size, 32.0f, 128.0f);

		ImGui::Separator();

		// Asset list with scrolling
		ImGui::BeginChild("AssetList", ImVec2(0, -40), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);

		const auto& assets = library.getAssets();
		for (size_t i = 0; i < assets.size(); ++i) {
			const Asset& asset = assets[i];
			bool is_selected = (int)i == selected_asset_index;

			// Asset icon and name
			ImGui::PushID((int)i);

			if (asset.is_directory) {
				// Folder display
				if (ImGui::Selectable(("📁 " + asset.name).c_str(), is_selected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, 30))) {
					selected_asset_index = (int)i;

					// Double-click to navigate
					if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
						library.navigateToDirectory(asset.name);
						selected_asset_index = -1;
					}
				}
			} else if (library.isSupportedFormat(asset.type)) {
				// Supported asset display
				std::string icon;
				if (asset.type == "gif") {
					icon = "🎬";
				} else if (asset.type == "png" || asset.type == "jpg" || asset.type == "jpeg") {
					icon = "🖼️";
				} else {
					icon = "📄";
				}

				std::string label = icon + " " + asset.name;
				if (!asset.is_directory && asset.size_bytes > 0) {
					label += " (" + AssetLibrary::formatFileSize(asset.size_bytes) + ")";
				}

				if (ImGui::Selectable(label.c_str(), is_selected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, 30))) {
					selected_asset_index = (int)i;

					// Double-click to import
					if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
						if (on_asset_selected) {
							on_asset_selected(asset.path);
						}
					}
				}

				// Show tooltip with file info
				if (ImGui::IsItemHovered()) {
					ImGui::BeginTooltip();
					ImGui::Text("Name: %s", asset.name.c_str());
					ImGui::Text("Type: %s", asset.type.c_str());
					ImGui::Text("Size: %s", AssetLibrary::formatFileSize(asset.size_bytes).c_str());
					ImGui::EndTooltip();
				}
			} else {
				// Unsupported format (grayed out)
				ImGui::TextDisabled("❌ %s", asset.name.c_str());
			}

			ImGui::PopID();
		}

		ImGui::EndChild();

		ImGui::Separator();

		// Action buttons
		if (ImGui::Button("Import##asset_browser", ImVec2(-1, 0))) {
			if (selected_asset_index >= 0 && selected_asset_index < (int)assets.size()) {
				const Asset& asset = assets[selected_asset_index];
				if (!asset.is_directory && on_asset_selected) {
					on_asset_selected(asset.path);
					selected_asset_index = -1;
				}
			}
		}

		ImGui::End();
	}

	// Toggle window visibility
	void setVisible(bool visible) { show_window = visible; }
	bool isVisible() const { return show_window; }

private:
	bool show_window;
	int selected_asset_index;
	float icon_size;
	OnAssetSelected on_asset_selected;
};

