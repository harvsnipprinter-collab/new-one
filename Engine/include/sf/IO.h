// SculptForge 3D - mesh import / export
#pragma once
#include "Scene.h"
#include <filesystem>

namespace sf {

enum class FileFormat : int { STL = 0, OBJ = 1, PLY = 2, GLB = 3, ThreeMF = 4 };

struct ExportItem {
    const Mesh* mesh = nullptr;
    Mat4 world;
    std::string name;
    Material mat;
};

// scale converts scene units to file units (for STL / 3MF the file unit is millimetres).
// STL, PLY, 3MF: transforms are baked and all items written as one mesh / one build.
// OBJ: baked transforms, one `o` group per item, vertex colours as "v x y z r g b".
// GLB: one node + mesh + PBR material per item, vertex colours as COLOR_0.
bool exportFile(const std::vector<ExportItem>& items, FileFormat format, const std::string& path, float scale,
                std::string* error = nullptr);

struct ImportedObject {
    Mesh mesh;
    std::string name;
    Mat4 transform;
    bool hasMaterial = false;
    Material mat;
};
// Format is chosen from the file extension: .stl .obj .ply .glb
bool importFile(const std::string& path, std::vector<ImportedObject>& out, std::string* error = nullptr);

inline std::filesystem::path toPath(const std::string& utf8) {
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}
bool readWholeFile(const std::string& path, std::vector<uint8_t>& data);
bool writeWholeFile(const std::string& path, const std::vector<uint8_t>& data);

} // namespace sf
