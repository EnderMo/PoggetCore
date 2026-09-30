#pragma once

#include "../Storage/FlowStorage.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#include <bcrypt.h>
#include <wincrypt.h>
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "crypt32.lib")
#endif

namespace PoggetCore::Flow {

    enum class FlowTrustState {
        trusted,
        untrusted,
        invalidFlow,
        storeError,
        ioError
    };

    struct FlowTrustResult {
        FlowTrustState state = FlowTrustState::untrusted;
        std::string fingerprint;
        std::string message;
        Storage::FlowDocument document;

        bool trusted() const noexcept { return state == FlowTrustState::trusted; }
    };

    namespace FlowTrustDetail {

        inline constexpr std::string_view StoreFormat = "pogget.flow.trust";
        inline constexpr std::int64_t StoreSchemaVersion = 1;
        inline constexpr std::string_view WindowsHeader = "POGGET-FLOW-TRUST-DPAPI-V1\n";
        inline constexpr std::string_view PortableHeader = "POGGET-FLOW-TRUST-PORTABLE-V1\n";
        inline constexpr std::uintmax_t MaximumFlowBytes = 8 * 1024 * 1024;
        inline constexpr std::uintmax_t MaximumStoreBytes = 4 * 1024 * 1024;

        struct TrustEntry {
            std::string id;
            std::string fingerprint;
        };

        inline std::uint32_t RotateRight(std::uint32_t value,
            std::uint32_t bits) noexcept {
            return (value >> bits) | (value << (32U - bits));
        }

        class PortableSha256 final {
        public:
            PortableSha256() = default;

            void Update(const unsigned char* bytes, std::size_t size) noexcept {
                if (!bytes || size == 0) return;
                for (std::size_t index = 0; index < size; ++index) {
                    block_[blockSize_++] = bytes[index];
                    if (blockSize_ == block_.size()) {
                        Transform();
                        bitLength_ += 512;
                        blockSize_ = 0;
                    }
                }
            }

            std::array<unsigned char, 32> Finish() noexcept {
                std::size_t index = blockSize_;
                block_[index++] = 0x80;
                if (index > 56) {
                    while (index < 64) block_[index++] = 0;
                    Transform();
                    index = 0;
                }
                while (index < 56) block_[index++] = 0;
                bitLength_ += static_cast<std::uint64_t>(blockSize_) * 8;
                for (int shift = 56; shift >= 0; shift -= 8)
                    block_[index++] = static_cast<unsigned char>(bitLength_ >> shift);
                Transform();

                std::array<unsigned char, 32> digest{};
                for (std::size_t word = 0; word < state_.size(); ++word) {
                    digest[word * 4] = static_cast<unsigned char>(state_[word] >> 24);
                    digest[word * 4 + 1] = static_cast<unsigned char>(state_[word] >> 16);
                    digest[word * 4 + 2] = static_cast<unsigned char>(state_[word] >> 8);
                    digest[word * 4 + 3] = static_cast<unsigned char>(state_[word]);
                }
                return digest;
            }

