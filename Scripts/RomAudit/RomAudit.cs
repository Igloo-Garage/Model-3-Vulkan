using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Xml;

/*
 * RomAudit - works out which Supermodel games a ROM folder can actually play.
 *
 * ============================================================================================
 * The question this answers, and why it is not the obvious one
 * ============================================================================================
 *
 * The obvious check - "does the zip called X contain the files for set X" - is the wrong question,
 * and answering it produces confident nonsense. Supermodel identifies a game by the CRC-32 of the
 * files inside the zip and ignores the file name entirely; the upstream manual says so outright
 * ("file names are not important"). A zip called scud.zip that holds the Australian revision is
 * recognised as scudau and loads perfectly, while a name-based check would report every one of its
 * files as missing.
 *
 * So the useful question is the reverse: given the zips present, which defined sets can be satisfied?
 * That is what --coverage reports, and it is the answer to "which of these games can I play".
 *
 * ============================================================================================
 * Why this is a C# program rather than a script
 * ============================================================================================
 *
 * A PowerShell version of this existed briefly and was unreliable in ways that produced believable
 * wrong answers - once reporting a complete 39-file ROM set as entirely missing, because a string
 * somewhere in the pipeline had lost its format placeholders and every CRC collapsed onto the same
 * key. A checking tool that can be wrong without saying so is worse than no tool at all, so this one
 * is compiled, typed, and was validated against a case whose correct answer was already known from
 * the emulator's own log before it was ever used to report anything new.
 *
 * ============================================================================================
 * Modes
 * ============================================================================================
 *
 *   --coverage            (default) every defined set, and whether some zip satisfies it
 *   --set <name>          full file-by-file detail for one set
 *   --zip <name>          which sets a particular zip satisfies, and any spare files it holds
 *   --orphans             zips in the folder that satisfy no defined set
 *
 *   --missing-only        with --coverage, list only the sets that cannot be satisfied
 *
 * The exit code is 0 when every defined set can be satisfied, 1 otherwise, so a build or a batch
 * script can act on it.
 */
static class Program
{
    // ---------------------------------------------------------------- model

    sealed class RomFile
    {
        public string Region;
        public long Offset;       // where this file sits inside its region
        public string Crc;        // "0xXXXXXXXX"
        public string Name;
        public bool Optional;
    }

    // A region after parent/child merging: one file per offset.
    sealed class MergedRegion
    {
        public string Name;
        public bool Optional;
        public readonly Dictionary<long, RomFile> ByOffset = new Dictionary<long, RomFile>();
        public IEnumerable<RomFile> Files => ByOffset.Values;
    }

    sealed class Region
    {
        public string Name;
        public bool Optional;
        public readonly List<RomFile> Files = new List<RomFile>();
    }

    sealed class GameDef
    {
        public string Name;
        public string Title;
        public string Version;
        public int Year;
        public string DriveBoard;
        public bool NetBoard;
        public string Parent;
        public readonly List<Region> Regions = new List<Region>();

        public IEnumerable<RomFile> AllFiles => Regions.SelectMany(r => r.Files);
    }

    sealed class ZipInfo
    {
        public string Name;                          // file name without the .zip
        public readonly HashSet<string> Crcs = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        public string Error;                         // set when the archive could not be read
    }

    static Dictionary<string, GameDef> _games;
    static List<ZipInfo> _zips;

    static int Main(string[] args)
    {
        if (args.Length < 2)
        {
            Console.Error.WriteLine("usage: RomAudit <romdir> <games.xml> [--coverage|--set X|--zip X|--orphans] [--missing-only]");
            return 2;
        }

        string romDir = args[0];
        string gamesXml = args[1];
        string mode = "coverage";
        string subject = null;
        bool missingOnly = false;

        for (int i = 2; i < args.Length; i++)
        {
            switch (args[i])
            {
                case "--coverage": mode = "coverage"; break;
                case "--orphans": mode = "orphans"; break;
                case "--driveboard": mode = "driveboard"; break;
                case "--missing-only": missingOnly = true; break;
                case "--set": mode = "set"; subject = Next(args, ref i); break;
                case "--zip": mode = "zip"; subject = Next(args, ref i); break;
                default: Console.Error.WriteLine($"unknown argument: {args[i]}"); return 2;
            }
        }

        if (!Directory.Exists(romDir)) { Console.Error.WriteLine($"ROM directory not found: {romDir}"); return 2; }
        if (!File.Exists(gamesXml)) { Console.Error.WriteLine($"Games.xml not found: {gamesXml}"); return 2; }

        try { _games = LoadGames(gamesXml); }
        catch (Exception e) { Console.Error.WriteLine($"Could not parse {gamesXml}: {e.Message}"); return 2; }

        _zips = LoadZips(romDir);

        switch (mode)
        {
            case "set": return ReportSet(subject);
            case "zip": return ReportZip(subject);
            case "orphans": return ReportOrphans();
            case "driveboard": return ReportDriveBoards();
            default: return ReportCoverage(missingOnly);
        }
    }

