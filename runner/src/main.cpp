#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <intrin.h>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>
#include <winsock2.h>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <iphlpapi.h>
#include <tlhelp32.h>
#include <winioctl.h>
#include <winnetwk.h>
#include <winternl.h>
#include <wbemidl.h>
#include "embedded_profile.generated.h"

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "mpr.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "wbemuuid.lib")

#ifndef KEY_WOW64_32KEY
#define KEY_WOW64_32KEY 0x0200
#endif

#ifndef KEY_WOW64_64KEY
#define KEY_WOW64_64KEY 0x0100
#endif

#ifndef WNNC_NET_RDR2SAMPLE
#define WNNC_NET_RDR2SAMPLE 0x00250000
#endif

#ifndef _WDMDDK_
typedef struct _RTL_BITMAP
{
    ULONG SizeOfBitMap;
    PULONG Buffer;
} RTL_BITMAP, *PRTL_BITMAP;
#endif

enum class ResultStatus
{
    detected,
    not_detected,
    error,
    unsupported
};

struct CheckResult
{
    std::string check_id;
    ResultStatus status;
    std::string evidence;
    std::string started_at;
    std::string finished_at;
};

struct Profile
{
    std::string profile_id;
    std::string name;
    int version = 0;
    std::vector<std::string> checks;
    std::string output_directory;
    bool console_logging_enabled = false;
    bool json_logging_enabled = false;
};

struct PafishPeb
{
    BOOLEAN InheritedAddressSpace;
    BOOLEAN ReadImageFileExecOptions;
    BOOLEAN BeingDebugged;
    BOOLEAN SpareBool;
    HANDLE Mutant;
    HMODULE ImageBaseAddress;
    PPEB_LDR_DATA LdrData;
    RTL_USER_PROCESS_PARAMETERS* ProcessParameters;
    PVOID SubSystemData;
    HANDLE ProcessHeap;
    PRTL_CRITICAL_SECTION FastPebLock;
    PVOID FastPebLockRoutine;
    PVOID FastPebUnlockRoutine;
    ULONG EnvironmentUpdateCount;
    PVOID KernelCallbackTable;
    ULONG Reserved[2];
    PVOID FreeList;
    ULONG TlsExpansionCounter;
    PRTL_BITMAP TlsBitmap;
    ULONG TlsBitmapBits[2];
    PVOID ReadOnlySharedMemoryBase;
    PVOID ReadOnlySharedMemoryHeap;
    PVOID* ReadOnlyStaticServerData;
    PVOID AnsiCodePageData;
    PVOID OemCodePageData;
    PVOID UnicodeCaseTableData;
    ULONG NumberOfProcessors;
    ULONG NtGlobalFlag;
};

struct CpuidRegisters
{
    int eax = 0;
    int ebx = 0;
    int ecx = 0;
    int edx = 0;
};

using CheckHandler = std::function<CheckResult()>;

namespace
{
std::string Trim(const std::string& value)
{
    const std::string whitespace = " \t\r\n";
    const std::size_t start = value.find_first_not_of(whitespace);
    if (start == std::string::npos)
    {
        return "";
    }

    const std::size_t end = value.find_last_not_of(whitespace);
    return value.substr(start, end - start + 1);
}

std::string ToUpperAscii(std::string value)
{
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char ch)
        {
            return static_cast<char>(std::toupper(ch));
        });
    return value;
}

bool ContainsInsensitive(const std::string& haystack, const std::string& needle)
{
    return ToUpperAscii(haystack).find(ToUpperAscii(needle)) != std::string::npos;
}

std::string JoinStrings(const std::vector<std::string>& values, const std::string& separator)
{
    std::ostringstream stream;
    for (std::size_t index = 0; index < values.size(); ++index)
    {
        if (index > 0)
        {
            stream << separator;
        }

        stream << values[index];
    }

    return stream.str();
}

std::string StatusToString(ResultStatus status)
{
    switch (status)
    {
    case ResultStatus::detected:
        return "detected";
    case ResultStatus::not_detected:
        return "not_detected";
    case ResultStatus::error:
        return "error";
    case ResultStatus::unsupported:
        return "unsupported";
    }

    return "error";
}

std::string ReadTextFile(const std::filesystem::path& path, std::string& error_message)
{
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open())
    {
        error_message = "failed to open file: " + path.string();
        return "";
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    if (!input.good() && !input.eof())
    {
        error_message = "failed to read file: " + path.string();
        return "";
    }

    return buffer.str();
}

std::string ExpandPath(const std::string& input)
{
    char buffer[MAX_PATH * 4]{};
    const DWORD written = ExpandEnvironmentStringsA(input.c_str(), buffer, static_cast<DWORD>(std::size(buffer)));
    if (written == 0 || written > std::size(buffer))
    {
        return input;
    }

    return std::string(buffer);
}

bool PathExists(const std::string& path)
{
    return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::string FormatLastError(DWORD error_code)
{
    LPSTR buffer = nullptr;
    const DWORD size = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error_code,
        0,
        reinterpret_cast<LPSTR>(&buffer),
        0,
        nullptr);

    std::string message = size != 0 && buffer != nullptr ? Trim(buffer) : "unknown error";
    if (buffer != nullptr)
    {
        LocalFree(buffer);
    }

    std::ostringstream stream;
    stream << message << " (" << error_code << ")";
    return stream.str();
}

LONG TryOpenRegistryKey(HKEY root, const std::string& subkey, HKEY& key_handle)
{
    const REGSAM sam_flags[] =
    {
        KEY_READ | KEY_WOW64_64KEY,
        KEY_READ | KEY_WOW64_32KEY,
        KEY_READ
    };

    for (const REGSAM sam_flag : sam_flags)
    {
        const LONG result = RegOpenKeyExA(root, subkey.c_str(), 0, sam_flag, &key_handle);
        if (result == ERROR_SUCCESS)
        {
            return ERROR_SUCCESS;
        }

        if (result != ERROR_FILE_NOT_FOUND && result != ERROR_PATH_NOT_FOUND)
        {
            return result;
        }
    }

    return ERROR_FILE_NOT_FOUND;
}

std::optional<std::string> ReadRegistryStringValue(
    HKEY root,
    const std::string& subkey,
    const std::string& value_name,
    DWORD& error_code)
{
    HKEY key_handle = nullptr;
    error_code = TryOpenRegistryKey(root, subkey, key_handle);
    if (error_code != ERROR_SUCCESS)
    {
        return std::nullopt;
    }

    DWORD type = 0;
    DWORD size = 0;
    error_code = RegQueryValueExA(key_handle, value_name.c_str(), nullptr, &type, nullptr, &size);
    if (error_code != ERROR_SUCCESS)
    {
        RegCloseKey(key_handle);
        return std::nullopt;
    }

    std::vector<char> buffer(size + 2, '\0');
    error_code = RegQueryValueExA(
        key_handle,
        value_name.c_str(),
        nullptr,
        &type,
        reinterpret_cast<LPBYTE>(buffer.data()),
        &size);
    RegCloseKey(key_handle);
    if (error_code != ERROR_SUCCESS)
    {
        return std::nullopt;
    }

    if (type != REG_SZ && type != REG_EXPAND_SZ && type != REG_MULTI_SZ)
    {
        error_code = ERROR_INVALID_DATATYPE;
        return std::nullopt;
    }

    return std::string(buffer.data());
}

bool RegistryKeyExists(HKEY root, const std::string& subkey, DWORD& error_code)
{
    HKEY key_handle = nullptr;
    error_code = TryOpenRegistryKey(root, subkey, key_handle);
    if (error_code == ERROR_SUCCESS)
    {
        RegCloseKey(key_handle);
        return true;
    }

    return false;
}

std::string JsonEscape(const std::string& value)
{
    std::ostringstream escaped;

    for (const char ch : value)
    {
        switch (ch)
        {
        case '\\':
            escaped << "\\\\";
            break;
        case '\"':
            escaped << "\\\"";
            break;
        case '\b':
            escaped << "\\b";
            break;
        case '\f':
            escaped << "\\f";
            break;
        case '\n':
            escaped << "\\n";
            break;
        case '\r':
            escaped << "\\r";
            break;
        case '\t':
            escaped << "\\t";
            break;
        default:
            if (static_cast<unsigned char>(ch) < 0x20)
            {
                escaped << "\\u"
                        << std::hex
                        << std::setw(4)
                        << std::setfill('0')
                        << static_cast<int>(static_cast<unsigned char>(ch))
                        << std::dec
                        << std::setfill(' ');
            }
            else
            {
                escaped << ch;
            }
            break;
        }
    }

    return escaped.str();
}

std::optional<std::string> ExtractJsonStringField(
    const std::string& json_text,
    const std::string& field_name)
{
    const std::string field_token = "\"" + field_name + "\"";
    const std::size_t field_position = json_text.find(field_token);
    if (field_position == std::string::npos)
    {
        return std::nullopt;
    }

    const std::size_t colon_position = json_text.find(':', field_position + field_token.size());
    if (colon_position == std::string::npos)
    {
        return std::nullopt;
    }

    const std::size_t opening_quote = json_text.find('\"', colon_position + 1);
    if (opening_quote == std::string::npos)
    {
        return std::nullopt;
    }

    std::size_t closing_quote = opening_quote + 1;
    while (closing_quote < json_text.size())
    {
        if (json_text[closing_quote] == '\"' && json_text[closing_quote - 1] != '\\')
        {
            return json_text.substr(opening_quote + 1, closing_quote - opening_quote - 1);
        }

        ++closing_quote;
    }

    return std::nullopt;
}

