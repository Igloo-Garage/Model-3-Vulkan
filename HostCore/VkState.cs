using System.Buffers;
using System.Globalization;
using System.IO.Compression;
using System.Text.Json;

namespace Model3VulkanHostCore;

internal sealed class SaveListEntry
{
    public required HostSaveMeta Meta;
    public required byte[] Path;
    public required byte[] Thumbnail;
}

/*
 * Save container (the .vkstate format, now named like the classic save slots):
 *
 *   meta.json   source-generated JSON (game, CRC, time, version, user title)
 *   core.zst    valid Zstandard frame with the raw Supermodel save file inside
 *   thumb.png   already-encoded PNG from the emulator (optional)
 *
 * The ZIP itself is stored without compression: core.zst and thumb.png are
 * already compressed, and re-deflating a 30 MB state would only cost time.
 *
 * Naming mirrors the legacy Scheme: "daytona2.st1" (slot 1) becomes
 * "daytona2.zst1" - the same name with one extra letter.  The name is a
 * convention only: LoadCore() sniffs the ZIP magic and reads anything without
 * it as a legacy raw .st.  Containers written by the first builds of this
 * format used a .vkstate suffix and are still listed.
 */
internal static unsafe class VkState
{
    private const uint ZipLocalHeaderMagic = 0x04034b50;

    private static readonly string[] ContainerPatterns = { "*.zst*", "*.vkstate" };

    public static void Save(string path, HostSaveMeta* meta, ReadOnlySpan<byte> core, ReadOnlySpan<byte> thumbnail)
    {
        string? directory = Path.GetDirectoryName(path);
        if (!string.IsNullOrEmpty(directory))
            Directory.CreateDirectory(directory);

        string temporary = path + ".tmp";
        if (File.Exists(temporary))
            File.Delete(temporary);

        SaveMetaDocument document = ToDocument(meta);
        document.RawSize = core.Length;
        document.SavedAt = string.IsNullOrWhiteSpace(document.SavedAt)
            ? DateTimeOffset.UtcNow.ToString("yyyy-MM-dd'T'HH:mm:ss'Z'", CultureInfo.InvariantCulture)
            : document.SavedAt;
        if (string.IsNullOrWhiteSpace(document.Title))
            document.Title = document.GameName + " " + document.SavedAt;

        try
        {
            using (var stream = new FileStream(temporary, FileMode.CreateNew, FileAccess.ReadWrite, FileShare.None))
            using (var archive = new ZipArchive(stream, ZipArchiveMode.Create, leaveOpen: false))
            {
                ZipArchiveEntry metaEntry = archive.CreateEntry("meta.json", CompressionLevel.Optimal);
                using (Stream entryStream = metaEntry.Open())
                {
                    JsonSerializer.Serialize(entryStream, document, HostJsonContext.Default.SaveMetaDocument);
                }

                ZipArchiveEntry coreEntry = archive.CreateEntry("core.zst", CompressionLevel.NoCompression);
                using (Stream entryStream = coreEntry.Open())
                {
                    // The frame goes straight into the ZIP entry. The old code
                    // compressed into a full-size temporary array first, which
                    // doubled peak memory for a 31 MB state; the archive is
                    // written to "<path>.tmp" and discarded on failure, so a
                    // codec error still cannot publish a half-written save.
                    ZstdCodec.Compress(core, entryStream);
                }

                if (!thumbnail.IsEmpty)
                {
                    ZipArchiveEntry thumbEntry = archive.CreateEntry("thumb.png", CompressionLevel.NoCompression);
                    using Stream entryStream = thumbEntry.Open();
                    entryStream.Write(thumbnail);
                }
            }

            File.Move(temporary, path, overwrite: true);
        }
        catch
        {
            try { File.Delete(temporary); } catch { }
            throw;
        }

        // The caller's metadata is completed with what was actually written.
        meta->FileSize = (ulong)new FileInfo(path).Length;
        meta->RawSize = (ulong)core.Length;
        meta->ThumbSize = (ulong)thumbnail.Length;
    }

    /*
     * Loads a state into caller-owned storage and reports how much room it
     * needed.  Returns false when destination is too small; the C ABI's
     * two-pass protocol then allocates `required` bytes and calls again.
     *
     * When meta.json records rawSize the probe pass never decompresses:
     * opening a save costs one ZIP directory read and a small JSON parse, and
     * the second pass decompresses directly into the C++ buffer through
     * DecompressionStream (no intermediate frame or output array).  Only
     * containers that predate the rawSize field fall back to reading core.zst
     * into a pooled buffer so the frame itself can report its bound.
     */
    public static bool TryLoadCore(string path, HostSaveMeta* meta, Span<byte> destination, out int required)
    {
        if (!IsZip(path))
        {
            // Legacy Supermodel .st: the raw block file is already what the
            // C++ deserialiser expects.  Header sniffing happens here so the
            // upper layer never has to know which format it opened.
            using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read);
            long length = stream.Length;
            if (length > int.MaxValue)
                throw new InvalidDataException("The save state is too large for one zstd frame.");

            required = (int)length;
            meta->RawSize = (ulong)length;
            if (destination.Length < required)
                return false;

            if (required > 0)
                stream.ReadExactly(destination[..required]);
            return true;
        }

