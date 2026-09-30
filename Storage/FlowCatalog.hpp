#pragma once

#include "FlowStorage.hpp"

#include <array>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace PoggetCore::Storage {

    enum class FlowCatalogEntryState {
        ready,
        invalid,
        duplicateId
    };

    struct FlowCatalogEntry {
        std::filesystem::path path;
        FlowDocument document;
        FlowCatalogEntryState state = FlowCatalogEntryState::ready;
        FlowStorageResult error;
    };

    struct FlowCatalogLimits {
        FlowLimits flow{};
        std::size_t maxFiles = 4096;
    };

    struct FlowCatalogResult {
        FlowStorageResult status;
        std::vector<FlowCatalogEntry> entries;

        explicit operator bool() const noexcept { return status.ok(); }
    };

    class FlowCatalog {
    public:
        explicit FlowCatalog(std::filesystem::path root, FlowCatalogLimits limits = {})
            : root_(std::move(root)), limits_(std::move(limits)) {}

        const std::filesystem::path& Root() const noexcept { return root_; }

        FlowCatalogResult Scan() const {
            FlowCatalogResult result;
            std::error_code error;
            if (!std::filesystem::exists(root_, error)) return result;
            if (error || !std::filesystem::is_directory(root_, error) || error) {
                result.status = { FlowStorageError::ioError,
                    "flow catalog root is not a readable directory" };
                return result;
            }

            std::vector<std::filesystem::path> files;
            CollectJsonFiles(root_, true, files, result.status);
            if (!result.status) return result;

            std::unordered_map<std::string, std::size_t> ids;
            result.entries.reserve(files.size());
            for (const auto& path : files) {
                FlowCatalogEntry entry;
                entry.path = path;
                entry.error = LoadReadOnly(path, entry.document, limits_.flow);
                if (!entry.error) {
                    entry.state = FlowCatalogEntryState::invalid;
                }
                else {
                    const auto [iterator, inserted] = ids.emplace(
                        entry.document.id, result.entries.size());
                    if (!inserted) {
                        entry.state = FlowCatalogEntryState::duplicateId;
                        entry.error = { FlowStorageError::conflict,
                            "another installed flow uses the same id", "$.id" };
                        auto& first = result.entries[iterator->second];
                        first.state = FlowCatalogEntryState::duplicateId;
                        first.error = entry.error;
                    }
                }
                result.entries.push_back(std::move(entry));
            }

            std::sort(result.entries.begin(), result.entries.end(),
                [](const FlowCatalogEntry& left, const FlowCatalogEntry& right) {
                    if (left.state != right.state) return left.state < right.state;
                    if (left.document.publisher != right.document.publisher)
                        return left.document.publisher < right.document.publisher;
                    if (left.document.name != right.document.name)
                        return left.document.name < right.document.name;
                    return left.path.native() < right.path.native();
                });
            return result;
        }

        FlowStorageResult ImportCopy(const std::filesystem::path& source,
            std::filesystem::path& installedPath, FlowDocument* installedDocument = nullptr) const {
            installedPath.clear();
            std::error_code error;
            if (!FlowDetail::HasJsonExtension(source))
                return { FlowStorageError::invalidArgument,
                    "flow files must use the .json extension" };
            const auto sourceStatus = std::filesystem::symlink_status(source, error);
            if (error || sourceStatus.type() != std::filesystem::file_type::regular)
                return { FlowStorageError::invalidArgument,
                    "selected flow must be a regular file" };

            FlowDocument document;
            auto loaded = LoadReadOnly(source, document, limits_.flow);
            if (!loaded) return loaded;

            const auto existing = Scan();
            if (!existing) return existing.status;
            for (const auto& entry : existing.entries) {
                if (entry.state != FlowCatalogEntryState::invalid &&
                    entry.document.id == document.id)
                    return { FlowStorageError::conflict,
                        "a flow with the same id is already installed", "$.id" };
            }

            document.enabled = false;
            const auto directory = root_ / PublisherBucket(document.publisher);
            const auto target = directory / (StableToken(document.id) + L".json");
            FlowStorage storage(limits_.flow);
            storage.SetBackupPath(root_ / L".backup" /
                PublisherBucket(document.publisher) /
                (StableToken(document.id) + L".json.bak"));
            auto created = storage.Create(target, document);
            if (!created) return created;
            installedPath = target;
            if (installedDocument) *installedDocument = std::move(document);
            return {};
        }

        FlowStorageResult CreateManaged(const FlowDocument& source,
            std::filesystem::path& installedPath) const {
            installedPath.clear();
            if (source.id.empty())
                return { FlowStorageError::invalidArgument, "flow id cannot be empty", "$.id" };
            const auto existing = Scan();
            if (!existing) return existing.status;
            for (const auto& entry : existing.entries) {
                if (entry.state != FlowCatalogEntryState::invalid &&
                    entry.document.id == source.id)
                    return { FlowStorageError::conflict,
                        "a flow with the same id is already installed", "$.id" };
            }

            FlowDocument document = source;
            document.enabled = false;
            const auto bucket = PublisherBucket(document.publisher);
            const auto target = root_ / bucket / (StableToken(document.id) + L".json");
            FlowStorage storage(limits_.flow);
            storage.SetBackupPath(root_ / L".backup" / bucket /
                (StableToken(document.id) + L".json.bak"));
            auto created = storage.Create(target, document);
            if (!created) return created;
            installedPath = target;
            return {};
        }

        FlowStorageResult UpdateMetadataManaged(const std::filesystem::path& installedPath,
            std::string name, std::string publisher, std::string description,
            FlowDocument* savedDocument = nullptr) const {
            std::filesystem::path relativePath;
            auto validated = ValidateManagedPath(installedPath, relativePath);
            if (!validated) return validated;

            FlowStorage storage(limits_.flow);
            storage.SetBackupPath(BackupPathFor(relativePath));
            FlowDocument document;
            auto loaded = storage.Load(installedPath, document);
            if (!loaded) return loaded;

            document.name = std::move(name);
            document.publisher = std::move(publisher);
            document.description = std::move(description);
            auto advanced = AdvanceRevision(document);
            if (!advanced) return advanced;
            auto saved = storage.Save(document);
            if (saved && savedDocument) *savedDocument = document;
            return saved;
        }

        FlowStorageResult UpdateEnabledManaged(const std::filesystem::path& installedPath,
            std::int64_t expectedRevision, bool enabled,
            FlowDocument* savedDocument = nullptr) const {
            std::filesystem::path relativePath;
            auto validated = ValidateManagedPath(installedPath, relativePath);
            if (!validated) return validated;

            FlowStorage storage(limits_.flow);
            storage.SetBackupPath(BackupPathFor(relativePath));
            FlowDocument document;
            auto loaded = storage.Load(installedPath, document);
            if (!loaded) return loaded;
            if (document.revision != expectedRevision)
                return { FlowStorageError::conflict,
                    "flow revision changed; reload before updating its enabled state",
                    "$.revision" };

            document.enabled = enabled;
            auto advanced = AdvanceRevision(document);
            if (!advanced) return advanced;
            auto saved = storage.Save(document);
            if (saved && savedDocument) *savedDocument = document;
            return saved;
        }

        FlowStorageResult UpdateDefinitionManaged(const std::filesystem::path& installedPath,
            std::int64_t expectedRevision, std::optional<FlowNode> trigger,
            std::vector<FlowNode> actions, FlowDocument* savedDocument = nullptr) const {
            std::filesystem::path relativePath;
            auto validated = ValidateManagedPath(installedPath, relativePath);
            if (!validated) return validated;

            FlowStorage storage(limits_.flow);
            storage.SetBackupPath(BackupPathFor(relativePath));
            FlowDocument document;
            auto loaded = storage.Load(installedPath, document);
            if (!loaded) return loaded;
            if (document.revision != expectedRevision)
                return { FlowStorageError::conflict,
                    "flow revision changed; reload before saving the editor",
                    "$.revision" };

            document.trigger = std::move(trigger);
            document.actions = std::move(actions);
            auto advanced = AdvanceRevision(document);
            if (!advanced) return advanced;
            auto saved = storage.Save(document);
            if (saved && savedDocument) *savedDocument = document;
            return saved;
        }

        FlowStorageResult DeleteManaged(const std::filesystem::path& installedPath) const {
            std::filesystem::path relativePath;
            auto validated = ValidateManagedPath(installedPath, relativePath);
            if (!validated) return validated;

#ifdef _WIN32
            FlowDetail::ProcessLock processLock(installedPath);
            if (!processLock.locked())
                return { FlowStorageError::busy, "flow JSON file is busy in another process" };
#endif

            std::error_code error;
            const bool removed = std::filesystem::remove(installedPath, error);
            if (error || !removed)
                return { FlowStorageError::ioError, "could not delete flow JSON file" };

            std::error_code ignored;
            std::filesystem::remove(BackupPathFor(relativePath), ignored);
            std::filesystem::remove(
                std::filesystem::path(installedPath.wstring() + L".bak"), ignored);
            return {};
        }

        static FlowStorageResult LoadReadOnly(const std::filesystem::path& path,
            FlowDocument& document, const FlowLimits& limits = {}) {
            if (!FlowDetail::HasJsonExtension(path))
                return { FlowStorageError::invalidArgument,
                    "flow files must use the .json extension" };
            std::string bytes;
            auto read = FlowDetail::ReadFile(path, bytes, limits);
            if (!read) return read;
            return ParseFlowJson(bytes, document, limits);
        }

    private:

        static FlowStorageResult AdvanceRevision(FlowDocument& document) {
            if (document.revision == (std::numeric_limits<std::int64_t>::max)())
                return { FlowStorageError::resourceLimit,
                    "flow revision cannot be advanced", "$.revision" };
            ++document.revision;
            return {};
        }

        FlowStorageResult ValidateManagedPath(const std::filesystem::path& path,
            std::filesystem::path& relativePath) const {
            relativePath.clear();
            if (!FlowDetail::HasJsonExtension(path))
                return { FlowStorageError::invalidArgument,
                    "flow files must use the .json extension" };

            std::error_code error;
            const auto absoluteRoot = std::filesystem::absolute(root_, error).lexically_normal();
            if (error) return { FlowStorageError::ioError,
                "could not resolve the flow catalog root" };
            const auto absolutePath = std::filesystem::absolute(path, error).lexically_normal();
            if (error) return { FlowStorageError::ioError,
                "could not resolve the flow JSON path" };
            relativePath = absolutePath.lexically_relative(absoluteRoot);
            if (relativePath.empty() || relativePath.is_absolute())
                return { FlowStorageError::invalidArgument,
                    "flow JSON path is outside the managed catalog" };

            std::size_t componentCount = 0;
            for (const auto& component : relativePath) {
                const auto name = component.wstring();
                if (name.empty() || name == L"." || name == L".." ||
                    (componentCount == 0 && name.front() == L'.'))
                    return { FlowStorageError::invalidArgument,
                        "flow JSON path is outside the managed catalog" };
                ++componentCount;
            }
            if (componentCount == 0 || componentCount > 2)
                return { FlowStorageError::invalidArgument,
                    "flow JSON path is not at a managed catalog depth" };

            const auto status = std::filesystem::symlink_status(absolutePath, error);
            if (error || status.type() != std::filesystem::file_type::regular)
                return { FlowStorageError::invalidArgument,
                    "managed flow JSON must be a regular file" };
            return {};
        }

        std::filesystem::path BackupPathFor(
            const std::filesystem::path& relativePath) const {
            return root_ / L".backup" / relativePath.parent_path() /
                std::filesystem::path(relativePath.filename().wstring() + L".bak");
        }

        void CollectJsonFiles(const std::filesystem::path& directory, bool rootLevel,
            std::vector<std::filesystem::path>& files, FlowStorageResult& status) const {
            std::error_code error;
            for (std::filesystem::directory_iterator iterator(
                directory, std::filesystem::directory_options::skip_permission_denied, error), end;
                !error && iterator != end; iterator.increment(error)) {
                const auto symlinkStatus = iterator->symlink_status(error);
                if (error) break;
                if (symlinkStatus.type() == std::filesystem::file_type::regular &&
                    FlowDetail::HasJsonExtension(iterator->path())) {
                    if (limits_.maxFiles != 0 && files.size() >= limits_.maxFiles) {
                        status = { FlowStorageError::resourceLimit,
                            "flow catalog file count exceeds the limit" };
                        return;
                    }
                    files.push_back(iterator->path());
                }
                else if (rootLevel && symlinkStatus.type() == std::filesystem::file_type::directory) {
                    const auto name = iterator->path().filename().wstring();
                    if (!name.empty() && name.front() != L'.')
                        CollectJsonFiles(iterator->path(), false, files, status);
                    if (!status) return;
                }
            }
            if (error) status = { FlowStorageError::ioError,
                "could not enumerate the flow catalog" };
        }

        static std::uint64_t Hash(std::string_view value) noexcept {
            std::uint64_t hash = 1469598103934665603ULL;
            for (const unsigned char character : value) {
                hash ^= character;
                hash *= 1099511628211ULL;
            }
            return hash;
        }

        static std::wstring Hex(std::uint64_t value) {
            std::wostringstream stream;
            stream << std::hex << std::setfill(L'0') << std::setw(16) << value;
            return stream.str();
        }

        static std::wstring StableToken(std::string_view value) {
            return Hex(Hash(value));
        }

        static std::wstring PublisherBucket(std::string_view publisher) {
            return publisher.empty() ? L"local" : L"publisher-" + StableToken(publisher);
        }

        std::filesystem::path root_;
        FlowCatalogLimits limits_;
    };

} // namespace PoggetCore::Storage
