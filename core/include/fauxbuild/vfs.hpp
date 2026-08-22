#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "fauxbuild/grp.hpp"
#include "fauxbuild/result.hpp"

namespace fauxbuild {

struct VfsFile {
    std::string name;   // normalized (uppercase) name
    std::string origin; // "<mount>:<internal path>"
    std::uint64_t size = 0;
    std::vector<std::uint8_t> bytes;
};

struct VfsDiagnostics {
    std::vector<std::string> warnings;
};

// Normalizes a query name: ASCII uppercase, flat (Build VFS names contain no
// separators). Rejects empty names, '/'/'\\', "." and ".." — path traversal
// is impossible by construction (plan §7.2).
Result<std::string> normalize_vfs_name(const std::string& query);

class Mount {
  public:
    virtual ~Mount() = default;

    virtual std::string describe() const = 0;
    virtual bool contains(const std::string& key) const = 0;
    virtual Result<VfsFile> open(const std::string& key) const = 0;
    virtual std::vector<std::string> keys() const = 0;
};

// Mount stack (plan §7.2). Deterministic precedence: the most recently added
// mount shadows earlier ones. Read-only. Duplicate names across mounts are
// reported through diagnostics().
class Vfs {
  public:
    void add_mount(std::unique_ptr<Mount> mount);

    Result<VfsFile> open(const std::string& query) const;
    bool contains(const std::string& query) const;
    std::vector<std::string> keys() const; // deduplicated union
    VfsDiagnostics diagnostics() const;

    std::size_t mount_count() const { return mounts_.size(); }

  private:
    std::vector<std::unique_ptr<Mount>> mounts_;
};

// In-memory mount for tests. Within a mount the first registration of a name
// wins; later duplicates are ignored (same rule as GRP directory entries).
class MemoryMount final : public Mount {
  public:
    explicit MemoryMount(std::string description);

    void add_file(const std::string& name, std::vector<std::uint8_t> bytes);

    std::string describe() const override;
    bool contains(const std::string& key) const override;
    Result<VfsFile> open(const std::string& key) const override;
    std::vector<std::string> keys() const override;

  private:
    struct Entry {
        std::string key;
        std::string original;
        std::vector<std::uint8_t> bytes;
    };

    std::string description_;
    std::vector<Entry> entries_;
};

// Directory mount (development, loose game files). Snapshots the flat list of
// regular files at create() time; names containing separators and dotfiles
// are skipped. Names are matched case-insensitively via normalization.
class DirectoryMount final : public Mount {
  public:
    static Result<std::unique_ptr<DirectoryMount>> create(const std::string& root);

    std::string describe() const override;
    bool contains(const std::string& key) const override;
    Result<VfsFile> open(const std::string& key) const override;
    std::vector<std::string> keys() const override;

  private:
    DirectoryMount(std::string root, std::map<std::string, std::string> files);

    std::string root_;
    std::map<std::string, std::string> files_; // key -> filename on disk
};

// GRP mount. No extraction: entry bytes are slices of the mounted image.
class GrpMount final : public Mount {
  public:
    static Result<std::unique_ptr<GrpMount>> create(const std::string& path,
                                                    grp::GrpDiagnostics* diags = nullptr);
    static Result<std::unique_ptr<GrpMount>> from_image(std::string origin,
                                                        std::vector<std::uint8_t> image,
                                                        grp::GrpDiagnostics* diags = nullptr);

    std::string describe() const override;
    bool contains(const std::string& key) const override;
    Result<VfsFile> open(const std::string& key) const override;
    std::vector<std::string> keys() const override;

  private:
    GrpMount(std::string origin, std::vector<std::uint8_t> image, grp::GrpData data);

    std::string origin_;
    std::vector<std::uint8_t> image_;
    grp::GrpData data_;
};

} // namespace fauxbuild