        private:
            void Transform() noexcept {
                static constexpr std::array<std::uint32_t, 64> constants{
                    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
                    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
                    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
                    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
                    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
                    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
                    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
                    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
                    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
                    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
                    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
                    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
                    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
                    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
                    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
                    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
                };
                std::array<std::uint32_t, 64> words{};
                for (std::size_t i = 0; i < 16; ++i) {
                    words[i] = static_cast<std::uint32_t>(block_[i * 4]) << 24 |
                        static_cast<std::uint32_t>(block_[i * 4 + 1]) << 16 |
                        static_cast<std::uint32_t>(block_[i * 4 + 2]) << 8 |
                        static_cast<std::uint32_t>(block_[i * 4 + 3]);
                }
                for (std::size_t i = 16; i < words.size(); ++i) {
                    const auto s0 = RotateRight(words[i - 15], 7) ^
                        RotateRight(words[i - 15], 18) ^ (words[i - 15] >> 3);
                    const auto s1 = RotateRight(words[i - 2], 17) ^
                        RotateRight(words[i - 2], 19) ^ (words[i - 2] >> 10);
                    words[i] = words[i - 16] + s0 + words[i - 7] + s1;
                }

                auto a = state_[0];
                auto b = state_[1];
                auto c = state_[2];
                auto d = state_[3];
                auto e = state_[4];
                auto f = state_[5];
                auto g = state_[6];
                auto h = state_[7];
                for (std::size_t i = 0; i < words.size(); ++i) {
                    const auto s1 = RotateRight(e, 6) ^ RotateRight(e, 11) ^
                        RotateRight(e, 25);
                    const auto choice = (e & f) ^ (~e & g);
                    const auto temporary1 = h + s1 + choice + constants[i] + words[i];
                    const auto s0 = RotateRight(a, 2) ^ RotateRight(a, 13) ^
                        RotateRight(a, 22);
                    const auto majority = (a & b) ^ (a & c) ^ (b & c);
                    const auto temporary2 = s0 + majority;
                    h = g;
                    g = f;
                    f = e;
                    e = d + temporary1;
                    d = c;
                    c = b;
                    b = a;
                    a = temporary1 + temporary2;
                }
                state_[0] += a;
                state_[1] += b;
                state_[2] += c;
                state_[3] += d;
                state_[4] += e;
                state_[5] += f;
                state_[6] += g;
                state_[7] += h;
            }

            std::array<unsigned char, 64> block_{};
            std::size_t blockSize_ = 0;
            std::uint64_t bitLength_ = 0;
            std::array<std::uint32_t, 8> state_{
                0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U
            };
        };

        inline bool Sha256(std::string_view bytes,
            std::array<unsigned char, 32>& digest) {
#ifdef _WIN32
            BCRYPT_ALG_HANDLE algorithm = nullptr;
            BCRYPT_HASH_HANDLE hash = nullptr;
            DWORD objectLength = 0;
            DWORD returned = 0;
            bool success = BCryptOpenAlgorithmProvider(&algorithm,
                BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0 &&
                BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                    reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength),
                    &returned, 0) >= 0 && objectLength != 0;
            std::vector<unsigned char> object(success ? objectLength : 0);
            if (success) success = BCryptCreateHash(algorithm, &hash,
                object.data(), objectLength, nullptr, 0, 0) >= 0;
            std::size_t offset = 0;
            while (success && offset < bytes.size()) {
                const auto remaining = bytes.size() - offset;
                const auto chunk = static_cast<ULONG>((std::min)(remaining,
                    static_cast<std::size_t>((std::numeric_limits<ULONG>::max)())));
                success = BCryptHashData(hash,
                    reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data() + offset)),
                    chunk, 0) >= 0;
                offset += chunk;
            }
            if (success) success = BCryptFinishHash(hash, digest.data(),
                static_cast<ULONG>(digest.size()), 0) >= 0;
            if (hash) BCryptDestroyHash(hash);
            if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
            if (success) return true;
#endif
            PortableSha256 portable;
            portable.Update(reinterpret_cast<const unsigned char*>(bytes.data()),
                bytes.size());
            digest = portable.Finish();
            return true;
        }

        inline std::string Hex(const std::array<unsigned char, 32>& digest) {
            std::ostringstream output;
            output << std::hex << std::setfill('0');
            for (const auto byte : digest)
                output << std::setw(2) << static_cast<unsigned int>(byte);
            return output.str();
        }

        inline bool ConstantTimeEqual(std::string_view left,
            std::string_view right) noexcept {
            if (left.size() != right.size()) return false;
            unsigned char difference = 0;
            for (std::size_t i = 0; i < left.size(); ++i)
                difference |= static_cast<unsigned char>(left[i] ^ right[i]);
            return difference == 0;
        }

        inline bool ReadBounded(const std::filesystem::path& path,
            std::uintmax_t maximum, std::string& bytes, std::string& message) {
            bytes.clear();
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input) {
                message = "could not open file";
                return false;
            }
            const auto size = input.tellg();
            if (size < 0 || static_cast<std::uintmax_t>(size) > maximum) {
                message = "file exceeds its byte limit";
                return false;
            }
            bytes.resize(static_cast<std::size_t>(size));
            input.seekg(0);
            if (!bytes.empty() && !input.read(bytes.data(),
                static_cast<std::streamsize>(bytes.size()))) {
                bytes.clear();
                message = "could not read complete file";
                return false;
            }
            return true;
        }