    static string Next(string[] a, ref int i)
    {
        if (i + 1 >= a.Length) throw new ArgumentException($"{a[i]} needs a value");
        return a[++i];
    }

    // ---------------------------------------------------------------- inputs

    static Dictionary<string, GameDef> LoadGames(string path)
    {
        var doc = new XmlDocument();
        doc.Load(path);
        var games = new Dictionary<string, GameDef>(StringComparer.OrdinalIgnoreCase);

        var root = doc.SelectSingleNode("games") ?? throw new Exception("no <games> root element");
        foreach (XmlNode node in root.SelectNodes("game"))
        {
            var name = node.Attributes?["name"]?.Value;
            if (string.IsNullOrEmpty(name)) continue;

            var g = new GameDef
            {
                Name = name,
                Title = node.SelectSingleNode("identity/title")?.InnerText?.Trim() ?? "",
                Version = node.SelectSingleNode("identity/version")?.InnerText?.Trim() ?? "",
                Year = int.TryParse(node.SelectSingleNode("identity/year")?.InnerText?.Trim(), out int y) ? y : 0,
                DriveBoard = node.SelectSingleNode("hardware/drive_board")?.InnerText?.Trim() ?? "",
                NetBoard = node.SelectSingleNode("hardware/netboard")?.InnerText?.Trim() == "true",
                Parent = node.Attributes?["parent"]?.Value,
            };

            foreach (XmlNode regionNode in node.SelectNodes("roms/region"))
            {
                // Required unless explicitly marked otherwise: only an explicit required="false" makes
                // a region optional, and the difference decides whether a miss is fatal or merely
                // silent - which is the distinction this whole tool exists to make.
                var region = new Region
                {
                    Name = regionNode.Attributes?["name"]?.Value ?? "",
                    Optional = regionNode.Attributes?["required"]?.Value == "false",
                };
                foreach (XmlNode file in regionNode.SelectNodes("file"))
                {
                    region.Files.Add(new RomFile
                    {
                        Region = region.Name,
                        Offset = ParseOffset(file.Attributes?["offset"]?.Value),
                        Crc = NormaliseCrc(file.Attributes?["crc32"]?.Value),
                        Name = file.Attributes?["name"]?.Value ?? "",
                        Optional = region.Optional,
                    });
                }
                g.Regions.Add(region);
            }
            games[name] = g;
        }
        return games;
    }

    static string NormaliseCrc(string raw)
    {
        if (string.IsNullOrEmpty(raw)) return "";
        raw = raw.Trim();
        if (raw.StartsWith("0x", StringComparison.OrdinalIgnoreCase)) raw = raw.Substring(2);
        return uint.TryParse(raw, System.Globalization.NumberStyles.HexNumber, null, out uint v)
            ? "0x" + v.ToString("X8")
            : raw.ToUpperInvariant();
    }

    static long ParseOffset(string raw)
    {
        if (string.IsNullOrEmpty(raw)) return 0;
        raw = raw.Trim();
        if (raw.StartsWith("0x", StringComparison.OrdinalIgnoreCase))
            return long.TryParse(raw.Substring(2), System.Globalization.NumberStyles.HexNumber, null, out long h) ? h : 0;
        return long.TryParse(raw, out long d) ? d : 0;
    }