std::optional<int> ExtractJsonIntField(const std::string& json_text, const std::string& field_name)
{
    const std::string field_token = "\"" + field_name + "\"";
    const std::size_t field_position = json_text.find(field_token);
    if (field_position == std::string::npos)
    {
        return std::nullopt;
    }

    const std::size_t colon_position = json_text.find(':', field_position + field_token.size());
    if (colon_position == std::string::npos)
    {
        return std::nullopt;
    }

    const std::size_t value_start = json_text.find_first_of("-0123456789", colon_position + 1);
    if (value_start == std::string::npos)
    {
        return std::nullopt;
    }

    std::size_t value_end = value_start;
    while (value_end < json_text.size() && (json_text[value_end] == '-' || std::isdigit(static_cast<unsigned char>(json_text[value_end]))))
    {
        ++value_end;
    }

    try
    {
        return std::stoi(json_text.substr(value_start, value_end - value_start));
    }
    catch (...)
    {
        return std::nullopt;
    }
}

std::optional<bool> ExtractJsonBoolField(const std::string& json_text, const std::string& field_name)
{
    const std::string field_token = "\"" + field_name + "\"";
    const std::size_t field_position = json_text.find(field_token);
    if (field_position == std::string::npos)
    {
        return std::nullopt;
    }

    const std::size_t colon_position = json_text.find(':', field_position + field_token.size());
    if (colon_position == std::string::npos)
    {
        return std::nullopt;
    }

    const std::size_t value_start = json_text.find_first_not_of(" \t\r\n", colon_position + 1);
    if (value_start == std::string::npos)
    {
        return std::nullopt;
    }

    if (json_text.compare(value_start, 4, "true") == 0)
    {
        return true;
    }

    if (json_text.compare(value_start, 5, "false") == 0)
    {
        return false;
    }

    return std::nullopt;
}

std::optional<std::vector<std::string>> ExtractJsonStringArrayField(
    const std::string& json_text,
    const std::string& field_name)
{
    const std::string field_token = "\"" + field_name + "\"";
    const std::size_t field_position = json_text.find(field_token);
    if (field_position == std::string::npos)
    {
        return std::nullopt;
    }

    const std::size_t colon_position = json_text.find(':', field_position + field_token.size());
    if (colon_position == std::string::npos)
    {
        return std::nullopt;
    }

    const std::size_t opening_bracket = json_text.find('[', colon_position + 1);
    const std::size_t closing_bracket = json_text.find(']', opening_bracket + 1);
    if (opening_bracket == std::string::npos || closing_bracket == std::string::npos)
    {
        return std::nullopt;
    }

    std::vector<std::string> values;
    std::size_t position = opening_bracket + 1;
    while (position < closing_bracket)
    {
        const std::size_t opening_quote = json_text.find('\"', position);
        if (opening_quote == std::string::npos || opening_quote >= closing_bracket)
        {
            break;
        }

        std::size_t closing_quote = opening_quote + 1;
        while (closing_quote < closing_bracket)
        {
            if (json_text[closing_quote] == '\"' && json_text[closing_quote - 1] != '\\')
            {
                values.push_back(json_text.substr(opening_quote + 1, closing_quote - opening_quote - 1));
                position = closing_quote + 1;
                break;
            }

            ++closing_quote;
        }

        if (closing_quote >= closing_bracket)
        {
            return std::nullopt;
        }
    }

    return values;
}

bool ParseProfileJson(const std::string& json_text, Profile& profile, std::string& error_message)
{
    const std::optional<std::string> profile_id = ExtractJsonStringField(json_text, "profile_id");
    const std::optional<std::string> name = ExtractJsonStringField(json_text, "name");
    const std::optional<int> version = ExtractJsonIntField(json_text, "version");
    const std::optional<std::vector<std::string>> checks = ExtractJsonStringArrayField(json_text, "checks");
    const std::optional<std::string> output_directory = ExtractJsonStringField(json_text, "output_directory");
    const std::optional<bool> console_logging_enabled = ExtractJsonBoolField(json_text, "console_logging_enabled");
    const std::optional<bool> json_logging_enabled = ExtractJsonBoolField(json_text, "json_logging_enabled");

    if (!profile_id || !name || !version || !checks || !output_directory || !console_logging_enabled || !json_logging_enabled)
    {
        error_message = "profile is missing one or more required fields";
        return false;
    }

    profile.profile_id = *profile_id;
    profile.name = *name;
    profile.version = *version;
    profile.checks = *checks;
    profile.output_directory = Trim(*output_directory);
    profile.console_logging_enabled = *console_logging_enabled;
    profile.json_logging_enabled = *json_logging_enabled;
    return true;
}

bool LoadProfile(const std::filesystem::path& profile_path, Profile& profile, std::string& error_message)
{
    const std::string json_text = ReadTextFile(profile_path, error_message);
    if (!error_message.empty())
    {
        return false;
    }

    return ParseProfileJson(json_text, profile, error_message);
}

bool LoadEmbeddedProfile(Profile& profile, std::string& error_message)
{
    return ParseProfileJson(sandbox_benchmark::kEmbeddedProfileJson, profile, error_message);
}

CpuidRegisters QueryCpuid(int leaf, int subleaf = 0)
{
    int registers[4]{};
    __cpuidex(registers, leaf, subleaf);
    return CpuidRegisters{registers[0], registers[1], registers[2], registers[3]};
}

std::string RegistersToString(int first, int second, int third)
{
    char value[13]{};
    std::memcpy(value, &first, sizeof(first));
    std::memcpy(value + 4, &second, sizeof(second));
    std::memcpy(value + 8, &third, sizeof(third));
    return std::string(value);
}

std::string GetCpuVendorString()
{
#if defined(_M_IX86) || defined(_M_X64)
    const CpuidRegisters registers = QueryCpuid(0x00000000);
    return Trim(RegistersToString(registers.ebx, registers.edx, registers.ecx));
#else
    return "";
#endif
}

std::string GetHypervisorVendorString()
{
#if defined(_M_IX86) || defined(_M_X64)
    const CpuidRegisters registers = QueryCpuid(0x40000000);
    return Trim(RegistersToString(registers.ebx, registers.ecx, registers.edx));
#else
    return "";
#endif
}

std::string GetCpuBrandString()
{
#if defined(_M_IX86) || defined(_M_X64)
    const CpuidRegisters maximum_leaf = QueryCpuid(0x80000000);
    if (static_cast<std::uint32_t>(maximum_leaf.eax) < 0x80000004U)
    {
        return "";
    }

    std::array<int, 12> brand_registers{};
    for (int offset = 0; offset < 3; ++offset)
    {
        const CpuidRegisters registers = QueryCpuid(0x80000002 + offset);
        brand_registers[offset * 4 + 0] = registers.eax;
        brand_registers[offset * 4 + 1] = registers.ebx;
        brand_registers[offset * 4 + 2] = registers.ecx;
        brand_registers[offset * 4 + 3] = registers.edx;
    }

    char brand[49]{};
    std::memcpy(brand, brand_registers.data(), 48);
    return Trim(std::string(brand));
#else
    return "";
#endif
}

std::optional<bool> GetHypervisorBit()
{
#if defined(_M_IX86) || defined(_M_X64)
    const CpuidRegisters registers = QueryCpuid(0x00000001);
    return ((registers.ecx >> 31) & 0x1) != 0;
#else
    return std::nullopt;
#endif
}

std::optional<std::uint32_t> GetPebProcessorCount()
{
#if defined(_M_X64)
    const auto* peb = reinterpret_cast<const PafishPeb*>(__readgsqword(0x60));
    return peb != nullptr ? std::optional<std::uint32_t>(peb->NumberOfProcessors) : std::nullopt;
#elif defined(_M_IX86)
    const auto* peb = reinterpret_cast<const PafishPeb*>(__readfsdword(0x30));
    return peb != nullptr ? std::optional<std::uint32_t>(peb->NumberOfProcessors) : std::nullopt;
#else
    return std::nullopt;
#endif
}

std::string WideToUtf8(const std::wstring& value)
{
    if (value.empty())
    {
        return "";
    }

    const int required_size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (required_size <= 0)
    {
        return "";
    }

    std::string output(static_cast<std::size_t>(required_size) - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, output.data(), required_size, nullptr, nullptr);
    return output;
}

ResultStatus BoolToStatus(bool detected)
{
    return detected ? ResultStatus::detected : ResultStatus::not_detected;
}

std::string FormatTimestamp(std::chrono::system_clock::time_point time_point)
{
    const std::time_t timestamp = std::chrono::system_clock::to_time_t(time_point);
    std::tm local_time{};
    localtime_s(&local_time, &timestamp);

    std::ostringstream stream;
    stream << std::put_time(&local_time, "%Y-%m-%dT%H:%M:%S");
    return stream.str();
}

