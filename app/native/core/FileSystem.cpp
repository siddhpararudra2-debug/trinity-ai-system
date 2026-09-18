#include "FileSystem.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <system_error>

#include "Uuid.hpp"

namespace trinity::core {

std::filesystem::path FileSystem::normalise(const std::string& raw) {
    std::error_code ec;
    std::filesystem::path absolute = std::filesystem::absolute(std::filesystem::path(raw), ec);
    if (ec) absolute = std::filesystem::path(raw);
    std::filesystem::path normalised = absolute.lexically_normal();
    // Prefer forward slashes in stored metadata for cross-boundary stability.
    std::string text = normalised.generic_string();
    return std::filesystem::path(text);
}

bool FileSystem::is_within(const std::filesystem::path& root,
                           const std::filesystem::path& candidate) {
    const auto root_text = root.lexically_normal().generic_string();
    const auto candidate_text = candidate.lexically_normal().generic_string();
    if (candidate_text == root_text) return true;
    if (candidate_text.size() <= root_text.size()) return false;
    const bool separator = root_text.back() == '/';
    return candidate_text.compare(0, root_text.size(), root_text) == 0 &&
           (separator || candidate_text[root_text.size()] == '/');
}

std::optional<std::filesystem::path> FileSystem::validate_under(const std::string& root,
                                                                const std::string& candidate) {
    if (root.empty() || candidate.empty()) return std::nullopt;
    const std::filesystem::path root_path = normalise(root);
    const std::filesystem::path candidate_path = normalise(candidate);
    if (!is_within(root_path, candidate_path)) return std::nullopt;
    return candidate_path;
}

bool FileSystem::ensure_directory(const std::string& path, std::error_code& ec) {
    if (path.empty()) {
        ec = std::make_error_code(std::errc::invalid_argument);
        return false;
    }
    return std::filesystem::create_directories(normalise(path), ec) || !ec;
}

bool FileSystem::ensure_parent_directories(const std::string& file_path, std::error_code& ec) {
    const std::filesystem::path parent = normalise(file_path).parent_path();
    if (parent.empty()) return true;
    return std::filesystem::create_directories(parent, ec) || !ec;
}

bool FileSystem::remove_all(const std::string& path, std::error_code& ec) {
    const std::filesystem::path target = normalise(path);
    // Guard: refuse to remove filesystem roots or empty paths.
    if (target.empty() || !target.has_filename()) {
        ec = std::make_error_code(std::errc::invalid_argument);
        return false;
    }
    std::filesystem::remove_all(target, ec);
    return !ec;
}

std::optional<std::string> FileSystem::read_file(const std::string& path) {
    std::FILE* file = nullptr;
#if defined(_WIN32)
    if (fopen_s(&file, path.c_str(), "rb") != 0) return std::nullopt;
#else
    file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) return std::nullopt;
#endif
    if (file == nullptr) return std::nullopt;
    std::string contents;
    char buffer[8192];
    std::size_t read = 0;
    while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
        contents.append(buffer, read);
    }
    std::fclose(file);
    return contents;
}

bool FileSystem::write_file_atomic(const std::string& path, std::string_view content,
                                   std::error_code& ec) {
    const std::filesystem::path final_path = normalise(path);
    std::filesystem::path parent = final_path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) return false;
    }
    const std::filesystem::path temp_path = final_path.string() + ".tmp-" + new_uuid().substr(0, 8);
    {
        std::FILE* file = nullptr;
#if defined(_WIN32)
        if (fopen_s(&file, temp_path.string().c_str(), "wb") != 0) {
            ec = std::make_error_code(std::errc::io_error);
            return false;
        }
#else
        file = std::fopen(temp_path.string().c_str(), "wb");
        if (file == nullptr) {
            ec = std::make_error_code(std::errc::io_error);
            return false;
        }
#endif
        if (content.size() > 0) {
            std::fwrite(content.data(), 1, content.size(), file);
        }
        std::fclose(file);
    }
    std::filesystem::rename(temp_path, final_path, ec);
    if (ec) {
        std::filesystem::remove(temp_path, ec);
        return false;
    }
    return true;
}

std::string FileSystem::make_temp_directory(const std::string& base, const std::string& prefix) {
    std::error_code ec;
    std::filesystem::create_directories(normalise(base), ec);
    if (ec) return "";
    for (int attempt = 0; attempt < 8; ++attempt) {
        const std::string candidate =
            normalise(base).string() + "/" + prefix + new_uuid().substr(0, 12);
        if (std::filesystem::create_directory(candidate, ec) && !ec) {
            return candidate;
        }
        ec.clear();
    }
    return "";
}

std::uintmax_t FileSystem::file_size(const std::string& path) {
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(normalise(path), ec);
    return ec ? 0 : size;
}

std::vector<std::string> FileSystem::list_files(const std::string& directory) {
    std::vector<std::string> out;
    std::error_code ec;
    std::filesystem::directory_iterator it(normalise(directory), ec);
    if (ec) return out;
    for (const std::filesystem::directory_entry& entry : it) {
        std::error_code entry_ec;
        if (entry.is_regular_file(entry_ec) && !entry_ec) {
            out.push_back(entry.path().filename().generic_string());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace trinity::core
