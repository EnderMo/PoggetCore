#pragma once

#include "../Storage/FlowStorage.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>

namespace PoggetCore::Flow {

    inline constexpr std::string_view TriggerManual = "pogget.trigger.manual";
    inline constexpr std::string_view TriggerDirectory = "pogget.trigger.directory";
    inline constexpr std::string_view TriggerContainer = "pogget.trigger.container";
    inline constexpr std::string_view TriggerSchedule = "pogget.trigger.schedule";
    inline constexpr std::string_view ActionMoveFile = "pogget.action.file.move";
    inline constexpr std::string_view ActionMapFile = "pogget.action.file.map";
    inline constexpr std::string_view ActionRegexRename = "pogget.action.file.regexRename";
    inline constexpr std::string_view ActionReadText = "pogget.action.file.readText";
    inline constexpr std::string_view ActionCreateContainer = "pogget.action.container.create";
    inline constexpr std::string_view ActionRenameContainer = "pogget.action.container.rename";
    inline constexpr std::string_view ActionRunProgram = "pogget.action.program.run";
    inline constexpr std::string_view ControlIf = "pogget.control.if";
    inline constexpr std::string_view InteractionTip = "pogget.interaction.tip";

    enum class MapInsertPosition {
        end,
        beginning
    };

    inline constexpr std::string_view MapInsertEnd = "end";
    inline constexpr std::string_view MapInsertBeginning = "beginning";
    inline constexpr std::string_view ContainerModeOrdinary = "ordinary";
    inline constexpr std::string_view ContainerModeMapped = "mapped";

    enum class ModuleKind {
        manualTrigger,
        automaticTrigger,
        action,
        control,
        interaction
    };

    struct ModuleDescriptor {
        std::string_view type;
        ModuleKind kind;
        std::string_view displayName;
        bool producesValue = false;
    };

    enum class ModuleFieldKind {
        text,
        boolean,
        choice,
        file,
        folder,
        container
    };

    struct ModuleFieldDescriptor {
        std::string_view id;
        ModuleFieldKind kind = ModuleFieldKind::text;
        bool required = false;
        std::size_t maximumBytes = 0;
        bool deprecated = false;
    };