CheckResult MakeCheckResult(
    const std::string& check_id,
    ResultStatus status,
    const std::string& evidence,
    std::chrono::system_clock::time_point started_at,
    std::chrono::system_clock::time_point finished_at)
{
    return CheckResult{
        check_id,
        status,
        evidence,
        FormatTimestamp(started_at),
        FormatTimestamp(finished_at)
    };
}

CheckResult RunDemoCheck(const std::string& check_id, const std::string& evidence)
{
    const auto started_at = std::chrono::system_clock::now();
    const auto finished_at = std::chrono::system_clock::now();
    return MakeCheckResult(check_id, ResultStatus::not_detected, evidence, started_at, finished_at);
}

CheckResult RunLogicalProcessorCountCheck()
{
    const auto started_at = std::chrono::system_clock::now();

    SYSTEM_INFO system_info{};
    GetSystemInfo(&system_info);

    std::ostringstream evidence;
    evidence << "logical processors: " << system_info.dwNumberOfProcessors;

    const auto finished_at = std::chrono::system_clock::now();
    return MakeCheckResult(
        "env.cpu.logical_processor_count",
        ResultStatus::not_detected,
        evidence.str(),
        started_at,
        finished_at);
}

CheckResult RunTotalPhysicalMemoryCheck()
{
    const auto started_at = std::chrono::system_clock::now();

    MEMORYSTATUSEX memory_status{};
    memory_status.dwLength = sizeof(memory_status);
    if (!GlobalMemoryStatusEx(&memory_status))
    {
        const auto finished_at = std::chrono::system_clock::now();
        return MakeCheckResult(
            "env.memory.total_physical_mb",
            ResultStatus::error,
            "failed to query total physical memory",
            started_at,
            finished_at);
    }

    const std::uint64_t total_physical_mb =
        static_cast<std::uint64_t>(memory_status.ullTotalPhys / (1024ULL * 1024ULL));

    std::ostringstream evidence;
    evidence << "total physical memory mb: " << total_physical_mb;

    const auto finished_at = std::chrono::system_clock::now();
    return MakeCheckResult(
        "env.memory.total_physical_mb",
        ResultStatus::not_detected,
        evidence.str(),
        started_at,
        finished_at);
}

CheckResult RunSystemDriveTotalSizeCheck()
{
    const auto started_at = std::chrono::system_clock::now();

    char windows_directory[MAX_PATH]{};
    const UINT windows_directory_length = GetWindowsDirectoryA(windows_directory, MAX_PATH);
    if (windows_directory_length == 0 || windows_directory_length >= MAX_PATH)
    {
        const auto finished_at = std::chrono::system_clock::now();
        return MakeCheckResult(
            "env.storage.system_drive_total_gb",
            ResultStatus::error,
            "failed to resolve windows directory",
            started_at,
            finished_at);
    }

    const std::filesystem::path windows_path(windows_directory);
    const std::filesystem::path system_drive_root = windows_path.root_name() / windows_path.root_directory();

    ULARGE_INTEGER total_bytes{};
    if (!GetDiskFreeSpaceExA(system_drive_root.string().c_str(), nullptr, &total_bytes, nullptr))
    {
        const auto finished_at = std::chrono::system_clock::now();
        return MakeCheckResult(
            "env.storage.system_drive_total_gb",
            ResultStatus::error,
            "failed to query system drive size",
            started_at,
            finished_at);
    }

    const std::uint64_t total_gb =
        static_cast<std::uint64_t>(total_bytes.QuadPart / (1024ULL * 1024ULL * 1024ULL));

    std::ostringstream evidence;
    evidence << "system drive total gb: " << total_gb;

    const auto finished_at = std::chrono::system_clock::now();
    return MakeCheckResult(
        "env.storage.system_drive_total_gb",
        ResultStatus::not_detected,
        evidence.str(),
        started_at,
        finished_at);
}

CheckResult RunRegistryKeyExistsCheck(
    const std::string& check_id,
    HKEY root,
    const std::string& subkey)
{
    const auto started_at = std::chrono::system_clock::now();
    DWORD error_code = ERROR_SUCCESS;
    const bool detected = RegistryKeyExists(root, subkey, error_code);
    const std::string location = (root == HKEY_LOCAL_MACHINE ? "HKLM\\" : "HKCU\\") + subkey;

    if (!detected && error_code != ERROR_FILE_NOT_FOUND && error_code != ERROR_PATH_NOT_FOUND)
    {
        return MakeCheckResult(
            check_id,
            ResultStatus::error,
            "failed to open registry key " + location + ": " + FormatLastError(error_code),
            started_at,
            std::chrono::system_clock::now());
    }

    return MakeCheckResult(
        check_id,
        BoolToStatus(detected),
        std::string("registry key ") + (detected ? "present: " : "not present: ") + location,
        started_at,
        std::chrono::system_clock::now());
}

CheckResult RunRegistryValueContainsCheck(
    const std::string& check_id,
    HKEY root,
    const std::string& subkey,
    const std::string& value_name,
    const std::string& needle)
{
    const auto started_at = std::chrono::system_clock::now();
    DWORD error_code = ERROR_SUCCESS;
    const std::optional<std::string> value = ReadRegistryStringValue(root, subkey, value_name, error_code);
    const std::string location = (root == HKEY_LOCAL_MACHINE ? "HKLM\\" : "HKCU\\") + subkey + " [" + value_name + "]";
    if (!value.has_value())
    {
        if (error_code == ERROR_FILE_NOT_FOUND || error_code == ERROR_PATH_NOT_FOUND)
        {
            return MakeCheckResult(
                check_id,
                ResultStatus::not_detected,
                "registry value not present: " + location,
                started_at,
                std::chrono::system_clock::now());
        }

        return MakeCheckResult(
            check_id,
            ResultStatus::error,
            "failed to read registry value " + location + ": " + FormatLastError(error_code),
            started_at,
            std::chrono::system_clock::now());
    }

    return MakeCheckResult(
        check_id,
        BoolToStatus(ContainsInsensitive(*value, needle)),
        "registry value " + location + ": " + *value,
        started_at,
        std::chrono::system_clock::now());
}

CheckResult RunAnyFileCheck(
    const std::string& check_id,
    const std::vector<std::string>& candidate_paths,
    const std::string& category)
{
    const auto started_at = std::chrono::system_clock::now();
    for (const std::string& candidate_path : candidate_paths)
    {
        const std::string expanded_path = ExpandPath(candidate_path);
        if (PathExists(expanded_path))
        {
            return MakeCheckResult(
                check_id,
                ResultStatus::detected,
                category + " present: " + expanded_path,
                started_at,
                std::chrono::system_clock::now());
        }
    }

    return MakeCheckResult(
        check_id,
        ResultStatus::not_detected,
        category + " not present",
        started_at,
        std::chrono::system_clock::now());
}

