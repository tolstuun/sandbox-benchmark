#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

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

bool LoadProfile(const std::filesystem::path& profile_path, Profile& profile, std::string& error_message)
{
    const std::string json_text = ReadTextFile(profile_path, error_message);
    if (!error_message.empty())
    {
        return false;
    }

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
} // namespace

int main()
{
    const std::filesystem::path profile_path = std::filesystem::path("profiles") / "default.json";
    Profile profile;
    std::string error_message;
    if (!LoadProfile(profile_path, profile, error_message))
    {
        std::cerr << "Failed to load profile: " << error_message << std::endl;
        return 1;
    }

    const std::map<std::string, CheckHandler> registry = BuildCheckRegistry();
    const std::vector<CheckResult> results = ExecuteRequestedChecks(profile, registry);
    const std::filesystem::path results_path = std::filesystem::path(profile.output_directory) / "results.json";
    const std::string generated_at = FormatTimestamp(std::chrono::system_clock::now());

    if (profile.console_logging_enabled)
    {
        std::cout << "Loaded profile " << profile.profile_id << " from " << profile_path.string() << std::endl;
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