    inline constexpr std::array<ModuleFieldDescriptor, 5> DirectoryFields{ {
        { "path", ModuleFieldKind::folder, true, 32 * 1024 },
        { "event", ModuleFieldKind::choice, true, 32 },
        { "nameRegex", ModuleFieldKind::text, false, 128 },
        { "recursive", ModuleFieldKind::boolean, false, 0 },
        { "intervalMs", ModuleFieldKind::text, false, 0 }
    } };
    inline constexpr std::array<ModuleFieldDescriptor, 4> ContainerFields{ {
        { "containerId", ModuleFieldKind::container, true, 256 },
        { "event", ModuleFieldKind::choice, true, 32 },
        { "nameRegex", ModuleFieldKind::text, false, 128 },
        { "intervalMs", ModuleFieldKind::text, false, 0 }
    } };
    inline constexpr std::array<ModuleFieldDescriptor, 2> ScheduleFields{ {
        { "hour", ModuleFieldKind::choice, true, 0 },
        { "minute", ModuleFieldKind::choice, true, 0 }
    } };
    inline constexpr std::array<ModuleFieldDescriptor, 4> MoveFields{ {
        { "source", ModuleFieldKind::file, false, 32 * 1024 },
        { "destinationContainerId", ModuleFieldKind::container, false, 256 },
        { "destinationDirectory", ModuleFieldKind::folder, false, 32 * 1024 },
        { "result", ModuleFieldKind::text, false, 128, true }
    } };
    inline constexpr std::array<ModuleFieldDescriptor, 4> MapFields{ {
        { "source", ModuleFieldKind::file, false, 32 * 1024 },
        { "containerId", ModuleFieldKind::container, true, 256 },
        { "insertPosition", ModuleFieldKind::choice, false, 16 },
        { "result", ModuleFieldKind::text, false, 128, true }
    } };
    inline constexpr std::array<ModuleFieldDescriptor, 4> RegexRenameFields{ {
        { "source", ModuleFieldKind::file, false, 32 * 1024 },
        { "pattern", ModuleFieldKind::text, true, 128 },
        { "replacement", ModuleFieldKind::text, false, 512 },
        { "result", ModuleFieldKind::text, false, 128, true }
    } };
    inline constexpr std::array<ModuleFieldDescriptor, 3> ReadTextFields{ {
        { "source", ModuleFieldKind::file, false, 32 * 1024 },
        { "maximumBytes", ModuleFieldKind::choice, false, 0 },
        { "result", ModuleFieldKind::text, false, 128, true }
    } };
    inline constexpr std::array<ModuleFieldDescriptor, 4> IfFields{ {
        { "source", ModuleFieldKind::file, false, 32 * 1024 },
        { "left", ModuleFieldKind::choice, true, 128 },
        { "operation", ModuleFieldKind::choice, true, 32 },
        { "right", ModuleFieldKind::text, false, 4096 }
    } };
    inline constexpr std::array<ModuleFieldDescriptor, 2> TipFields{ {
        { "title", ModuleFieldKind::text, false, 512 },
        { "message", ModuleFieldKind::text, true, 16 * 1024 }
    } };
    inline constexpr std::array<ModuleFieldDescriptor, 3> CreateContainerFields{ {
        { "name", ModuleFieldKind::text, true, 512 },
        { "mode", ModuleFieldKind::choice, true, 16 },
        { "targetDirectory", ModuleFieldKind::folder, false, 32 * 1024 }
    } };
    inline constexpr std::array<ModuleFieldDescriptor, 2> RenameContainerFields{ {
        { "containerId", ModuleFieldKind::container, false, 256 },
        { "name", ModuleFieldKind::text, true, 512 }
    } };
    inline constexpr std::array<ModuleFieldDescriptor, 4> RunProgramFields{ {
        { "executable", ModuleFieldKind::text, true, 32 * 1024 },
        { "arguments", ModuleFieldKind::text, false, 32 * 1024 },
        { "workingDirectory", ModuleFieldKind::folder, false, 32 * 1024 },
        { "wait", ModuleFieldKind::boolean, false, 0 }
    } };

    inline std::span<const ModuleFieldDescriptor> DescribeModuleFields(
        std::string_view type) noexcept {
        if (type == TriggerDirectory) return DirectoryFields;
        if (type == TriggerContainer) return ContainerFields;
        if (type == TriggerSchedule) return ScheduleFields;
        if (type == ActionMoveFile) return MoveFields;
        if (type == ActionMapFile) return MapFields;
        if (type == ActionRegexRename) return RegexRenameFields;
        if (type == ActionReadText) return ReadTextFields;
        if (type == ControlIf) return IfFields;
        if (type == InteractionTip) return TipFields;
        if (type == ActionCreateContainer) return CreateContainerFields;
        if (type == ActionRenameContainer) return RenameContainerFields;
        if (type == ActionRunProgram) return RunProgramFields;
        return {};
    }

    inline constexpr std::array<ModuleDescriptor, 13> Modules{ {
        { TriggerManual, ModuleKind::manualTrigger, "Manual trigger", false },
        { TriggerDirectory, ModuleKind::automaticTrigger, "Directory monitor", false },
        { TriggerContainer, ModuleKind::automaticTrigger, "Container monitor", false },
        { TriggerSchedule, ModuleKind::automaticTrigger, "Scheduled time", false },
        { ActionMoveFile, ModuleKind::action, "Move file", true },
        { ActionMapFile, ModuleKind::action, "Map file", true },
        { ActionRegexRename, ModuleKind::action, "Regex rename", true },
        { ActionReadText, ModuleKind::action, "Read text file", true },
        { ControlIf, ModuleKind::control, "If", false },
        { InteractionTip, ModuleKind::interaction, "Show prompt", false },
        { ActionCreateContainer, ModuleKind::action, "Create container", true },
        { ActionRenameContainer, ModuleKind::action, "Rename container", true },
        { ActionRunProgram, ModuleKind::action, "Run local program", true }
    } };

