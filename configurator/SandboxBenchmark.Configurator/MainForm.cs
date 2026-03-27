using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Diagnostics;

namespace SandboxBenchmark.Configurator;

public partial class MainForm : Form
{
    private static readonly string[] KnownChecks =
    [
        "demo.runner_start",
        "demo.profile_loaded",
        "env.cpu.logical_processor_count",
        "env.memory.total_physical_mb",
        "env.storage.system_drive_total_gb"
    ];

    private readonly JsonSerializerOptions _jsonOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.SnakeCaseLower,
        WriteIndented = true
    };

    private string? _profilePath;
    private string? _repoRoot;

    public MainForm()
    {
        InitializeComponent();
        Text = "Sandbox Benchmark Configurator";
        Load += MainForm_Load;
    }

    private void MainForm_Load(object? sender, EventArgs e)
    {
        try
        {
            _profilePath = ResolveProfilePath();
            _repoRoot = Path.GetDirectoryName(Path.GetDirectoryName(_profilePath))!;
            LoadProfileIntoForm(_profilePath);
        }
        catch (Exception exception)
        {
            ShowError($"Failed to load profile: {exception.Message}");
        }
    }

    private void SaveButton_Click(object? sender, EventArgs e)
    {
        if (_profilePath is null)
        {
            ShowError("Profile path is not available.");
            return;
        }

        try
        {
            var profile = BuildProfileFromForm();
            var json = JsonSerializer.Serialize(profile, _jsonOptions);
            File.WriteAllText(_profilePath, json + Environment.NewLine, new UTF8Encoding(false));
            statusLabel.Text = $"Saved {_profilePath}";
        }
        catch (Exception exception)
        {
            ShowError($"Failed to save profile: {exception.Message}");
        }
    }

    private void ReloadButton_Click(object? sender, EventArgs e)
    {
        if (_profilePath is null)
        {
            ShowError("Profile path is not available.");
            return;
        }

        try
        {
            LoadProfileIntoForm(_profilePath);
        }
        catch (Exception exception)
        {
            ShowError($"Failed to reload profile: {exception.Message}");
        }
    }

    private void BuildButton_Click(object? sender, EventArgs e)
    {
        if (_profilePath is null || _repoRoot is null)
        {
            ShowError("Project paths are not available.");
            return;
        }

        try
        {
            var profile = BuildProfileFromForm();
            SaveProfile(profile, _profilePath);

            statusLabel.Text = "Building runner...";
            Application.DoEvents();

            RunBuildCommand("cmake", "-S runner -B runner/build", _repoRoot);
            RunBuildCommand("cmake", "--build runner/build --config Release", _repoRoot);

            var artifactDirectory = CreateArtifact(profile, _profilePath, _repoRoot);
            var successMessage = $"Build succeeded. Artifact created at {artifactDirectory}";
            statusLabel.Text = successMessage;
            MessageBox.Show(this, successMessage, "Sandbox Benchmark Configurator", MessageBoxButtons.OK, MessageBoxIcon.Information);
        }
        catch (Exception exception)
        {
            ShowError($"Build failed: {exception.Message}");
        }
    }

    private void LoadProfileIntoForm(string profilePath)
    {
        var json = File.ReadAllText(profilePath, Encoding.UTF8);
        var profile = JsonSerializer.Deserialize<ProfileDocument>(json);
        if (profile is null)
        {
            throw new InvalidOperationException("Profile JSON could not be parsed.");
        }

        profileIdValueLabel.Text = profile.ProfileId;
        profileNameTextBox.Text = profile.Name;
        versionNumericUpDown.Value = profile.Version;
        outputDirectoryTextBox.Text = profile.OutputDirectory;
        consoleLoggingCheckBox.Checked = profile.ConsoleLoggingEnabled;
        jsonLoggingCheckBox.Checked = profile.JsonLoggingEnabled;

        PopulateChecks(profile.Checks);
        statusLabel.Text = $"Loaded {profilePath}";
    }

    private void PopulateChecks(IReadOnlyCollection<string> selectedChecks)
    {
        checksCheckedListBox.Items.Clear();

        foreach (var checkId in KnownChecks.Concat(selectedChecks).Distinct(StringComparer.Ordinal))
        {
            checksCheckedListBox.Items.Add(checkId, selectedChecks.Contains(checkId));
        }
    }

    private ProfileDocument BuildProfileFromForm()
    {
        var checks = checksCheckedListBox.CheckedItems
            .OfType<string>()
            .ToList();

        return new ProfileDocument
        {
            ProfileId = profileIdValueLabel.Text,
            Name = profileNameTextBox.Text.Trim(),
            Version = Decimal.ToInt32(versionNumericUpDown.Value),
            Checks = checks,
            OutputDirectory = outputDirectoryTextBox.Text.Trim(),
            ConsoleLoggingEnabled = consoleLoggingCheckBox.Checked,
            JsonLoggingEnabled = jsonLoggingCheckBox.Checked
        };
    }

    private void SaveProfile(ProfileDocument profile, string profilePath)
    {
        var json = JsonSerializer.Serialize(profile, _jsonOptions);
        File.WriteAllText(profilePath, json + Environment.NewLine, new UTF8Encoding(false));
        statusLabel.Text = $"Saved {profilePath}";
    }

    private static void RunBuildCommand(string fileName, string arguments, string workingDirectory)
    {
        var startInfo = new ProcessStartInfo
        {
            FileName = fileName,
            Arguments = arguments,
            WorkingDirectory = workingDirectory,
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true
        };

        using var process = Process.Start(startInfo);
        if (process is null)
        {
            throw new InvalidOperationException($"Failed to start process: {fileName}");
        }

        var standardOutput = process.StandardOutput.ReadToEnd();
        var standardError = process.StandardError.ReadToEnd();
        process.WaitForExit();

        if (process.ExitCode != 0)
        {
            var summary = string.Join(
                Environment.NewLine,
                new[]
                {
                    $"Command: {fileName} {arguments}",
                    TrimForDisplay(standardOutput),
                    TrimForDisplay(standardError)
                }.Where(value => !string.IsNullOrWhiteSpace(value)));

            throw new InvalidOperationException(summary);
        }
    }

    private string CreateArtifact(ProfileDocument profile, string profilePath, string repoRoot)
    {
        var buildId = DateTime.Now.ToString("yyyyMMdd-HHmmss");
        var artifactDirectory = Path.Combine(repoRoot, "artifacts", buildId);
        Directory.CreateDirectory(artifactDirectory);

        var sourceRunnerPath = Path.Combine(repoRoot, "runner", "build", "Release", "SandboxBenchmarkRunner.exe");
        var artifactExeName = Path.GetFileName(sourceRunnerPath);
        var artifactRunnerPath = Path.Combine(artifactDirectory, artifactExeName);
        File.Copy(sourceRunnerPath, artifactRunnerPath, overwrite: true);

        var manifest = new ArtifactManifest
        {
            BuildId = buildId,
            CreatedAt = DateTime.Now.ToString("yyyy-MM-ddTHH:mm:ss"),
            ProfilePath = profilePath,
            SelectedChecks = profile.Checks,
            SourceRunnerPath = sourceRunnerPath,
            ArtifactExeName = artifactExeName
        };

        var manifestPath = Path.Combine(artifactDirectory, "manifest.json");
        var manifestJson = JsonSerializer.Serialize(manifest, _jsonOptions);
        File.WriteAllText(manifestPath, manifestJson + Environment.NewLine, new UTF8Encoding(false));

        return artifactDirectory;
    }

    private static string ResolveProfilePath()
    {
        var current = new DirectoryInfo(AppContext.BaseDirectory);
        while (current is not null)
        {
            var candidate = Path.Combine(current.FullName, "profiles", "default.json");
            if (File.Exists(candidate))
            {
                return candidate;
            }

            current = current.Parent;
        }

        throw new FileNotFoundException("Could not find profiles/default.json from the application directory.");
    }

    private static string TrimForDisplay(string value)
    {
        const int maxLength = 1200;
        var trimmed = value.Trim();
        if (trimmed.Length <= maxLength)
        {
            return trimmed;
        }

        return trimmed[..maxLength] + Environment.NewLine + "...";
    }

    private void ShowError(string message)
    {
        statusLabel.Text = message;
        MessageBox.Show(this, message, "Sandbox Benchmark Configurator", MessageBoxButtons.OK, MessageBoxIcon.Error);
    }
}

