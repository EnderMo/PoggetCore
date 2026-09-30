#pragma once

#include "FlowDataflow.hpp"
#include "FlowTrust.hpp"
#include "../PoggetHistoryFileSystem.hpp"
#include "../Storage/FlowCatalog.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace PoggetCore::Flow {

    struct ContainerSnapshot {
        std::string id;
        std::vector<std::filesystem::path> paths;
    };

    struct MapFileResult {
        bool success = false;
        std::filesystem::path mappedPath;
        std::wstring error;
    };

    struct ResolveContainerFolderResult {
        bool success = false;
        std::filesystem::path folder;
        std::wstring error;
    };

    struct ShowTipResult {
        bool success = false;
        bool accepted = false;
        std::wstring error;
    };

    struct CreateContainerResult {
        bool success = false;
        std::string id;
        std::wstring name;
        std::wstring error;
    };

    struct RenameContainerResult {
        bool success = false;
        std::wstring error;
    };

    struct RuntimeAdapters {
        std::function<ResolveContainerFolderResult(const std::string&)>
            resolveMoveTargetFolder;
        std::function<MapFileResult(const std::string&,
            const std::filesystem::path&, MapInsertPosition)> mapFile;
        std::function<ShowTipResult(const std::wstring&, const std::wstring&)> showTip;
        std::function<CreateContainerResult(const std::wstring&, std::string_view,
            const std::wstring&)> createContainer;
        std::function<RenameContainerResult(const std::string&, const std::wstring&)>
            renameContainer;
        std::function<void(const std::wstring&, const std::wstring&)> log;
        std::function<std::chrono::system_clock::time_point()> currentTime;
    };

    class FlowRuntime final {
    public:
        explicit FlowRuntime(std::filesystem::path root = {}, RuntimeAdapters adapters = {})
            : root_(std::move(root)), adapters_(std::move(adapters)),
            worker_([this](std::stop_token stop) { WorkerLoop(stop); }) {}

        ~FlowRuntime() {
            worker_.request_stop();
            condition_.notify_all();
            if (worker_.joinable()) worker_.join();
        }

        FlowRuntime(const FlowRuntime&) = delete;
        FlowRuntime& operator=(const FlowRuntime&) = delete;

        void Configure(std::filesystem::path root, RuntimeAdapters adapters) {
            {
                std::lock_guard lock(mutex_);
                root_ = std::move(root);
                adapters_ = std::move(adapters);
                resetMonitoringPending_ = true;
            }
            condition_.notify_one();
        }

        void UpdateContainerSnapshots(std::vector<ContainerSnapshot> snapshots) {
            std::lock_guard lock(mutex_);
            latestContainers_.clear();
            for (auto& snapshot : snapshots) {
                if (snapshot.id.empty()) continue;
                latestContainers_.emplace(snapshot.id, std::move(snapshot.paths));
            }
        }

        void RequestTick() {
            {
                std::lock_guard lock(mutex_);
                tickPending_ = true;
            }
            condition_.notify_one();
        }

        void RequestManualRun(std::filesystem::path installedPath) {
            if (installedPath.empty()) return;
            std::function<void(const std::wstring&, const std::wstring&)> logger;
            bool queued = false;
            {
                std::lock_guard lock(mutex_);
                if (manualQueue_.size() >= maxManualQueue_) {
                    logger = adapters_.log;
                }
                else {
                    manualQueue_.push_back(std::move(installedPath));
                    queued = true;
                }
            }
            if (!queued) {
                if (logger) logger(L"WARNING", L"Flow manual-run queue is full.");
                return;
            }
            condition_.notify_one();
        }

        bool Busy() const {
            std::lock_guard lock(mutex_);
            return busy_ || tickPending_ || !manualQueue_.empty();
        }

        bool NeedsContainerSnapshots() const noexcept {
			return needsContainerSnapshots_.load(std::memory_order_relaxed);
		}

    private:
        enum class EventKind { created, removed, modified, added };

        struct Event {
            EventKind kind = EventKind::created;
            std::filesystem::path path;
            std::string containerId;
        };

        struct EntryStamp {
            std::uintmax_t size = 0;
            std::filesystem::file_time_type writeTime{};
            bool directory = false;

            bool operator==(const EntryStamp& other) const noexcept {
                return size == other.size && writeTime == other.writeTime &&
                    directory == other.directory;
            }
        };

        using DirectorySnapshot = std::map<std::filesystem::path, EntryStamp>;

        struct DirectoryState {
            DirectorySnapshot snapshot;
            bool initialized = false;
        };

        struct ContainerState {
            std::set<std::filesystem::path> paths;
            bool initialized = false;
        };

        struct TriggerScheduleState {
            std::uint64_t lastTick = 0;
            std::int64_t lastTimeSlot = -1;
        };

        using RuntimeValue = std::variant<std::filesystem::path, std::wstring, bool>;
        using Context = std::unordered_map<std::wstring, RuntimeValue>;

        static std::wstring Utf8ToWide(std::string_view text) {
            if (text.empty()) return {};
#ifdef _WIN32
            if (text.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
                return {};
            const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                text.data(), static_cast<int>(text.size()), nullptr, 0);
            if (length <= 0) return {};
            std::wstring result(static_cast<std::size_t>(length), L'\0');
            if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                text.data(), static_cast<int>(text.size()), result.data(), length) != length)
                return {};
            return result;
#else
            std::wstring result;
            result.reserve(text.size());
            for (const unsigned char character : text) result.push_back(character);
            return result;
#endif
        }

        static std::string WideToUtf8(std::wstring_view text) {
#ifdef _WIN32
            if (text.empty()) return {};
            const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
            if (length <= 0) return {};
            std::string result(static_cast<std::size_t>(length), '\0');
            if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                static_cast<int>(text.size()), result.data(), length, nullptr, nullptr) != length)
                return {};
            return result;
#else
            return std::string(text.begin(), text.end());