    inline const ModuleDescriptor* FindModule(std::string_view type) noexcept {
        for (const auto& module : Modules)
            if (module.type == type) return &module;
        return nullptr;
    }

    inline bool IsManualTrigger(std::string_view type) noexcept {
        const auto* module = FindModule(type);
        return module && module->kind == ModuleKind::manualTrigger;
    }

    inline bool IsAutomaticTrigger(std::string_view type) noexcept {
        const auto* module = FindModule(type);
        return module && module->kind == ModuleKind::automaticTrigger;
    }

    inline bool IsAutomaticFlow(const Storage::FlowDocument& document) noexcept {
        return document.trigger && IsAutomaticTrigger(document.trigger->type);
    }

    struct ModuleValidationResult {
        bool valid = true;
        std::string message;
        std::string path;

        explicit operator bool() const noexcept { return valid; }
    };

    namespace Detail {
        inline ModuleValidationResult Error(std::string path, std::string message) {
            return { false, std::move(message), std::move(path) };
        }

        inline const Storage::FlowJson::Value* Parameter(
            const Storage::FlowNode& node, std::string_view name) noexcept {
            return Storage::FlowJson::Find(node.parameters, name);
        }

        inline ModuleValidationResult RequireString(const Storage::FlowNode& node,
            std::string_view name, const std::string& path, std::size_t maximum,
            bool allowEmpty = false) {
            const auto* value = Parameter(node, name);
            const auto* text = value ? value->get_if<std::string>() : nullptr;
            if (!text) return Error(path + ".parameters." + std::string(name),
                "parameter must be a string");
            if (!allowEmpty && text->empty())
                return Error(path + ".parameters." + std::string(name),
                    "parameter cannot be empty");
            if (maximum != 0 && text->size() > maximum)
                return Error(path + ".parameters." + std::string(name),
                    "parameter exceeds its size limit");
            return {};
        }

        inline ModuleValidationResult OptionalString(const Storage::FlowNode& node,
            std::string_view name, const std::string& path, std::size_t maximum) {
            const auto* value = Parameter(node, name);
            if (!value) return {};
            const auto* text = value->get_if<std::string>();
            if (!text) return Error(path + ".parameters." + std::string(name),
                "parameter must be a string");
            if (maximum != 0 && text->size() > maximum)
                return Error(path + ".parameters." + std::string(name),
                    "parameter exceeds its size limit");
            return {};
        }

        inline ModuleValidationResult OptionalBool(const Storage::FlowNode& node,
            std::string_view name, const std::string& path) {
            const auto* value = Parameter(node, name);
            if (value && !value->get_if<bool>())
                return Error(path + ".parameters." + std::string(name),
                    "parameter must be a boolean");
            return {};
        }

        inline ModuleValidationResult OptionalIntegerRange(
            const Storage::FlowNode& node, std::string_view name,
            const std::string& path, std::int64_t minimum,
            std::int64_t maximum, std::int64_t multiple = 1) {
            const auto* value = Parameter(node, name);
            if (!value) return {};
            const auto* integer = value->get_if<std::int64_t>();
            if (!integer) return Error(path + ".parameters." + std::string(name),
                "parameter must be an integer");
            if (*integer < minimum || *integer > maximum ||
                (multiple > 1 && *integer % multiple != 0))
                return Error(path + ".parameters." + std::string(name),
                    "integer parameter is outside its allowed range");
            return {};
        }