CheckResult RunAnyDeviceCheck(
    const std::string& check_id,
    const std::vector<std::string>& device_names,
    const std::string& category)
{
    const auto started_at = std::chrono::system_clock::now();
    std::vector<std::string> matches;
    for (const std::string& device_name : device_names)
    {
        HANDLE handle = CreateFileA(
            device_name.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (handle != INVALID_HANDLE_VALUE)
        {
            matches.push_back(device_name);
            CloseHandle(handle);
        }
    }

    return MakeCheckResult(
        check_id,
        BoolToStatus(!matches.empty()),
        matches.empty() ? category + " not present" : category + " opened: " + JoinStrings(matches, ", "),
        started_at,
        std::chrono::system_clock::now());
}

CheckResult RunAnyProcessCheck(
    const std::string& check_id,
    const std::vector<std::string>& process_names,
    const std::string& category)
{
    const auto started_at = std::chrono::system_clock::now();
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        return MakeCheckResult(
            check_id,
            ResultStatus::error,
            "failed to enumerate processes: " + FormatLastError(GetLastError()),
            started_at,
            std::chrono::system_clock::now());
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    std::vector<std::string> matches;
    if (Process32FirstW(snapshot, &entry))
    {
        do
        {
            const std::string process_name = WideToUtf8(entry.szExeFile);
            for (const std::string& candidate : process_names)
            {
                if (_stricmp(process_name.c_str(), candidate.c_str()) == 0)
                {
                    matches.push_back(process_name);
                }
            }
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return MakeCheckResult(
        check_id,
        BoolToStatus(!matches.empty()),
        matches.empty() ? category + " not found" : category + " found: " + JoinStrings(matches, ", "),
        started_at,
        std::chrono::system_clock::now());
}

CheckResult RunWindowCheck(
    const std::string& check_id,
    const std::vector<std::pair<std::string, std::string>>& variants,
    const std::string& category)
{
    const auto started_at = std::chrono::system_clock::now();
    std::vector<std::string> matches;
    for (const auto& variant : variants)
    {
        const HWND window_handle = FindWindowA(
            variant.first.empty() ? nullptr : variant.first.c_str(),
            variant.second.empty() ? nullptr : variant.second.c_str());
        if (window_handle != nullptr)
        {
            matches.push_back(
                "class=" + (variant.first.empty() ? std::string("<null>") : variant.first) +
                ", title=" + (variant.second.empty() ? std::string("<null>") : variant.second));
        }
    }

    return MakeCheckResult(
        check_id,
        BoolToStatus(!matches.empty()),
        matches.empty() ? category + " not found" : category + " found: " + JoinStrings(matches, " | "),
        started_at,
        std::chrono::system_clock::now());
}

CheckResult RunMacPrefixCheck(
    const std::string& check_id,
    const std::vector<std::array<std::uint8_t, 3>>& prefixes,
    const std::string& category)
{
    const auto started_at = std::chrono::system_clock::now();
    ULONG buffer_length = 0;
    ULONG result = GetAdaptersAddresses(AF_UNSPEC, 0, nullptr, nullptr, &buffer_length);
    if (result != ERROR_BUFFER_OVERFLOW)
    {
        return MakeCheckResult(
            check_id,
            ResultStatus::error,
            "GetAdaptersAddresses size query failed: " + FormatLastError(result),
            started_at,
            std::chrono::system_clock::now());
    }

    std::vector<std::uint8_t> buffer(buffer_length);
    auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
    result = GetAdaptersAddresses(AF_UNSPEC, 0, nullptr, adapters, &buffer_length);
    if (result != ERROR_SUCCESS)
    {
        return MakeCheckResult(
            check_id,
            ResultStatus::error,
            "GetAdaptersAddresses failed: " + FormatLastError(result),
            started_at,
            std::chrono::system_clock::now());
    }

    for (const IP_ADAPTER_ADDRESSES* adapter = adapters; adapter != nullptr; adapter = adapter->Next)
    {
        if (adapter->PhysicalAddressLength < 3)
        {
            continue;
        }

        for (const auto& prefix : prefixes)
        {
            if (adapter->PhysicalAddress[0] == prefix[0] &&
                adapter->PhysicalAddress[1] == prefix[1] &&
                adapter->PhysicalAddress[2] == prefix[2])
            {
                std::ostringstream evidence;
                evidence << category << " MAC prefix: "
                         << std::uppercase << std::hex << std::setfill('0')
                         << std::setw(2) << static_cast<int>(adapter->PhysicalAddress[0]) << ":"
                         << std::setw(2) << static_cast<int>(adapter->PhysicalAddress[1]) << ":"
                         << std::setw(2) << static_cast<int>(adapter->PhysicalAddress[2]);
                return MakeCheckResult(check_id, ResultStatus::detected, evidence.str(), started_at, std::chrono::system_clock::now());
            }
        }
    }

    return MakeCheckResult(check_id, ResultStatus::not_detected, category + " MAC prefix not found", started_at, std::chrono::system_clock::now());
}

CheckResult RunAdapterDescriptionCheck(
    const std::string& check_id,
    const std::string& needle,
    const std::string& category)
{
    const auto started_at = std::chrono::system_clock::now();
    ULONG buffer_length = 0;
    ULONG result = GetAdaptersAddresses(AF_UNSPEC, 0, nullptr, nullptr, &buffer_length);
    if (result != ERROR_BUFFER_OVERFLOW)
    {
        return MakeCheckResult(
            check_id,
            ResultStatus::error,
            "GetAdaptersAddresses size query failed: " + FormatLastError(result),
            started_at,
            std::chrono::system_clock::now());
    }

    std::vector<std::uint8_t> buffer(buffer_length);
    auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
    result = GetAdaptersAddresses(AF_UNSPEC, 0, nullptr, adapters, &buffer_length);
    if (result != ERROR_SUCCESS)
    {
        return MakeCheckResult(
            check_id,
            ResultStatus::error,
            "GetAdaptersAddresses failed: " + FormatLastError(result),
            started_at,
            std::chrono::system_clock::now());
    }

    for (const IP_ADAPTER_ADDRESSES* adapter = adapters; adapter != nullptr; adapter = adapter->Next)
    {
        const std::string description = WideToUtf8(adapter->Description != nullptr ? adapter->Description : L"");
        if (ContainsInsensitive(description, needle))
        {
            return MakeCheckResult(
                check_id,
                ResultStatus::detected,
                category + " adapter description: " + description,
                started_at,
                std::chrono::system_clock::now());
        }
    }

    return MakeCheckResult(check_id, ResultStatus::not_detected, category + " adapter description not found", started_at, std::chrono::system_clock::now());
}

bool InitializeWmi(IWbemServices** services, std::string& error_message)
{
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(result) && result != RPC_E_CHANGED_MODE)
    {
        error_message = "CoInitializeEx failed";
        return false;
    }

    result = CoInitializeSecurity(
        nullptr,
        -1,
        nullptr,
        nullptr,
        RPC_C_AUTHN_LEVEL_DEFAULT,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        nullptr,
        EOAC_NONE,
        nullptr);
    if (FAILED(result) && result != RPC_E_TOO_LATE)
    {
        error_message = "CoInitializeSecurity failed";
        CoUninitialize();
        return false;
    }

    IWbemLocator* locator = nullptr;
    result = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator, reinterpret_cast<LPVOID*>(&locator));
    if (FAILED(result) || locator == nullptr)
    {
        error_message = "CoCreateInstance for IWbemLocator failed";
        CoUninitialize();
        return false;
    }

    BSTR namespace_name = SysAllocString(L"ROOT\\CIMV2");
    result = locator->ConnectServer(namespace_name, nullptr, nullptr, nullptr, 0, nullptr, nullptr, services);
    SysFreeString(namespace_name);
    locator->Release();
    if (FAILED(result) || *services == nullptr)
    {
        error_message = "IWbemLocator::ConnectServer failed";
        CoUninitialize();
        return false;
    }

    result = CoSetProxyBlanket(
        *services,
        RPC_C_AUTHN_WINNT,
        RPC_C_AUTHZ_NONE,
        nullptr,
        RPC_C_AUTHN_LEVEL_CALL,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        nullptr,
        EOAC_NONE);
    if (FAILED(result))
    {
        error_message = "CoSetProxyBlanket failed";
        (*services)->Release();
        *services = nullptr;
        CoUninitialize();
        return false;
    }

    return true;
}

void CleanupWmi(IWbemServices* services)
{
    if (services != nullptr)
    {
        services->Release();
    }

    CoUninitialize();
}

CheckResult RunWmiContainsCheck(
    const std::string& check_id,
    const std::wstring& query,
    const std::wstring& property_name,
    const std::vector<std::wstring>& needles,
    const std::string& category)
{
    const auto started_at = std::chrono::system_clock::now();
    IWbemServices* services = nullptr;
    std::string error_message;
    if (!InitializeWmi(&services, error_message))
    {
        return MakeCheckResult(check_id, ResultStatus::error, error_message, started_at, std::chrono::system_clock::now());
    }

    IEnumWbemClassObject* enumerator = nullptr;
    BSTR language = SysAllocString(L"WQL");
    BSTR query_bstr = SysAllocString(query.c_str());
    HRESULT result = services->ExecQuery(
        language,
        query_bstr,
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        nullptr,
        &enumerator);
    SysFreeString(language);
    SysFreeString(query_bstr);
    if (FAILED(result) || enumerator == nullptr)
    {
        CleanupWmi(services);
        return MakeCheckResult(check_id, ResultStatus::error, "WMI query failed", started_at, std::chrono::system_clock::now());
    }

    bool detected = false;
    std::string observed_value = category + " not found";
    while (!detected)
    {
        IWbemClassObject* row = nullptr;
        ULONG returned = 0;
        result = enumerator->Next(WBEM_INFINITE, 1, &row, &returned);
        if (FAILED(result) || returned == 0 || row == nullptr)
        {
            break;
        }

        VARIANT value;
        VariantInit(&value);
        if (SUCCEEDED(row->Get(property_name.c_str(), 0, &value, nullptr, nullptr)) &&
            value.vt == VT_BSTR &&
            value.bstrVal != nullptr)
        {
            const std::wstring current_value = value.bstrVal;
            observed_value = WideToUtf8(current_value);
            for (const std::wstring& needle : needles)
            {
                if (current_value.find(needle) != std::wstring::npos)
                {
                    detected = true;
                    break;
                }
            }
        }

        VariantClear(&value);
        row->Release();
    }

    enumerator->Release();
    CleanupWmi(services);
    return MakeCheckResult(
        check_id,
        BoolToStatus(detected),
        category + ": " + observed_value,
        started_at,
        std::chrono::system_clock::now());
}

CheckResult RunHypervisorBitCheck()
{
    const auto started_at = std::chrono::system_clock::now();
    const std::optional<bool> hypervisor_bit = GetHypervisorBit();
    if (!hypervisor_bit.has_value())
    {
        return MakeCheckResult(
            "pafish.cpu.hypervisor_bit",
            ResultStatus::unsupported,
            "cpuid not supported on this architecture",
            started_at,
            std::chrono::system_clock::now());
    }

    return MakeCheckResult(
        "pafish.cpu.hypervisor_bit",
        BoolToStatus(*hypervisor_bit),
        std::string("hypervisor bit: ") + (*hypervisor_bit ? "1" : "0"),
        started_at,
        std::chrono::system_clock::now());
}

CheckResult RunKnownHypervisorVendorCheck()
{
    const auto started_at = std::chrono::system_clock::now();
    const std::optional<bool> hypervisor_bit = GetHypervisorBit();
    if (!hypervisor_bit.has_value())
    {
        return MakeCheckResult(
            "pafish.cpu.known_vm_hypervisor_vendor",
            ResultStatus::unsupported,
            "cpuid not supported on this architecture",
            started_at,
            std::chrono::system_clock::now());
    }

    const std::string vendor = GetHypervisorVendorString();
    const std::vector<std::string> known_vendors =
    {
        "KVMKVMKVM",
        "MICROSOFT HV",
        "VMWAREVMWARE",
        "XENVMMXENVMM",
        "PRL HYPERV",
        "VBOXVBOXVBOX"
    };

    bool detected = false;
    const std::string upper_vendor = ToUpperAscii(vendor);
    for (const std::string& known_vendor : known_vendors)
    {
        if (upper_vendor.find(known_vendor) != std::string::npos)
        {
            detected = true;
            break;
        }
    }

    return MakeCheckResult(
        "pafish.cpu.known_vm_hypervisor_vendor",
        BoolToStatus(detected),
        "hypervisor vendor: " + (vendor.empty() ? std::string("<empty>") : vendor),
        started_at,
        std::chrono::system_clock::now());
}

CheckResult RunUsernameCheck()
{
    const auto started_at = std::chrono::system_clock::now();
    char username[256]{};
    DWORD username_size = static_cast<DWORD>(std::size(username));
    if (!GetUserNameA(username, &username_size))
    {
        return MakeCheckResult(
            "pafish.gensandbox.username",
            ResultStatus::error,
            "failed to query user name: " + FormatLastError(GetLastError()),
            started_at,
            std::chrono::system_clock::now());
    }

    const std::string value = username;
    const bool detected = ContainsInsensitive(value, "SANDBOX") ||
                          ContainsInsensitive(value, "VIRUS") ||
                          ContainsInsensitive(value, "MALWARE");
    return MakeCheckResult(
        "pafish.gensandbox.username",
        BoolToStatus(detected),
        "user name: " + value,
        started_at,
        std::chrono::system_clock::now());
}

CheckResult RunPathCheck()
{
    const auto started_at = std::chrono::system_clock::now();
    char path[MAX_PATH]{};
    const DWORD path_length = GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (path_length == 0 || path_length >= MAX_PATH)
    {
        return MakeCheckResult(
            "pafish.gensandbox.path",
            ResultStatus::error,
            "failed to query module path: " + FormatLastError(GetLastError()),
            started_at,
            std::chrono::system_clock::now());
    }

    const std::string value = path;
    const bool detected = ContainsInsensitive(value, "\\SAMPLE") ||
                          ContainsInsensitive(value, "\\VIRUS") ||
                          ContainsInsensitive(value, "SANDBOX");
    return MakeCheckResult(
        "pafish.gensandbox.path",
        BoolToStatus(detected),
        "module path: " + value,
        started_at,
        std::chrono::system_clock::now());
}

CheckResult RunCommonSampleNamesCheck()
{
    const auto started_at = std::chrono::system_clock::now();
    char drive_buffer[512]{};
    const DWORD length = GetLogicalDriveStringsA(static_cast<DWORD>(std::size(drive_buffer)), drive_buffer);
    if (length == 0 || length > std::size(drive_buffer))
    {
        return MakeCheckResult(
            "pafish.gensandbox.common_sample_names",
            ResultStatus::error,
            "failed to enumerate logical drives: " + FormatLastError(GetLastError()),
            started_at,
            std::chrono::system_clock::now());
    }

    std::vector<std::string> matches;
    for (char* drive = drive_buffer; *drive != '\0'; drive += std::strlen(drive) + 1)
    {
        if (GetDriveTypeA(drive) == DRIVE_REMOVABLE)
        {
            continue;
        }

        const std::string sample_path = std::string(drive) + "sample.exe";
        if (PathExists(sample_path))
        {
            matches.push_back(sample_path);
        }

        const std::string malware_path = std::string(drive) + "malware.exe";
        if (PathExists(malware_path))
        {
            matches.push_back(malware_path);
        }
    }

    return MakeCheckResult(
        "pafish.gensandbox.common_sample_names",
        BoolToStatus(!matches.empty()),
        matches.empty() ? "common sample names not found on fixed drives" : "matched files: " + JoinStrings(matches, ", "),
        started_at,
        std::chrono::system_clock::now());
}

CheckResult RunDriveSizeDeviceIoControlCheck()
{
    const auto started_at = std::chrono::system_clock::now();
    HANDLE drive = CreateFileA(
        "\\\\.\\PhysicalDrive0",
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr);
    if (drive == INVALID_HANDLE_VALUE)
    {
        return MakeCheckResult(
            "pafish.gensandbox.drive_size_deviceiocontrol",
            ResultStatus::error,
            "failed to open \\\\.\\PhysicalDrive0: " + FormatLastError(GetLastError()),
            started_at,
            std::chrono::system_clock::now());
    }

    GET_LENGTH_INFORMATION size{};
    DWORD bytes_returned = 0;
    const BOOL device_result = DeviceIoControl(
        drive,
        IOCTL_DISK_GET_LENGTH_INFO,
        nullptr,
        0,
        &size,
        sizeof(size),
        &bytes_returned,
        nullptr);
    const DWORD error_code = GetLastError();
    CloseHandle(drive);
    if (!device_result)
    {
        return MakeCheckResult(
            "pafish.gensandbox.drive_size_deviceiocontrol",
            ResultStatus::error,
            "failed to query drive length: " + FormatLastError(error_code),
            started_at,
            std::chrono::system_clock::now());
    }

    const std::uint64_t total_gb = static_cast<std::uint64_t>(size.Length.QuadPart / 1073741824LL);
    std::ostringstream evidence;
    evidence << "physical drive 0 total gb: " << total_gb;
    return MakeCheckResult(
        "pafish.gensandbox.drive_size_deviceiocontrol",
        BoolToStatus(total_gb <= 60),
        evidence.str(),
        started_at,
        std::chrono::system_clock::now());
}

CheckResult RunDriveSizeGetDiskFreeSpaceExCheck()
{
    const auto started_at = std::chrono::system_clock::now();
    ULARGE_INTEGER total_bytes{};
    if (!GetDiskFreeSpaceExA("C:\\", nullptr, &total_bytes, nullptr))
    {
        return MakeCheckResult(
            "pafish.gensandbox.drive_size_getdiskfreespaceex",
            ResultStatus::error,
            "failed to query C: drive size: " + FormatLastError(GetLastError()),
            started_at,
            std::chrono::system_clock::now());
    }

    const std::uint64_t total_gb = total_bytes.QuadPart / (1024ULL * 1024ULL * 1024ULL);
    std::ostringstream evidence;
    evidence << "C: total gb: " << total_gb;
    return MakeCheckResult(
        "pafish.gensandbox.drive_size_getdiskfreespaceex",
        BoolToStatus(total_gb <= 60),
        evidence.str(),
        started_at,
        std::chrono::system_clock::now());
}

CheckResult RunOneCpuPebCheck()
{
    const auto started_at = std::chrono::system_clock::now();
    const std::optional<std::uint32_t> processor_count = GetPebProcessorCount();
    if (!processor_count.has_value())
    {
        return MakeCheckResult(
            "pafish.gensandbox.one_cpu_peb",
            ResultStatus::unsupported,
            "PEB processor count unsupported on this architecture",
            started_at,
            std::chrono::system_clock::now());
    }

    std::ostringstream evidence;
    evidence << "PEB processor count: " << *processor_count;
    return MakeCheckResult(
        "pafish.gensandbox.one_cpu_peb",
        BoolToStatus(*processor_count < 2),
        evidence.str(),
        started_at,
        std::chrono::system_clock::now());
}

CheckResult RunOneCpuGetSystemInfoCheck()
{
    const auto started_at = std::chrono::system_clock::now();
    SYSTEM_INFO system_info{};
    GetSystemInfo(&system_info);

    std::ostringstream evidence;
    evidence << "GetSystemInfo processor count: " << system_info.dwNumberOfProcessors;
    return MakeCheckResult(
        "pafish.gensandbox.one_cpu_getsysteminfo",
        BoolToStatus(system_info.dwNumberOfProcessors < 2),
        evidence.str(),
        started_at,
        std::chrono::system_clock::now());
}

CheckResult RunLessThanOneGbCheck()
{
    const auto started_at = std::chrono::system_clock::now();
    MEMORYSTATUSEX memory_status{};
    memory_status.dwLength = sizeof(memory_status);
    if (!GlobalMemoryStatusEx(&memory_status))
    {
        return MakeCheckResult(
            "pafish.gensandbox.less_than_one_gb",
            ResultStatus::error,
            "failed to query total physical memory: " + FormatLastError(GetLastError()),
            started_at,
            std::chrono::system_clock::now());
    }

    const std::uint64_t total_mb = memory_status.ullTotalPhys / (1024ULL * 1024ULL);
    std::ostringstream evidence;
    evidence << "total physical memory mb: " << total_mb;
    return MakeCheckResult(
        "pafish.gensandbox.less_than_one_gb",
        BoolToStatus(total_mb < 1024),
        evidence.str(),
        started_at,
        std::chrono::system_clock::now());
}

CheckResult RunUptimeCheck()
{
    const auto started_at = std::chrono::system_clock::now();
    const ULONGLONG uptime_ms = GetTickCount64();
    std::ostringstream evidence;
    evidence << "uptime ms: " << uptime_ms;
    return MakeCheckResult(
        "pafish.gensandbox.uptime",
        BoolToStatus(uptime_ms < 0xAFE74ULL),
        evidence.str(),
        started_at,
        std::chrono::system_clock::now());
}

CheckResult RunIsNativeVhdBootCheck()
{
    const auto started_at = std::chrono::system_clock::now();
    using IsNativeVhdBootFunction = BOOL(WINAPI*)(BOOL*);
    const auto function = reinterpret_cast<IsNativeVhdBootFunction>(
        GetProcAddress(GetModuleHandleA("kernel32.dll"), "IsNativeVhdBoot"));
    if (function == nullptr)
    {
        return MakeCheckResult(
            "pafish.gensandbox.is_native_vhd_boot",
            ResultStatus::unsupported,
            "IsNativeVhdBoot is not available on this Windows version",
            started_at,
            std::chrono::system_clock::now());
    }

    BOOL is_native_vhd_boot = FALSE;
    if (!function(&is_native_vhd_boot))
    {
        return MakeCheckResult(
            "pafish.gensandbox.is_native_vhd_boot",
            ResultStatus::error,
            "IsNativeVhdBoot failed: " + FormatLastError(GetLastError()),
            started_at,
            std::chrono::system_clock::now());
    }

    return MakeCheckResult(
        "pafish.gensandbox.is_native_vhd_boot",
        BoolToStatus(is_native_vhd_boot != FALSE),
        std::string("native VHD boot: ") + (is_native_vhd_boot ? "true" : "false"),
        started_at,
        std::chrono::system_clock::now());
}

CheckResult RunSandboxieSbieDllCheck()
{
    const auto started_at = std::chrono::system_clock::now();
    const bool detected = GetModuleHandleA("sbiedll.dll") != nullptr;
    return MakeCheckResult(
        "pafish.sandboxie.sbiedll",
        BoolToStatus(detected),
        std::string("sbiedll.dll loaded: ") + (detected ? "yes" : "no"),
        started_at,
        std::chrono::system_clock::now());
}

CheckResult RunWineGetUnixFileNameCheck()
{
    const auto started_at = std::chrono::system_clock::now();
    const HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
    if (kernel32 == nullptr)
    {
        return MakeCheckResult(
            "pafish.wine.get_unix_file_name",
            ResultStatus::error,
            "failed to resolve kernel32.dll",
            started_at,
            std::chrono::system_clock::now());
    }

    const bool detected = GetProcAddress(kernel32, "wine_get_unix_file_name") != nullptr;
    return MakeCheckResult(
        "pafish.wine.get_unix_file_name",
        BoolToStatus(detected),
        std::string("kernel32!wine_get_unix_file_name: ") + (detected ? "present" : "absent"),
        started_at,
        std::chrono::system_clock::now());
}

CheckResult RunWineRegistryKeyCheck()
{
    return RunRegistryKeyExistsCheck("pafish.wine.registry_key", HKEY_CURRENT_USER, "SOFTWARE\\Wine");
}

CheckResult RunQemuCpuNameCheck()
{
    const auto started_at = std::chrono::system_clock::now();
#if defined(_M_IX86) || defined(_M_X64)
    const std::string brand = GetCpuBrandString();
    const bool detected = brand.rfind("QEMU Virtual CPU", 0) == 0;
    return MakeCheckResult(
        "pafish.qemu.cpu_name",
        BoolToStatus(detected),
        "cpu brand string: " + (brand.empty() ? std::string("<empty>") : brand),
        started_at,
        std::chrono::system_clock::now());
#else
    return MakeCheckResult(
        "pafish.qemu.cpu_name",
        ResultStatus::unsupported,
        "cpuid brand string unsupported on this architecture",
        started_at,
        std::chrono::system_clock::now());
#endif
}

CheckResult RunBochsCpuAmd1Check()
{
    const auto started_at = std::chrono::system_clock::now();
#if defined(_M_IX86) || defined(_M_X64)
    const std::string brand = GetCpuBrandString();
    return MakeCheckResult(
        "pafish.bochs.cpu_amd_1",
        BoolToStatus(brand.rfind("AMD Athlon(tm) processor", 0) == 0),
        "cpu brand string: " + (brand.empty() ? std::string("<empty>") : brand),
        started_at,
        std::chrono::system_clock::now());
#else
    return MakeCheckResult(
        "pafish.bochs.cpu_amd_1",
        ResultStatus::unsupported,
        "cpuid brand string unsupported on this architecture",
        started_at,
        std::chrono::system_clock::now());
#endif
}

CheckResult RunBochsCpuAmd2Check()
{
    const auto started_at = std::chrono::system_clock::now();
#if defined(_M_IX86) || defined(_M_X64)
    const std::string vendor = GetCpuVendorString();
    if (!ContainsInsensitive(vendor, "AUTHENTICAMD"))
    {
        return MakeCheckResult(
            "pafish.bochs.cpu_amd_2",
            ResultStatus::not_detected,
            "cpu vendor string: " + (vendor.empty() ? std::string("<empty>") : vendor),
            started_at,
            std::chrono::system_clock::now());
    }

    const CpuidRegisters registers = QueryCpuid(0x8fffffff);
    std::ostringstream evidence;
    evidence << "cpu vendor string: " << vendor << ", cpuid(0x8fffffff).ecx: 0x"
             << std::uppercase << std::hex << registers.ecx;
    return MakeCheckResult(
        "pafish.bochs.cpu_amd_2",
        BoolToStatus(registers.ecx == 0),
        evidence.str(),
        started_at,
        std::chrono::system_clock::now());
#else
    return MakeCheckResult(
        "pafish.bochs.cpu_amd_2",
        ResultStatus::unsupported,
        "cpuid unsupported on this architecture",
        started_at,
        std::chrono::system_clock::now());
#endif
}

CheckResult RunBochsCpuIntel1Check()
{
    const auto started_at = std::chrono::system_clock::now();
#if defined(_M_IX86) || defined(_M_X64)
    const std::string brand = GetCpuBrandString();
    const bool detected = brand == "Intel(R) Pentium(R) 4 CPU" || brand == "Intel(R) Pentium(R) 4 CPU        ";
    return MakeCheckResult(
        "pafish.bochs.cpu_intel_1",
        BoolToStatus(detected),
        "cpu brand string: " + (brand.empty() ? std::string("<empty>") : brand),
        started_at,
        std::chrono::system_clock::now());
#else
    return MakeCheckResult(
        "pafish.bochs.cpu_intel_1",
        ResultStatus::unsupported,
        "cpuid brand string unsupported on this architecture",
        started_at,
        std::chrono::system_clock::now());
#endif
}

CheckResult RunVBoxRegistryKey9Check()
{
    const auto started_at = std::chrono::system_clock::now();
    const std::vector<std::string> keys =
    {
        "SYSTEM\\ControlSet001\\Services\\VBoxGuest",
        "SYSTEM\\ControlSet001\\Services\\VBoxMouse",
        "SYSTEM\\ControlSet001\\Services\\VBoxService",
        "SYSTEM\\ControlSet001\\Services\\VBoxSF",
        "SYSTEM\\ControlSet001\\Services\\VBoxVideo"
    };

    std::vector<std::string> matches;
    for (const std::string& key : keys)
    {
        DWORD error_code = ERROR_SUCCESS;
        if (RegistryKeyExists(HKEY_LOCAL_MACHINE, key, error_code))
        {
            matches.push_back("HKLM\\" + key);
        }
    }

    return MakeCheckResult(
        "pafish.virtualbox.registry_key_9",
        BoolToStatus(!matches.empty()),
        matches.empty() ? "VirtualBox service registry keys not present" : "registry keys present: " + JoinStrings(matches, ", "),
        started_at,
        std::chrono::system_clock::now());
}

CheckResult RunVBoxNetworkShareCheck()
{
    const auto started_at = std::chrono::system_clock::now();
    DWORD provider_size = 4096;
    std::vector<char> provider(provider_size, '\0');
    const DWORD result = WNetGetProviderNameA(WNNC_NET_RDR2SAMPLE, provider.data(), &provider_size);
    if (result == NO_ERROR)
    {
        const std::string provider_name = provider.data();
        return MakeCheckResult(
            "pafish.virtualbox.network_share",
            BoolToStatus(_stricmp(provider_name.c_str(), "VirtualBox Shared Folders") == 0),
            "network provider: " + provider_name,
            started_at,
            std::chrono::system_clock::now());
    }

    if (result == ERROR_NO_NETWORK || result == ERROR_NOT_CONNECTED || result == ERROR_BAD_DEVICE)
    {
        return MakeCheckResult(
            "pafish.virtualbox.network_share",
            ResultStatus::not_detected,
            "VirtualBox shared folders provider not present",
            started_at,
            std::chrono::system_clock::now());
    }

    return MakeCheckResult(
        "pafish.virtualbox.network_share",
        ResultStatus::error,
        "WNetGetProviderNameA failed: " + FormatLastError(result),
        started_at,
        std::chrono::system_clock::now());
}

std::map<std::string, CheckHandler> BuildCheckRegistry()
{
    std::map<std::string, CheckHandler> registry;
    registry.emplace(
        "demo.runner_start",
        []()
        {
            return RunDemoCheck("demo.runner_start", "runner skeleton executed");
        });
    registry.emplace(
        "demo.profile_loaded",
        []()
        {
            return RunDemoCheck("demo.profile_loaded", "profile loaded successfully");
        });
    registry.emplace(
        "env.cpu.logical_processor_count",
        []()
        {
            return RunLogicalProcessorCountCheck();
        });
    registry.emplace(
        "env.memory.total_physical_mb",
        []()
        {
            return RunTotalPhysicalMemoryCheck();
        });
    registry.emplace(
        "env.storage.system_drive_total_gb",
        []()
        {
            return RunSystemDriveTotalSizeCheck();
        });
    registry.emplace("pafish.cpu.hypervisor_bit", []() { return RunHypervisorBitCheck(); });
    registry.emplace("pafish.cpu.known_vm_hypervisor_vendor", []() { return RunKnownHypervisorVendorCheck(); });
    registry.emplace("pafish.gensandbox.username", []() { return RunUsernameCheck(); });
    registry.emplace("pafish.gensandbox.path", []() { return RunPathCheck(); });
    registry.emplace("pafish.gensandbox.common_sample_names", []() { return RunCommonSampleNamesCheck(); });
    registry.emplace("pafish.gensandbox.drive_size_deviceiocontrol", []() { return RunDriveSizeDeviceIoControlCheck(); });
    registry.emplace("pafish.gensandbox.drive_size_getdiskfreespaceex", []() { return RunDriveSizeGetDiskFreeSpaceExCheck(); });
    registry.emplace("pafish.gensandbox.one_cpu_peb", []() { return RunOneCpuPebCheck(); });
    registry.emplace("pafish.gensandbox.one_cpu_getsysteminfo", []() { return RunOneCpuGetSystemInfoCheck(); });
    registry.emplace("pafish.gensandbox.less_than_one_gb", []() { return RunLessThanOneGbCheck(); });
    registry.emplace("pafish.gensandbox.uptime", []() { return RunUptimeCheck(); });
    registry.emplace("pafish.gensandbox.is_native_vhd_boot", []() { return RunIsNativeVhdBootCheck(); });
    registry.emplace("pafish.sandboxie.sbiedll", []() { return RunSandboxieSbieDllCheck(); });
    registry.emplace("pafish.wine.get_unix_file_name", []() { return RunWineGetUnixFileNameCheck(); });
    registry.emplace("pafish.wine.registry_key", []() { return RunWineRegistryKeyCheck(); });
    registry.emplace("pafish.qemu.cpu_name", []() { return RunQemuCpuNameCheck(); });
    registry.emplace(
        "pafish.qemu.registry_key_1",
        []()
        {
            return RunRegistryValueContainsCheck(
                "pafish.qemu.registry_key_1",
                HKEY_LOCAL_MACHINE,
                "HARDWARE\\DEVICEMAP\\Scsi\\Scsi Port 0\\Scsi Bus 0\\Target Id 0\\Logical Unit Id 0",
                "Identifier",
                "QEMU");
        });
    registry.emplace(
        "pafish.qemu.registry_key_2",
        []()
        {
            return RunRegistryValueContainsCheck(
                "pafish.qemu.registry_key_2",
                HKEY_LOCAL_MACHINE,
                "HARDWARE\\Description\\System",
                "SystemBiosVersion",
                "QEMU");
        });
    registry.emplace(
        "pafish.bochs.registry_key_1",
        []()
        {
            return RunRegistryValueContainsCheck(
                "pafish.bochs.registry_key_1",
                HKEY_LOCAL_MACHINE,
                "HARDWARE\\Description\\System",
                "SystemBiosVersion",
                "BOCHS");
        });
    registry.emplace("pafish.bochs.cpu_amd_1", []() { return RunBochsCpuAmd1Check(); });
    registry.emplace("pafish.bochs.cpu_amd_2", []() { return RunBochsCpuAmd2Check(); });
    registry.emplace("pafish.bochs.cpu_intel_1", []() { return RunBochsCpuIntel1Check(); });
    registry.emplace(
        "pafish.virtualbox.registry_key_1",
        []()
        {
            return RunRegistryValueContainsCheck(
                "pafish.virtualbox.registry_key_1",
                HKEY_LOCAL_MACHINE,
                "HARDWARE\\DEVICEMAP\\Scsi\\Scsi Port 0\\Scsi Bus 0\\Target Id 0\\Logical Unit Id 0",
                "Identifier",
                "VBOX");
        });
    registry.emplace(
        "pafish.virtualbox.registry_key_2",
        []()
        {
            return RunRegistryValueContainsCheck(
                "pafish.virtualbox.registry_key_2",
                HKEY_LOCAL_MACHINE,
                "HARDWARE\\Description\\System",
                "SystemBiosVersion",
                "VBOX");
        });
    registry.emplace("pafish.virtualbox.registry_key_3", []() { return RunRegistryKeyExistsCheck("pafish.virtualbox.registry_key_3", HKEY_LOCAL_MACHINE, "SOFTWARE\\Oracle\\VirtualBox Guest Additions"); });
    registry.emplace(
        "pafish.virtualbox.registry_key_4",
        []()
        {
            return RunRegistryValueContainsCheck(
                "pafish.virtualbox.registry_key_4",
                HKEY_LOCAL_MACHINE,
                "HARDWARE\\Description\\System",
                "VideoBiosVersion",
                "VIRTUALBOX");
        });
    registry.emplace("pafish.virtualbox.registry_key_5", []() { return RunRegistryKeyExistsCheck("pafish.virtualbox.registry_key_5", HKEY_LOCAL_MACHINE, "HARDWARE\\ACPI\\DSDT\\VBOX__"); });
    registry.emplace("pafish.virtualbox.registry_key_7", []() { return RunRegistryKeyExistsCheck("pafish.virtualbox.registry_key_7", HKEY_LOCAL_MACHINE, "HARDWARE\\ACPI\\FADT\\VBOX__"); });
    registry.emplace("pafish.virtualbox.registry_key_8", []() { return RunRegistryKeyExistsCheck("pafish.virtualbox.registry_key_8", HKEY_LOCAL_MACHINE, "HARDWARE\\ACPI\\RSDT\\VBOX__"); });
    registry.emplace("pafish.virtualbox.registry_key_9", []() { return RunVBoxRegistryKey9Check(); });
    registry.emplace(
        "pafish.virtualbox.registry_key_10",
        []()
        {
            return RunRegistryValueContainsCheck(
                "pafish.virtualbox.registry_key_10",
                HKEY_LOCAL_MACHINE,
                "HARDWARE\\DESCRIPTION\\System",
                "SystemBiosDate",
                "06/23/99");
        });
    registry.emplace("pafish.virtualbox.file_1", []() { return RunAnyFileCheck("pafish.virtualbox.file_1", { "%WINDIR%\\system32\\drivers\\VBoxMouse.sys", "%WINDIR%\\system32\\drivers\\VBoxGuest.sys", "%WINDIR%\\system32\\drivers\\VBoxSF.sys", "%WINDIR%\\system32\\drivers\\VBoxVideo.sys" }, "VirtualBox file"); });
    registry.emplace("pafish.virtualbox.file_2", []() { return RunAnyFileCheck("pafish.virtualbox.file_2", { "%WINDIR%\\system32\\vboxdisp.dll", "%WINDIR%\\system32\\vboxhook.dll", "%WINDIR%\\system32\\vboxmrxnp.dll", "%WINDIR%\\system32\\vboxogl.dll", "%WINDIR%\\system32\\vboxoglarrayspu.dll", "%WINDIR%\\system32\\vboxoglcrutil.dll", "%WINDIR%\\system32\\vboxoglerrorspu.dll", "%WINDIR%\\system32\\vboxoglfeedbackspu.dll", "%WINDIR%\\system32\\vboxoglpackspu.dll", "%WINDIR%\\system32\\vboxoglpassthroughspu.dll", "%WINDIR%\\system32\\vboxservice.exe", "%WINDIR%\\system32\\vboxtray.exe", "%WINDIR%\\system32\\VBoxControl.exe", "%ProgramFiles%\\Oracle\\VirtualBox Guest Additions", "%ProgramFiles(x86)%\\Oracle\\VirtualBox Guest Additions" }, "VirtualBox file"); });
    registry.emplace("pafish.virtualbox.mac_prefix", []() { return RunMacPrefixCheck("pafish.virtualbox.mac_prefix", { { 0x08, 0x00, 0x27 } }, "VirtualBox"); });
    registry.emplace("pafish.virtualbox.devices", []() { return RunAnyDeviceCheck("pafish.virtualbox.devices", { "\\\\.\\VBoxMiniRdrDN", "\\\\.\\pipe\\VBoxMiniRdDN", "\\\\.\\VBoxTrayIPC", "\\\\.\\pipe\\VBoxTrayIPC" }, "VirtualBox device"); });
    registry.emplace("pafish.virtualbox.tray_window", []() { return RunWindowCheck("pafish.virtualbox.tray_window", { { "VBoxTrayToolWndClass", "" }, { "", "VBoxTrayToolWnd" } }, "VirtualBox tray window"); });
    registry.emplace("pafish.virtualbox.network_share", []() { return RunVBoxNetworkShareCheck(); });
    registry.emplace("pafish.virtualbox.processes", []() { return RunAnyProcessCheck("pafish.virtualbox.processes", { "vboxservice.exe", "vboxtray.exe" }, "VirtualBox process"); });
    registry.emplace("pafish.virtualbox.wmi_devices", []() { return RunWmiContainsCheck("pafish.virtualbox.wmi_devices", L"SELECT DeviceId FROM Win32_PnPEntity", L"DeviceId", { L"PCI\\VEN_80EE&DEV_CAFE" }, "VirtualBox WMI device"); });
    registry.emplace(
        "pafish.vmware.registry_key_1",
        []()
        {
            const auto started_at = std::chrono::system_clock::now();
            const std::vector<std::string> keys =
            {
                "HARDWARE\\DEVICEMAP\\Scsi\\Scsi Port 0\\Scsi Bus 0\\Target Id 0\\Logical Unit Id 0",
                "HARDWARE\\DEVICEMAP\\Scsi\\Scsi Port 1\\Scsi Bus 0\\Target Id 0\\Logical Unit Id 0",
                "HARDWARE\\DEVICEMAP\\Scsi\\Scsi Port 2\\Scsi Bus 0\\Target Id 0\\Logical Unit Id 0"
            };
            for (const std::string& key : keys)
            {
                DWORD error_code = ERROR_SUCCESS;
                const std::optional<std::string> value = ReadRegistryStringValue(HKEY_LOCAL_MACHINE, key, "Identifier", error_code);
                if (value.has_value() && ContainsInsensitive(*value, "VMWARE"))
                {
                    return MakeCheckResult(
                        "pafish.vmware.registry_key_1",
                        ResultStatus::detected,
                        "registry value HKLM\\" + key + " [Identifier]: " + *value,
                        started_at,
                        std::chrono::system_clock::now());
                }
            }

            return MakeCheckResult(
                "pafish.vmware.registry_key_1",
                ResultStatus::not_detected,
                "VMware SCSI registry identifiers not present",
                started_at,
                std::chrono::system_clock::now());
        });
    registry.emplace("pafish.vmware.registry_key_2", []() { return RunRegistryKeyExistsCheck("pafish.vmware.registry_key_2", HKEY_LOCAL_MACHINE, "SOFTWARE\\VMware, Inc.\\VMware Tools"); });
    registry.emplace("pafish.vmware.file_1", []() { return RunAnyFileCheck("pafish.vmware.file_1", { "%WINDIR%\\system32\\drivers\\vmmouse.sys" }, "VMware file"); });
    registry.emplace("pafish.vmware.file_2", []() { return RunAnyFileCheck("pafish.vmware.file_2", { "%WINDIR%\\system32\\drivers\\vmhgfs.sys" }, "VMware file"); });
    registry.emplace("pafish.vmware.mac_prefix", []() { return RunMacPrefixCheck("pafish.vmware.mac_prefix", { { 0x00, 0x05, 0x69 }, { 0x00, 0x0C, 0x29 }, { 0x00, 0x1C, 0x14 }, { 0x00, 0x50, 0x56 } }, "VMware"); });
    registry.emplace("pafish.vmware.adapter_name", []() { return RunAdapterDescriptionCheck("pafish.vmware.adapter_name", "VMware", "VMware"); });
    registry.emplace("pafish.vmware.devices", []() { return RunAnyDeviceCheck("pafish.vmware.devices", { "\\\\.\\HGFS", "\\\\.\\vmci" }, "VMware device"); });
    registry.emplace("pafish.vmware.wmi_serial", []() { return RunWmiContainsCheck("pafish.vmware.wmi_serial", L"SELECT SerialNumber FROM Win32_Bios", L"SerialNumber", { L"VMware" }, "VMware BIOS serial"); });
    return registry;
}

CheckResult MakeUnsupportedResult(const std::string& check_id)
{
    const auto started_at = std::chrono::system_clock::now();
    const auto finished_at = std::chrono::system_clock::now();
    return MakeCheckResult(check_id, ResultStatus::unsupported, "check not registered", started_at, finished_at);
}

void LogToConsole(const CheckResult& result)
{
    std::cout
        << "[" << result.started_at << "] "
        << result.check_id
        << " status=" << StatusToString(result.status)
        << " evidence=\"" << result.evidence << "\""
        << std::endl;
}

bool WriteJsonResults(
    const std::filesystem::path& output_path,
    const std::vector<CheckResult>& results,
    const std::string& generated_at,
    std::string& error_message)
{
    std::error_code create_error;
    std::filesystem::create_directories(output_path.parent_path(), create_error);
    if (create_error)
    {
        error_message = "failed to create log directory: " + create_error.message();
        return false;
    }

    std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
    if (!output.is_open())
    {
        error_message = "failed to open results file for writing";
        return false;
    }

    output << "{\n";
    output << "  \"generated_at\": \"" << JsonEscape(generated_at) << "\",\n";
    output << "  \"results\": [\n";

    for (std::size_t index = 0; index < results.size(); ++index)
    {
        const CheckResult& result = results[index];
        output << "    {\n";
        output << "      \"check_id\": \"" << JsonEscape(result.check_id) << "\",\n";
        output << "      \"status\": \"" << JsonEscape(StatusToString(result.status)) << "\",\n";
        output << "      \"evidence\": \"" << JsonEscape(result.evidence) << "\",\n";
        output << "      \"started_at\": \"" << JsonEscape(result.started_at) << "\",\n";
        output << "      \"finished_at\": \"" << JsonEscape(result.finished_at) << "\"\n";
        output << "    }";
        if (index + 1 < results.size())
        {
            output << ",";
        }
        output << "\n";
    }

    output << "  ]\n";
    output << "}\n";

    if (!output.good())
    {
        error_message = "failed while writing results file";
        return false;
    }

    return true;
}

std::vector<CheckResult> ExecuteRequestedChecks(
    const Profile& profile,
    const std::map<std::string, CheckHandler>& registry)
{
    std::vector<CheckResult> results;
    results.reserve(profile.checks.size());

    for (const std::string& check_id : profile.checks)
    {
        const auto registry_entry = registry.find(check_id);
        if (registry_entry == registry.end())
        {
            results.push_back(MakeUnsupportedResult(check_id));
            continue;
        }

        results.push_back(registry_entry->second());
    }

    return results;
}

std::filesystem::path GetExecutablePath()
{
    char executable_path_buffer[MAX_PATH]{};
    const DWORD path_length = GetModuleFileNameA(nullptr, executable_path_buffer, MAX_PATH);
    if (path_length == 0 || path_length >= MAX_PATH)
    {
        return {};
    }

    return std::filesystem::path(executable_path_buffer);
}

std::filesystem::path ResolveProfilePath(int argc, char* argv[])
{
    for (int index = 1; index < argc; ++index)
    {
        const std::string argument = argv[index];
        if (argument == "--profile")
        {
            if (index + 1 >= argc)
            {
                return {};
            }

            return std::filesystem::path(argv[index + 1]);
        }
    }

    return {};
}
} // namespace

