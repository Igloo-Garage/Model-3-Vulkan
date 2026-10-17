using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Linq;

/*
 * SaveStateDump - reads Supermodel save states, visualises what they hold, and compares two of them.
 *
 * Written to answer a question the emulator's own logging could not: when geometry renders black, is
 * the data the renderer reads actually wrong, or is the renderer reading good data wrongly?
 *
 * A save state carries the whole Real3D memory pool (Real3D::SaveState writes MEM_POOL_SIZE_RW), and
 * that pool holds every buffer the renderer samples: polygon RAM (the display list), culling RAM, the
 * texture pages and the texture FIFO. So two states taken at the *same moment of the game* - one while
 * the corruption is on screen, one right after loading an earlier state clears it - differ only by the
 * corruption itself. `--diff` reports that difference per region.
 *
 * What the result means:
 *   the two states differ in poly/texture RAM  -> the emulated memory is being corrupted, and the
 *                                                 buffer that differs names the culprit
 *   the two states are identical               -> the memory is fine and the fault lies in a renderer's
 *                                                 own accumulated state, which a save state resets only
 *                                                 as a side effect of reloading
 *
 * Usage:
 *   SaveStateDump <file.st> [outdir]         visualise and dump regions
 *   SaveStateDump --diff <a.st> <b.st>       compare two states region by region
 */
static class Program
{
    // Real3D.cpp: the layout of the memory pool the save state stores. The order matters: it is the
    // order the offsets were defined in, and MEM_POOL_SIZE_RW is the sum of the read/write regions.
    sealed class Region
    {
        public string Name;
        public int Offset;
        public int Size;
        public override string ToString() => Name;
    }

    static readonly Region[] Regions =
    {
        new Region { Name = "cullingRAMLo",  Offset = 0x0000000, Size = 0x400000 },  // 4 MB
        new Region { Name = "cullingRAMHi",  Offset = 0x0400000, Size = 0x100000 },  // 1 MB
        new Region { Name = "polyRAM",       Offset = 0x0500000, Size = 0x400000 },  // 4 MB
        new Region { Name = "textureRAM",    Offset = 0x0900000, Size = 0x800000 },  // 8 MB
        new Region { Name = "textureFIFO",   Offset = 0x1100000, Size = 0x100000 },  // 1 MB
    };

    const int PAGE_W = 2048;
    const int PAGE_H = 1024;
    const int PAGE_BYTES = PAGE_W * PAGE_H * 2;
    const int TILE = 32;

    static int Main(string[] args)
    {
        if (args.Length == 0)
        {
            Console.Error.WriteLine("usage: SaveStateDump <file.st> [outdir]");
            Console.Error.WriteLine("       SaveStateDump --diff <a.st> <b.st>");
            return 2;
        }

        if (args[0] == "--diff")
        {
            if (args.Length < 3) { Console.Error.WriteLine("--diff needs two files"); return 2; }
            return Diff(args[1], args[2]);
        }

        return Dump(args[0], args.Length > 1 ? args[1] : Directory.GetCurrentDirectory());
    }

    // ------------------------------------------------------------------ shared

    static byte[] ReadReal3D(string path, out int offset)
    {
        var file = File.ReadAllBytes(path);
        var found = FindBlock(file, "Real3D", false);
        if (found == null) throw new Exception("no Real3D block (not a Model 3 save state?)");
        offset = found.Value.Offset;
        return file;
    }

    static (int Offset, int Size)? FindBlock(byte[] file, string wanted, bool verbose)
    {
        int p = 0;
        while (p + 12 <= file.Length)
        {
            uint blockLength = BitConverter.ToUInt32(file, p);
            uint nameLength = BitConverter.ToUInt32(file, p + 4);
            uint commentLength = BitConverter.ToUInt32(file, p + 8);
            if (blockLength < 12 || p + (int)blockLength > file.Length) break;

            string name = System.Text.Encoding.ASCII.GetString(file, p + 12, (int)Math.Max(0, (int)nameLength - 1));
            int dataOff = p + 12 + (int)nameLength + (int)commentLength;
            int dataSize = (int)blockLength - 12 - (int)nameLength - (int)commentLength;
            if (verbose) Console.WriteLine($"  '{name}', {dataSize:N0} bytes");
            if (name == wanted) return (dataOff, dataSize);
            p += (int)blockLength;
        }
        return null;
    }

    // ------------------------------------------------------------------ diff

