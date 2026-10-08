// MapScriptExtra.cs -- the "Fish Map Script" bonus install, extracted
// verbatim from the original single-mod InstallerCore. Not really "part of"
// kek-mod so much as a separate always-on extra that happens to only apply
// there -- pulled in alongside kek-mod if the player doesn't already have it,
// own repo, own release cadence (no dev/prod, no beta channel, just
// "latest"), entirely best-effort (swallows every failure).

using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Text.RegularExpressions;

namespace KekModInstaller
{
    internal static class MapScriptExtra
    {
        // Installs into "Fish Map Script v<version>" so players who install
        // by hand can see which version they have. Community installs (and
        // release zips up to the v1.0 re-publish) used the bare "Fish Map
        // Script", NOT the "pangea-stratbal" repo's own label -- both are
        // recognized, and an up-to-date bare folder is just renamed. Only
        // one copy is ever kept: two would list the map twice and fight
        // over the same V*.lua include() names.
        private const string RepoOwner = "OBLASTWAR";
        private const string RepoName = "pangea-stratbal";
        // Internal, not private: ExtraModScan matches Assets/Maps folders
        // against this (see IsFishFolderName).
        internal const string FolderName = "Fish Map Script";

        internal static bool IsFishFolderName(string name)
        {
            return string.Equals(name, FolderName, StringComparison.OrdinalIgnoreCase)
                || name.StartsWith(FolderName + " v", StringComparison.OrdinalIgnoreCase);
        }

        private static string VersionedFolderName(string tag)
        {
            return FolderName + " v" + tag.TrimStart('v', 'V'); // "v1.0" -> "Fish Map Script v1.0"
        }

        // Installs the newest release, replacing an out-of-date copy, and
        // leaves exactly one "Fish Map Script v<version>" folder behind.
        // Deliberately swallows every failure (offline, repo/release
        // missing, extraction hiccup): this is a bonus, not part of any mod
        // itself, so it should never fail the main install.
        public static void EnsureInstalled(string dlcRoot, Action<string> log)
        {
            try
            {
                string mapsFolder = MapsFolder(dlcRoot);
                List<GhRelease> releases = GitHubModSource.FetchReleases(RepoOwner, RepoName);
                GhRelease release = releases[0]; // newest
                string targetDir = Path.Combine(mapsFolder, VersionedFolderName(release.TagName));

                string current = InstalledFolders(mapsFolder)
                    .FirstOrDefault(d => MapVersion.Same(DetectVersion(d), release.TagName));
                if (current != null)
                {
                    log(FolderName + " " + release.TagName + " already installed.");
                    if (!string.Equals(Path.GetFileName(current), Path.GetFileName(targetDir), StringComparison.OrdinalIgnoreCase)
                        && !Directory.Exists(targetDir))
                    {
                        log("Renaming " + Path.GetFileName(current) + " -> " + Path.GetFileName(targetDir) + "...");
                        Directory.Move(current, targetDir);
                        current = targetDir;
                    }
                }
                else
                {
                    GhAsset asset = release.Assets != null ? release.Assets.FirstOrDefault() : null;
                    if (asset == null)
                    {
                        log(FolderName + ": release " + release.TagName + " has no assets, skipping.");
                        return;
                    }

                    log("Installing " + FolderName + " " + release.TagName + "...");
                    string zipPath = GitHubModSource.DownloadAsset(asset);
                    string tempDir = Path.Combine(mapsFolder, ".kekmod-" + Guid.NewGuid().ToString("N"));
                    try
                    {
                        Directory.CreateDirectory(tempDir);
                        ZipFile.ExtractToDirectory(zipPath, tempDir);
                        // The zip holds one folder: "Fish Map Script" (v1.0's
                        // original layout) or "Fish Map Script v<version>".
                        string extracted = Directory.GetDirectories(tempDir).FirstOrDefault(d => IsFishFolderName(Path.GetFileName(d)));
                        if (extracted == null)
                        {
                            throw new InvalidOperationException("no \"" + FolderName + "\" folder in " + asset.Name);
                        }
                        if (Directory.Exists(targetDir))
                        {
                            Directory.Delete(targetDir, true); // same name, wrong contents
                        }
                        Directory.Move(extracted, targetDir);
                    }
                    finally
                    {
                        File.Delete(zipPath);
                        try { if (Directory.Exists(tempDir)) Directory.Delete(tempDir, true); } catch (Exception) { }
                    }
                    current = targetDir;
                    log(FolderName + " " + release.TagName + " installed.");
                }

                foreach (string dir in InstalledFolders(mapsFolder))
                {
                    if (!string.Equals(dir, current, StringComparison.OrdinalIgnoreCase))
                    {
                        log("Removing old " + Path.GetFileName(dir) + "...");
                        Directory.Delete(dir, true);
                    }
                }
            }
            catch (Exception ex)
            {
                log(FolderName + ": couldn't install (" + ex.Message + ")");
            }
        }