    static List<ZipInfo> LoadZips(string romDir)
    {
        var list = new List<ZipInfo>();
        foreach (var path in Directory.GetFiles(romDir, "*.zip").OrderBy(p => p, StringComparer.OrdinalIgnoreCase))
        {
            var info = new ZipInfo { Name = Path.GetFileNameWithoutExtension(path) };
            try { ReadCentralDirectory(path, info.Crcs); }
            catch (Exception e) { info.Error = e.Message; }
            list.Add(info);
        }
        return list;
    }

    /*
     * Reads the CRC-32 of every entry from the zip's central directory.
     *
     * Not System.IO.Compression: ZipArchiveEntry.Crc32 only exists on .NET 6 and later, and this has
     * to build with the framework compiler that ships alongside the Visual Studio installation the
     * project already uses. Parsing the directory is portable and cheap - the CRCs are stored
     * verbatim, so nothing is decompressed.
     *
     * It is also the value the emulator sees: unzGetCurrentFileInfo() reports the central directory
     * CRC, so a check based on it agrees exactly with what the loader matches on.
     */
    static void ReadCentralDirectory(string path, HashSet<string> crcs)
    {
        var bytes = File.ReadAllBytes(path);
        if (bytes.Length < 22) throw new InvalidDataException("too small to be a zip");

        const uint EOCD = 0x06054b50;
        int eocd = -1;
        int lowest = Math.Max(0, bytes.Length - 22 - 0xFFFF);
        for (int i = bytes.Length - 22; i >= lowest; i--)
            if (BitConverter.ToUInt32(bytes, i) == EOCD) { eocd = i; break; }
        if (eocd < 0) throw new InvalidDataException("no end-of-central-directory record");

        ushort count = BitConverter.ToUInt16(bytes, eocd + 10);
        int p = checked((int)BitConverter.ToUInt32(bytes, eocd + 16));

        const uint CEN = 0x02014b50;
        for (int n = 0; n < count; n++)
        {
            if (p + 46 > bytes.Length) throw new InvalidDataException("central directory truncated");
            if (BitConverter.ToUInt32(bytes, p) != CEN) throw new InvalidDataException("bad central directory entry");

            uint crc = BitConverter.ToUInt32(bytes, p + 16);
            int nameLen = BitConverter.ToUInt16(bytes, p + 28);
            int extraLen = BitConverter.ToUInt16(bytes, p + 30);
            int commentLen = BitConverter.ToUInt16(bytes, p + 32);

            if (crc != 0) crcs.Add("0x" + crc.ToString("X8"));   // directories are stored with CRC 0
            p += 46 + nameLen + extraLen + commentLen;
        }
    }

    // ---------------------------------------------------------------- matching

    /*
     * Merged region map: the parent's regions are copied first, then any file the child defines at
     * the same offset replaces the parent's, and a file at an offset the parent did not have is
     * appended.
     *
     * Lifted directly from GameLoader::MergeChildrenWithParents, which does exactly this:
     *
     *     // Rebuild child regions by copying over all parent regions first, then
     *     // merge in files from equivalent child regions
     *     if (new_region->FindFileIndexByOffset(&idx, child_region->files[i]->offset))
     *         new_region->files[idx] = child_region->files[i];
     *     else
     *         new_region->files.push_back(child_region->files[i]);
     *
     * The distinction from a wholesale per-region replacement matters only when a child declares
     * fewer offsets than its parent - and then it matters a lot. mgtrkbad declares just two of crom's
     * four chips and relies on the parent supplying the other two, saying so in a comment ("same data
     * as EPR-21436 in MAGTRUCK"). Replacing the region outright made those two files look unused, so
     * the tool listed them as spare and would have called the set complete even with them absent.
     */
    static Dictionary<string, MergedRegion> MergedRegions(GameDef g)
    {
        // Parent first. The loader forbids more than one level of parents, but walking generally
        // costs nothing and degrades gracefully if that rule is ever relaxed.
        var chain = new List<GameDef>();
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        for (var cur = g; cur != null && seen.Add(cur.Name); cur = ParentOf(cur))
            chain.Add(cur);
        chain.Reverse();

        var map = new Dictionary<string, MergedRegion>(StringComparer.OrdinalIgnoreCase);
        foreach (var def in chain)
        {
            foreach (var region in def.Regions)
            {
                // Region attributes come from the first definition, which is the parent's if there is
                // one, matching the loader's copy-parent-then-patch order.
                if (!map.TryGetValue(region.Name, out var merged))
                    map[region.Name] = merged = new MergedRegion { Name = region.Name, Optional = region.Optional };

                foreach (var f in region.Files)
                    merged.ByOffset[f.Offset] = f;
            }
        }
        return map;
    }