    /*
     * Compares the renderer-visible memory of two states.
     *
     * The states must be taken at the same instant of gameplay for the result to mean anything - the
     * intended procedure is to save while the corruption is on screen, load an earlier state to clear
     * it, and save again without letting the game advance. Any difference between those two is then
     * attributable to the corruption rather than to the cars having moved.
     */
    static int Diff(string pathA, string pathB)
    {
        byte[] a = ReadReal3D(pathA, out int offA);
        byte[] b = ReadReal3D(pathB, out int offB);

        Console.WriteLine($"A: {pathA}");
        Console.WriteLine($"B: {pathB}");
        Console.WriteLine();
        Console.WriteLine("region         size      bytes differing    first offset   last offset");
        Console.WriteLine("--------------------------------------------------------------------------------");

        int anyDiff = 0;
        foreach (var r in Regions)
        {
            if (offA + r.Offset + r.Size > a.Length || offB + r.Offset + r.Size > b.Length)
            {
                Console.WriteLine($"  {r.Name,-14} (beyond the stored data)");
                continue;
            }

            long differing = 0;
            int first = -1, last = -1;
            for (int i = 0; i < r.Size; i++)
            {
                if (a[offA + r.Offset + i] == b[offB + r.Offset + i]) continue;
                differing++;
                if (first < 0) first = i;
                last = i;
            }

            string verdict = differing == 0 ? "identical" : $"{differing:N0}";
            Console.WriteLine($"  {r.Name,-14} {r.Size / 1024,6} KB  {verdict,16}    {(first < 0 ? "-" : first.ToString("X"))}           {(last < 0 ? "-" : last.ToString("X"))}");
            if (differing > 0) anyDiff++;
        }

        Console.WriteLine();
        if (anyDiff == 0)
        {
            Console.WriteLine("All renderer-visible memory is IDENTICAL between the two states.");
            Console.WriteLine("So the corruption is not in the emulated memory - the renderer is reading good data");
            Console.WriteLine("and displaying it wrongly, which means a rendering-side fault rather than a data fault.");
        }
        else
        {
            Console.WriteLine($"{anyDiff} region(s) differ. The ones listed with differences are where the corruption lives;");
            Console.WriteLine("a wholly different textureRAM points at texture memory, a different polyRAM at the geometry");
            Console.WriteLine("the game built, and so on.");
        }

        // Where the polygon RAM differs most is worth narrowing down, since that buffer holds the
        // display list: a run of differences there is a scene that was built differently, whereas
        // scattered single bytes look more like a stray write.
        var poly = Regions.First(r => r.Name == "polyRAM");
        DumpRuns(a, offA + poly.Offset, b, offB + poly.Offset, poly.Size, poly.Name);

        return anyDiff == 0 ? 0 : 1;
    }

    // Summarises differences in 4 KB pages, which is the granularity the hardware updates memory in.
    static void DumpRuns(byte[] a, int aBase, byte[] b, int bBase, int size, string label)
    {
        const int page = 4096;
        var differing = new List<int>();
        for (int p = 0; p < size; p += page)
        {
            bool any = false;
            for (int i = p; i < Math.Min(p + page, size); i++)
                if (a[aBase + i] != b[bBase + i]) { any = true; break; }
            if (any) differing.Add(p / page);
        }

        Console.WriteLine();
        Console.WriteLine($"  {label}: {differing.Count} of {size / page} 4 KB page(s) differ");
        if (differing.Count > 0 && differing.Count <= 64)
            Console.WriteLine("    pages: " + string.Join(", ", differing));
        else if (differing.Count > 64)
            Console.WriteLine($"    pages (first 64): {string.Join(", ", differing.Take(64))} ...");
    }

    // ------------------------------------------------------------------ dump

    static int Dump(string stPath, string outDir)
    {
        Directory.CreateDirectory(outDir);
        var file = File.ReadAllBytes(stPath);
        Console.WriteLine($"File : {stPath}");
        Console.WriteLine($"Size : {file.Length:N0} bytes");
        Console.WriteLine();
        Console.WriteLine("Blocks:");
        var real3d = FindBlock(file, "Real3D", true);
        if (real3d == null) { Console.Error.WriteLine("No Real3D block - is this a Model 3 save state?"); return 1; }

        var (dataOff, dataSize) = real3d.Value;
        Console.WriteLine();
        Console.WriteLine($"Real3D block data: {dataSize:N0} bytes");

        Console.WriteLine();
        Console.WriteLine("Regions present, and how much of each is non-zero:");
        Console.WriteLine("  (a region that should hold a built scene but is nearly empty is itself a finding)");
        foreach (var r in Regions)
        {
            if (dataOff + r.Offset + r.Size > file.Length) { Console.WriteLine($"  {r.Name,-14} beyond the stored data"); continue; }
            long nonZero = 0;
            for (int i = 0; i < r.Size; i++)
                if (file[dataOff + r.Offset + i] != 0) nonZero++;
            Console.WriteLine($"  {r.Name,-14} {r.Size / 1024,6} KB   {100.0 * nonZero / r.Size,6:F1}% non-zero");
        }

        if (dataOff + Regions[3].Offset + PAGE_BYTES * 2 > file.Length)
        {
            Console.Error.WriteLine("Block is too small to contain texture RAM.");
            return 1;
        }

        var texOff = dataOff + Regions[3].Offset;
        Console.WriteLine();
        Console.WriteLine("Texture RAM tile occupancy (one character per 32x32 tile)");
        Console.WriteLine("  '-' none of the tile's 1024 texels are non-zero");
        Console.WriteLine("  '0'-'9' decile of non-zero texels (9 means 90-100% filled)");
        for (int page = 0; page < 2; page++)
        {
            Console.WriteLine();
            Console.WriteLine($"  page {page}:");
            PrintOccupancy(file, texOff, page);
        }

        Console.WriteLine();
        Console.WriteLine("Writing images...");
        for (int page = 0; page < 2; page++)
        {
            string png = Path.Combine(outDir, $"texram_page{page}.png");
            WritePage(file, texOff, page, png);
            Console.WriteLine($"  {png}");
        }
        string combined = Path.Combine(outDir, "texram_both_pages.png");
        WriteCombined(file, texOff, combined);
        Console.WriteLine($"  {combined}");

        return 0;
    }

