namespace SandboxBenchmark.Configurator;

static class Program
{
    /// <summary>
    ///  The main entry point for the application.
    /// </summary>
    [STAThread]
    static void Main()
    {
        // To customize application configuration such as set high DPI settings or default font,
        // see https://aka.ms/applicationconfiguration.
        ApplicationConfiguration.Initialize();
        if (Environment.GetCommandLineArgs().Skip(1).Any(argument => string.Equals(argument, "--build-artifact", StringComparison.Ordinal)))
        {
            try
            {
                Console.WriteLine(MainForm.RunHeadlessBuild());
                return;
            }
            catch (Exception exception)
            {
                Console.Error.WriteLine(exception.Message);
                Environment.ExitCode = 1;
                return;
            }
        }

        Application.Run(new MainForm());
    }    
}
