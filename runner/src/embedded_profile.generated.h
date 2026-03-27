#pragma once

namespace sandbox_benchmark
{
static constexpr const char kEmbeddedProfileJson[] =
  "{\n"
  "  \"profile_id\": \"default\",\n"
  "  \"name\": \"Default Profile\",\n"
  "  \"version\": 1,\n"
  "  \"checks\": [\n"
  "    \"demo.runner_start\",\n"
  "    \"demo.profile_loaded\",\n"
  "    \"env.cpu.logical_processor_count\",\n"
  "    \"env.memory.total_physical_mb\",\n"
  "    \"env.storage.system_drive_total_gb\"\n"
  "  ],\n"
  "  \"output_directory\": \"logs\",\n"
  "  \"console_logging_enabled\": true,\n"
  "  \"json_logging_enabled\": true\n"
  "}\n"
;
} // namespace sandbox_benchmark