    // Everything the set needs after merging with its ancestors.
    static List<RomFile> MergedFiles(GameDef g) =>
        MergedRegions(g).Values.SelectMany(r => r.Files).ToList();

    // Only the files the set declares itself, before any merging. Used to decide whether a zip
    // "identifies" a child set, which is how the loader decides to go looking for a parent zip.
    static List<RomFile> OwnFiles(GameDef g) => g.Regions.SelectMany(r => r.Files).ToList();

    static GameDef ParentOf(GameDef g) =>
        (!string.IsNullOrEmpty(g.Parent) && _games.TryGetValue(g.Parent, out var p)) ? p : null;

    /*
     * Whether a single zip can satisfy a set on its own.
     *
     * Only the required files are considered. Optional regions cannot be used to identify a game -
     * that is how the emulator chooses between overlapping sets - and a missing optional file is a
     * degraded feature rather than a failure to load.
     *
     * For a set that has a parent this can only ever be a partial answer, because the emulator will
     * also fetch <parent>.zip by name. That case is handled by SattisfiedByFolder, which is what the
     * coverage report uses; this is kept separate so the two questions stay distinguishable.
     */
    static bool SatisfiedByZip(GameDef g, ZipInfo zip, out int missing)
    {
        missing = 0;
        foreach (var f in MergedFiles(g))
            if (!f.Optional && !zip.Crcs.Contains(f.Crc)) missing++;
        return missing == 0;
    }

    /*
     * Whether the folder can satisfy a set, mirroring how the emulator picks a game.
     *
     * Two routes, in the precedence the loader uses (GameLoader::ChooseGameInZipArchive):
     *
     *   A. One archive holds the complete merged set. "Complete merged games take highest precedence."
     *
     *   B. The archive identifies a child set - at least one of the child's own files is in it - and
     *      the loader then opens <parent>.zip by name and searches the two archives together. The
     *      name matters here and only here: the parent is requested by name, not found by content.
     *
     * Verified against the emulator on every set whose verdict changed while this was being written.
     * An earlier version unioned the parent's files into every candidate without requiring the zip to
     * identify the child at all, and so reported that one 35-file archive satisfied eight unrelated
     * games — the parent archives were quietly supplying everything.
     */
    static bool SatisfiedByFolder(GameDef g, out ZipInfo via, out int missing)
    {
        via = null;
        var parent = ParentOf(g);
        var required = MergedFiles(g).Where(f => !f.Optional).ToList();

        // Route A: the whole merged set inside a single archive.
        foreach (var zip in _zips)
        {
            if (zip.Error != null) continue;
            if (required.All(f => zip.Crcs.Contains(f.Crc))) { via = zip; missing = 0; return true; }
        }

        // Route B: identified by its own files, completed from the parent archive.
        if (parent != null)
        {
            var parentZip = _zips.FirstOrDefault(z => z.Error == null &&
                string.Equals(z.Name, parent.Name, StringComparison.OrdinalIgnoreCase));
            if (parentZip != null)
            {
                var own = OwnFiles(g);
                foreach (var zip in _zips)
                {
                    if (zip.Error != null) continue;
                    if (!own.Any(f => zip.Crcs.Contains(f.Crc))) continue;
                    if (required.All(f => zip.Crcs.Contains(f.Crc) || parentZip.Crcs.Contains(f.Crc)))
                    {
                        via = zip;
                        missing = 0;
                        return true;
                    }
                }
            }
        }

        // How far short the closest single archive fell, which separates "one file short" from "absent".
        missing = int.MaxValue;
        foreach (var zip in _zips)
        {
            if (zip.Error != null) continue;
            int miss = required.Count(f => !zip.Crcs.Contains(f.Crc));
            if (miss < missing) { missing = miss; via = zip; }
        }
        if (missing == int.MaxValue) missing = required.Count;
        return false;
    }

    // ---------------------------------------------------------------- reports