        // Removes every Fish Map Script folder from Assets/Maps. Never
        // throws -- a missing folder or locked file shouldn't fail the
        // overall uninstall.
        public static void Remove(string dlcRoot, Action<string> log)
        {
            try
            {
                foreach (string dir in InstalledFolders(MapsFolder(dlcRoot)))
                {
                    log("Removing " + Path.GetFileName(dir) + "...");
                    Directory.Delete(dir, true);
                }
            }
            catch (Exception ex)
            {
                log(FolderName + ": couldn't remove (" + ex.Message + ")");
            }
        }

        // Surfaced as its own pseudo-entry in the multi-mod [INSTALLED] line
        // -- it isn't a "mod" in the ModRegistry sense, but it's a visible
        // extra players installed and should show up the same way.
        public static DetectedModInstall DetectInstalled(string dlcRoot)
        {
            List<string> dirs = InstalledFolders(MapsFolder(dlcRoot));
            if (dirs.Count == 0)
            {
                return null;
            }
            var d = new DetectedModInstall();
            d.ModId = "fishmapscript";
            d.DisplayName = FolderName;
            d.FolderNames = dirs.Select(Path.GetFileName).ToList();
            d.VersionLabel = DetectVersion(dirs[0]);
            return d;
        }

        // Newest release's tag, e.g. "v1.0" -- same form as DetectVersion.
        public static string FetchLatestVersion()
        {
            return GitHubModSource.FetchReleases(RepoOwner, RepoName)[0].TagName;
        }

        private static List<string> InstalledFolders(string mapsFolder)
        {
            if (!Directory.Exists(mapsFolder))
            {
                return new List<string>();
            }
            return Directory.GetDirectories(mapsFolder, FolderName + "*")
                .Where(d => IsFishFolderName(Path.GetFileName(d))).OrderBy(d => d).ToList();
        }

        // Read off the release's .modinfo file name, e.g.
        // "VFishMapScriptv1.0.modinfo" -> "v1.0" (release.sh keeps it in step
        // with the tag). Null if no .modinfo file is present or its name
        // doesn't end in a version.
        private static string DetectVersion(string targetDir)
        {
            try
            {
                string modinfo = Directory.GetFiles(targetDir, "*.modinfo").FirstOrDefault();
                if (modinfo == null)
                {
                    return null;
                }
                Match m = Regex.Match(Path.GetFileNameWithoutExtension(modinfo), @"(\d+(?:\.\d+)*)$");
                return m.Success ? "v" + m.Groups[1].Value : null;
            }
            catch (Exception)
            {
                return null;
            }
        }

        private static string MapsFolder(string dlcRoot)
        {
            string assetsFolder = Path.GetDirectoryName(dlcRoot); // DLC's parent
            return Path.Combine(assetsFolder, "Maps");
        }
    }
}
