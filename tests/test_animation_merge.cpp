/**
 * @file test_animation_merge.cpp
 * @brief Synthetic GRN fixtures for the external --anim conversion path.
 */

#include "../src/converter/converter.h"
#include "../src/core/grn_writer.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

static nlohmann::json read_glb_json(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    require(static_cast<bool>(file), "Cannot open generated GLB");
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
    require(bytes.size() >= 20, "Generated GLB is too short");
    auto read_u32 = [&](size_t offset) {
        return uint32_t(bytes[offset]) | (uint32_t(bytes[offset + 1]) << 8) |
               (uint32_t(bytes[offset + 2]) << 16) | (uint32_t(bytes[offset + 3]) << 24);
    };
    require(read_u32(0) == 0x46546c67 && read_u32(16) == 0x4e4f534a,
            "Generated file has no GLB JSON chunk");
    const size_t json_size = read_u32(12);
    require(json_size <= bytes.size() - 20, "GLB JSON chunk exceeds file size");
    return nlohmann::json::parse(bytes.begin() + 20, bytes.begin() + 20 + json_size);
}

int main() {
    const fs::path temp = fs::temp_directory_path() /
        ("grn-animation-merge-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {
        fs::path path;
        ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); }
    } cleanup{temp};

    try {
        fs::create_directories(temp);
        const fs::path model_path = temp / "model.grn";
        const fs::path motion_path = temp / "motion.grn";
        const fs::path empty_motion_path = temp / "empty-motion.grn";
        const fs::path plain_path = temp / "plain.glb";
        const fs::path animated_path = temp / "animated.glb";

        grn::GrnModel model;
        grn::GrnBone bone;
        bone.name = "RootBone";
        model.bones.push_back(bone);
        grn::GrnMesh mesh;
        mesh.name = "SyntheticTriangle";
        mesh.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
        mesh.faces = {{{0, 1, 2}}};
        model.meshes.push_back(mesh);
        require(grn::write_grn_file(model_path, model), "Could not write model fixture");

        grn::GrnModel motion;
        motion.bones.push_back(bone);
        grn::GrnAnimation animation;
        animation.name = "SyntheticMotion";
        grn::AnimTrack track;
        track.bone_name = "RootBone";
        track.channel_id = 1;
        track.translation_times = {0.0f, 1.0f};
        track.translations = {{0, 0, 0}, {0, 0, 1}};
        animation.tracks.push_back(track);
        motion.animations.push_back(animation);
        require(grn::write_grn_file(motion_path, motion), "Could not write motion fixture");
        require(grn::write_grn_file(empty_motion_path, model),
                "Could not write empty motion fixture");

        require(grn::convert_file(model_path, plain_path, {}), "Plain conversion failed");
        const auto plain = read_glb_json(plain_path);
        require(!plain.contains("animations") || plain["animations"].empty(),
                "Plain conversion unexpectedly contains animation");

        grn::ConversionOptions options;
        options.anim_file = motion_path;
        require(grn::convert_file(model_path, animated_path, options), "Animation merge failed");
        const auto animated = read_glb_json(animated_path);
        require(animated.contains("animations") && animated["animations"].size() == 1,
                "Merged GLB must contain one animation");
        require(!animated["animations"][0]["channels"].empty(),
                "Merged animation must target at least one bone");

        options.anim_file = empty_motion_path;
        require(!grn::convert_file(model_path, temp / "empty.glb", options),
                "Empty external animation must fail");
        options.anim_file = temp / "missing.grn";
        require(!grn::convert_file(model_path, temp / "missing.glb", options),
                "Missing external animation must fail");
        options.anim_file = motion_path;
        require(!grn::convert_file(plain_path, temp / "reverse.grn", options),
                "--anim must reject GLB -> GRN conversion");

        std::cout << "Synthetic external animation merge tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Animation merge test failed: " << error.what() << '\n';
        return 1;
    }
}