    /*
     * Coverage, grouped by game rather than by set.
     *
     * The per-set question ("is set X satisfiable?") is not the one anyone asks. Sets are revisions,
     * several revisions usually share a title, and the emulator picks whichever revision the files in
     * the folder add up to. So a folder that cannot supply scudplus exactly may still play "Scud Race
     * Plus" perfectly well through scudplusa, and reporting the set as unplayable would be true and
     * useless at the same time.
     *
     * Verified against the emulator rather than assumed: scudplus, harley and vf3 were all reported
     * unplayable by the per-set version of this check and all three start normally, loading a
     * neighbouring revision.
     */
    static int ReportCoverage(bool missingOnly)
    {
        Console.WriteLine("================ COVERAGE ================");
        Console.WriteLine($"  sets defined : {_games.Count}");
        Console.WriteLine($"  zips found   : {_zips.Count}   ({_zips.Sum(z => (long)z.Crcs.Count)} files with a CRC in total)");
        Console.WriteLine();
        Console.WriteLine("  A game is playable when some zip holds every file that one of its revisions needs.");
        Console.WriteLine("  Zip names do not have to match: the emulator identifies games by content.");
        Console.WriteLine();

        var groups = _games.Values
            .GroupBy(g => g.Title, StringComparer.OrdinalIgnoreCase)
            .OrderBy(grp => grp.First().Year).ThenBy(grp => grp.Key, StringComparer.OrdinalIgnoreCase)
            .ToList();

        Console.WriteLine("  year  game                                    playable via");
        Console.WriteLine("  ------------------------------------------------------------------------------------");

        int playable = 0;
        var blocked = new List<string>();

        foreach (var grp in groups)
        {
            var working = new List<(GameDef Set, ZipInfo Via)>();
            foreach (var g in grp.OrderBy(g => g.Name, StringComparer.OrdinalIgnoreCase))
                if (SatisfiedByFolder(g, out var foundIn, out _)) working.Add((g, foundIn));

            bool ok = working.Count > 0;
            if (ok) playable++; else blocked.Add(grp.Key);
            if (missingOnly && ok) continue;

            string viaText = ok
                ? string.Join(", ", working.Select(w => w.Set.Name + " (" + w.Via.Name + ".zip)"))
                : "NONE - no revision is complete";
            Console.WriteLine($"  {grp.First().Year,-5} {Truncate(grp.Key, 39),-39} {viaText}");
        }

        Console.WriteLine();
        Console.WriteLine($"  playable : {playable} / {groups.Count} games");
        if (blocked.Count > 0)
        {
            Console.WriteLine($"  blocked  : {blocked.Count}");
            Console.WriteLine();
            foreach (var b in blocked) Console.WriteLine($"      {b}");
        }
        Console.WriteLine();
        return blocked.Count == 0 ? 0 : 1;
    }

