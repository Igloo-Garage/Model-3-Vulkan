using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json.Serialization;

namespace Model3VulkanHostCore;

/*
 * The C ABI contract.  Every field is fixed-size and blittable so that the
 * same bytes mean the same thing in C# and C++.  StructSize is filled by the
 * producer and checked by the consumer; it is the guard against a stale DLL
 * being loaded next to a newer exe.
 */
internal enum HostStatus : int
{
    Ok = 0,
    InvalidArgument = 1,
    IoError = 2,
    FormatError = 3,
    BufferTooSmall = 4,
    Unsupported = 5,
    InternalError = 6,
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct HostCoreConfig
{
    public uint StructSize;
    public uint AbiVersion;

    public int XResolution;
    public int YResolution;
    public int Msaa;
    public int VSync;
    public int Vrr;
    public int CrtShader;
    public int MousePollingHz;
    public int MouseFilter;
    public int RecenterKey;
    public int SteamDeckBackButtons;

    public fixed byte CrtShaderName[64];
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct HostSaveMeta
{
    public uint StructSize;

    public fixed byte GameName[64];
    public fixed byte RomCrc32[16];
    public fixed byte Title[128];
    public fixed byte SavedAt[32];
    public fixed byte Version[32];

    public ulong FileSize;
    public ulong RawSize;
    public ulong ThumbSize;
}

internal static unsafe class FixedText
{
    public static void Write(byte* destination, int capacity, string? value)
    {
        if (destination == null || capacity <= 0)
            return;

        for (int i = 0; i < capacity; i++)
            destination[i] = 0;

        if (string.IsNullOrEmpty(value))
            return;

        // Truncate instead of letting Encoding throw when a field is longer
        // than its fixed buffer (an ISO timestamp with an offset is the usual
        // case).  A truncated display string is harmless; an exception in the
        // C ABI is not.
        byte[] bytes = Encoding.UTF8.GetBytes(value);
        int written = Math.Min(bytes.Length, capacity - 1);
        bytes.AsSpan(0, written).CopyTo(new Span<byte>(destination, written));
        destination[written] = 0;
    }

    public static string Read(byte* source, int capacity)
    {
        if (source == null || capacity <= 0)
            return string.Empty;

        int length = 0;
        while (length < capacity && source[length] != 0)
            length++;

        return Encoding.UTF8.GetString(source, length);
    }
}

/*
 * JSON models.  Only the source-generated context below is used; there is no
 * reflection-based serializer in this assembly.
 */
internal sealed class SaveMetaDocument
{
    [JsonPropertyName("gameName")] public string GameName { get; set; } = "";
    [JsonPropertyName("romCrc32")] public string RomCrc32 { get; set; } = "";
    [JsonPropertyName("title")] public string Title { get; set; } = "";
    [JsonPropertyName("savedAt")] public string SavedAt { get; set; } = "";
    [JsonPropertyName("version")] public string Version { get; set; } = "";
    [JsonPropertyName("rawSize")] public long RawSize { get; set; }
}

[JsonSourceGenerationOptions(
    WriteIndented = true,
    PropertyNamingPolicy = JsonKnownNamingPolicy.CamelCase,
    GenerationMode = JsonSourceGenerationMode.Metadata)]
[JsonSerializable(typeof(SaveMetaDocument))]
internal partial class HostJsonContext : JsonSerializerContext
{
}