        inline ModuleValidationResult RequireIntegerRange(
            const Storage::FlowNode& node, std::string_view name,
            const std::string& path, std::int64_t minimum,
            std::int64_t maximum) {
            if (!Parameter(node, name)) return Error(
                path + ".parameters." + std::string(name),
                "parameter must be an integer");
            return OptionalIntegerRange(node, name, path, minimum, maximum);
        }

        inline bool StringIsOneOf(const Storage::FlowNode& node,
            std::string_view name, std::initializer_list<std::string_view> values) {
            const auto* value = Parameter(node, name);
            const auto* text = value ? value->get_if<std::string>() : nullptr;
            if (!text) return false;
            for (const auto candidate : values)
                if (*text == candidate) return true;
            return false;
        }

        inline bool HasInput(const Storage::FlowNode& node,
            std::string_view name) noexcept {
            return std::any_of(node.inputs.begin(), node.inputs.end(),
                [name](const auto& input) { return input.first == name; });
        }

        inline ModuleValidationResult FileSource(const Storage::FlowNode& node,
            const std::string& path) {
            if (HasInput(node, "file"))
                return OptionalString(node, "source", path, 32 * 1024);
            return RequireString(node, "source", path, 32 * 1024);
        }

        inline bool IsSafeRegexPattern(std::string_view pattern) noexcept {
            if (pattern.size() > 128) return false;
            bool escaped = false;
            bool inClass = false;
            for (const unsigned char character : pattern) {
                if (escaped) {
                    if (character >= '0' && character <= '9') return false;
                    escaped = false;
                    continue;
                }
                if (character == '\\') {
                    escaped = true;
                    continue;
                }
                if (character == '[') inClass = true;
                else if (character == ']') inClass = false;
                else if (!inClass && (character == '(' || character == ')' ||
                    character == '|' || character == '{' || character == '}')) return false;
            }
            return !escaped && !inClass;
        }

        inline ModuleValidationResult OptionalSafeRegex(const Storage::FlowNode& node,
            std::string_view name, const std::string& path) {
            auto result = OptionalString(node, name, path, 128);
            if (!result) return result;
            const auto* value = Parameter(node, name);
            const auto* text = value ? value->get_if<std::string>() : nullptr;
            if (text && !text->empty() && !IsSafeRegexPattern(*text))
                return Error(path + ".parameters." + std::string(name),
                    "regular expression uses an unsupported or unsafe construct");
            return {};
        }
    }