    static int ReportSet(string set)
    {
        if (!_games.TryGetValue(set, out var g))
        {
            Console.Error.WriteLine($"'{set}' is not defined in Games.xml.");
            return 2;
        }

        var merged = MergedFiles(g);
        bool ok = SatisfiedByFolder(g, out var via, out int missing);
        var parent = ParentOf(g);

        Console.WriteLine($"================ {g.Name} ================");
        Console.WriteLine($"  Title       : {g.Title}{(string.IsNullOrEmpty(g.Version) ? "" : " (" + g.Version + ")")}");
        Console.WriteLine($"  Year        : {g.Year}");
        Console.WriteLine($"  Parent      : {(parent == null ? "(none)" : parent.Name)}");
        Console.WriteLine($"  Drive board : {(string.IsNullOrEmpty(g.DriveBoard) ? "(none declared)" : g.DriveBoard)}");
        Console.WriteLine($"  Net board   : {(g.NetBoard ? "yes" : "no")}");
        Console.WriteLine();
        Console.WriteLine(ok
            ? $"  PLAYABLE - every required file is present (found in {via.Name}.zip)"
            : $"  NOT PLAYABLE - {missing} required file(s) absent from the whole folder");
        Console.WriteLine();

        // Which zip each file was found in, searching the child first and then the parent by name.
        var parentZip = parent == null ? null : _zips.FirstOrDefault(z => string.Equals(z.Name, parent.Name, StringComparison.OrdinalIgnoreCase));

        Console.WriteLine("  region                 file                       found in");
        Console.WriteLine("  ------------------------------------------------------------------------");
        int missReq = 0, missOpt = 0;
        foreach (var f in merged.OrderBy(f => f.Region).ThenBy(f => f.Crc))
        {
            string where = null;
            foreach (var z in _zips)
            {
                if (z.Error == null && z.Crcs.Contains(f.Crc)) { where = z.Name + ".zip"; break; }
            }
            string note = where != null ? where : (f.Optional ? "MISSING (optional)" : "MISSING (REQUIRED)");
            if (where == null) { if (f.Optional) missOpt++; else missReq++; }
            Console.WriteLine($"  {f.Region,-22} {f.Name,-26} {note}");
        }

        Console.WriteLine();
        Console.WriteLine($"  files required           : {merged.Count(f => !f.Optional)}");
        Console.WriteLine($"  missing required         : {missReq}");
        Console.WriteLine($"  optional files declared  : {merged.Count(f => f.Optional)}");
        Console.WriteLine($"  missing optional         : {missOpt}");
        if (parentZip == null && parent != null)
            Console.WriteLine($"  note: parent {parent.Name}.zip is NOT present, so this child set cannot load merged");

        if (missOpt > 0 && !string.IsNullOrEmpty(g.DriveBoard))
        {
            Console.WriteLine();
            Console.WriteLine("  NOTE: an optional file is missing and this game declares a drive board. If the missing");
            Console.WriteLine("        file is the drive board program, the board is replaced by an unconnected stub");
            Console.WriteLine("        and the game will most likely stop at its hardware self-test.");
        }
        Console.WriteLine();
        return ok ? 0 : 1;
    }

