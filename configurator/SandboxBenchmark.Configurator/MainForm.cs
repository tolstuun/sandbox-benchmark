using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

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