    inline ModuleValidationResult ValidateModule(const Storage::FlowNode& node,
        const std::string& path = "$.node") {
        const auto* descriptor = FindModule(node.type);
        if (!descriptor)
            return Detail::Error(path + ".type", "module type is not supported by this runtime");

        if (node.type != ControlIf && !node.branches.empty())
            return Detail::Error(path + ".branches",
                "this module type does not accept branches");

        const auto fields = DescribeModuleFields(node.type);
        for (std::size_t index = 0; index < node.parameters.size(); ++index) {
            const auto& parameter = node.parameters[index];
            if (std::none_of(fields.begin(), fields.end(), [&parameter](const auto& field) {
                return field.id == parameter.first;
            })) return Detail::Error(path + ".parameters." + parameter.first,
                "parameter is not declared by the module schema");
            if (std::any_of(node.parameters.begin(), node.parameters.begin() + index,
                [&parameter](const auto& existing) {
                    return existing.first == parameter.first;
                })) return Detail::Error(path + ".parameters." + parameter.first,
                    "module parameter cannot be declared more than once");
        }

        ModuleValidationResult result;
        if (descriptor->kind == ModuleKind::manualTrigger) {
            if (!node.parameters.empty())
                return Detail::Error(path + ".parameters",
                    "manual trigger does not accept parameters");
            return {};
        }
        if (node.type == TriggerDirectory) {
            if (!(result = Detail::RequireString(node, "path", path, 32 * 1024))) return result;
            if (!(result = Detail::RequireString(node, "event", path, 32))) return result;
            if (!Detail::StringIsOneOf(node, "event",
                { "any", "created", "removed", "modified" }))
                return Detail::Error(path + ".parameters.event", "unsupported directory event");
            if (!(result = Detail::OptionalSafeRegex(node, "nameRegex", path))) return result;
            if (!(result = Detail::OptionalBool(node, "recursive", path))) return result;
            return Detail::OptionalIntegerRange(node, "intervalMs", path,
                2'600, 1'560'000, 2'600);
        }
        if (node.type == TriggerContainer) {
            if (!(result = Detail::RequireString(node, "containerId", path, 256))) return result;
            if (!(result = Detail::RequireString(node, "event", path, 32))) return result;
            if (!Detail::StringIsOneOf(node, "event", { "any", "added", "removed" }))
                return Detail::Error(path + ".parameters.event", "unsupported container event");
            if (!(result = Detail::OptionalSafeRegex(node, "nameRegex", path))) return result;
            return Detail::OptionalIntegerRange(node, "intervalMs", path,
                2'600, 1'560'000, 2'600);
        }
        if (node.type == TriggerSchedule) {
            if (!(result = Detail::RequireIntegerRange(node, "hour", path, 0, 23)))
                return result;
            return Detail::RequireIntegerRange(node, "minute", path, 0, 59);
        }
        if (node.type == ActionMoveFile) {
            if (!(result = Detail::FileSource(node, path))) return result;
            if (!(result = Detail::OptionalString(node, "destinationContainerId", path,
                256))) return result;
            if (!(result = Detail::OptionalString(node, "destinationDirectory", path,
                32 * 1024))) return result;
            const auto container = Detail::Parameter(node, "destinationContainerId");
            const auto directory = Detail::Parameter(node, "destinationDirectory");
            const auto* containerText = container ? container->get_if<std::string>() : nullptr;
            const auto* directoryText = directory ? directory->get_if<std::string>() : nullptr;
            const bool hasContainer = containerText && !containerText->empty();
            const bool hasDirectory = directoryText && !directoryText->empty();
            if (hasContainer == hasDirectory)
                return Detail::Error(path + ".parameters",
                    "move file requires exactly one destination container or directory");
            return Detail::OptionalString(node, "result", path, 128);
        }
        if (node.type == ActionMapFile) {
            if (!(result = Detail::FileSource(node, path))) return result;
            if (!(result = Detail::RequireString(node, "containerId", path, 256))) return result;
            if (!(result = Detail::OptionalString(node, "insertPosition", path, 16)))
                return result;
            if (Detail::Parameter(node, "insertPosition") &&
                !Detail::StringIsOneOf(node, "insertPosition",
                    { MapInsertEnd, MapInsertBeginning }))
                return Detail::Error(path + ".parameters.insertPosition",
                    "unsupported map insertion position");
            return Detail::OptionalString(node, "result", path, 128);
        }
        if (node.type == ActionRegexRename) {
            if (!(result = Detail::FileSource(node, path))) return result;
            if (!(result = Detail::RequireString(node, "pattern", path, 128))) return result;
            if (!(result = Detail::OptionalString(node, "replacement", path, 512)))
                return result;
            const auto* patternValue = Detail::Parameter(node, "pattern");
            const auto* pattern = patternValue ? patternValue->get_if<std::string>() : nullptr;
            if (!pattern || !Detail::IsSafeRegexPattern(*pattern))
                return Detail::Error(path + ".parameters.pattern",
                    "regular expression uses an unsupported or unsafe construct");
            return Detail::OptionalString(node, "result", path, 128);
        }
        if (node.type == ActionReadText) {
            if (!(result = Detail::FileSource(node, path))) return result;
            if (!(result = Detail::OptionalIntegerRange(node, "maximumBytes", path,
                1, 32 * 1024))) return result;
            return Detail::OptionalString(node, "result", path, 128);
        }
        if (node.type == InteractionTip) {
            if (!(result = Detail::RequireString(node, "message", path, 16 * 1024))) return result;
            return Detail::OptionalString(node, "title", path, 512);
        }
        if (node.type == ActionCreateContainer) {
            if (!(result = Detail::RequireString(node, "name", path, 512))) return result;
            if (!(result = Detail::RequireString(node, "mode", path, 16))) return result;
            if (!Detail::StringIsOneOf(node, "mode",
                { ContainerModeOrdinary, ContainerModeMapped }))
                return Detail::Error(path + ".parameters.mode", "unsupported container mode");
            if (!(result = Detail::OptionalString(node, "targetDirectory", path,
                32 * 1024))) return result;
            const auto* target = Detail::Parameter(node, "targetDirectory");
            const auto* targetText = target ? target->get_if<std::string>() : nullptr;
            const bool hasTarget = targetText && !targetText->empty();
            const auto* mode = Detail::Parameter(node, "mode");
            const auto* modeText = mode ? mode->get_if<std::string>() : nullptr;
            if (modeText && (std::string_view(*modeText) == ContainerModeMapped) != hasTarget)
                return Detail::Error(path + ".parameters", "mapped containers require a target directory and ordinary containers do not");
            return {};
        }
        if (node.type == ActionRenameContainer) {
            const bool hasBinding = Detail::HasInput(node, "container");
            if (!hasBinding && !(result = Detail::RequireString(node, "containerId", path, 256))) return result;
            if (hasBinding && !(result = Detail::OptionalString(node, "containerId", path, 256))) return result;
            return Detail::RequireString(node, "name", path, 512);
        }
        if (node.type == ActionRunProgram) {
            if (!(result = Detail::RequireString(node, "executable", path, 32 * 1024))) return result;
            if (!(result = Detail::OptionalString(node, "arguments", path, 32 * 1024))) return result;
            if (!(result = Detail::OptionalString(node, "workingDirectory", path, 32 * 1024))) return result;
            if (!(result = Detail::OptionalBool(node, "wait", path))) return result;
            return {};
        }
        if (node.type == ControlIf) {
            if (!(result = Detail::RequireString(node, "left", path, 128))) return result;
            if (Detail::StringIsOneOf(node, "left",
                { "file.path", "file.name", "file.extension", "file.exists" })) {
                if (!(result = Detail::FileSource(node, path))) return result;
            }
            else if (!(result = Detail::OptionalString(node, "source", path,
                32 * 1024))) return result;
            if (!(result = Detail::RequireString(node, "operation", path, 32))) return result;
            if (!Detail::StringIsOneOf(node, "operation",
                { "exists", "equals", "notEquals", "contains", "matches", "isTrue" }))
                return Detail::Error(path + ".parameters.operation", "unsupported condition operation");
            if (!(result = Detail::OptionalString(node, "right", path, 4096))) return result;
            if (Detail::StringIsOneOf(node, "operation", { "matches" })) {
                const auto* right = Detail::Parameter(node, "right");
                const auto* pattern = right ? right->get_if<std::string>() : nullptr;
                if (pattern && !Detail::IsSafeRegexPattern(*pattern))
                    return Detail::Error(path + ".parameters.right",
                        "regular expression uses an unsupported or unsafe construct");
            }
            bool hasThen = false;
            bool hasElse = false;
            for (const auto& branch : node.branches) {
                if (branch.first == "then") {
                    if (hasThen) return Detail::Error(path + ".branches.then",
                        "if module cannot contain duplicate then branches");
                    hasThen = true;
                }
                else if (branch.first == "else") {
                    if (hasElse) return Detail::Error(path + ".branches.else",
                        "if module cannot contain duplicate else branches");
                    hasElse = true;
                }
                else return Detail::Error(path + ".branches." + branch.first,
                    "if module only accepts then and else branches");
            }
            if (!hasThen || !hasElse)
                return Detail::Error(path + ".branches",
                    "if module requires then and else branches");
            return {};
        }
        return {};
    }

} // namespace PoggetCore::Flow