int main(int argc, char* argv[])
{
    const std::filesystem::path profile_path = ResolveProfilePath(argc, argv);
    Profile profile;
    std::string error_message;
    std::string profile_source;
    if (!profile_path.empty())
    {
        if (!LoadProfile(profile_path, profile, error_message))
        {
            std::cerr << "Failed to load profile: " << error_message << std::endl;
            return 1;
        }

        profile_source = profile_path.string();
    }
    else
    {
        if (!LoadEmbeddedProfile(profile, error_message))
        {
            std::cerr << "Failed to load embedded profile: " << error_message << std::endl;
            return 1;
        }

        profile_source = "embedded profile";
    }

    const std::map<std::string, CheckHandler> registry = BuildCheckRegistry();
    const std::vector<CheckResult> results = ExecuteRequestedChecks(profile, registry);
    const std::filesystem::path results_path = std::filesystem::path(profile.output_directory) / "results.json";
    const std::string generated_at = FormatTimestamp(std::chrono::system_clock::now());

    if (profile.console_logging_enabled)
    {
        std::cout << "Loaded profile " << profile.profile_id << " from " << profile_source << std::endl;
        for (const CheckResult& result : results)
        {
            LogToConsole(result);
        }
    }

    if (profile.json_logging_enabled && !WriteJsonResults(results_path, results, generated_at, error_message))
    {
        std::cerr << "Failed to write JSON results: " << error_message << std::endl;
        return 1;
    }

    if (profile.console_logging_enabled && profile.json_logging_enabled)
    {
        std::cout << "Wrote JSON results to " << results_path.string() << std::endl;
    }

    return 0;
}