#ifdef _WIN32
        inline constexpr BYTE TrustEntropy[] = {
            0x50, 0x6f, 0x67, 0x67, 0x65, 0x74, 0x2e, 0x46,
            0x6c, 0x6f, 0x77, 0x2e, 0x54, 0x72, 0x75, 0x73,
            0x74, 0x2e, 0x56, 0x31
        };

        inline bool ProtectOnce(const std::vector<unsigned char>& input,
            DWORD flags, const wchar_t* description,
            std::vector<unsigned char>& output) {
            output.clear();
            if (input.empty() || input.size() > (std::numeric_limits<DWORD>::max)())
                return false;
            DATA_BLOB inputBlob{ static_cast<DWORD>(input.size()),
                const_cast<BYTE*>(input.data()) };
            DATA_BLOB entropyBlob{ static_cast<DWORD>(sizeof(TrustEntropy)),
                const_cast<BYTE*>(TrustEntropy) };
            DATA_BLOB protectedBlob{};
            const BOOL ok = CryptProtectData(&inputBlob, description, &entropyBlob,
                nullptr, nullptr, flags | CRYPTPROTECT_UI_FORBIDDEN, &protectedBlob);
            if (!ok || !protectedBlob.pbData || protectedBlob.cbData == 0) {
                if (protectedBlob.pbData) LocalFree(protectedBlob.pbData);
                return false;
            }
            output.assign(protectedBlob.pbData,
                protectedBlob.pbData + protectedBlob.cbData);
            SecureZeroMemory(protectedBlob.pbData, protectedBlob.cbData);
            LocalFree(protectedBlob.pbData);
            return true;
        }

        inline bool UnprotectOnce(const std::vector<unsigned char>& input,
            std::vector<unsigned char>& output) {
            output.clear();
            if (input.empty() || input.size() > (std::numeric_limits<DWORD>::max)())
                return false;
            DATA_BLOB inputBlob{ static_cast<DWORD>(input.size()),
                const_cast<BYTE*>(input.data()) };
            DATA_BLOB entropyBlob{ static_cast<DWORD>(sizeof(TrustEntropy)),
                const_cast<BYTE*>(TrustEntropy) };
            DATA_BLOB clearBlob{};
            LPWSTR description = nullptr;
            const BOOL ok = CryptUnprotectData(&inputBlob, &description, &entropyBlob,
                nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &clearBlob);
            if (description) LocalFree(description);
            if (!ok || !clearBlob.pbData) {
                if (clearBlob.pbData) LocalFree(clearBlob.pbData);
                return false;
            }
            output.assign(clearBlob.pbData, clearBlob.pbData + clearBlob.cbData);
            SecureZeroMemory(clearBlob.pbData, clearBlob.cbData);
            LocalFree(clearBlob.pbData);
            return true;
        }

        inline bool EncodeBase64(const std::vector<unsigned char>& input,
            std::string& output) {
            output.clear();
            if (input.empty() || input.size() > (std::numeric_limits<DWORD>::max)())
                return false;
            DWORD characters = 0;
            if (!CryptBinaryToStringA(input.data(), static_cast<DWORD>(input.size()),
                CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &characters) ||
                characters == 0) return false;
            output.resize(characters);
            if (!CryptBinaryToStringA(input.data(), static_cast<DWORD>(input.size()),
                CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                output.data(), &characters)) return false;
            output.resize(characters);
            if (!output.empty() && output.back() == '\0') output.pop_back();
            return !output.empty();
        }

        inline bool DecodeBase64(std::string_view input,
            std::vector<unsigned char>& output) {
            output.clear();
            if (input.empty() || input.size() > MaximumStoreBytes ||
                input.size() > (std::numeric_limits<DWORD>::max)()) return false;
            DWORD size = 0;
            if (!CryptStringToBinaryA(input.data(), static_cast<DWORD>(input.size()),
                CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT, nullptr, &size,
                nullptr, nullptr) || size == 0) return false;
            output.resize(size);
            if (!CryptStringToBinaryA(input.data(), static_cast<DWORD>(input.size()),
                CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT, output.data(), &size,
                nullptr, nullptr)) {
                output.clear();
                return false;
            }
            output.resize(size);
            return true;
        }

        inline bool ProtectPayload(std::string_view payload, std::string& encoded) {
            std::vector<unsigned char> clear(payload.begin(), payload.end());
            std::vector<unsigned char> userProtected;
            std::vector<unsigned char> machineProtected;
            const bool ok = ProtectOnce(clear, 0, L"Pogget Flow trust user layer",
                userProtected) && ProtectOnce(userProtected, CRYPTPROTECT_LOCAL_MACHINE,
                    L"Pogget Flow trust machine layer", machineProtected) &&
                EncodeBase64(machineProtected, encoded);
            if (!clear.empty()) SecureZeroMemory(clear.data(), clear.size());
            if (!userProtected.empty())
                SecureZeroMemory(userProtected.data(), userProtected.size());
            if (!machineProtected.empty())
                SecureZeroMemory(machineProtected.data(), machineProtected.size());
            return ok;
        }

        inline bool UnprotectPayload(std::string_view encoded, std::string& payload) {
            payload.clear();
            std::vector<unsigned char> machineProtected;
            std::vector<unsigned char> userProtected;
            std::vector<unsigned char> clear;
            const bool ok = DecodeBase64(encoded, machineProtected) &&
                UnprotectOnce(machineProtected, userProtected) &&
                UnprotectOnce(userProtected, clear);
            if (ok) payload.assign(clear.begin(), clear.end());
            if (!machineProtected.empty())
                SecureZeroMemory(machineProtected.data(), machineProtected.size());
            if (!userProtected.empty())
                SecureZeroMemory(userProtected.data(), userProtected.size());
            if (!clear.empty()) SecureZeroMemory(clear.data(), clear.size());
            return ok;
        }