internal sealed class ProfileDocument
{
    [JsonPropertyName("profile_id")]
    public string ProfileId { get; set; } = "default";

    [JsonPropertyName("name")]
    public string Name { get; set; } = string.Empty;

    [JsonPropertyName("version")]
    public int Version { get; set; }

    [JsonPropertyName("checks")]
    public List<string> Checks { get; set; } = [];

    [JsonPropertyName("output_directory")]
    public string OutputDirectory { get; set; } = "logs";

    [JsonPropertyName("console_logging_enabled")]
    public bool ConsoleLoggingEnabled { get; set; }

    [JsonPropertyName("json_logging_enabled")]
    public bool JsonLoggingEnabled { get; set; }
}

internal sealed class ArtifactManifest
{
    [JsonPropertyName("build_id")]
    public string BuildId { get; set; } = string.Empty;

    [JsonPropertyName("created_at")]
    public string CreatedAt { get; set; } = string.Empty;

    [JsonPropertyName("profile_path")]
    public string ProfilePath { get; set; } = string.Empty;

    [JsonPropertyName("selected_checks")]
    public List<string> SelectedChecks { get; set; } = [];

    [JsonPropertyName("source_runner_path")]
    public string SourceRunnerPath { get; set; } = string.Empty;

    [JsonPropertyName("artifact_exe_name")]
    public string ArtifactExeName { get; set; } = string.Empty;
}