    static void PrintOccupancy(byte[] file, int texOff, int page)
    {
        int tilesX = PAGE_W / TILE;
        int tilesY = PAGE_H / TILE;
        int baseOff = texOff + page * PAGE_BYTES;

        for (int ty = 0; ty < tilesY; ty++)
        {
            var line = new System.Text.StringBuilder(tilesX);
            for (int tx = 0; tx < tilesX; tx++)
            {
                int nonZero = 0;
                for (int y = 0; y < TILE; y++)
                {
                    int rowOff = baseOff + ((ty * TILE + y) * PAGE_W + tx * TILE) * 2;
                    for (int x = 0; x < TILE; x++)
                        if (BitConverter.ToUInt16(file, rowOff + x * 2) != 0) nonZero++;
                }
                if (nonZero == 0) line.Append('-');
                else line.Append((char)('0' + Math.Min(9, nonZero * 10 / (TILE * TILE))));
            }
            Console.WriteLine("    " + line);
        }
    }

    static void WritePage(byte[] file, int texOff, int page, string path)
    {
        var bmp = new Bitmap(PAGE_W, PAGE_H, PixelFormat.Format32bppArgb);
        var bd = bmp.LockBits(new Rectangle(0, 0, PAGE_W, PAGE_H), ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
        var buf = new byte[bd.Stride * PAGE_H];
        int baseOff = texOff + page * PAGE_BYTES;

        for (int y = 0; y < PAGE_H; y++)
        {
            int row = y * bd.Stride;
            for (int x = 0; x < PAGE_W; x++)
            {
                ushort v = BitConverter.ToUInt16(file, baseOff + (y * PAGE_W + x) * 2);
                DecodeT1RGB5(v, out byte r, out byte g, out byte b);
                int o = row + x * 4;
                buf[o + 0] = b; buf[o + 1] = g; buf[o + 2] = r; buf[o + 3] = 255;
            }
        }
        System.Runtime.InteropServices.Marshal.Copy(buf, 0, bd.Scan0, buf.Length);
        bmp.UnlockBits(bd);
        bmp.Save(path, ImageFormat.Png);
        bmp.Dispose();
    }

    static void WriteCombined(byte[] file, int texOff, string path)
    {
        int w = PAGE_W, h = PAGE_H * 2;
        var bmp = new Bitmap(w, h, PixelFormat.Format32bppArgb);
        var bd = bmp.LockBits(new Rectangle(0, 0, w, h), ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
        var buf = new byte[bd.Stride * h];

        for (int y = 0; y < h; y++)
        {
            int row = y * bd.Stride;
            for (int x = 0; x < w; x++)
            {
                ushort v = BitConverter.ToUInt16(file, texOff + (y * PAGE_W + x) * 2);
                DecodeT1RGB5(v, out byte r, out byte g, out byte b);
                int o = row + x * 4;
                buf[o + 0] = b; buf[o + 1] = g; buf[o + 2] = r; buf[o + 3] = 255;
            }
        }
        System.Runtime.InteropServices.Marshal.Copy(buf, 0, bd.Scan0, buf.Length);
        bmp.UnlockBits(bd);
        bmp.Save(path, ImageFormat.Png);
        bmp.Dispose();
    }

    // T1RGB5: 1 alpha bit, then 5 bits each of red, green and blue.
    static void DecodeT1RGB5(ushort v, out byte r, out byte g, out byte b)
    {
        r = (byte)(((v >> 10) & 0x1F) * 255 / 31);
        g = (byte)(((v >> 5) & 0x1F) * 255 / 31);
        b = (byte)((v & 0x1F) * 255 / 31);
    }
}