#else
        inline bool ProtectPayload(std::string_view payload, std::string& encoded) {
            encoded.assign(payload);
            return true;
        }

        inline bool UnprotectPayload(std::string_view encoded, std::string& payload) {
            payload.assign(encoded);
            return true;
        }
#endif

        inline bool ParseEntries(std::string_view payload,
            std::vector<TrustEntry>& entries, std::string& message) {
            entries.clear();
            Storage::FlowJson::Value root;
            Storage::FlowJson::Error error;
            Storage::FlowJson::Limits limits;
            limits.maxInputBytes = MaximumStoreBytes;
            if (!Storage::FlowJson::Parse(payload, root, error, limits)) {
                message = "Flow trust store JSON is invalid";
                return false;
            }
            const auto* object = root.get_if<Storage::FlowJson::Value::Object>();
            if (!object) {
                message = "Flow trust store root is invalid";
                return false;
            }
            const auto* formatValue = Storage::FlowJson::Find(*object, "format");
            const auto* format = formatValue ? formatValue->get_if<std::string>() : nullptr;
            const auto* versionValue = Storage::FlowJson::Find(*object, "schemaVersion");
            const auto* version = versionValue ? versionValue->get_if<std::int64_t>() : nullptr;
            const auto* entriesValue = Storage::FlowJson::Find(*object, "entries");
            const auto* array = entriesValue ?
                entriesValue->get_if<Storage::FlowJson::Value::Array>() : nullptr;
            if (!format || *format != StoreFormat || !version ||
                *version != StoreSchemaVersion || !array || array->size() > 4096) {
                message = "Flow trust store schema is unsupported";
                return false;
            }
            entries.reserve(array->size());
            for (const auto& value : *array) {
                const auto* entryObject = value.get_if<Storage::FlowJson::Value::Object>();
                if (!entryObject) {
                    message = "Flow trust store entry is invalid";
                    return false;
                }
                const auto* idValue = Storage::FlowJson::Find(*entryObject, "id");
                const auto* fingerprintValue = Storage::FlowJson::Find(
                    *entryObject, "sha256");
                const auto* id = idValue ? idValue->get_if<std::string>() : nullptr;
                const auto* fingerprint = fingerprintValue ?
                    fingerprintValue->get_if<std::string>() : nullptr;
                if (!id || id->empty() || id->size() > 256 || !fingerprint ||
                    fingerprint->size() != 64 ||
                    !std::all_of(fingerprint->begin(), fingerprint->end(), [](char value) {
                        return (value >= '0' && value <= '9') ||
                            (value >= 'a' && value <= 'f');
                    })) {
                    message = "Flow trust store entry fields are invalid";
                    return false;
                }
                if (std::any_of(entries.begin(), entries.end(), [&id](const auto& existing) {
                    return existing.id == *id;
                })) {
                    message = "Flow trust store contains duplicate Flow ids";
                    return false;
                }
                entries.push_back({ *id, *fingerprint });
            }
            return true;
        }

        inline bool SerializeEntries(const std::vector<TrustEntry>& entries,
            std::string& payload, std::string& message) {
            Storage::FlowJson::Value::Array array;
            array.reserve(entries.size());
            for (const auto& entry : entries) {
                array.emplace_back(Storage::FlowJson::Value::Object{
                    { "id", entry.id }, { "sha256", entry.fingerprint }
                });
            }
            Storage::FlowJson::Value root(Storage::FlowJson::Value::Object{
                { "format", std::string(StoreFormat) },
                { "schemaVersion", StoreSchemaVersion },
                { "entries", std::move(array) }
            });
            Storage::FlowJson::Error error;
            Storage::FlowJson::Limits limits;
            if (!Storage::FlowJson::Stringify(root, payload, error, limits)) {
                message = "Flow trust store could not be serialized";
                return false;
            }
            if (payload.size() > MaximumStoreBytes) {
                payload.clear();
                message = "Flow trust store exceeds its byte limit";
                return false;
            }
            return true;
        }

        inline bool WriteAtomic(const std::filesystem::path& path,
            std::string_view bytes, std::string& message) {
            std::error_code error;
            std::filesystem::create_directories(path.parent_path(), error);
            if (error) {
                message = "could not create Flow trust directory";
                return false;
            }
            static std::atomic<std::uint64_t> sequence{ 0 };
#ifdef _WIN32
            const auto temporary = std::filesystem::path(path.wstring() + L".tmp." +
                std::to_wstring(GetCurrentProcessId()) + L"." +
                std::to_wstring(++sequence));
#else
            const auto temporary = std::filesystem::path(path.string() + ".tmp." +
                std::to_string(++sequence));
#endif
            {
                std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
                if (!output) {
                    message = "could not create temporary Flow trust file";
                    return false;
                }
                output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
                output.flush();
                if (!output.good()) {
                    output.close();
                    std::filesystem::remove(temporary, error);
                    message = "could not write complete Flow trust file";
                    return false;
                }
            }
#ifdef _WIN32
            HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE,
                FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_WRITE_THROUGH, nullptr);
            if (file == INVALID_HANDLE_VALUE) {
                std::filesystem::remove(temporary, error);
                message = "could not flush Flow trust file";
                return false;
            }
            const bool flushed = FlushFileBuffers(file) != FALSE;
            CloseHandle(file);
            if (!flushed || !MoveFileExW(temporary.c_str(), path.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                std::filesystem::remove(temporary, error);
                message = "could not publish Flow trust file";
                return false;
            }
#else
            std::filesystem::rename(temporary, path, error);
            if (error) {
                std::filesystem::remove(temporary, error);
                message = "could not publish Flow trust file";
                return false;
            }
            std::filesystem::permissions(path,
                std::filesystem::perms::owner_read |
                std::filesystem::perms::owner_write,
                std::filesystem::perm_options::replace, error);
#endif
            return true;
        }
    }

    class FlowTrustStore final {
    public:
        explicit FlowTrustStore(std::filesystem::path flowRoot = {})
            : flowRoot_(std::move(flowRoot)), storePath_(flowRoot_ / L"Flow.vina") {}

        const std::filesystem::path& Path() const noexcept { return storePath_; }

        FlowTrustResult Inspect(const std::filesystem::path& flowPath,
            std::string_view expectedId = {}) const {
            FlowTrustResult result;
            std::string bytes;
            if (!FlowTrustDetail::ReadBounded(flowPath,
                FlowTrustDetail::MaximumFlowBytes, bytes, result.message)) {
                result.state = FlowTrustState::ioError;
                return result;
            }
            std::array<unsigned char, 32> digest{};
            if (!FlowTrustDetail::Sha256(bytes, digest)) {
                result.state = FlowTrustState::ioError;
                result.message = "could not calculate Flow fingerprint";
                return result;
            }
            result.fingerprint = FlowTrustDetail::Hex(digest);
            std::string_view json = bytes;
            if (json.size() >= 3 && static_cast<unsigned char>(json[0]) == 0xef &&
                static_cast<unsigned char>(json[1]) == 0xbb &&
                static_cast<unsigned char>(json[2]) == 0xbf) json.remove_prefix(3);
            const auto parsed = Storage::ParseFlowJson(json, result.document);
            if (!parsed) {
                result.state = FlowTrustState::invalidFlow;
                result.message = parsed.message;
                return result;
            }
            if (!expectedId.empty() && result.document.id != expectedId) {
                result.state = FlowTrustState::invalidFlow;
                result.message = "Flow id changed while trust was being checked";
                return result;
            }

            std::vector<FlowTrustDetail::TrustEntry> entries;
            bool missing = false;
            if (!LoadStore(entries, missing, result.message)) {
                result.state = FlowTrustState::storeError;
                return result;
            }
            const auto found = std::find_if(entries.begin(), entries.end(),
                [&result](const auto& entry) {
                    return entry.id == result.document.id;
                });
            result.state = found != entries.end() &&
                FlowTrustDetail::ConstantTimeEqual(found->fingerprint,
                    result.fingerprint) ? FlowTrustState::trusted :
                FlowTrustState::untrusted;
            if (missing) result.message.clear();
            return result;
        }

        FlowTrustResult Trust(const std::filesystem::path& flowPath,
            std::string_view expectedId = {},
            std::string_view expectedFingerprint = {}) const {
            auto result = Inspect(flowPath, expectedId);
            if (result.state == FlowTrustState::invalidFlow ||
                result.state == FlowTrustState::ioError ||
                result.state == FlowTrustState::storeError) return result;
            if (!expectedFingerprint.empty() &&
                !FlowTrustDetail::ConstantTimeEqual(
                    result.fingerprint, expectedFingerprint)) {
                result.state = FlowTrustState::invalidFlow;
                result.message = "Flow changed after its fingerprint was inspected";
                return result;
            }

            std::lock_guard lock(ProcessMutex());
#ifdef _WIN32
            Storage::FlowDetail::ProcessLock processLock(storePath_);
            if (!processLock.locked()) {
                result.state = FlowTrustState::storeError;
                result.message = "Flow trust store is busy in another process";
                return result;
            }
#endif
            auto current = Inspect(flowPath, result.document.id);
            if (current.fingerprint != result.fingerprint ||
                current.document.id != result.document.id ||
                (!expectedFingerprint.empty() &&
                    !FlowTrustDetail::ConstantTimeEqual(
                        current.fingerprint, expectedFingerprint)) ||
                current.state == FlowTrustState::invalidFlow ||
                current.state == FlowTrustState::ioError ||
                current.state == FlowTrustState::storeError) {
                current.state = current.state == FlowTrustState::untrusted ?
                    FlowTrustState::invalidFlow : current.state;
                if (current.message.empty())
                    current.message = "Flow changed while trust was being granted";
                return current;
            }

            std::vector<FlowTrustDetail::TrustEntry> entries;
            bool missing = false;
            if (!LoadStore(entries, missing, result.message)) {
                result.state = FlowTrustState::storeError;
                return result;
            }
            auto found = std::find_if(entries.begin(), entries.end(),
                [&result](const auto& entry) { return entry.id == result.document.id; });
            if (found == entries.end())
                entries.push_back({ result.document.id, result.fingerprint });
            else
                found->fingerprint = result.fingerprint;
            if (!SaveStore(entries, result.message)) {
                result.state = FlowTrustState::storeError;
                return result;
            }
            result.state = FlowTrustState::trusted;
            result.message.clear();
            return result;
        }

        bool Forget(std::string_view flowId, std::string& message) const {
            if (flowId.empty()) return true;
            std::lock_guard lock(ProcessMutex());
#ifdef _WIN32
            Storage::FlowDetail::ProcessLock processLock(storePath_);
            if (!processLock.locked()) {
                message = "Flow trust store is busy in another process";
                return false;
            }
#endif
            std::vector<FlowTrustDetail::TrustEntry> entries;
            bool missing = false;
            if (!LoadStore(entries, missing, message)) return false;
            if (missing) return true;
            const auto previous = entries.size();
            std::erase_if(entries, [flowId](const auto& entry) {
                return entry.id == flowId;
            });
            return entries.size() == previous || SaveStore(entries, message);
        }

    private:
        static std::mutex& ProcessMutex() {
            static std::mutex mutex;
            return mutex;
        }

        bool LoadStore(std::vector<FlowTrustDetail::TrustEntry>& entries,
            bool& missing, std::string& message) const {
            entries.clear();
            missing = false;
            std::error_code error;
            if (!std::filesystem::exists(storePath_, error)) {
                if (error) {
                    message = "could not inspect Flow trust store";
                    return false;
                }
                missing = true;
                return true;
            }
            std::string bytes;
            if (!FlowTrustDetail::ReadBounded(storePath_,
                FlowTrustDetail::MaximumStoreBytes, bytes, message)) return false;
#ifdef _WIN32
            const auto header = FlowTrustDetail::WindowsHeader;
#else
            const auto header = FlowTrustDetail::PortableHeader;
#endif
            if (bytes.size() <= header.size() ||
                std::string_view(bytes).substr(0, header.size()) != header) {
                message = "Flow trust store header is invalid";
                return false;
            }
            std::string payload;
            if (!FlowTrustDetail::UnprotectPayload(
                std::string_view(bytes).substr(header.size()), payload)) {
                message = "Flow trust store is not valid for this user and device";
                return false;
            }
            const bool parsed = FlowTrustDetail::ParseEntries(payload, entries, message);
#ifdef _WIN32
            if (!payload.empty()) SecureZeroMemory(payload.data(), payload.size());
#else
            std::fill(payload.begin(), payload.end(), '\0');
#endif
            return parsed;
        }

        bool SaveStore(const std::vector<FlowTrustDetail::TrustEntry>& entries,
            std::string& message) const {
            std::string payload;
            if (!FlowTrustDetail::SerializeEntries(entries, payload, message)) return false;
            std::string encoded;
            const bool protectedOk = FlowTrustDetail::ProtectPayload(payload, encoded);
#ifdef _WIN32
            if (!payload.empty()) SecureZeroMemory(payload.data(), payload.size());
#else
            std::fill(payload.begin(), payload.end(), '\0');
#endif
            if (!protectedOk) {
                message = "Flow trust store could not be protected";
                return false;
            }
#ifdef _WIN32
            std::string bytes(FlowTrustDetail::WindowsHeader);
#else
            std::string bytes(FlowTrustDetail::PortableHeader);
#endif
            bytes += encoded;
            const bool written = FlowTrustDetail::WriteAtomic(storePath_, bytes, message);
            std::fill(encoded.begin(), encoded.end(), '\0');
            return written;
        }

        std::filesystem::path flowRoot_;
        std::filesystem::path storePath_;
    };

} // namespace PoggetCore::Flow
