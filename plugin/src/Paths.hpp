#pragma once
#include "PluginKind.hpp"
#include <functional>
#include <string>
#include <vector>

namespace pe {

enum class Platform { Linux, MacOS, Windows };

/// the platform this build runs on
Platform hostPlatform();

/// the folders for the user's own files (an empty string: the environment names none)
struct DataDirs {
    std::string data;   ///< instruments added, snapshots, presets
    std::string config; ///< MIDI maps
};

/// Linux: $XDG_DATA_HOME/dussek and $XDG_CONFIG_HOME/dussek (absolute ones only), else
/// ~/.local/share/dussek and ~/.config/dussek; macOS: ~/Library/Application Support/Dussek
/// for both; Windows: %APPDATA%\Dussek for both. `env` reads a variable (nullptr: unset).
DataDirs dataDirs(const std::function<const char*(const char*)>& env, Platform platform);

/// the process's: its environment, this platform
DataDirs dataDirs();

/// the folders an installer fills (the instruments, once for every user): Windows
/// %PROGRAMDATA%\Dussek (none when unset); macOS /Library/Application Support/Dussek; Linux each
/// absolute entry of $XDG_DATA_DIRS + /dussek, then /usr/local/share/dussek and /usr/share/dussek
/// where the list leaves them out
std::vector<std::string> systemDataDirs(const std::function<const char*(const char*)>& env, Platform platform);

/// the process's: its environment, this platform
std::vector<std::string> systemDataDirs();

/// Linux: the data folders an installation implies from where the host found the plugin (`bundle`:
/// a path inside it, its resources folder): under <prefix>/lib*/{lv2,vst3,clap} →
/// <prefix>/share/dussek (a /usr prefix also <prefix>/local/share/dussek: install.sh --system puts
/// CLAP in /usr/lib/clap); under <home>/.{lv2,vst3,clap} → <home>/.local/share/dussek. A sandboxed
/// host (a Flatpak's /run/host/…) gives the path it sees. Other platforms, or none found: empty.
std::vector<std::string> bundleDataDirs(const std::string& bundle, Platform platform);

/// Linux in a sandbox that moves the user's folders: the data folders of the real home and the host's
/// system. A Flatpak (FLATPAK_ID set; XDG_DATA_HOME is the app's own): $HOME/.local/share/dussek, then
/// /run/host/usr/local/share/dussek and /run/host/usr/share/dussek (the host's, when the app may see
/// them); a Snap (SNAP_REAL_HOME set; HOME is the snap's): $SNAP_REAL_HOME/.local/share/dussek. Else
/// empty.
std::vector<std::string> sandboxDataDirs(const std::function<const char*(const char*)>& env, Platform platform);

/// the process's: its environment, this platform
std::vector<std::string> sandboxDataDirs();

/// The standalone's executable, for Copy CLI's command line: `self` the running binary
/// (DPF's getBinaryFilename) and whether it is the standalone; in a plugin, the standalone next to its
/// bundle (links resolved: `just install` links the bundles to plugin/build/bin), up to four folders
/// up (VST3's Contents/<arch>, a macOS bundle's Contents/MacOS), then in `installed` (the folders an
/// installer puts it in: installedProgramDirs), then in the folders of `path` (the PATH variable;
/// null: none). Empty: none found.
std::string standaloneExecutable(PluginKind k, const std::string& self, bool selfIsStandalone, const char* path,
    const std::vector<std::string>& installed = {});

/// the running program's path (the standalone's, or the host's for a plugin); empty: unknown
std::string executablePath();

/// an unpacked archive's folder, when `exe` is a standalone (named dussek or weidinger, .exe on Windows, any
/// case; on macOS inside Dussek.app/Contents/MacOS: the folder holding the app); empty for any other
/// program (a host running the plugin). Splits at `/` and `\`.
std::string portableFolder(const std::string& exe);

/// where the installers put the standalone: Windows %PROGRAMFILES%\Dussek, macOS /Applications
/// (Linux: on PATH)
std::vector<std::string> installedProgramDirs(PluginKind k);

/// The folder a file lies in (a file browser's answer): up to its last separator, `/` (on Windows
/// also `\`); a root kept ("/", "C:\"); empty for a bare name.
std::string folderOf(const std::string& file);
/// A path's last part (after its last separator, as folderOf splits): the file's name.
std::string fileNameOf(const std::string& file);

} // namespace pe
