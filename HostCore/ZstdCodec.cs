using System.IO.Compression;
using ZstdSharp;

namespace Model3VulkanHostCore;

/*
 * Zstandard codec, backed by the pure managed ZstdSharp.Port.
 *
 * The module used to P/Invoke the official libzstd.dll, which meant every
 * package had to carry a platform-specific binary next to Model3VulkanHostCore.dll
 * (libzstd.dll on Windows, libzstd.so on Linux, libzstd.dylib on macOS) and keep
 * its ABI in sync with the .NET module.  The managed port is the same reference
 * implementation translated to C#, so Native AOT folds it into the single
 * Model3VulkanHostCore image and the deployment no longer has a native dependency.
 *
 * The wire format is unchanged: frames produced here are ordinary RFC 8878
 * zstd frames, and frames written by the previous libzstd encoder (and by the
 * even older hand-written RAW/RLE encoder) decompress with the same calls.
 * Existing .zst / .vkstate saves therefore keep loading.
 *
 * Both directions work on spans: the C++ side passes a pinned state buffer and
 * the codec reads or writes it in place.  Save streams the frame into the ZIP
 * entry, load streams the entry into the caller's pre-allocated destination, so
 * a 31 MB state never has to be copied through a second managed array.
 */
internal static class ZstdCodec
{
    // Level 2-3 is the fast end of zstd's range: within a few percent of the
    // default level 3 size at twice the speed on this kind of data.
    private const int Level = 3;

    /*
     * Saves are one complete frame.  Pledging the source size makes the frame
     * header record the content size, exactly like the old one-shot
     * ZSTD_compress call did: external zstd tools can size their output, and
     * the loader has a second source of truth if meta.json lost rawSize.
     *
     * The frame is streamed into the ZIP entry rather than built in a temporary
     * array.  A codec failure still cannot publish a bad save: VkState.Save
     * writes the archive to "<path>.tmp" and deletes it on any exception.
     */
    public static void Compress(ReadOnlySpan<byte> source, Stream destination)
    {
        try
        {
            using var compressor = new Compressor(Level);
            compressor.SetPledgedSrcSize((ulong)source.Length);

            // leaveOpen: the ZipArchiveEntry stream is owned by the caller.
            // Disposing this stream flushes the final block (ZSTD_e_end) and
            // returns its pooled output buffer.
            using var stream = new CompressionStream(destination, compressor, leaveOpen: true);
            stream.Write(source);
        }
        catch (ZstdException ex)
        {
            throw new InvalidDataException($"Zstandard compression failed: {ex.Message}", ex);
        }
    }

    /*
     * Decompresses exactly destination.Length bytes straight into the caller's
     * buffer.  A frame that is shorter or longer than its metadata is a
     * corrupt save, not a partial load: both cases throw so the C++ side keeps
     * its old "nothing was written" behaviour.
     *
     * Reading one extra byte detects a concatenated second frame or trailing
     * garbage that would otherwise be silently ignored.
     */
    public static int Decompress(Stream source, Span<byte> destination)
    {
        try
        {
            using var decompressor = new Decompressor();
            using var stream = new DecompressionStream(source, decompressor, leaveOpen: true);

            int total = 0;
            while (total < destination.Length)
            {
                int read = stream.Read(destination[total..]);
                if (read == 0)
                    break;
                total += read;
            }

            if (total != destination.Length)
            {
                throw new InvalidDataException(
                    $"The zstd frame holds {total} bytes but its metadata says {destination.Length}.");
            }

            Span<byte> extra = stackalloc byte[1];
            if (stream.Read(extra) != 0)
            {
                throw new InvalidDataException(
                    $"The zstd frame holds more than the {destination.Length} bytes its metadata says.");
            }

            return total;
        }
        catch (ZstdException ex)
        {
            throw new InvalidDataException($"Zstandard decompression failed: {ex.Message}", ex);
        }
    }

    /*
     * Fallback for containers whose meta.json predates rawSize (the very first
     * .vkstate builds).  ZSTD_decompressBound walks the frame and returns an
     * upper bound even when the header does not record a content size, so the
     * two-pass C ABI can still allocate a destination.  Unwrap() then reports
     * the exact length, which HostCore_LoadVkState writes back to *written.
     */
    public static ulong GetDecompressedSize(ReadOnlySpan<byte> frame)
    {
        try
        {
            return Decompressor.GetDecompressedSize(frame);
        }
        catch (ZstdException ex)
        {
            throw new InvalidDataException($"The zstd frame has no usable size: {ex.Message}", ex);
        }
    }

    public static int Decompress(ReadOnlySpan<byte> frame, Span<byte> destination)
    {
        try
        {
            using var decompressor = new Decompressor();
            return decompressor.Unwrap(frame, destination);
        }
        catch (ZstdException ex)
        {
            throw new InvalidDataException($"Zstandard decompression failed: {ex.Message}", ex);
        }
    }
}
