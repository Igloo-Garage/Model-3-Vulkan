using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;

namespace Model3VulkanHostCore;

/*
 * Exported surface.  Every entry point:
 *
 *   - uses the C calling convention,
 *   - takes/returns only blittable values,
 *   - catches every exception and turns it into a status + UTF-8 message.
 *
 * The C++ side never sees a .NET exception.  A missing file, a corrupt ZIP or
 * a bad JSON document is data, not a process-ending event.
 */
internal static unsafe class Exports
{
    public const uint AbiVersion = 1;
    private const uint ListBlobVersion = 1;

    private static readonly IntPtr VersionText =
        Marshal.StringToCoTaskMemUTF8("Model3VulkanHostCore 1.0 (Native AOT)");

    [UnmanagedCallersOnly(EntryPoint = "HostCore_AbiVersion", CallConvs = new[] { typeof(CallConvCdecl) })]
    public static uint GetAbiVersion() => AbiVersion;

    [UnmanagedCallersOnly(EntryPoint = "HostCore_Version", CallConvs = new[] { typeof(CallConvCdecl) })]
    public static byte* GetVersion() => (byte*)VersionText;

    [UnmanagedCallersOnly(EntryPoint = "HostCore_LoadIni", CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int LoadIni(byte* path, HostCoreConfig* output, byte* error, nuint errorCapacity)
    {
        try
        {
            if (path == null || output == null)
                return Fail(error, errorCapacity, HostStatus.InvalidArgument, "path and output must not be null.");

            *output = IniConfig.Load(Utf8(path));
            return (int)HostStatus.Ok;
        }
        catch (Exception ex)
        {
            return Fail(error, errorCapacity, HostStatus.IoError, ex.Message);
        }
    }

    [UnmanagedCallersOnly(EntryPoint = "HostCore_SaveIni", CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int SaveIni(byte* path, HostCoreConfig* config, byte* error, nuint errorCapacity)
    {
        try
        {
            if (path == null || config == null)
                return Fail(error, errorCapacity, HostStatus.InvalidArgument, "path and config must not be null.");

            IniConfig.Save(Utf8(path), *config);
            return (int)HostStatus.Ok;
        }
        catch (Exception ex)
        {
            return Fail(error, errorCapacity, HostStatus.IoError, ex.Message);
        }
    }

    [UnmanagedCallersOnly(EntryPoint = "HostCore_SaveVkState", CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int SaveVkState(
        byte* path,
        HostSaveMeta* meta,
        byte* core,
        nuint coreSize,
        byte* thumbnail,
        nuint thumbnailSize,
        byte* error,
        nuint errorCapacity)
    {
        try
        {
            if (path == null || meta == null)
                return Fail(error, errorCapacity, HostStatus.InvalidArgument, "path and meta must not be null.");
            if (coreSize > int.MaxValue || thumbnailSize > int.MaxValue)
                return Fail(error, errorCapacity, HostStatus.InvalidArgument, "The state or thumbnail is too large.");

            VkState.Save(Utf8(path), meta, Span(core, coreSize), Span(thumbnail, thumbnailSize));
            return (int)HostStatus.Ok;
        }
        catch (Exception ex)
        {
            return Fail(error, errorCapacity, HostStatus.FormatError, ex.Message);
        }
    }

    [UnmanagedCallersOnly(EntryPoint = "HostCore_LoadVkState", CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int LoadVkState(
        byte* path,
        byte* destination,
        nuint destinationCapacity,
        nuint* written,
        HostSaveMeta* metaOut,
        byte* error,
        nuint errorCapacity)
    {
        try
        {
            if (path == null || written == null)
                return Fail(error, errorCapacity, HostStatus.InvalidArgument, "path and written must not be null.");

            var meta = new HostSaveMeta();

            // Probe calls arrive with destination == null.  The destination is
            // the C++ pre-allocated state buffer, so the decompressor writes
            // into it directly instead of returning another managed array.
            int capacity = destinationCapacity > int.MaxValue ? int.MaxValue : (int)destinationCapacity;
            Span<byte> target = destination == null || capacity == 0
                ? Span<byte>.Empty
                : new Span<byte>(destination, capacity);

            if (!VkState.TryLoadCore(Utf8(path), &meta, target, out int required))
            {
                *written = (nuint)required;
                if (metaOut != null)
                    *metaOut = meta;
                return (int)HostStatus.BufferTooSmall;
            }

            *written = (nuint)required;
            if (metaOut != null)
                *metaOut = meta;
            return (int)HostStatus.Ok;
        }
        catch (NotSupportedException ex)
        {
            return Fail(error, errorCapacity, HostStatus.Unsupported, ex.Message);
        }
        catch (Exception ex)
        {
            return Fail(error, errorCapacity, HostStatus.FormatError, ex.Message);
        }
    }

    [UnmanagedCallersOnly(EntryPoint = "HostCore_SniffState", CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int SniffState(byte* path, int* isVkState, byte* error, nuint errorCapacity)
    {
        try
        {
            if (path == null || isVkState == null)
                return Fail(error, errorCapacity, HostStatus.InvalidArgument, "path and isVkState must not be null.");

            *isVkState = VkState.IsZip(Utf8(path)) ? 1 : 0;
            return (int)HostStatus.Ok;
        }
        catch (Exception ex)
        {
            return Fail(error, errorCapacity, HostStatus.IoError, ex.Message);
        }
    }

    [UnmanagedCallersOnly(EntryPoint = "HostCore_RenameVkState", CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int RenameVkState(byte* path, byte* newTitle, byte* error, nuint errorCapacity)
    {
        try
        {
            if (path == null || newTitle == null)
                return Fail(error, errorCapacity, HostStatus.InvalidArgument, "path and newTitle must not be null.");

            VkState.Rename(Utf8(path), Utf8(newTitle));
            return (int)HostStatus.Ok;
        }
        catch (Exception ex)
        {
            return Fail(error, errorCapacity, HostStatus.FormatError, ex.Message);
        }
    }

    [UnmanagedCallersOnly(EntryPoint = "HostCore_ConvertStToVkState", CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int ConvertStToVkState(
        byte* legacyPath, byte* vkStatePath, HostSaveMeta* meta, byte* error, nuint errorCapacity)
    {
        try
        {
            if (legacyPath == null || vkStatePath == null || meta == null)
                return Fail(error, errorCapacity, HostStatus.InvalidArgument, "paths and meta must not be null.");

            VkState.ConvertLegacy(Utf8(legacyPath), Utf8(vkStatePath), meta);
            return (int)HostStatus.Ok;
        }
        catch (Exception ex)
        {
            return Fail(error, errorCapacity, HostStatus.FormatError, ex.Message);
        }
    }

    /*
     * Batch listing.  The blob is:
     *
     *   uint32 version; uint32 count;
     *   repeat count times:
     *     HostSaveMeta meta;
     *     uint32 pathLength;  byte[pathLength] path;
     *     uint32 thumbLength; byte[thumbLength] thumb;
     *
     * No core.zst is opened, so listing a directory never decompresses a save.
     * The first call may pass destination=null to obtain the required size.
     */
    [UnmanagedCallersOnly(EntryPoint = "HostCore_ListVkStates", CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int ListVkStates(
        byte* directory, byte* destination, nuint destinationCapacity, nuint* required,
        byte* error, nuint errorCapacity)
    {
        try
        {
            if (directory == null || required == null)
                return Fail(error, errorCapacity, HostStatus.InvalidArgument, "directory and required must not be null.");

            List<SaveListEntry> entries = VkState.List(Utf8(directory));

            nuint total = 8;
            foreach (SaveListEntry entry in entries)
            {
                total += (nuint)sizeof(HostSaveMeta) + 4 + (nuint)entry.Path.Length + 4 + (nuint)entry.Thumbnail.Length;
            }

            *required = total;
            if (destination == null || destinationCapacity < total)
                return (int)HostStatus.BufferTooSmall;

            byte* cursor = destination;
            Unsafe.WriteUnaligned(cursor, ListBlobVersion);
            cursor += 4;
            Unsafe.WriteUnaligned(cursor, (uint)entries.Count);
            cursor += 4;

            foreach (SaveListEntry entry in entries)
            {
                Unsafe.WriteUnaligned(cursor, entry.Meta);
                cursor += sizeof(HostSaveMeta);

                Unsafe.WriteUnaligned(cursor, (uint)entry.Path.Length);
                cursor += 4;
                entry.Path.AsSpan().CopyTo(new Span<byte>(cursor, entry.Path.Length));
                cursor += entry.Path.Length;

                Unsafe.WriteUnaligned(cursor, (uint)entry.Thumbnail.Length);
                cursor += 4;
                entry.Thumbnail.AsSpan().CopyTo(new Span<byte>(cursor, entry.Thumbnail.Length));
                cursor += entry.Thumbnail.Length;
            }

            return (int)HostStatus.Ok;
        }
        catch (Exception ex)
        {
            return Fail(error, errorCapacity, HostStatus.IoError, ex.Message);
        }
    }

    private static string Utf8(byte* text)
    {
        return text == null ? string.Empty : Marshal.PtrToStringUTF8((IntPtr)text) ?? string.Empty;
    }

    private static ReadOnlySpan<byte> Span(byte* data, nuint length)
    {
        return data == null || length == 0
            ? ReadOnlySpan<byte>.Empty
            : new ReadOnlySpan<byte>(data, checked((int)length));
    }

    private static int Fail(byte* error, nuint capacity, HostStatus status, string message)
    {
        int safeCapacity = capacity > int.MaxValue ? int.MaxValue : (int)capacity;
        FixedText.Write(error, safeCapacity, message);
        return (int)status;
    }
}