        using var container = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read);
        using var archive = new ZipArchive(container, ZipArchiveMode.Read);

        ReadMetaInto(archive, meta, path);

        ZipArchiveEntry? coreEntry = archive.GetEntry("core.zst");
        if (coreEntry == null)
            throw new InvalidDataException("The .zst archive has no core.zst entry.");

        using Stream coreStream = coreEntry.Open();

        if (meta->RawSize > 0 && meta->RawSize <= int.MaxValue)
        {
            required = (int)meta->RawSize;
            if (destination.Length < required)
                return false;

            int written = ZstdCodec.Decompress(coreStream, destination[..required]);
            meta->RawSize = (ulong)written;
            return true;
        }

        if (meta->RawSize > int.MaxValue)
            throw new InvalidDataException("The save state is too large for one zstd frame.");

        // No rawSize in meta.json (the first .vkstate builds): the frame header
        // or its block scan is the only source of truth left.
        byte[] frame = RentEntry(coreEntry, coreStream, out int frameLength);
        try
        {
            ulong size = ZstdCodec.GetDecompressedSize(frame.AsSpan(0, frameLength));
            if (size > int.MaxValue)
                throw new InvalidDataException("The save state is too large for one zstd frame.");

            required = (int)size;
            if (destination.Length < required)
                return false;

            int written = ZstdCodec.Decompress(frame.AsSpan(0, frameLength), destination[..required]);
            meta->RawSize = (ulong)written;
            required = written;
            return true;
        }
        finally
        {
            ArrayPool<byte>.Shared.Return(frame);
        }
    }

    public static bool IsZip(string path)
    {
        using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read);
        Span<byte> magic = stackalloc byte[4];
        if (stream.Read(magic) != 4)
            return false;
        return BitConverter.ToUInt32(magic) == ZipLocalHeaderMagic;
    }

    public static void Rename(string path, string newTitle)
    {
        using var stream = new FileStream(path, FileMode.Open, FileAccess.ReadWrite, FileShare.None);
        using var archive = new ZipArchive(stream, ZipArchiveMode.Update);

        ZipArchiveEntry? metaEntry = archive.GetEntry("meta.json");
        if (metaEntry == null)
            throw new InvalidDataException("The .zst archive has no meta.json entry.");

        SaveMetaDocument document;
        using (Stream input = metaEntry.Open())
        {
            document = JsonSerializer.Deserialize(input, HostJsonContext.Default.SaveMetaDocument)
                ?? new SaveMetaDocument();
        }

        document.Title = newTitle;

        metaEntry.Delete();
        ZipArchiveEntry replacement = archive.CreateEntry("meta.json", CompressionLevel.Optimal);
        using Stream output = replacement.Open();
        JsonSerializer.Serialize(output, document, HostJsonContext.Default.SaveMetaDocument);
    }

    public static void ConvertLegacy(string legacyPath, string vkStatePath, HostSaveMeta* meta)
    {
        byte[] raw = File.ReadAllBytes(legacyPath);
        Save(vkStatePath, meta, raw, ReadOnlySpan<byte>.Empty);
    }

    public static List<SaveListEntry> List(string directory)
    {
        var result = new List<SaveListEntry>();
        if (!Directory.Exists(directory))
            return result;

        foreach (string path in EnumerateContainers(directory))
        {
            try
            {
                var meta = new HostSaveMeta();
                byte[] thumbnail;
                using (var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read))
                using (var archive = new ZipArchive(stream, ZipArchiveMode.Read))
                {
                    ReadMetaInto(archive, &meta, path);
                    thumbnail = ReadEntryBytes(archive, "thumb.png");
                }

                var fileInfo = new FileInfo(path);
                meta.FileSize = (ulong)fileInfo.Length;
                meta.ThumbSize = (ulong)thumbnail.Length;
                meta.StructSize = (uint)sizeof(HostSaveMeta);

                result.Add(new SaveListEntry
                {
                    Meta = meta,
                    Path = System.Text.Encoding.UTF8.GetBytes(path),
                    Thumbnail = thumbnail,
                });
            }
            catch
            {
                // A damaged container must not hide the other saves.  The UI
                // simply does not see this one; the log records nothing here
                // because the C ABI has no per-entry result channel.
            }
        }

        result.Sort((a, b) => string.CompareOrdinal(SavedAtOf(b.Meta), SavedAtOf(a.Meta)));
        return result;
    }

    private static string SavedAtOf(HostSaveMeta value)
    {
        HostSaveMeta copy = value;
        return ReadString(&copy, "saved");
    }

    /*
     * "*.zst*" covers the slot naming ("daytona2.zst1") and a bare "*.zst"; "*.vkstate" is what
     * the first builds of this container used.  Both are listed so a folder written by an older
     * build does not look empty, and the loader does not care about either name anyway - it sniffs
     * the file header.
     */
    private static IEnumerable<string> EnumerateContainers(string directory)
    {
        foreach (string pattern in ContainerPatterns)
        {
            foreach (string path in Directory.EnumerateFiles(directory, pattern, SearchOption.TopDirectoryOnly))
                yield return path;
        }
    }

    private static void ReadMetaInto(ZipArchive archive, HostSaveMeta* meta, string path)
    {
        meta->StructSize = (uint)sizeof(HostSaveMeta);
        SetString(meta, "game", ReadGameNameFromPath(path));

        ZipArchiveEntry? metaEntry = archive.GetEntry("meta.json");
        if (metaEntry == null)
            return;

        SaveMetaDocument? document;
        using (Stream input = metaEntry.Open())
        {
            document = JsonSerializer.Deserialize(input, HostJsonContext.Default.SaveMetaDocument);
        }

        if (document == null)
            return;

        SetString(meta, "game", document.GameName);
        SetString(meta, "crc", document.RomCrc32);
        SetString(meta, "title", document.Title);
        SetString(meta, "saved", document.SavedAt);
        SetString(meta, "version", document.Version);
        meta->RawSize = document.RawSize > 0 ? (ulong)document.RawSize : 0;
    }

    private static SaveMetaDocument ToDocument(HostSaveMeta* meta)
    {
        return new SaveMetaDocument
        {
            GameName = ReadString(meta, "game"),
            RomCrc32 = ReadString(meta, "crc"),
            Title = ReadString(meta, "title"),
            SavedAt = ReadString(meta, "saved"),
            Version = ReadString(meta, "version"),
            RawSize = meta->RawSize > long.MaxValue ? 0 : (long)meta->RawSize,
        };
    }

    private static byte[] RentEntry(ZipArchiveEntry entry, Stream stream, out int length)
    {
        long declared = entry.Length;
        if (declared < 0 || declared > int.MaxValue)
            throw new InvalidDataException("The core.zst entry is too large.");

        byte[] buffer = ArrayPool<byte>.Shared.Rent((int)declared);
        try
        {
            int total = 0;
            int expected = (int)declared;
            while (total < expected)
            {
                int read = stream.Read(buffer.AsSpan(total, expected - total));
                if (read == 0)
                    break;
                total += read;
            }

            if (total != expected)
                throw new InvalidDataException("The core.zst entry ended prematurely.");

            length = total;
            return buffer;
        }
        catch
        {
            ArrayPool<byte>.Shared.Return(buffer);
            throw;
        }
    }

    private static byte[] ReadEntryBytes(ZipArchive archive, string name)
    {
        ZipArchiveEntry? entry = archive.GetEntry(name);
        if (entry == null)
            return Array.Empty<byte>();

        using Stream stream = entry.Open();
        using var memory = new MemoryStream(entry.Length <= int.MaxValue ? (int)entry.Length : 0);
        stream.CopyTo(memory);
        return memory.ToArray();
    }

    private static string ReadGameNameFromPath(string path)
    {
        string name = Path.GetFileNameWithoutExtension(path);
        return name.Length > 0 ? name : "unknown";
    }

    /*
     * Fixed-buffer field access.  The field names are a small string switch so
     * the rest of the file stays readable; the compiler resolves each fixed
     * buffer to a byte* in the unsafe context.
     */
    private static void SetString(HostSaveMeta* meta, string field, string? value)
    {
        switch (field)
        {
            case "game": FixedText.Write(meta->GameName, 64, value); break;
            case "crc": FixedText.Write(meta->RomCrc32, 16, value); break;
            case "title": FixedText.Write(meta->Title, 128, value); break;
            case "saved": FixedText.Write(meta->SavedAt, 32, value); break;
            case "version": FixedText.Write(meta->Version, 32, value); break;
        }
    }

    private static string ReadString(HostSaveMeta* meta, string field)
    {
        return field switch
        {
            "game" => FixedText.Read(meta->GameName, 64),
            "crc" => FixedText.Read(meta->RomCrc32, 16),
            "title" => FixedText.Read(meta->Title, 128),
            "saved" => FixedText.Read(meta->SavedAt, 32),
            "version" => FixedText.Read(meta->Version, 32),
            _ => string.Empty,
        };
    }

    private static string ReadMetaString(HostSaveMeta* meta, bool isTitle)
    {
        return isTitle ? ReadString(meta, "title") : ReadString(meta, "saved");
    }
}
