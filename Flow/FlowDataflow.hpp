#pragma once

#include "FlowModules.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace PoggetCore::Flow {

    inline constexpr std::string_view ConditionFilePath = "file.path";
    inline constexpr std::string_view ConditionFileName = "file.name";
    inline constexpr std::string_view ConditionFileExtension = "file.extension";
    inline constexpr std::string_view ConditionFileExists = "file.exists";
    inline constexpr std::string_view ConditionTextInput = "input.text";
    inline constexpr std::string_view ConditionBooleanInput = "input.boolean";

    enum class FlowValueType {
        file,
        text,
        boolean
    };

    enum class FlowPortScope {
        following,
        thenBranch,
        elseBranch
    };

    enum class FlowPortSemantic {
        triggerFile,
        triggerPath,
        triggerName,
        triggerEventType,
        movedFile,
        moveSuccess,
        moveError,
        mappedFile,
        mapSuccess,
        mapError,
        renamedFile,
        renameSuccess,
        renameError,
        fileContent,
        readSuccess,
        readError,
        evaluatedFile,
        conditionResult,
        matchedFile,
        unmatchedFile,
        tipResult
        ,containerCreatedId
        ,containerCreatedName
        ,containerCreateSuccess
        ,containerCreateError
        ,containerRenameSuccess
        ,containerRenameError
        ,programSuccess
        ,programExitCode
        ,programError
    };

    struct FlowInputDescriptor {
        std::string_view id;
        FlowValueType valueType = FlowValueType::text;
        std::string_view legacyParameter;
    };

    struct FlowPortReference {
        std::string nodeId;
        std::string portId;
        std::string nodeDisplayName;
        std::string portAlias;
        std::string variableAlias;
        FlowValueType valueType = FlowValueType::text;
        FlowPortScope scope = FlowPortScope::following;
        FlowPortSemantic semantic = FlowPortSemantic::triggerPath;

        Storage::FlowInputBinding Binding() const {
            return { nodeId, portId };
        }
    };

    struct FlowDataflowValidationResult {
        bool valid = true;
        std::string message;
        std::string path;

        explicit operator bool() const noexcept { return valid; }
    };

    struct FlowConsumerReference {
        std::string nodeId;
        std::string inputId;
    };

    inline const Storage::FlowInputBinding* FindFlowInput(
        const Storage::FlowNode& node, std::string_view inputId) noexcept {
        const auto found = std::find_if(node.inputs.begin(), node.inputs.end(),
            [inputId](const auto& input) { return input.first == inputId; });
        return found == node.inputs.end() ? nullptr : &found->second;
    }

    inline void SetFlowInput(Storage::FlowNode& node, std::string inputId,
        Storage::FlowInputBinding binding) {
        const auto found = std::find_if(node.inputs.begin(), node.inputs.end(),
            [&inputId](const auto& input) { return input.first == inputId; });
        if (found == node.inputs.end())
            node.inputs.emplace_back(std::move(inputId), std::move(binding));
        else
            found->second = std::move(binding);
    }

    inline void ClearFlowInput(Storage::FlowNode& node,
        std::string_view inputId) {
        std::erase_if(node.inputs,
            [inputId](const auto& input) { return input.first == inputId; });
    }

    inline std::string FlowPortAlias(const Storage::FlowNode& node,
        std::string_view portId) {
        const auto found = std::find_if(node.portAliases.begin(), node.portAliases.end(),
            [portId](const auto& alias) { return alias.first == portId; });
        return found == node.portAliases.end() ? std::string{} : found->second;
    }

    inline std::string FlowVariableAlias(const Storage::FlowNode& node,
        std::string_view portId) {
        const auto found = std::find_if(node.variableAliases.begin(),
            node.variableAliases.end(), [portId](const auto& alias) {
                return alias.first == portId;
            });
        return found == node.variableAliases.end() ? std::string{} : found->second;
    }

    inline std::vector<FlowInputDescriptor> DescribeFlowInputs(
        std::string_view type) {
        if (type == ActionMoveFile || type == ActionMapFile ||
            type == ActionRegexRename || type == ActionReadText ||
            type == ActionRunProgram)
            return { { "file", FlowValueType::file, "source" } };
        if (type == ControlIf) return {
            { "file", FlowValueType::file, "source" },
            { "text", FlowValueType::text, "left" },
            { "boolean", FlowValueType::boolean, "left" }
        };
        if (type == ActionRenameContainer) return {
            { "container", FlowValueType::text, "containerId" }
        };
        return {};
    }

    inline std::string EncodeFlowNodeId(std::string_view id) {
        static constexpr char digits[] = "0123456789abcdef";
        std::string encoded;
        encoded.reserve(id.size() * 2);
        for (const unsigned char value : id) {
            encoded.push_back(digits[value >> 4]);
            encoded.push_back(digits[value & 0x0f]);
        }
        return encoded;
    }

    // Legacy expression support is deliberately isolated here. New documents
    // persist FlowInputBinding instead of this runtime context key.
    inline std::string FlowOutputKey(std::string_view nodeId,
        std::string_view portId) {
        return "node." + EncodeFlowNodeId(nodeId) + "." + std::string(portId);
    }

    inline std::string FlowOutputKey(const Storage::FlowNode& node,
        std::string_view portId) {
        return FlowOutputKey(node.id, portId);
    }

    inline std::string FlowOutputExpression(const Storage::FlowNode& node,
        std::string_view portId) {
        return "${" + FlowOutputKey(node, portId) + "}";
    }

    inline FlowPortReference MakeFlowPort(const Storage::FlowNode& node,
        std::string_view portId, FlowValueType valueType, FlowPortScope scope,
        FlowPortSemantic semantic) {
        return { node.id, std::string(portId), node.displayName,
            FlowPortAlias(node, portId), FlowVariableAlias(node, portId),
            valueType, scope, semantic };
    }

    inline bool IsUserVariableOutput(FlowPortSemantic semantic) noexcept {
        return semantic == FlowPortSemantic::triggerFile ||
            semantic == FlowPortSemantic::movedFile ||
            semantic == FlowPortSemantic::moveSuccess ||
            semantic == FlowPortSemantic::moveError ||
            semantic == FlowPortSemantic::mappedFile ||
            semantic == FlowPortSemantic::mapSuccess ||
            semantic == FlowPortSemantic::mapError ||
            semantic == FlowPortSemantic::renamedFile ||
            semantic == FlowPortSemantic::renameSuccess ||
            semantic == FlowPortSemantic::renameError ||
            semantic == FlowPortSemantic::fileContent ||
            semantic == FlowPortSemantic::readSuccess ||
            semantic == FlowPortSemantic::readError ||
            semantic == FlowPortSemantic::conditionResult ||
            semantic == FlowPortSemantic::tipResult ||
            semantic == FlowPortSemantic::containerCreatedId ||
            semantic == FlowPortSemantic::containerCreatedName ||
            semantic == FlowPortSemantic::containerCreateSuccess ||
            semantic == FlowPortSemantic::containerCreateError ||
            semantic == FlowPortSemantic::containerRenameSuccess ||
            semantic == FlowPortSemantic::containerRenameError ||
            semantic == FlowPortSemantic::programSuccess ||
            semantic == FlowPortSemantic::programExitCode ||
            semantic == FlowPortSemantic::programError;
    }

    inline std::vector<FlowPortReference> DescribeFlowOutputs(
        const Storage::FlowNode& node) {
        if (node.id.empty()) return {};
        if (node.type == TriggerDirectory || node.type == TriggerContainer) {
            return {
                MakeFlowPort(node, "file", FlowValueType::file,
                    FlowPortScope::following, FlowPortSemantic::triggerFile),
                MakeFlowPort(node, "path", FlowValueType::text,
                    FlowPortScope::following, FlowPortSemantic::triggerPath),
                MakeFlowPort(node, "name", FlowValueType::text,
                    FlowPortScope::following, FlowPortSemantic::triggerName),
                MakeFlowPort(node, "eventType", FlowValueType::text,
                    FlowPortScope::following, FlowPortSemantic::triggerEventType)
            };
        }
        if (node.type == ActionMoveFile) {
            return {
                MakeFlowPort(node, "file", FlowValueType::file,
                    FlowPortScope::following, FlowPortSemantic::movedFile),
                MakeFlowPort(node, "success", FlowValueType::boolean,
                    FlowPortScope::following, FlowPortSemantic::moveSuccess),
                MakeFlowPort(node, "error", FlowValueType::text,
                    FlowPortScope::following, FlowPortSemantic::moveError)
            };
        }
        if (node.type == ActionMapFile) {
            return {
                MakeFlowPort(node, "file", FlowValueType::file,
                    FlowPortScope::following, FlowPortSemantic::mappedFile),
                MakeFlowPort(node, "success", FlowValueType::boolean,
                    FlowPortScope::following, FlowPortSemantic::mapSuccess),
                MakeFlowPort(node, "error", FlowValueType::text,
                    FlowPortScope::following, FlowPortSemantic::mapError)
            };
        }
        if (node.type == ActionRegexRename) {
            return {
                MakeFlowPort(node, "file", FlowValueType::file,
                    FlowPortScope::following, FlowPortSemantic::renamedFile),
                MakeFlowPort(node, "success", FlowValueType::boolean,
                    FlowPortScope::following, FlowPortSemantic::renameSuccess),
                MakeFlowPort(node, "error", FlowValueType::text,
                    FlowPortScope::following, FlowPortSemantic::renameError)
            };
        }
        if (node.type == ActionReadText) {
            return {
                MakeFlowPort(node, "content", FlowValueType::text,
                    FlowPortScope::following, FlowPortSemantic::fileContent),
                MakeFlowPort(node, "success", FlowValueType::boolean,
                    FlowPortScope::following, FlowPortSemantic::readSuccess),
                MakeFlowPort(node, "error", FlowValueType::text,
                    FlowPortScope::following, FlowPortSemantic::readError)
            };
        }
        if (node.type == ControlIf) {
            return {
                MakeFlowPort(node, "file", FlowValueType::file,
                    FlowPortScope::following, FlowPortSemantic::evaluatedFile),
                MakeFlowPort(node, "matched", FlowValueType::boolean,
                    FlowPortScope::following, FlowPortSemantic::conditionResult),
                MakeFlowPort(node, "then.file", FlowValueType::file,
                    FlowPortScope::thenBranch, FlowPortSemantic::matchedFile),
                MakeFlowPort(node, "else.file", FlowValueType::file,
                    FlowPortScope::elseBranch, FlowPortSemantic::unmatchedFile)
            };
        }
        if (node.type == InteractionTip) {
            return { MakeFlowPort(node, "accepted", FlowValueType::boolean,
                FlowPortScope::following, FlowPortSemantic::tipResult) };
        }
        if (node.type == ActionCreateContainer) return {
            MakeFlowPort(node, "id", FlowValueType::text, FlowPortScope::following,
                FlowPortSemantic::containerCreatedId),
            MakeFlowPort(node, "name", FlowValueType::text, FlowPortScope::following,
                FlowPortSemantic::containerCreatedName),
            MakeFlowPort(node, "success", FlowValueType::boolean, FlowPortScope::following,
                FlowPortSemantic::containerCreateSuccess),
            MakeFlowPort(node, "error", FlowValueType::text, FlowPortScope::following,
                FlowPortSemantic::containerCreateError)
        };
        if (node.type == ActionRenameContainer) return {
            MakeFlowPort(node, "success", FlowValueType::boolean, FlowPortScope::following,
                FlowPortSemantic::containerRenameSuccess),
            MakeFlowPort(node, "error", FlowValueType::text, FlowPortScope::following,
                FlowPortSemantic::containerRenameError)
        };
        if (node.type == ActionRunProgram) return {
            MakeFlowPort(node, "success", FlowValueType::boolean, FlowPortScope::following,
                FlowPortSemantic::programSuccess),
            MakeFlowPort(node, "exitCode", FlowValueType::text, FlowPortScope::following,
                FlowPortSemantic::programExitCode),
            MakeFlowPort(node, "error", FlowValueType::text, FlowPortScope::following,
                FlowPortSemantic::programError)
        };
        return {};
    }

    inline void AppendFlowOutputs(std::vector<FlowPortReference>& output,
        const Storage::FlowNode& node, FlowPortScope scope) {
        if (!node.enabled) return;
        for (auto& reference : DescribeFlowOutputs(node)) {
            const bool conditionResultInBranch = node.type == ControlIf &&
                reference.semantic == FlowPortSemantic::conditionResult &&
                (scope == FlowPortScope::thenBranch ||
                    scope == FlowPortScope::elseBranch);
            if (reference.scope != scope && !conditionResultInBranch) continue;
            if (std::none_of(output.begin(), output.end(),
                [&reference](const auto& existing) {
                    return existing.nodeId == reference.nodeId &&
                        existing.portId == reference.portId;
                })) output.push_back(std::move(reference));
        }
    }

    inline std::vector<FlowPortReference> FlowValuesBefore(
        const Storage::FlowDocument& document, std::size_t actionLimit) {
        std::vector<FlowPortReference> values;
        if (document.trigger && IsAutomaticTrigger(document.trigger->type))
            AppendFlowOutputs(values, *document.trigger, FlowPortScope::following);
        const auto count = (std::min)(actionLimit, document.actions.size());
        for (std::size_t index = 0; index < count; ++index)
            AppendFlowOutputs(values, document.actions[index], FlowPortScope::following);
        return values;
    }

    inline std::vector<FlowPortReference> FlowBranchValuesBefore(
        const Storage::FlowDocument& document, std::size_t parentIndex,
        std::string_view branchName, std::size_t childLimit) {
        auto values = FlowValuesBefore(document, parentIndex);
        if (parentIndex >= document.actions.size()) return values;
        const auto& parent = document.actions[parentIndex];
        const auto branchScope = branchName == "then" ? FlowPortScope::thenBranch :
            (branchName == "else" ? FlowPortScope::elseBranch :
                FlowPortScope::following);
        if (branchScope != FlowPortScope::following)
            AppendFlowOutputs(values, parent, branchScope);
        for (const auto& branch : parent.branches) {
            if (branch.first != branchName) continue;
            const auto count = (std::min)(childLimit, branch.second.size());
            for (std::size_t index = 0; index < count; ++index)
                AppendFlowOutputs(values, branch.second[index],
                    FlowPortScope::following);
            break;
        }
        return values;
    }

    inline bool FindFlowValuesBeforeNodeInList(
        const std::vector<Storage::FlowNode>& nodes, std::string_view nodeId,
        std::vector<FlowPortReference>& available,
        std::vector<FlowPortReference>& result) {
        for (const auto& node : nodes) {
            if (node.id == nodeId) {
                result = available;
                return true;
            }
            for (const auto& branch : node.branches) {
                auto branchAvailable = available;
                AppendFlowOutputs(branchAvailable, node,
                    branch.first == "then" ? FlowPortScope::thenBranch :
                    FlowPortScope::elseBranch);
                if (FindFlowValuesBeforeNodeInList(branch.second, nodeId,
                    branchAvailable, result)) return true;
            }
            AppendFlowOutputs(available, node, FlowPortScope::following);
        }
        return false;
    }

    inline std::vector<FlowPortReference> FlowValuesBeforeNode(
        const Storage::FlowDocument& document, std::string_view nodeId) {
        std::vector<FlowPortReference> available;
        if (document.trigger && IsAutomaticTrigger(document.trigger->type))
            AppendFlowOutputs(available, *document.trigger, FlowPortScope::following);
        std::vector<FlowPortReference> result;
        FindFlowValuesBeforeNodeInList(document.actions, nodeId, available, result);
        return result;
    }

    inline const Storage::FlowNode* FindFlowNodeById(
        const std::vector<Storage::FlowNode>& nodes,
        std::string_view nodeId) noexcept {
        for (const auto& node : nodes) {
            if (node.id == nodeId) return &node;
            for (const auto& branch : node.branches)
                if (const auto* found = FindFlowNodeById(branch.second, nodeId))
                    return found;
        }
        return nullptr;
    }

    inline std::vector<FlowPortReference> FlowBranchValuesAtEnd(
        const Storage::FlowDocument& document, std::string_view parentNodeId,
        std::string_view branchName) {
        auto available = FlowValuesBeforeNode(document, parentNodeId);
        const auto* parent = FindFlowNodeById(document.actions, parentNodeId);
        if (!parent) return available;
        AppendFlowOutputs(available, *parent,
            branchName == "then" ? FlowPortScope::thenBranch :
            FlowPortScope::elseBranch);
        for (const auto& branch : parent->branches) {
            if (branch.first != branchName) continue;
            for (const auto& child : branch.second)
                AppendFlowOutputs(available, child, FlowPortScope::following);
            break;
        }
        return available;
    }

    inline std::optional<FlowPortReference> FindFlowOutput(
        const Storage::FlowDocument& document, std::string_view nodeId,
        std::string_view portId) {
        auto inspect = [nodeId, portId](const Storage::FlowNode& node)
            -> std::optional<FlowPortReference> {
            if (node.id != nodeId) return std::nullopt;
            for (auto& reference : DescribeFlowOutputs(node))
                if (reference.portId == portId) return reference;
            return std::nullopt;
        };
        if (document.trigger) {
            if (auto found = inspect(*document.trigger)) return found;
        }
        std::vector<const Storage::FlowNode*> pending;
        for (const auto& node : document.actions) pending.push_back(&node);
        while (!pending.empty()) {
            const auto* node = pending.back();
            pending.pop_back();
            if (auto found = inspect(*node)) return found;
            for (const auto& branch : node->branches)
                for (const auto& child : branch.second) pending.push_back(&child);
        }
        return std::nullopt;
    }

    inline void AppendFlowConsumers(
        const std::vector<Storage::FlowNode>& nodes,
        std::string_view producerId, std::string_view portId,
        std::vector<FlowConsumerReference>& consumers) {
        for (const auto& candidate : nodes) {
            for (const auto& input : candidate.inputs) {
                if (input.second.nodeId == producerId &&
                    input.second.portId == portId)
                    consumers.push_back({ candidate.id, input.first });
            }
            for (const auto& branch : candidate.branches)
                AppendFlowConsumers(branch.second, producerId, portId, consumers);
        }
    }

    inline std::vector<FlowConsumerReference> FlowConsumersOf(
        const Storage::FlowDocument& document, std::string_view producerId,
        std::string_view portId) {
        std::vector<FlowConsumerReference> consumers;
        AppendFlowConsumers(document.actions, producerId, portId, consumers);
        return consumers;
    }

    inline const FlowPortReference* FindVisibleFlowOutput(
        const std::vector<FlowPortReference>& available,
        const Storage::FlowInputBinding& binding) noexcept {
        const auto found = std::find_if(available.begin(), available.end(),
            [&binding](const auto& output) {
                return output.nodeId == binding.nodeId &&
                    output.portId == binding.portId;
            });
        return found == available.end() ? nullptr : &*found;
    }

    inline FlowDataflowValidationResult ValidateNodeInputs(
        const Storage::FlowNode& node,
        const std::vector<FlowPortReference>& available,
        const std::string& path,
        std::unordered_set<std::string>& variableAliases) {
        const auto descriptors = DescribeFlowInputs(node.type);
        for (std::size_t index = 0; index < node.inputs.size(); ++index) {
            const auto& input = node.inputs[index];
            if (std::any_of(node.inputs.begin(), node.inputs.begin() + index,
                [&input](const auto& existing) {
                    return existing.first == input.first;
                })) return { false, "input port cannot be connected more than once",
                    path + ".inputs." + input.first };
            const auto descriptor = std::find_if(descriptors.begin(), descriptors.end(),
                [&input](const auto& candidate) { return candidate.id == input.first; });
            if (descriptor == descriptors.end())
                return { false, "module does not declare this input port",
                    path + ".inputs." + input.first };
            const auto* output = FindVisibleFlowOutput(available, input.second);
            if (!output)
                return { false, "input source is not visible before this module",
                    path + ".inputs." + input.first };
            if (output->valueType != descriptor->valueType)
                return { false, "input and output port types do not match",
                    path + ".inputs." + input.first };
        }
        for (std::size_t index = 0; index < node.portAliases.size(); ++index) {
            const auto& alias = node.portAliases[index];
            if (std::any_of(node.portAliases.begin(), node.portAliases.begin() + index,
                [&alias](const auto& existing) {
                    return existing.first == alias.first;
                })) return { false, "output port cannot have more than one alias",
                    path + ".portAliases." + alias.first };
            const auto outputs = DescribeFlowOutputs(node);
            if (std::none_of(outputs.begin(), outputs.end(), [&alias](const auto& output) {
                return output.portId == alias.first;
            })) return { false, "module does not declare this output port",
                path + ".portAliases." + alias.first };
        }
        for (std::size_t index = 0; index < node.variableAliases.size(); ++index) {
            const auto& alias = node.variableAliases[index];
            if (std::any_of(node.variableAliases.begin(),
                node.variableAliases.begin() + index,
                [&alias](const auto& existing) {
                    return existing.first == alias.first;
                })) return { false, "output port cannot have more than one variable alias",
                    path + ".variableAliases." + alias.first };
            if (alias.second.empty() ||
                alias.second.find_first_of("{}\r\n") != std::string::npos)
                return { false, "variable alias contains unsupported characters",
                    path + ".variableAliases." + alias.first };
            const auto outputs = DescribeFlowOutputs(node);
            const auto output = std::find_if(outputs.begin(), outputs.end(),
                [&alias](const auto& candidate) {
                    return candidate.portId == alias.first;
                });
            if (output == outputs.end() || !IsUserVariableOutput(output->semantic))
                return { false, "module does not expose this output as a variable",
                    path + ".variableAliases." + alias.first };
            if (!variableAliases.emplace(alias.second).second)
                return { false, "variable alias must be unique within the Flow",
                    path + ".variableAliases." + alias.first };
        }
        return {};
    }

    inline FlowDataflowValidationResult ValidateFlowNodeList(
        const std::vector<Storage::FlowNode>& nodes,
        std::vector<FlowPortReference>& available, const std::string& listPath,
        std::unordered_set<std::string>& variableAliases) {
        for (std::size_t index = 0; index < nodes.size(); ++index) {
            const auto& node = nodes[index];
            const auto path = listPath + "[" + std::to_string(index) + "]";
            auto result = ValidateNodeInputs(node, available, path, variableAliases);
            if (!result) return result;
            for (const auto& branch : node.branches) {
                auto branchAvailable = available;
                AppendFlowOutputs(branchAvailable, node,
                    branch.first == "then" ? FlowPortScope::thenBranch :
                    FlowPortScope::elseBranch);
                result = ValidateFlowNodeList(branch.second, branchAvailable,
                    path + ".branches." + branch.first, variableAliases);
                if (!result) return result;
            }
            AppendFlowOutputs(available, node, FlowPortScope::following);
        }
        return {};
    }

    inline FlowDataflowValidationResult ValidateFlowDataflow(
        const Storage::FlowDocument& document) {
        std::unordered_set<std::string> variableAliases;
        if (document.trigger) {
            const auto triggerResult = ValidateNodeInputs(
                *document.trigger, {}, "$.trigger", variableAliases);
            if (!triggerResult) return triggerResult;
        }
        auto available = FlowValuesBefore(document, 0);
        return ValidateFlowNodeList(document.actions, available, "$.actions",
            variableAliases);
    }

} // namespace PoggetCore::Flow
