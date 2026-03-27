#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
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

namespace
{
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

std::string FormatTimestamp(std::chrono::system_clock::time_point time_point)
{
    const std::time_t timestamp = std::chrono::system_clock::to_time_t(time_point);
    std::tm local_time{};
    localtime_s(&local_time, &timestamp);

    std::ostringstream stream;
    stream << std::put_time(&local_time, "%Y-%m-%dT%H:%M:%S");
    return stream.str();
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
} // namespace

int main()
{
    const auto start_time = std::chrono::system_clock::now();

    CheckResult demo_result{
        "demo.runner_start",
        ResultStatus::not_detected,
        "runner skeleton executed",
        FormatTimestamp(start_time),
        FormatTimestamp(std::chrono::system_clock::now())
    };

    const std::vector<CheckResult> results{demo_result};
    const std::filesystem::path results_path = std::filesystem::path("logs") / "results.json";
    const std::string generated_at = FormatTimestamp(std::chrono::system_clock::now());

    LogToConsole(demo_result);

    std::string error_message;
    if (!WriteJsonResults(results_path, results, generated_at, error_message))
    {
        std::cerr << "Failed to write JSON results: " << error_message << std::endl;
        return 1;
    }

    std::cout << "Wrote JSON results to " << results_path.string() << std::endl;
    return 0;
}
