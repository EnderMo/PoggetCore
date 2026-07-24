#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>
#ifdef _WIN32
#undef min
#undef max
#endif
namespace PoggetCore::HistoryFileSystem {

    inline constexpr std::uintmax_t FullContentVerificationLimit = 64ULL * 1024ULL * 1024ULL;
    inline constexpr std::uintmax_t SampleVerificationBlockSize = 1ULL * 1024ULL * 1024ULL;
    inline constexpr size_t SampleVerificationBlockCount = 8;
    inline constexpr wchar_t OperationStorageDirectoryName[] = L".pogget_operations";
    inline constexpr wchar_t LegacyUndoStorageDirectoryName[] = L".pogget_undo";

    struct Result {
        bool success = false;
        std::error_code error;
        std::wstring context;

        explicit operator bool() const noexcept { return success; }
    };

    struct MoveStep {
        std::filesystem::path source;
        std::filesystem::path destination;
    };

    inline std::wstring ComparablePath(const std::filesystem::path& path) {
        if (path.empty()) return {};
        std::error_code ec;
        auto normalized = std::filesystem::absolute(path, ec).lexically_normal().wstring();
        if (ec) normalized = path.lexically_normal().wstring();
#ifdef _WIN32
        std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::towlower);
#endif
        return normalized;
    }

    inline bool Exists(const std::filesystem::path& path) noexcept {
        if (path.empty()) return false;
        std::error_code ec;
        const auto status = std::filesystem::symlink_status(path, ec);
        return !ec && status.type() != std::filesystem::file_type::not_found;
    }

    inline bool IsPathInside(
        const std::filesystem::path& candidate,
        const std::filesystem::path& parent) {
        auto candidateKey = ComparablePath(candidate);
        auto parentKey = ComparablePath(parent);
        if (candidateKey.empty() || parentKey.empty() || candidateKey == parentKey) return false;
        if (parentKey.back() != L'\\' && parentKey.back() != L'/') {
            parentKey.push_back(std::filesystem::path::preferred_separator);
        }
        return candidateKey.starts_with(parentKey);
    }

    inline bool IsHistoryStorageRootName(std::wstring name) {
#ifdef _WIN32
        std::transform(name.begin(), name.end(), name.begin(), ::towlower);
#endif
        return name == OperationStorageDirectoryName ||
            name == LegacyUndoStorageDirectoryName;
    }

    inline bool IsManagedHistoryPath(const std::filesystem::path& path) {
        if (path.empty()) return false;
        std::vector<std::wstring> components;
        for (const auto& component : path.lexically_normal()) {
            auto value = component.wstring();
#ifdef _WIN32
            std::transform(value.begin(), value.end(), value.begin(), ::towlower);
#endif
            components.push_back(std::move(value));
        }
        for (size_t index = 0; index < components.size(); ++index) {
            if (IsHistoryStorageRootName(components[index]) ||
                components[index] == L"poggetundo") {
                return true;
            }
            if (index >= 2 &&
                (components[index] == L"operations" || components[index] == L"undo") &&
                components[index - 1] == L".temp" &&
                components[index - 2] == L".data") {
                return true;
            }
        }
        return false;
    }

    inline bool EquivalentFileContents(
        const std::filesystem::path& left,
        const std::filesystem::path& right,
        bool verifyContent = false) {
        std::error_code ec;
        const auto leftSize = std::filesystem::file_size(left, ec);
        if (ec) return false;
        const auto rightSize = std::filesystem::file_size(right, ec);
        if (ec || leftSize != rightSize) return false;
        if (!verifyContent) return true;

        std::ifstream leftStream(left, std::ios::binary);
        std::ifstream rightStream(right, std::ios::binary);
        if (!leftStream.is_open() || !rightStream.is_open()) return false;

        std::vector<char> leftBuffer(1024 * 1024);
        std::vector<char> rightBuffer(1024 * 1024);

        auto compareRange = [&](std::uintmax_t offset, std::uintmax_t length) {
            if (offset > static_cast<std::uintmax_t>(std::numeric_limits<std::streamoff>::max())) {
                return false;
            }
            leftStream.clear();
            rightStream.clear();
            leftStream.seekg(static_cast<std::streamoff>(offset));
            rightStream.seekg(static_cast<std::streamoff>(offset));
            if (!leftStream.good() || !rightStream.good()) return false;

            while (length > 0) {
                const auto chunk = static_cast<std::streamsize>(
                    std::min<std::uintmax_t>(length, leftBuffer.size()));
                leftStream.read(leftBuffer.data(), chunk);
                rightStream.read(rightBuffer.data(), chunk);
                if (leftStream.gcount() != chunk || rightStream.gcount() != chunk ||
                    !std::equal(leftBuffer.begin(), leftBuffer.begin() + chunk, rightBuffer.begin())) {
                    return false;
                }
                length -= static_cast<std::uintmax_t>(chunk);
            }
            return true;
        };

        if (leftSize <= FullContentVerificationLimit) {
            return compareRange(0, leftSize);
        }

        // Large files are sampled after the operating-system copy has completed
        // successfully. This keeps verification bounded while still checking the
        // beginning, end, and evenly distributed regions of the copied payload.
        const auto blockSize = (std::min)(leftSize, SampleVerificationBlockSize);
        const auto maxOffset = leftSize - blockSize;
        const auto intervals = static_cast<std::uintmax_t>(SampleVerificationBlockCount - 1);
        const auto quotient = maxOffset / intervals;
        const auto remainder = maxOffset % intervals;
        for (std::uintmax_t index = 0; index < SampleVerificationBlockCount; ++index) {
            const auto offset = quotient * index + (remainder * index) / intervals;
            if (!compareRange(offset, blockSize)) return false;
        }
        return true;
    }

    inline bool EquivalentSingleEntry(
        const std::filesystem::path& left,
        const std::filesystem::path& right,
        bool verifyContent = false) {
        std::error_code ec;
        const auto leftStatus = std::filesystem::symlink_status(left, ec);
        if (ec) return false;
        const auto rightStatus = std::filesystem::symlink_status(right, ec);
        if (ec || leftStatus.type() != rightStatus.type()) return false;

        if (std::filesystem::is_regular_file(leftStatus)) {
            return EquivalentFileContents(left, right, verifyContent);
        }
        if (std::filesystem::is_symlink(leftStatus)) {
            const auto leftTarget = std::filesystem::read_symlink(left, ec);
            if (ec) return false;
            const auto rightTarget = std::filesystem::read_symlink(right, ec);
            return !ec && leftTarget == rightTarget;
        }
        return std::filesystem::is_directory(leftStatus);
    }

    inline bool EquivalentPathContents(
        const std::filesystem::path& left,
        const std::filesystem::path& right,
        bool verifyContent = false) {
        if (!EquivalentSingleEntry(left, right, verifyContent)) return false;

        std::error_code ec;
        const auto leftStatus = std::filesystem::symlink_status(left, ec);
        if (ec || !std::filesystem::is_directory(leftStatus)) return !ec;

        size_t leftCount = 0;
        for (std::filesystem::recursive_directory_iterator it(left, ec), end;
            !ec && it != end; it.increment(ec)) {
            ++leftCount;
            const auto relative = it->path().lexically_relative(left);
            if (relative.empty() ||
                !EquivalentSingleEntry(it->path(), right / relative, verifyContent)) return false;
        }
        if (ec) return false;

        size_t rightCount = 0;
        for (std::filesystem::recursive_directory_iterator it(right, ec), end;
            !ec && it != end; it.increment(ec)) {
            ++rightCount;
        }
        return !ec && leftCount == rightCount;
    }
    /*
     Validate an entire move transaction against a simulated file-system state.
     This prevents a late deterministic conflict from exposing a partially applied
     batch and then forcing visible rollback moves.
     */
    inline Result ValidateMoveSequence(const std::vector<MoveStep>& steps) {
        std::unordered_map<std::wstring, bool> simulated;
        auto existsInSimulation = [&](const std::filesystem::path& path) {
            const auto key = ComparablePath(path);
            auto it = simulated.find(key);
            if (it != simulated.end()) return it->second;
            const bool exists = Exists(path);
            simulated.emplace(key, exists);
            return exists;
        };

        for (const auto& step : steps) {
            if (step.source.empty() || step.destination.empty()) {
                return { false, {}, L"history transaction contains an empty path" };
            }
            const auto sourceKey = ComparablePath(step.source);
            const auto destinationKey = ComparablePath(step.destination);
            if (sourceKey == destinationKey) continue;
            if (!existsInSimulation(step.source)) {
                return { false, {}, L"history source is missing: " + step.source.wstring() };
            }
            if (existsInSimulation(step.destination)) {
                return { false, {}, L"history destination is occupied: " + step.destination.wstring() };
            }
            simulated[sourceKey] = false;
            simulated[destinationKey] = true;
        }
        return { true, {}, L"" };
    }

    inline std::filesystem::path MakeUniquePath(
        const std::filesystem::path& root,
        const std::filesystem::path& source,
        const wchar_t* purpose = L"payload",
        bool preserveExtension = false) {
        if (root.empty()) return {};

        std::error_code ec;
        std::filesystem::create_directories(root, ec);
        if (ec) return {};

        static std::atomic<unsigned long long> sequence{ 0 };
        const auto ticks = static_cast<unsigned long long>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        const auto sourceName = source.filename();
        const std::wstring baseName = sourceName.empty()
            ? L"item"
            : sourceName.wstring();
        const std::wstring extension = preserveExtension
            ? sourceName.extension().wstring()
            : L"";
        const std::wstring nameWithoutExtension = preserveExtension && !extension.empty()
            ? sourceName.stem().wstring()
            : baseName;

        for (int attempt = 0; attempt < 128; ++attempt) {
            const auto id = ++sequence;
            auto candidate = root /
                (nameWithoutExtension + L"." + purpose + L"." + std::to_wstring(ticks) +
                    L"." + std::to_wstring(id) + extension);
            if (!Exists(candidate)) return candidate;
        }
        return {};
    }

    inline Result RemovePath(const std::filesystem::path& path) {
        if (path.empty()) return { false, {}, L"empty path" };
        std::error_code ec;
        const auto absolutePath = std::filesystem::absolute(path, ec).lexically_normal();
        if (ec) return { false, ec, L"resolving removal path" };
        if (absolutePath == absolutePath.root_path()) {
            return { false, std::make_error_code(std::errc::operation_not_permitted),
                L"refusing to remove a file-system root" };
        }
        ec.clear();
        const auto status = std::filesystem::symlink_status(path, ec);
        if (ec) return { false, ec, L"checking path" };
        if (status.type() == std::filesystem::file_type::not_found) {
            return { true, {}, L"already absent" };
        }
        ec.clear();
        if (std::filesystem::is_directory(status)) {
            std::filesystem::remove_all(path, ec);
        }
        else {
            std::filesystem::remove(path, ec);
        }
        return { !ec, ec, ec ? L"removing path" : L"" };
    }

    inline Result CopyToStaging(
        const std::filesystem::path& source,
        const std::filesystem::path& destination,
        std::filesystem::path& staging,
        bool verifyContent = false) {
        staging.clear();
        std::error_code ec;
        const auto parent = destination.parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent, ec);
            if (ec) return { false, ec, L"creating destination parent" };
        }

        auto stagingRoot = parent;
        if (stagingRoot.empty()) {
            stagingRoot = std::filesystem::current_path(ec);
            if (ec) return { false, ec, L"resolving staging directory" };
        }
        staging = MakeUniquePath(stagingRoot, destination, L"staging");
        if (staging.empty()) return { false, {}, L"creating staging path" };

        const auto sourceStatus = std::filesystem::symlink_status(source, ec);
        if (ec) return { false, ec, L"reading source status" };

        if (std::filesystem::is_symlink(sourceStatus)) {
            std::filesystem::copy_symlink(source, staging, ec);
        }
        else if (std::filesystem::is_directory(sourceStatus)) {
            std::filesystem::copy(
                source,
                staging,
                std::filesystem::copy_options::recursive |
                    std::filesystem::copy_options::copy_symlinks,
                ec);
        }
        else if (std::filesystem::is_regular_file(sourceStatus)) {
            std::filesystem::copy_file(source, staging, ec);
        }
        else {
            return { false, std::make_error_code(std::errc::operation_not_supported),
                L"unsupported source type" };
        }
        if (ec) {
            RemovePath(staging);
            staging.clear();
            return { false, ec, L"copying to staging path" };
        }
        if (!EquivalentPathContents(source, staging, verifyContent)) {
            RemovePath(staging);
            staging.clear();
            return { false, std::make_error_code(std::errc::io_error),
                L"verifying staged copy" };
        }
        return { true, {}, L"" };
    }

    inline Result CopyPath(
        const std::filesystem::path& source,
        const std::filesystem::path& destination,
        bool verifyContent = false) {
        if (source.empty() || destination.empty()) {
            return { false, {}, L"empty source or destination" };
        }
        if (ComparablePath(source) == ComparablePath(destination)) {
            return { true, {}, L"same path" };
        }
        if (IsPathInside(destination, source)) {
            return { false, {}, L"destination is inside source" };
        }
        if (!Exists(source)) return { false, {}, L"source does not exist" };
        if (Exists(destination)) return { false, {}, L"destination already exists" };

        std::filesystem::path staging;
        auto stagingResult = CopyToStaging(source, destination, staging, verifyContent);
        if (!stagingResult) return stagingResult;

        std::error_code ec;
        std::filesystem::rename(staging, destination, ec);
        if (ec) {
            RemovePath(staging);
            return { false, ec, L"committing copied path" };
        }
        return { true, {}, L"" };
    }

    inline Result MovePath(
        const std::filesystem::path& source,
        const std::filesystem::path& destination,
        bool verifyContent = false) {
        if (source.empty() || destination.empty()) {
            return { false, {}, L"empty source or destination" };
        }
        if (ComparablePath(source) == ComparablePath(destination)) {
            return { true, {}, L"same path" };
        }
        if (IsPathInside(destination, source)) {
            return { false, {}, L"destination is inside source" };
        }
        if (!Exists(source)) return { false, {}, L"source does not exist" };
        if (Exists(destination)) return { false, {}, L"destination already exists" };

        std::error_code ec;
        const auto parent = destination.parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent, ec);
            if (ec) return { false, ec, L"creating destination parent" };
        }

        std::filesystem::rename(source, destination, ec);
        if (!ec) return { true, {}, L"" };

        /*
        Cross-volume moves use a two-phase commit. The source is first copied
        and verified without publishing the destination. It is then renamed to
        a same-volume holding path before the destination becomes visible.
        A cleanup failure can therefore only leave an extra recovery copy.
        */

        std::filesystem::path staging;
        auto stagingResult = CopyToStaging(source, destination, staging, verifyContent);
        if (!stagingResult) return stagingResult;

        auto sourceParent = source.parent_path();
        if (sourceParent.empty()) {
            sourceParent = std::filesystem::current_path(ec);
            if (ec) {
                RemovePath(staging);
                return { false, ec, L"resolving source holding directory" };
            }
        }
        const auto sourceHolding = MakeUniquePath(sourceParent, source, L"moving");
        if (sourceHolding.empty()) {
            RemovePath(staging);
            return { false, {}, L"creating source holding path" };
        }

        ec.clear();
        std::filesystem::rename(source, sourceHolding, ec);
        if (ec) {
            RemovePath(staging);
            return { false, ec, L"moving source to holding path" };
        }

        ec.clear();
        std::filesystem::rename(staging, destination, ec);
        if (ec) {
            std::error_code restoreEc;
            std::filesystem::rename(sourceHolding, source, restoreEc);
            RemovePath(staging);
            if (restoreEc) {
                return { false, restoreEc,
                    L"destination commit failed; source retained at " + sourceHolding.wstring() };
            }
            return { false, ec, L"committing cross-volume destination" };
        }

        if (!EquivalentPathContents(sourceHolding, destination, verifyContent)) {
            auto destinationCleanup = RemovePath(destination);
            std::error_code restoreEc;
            std::filesystem::rename(sourceHolding, source, restoreEc);
            if (restoreEc) {
                return { false, restoreEc,
                    L"destination verification failed; source retained at " + sourceHolding.wstring() };
            }
            return { false,
                destinationCleanup ? std::make_error_code(std::errc::io_error)
                    : destinationCleanup.error,
                L"verifying committed cross-volume destination" };
        }

        auto cleanupResult = RemovePath(sourceHolding);
        if (!cleanupResult) {
            return { true, cleanupResult.error,
                L"move committed; redundant source data retained at " + sourceHolding.wstring() };
        }
        return { true, {}, L"" };
    }

    inline Result BackupDestination(
        const std::filesystem::path& destination,
        const std::filesystem::path& backupRoot,
        std::filesystem::path& backupPath) {
        backupPath.clear();
        if (!Exists(destination)) return { true, {}, L"no destination to back up" };
        backupPath = MakeUniquePath(backupRoot, destination, L"replaced");
        if (backupPath.empty()) return { false, {}, L"cannot create replacement backup path" };
        auto result = MovePath(destination, backupPath);
        if (!result) backupPath.clear();
        return result;
    }

} // namespace PoggetCore::HistoryFileSystem