    static int ReportZip(string name)
    {
        var zip = _zips.FirstOrDefault(z => string.Equals(z.Name, name, StringComparison.OrdinalIgnoreCase));
        if (zip == null) { Console.Error.WriteLine($"no {name}.zip in the folder"); return 2; }
        if (zip.Error != null) { Console.Error.WriteLine($"{name}.zip could not be read: {zip.Error}"); return 2; }

        Console.WriteLine($"================ {zip.Name}.zip ================");
        Console.WriteLine($"  {zip.Crcs.Count} file(s) with a CRC");
        Console.WriteLine();
        Console.WriteLine("  sets this zip satisfies on its own:");
        var fits = _games.Values.Where(g => SatisfiedByZip(g, zip, out _)).OrderBy(g => g.Title).ToList();
        if (fits.Count == 0) Console.WriteLine("    (none - the files do not add up to any defined set)");
        foreach (var g in fits) Console.WriteLine($"    {g.Name,-10} {g.Title} ({g.Version})");
        Console.WriteLine();

        // Files the zip holds that no satisfied set needs: the tell-tale of a wrong revision.
        var needed = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var g in fits) foreach (var f in MergedFiles(g)) needed.Add(f.Crc);
        var spare = zip.Crcs.Where(c => !needed.Contains(c)).ToList();
        Console.WriteLine($"  spare files not used by the matched set(s): {spare.Count}");
        if (spare.Count > 0 && fits.Count > 0)
            Console.WriteLine("    (this is normal when a zip contains a revision the folder also needs,");
        Console.WriteLine("     and suspicious when it is empty of matches)");
        Console.WriteLine();
        return 0;
    }

    static int ReportOrphans()
    {
        /*
         * Which archive the folder audit credits for each playable set.
         *
         * A zip that completes a child set together with its parent is doing real work and is not an
         * orphan, even though on its own it holds only part of the merged game. Testing only the
         * single-archive route reported those zips as unused - von2o.zip among them - which is the
         * opposite of the truth: delete it and the set it identifies stops being playable.
         */
        var credited = new Dictionary<string, List<string>>(StringComparer.OrdinalIgnoreCase);
        foreach (var g in _games.Values)
        {
            if (!SatisfiedByFolder(g, out var via, out _) || via == null) continue;
            if (!credited.TryGetValue(via.Name, out var list)) credited[via.Name] = list = new List<string>();
            list.Add(g.Name);
        }

        Console.WriteLine("================ ZIPS MATCHING NO DEFINED SET ================");
        Console.WriteLine();
        int orphans = 0;
        foreach (var zip in _zips)
        {
            if (zip.Error != null) { Console.WriteLine($"  {zip.Name,-12} unreadable: {zip.Error}"); orphans++; continue; }
            if (credited.ContainsKey(zip.Name)) continue;

            orphans++;
            // Say how close it came, which separates "wrong game" from "one file short".
            int best = int.MaxValue;
            string bestSet = "";
            foreach (var g in _games.Values)
            {
                int miss = MergedFiles(g).Count(f => !f.Optional && !zip.Crcs.Contains(f.Crc));
                if (miss < best) { best = miss; bestSet = g.Name; }
            }
            Console.WriteLine($"  {zip.Name,-12} closest set: {bestSet} ({best} file(s) short)");
        }
        if (orphans == 0) Console.WriteLine("  (none - every zip contributes to a defined set)");
        Console.WriteLine();
        return orphans == 0 ? 0 : 1;
    }

    /*
     * Sets that declare a drive board but whose drive board ROM is nowhere in the folder.
     *
     * This is worth its own report because it is the one failure that leaves no trace. The region is
     * optional, so the loader drops it without complaint, substitutes an unconnected board, and the
     * game then stops at its own hardware self-test - a hang that looks like an emulator bug and has
     * nothing in the log to explain it. Naming the sets turns that into an obvious missing file.
     */
    static int ReportDriveBoards()
    {
        var anyZip = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var z in _zips) if (z.Error == null) anyZip.UnionWith(z.Crcs);

        var declared = _games.Values
            .Where(g => !string.IsNullOrEmpty(g.DriveBoard))
            .OrderBy(g => g.Title, StringComparer.OrdinalIgnoreCase).ThenBy(g => g.Name, StringComparer.OrdinalIgnoreCase)
            .ToList();

        Console.WriteLine("================ DRIVE BOARDS ================");
        Console.WriteLine($"  {declared.Count} of {_games.Count} sets declare a drive board.");
        Console.WriteLine();
        Console.WriteLine("  set        title                                  drive board    rom");
        Console.WriteLine("  ------------------------------------------------------------------------------------");

        int missing = 0;
        int noRom = 0;
        var affected = new List<string>();
        foreach (var g in declared)
        {
            // The drive board region comes from wherever the merged set takes it from.
            var merged = MergedRegions(g);
            var files = merged.TryGetValue("driveboard_program", out var board) ? board.Files.ToList() : new List<RomFile>();

            // Some boards genuinely have no ROM - the Ski board is logic with nothing to load - and
            // reporting those as missing would bury the boards that really are short a file.
            if (files.Count == 0)
            {
                noRom++;
                Console.WriteLine($"  {g.Name,-10} {Truncate(g.Title, 38),-38} {Truncate(g.DriveBoard, 14),-14} n/a (board has no ROM)");
                continue;
            }

            // A merged child often inherits the parent's board, so accept a hit from any zip.
            bool present = files.All(f => anyZip.Contains(f.Crc));
            if (!present) { missing++; affected.Add($"{g.Name} ({g.Title} / {g.Version})"); }

            Console.WriteLine($"  {g.Name,-10} {Truncate(g.Title, 38),-38} {Truncate(g.DriveBoard, 14),-14} {(present ? "OK" : "MISSING " + files[0].Name)}");
        }

        Console.WriteLine();
        Console.WriteLine($"  drive boards with a ROM present     : {declared.Count - missing - noRom}");
        Console.WriteLine($"  boards that need no ROM             : {noRom}");
        Console.WriteLine($"  drive boards falling back to a stub : {missing}");
        if (missing > 0)
        {
            Console.WriteLine();
            Console.WriteLine("  These games will most likely stop at their hardware self-test until the board ROM");
            Console.WriteLine("  is supplied. The common one across Model 3 sets is epr-18022.ic2 (CRC 0x0CA70F80).");
            Console.WriteLine();
            foreach (var a in affected) Console.WriteLine("      " + a);
        }
        Console.WriteLine();
        return missing == 0 ? 0 : 1;
    }

    static string Truncate(string s, int n) => s.Length <= n ? s : s.Substring(0, n - 1) + "\u2026";
}
