namespace SandboxBenchmark.Configurator;

partial class MainForm
{
    private Label profileIdLabel;
    private Label profileIdValueLabel;
    private Label profileNameLabel;
    private TextBox profileNameTextBox;
    private Label versionLabel;
    private NumericUpDown versionNumericUpDown;
    private Label outputDirectoryLabel;
    private TextBox outputDirectoryTextBox;
    private CheckBox consoleLoggingCheckBox;
    private CheckBox jsonLoggingCheckBox;
    private Label checksLabel;
    private CheckedListBox checksCheckedListBox;
    private Button saveButton;
    private Button reloadButton;
    private Label statusLabel;

    /// <summary>
    ///  Required designer variable.
    /// </summary>
    private System.ComponentModel.IContainer components = null;

    /// <summary>
    ///  Clean up any resources being used.
    /// </summary>
    /// <param name="disposing">true if managed resources should be disposed; otherwise, false.</param>
    protected override void Dispose(bool disposing)
    {
        if (disposing && (components != null))
        {
            components.Dispose();
        }
        base.Dispose(disposing);
    }

    #region Windows Form Designer generated code

    /// <summary>
    ///  Required method for Designer support.
    /// </summary>
    private void InitializeComponent()
    {
        profileIdLabel = new Label();
        profileIdValueLabel = new Label();
        profileNameLabel = new Label();
        profileNameTextBox = new TextBox();
        versionLabel = new Label();
        versionNumericUpDown = new NumericUpDown();
        outputDirectoryLabel = new Label();
        outputDirectoryTextBox = new TextBox();
        consoleLoggingCheckBox = new CheckBox();
        jsonLoggingCheckBox = new CheckBox();
        checksLabel = new Label();
        checksCheckedListBox = new CheckedListBox();
        saveButton = new Button();
        reloadButton = new Button();
        statusLabel = new Label();
        ((System.ComponentModel.ISupportInitialize)versionNumericUpDown).BeginInit();
        SuspendLayout();
        // 
        // profileIdLabel
        // 
        profileIdLabel.AutoSize = true;
        profileIdLabel.Location = new Point(20, 20);
        profileIdLabel.Name = "profileIdLabel";
        profileIdLabel.Size = new Size(57, 15);
        profileIdLabel.TabIndex = 0;
        profileIdLabel.Text = "Profile ID";
        // 
        // profileIdValueLabel
        // 
        profileIdValueLabel.AutoSize = true;
        profileIdValueLabel.Location = new Point(160, 20);
        profileIdValueLabel.Name = "profileIdValueLabel";
        profileIdValueLabel.Size = new Size(45, 15);
        profileIdValueLabel.TabIndex = 1;
        profileIdValueLabel.Text = "default";
        // 
        // profileNameLabel
        // 
        profileNameLabel.AutoSize = true;
        profileNameLabel.Location = new Point(20, 55);
        profileNameLabel.Name = "profileNameLabel";
        profileNameLabel.Size = new Size(76, 15);
        profileNameLabel.TabIndex = 2;
        profileNameLabel.Text = "Profile Name";
        // 
        // profileNameTextBox
        // 
        profileNameTextBox.Location = new Point(160, 52);
        profileNameTextBox.Name = "profileNameTextBox";
        profileNameTextBox.Size = new Size(560, 23);
        profileNameTextBox.TabIndex = 3;
        // 
        // versionLabel
        // 
        versionLabel.AutoSize = true;
        versionLabel.Location = new Point(20, 90);
        versionLabel.Name = "versionLabel";
        versionLabel.Size = new Size(45, 15);
        versionLabel.TabIndex = 4;
        versionLabel.Text = "Version";
        // 
        // versionNumericUpDown
        // 
        versionNumericUpDown.Location = new Point(160, 88);
        versionNumericUpDown.Maximum = new decimal(new int[] { 1000, 0, 0, 0 });
        versionNumericUpDown.Name = "versionNumericUpDown";
        versionNumericUpDown.Size = new Size(120, 23);
        versionNumericUpDown.TabIndex = 5;
        // 
        // outputDirectoryLabel
        // 
        outputDirectoryLabel.AutoSize = true;
        outputDirectoryLabel.Location = new Point(20, 125);
        outputDirectoryLabel.Name = "outputDirectoryLabel";
        outputDirectoryLabel.Size = new Size(95, 15);
        outputDirectoryLabel.TabIndex = 6;
        outputDirectoryLabel.Text = "Output Directory";
        // 
        // outputDirectoryTextBox
        // 
        outputDirectoryTextBox.Location = new Point(160, 122);
        outputDirectoryTextBox.Name = "outputDirectoryTextBox";
        outputDirectoryTextBox.Size = new Size(560, 23);
        outputDirectoryTextBox.TabIndex = 7;
        // 
        // consoleLoggingCheckBox
        // 
        consoleLoggingCheckBox.AutoSize = true;
        consoleLoggingCheckBox.Location = new Point(160, 160);
        consoleLoggingCheckBox.Name = "consoleLoggingCheckBox";
        consoleLoggingCheckBox.Size = new Size(168, 19);
        consoleLoggingCheckBox.TabIndex = 8;
        consoleLoggingCheckBox.Text = "Console Logging Enabled";
        consoleLoggingCheckBox.UseVisualStyleBackColor = true;
        // 
        // jsonLoggingCheckBox
        // 
        jsonLoggingCheckBox.AutoSize = true;
        jsonLoggingCheckBox.Location = new Point(340, 160);
        jsonLoggingCheckBox.Name = "jsonLoggingCheckBox";
        jsonLoggingCheckBox.Size = new Size(146, 19);
        jsonLoggingCheckBox.TabIndex = 9;
        jsonLoggingCheckBox.Text = "JSON Logging Enabled";
        jsonLoggingCheckBox.UseVisualStyleBackColor = true;
        // 
        // checksLabel
        // 
        checksLabel.AutoSize = true;
        checksLabel.Location = new Point(20, 200);
        checksLabel.Name = "checksLabel";
        checksLabel.Size = new Size(45, 15);
        checksLabel.TabIndex = 10;
        checksLabel.Text = "Checks";
        // 
        // checksCheckedListBox
        // 
        checksCheckedListBox.CheckOnClick = true;
        checksCheckedListBox.FormattingEnabled = true;
        checksCheckedListBox.Location = new Point(160, 200);
        checksCheckedListBox.Name = "checksCheckedListBox";
        checksCheckedListBox.Size = new Size(560, 220);
        checksCheckedListBox.TabIndex = 11;
        // 
        // saveButton
        // 
        saveButton.Location = new Point(564, 448);
        saveButton.Name = "saveButton";
        saveButton.Size = new Size(75, 30);
        saveButton.TabIndex = 12;
        saveButton.Text = "Save";
        saveButton.UseVisualStyleBackColor = true;
        saveButton.Click += SaveButton_Click;
        // 
        // reloadButton
        // 
        reloadButton.Location = new Point(645, 448);
        reloadButton.Name = "reloadButton";
        reloadButton.Size = new Size(75, 30);
        reloadButton.TabIndex = 13;
        reloadButton.Text = "Reload";
        reloadButton.UseVisualStyleBackColor = true;
        reloadButton.Click += ReloadButton_Click;
        // 
        // statusLabel
        // 
        statusLabel.AutoSize = true;
        statusLabel.Location = new Point(20, 456);
        statusLabel.Name = "statusLabel";
        statusLabel.Size = new Size(39, 15);
        statusLabel.TabIndex = 14;
        statusLabel.Text = "Ready";
        // 
        // MainForm
        // 
        AutoScaleDimensions = new SizeF(7F, 15F);
        AutoScaleMode = AutoScaleMode.Font;
        ClientSize = new Size(760, 520);
        Controls.Add(statusLabel);
        Controls.Add(reloadButton);
        Controls.Add(saveButton);
        Controls.Add(checksCheckedListBox);
        Controls.Add(checksLabel);
        Controls.Add(jsonLoggingCheckBox);
        Controls.Add(consoleLoggingCheckBox);
        Controls.Add(outputDirectoryTextBox);
        Controls.Add(outputDirectoryLabel);
        Controls.Add(versionNumericUpDown);
        Controls.Add(versionLabel);
        Controls.Add(profileNameTextBox);
        Controls.Add(profileNameLabel);
        Controls.Add(profileIdValueLabel);
        Controls.Add(profileIdLabel);
        FormBorderStyle = FormBorderStyle.FixedDialog;
        MaximizeBox = false;
        MinimizeBox = false;
        Name = "MainForm";
        StartPosition = FormStartPosition.CenterScreen;
        Text = "Sandbox Benchmark Configurator";
        ((System.ComponentModel.ISupportInitialize)versionNumericUpDown).EndInit();
        ResumeLayout(false);
        PerformLayout();
    }

    #endregion
}