#endif
        }

        static std::filesystem::path PathFromUtf8(std::string_view text) {
#if defined(__cpp_lib_char8_t)
            const auto* begin = reinterpret_cast<const char8_t*>(text.data());
            return std::filesystem::path(std::u8string(begin, begin + text.size()));
#else
            return std::filesystem::u8path(text.begin(), text.end());
#endif
        }

        static std::wstring EventName(EventKind kind) {
            switch (kind) {
            case EventKind::created: return L"created";
            case EventKind::removed: return L"removed";
            case EventKind::modified: return L"modified";
            case EventKind::added: return L"added";
            }
            return {};
        }

        static const Storage::FlowJson::Value* Parameter(
            const Storage::FlowNode& node, std::string_view key) noexcept {
            return Storage::FlowJson::Find(node.parameters, key);
        }

        static std::string StringParameter(const Storage::FlowNode& node,
            std::string_view key, std::string fallback = {}) {
            const auto* value = Parameter(node, key);
            const auto* text = value ? value->get_if<std::string>() : nullptr;
            return text ? *text : std::move(fallback);
        }

        static bool BoolParameter(const Storage::FlowNode& node,
            std::string_view key, bool fallback) {
            const auto* value = Parameter(node, key);
            const auto* boolean = value ? value->get_if<bool>() : nullptr;
            return boolean ? *boolean : fallback;
        }

        static std::int64_t IntegerParameter(const Storage::FlowNode& node,
            std::string_view key, std::int64_t fallback) {
            const auto* value = Parameter(node, key);
            const auto* integer = value ? value->get_if<std::int64_t>() : nullptr;
            return integer ? *integer : fallback;
        }

        static bool IsSafeRegexPattern(std::wstring_view pattern) noexcept {
            if (pattern.size() > maxRegexCharacters_) return false;
            bool escaped = false;
            bool inClass = false;
            for (wchar_t character : pattern) {
                if (escaped) {
                    if (character >= L'0' && character <= L'9') return false;
                    escaped = false;
                    continue;
                }
                if (character == L'\\') {
                    escaped = true;
                    continue;
                }
                if (character == L'[') inClass = true;
                else if (character == L']') inClass = false;
                else if (!inClass && (character == L'(' || character == L')' ||
                    character == L'|' || character == L'{' || character == L'}')) return false;
            }
            return !escaped && !inClass;
        }

        static bool NameMatches(const std::filesystem::path& path,
            const std::string& utf8Pattern) {
            if (utf8Pattern.empty()) return true;
            const auto pattern = Utf8ToWide(utf8Pattern);
            const auto name = path.filename().wstring();
            if (pattern.empty() || name.size() > maxRegexInputCharacters_ ||
                !IsSafeRegexPattern(pattern)) return false;
            try {
                const std::wregex expression(pattern,
                    std::regex_constants::ECMAScript | std::regex_constants::optimize);
                return std::regex_search(name, expression);
            }
            catch (const std::regex_error&) {
                return false;
            }
        }

        static std::wstring ValueText(const RuntimeValue& value) {
            if (const auto* path = std::get_if<std::filesystem::path>(&value))
                return path->wstring();
            if (const auto* text = std::get_if<std::wstring>(&value)) return *text;
            return std::get<bool>(value) ? L"true" : L"false";
        }

        static std::wstring Expand(std::wstring_view input, const Context& context) {
            std::wstring output;
            output.reserve((std::min)(input.size() + 32, maxExpandedCharacters_));
            std::size_t cursor = 0;
            const auto isBuiltinName = [](std::wstring_view name) {
                if (name.empty()) return false;
                return std::all_of(name.begin(), name.end(), [](wchar_t character) {
                    return (character >= L'a' && character <= L'z') ||
                        (character >= L'A' && character <= L'Z') ||
                        (character >= L'0' && character <= L'9') ||
                        character == L'_';
                });
            };
            while (cursor < input.size() && output.size() < maxExpandedCharacters_) {
                const auto expression = input.find(L"${", cursor);
                auto builtin = input.find(L'$', cursor);
                while (builtin != std::wstring_view::npos) {
                    const auto end = input.find(L'$', builtin + 1);
                    if (end != std::wstring_view::npos &&
                        isBuiltinName(input.substr(builtin + 1, end - builtin - 1)))
                        break;
                    builtin = input.find(L'$', builtin + 1);
                }
                std::size_t marker = expression;
                bool builtinMarker = false;
                if (builtin != std::wstring_view::npos &&
                    (marker == std::wstring_view::npos || builtin < marker)) {
                    marker = builtin;
                    builtinMarker = true;
                }
                if (marker == std::wstring_view::npos) {
                    output.append(input.substr(cursor,
                        (std::min)(input.size() - cursor,
                            maxExpandedCharacters_ - output.size())));
                    break;
                }
                output.append(input.substr(cursor, marker - cursor));
                const auto end = input.find(builtinMarker ? L'$' : L'}',
                    marker + (builtinMarker ? 1 : 2));
                if (end == std::wstring_view::npos) {
                    output.append(input.substr(marker));
                    break;
                }
                const std::wstring key(input.substr(marker + (builtinMarker ? 1 : 2),
                    end - marker - (builtinMarker ? 1 : 2)));
                const auto found = context.find(key);
                if (found != context.end()) output.append(ValueText(found->second));
                else output.append(input.substr(marker, end - marker + 1));
                cursor = end + 1;
            }
            if (output.size() > maxExpandedCharacters_) output.resize(maxExpandedCharacters_);
            return output;
        }

        static bool EventAccepted(EventKind kind, std::string_view configured) noexcept {
            if (configured == "any") return true;
            if (configured == "created") return kind == EventKind::created;
            if (configured == "removed") return kind == EventKind::removed;
            if (configured == "modified") return kind == EventKind::modified;
            if (configured == "added") return kind == EventKind::added;
            return false;
        }

        static void SetNodeOutput(Context& context, const Storage::FlowNode& node,
            std::string_view port, RuntimeValue value) {
            const auto key = Utf8ToWide(FlowOutputKey(node, port));
            context[key] = value;
            if (port == "file" || port == "path") {
                const auto text = ValueText(value);
                context[L"FilePath"] = value;
                context[L"FileName"] = std::filesystem::path(text).filename().wstring();
                context[L"FileExtension"] = std::filesystem::path(text).extension().wstring();
                context[L"FileDirectory"] = std::filesystem::path(text).parent_path().wstring();
            }
        }

        static void SetNodeOutput(Context& context, const Storage::FlowNode& node,
            std::string_view port, const wchar_t* value) {
            SetNodeOutput(context, node, port, std::wstring(value ? value : L""));
        }

        static std::optional<std::filesystem::path> ResolveFileInput(
            const Storage::FlowNode& node, std::string_view inputId,
            std::string_view legacyParameter, const Context& context) {
            if (const auto* binding = FindFlowInput(node, inputId)) {
                const auto found = context.find(Utf8ToWide(
                    FlowOutputKey(binding->nodeId, binding->portId)));
                if (found == context.end()) return std::nullopt;
                if (const auto* path = std::get_if<std::filesystem::path>(&found->second))
                    return *path;
                if (const auto* text = std::get_if<std::wstring>(&found->second)) {
                    if (!text->empty()) return std::filesystem::path(*text);
                }
                return std::nullopt;
            }
            const auto legacy = Expand(
                Utf8ToWide(StringParameter(node, legacyParameter)), context);
            if (legacy.empty()) return std::nullopt;
            return std::filesystem::path(legacy);
        }

        static const RuntimeValue* ResolveBoundInput(
            const Storage::FlowNode& node, std::string_view inputId,
            const Context& context) noexcept {
            const auto* binding = FindFlowInput(node, inputId);
            if (!binding) return nullptr;
            const auto found = context.find(Utf8ToWide(
                FlowOutputKey(binding->nodeId, binding->portId)));
            return found == context.end() ? nullptr : &found->second;
        }

        static std::wstring WatchKey(const std::filesystem::path& installedPath,
            const Storage::FlowNode& trigger) {
            const auto path = installedPath.lexically_normal().wstring();
            return std::to_wstring(path.size()) + L":" + path + Utf8ToWide(trigger.id);
        }

        static bool TriggerIntervalDue(const Storage::FlowNode& trigger,
            TriggerScheduleState& state, std::uint64_t tick) noexcept {
            const auto intervalMs = IntegerParameter(trigger, "intervalMs", 2'600);
            const auto intervalTicks = static_cast<std::uint64_t>(
                (std::max)(std::int64_t{ 1 }, intervalMs / 2'600));
            if (state.lastTick != 0 && tick - state.lastTick < intervalTicks)
                return false;
            state.lastTick = tick;
            return true;
        }

        std::optional<std::pair<std::int64_t, std::pair<int, int>>>
            CurrentLocalTimeSlot() const noexcept {
            std::chrono::system_clock::time_point now;
            try {
                now = activeAdapters_.currentTime ? activeAdapters_.currentTime() :
                    std::chrono::system_clock::now();
            }
            catch (...) { return std::nullopt; }
            const std::time_t value = std::chrono::system_clock::to_time_t(now);
            std::tm local{};
#ifdef _WIN32
            if (localtime_s(&local, &value) != 0) return std::nullopt;
#else
            if (!localtime_r(&value, &local)) return std::nullopt;
#endif
            const auto localSlot = (static_cast<std::int64_t>(local.tm_year) * 366 +
                local.tm_yday) * 24 * 60 + local.tm_hour * 60 + local.tm_min;
            return std::pair{ localSlot,
                std::pair{ local.tm_hour, local.tm_min } };
        }

        static bool IsSafeFileName(const std::wstring& name) noexcept {
            if (name.empty() || name == L"." || name == L".." ||
                name.size() > 255 || name.back() == L'.' || name.back() == L' ')
                return false;
            for (const wchar_t character : name) {
                if (character < 32 || character == L'<' || character == L'>' ||
                    character == L':' || character == L'"' || character == L'/' ||
                    character == L'\\' || character == L'|' || character == L'?' ||
                    character == L'*') return false;
            }
            auto stem = name.substr(0, name.find(L'.'));
            std::transform(stem.begin(), stem.end(), stem.begin(), ::towupper);
            static constexpr std::wstring_view reserved[] = {
                L"CON", L"PRN", L"AUX", L"NUL", L"COM1", L"COM2", L"COM3",
                L"COM4", L"COM5", L"COM6", L"COM7", L"COM8", L"COM9",
                L"LPT1", L"LPT2", L"LPT3", L"LPT4", L"LPT5", L"LPT6",
                L"LPT7", L"LPT8", L"LPT9"
            };
            return std::none_of(std::begin(reserved), std::end(reserved),
                [&stem](std::wstring_view value) { return stem == value; });
        }

        static bool ReadTextFile(const std::filesystem::path& source,
            std::size_t maximumBytes, std::wstring& output,
            std::wstring& message) {
            output.clear();
            message.clear();
            maximumBytes = (std::min)(maximumBytes, std::size_t{ 32 * 1024 });
            std::error_code error;
            const auto status = std::filesystem::symlink_status(source, error);
            if (error || status.type() != std::filesystem::file_type::regular) {
                message = L"source is not a regular file: " + source.wstring();
                return false;
            }
            const auto size = std::filesystem::file_size(source, error);
            if (error) {
                message = L"file size could not be read";
                return false;
            }
            if (size > maximumBytes) {
                message = L"file exceeds the configured read limit";
                return false;
            }
            std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
            std::ifstream input(source, std::ios::binary);
            if (!input) {
                message = L"file could not be opened for reading";
                return false;
            }
            if (!bytes.empty()) {
                input.read(reinterpret_cast<char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
                if (input.gcount() != static_cast<std::streamsize>(bytes.size())) {
                    message = L"file could not be read completely";
                    return false;
                }
            }

            std::size_t offset = 0;
            bool utf16 = false;
            bool bigEndian = false;
            if (bytes.size() >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb &&
                bytes[2] == 0xbf) offset = 3;
            else if (bytes.size() >= 2 && bytes[0] == 0xff && bytes[1] == 0xfe) {
                utf16 = true;
                offset = 2;
            }
            else if (bytes.size() >= 2 && bytes[0] == 0xfe && bytes[1] == 0xff) {
                utf16 = true;
                bigEndian = true;
                offset = 2;
            }

            if (utf16) {
                if ((bytes.size() - offset) % 2 != 0) {
                    message = L"UTF-16 text has an incomplete code unit";
                    return false;
                }
                output.reserve((bytes.size() - offset) / 2);
                for (std::size_t index = offset; index < bytes.size(); index += 2) {
                    const auto unit = static_cast<std::uint16_t>(bigEndian ?
                        (static_cast<std::uint16_t>(bytes[index]) << 8) |
                            bytes[index + 1] :
                        (static_cast<std::uint16_t>(bytes[index + 1]) << 8) |
                            bytes[index]);
                    if (unit == 0) {
                        message = L"file appears to contain binary data";
                        return false;
                    }
                    if (unit >= 0xd800 && unit <= 0xdbff) {
                        if (index + 3 >= bytes.size()) {
                            message = L"UTF-16 text contains an invalid surrogate";
                            return false;
                        }
                        const auto next = static_cast<std::uint16_t>(bigEndian ?
                            (static_cast<std::uint16_t>(bytes[index + 2]) << 8) |
                                bytes[index + 3] :
                            (static_cast<std::uint16_t>(bytes[index + 3]) << 8) |
                                bytes[index + 2]);
                        if (next < 0xdc00 || next > 0xdfff) {
                            message = L"UTF-16 text contains an invalid surrogate";
                            return false;
                        }
                    }
                    else if (unit >= 0xdc00 && unit <= 0xdfff) {
                        const bool followsHigh = !output.empty() &&
                            output.back() >= 0xd800 && output.back() <= 0xdbff;
                        if (!followsHigh) {
                            message = L"UTF-16 text contains an invalid surrogate";
                            return false;
                        }
                    }
                    output.push_back(static_cast<wchar_t>(unit));
                }
            }
            else {
                if (std::find(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                    bytes.end(), 0) != bytes.end()) {
                    message = L"file appears to contain binary data";
                    return false;
                }
                std::string utf8;
                if (offset < bytes.size()) utf8.assign(
                    reinterpret_cast<const char*>(bytes.data() + offset),
                    bytes.size() - offset);
                output = Utf8ToWide(utf8);
                if (!utf8.empty() && output.empty()) {
                    message = L"file is not valid UTF-8, UTF-16 LE, or UTF-16 BE text";
                    return false;
                }
            }
            if (std::any_of(output.begin(), output.end(), [](wchar_t character) {
                return character < 32 && character != L'\t' &&
                    character != L'\r' && character != L'\n';
            })) {
                output.clear();
                message = L"file appears to contain binary control data";
                return false;
            }
            return true;
        }

        void WorkerLoop(std::stop_token stop) {
            while (!stop.stop_requested()) {
                bool runTick = false;
                bool resetMonitoring = false;
                std::vector<std::filesystem::path> manual;
                std::filesystem::path root;
                RuntimeAdapters adapters;
                std::unordered_map<std::string, std::vector<std::filesystem::path>> containers;
                {
                    std::unique_lock lock(mutex_);
                    condition_.wait(lock, stop, [this]() {
                        return resetMonitoringPending_ || tickPending_ ||
                            !manualQueue_.empty();
                    });
                    if (stop.stop_requested()) break;
                    resetMonitoring = std::exchange(resetMonitoringPending_, false);
                    runTick = std::exchange(tickPending_, false);
                    manual.swap(manualQueue_);
                    root = root_;
                    adapters = adapters_;
                    containers = latestContainers_;
                    busy_ = true;
                }

                activeAdapters_ = std::move(adapters);
                if (resetMonitoring) {
                    directoryStates_.clear();
                    containerStates_.clear();
                    triggerScheduleStates_.clear();
                    needsContainerSnapshots_.store(false, std::memory_order_relaxed);
                }
                if (runTick && !root.empty()) RunAutomaticTick(root, containers);
                for (const auto& path : manual) RunManual(root, path);
                activeAdapters_ = {};

                {
                    std::lock_guard lock(mutex_);
                    busy_ = false;
                }
            }
        }

        void RunAutomaticTick(const std::filesystem::path& root,
            const std::unordered_map<std::string,
                std::vector<std::filesystem::path>>& containers) {
            const auto catalog = Storage::FlowCatalog(root).Scan();
            if (!catalog) {
                Log(L"ERROR", L"Flow catalog scan failed during monitor tick.");
                return;
            }

            ++automaticTick_;
            const FlowTrustStore trustStore(root);
            std::unordered_set<std::wstring> activeDirectoryKeys;
            std::unordered_set<std::wstring> activeContainerKeys;
			std::unordered_set<std::wstring> activeScheduleKeys;
			bool hasContainerMonitor = false;
            std::unordered_map<std::wstring, std::optional<DirectorySnapshot>> scanCache;
            for (const auto& entry : catalog.entries) {
                if (entry.state != Storage::FlowCatalogEntryState::ready) continue;
                const auto trust = trustStore.Inspect(entry.path, entry.document.id);
                if (!trust.trusted()) {
                    Log(L"WARNING", L"Skipped untrusted Flow during automatic execution: " +
                        entry.path.filename().wstring());
                    continue;
                }
                const auto& document = trust.document;
                if (!document.enabled || !document.trigger ||
                    !document.trigger->enabled ||
                    !IsAutomaticTrigger(document.trigger->type)) continue;
                const auto& trigger = *document.trigger;
                const auto validation = ValidateModule(trigger, "$.trigger");
                if (!validation) {
                    Log(L"WARNING", L"Skipped invalid Flow trigger: " +
                        Utf8ToWide(validation.message));
                    continue;
                }
                const auto key = WatchKey(entry.path, trigger);
                activeScheduleKeys.insert(key);
                auto& scheduleState = triggerScheduleStates_[key];
                if (trigger.type == TriggerDirectory) {
                    activeDirectoryKeys.insert(key);
                    auto& state = directoryStates_[key];
                    if (!TriggerIntervalDue(trigger, scheduleState, automaticTick_))
                        continue;
                    const auto rootPath = PathFromUtf8(StringParameter(trigger, "path"));
                    const bool recursive = BoolParameter(trigger, "recursive", false);
                    const auto cacheKey = rootPath.lexically_normal().wstring() +
                        (recursive ? L"\nR" : L"\nN");
                    auto cached = scanCache.find(cacheKey);
                    if (cached == scanCache.end())
                        cached = scanCache.emplace(cacheKey,
                            ScanDirectory(rootPath, recursive)).first;
                    if (!cached->second) continue;
                    ProcessDirectory(document, trigger, state,
                        *cached->second);
                }
                else if (trigger.type == TriggerContainer) {
					hasContainerMonitor = true;
                    activeContainerKeys.insert(key);
                    auto& state = containerStates_[key];
                    if (!TriggerIntervalDue(trigger, scheduleState, automaticTick_))
                        continue;
                    const auto id = StringParameter(trigger, "containerId");
                    const auto found = containers.find(id);
                    if (found == containers.end()) continue;
                    ProcessContainer(document, trigger, state, found->second);
                }
                else if (trigger.type == TriggerSchedule) {
                    const auto slot = CurrentLocalTimeSlot();
                    if (!slot) continue;
                    const auto hour = static_cast<int>(
                        IntegerParameter(trigger, "hour", -1));
                    const auto minute = static_cast<int>(
                        IntegerParameter(trigger, "minute", -1));
                    if (slot->second.first == hour && slot->second.second == minute &&
                        scheduleState.lastTimeSlot != slot->first) {
                        scheduleState.lastTimeSlot = slot->first;
                        Execute(document, std::nullopt);
                    }
                }
            }
            std::erase_if(directoryStates_, [&activeDirectoryKeys](const auto& item) {
                return !activeDirectoryKeys.contains(item.first);
            });
            std::erase_if(containerStates_, [&activeContainerKeys](const auto& item) {
                return !activeContainerKeys.contains(item.first);
            });
			std::erase_if(triggerScheduleStates_, [&activeScheduleKeys](const auto& item) {
				return !activeScheduleKeys.contains(item.first);
			});
			needsContainerSnapshots_.store(hasContainerMonitor, std::memory_order_relaxed);
        }

        std::optional<DirectorySnapshot> ScanDirectory(
            const std::filesystem::path& root, bool recursive) {
            std::error_code error;
            const auto rootStatus = std::filesystem::symlink_status(root, error);
            if (error || rootStatus.type() != std::filesystem::file_type::directory) {
                Log(L"WARNING", L"Flow monitor directory is unavailable: " + root.wstring());
                return std::nullopt;
            }
            DirectorySnapshot snapshot;
            auto add = [&snapshot](const std::filesystem::directory_entry& entry) {
                std::error_code statusError;
                const auto status = entry.symlink_status(statusError);
                if (statusError || status.type() == std::filesystem::file_type::symlink ||
                    (status.type() != std::filesystem::file_type::regular &&
                        status.type() != std::filesystem::file_type::directory)) return;
                EntryStamp stamp;
                stamp.directory = status.type() == std::filesystem::file_type::directory;
                stamp.writeTime = entry.last_write_time(statusError);
                if (statusError) return;
                if (!stamp.directory) {
                    stamp.size = entry.file_size(statusError);
                    if (statusError) return;
                }
                snapshot.emplace(entry.path().lexically_normal(), stamp);
            };

            bool exceededLimit = false;
            if (recursive) {
                std::filesystem::recursive_directory_iterator iterator(root,
                    std::filesystem::directory_options::skip_permission_denied, error), end;
                while (!error && iterator != end && snapshot.size() < maxDirectoryEntries_) {
                    add(*iterator);
                    iterator.increment(error);
                }
                exceededLimit = !error && iterator != end;
            }
            else {
                std::filesystem::directory_iterator iterator(root,
                    std::filesystem::directory_options::skip_permission_denied, error), end;
                while (!error && iterator != end && snapshot.size() < maxDirectoryEntries_) {
                    add(*iterator);
                    iterator.increment(error);
                }
                exceededLimit = !error && iterator != end;
            }
            if (error || exceededLimit) {
                Log(L"WARNING", L"Flow monitor scan failed or exceeded its entry limit: " +
                    root.wstring());
                return std::nullopt;
            }
            return snapshot;
        }

        void ProcessDirectory(const Storage::FlowDocument& document,
            const Storage::FlowNode& trigger, DirectoryState& state,
            const DirectorySnapshot& current) {
            if (!state.initialized) {
                state.snapshot = current;
                state.initialized = true;
                return;
            }
            std::vector<Event> events;
            events.reserve((std::min)(maxEventsPerFlow_, current.size()));
            const auto configured = StringParameter(trigger, "event", "any");
            const auto pattern = StringParameter(trigger, "nameRegex");
            for (const auto& [path, stamp] : current) {
                const auto previous = state.snapshot.find(path);
                const auto kind = previous == state.snapshot.end() ? EventKind::created :
                    (previous->second == stamp ? std::optional<EventKind>{} : EventKind::modified);
                if (kind && EventAccepted(*kind, configured) && NameMatches(path, pattern))
                    events.push_back({ *kind, path, {} });
                if (events.size() >= maxEventsPerFlow_) break;
            }
            if (events.size() < maxEventsPerFlow_) {
                for (const auto& [path, ignored] : state.snapshot) {
                    (void)ignored;
                    if (!current.contains(path) && EventAccepted(EventKind::removed, configured) &&
                        NameMatches(path, pattern))
                        events.push_back({ EventKind::removed, path, {} });
                    if (events.size() >= maxEventsPerFlow_) break;
                }
            }
            state.snapshot = current;
            for (const auto& event : events) Execute(document, event);
        }

        void ProcessContainer(const Storage::FlowDocument& document,
            const Storage::FlowNode& trigger, ContainerState& state,
            const std::vector<std::filesystem::path>& paths) {
            std::set<std::filesystem::path> current;
            for (const auto& path : paths) {
                if (path.empty()) continue;
                current.insert(path.lexically_normal());
                if (current.size() > maxContainerEntries_) {
                    Log(L"WARNING", L"Flow container snapshot exceeded its entry limit.");
                    return;
                }
            }
            if (!state.initialized) {
                state.paths = std::move(current);
                state.initialized = true;
                return;
            }
            std::vector<Event> events;
            const auto configured = StringParameter(trigger, "event", "any");
            const auto pattern = StringParameter(trigger, "nameRegex");
            const auto containerId = StringParameter(trigger, "containerId");
            for (const auto& path : current) {
                if (!state.paths.contains(path) && EventAccepted(EventKind::added, configured) &&
                    NameMatches(path, pattern))
                    events.push_back({ EventKind::added, path, containerId });
                if (events.size() >= maxEventsPerFlow_) break;
            }
            if (events.size() < maxEventsPerFlow_) {
                for (const auto& path : state.paths) {
                    if (!current.contains(path) && EventAccepted(EventKind::removed, configured) &&
                        NameMatches(path, pattern))
                        events.push_back({ EventKind::removed, path, containerId });
                    if (events.size() >= maxEventsPerFlow_) break;
                }
            }
            state.paths = std::move(current);
            for (const auto& event : events) Execute(document, event);
        }

        void RunManual(const std::filesystem::path& root,
            const std::filesystem::path& path) {
            const auto trust = FlowTrustStore(root).Inspect(path);
            if (!trust.trusted()) {
                Log(L"WARNING", L"Blocked manual execution of an untrusted Flow: " +
                    path.filename().wstring());
                return;
            }
            Execute(trust.document, std::nullopt);
        }

        void Execute(const Storage::FlowDocument& document,
            const std::optional<Event>& event) {
            const auto dataflow = ValidateFlowDataflow(document);
            if (!dataflow) {
                Log(L"ERROR", L"Flow contains an invalid data connection: " +
                    Utf8ToWide(dataflow.message));
                return;
            }
            Context context;
            if (event) {
                context.emplace(L"event.path", event->path);
                context.emplace(L"event.name", event->path.filename().wstring());
                context.emplace(L"event.type", EventName(event->kind));
                context.emplace(L"event.containerId", Utf8ToWide(event->containerId));
                if (document.trigger) {
                    SetNodeOutput(context, *document.trigger, "file", event->path);
                    SetNodeOutput(context, *document.trigger, "path", event->path.wstring());
                    SetNodeOutput(context, *document.trigger, "name",
                        event->path.filename().wstring());
                    SetNodeOutput(context, *document.trigger, "eventType",
                        EventName(event->kind));
                }
            }
            std::size_t executed = 0;
            ExecuteNodes(document.actions, context, 1, executed);
        }

        bool ExecuteNodes(const std::vector<Storage::FlowNode>& nodes,
            Context& context, std::size_t depth, std::size_t& executed) {
            if (depth > maxExecutionDepth_) {
                Log(L"ERROR", L"Flow execution depth limit was reached.");
                return false;
            }
            for (const auto& node : nodes) {
                if (!node.enabled) continue;
                if (++executed > maxExecutedNodes_) {
                    Log(L"ERROR", L"Flow execution node limit was reached.");
                    return false;
                }
                const auto validation = ValidateModule(node, "$.actions");
                if (!validation) {
                    Log(L"ERROR", L"Flow stopped at an invalid or unsupported module: " +
                        Utf8ToWide(validation.message));
                    return false;
                }
                if (node.type == ControlIf) {
                    const auto source = ResolveFileInput(node, "file", "source", context);
                    const bool condition = EvaluateCondition(node, source, context);
                    const std::string branchName = condition ? "then" : "else";
                    if (source) {
                        SetNodeOutput(context, node, "file", *source);
                        SetNodeOutput(context, node,
                            condition ? "then.file" : "else.file", *source);
                    }
                    SetNodeOutput(context, node, "matched", condition);
                    const auto branch = std::find_if(node.branches.begin(), node.branches.end(),
                        [&branchName](const auto& item) { return item.first == branchName; });
                    if (branch != node.branches.end() &&
                        !ExecuteNodes(branch->second, context, depth + 1, executed)) return false;
                    continue;
                }
                if (node.type == ActionMoveFile) {
                    SetNodeOutput(context, node, "success", false);
                    SetNodeOutput(context, node, "error", std::wstring{});
                    const auto failMove = [this, &context, &node](std::wstring message) {
                        if (message.empty()) message = L"move-file action failed";
                        SetNodeOutput(context, node, "success", false);
                        SetNodeOutput(context, node, "error", message);
                        Log(L"ERROR", L"Move-file action failed: " + message);
                    };
                    const auto resolvedSource = ResolveFileInput(
                        node, "file", "source", context);
                    if (!resolvedSource) {
                        failMove(L"input file is unavailable");
                        continue;
                    }
                    const auto& source = *resolvedSource;
                    std::filesystem::path directory;
                    const auto destinationContainer =
                        StringParameter(node, "destinationContainerId");
                    if (!destinationContainer.empty()) {
                        if (!activeAdapters_.resolveMoveTargetFolder) {
                            failMove(L"container resolver is unavailable");
                            continue;
                        }
                        ResolveContainerFolderResult resolved;
                        try {
                            resolved = activeAdapters_.resolveMoveTargetFolder(
                                destinationContainer);
                        }
                        catch (...) {
                            failMove(L"container resolver failed unexpectedly");
                            continue;
                        }
                        if (!resolved.success) {
                            failMove(resolved.error.empty() ?
                                L"move target container is unavailable" : resolved.error);
                            continue;
                        }
                        directory = resolved.folder;
                    }
                    else {
                        directory = std::filesystem::path(Expand(Utf8ToWide(
                            StringParameter(node, "destinationDirectory")), context));
                    }
                    std::error_code error;
                    const auto sourceStatus = std::filesystem::symlink_status(source, error);
                    if (error || sourceStatus.type() != std::filesystem::file_type::regular) {
                        failMove(L"source is not a regular file: " + source.wstring());
                        continue;
                    }
                    const auto directoryStatus = std::filesystem::symlink_status(directory, error);
                    if (error || directoryStatus.type() != std::filesystem::file_type::directory) {
                        failMove(L"destination is not a directory: " + directory.wstring());
                        continue;
                    }
                    const auto destination = directory / source.filename();
                    try {
                        const auto moved = PoggetCore::HistoryFileSystem::MovePath(
                            source, destination, true);
                        if (!moved) {
                            failMove(moved.context.empty() ?
                                L"file move was not completed" : moved.context);
                            continue;
                        }
                        error.clear();
                        const auto destinationStatus =
                            std::filesystem::symlink_status(destination, error);
                        if (error || destinationStatus.type() !=
                            std::filesystem::file_type::regular) {
                            failMove(L"move completed without a verifiable destination");
                            continue;
                        }
                        context[L"movedPath"] = destination;
                        SetNodeOutput(context, node, "file", destination);
                        SetNodeOutput(context, node, "success", true);
                        SetNodeOutput(context, node, "error", std::wstring{});
                        const auto legacyResult = Utf8ToWide(StringParameter(node, "result"));
                        if (!legacyResult.empty() && legacyResult != L"movedPath")
                            context[legacyResult] = destination;
                    }
                    catch (...) {
                        failMove(L"file move failed unexpectedly");
                    }
                    continue;
                }
                if (node.type == ActionMapFile) {
                    SetNodeOutput(context, node, "success", false);
                    SetNodeOutput(context, node, "error", std::wstring{});
                    const auto failMap = [this, &context, &node](std::wstring message) {
                        if (message.empty()) message = L"map-file action failed";
                        SetNodeOutput(context, node, "success", false);
                        SetNodeOutput(context, node, "error", message);
                        Log(L"ERROR", L"Map-file action failed: " + message);
                    };
                    const auto resolvedSource = ResolveFileInput(
                        node, "file", "source", context);
                    if (!resolvedSource) {
                        failMap(L"input file is unavailable");
                        continue;
                    }
                    const auto& source = *resolvedSource;
                    std::error_code error;
                    const auto status = std::filesystem::symlink_status(source, error);
                    if (error || status.type() != std::filesystem::file_type::regular ||
                        !activeAdapters_.mapFile) {
                        failMap(L"source is not a regular file or mapper is unavailable");
                        continue;
                    }
                    MapFileResult mapped;
                    try {
                        mapped = activeAdapters_.mapFile(
                            StringParameter(node, "containerId"), source,
                            StringParameter(node, "insertPosition") == MapInsertBeginning ?
                                MapInsertPosition::beginning : MapInsertPosition::end);
                    }
                    catch (...) {
                        failMap(L"mapper failed unexpectedly");
                        continue;
                    }
                    if (!mapped.success) {
                        failMap(mapped.error.empty() ?
                            L"file was not mapped" : mapped.error);
                        continue;
                    }
                    error.clear();
                    const auto mappedStatus = std::filesystem::symlink_status(
                        mapped.mappedPath, error);
                    if (mapped.mappedPath.empty() || error ||
                        mappedStatus.type() != std::filesystem::file_type::regular) {
                        failMap(L"mapping completed without a verifiable file result");
                        continue;
                    }
                    context[L"mappedPath"] = mapped.mappedPath;
                    SetNodeOutput(context, node, "file", mapped.mappedPath);
                    SetNodeOutput(context, node, "success", true);
                    SetNodeOutput(context, node, "error", std::wstring{});
                    const auto legacyResult = Utf8ToWide(StringParameter(node, "result"));
                    if (!legacyResult.empty() && legacyResult != L"mappedPath")
                        context[legacyResult] = mapped.mappedPath;
                    continue;
                }
                if (node.type == ActionRegexRename) {
                    SetNodeOutput(context, node, "success", false);
                    SetNodeOutput(context, node, "error", std::wstring{});
                    const auto failRename = [this, &context, &node](std::wstring message) {
                        if (message.empty()) message = L"regex rename failed";
                        SetNodeOutput(context, node, "success", false);
                        SetNodeOutput(context, node, "error", message);
                        Log(L"ERROR", L"Regex-rename action failed: " + message);
                    };
                    const auto resolvedSource = ResolveFileInput(
                        node, "file", "source", context);
                    if (!resolvedSource) {
                        failRename(L"input file is unavailable");
                        continue;
                    }
                    const auto source = resolvedSource->lexically_normal();
                    std::error_code error;
                    const auto status = std::filesystem::symlink_status(source, error);
                    if (error || status.type() != std::filesystem::file_type::regular) {
                        failRename(L"source is not a regular file: " + source.wstring());
                        continue;
                    }
                    const auto pattern = Utf8ToWide(StringParameter(node, "pattern"));
                    const auto replacement = Expand(
                        Utf8ToWide(StringParameter(node, "replacement")), context);
                    if (pattern.empty() || !IsSafeRegexPattern(pattern)) {
                        failRename(L"rename pattern is invalid or unsafe");
                        continue;
                    }
                    std::wstring renamedName;
                    try {
                        renamedName = std::regex_replace(source.filename().wstring(),
                            std::wregex(pattern, std::regex_constants::ECMAScript |
                                std::regex_constants::optimize), replacement);
                    }
                    catch (const std::regex_error&) {
                        failRename(L"rename pattern could not be compiled");
                        continue;
                    }
                    if (!IsSafeFileName(renamedName)) {
                        failRename(L"rename result is not a valid file name");
                        continue;
                    }
                    const auto destination =
                        (source.parent_path() / renamedName).lexically_normal();
                    if (destination == source) {
                        SetNodeOutput(context, node, "file", source);
                        SetNodeOutput(context, node, "success", true);
                        SetNodeOutput(context, node, "error", std::wstring{});
                        continue;
                    }
                    error.clear();
                    const auto destinationStatus =
                        std::filesystem::symlink_status(destination, error);
                    if (!error && destinationStatus.type() !=
                        std::filesystem::file_type::not_found) {
                        failRename(L"destination already exists: " + destination.wstring());
                        continue;
                    }
                    if (error && error != std::errc::no_such_file_or_directory) {
                        failRename(L"destination could not be inspected");
                        continue;
                    }
                    try {
                        const auto moved = PoggetCore::HistoryFileSystem::MovePath(
                            source, destination, true);
                        if (!moved) {
                            failRename(moved.context.empty() ?
                                L"file rename was not completed" : moved.context);
                            continue;
                        }
                    }
                    catch (...) {
                        failRename(L"file rename failed unexpectedly");
                        continue;
                    }
                    error.clear();
                    const auto finalStatus =
                        std::filesystem::symlink_status(destination, error);
                    if (error || finalStatus.type() !=
                        std::filesystem::file_type::regular) {
                        failRename(L"rename completed without a verifiable destination");
                        continue;
                    }
                    SetNodeOutput(context, node, "file", destination);
                    SetNodeOutput(context, node, "success", true);
                    SetNodeOutput(context, node, "error", std::wstring{});
                    continue;
                }
                if (node.type == ActionReadText) {
                    SetNodeOutput(context, node, "content", std::wstring{});
                    SetNodeOutput(context, node, "success", false);
                    SetNodeOutput(context, node, "error", std::wstring{});
                    const auto failRead = [this, &context, &node](std::wstring message) {
                        if (message.empty()) message = L"read-text action failed";
                        SetNodeOutput(context, node, "content", std::wstring{});
                        SetNodeOutput(context, node, "success", false);
                        SetNodeOutput(context, node, "error", message);
                        Log(L"ERROR", L"Read-text action failed: " + message);
                    };
                    const auto source = ResolveFileInput(node, "file", "source", context);
                    if (!source) {
                        failRead(L"input file is unavailable");
                        continue;
                    }
                    std::wstring content;
                    std::wstring readError;
                    if (!ReadTextFile(*source, static_cast<std::size_t>(
                        IntegerParameter(node, "maximumBytes", 16 * 1024)),
                        content, readError)) {
                        failRead(std::move(readError));
                        continue;
                    }
                    SetNodeOutput(context, node, "content", std::move(content));
                    SetNodeOutput(context, node, "success", true);
                    SetNodeOutput(context, node, "error", std::wstring{});
                    continue;
                }
                if (node.type == ActionCreateContainer) {
                    SetNodeOutput(context, node, "success", false);
                    SetNodeOutput(context, node, "id", std::wstring{});
                    SetNodeOutput(context, node, "name", std::wstring{});
                    SetNodeOutput(context, node, "error", std::wstring{});
                    if (!activeAdapters_.createContainer) {
                        SetNodeOutput(context, node, "error", L"container creation adapter is unavailable");
                        continue;
                    }
                    const auto name = Expand(Utf8ToWide(StringParameter(node, "name")), context);
                    const auto mode = StringParameter(node, "mode");
                    const auto target = Expand(Utf8ToWide(StringParameter(node, "targetDirectory")), context);
                    CreateContainerResult created;
                    try { created = activeAdapters_.createContainer(name, mode, target); }
                    catch (...) { created.error = L"container creation failed unexpectedly"; }
                    if (!created.success) {
                        SetNodeOutput(context, node, "error", created.error.empty() ?
                            L"container creation failed" : created.error);
                        continue;
                    }
                    SetNodeOutput(context, node, "id", Utf8ToWide(created.id));
                    SetNodeOutput(context, node, "name", created.name.empty() ? name : created.name);
                    SetNodeOutput(context, node, "success", true);
                    continue;
                }
                if (node.type == ActionRenameContainer) {
                    SetNodeOutput(context, node, "success", false);
                    SetNodeOutput(context, node, "error", std::wstring{});
                    if (!activeAdapters_.renameContainer) {
                        SetNodeOutput(context, node, "error", L"container rename adapter is unavailable");
                        continue;
                    }
                    std::string id = StringParameter(node, "containerId");
                    if (const auto* binding = ResolveBoundInput(node, "container", context))
                        id = WideToUtf8(ValueText(*binding));
                    const auto name = Expand(Utf8ToWide(StringParameter(node, "name")), context);
                    RenameContainerResult renamed;
                    try { renamed = activeAdapters_.renameContainer(id, name); }
                    catch (...) { renamed.error = L"container rename failed unexpectedly"; }
                    if (!renamed.success)
                        SetNodeOutput(context, node, "error", renamed.error.empty() ?
                            L"container rename failed" : renamed.error);
                    else SetNodeOutput(context, node, "success", true);
                    continue;
                }
                if (node.type == ActionRunProgram) {
                    SetNodeOutput(context, node, "success", false);
                    SetNodeOutput(context, node, "exitCode", std::wstring{});
                    SetNodeOutput(context, node, "error", std::wstring{});
                    const auto source = ResolveFileInput(node, "file", "source", context);
                    if (source) SetNodeOutput(context, node, "file", *source);
                    const auto executable = Expand(Utf8ToWide(StringParameter(node, "executable")), context);
                    const auto arguments = Expand(Utf8ToWide(StringParameter(node, "arguments")), context);
                    const auto workingDirectory = Expand(Utf8ToWide(
                        StringParameter(node, "workingDirectory")), context);
                    if (executable.empty()) {
                        SetNodeOutput(context, node, "error", L"program path is empty");
                        continue;
                    }
#ifdef _WIN32
                    std::wstring commandLine = L"\"" + executable + L"\"";
                    if (!arguments.empty()) commandLine += L" " + arguments;
                    std::vector<wchar_t> command(commandLine.begin(), commandLine.end());
                    command.push_back(L'\0');
                    STARTUPINFOW startup{};
                    startup.cb = sizeof(startup);
                    PROCESS_INFORMATION process{};
                    const auto cwd = workingDirectory.empty() ? nullptr : workingDirectory.c_str();
                    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
                        CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW, nullptr, cwd,
                        &startup, &process)) {
                        SetNodeOutput(context, node, "error", L"local program could not be started");
                        continue;
                    }
                    const bool wait = BoolParameter(node, "wait", true);
                    DWORD exitCode = STILL_ACTIVE;
                    if (wait) {
                        WaitForSingleObject(process.hProcess, INFINITE);
                        GetExitCodeProcess(process.hProcess, &exitCode);
                    }
                    else exitCode = 0;
                    CloseHandle(process.hThread);
                    CloseHandle(process.hProcess);
                    SetNodeOutput(context, node, "exitCode", std::to_wstring(exitCode));
                    SetNodeOutput(context, node, "success", !wait || exitCode == 0);
#else
                    SetNodeOutput(context, node, "error", L"local program execution is only supported on Windows");
#endif
                    continue;
                }
                if (node.type == InteractionTip) {
                    if (!activeAdapters_.showTip) {
                        Log(L"ERROR", L"Prompt adapter is unavailable.");
                        return false;
                    }
                    const auto result = activeAdapters_.showTip(
                        Expand(Utf8ToWide(StringParameter(node, "title")), context),
                        Expand(Utf8ToWide(StringParameter(node, "message")), context));
                    if (!result.success) {
                        Log(L"ERROR", L"Prompt could not be shown: " + result.error);
                        return false;
                    }
                    SetNodeOutput(context, node, "accepted", result.accepted);
                    continue;
                }
                Log(L"ERROR", L"Flow stopped at an unsupported action module.");
                return false;
            }
            return true;
        }

        static bool EvaluateCondition(const Storage::FlowNode& node,
            const std::optional<std::filesystem::path>& source,
            const Context& context) {
			const auto configuredLeft = StringParameter(node, "left");
			std::wstring left;
			bool leftAvailable = false;
			if (configuredLeft == ConditionFilePath) {
				left = source ? source->wstring() : L"";
				leftAvailable = !left.empty();
			}
			else if (configuredLeft == ConditionFileName) {
				left = source ? source->filename().wstring() : L"";
				leftAvailable = !left.empty();
			}
			else if (configuredLeft == ConditionFileExtension) {
				left = source ? source->extension().wstring() : L"";
				leftAvailable = source.has_value();
			}
			else if (configuredLeft == ConditionFileExists) {
				std::error_code error;
				leftAvailable = source.has_value();
				left = leftAvailable && std::filesystem::exists(*source, error) && !error ?
					L"true" : L"false";
			}
			else if (configuredLeft == ConditionTextInput) {
				if (const auto* value = ResolveBoundInput(node, "text", context)) {
					left = ValueText(*value);
					leftAvailable = true;
				}
			}
			else if (configuredLeft == ConditionBooleanInput) {
				if (const auto* value = ResolveBoundInput(node, "boolean", context)) {
					left = ValueText(*value);
					leftAvailable = true;
				}
			}
			else if (configuredLeft.find("${") != std::string::npos) {
				left = Expand(Utf8ToWide(configuredLeft), context);
				leftAvailable = !left.empty();
			}
			else {
				const auto found = context.find(Utf8ToWide(configuredLeft));
				if (found != context.end()) {
					left = ValueText(found->second);
					leftAvailable = true;
				}
			}
            const auto operation = StringParameter(node, "operation");
            const auto right = Expand(Utf8ToWide(StringParameter(node, "right")), context);
			if (operation == "exists") return leftAvailable && !left.empty();
            if (operation == "equals") return left == right;
            if (operation == "notEquals") return left != right;
            if (operation == "contains") return left.find(right) != std::wstring::npos;
            if (operation == "isTrue")
                return left == L"true" || left == L"1" || left == L"yes";
            if (operation == "matches" && IsSafeRegexPattern(right) &&
                left.size() <= maxRegexInputCharacters_) {
                try {
                    return std::regex_search(left, std::wregex(right,
                        std::regex_constants::ECMAScript | std::regex_constants::optimize));
                }
                catch (const std::regex_error&) { return false; }
            }
            return false;
        }

        void Log(const std::wstring& level, const std::wstring& message) const {
            if (activeAdapters_.log) activeAdapters_.log(level, message);
        }

        mutable std::mutex mutex_;
        std::condition_variable_any condition_;
        std::filesystem::path root_;
        RuntimeAdapters adapters_;
        RuntimeAdapters activeAdapters_;
        std::unordered_map<std::string, std::vector<std::filesystem::path>> latestContainers_;
        std::vector<std::filesystem::path> manualQueue_;
        bool tickPending_ = false;
        bool resetMonitoringPending_ = false;
        bool busy_ = false;
		std::atomic_bool needsContainerSnapshots_{ false };
        std::jthread worker_;
        std::unordered_map<std::wstring, DirectoryState> directoryStates_;
        std::unordered_map<std::wstring, ContainerState> containerStates_;
        std::unordered_map<std::wstring, TriggerScheduleState> triggerScheduleStates_;
        std::uint64_t automaticTick_ = 0;

        static constexpr std::size_t maxDirectoryEntries_ = 20'000;
        static constexpr std::size_t maxContainerEntries_ = 10'000;
        static constexpr std::size_t maxEventsPerFlow_ = 256;
        static constexpr std::size_t maxExecutionDepth_ = 32;
        static constexpr std::size_t maxExecutedNodes_ = 10'000;
        static constexpr std::size_t maxManualQueue_ = 32;
        static constexpr std::size_t maxRegexCharacters_ = 128;
        static constexpr std::size_t maxRegexInputCharacters_ = 512;
        static constexpr std::size_t maxExpandedCharacters_ = 32 * 1024;
    };

} // namespace PoggetCore::Flow
