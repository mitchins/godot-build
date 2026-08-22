#include "fauxbuild/vfs.hpp"

#include <algorithm>
#include <filesystem>
#include <set>

#include "fauxbuild/file_io.hpp"

namespace fauxbuild {

namespace {

std::string to_key(const std::string& name) {
    std::string key;
    key.reserve(name.size());
    for (const char c : name) {
        key.push_back(c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c);
    }
    return key;
}

} // namespace

Result<std::string> normalize_vfs_name(const std::string& query) {
    if (query.empty()) {
        return Result<std::string>::err({"vfs", 0, "lookup", ErrorCode::InvalidName, "empty name"});
    }
    if (query == "." || query == ".." || query.find('/') != std::string::npos ||
        query.find('\\') != std::string::npos) {
        return Result<std::string>::err(
            {"vfs", 0, "lookup", ErrorCode::PathTraversal,
             "names are flat; separators and traversal are rejected: " + query});
    }
    return Result<std::string>::ok(to_key(query));
}

void Vfs::add_mount(std::unique_ptr<Mount> mount) {
    mounts_.push_back(std::move(mount));
}

Result<VfsFile> Vfs::open(const std::string& query) const {
    auto key = normalize_vfs_name(query);
    if (!key.is_ok()) {
        return Result<VfsFile>::err(key.error());
    }
    for (auto it = mounts_.rbegin(); it != mounts_.rend(); ++it) {
        if ((*it)->contains(key.value())) {
            return (*it)->open(key.value());
        }
    }
    return Result<VfsFile>::err(
        {"vfs", 0, "lookup", ErrorCode::NotFound, "no mount provides " + key.value()});
}

bool Vfs::contains(const std::string& query) const {
    auto key = normalize_vfs_name(query);
    if (!key.is_ok()) {
        return false;
    }
    for (auto it = mounts_.rbegin(); it != mounts_.rend(); ++it) {
        if ((*it)->contains(key.value())) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> Vfs::keys() const {
    std::set<std::string> unique;
    for (const auto& mount : mounts_) {
        for (const auto& key : mount->keys()) {
            unique.insert(key);
        }
    }
    return std::vector<std::string>(unique.begin(), unique.end());
}

VfsDiagnostics Vfs::diagnostics() const {
    VfsDiagnostics diags;
    for (const auto& name : keys()) {
        std::vector<std::string> providers;
        for (const auto& mount : mounts_) {
            if (mount->contains(name)) {
                providers.push_back(mount->describe());
            }
        }
        if (providers.size() > 1) {
            // providers are in mount order: the last added (newest) is active
            // and shadows every earlier one.
            std::string text = name + " active from " + providers.back() + ", shadowing ";
            for (std::size_t i = 0; i + 1 < providers.size(); ++i) {
                if (i > 0) {
                    text += ", ";
                }
                text += providers[i];
            }
            diags.warnings.push_back(std::move(text));
        }
    }
    return diags;
}

MemoryMount::MemoryMount(std::string description) : description_(std::move(description)) {}

void MemoryMount::add_file(const std::string& name, std::vector<std::uint8_t> bytes) {
    auto key = normalize_vfs_name(name);
    if (!key.is_ok()) {
        return; // illegal names are ignored by the in-memory mount (tests author it)
    }
    for (const auto& entry : entries_) {
        if (entry.key == key.value()) {
            return; // first registration wins, same rule as GRP directory entries
        }
    }
    entries_.push_back({key.value(), name, std::move(bytes)});
}

std::string MemoryMount::describe() const {
    return description_;
}

bool MemoryMount::contains(const std::string& key) const {
    return std::any_of(entries_.begin(), entries_.end(),
                       [&](const Entry& e) { return e.key == key; });
}

Result<VfsFile> MemoryMount::open(const std::string& key) const {
    for (const auto& entry : entries_) {
        if (entry.key == key) {
            VfsFile file;
            file.name = entry.key;
            file.origin = description_ + ":" + entry.original;
            file.size = entry.bytes.size();
            file.bytes = entry.bytes;
            return Result<VfsFile>::ok(std::move(file));
        }
    }
    return Result<VfsFile>::err(
        {description_, 0, "lookup", ErrorCode::NotFound, "no entry " + key});
}

std::vector<std::string> MemoryMount::keys() const {
    std::vector<std::string> out;
    out.reserve(entries_.size());
    for (const auto& entry : entries_) {
        out.push_back(entry.key);
    }
    return out;
}

Result<std::unique_ptr<DirectoryMount>> DirectoryMount::create(const std::string& root) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path root_path(root);
    if (!fs::exists(root_path, ec) || !fs::is_directory(root_path, ec)) {
        return Result<std::unique_ptr<DirectoryMount>>::err(
            {root, 0, "directory_mount", ErrorCode::IoError, "not a directory"});
    }

    // Collect, then resolve: directory_iterator order is unspecified (hash
    // derived on ext4), so "first wins" is decided by an explicit sort, not
    // by whatever the filesystem hands us first.
    std::vector<std::pair<std::string, std::string>> found;
    for (fs::directory_iterator it(root_path, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string filename = it->path().filename().string();
        if (ec || !it->is_regular_file(ec) || filename.starts_with('.')) {
            continue; // dotfiles and non-files are skipped (documented)
        }
        const std::string key = to_key(filename);
        if (key.empty() || key == "." || key == ".." || key.find('/') != std::string::npos ||
            key.find('\\') != std::string::npos) {
            continue;
        }
        found.emplace_back(key, filename);
    }
    return Result<std::unique_ptr<DirectoryMount>>::ok(std::unique_ptr<DirectoryMount>(
        new DirectoryMount(root, resolve_file_table(std::move(found)))));
}

std::map<std::string, std::string>
DirectoryMount::resolve_file_table(std::vector<std::pair<std::string, std::string>> entries) {
    std::sort(entries.begin(), entries.end());
    std::map<std::string, std::string> files;
    for (auto& [key, filename] : entries) {
        files.emplace(std::move(key), std::move(filename)); // first wins
    }
    return files;
}

DirectoryMount::DirectoryMount(std::string root, std::map<std::string, std::string> files)
    : root_(std::move(root)), files_(std::move(files)) {}

std::string DirectoryMount::describe() const {
    return "dir:" + root_;
}

bool DirectoryMount::contains(const std::string& key) const {
    return files_.find(key) != files_.end();
}

Result<VfsFile> DirectoryMount::open(const std::string& key) const {
    const auto it = files_.find(key);
    if (it == files_.end()) {
        return Result<VfsFile>::err(
            {"dir:" + root_, 0, "lookup", ErrorCode::NotFound, "no entry " + key});
    }
    const std::string path = (std::filesystem::path(root_) / it->second).string();
    auto bytes = read_file_bytes(path);
    if (!bytes.is_ok()) {
        return Result<VfsFile>::err(bytes.error());
    }
    VfsFile file;
    file.name = key;
    file.origin = describe() + ":" + it->second;
    file.size = bytes.value().size();
    file.bytes = bytes.take();
    return Result<VfsFile>::ok(std::move(file));
}

std::vector<std::string> DirectoryMount::keys() const {
    std::vector<std::string> out;
    out.reserve(files_.size());
    for (const auto& [key, name] : files_) {
        out.push_back(key);
    }
    return out;
}

Result<std::unique_ptr<GrpMount>> GrpMount::create(const std::string& path,
                                                   grp::GrpDiagnostics* diags) {
    auto image = read_file_bytes(path);
    if (!image.is_ok()) {
        return Result<std::unique_ptr<GrpMount>>::err(image.error());
    }
    return from_image("grp:" + path, image.take(), diags);
}

Result<std::unique_ptr<GrpMount>> GrpMount::from_image(std::string origin,
                                                       std::vector<std::uint8_t> image,
                                                       grp::GrpDiagnostics* diags) {
    const std::string_view view(reinterpret_cast<const char*>(image.data()), image.size());
    auto data = grp::parse(view, origin, diags);
    if (!data.is_ok()) {
        return Result<std::unique_ptr<GrpMount>>::err(data.error());
    }
    return Result<std::unique_ptr<GrpMount>>::ok(
        std::unique_ptr<GrpMount>(new GrpMount(std::move(origin), std::move(image), data.take())));
}

GrpMount::GrpMount(std::string origin, std::vector<std::uint8_t> image, grp::GrpData data)
    : origin_(std::move(origin)), image_(std::move(image)), data_(std::move(data)) {}

std::string GrpMount::describe() const {
    return origin_;
}

bool GrpMount::contains(const std::string& key) const {
    for (const auto& entry : data_.entries) {
        if (entry.key == key) {
            return true;
        }
    }
    return false;
}

Result<VfsFile> GrpMount::open(const std::string& key) const {
    for (const auto& entry : data_.entries) {
        if (entry.key != key) {
            continue;
        }
        VfsFile file;
        file.name = entry.key;
        file.origin = origin_ + ":" + entry.name;
        file.size = entry.size;
        file.bytes.assign(image_.begin() + static_cast<std::ptrdiff_t>(entry.offset),
                          image_.begin() + static_cast<std::ptrdiff_t>(entry.offset + entry.size));
        return Result<VfsFile>::ok(std::move(file));
    }
    return Result<VfsFile>::err({origin_, 0, "lookup", ErrorCode::NotFound, "no entry " + key});
}

std::vector<std::string> GrpMount::keys() const {
    std::vector<std::string> out;
    out.reserve(data_.entries.size());
    for (const auto& entry : data_.entries) {
        out.push_back(entry.key);
    }
    return out;
}

} // namespace fauxbuild
