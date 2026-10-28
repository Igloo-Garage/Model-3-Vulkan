using System.Globalization;
using System.Text;

namespace Model3VulkanHostCore;

/*
 * Cold-path .ini reader/writer.
 *
 * The parser walks ReadOnlySpan<char> lines and only materialises the keys it
 * needs.  It never throws a format exception to the caller: missing keys use
 * documented defaults and malformed numbers keep the default, which is what
 * the C++ parser has always done for this file.
 */
internal static unsafe class IniConfig
{
    public const uint AbiVersion = 1;

    public static HostCoreConfig Load(string path)
    {
        string text = File.ReadAllText(path, Encoding.UTF8);
        Dictionary<string, string> values = ParseGlobal(text.AsSpan());

        var config = new HostCoreConfig
        {
            StructSize = (uint)sizeof(HostCoreConfig),
            AbiVersion = AbiVersion,
            XResolution = GetInt(values, "XResolution", 496),
            YResolution = GetInt(values, "YResolution", 384),
            Msaa = GetInt(values, "MSAA", 1),
            VSync = GetBool(values, "VSync", true) ? 1 : 0,
            Vrr = GetBool(values, "VRREnabled", GetInt(values, "ExclusiveRefreshRate", 0) > 0) ? 1 : 0,
            CrtShader = GetBool(values, "PostCRT", false) ? 1 : 0,
            MousePollingHz = GetInt(values, "MousePollingHz", 1000),
            MouseFilter = GetBool(values, "MouseFilter", true) ? 1 : 0,
            RecenterKey = GetInt(values, "RecenterKey", 0),
            SteamDeckBackButtons = GetBool(values, "SteamDeckBackButtons", false) ? 1 : 0,
        };

        FixedText.Write(config.CrtShaderName, 64,
            GetString(values, "CrtShaderName", GetString(values, "PostShader", "crt.cfg")));

        return config;
    }

    public static void Save(string path, HostCoreConfig config)
    {
        var builder = new StringBuilder(512);
        builder.AppendLine(";;");
        builder.AppendLine(";; Model3-Vulkan configuration");
        builder.AppendLine(";; Pure Vulkan / SDL3 settings managed by Model3VulkanHostCore.");
        builder.AppendLine(";;");
        builder.AppendLine("[Global]");
        builder.AppendLine();
        builder.Append("XResolution = ").AppendLine(config.XResolution.ToString(CultureInfo.InvariantCulture));
        builder.Append("YResolution = ").AppendLine(config.YResolution.ToString(CultureInfo.InvariantCulture));
        builder.Append("MSAA = ").AppendLine(config.Msaa.ToString(CultureInfo.InvariantCulture));
        builder.Append("VSync = ").AppendLine(config.VSync != 0 ? "1" : "0");
        builder.Append("VRREnabled = ").AppendLine(config.Vrr != 0 ? "1" : "0");
        builder.Append("PostCRT = ").AppendLine(config.CrtShader != 0 ? "1" : "0");
        builder.Append("CrtShaderName = \"").Append(FixedText.Read(config.CrtShaderName, 64)).AppendLine("\"");
        builder.Append("MousePollingHz = ").AppendLine(config.MousePollingHz.ToString(CultureInfo.InvariantCulture));
        builder.Append("MouseFilter = ").AppendLine(config.MouseFilter != 0 ? "1" : "0");
        builder.Append("RecenterKey = ").AppendLine(config.RecenterKey.ToString(CultureInfo.InvariantCulture));
        builder.Append("SteamDeckBackButtons = ").AppendLine(config.SteamDeckBackButtons != 0 ? "1" : "0");

        File.WriteAllText(path, builder.ToString(), new UTF8Encoding(false));
    }

    private static Dictionary<string, string> ParseGlobal(ReadOnlySpan<char> text)
    {
        var values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        bool inGlobal = true;

        foreach (ReadOnlySpan<char> rawLine in text.EnumerateLines())
        {
            ReadOnlySpan<char> line = rawLine.Trim();
            if (line.IsEmpty || line[0] == ';' || line[0] == '#')
                continue;

            if (line[0] == '[')
            {
                int close = line.IndexOf(']');
                if (close > 1)
                {
                    ReadOnlySpan<char> section = line[1..close].Trim();
                    inGlobal = section.Equals("Global".AsSpan(), StringComparison.OrdinalIgnoreCase);
                }
                continue;
            }

            if (!inGlobal)
                continue;

            int equals = line.IndexOf('=');
            if (equals <= 0)
                continue;

            ReadOnlySpan<char> key = line[..equals].Trim();
            ReadOnlySpan<char> value = line[(equals + 1)..].Trim();

            // The C++ writer quotes strings; drop one matching pair.
            if (value.Length >= 2 &&
                ((value[0] == '"' && value[^1] == '"') || (value[0] == '\'' && value[^1] == '\'')))
            {
                value = value[1..^1];
            }

            if (!key.IsEmpty)
                values[key.ToString()] = value.ToString();
        }

        return values;
    }

    private static string GetString(Dictionary<string, string> values, string key, string fallback)
    {
        return values.TryGetValue(key, out string? value) && !string.IsNullOrWhiteSpace(value)
            ? value
            : fallback;
    }

    private static bool GetBool(Dictionary<string, string> values, string key, bool fallback)
    {
        if (!values.TryGetValue(key, out string? value))
            return fallback;

        value = value.Trim();
        if (value.Equals("true", StringComparison.OrdinalIgnoreCase) ||
            value.Equals("yes", StringComparison.OrdinalIgnoreCase) ||
            value.Equals("on", StringComparison.OrdinalIgnoreCase))
            return true;
        if (value.Equals("false", StringComparison.OrdinalIgnoreCase) ||
            value.Equals("no", StringComparison.OrdinalIgnoreCase) ||
            value.Equals("off", StringComparison.OrdinalIgnoreCase))
            return false;

        return int.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out int number)
            ? number != 0
            : fallback;
    }

    private static int GetInt(Dictionary<string, string> values, string key, int fallback)
    {
        return values.TryGetValue(key, out string? value) &&
               int.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out int number)
            ? number
            : fallback;
    }
}
