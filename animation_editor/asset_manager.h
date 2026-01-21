// Asset Management System - Resource library and browser
// Manages imported assets, textures, and clips

#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <memory>
#include <algorithm>
#include <iostream>

#include <glad/glad.h>
#include "stb_image.h"

namespace fs = std::filesystem;

// ============================================================================
// SECTION 1: ASSET STRUCTURE
// ============================================================================

struct Asset {
	std::string name;                   // Asset name (filename without path)
	std::string path;                   // Full file path
	std::string type;                   // Asset type: "gif", "png", "jpg", etc.
	unsigned long size_bytes = 0;       // File size in bytes
	bool is_directory = false;          // Whether this is a folder
	int texture_id = -1;                // Texture ID if loaded (for preview)
	int frame_count = 1;                // Number of frames (for animated clips)
};

// ============================================================================
// SECTION 2: ASSET LIBRARY - Manages all imported assets
// ============================================================================

class AssetLibrary {
public:
	AssetLibrary() : next_texture_id(1000) {}

	~AssetLibrary() {
		// Texture cleanup is managed here if needed
		for (auto& a : assets) {
			if (a.texture_id > 0) glDeleteTextures(1, (GLuint*)&a.texture_id);
		}
	}

	// Scan directory for assets
	void scanDirectory(const std::string& directory_path) {
		current_directory = directory_path;
		assets.clear();

		if (!fs::exists(directory_path)) {
			std::cerr << "Directory does not exist: " << directory_path << "\n";
			return;
		}

		// Add parent directory entry
		if (directory_path != fs::path(directory_path).root_path().string()) {
			Asset parent;
			parent.name = "..";
			parent.path = fs::path(directory_path).parent_path().string();
			parent.type = "folder";
			parent.is_directory = true;
			assets.push_back(parent);
		}

		// Scan directory contents
		try {
			for (const auto& entry : fs::directory_iterator(directory_path)) {
				Asset asset;
				asset.name = entry.path().filename().string();
				asset.path = entry.path().string();
				asset.is_directory = fs::is_directory(entry);

				if (asset.is_directory) {
					asset.type = "folder";
				} else {
					std::string ext = entry.path().extension().string();
					for (auto& c : ext) c = (char)tolower(c);
					asset.type = ext.size() > 0 ? ext.substr(1) : std::string();

					try {
						asset.size_bytes = fs::file_size(entry);
					} catch (...) {
						asset.size_bytes = 0;
					}
				}

				assets.push_back(asset);
			}
		} catch (const std::exception& e) {
			std::cerr << "Error scanning directory: " << e.what() << "\n";
		}

		// Sort: folders first, then alphabetically
		sortAssets();
	}

	// Get current directory
	const std::string& getCurrentDirectory() const { return current_directory; }

	// Get all assets in current directory
	const std::vector<Asset>& getAssets() const { return assets; }

	// Navigate to subdirectory
	bool navigateToDirectory(const std::string& dir_name) {
		std::string new_path;
		
		if (dir_name == "..") {
			new_path = fs::path(current_directory).parent_path().string();
		} else {
			new_path = (fs::path(current_directory) / dir_name).string();
		}

		if (fs::exists(new_path) && fs::is_directory(new_path)) {
			scanDirectory(new_path);
			return true;
		}
		return false;
	}

	// Find asset index by full path
	int findAssetIndexByPath(const std::string& path) const {
		for (size_t i = 0; i < assets.size(); ++i) {
			if (!assets[i].is_directory && assets[i].path == path) return (int)i;
		}
		return -1;
	}

	// Add or update asset entry from path, returns index
	int addOrUpdateAsset(const std::string& path) {
		int idx = findAssetIndexByPath(path);
		if (idx >= 0) return idx;

		Asset asset;
		asset.path = path;
		asset.name = fs::path(path).filename().string();
		asset.is_directory = false;
		std::string ext = fs::path(path).extension().string();
		for (auto& c : ext) c = (char)tolower(c);
		asset.type = ext.size() > 0 ? ext.substr(1) : std::string();
		try { asset.size_bytes = fs::file_size(path); } catch (...) { asset.size_bytes = 0; }

		assets.push_back(asset);
		sortAssets();
		return (int)assets.size() - 1;
	}

	// Set asset's preview texture id
	void setAssetTexture(int index, int tex_id) {
		if (index < 0 || index >= (int)assets.size()) return;
		assets[index].texture_id = tex_id;
	}

	// Set asset's frame count (for animated clips)
	void setAssetFrameCount(int index, int fc) {
		if (index < 0 || index >= (int)assets.size()) return;
		assets[index].frame_count = fc;
	}

	// Generate thumbnail texture for a file (returns texture id or -1)
	int generateThumbnail(const std::string& path, int max_dim = 128) {
		int w, h, n;
		unsigned char* data = stbi_load(path.c_str(), &w, &h, &n, 4);
		if (!data) return -1;

		// Optionally scale down here; for now use original dimensions and rely on GPU scaling
		GLuint tex = 0;
		glGenTextures(1, &tex);
		glBindTexture(GL_TEXTURE_2D, tex);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glGenerateMipmap(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, 0);
		stbi_image_free(data);
		return (int)tex;
	}

	// Format file size for display
	static std::string formatFileSize(unsigned long bytes) {
		if (bytes == 0) return "0 B";
		const char* units[] = { "B", "KB", "MB", "GB" };
		int unit_idx = 0;
		double size = (double)bytes;
		while (size >= 1024.0 && unit_idx < 3) {
			size /= 1024.0;
			unit_idx++;
		}
		char buffer[32];
		snprintf(buffer, sizeof(buffer), "%.2f %s", size, units[unit_idx]);
		return std::string(buffer);
	}

	// Check if file is supported
	bool isSupportedFormat(const std::string& type) const {
		static const std::vector<std::string> supported = { "gif", "png", "jpg", "jpeg", "bmp" };
		for (const auto& fmt : supported) {
			if (type == fmt) return true;
		}
		return false;
	}

private:
	void sortAssets() {
		std::sort(assets.begin(), assets.end(), [](const Asset& a, const Asset& b) {
			if (a.is_directory != b.is_directory) return a.is_directory > b.is_directory;
			return a.name < b.name;
		});
	}

	std::string current_directory;
	std::vector<Asset> assets;
	int next_texture_id;
};

// ============================================================================
// SECTION 3: RECENT ASSETS - Tracks recently opened files
// ============================================================================

class RecentAssets {
public:
	static const int MAX_RECENT = 10;

	void addRecent(const std::string& path) {
		// Remove if already exists
		auto it = std::find(recent_paths.begin(), recent_paths.end(), path);
		if (it != recent_paths.end()) {
			recent_paths.erase(it);
		}

		// Add to front
		recent_paths.insert(recent_paths.begin(), path);

		// Keep only MAX_RECENT
		if (recent_paths.size() > MAX_RECENT) {
			recent_paths.resize(MAX_RECENT);
		}
	}

	const std::vector<std::string>& getRecent() const {
		return recent_paths;
	}

	void clear() {
		recent_paths.clear();
	}

private:
	std::vector<std::string> recent_paths;
};

