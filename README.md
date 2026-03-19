# CS 1.6 Master Servers Restored

Restores custom master server support for Counter-Strike 1.6, allowing server browsers to query independent master servers instead of Valve's deprecated infrastructure.

Supports all three CS 1.6 client variants:
- **Steam Legacy** (pre-anniversary update)
- **Anniversary Update** (25th anniversary build)
- **GoldClient/CSPro** (non-Steam client)

## Screenshots

### Steam Legacy
![Steam Legacy](screenshots/steam_legacy.png)

### Anniversary Update
![Anniversary](screenshots/anniversary.png)

### GoldClient/CSPro
![GoldClient](screenshots/goldclient.png)

## How It Works

### Steam Legacy & Anniversary

A proxy DLL (`mastersrv.dll`) intercepts the Steam matchmaking API to query custom master servers via the standard Valve HL1 UDP protocol. The original `ServerBrowser.dll` is patched to load `mastersrv.dll` instead of `steam_api.dll` for matchmaking functions only - all other Steam API calls (favorites, LAN, friends, history) pass through to the real `steam_api.dll`.

- Master server list loaded from `platform/config/MasterServers.vdf`
- Servers queried via A2S_INFO for live details (name, map, players, ping)
- Results delivered incrementally to the server browser UI
- One universal `mastersrv.dll` works with both pre-anniversary and anniversary builds

### GoldClient/CSPro

The CSPro `steam_api.dll` has hardcoded GMS (Game Master Server) protocol URLs that are XOR-obfuscated in the binary. The patcher replaces these URLs with custom master server addresses at specific instruction offsets.

A hosts file is included to block the CSPro auto-updater from overwriting the patched DLL.

## Installation

### Steam Legacy & Anniversary

Download the `pre-anniversary` or `anniversary` release and copy into your Half-Life folder:

```
Half-Life/
├── mastersrv.dll                      <- new
└── platform/
    ├── config/
    │   └── MasterServers.vdf          <- new
    └── servers/
        └── ServerBrowser.dll          <- replace
```

Launch with `-insecure` flag.

### GoldClient/CSPro

Download the `goldclient` release:

1. Replace `steam_api.dll` in your game folder with the patched version
2. Append contents of `hosts_append.txt` to `C:\Windows\System32\drivers\etc\hosts` (run as Administrator)

The hosts file blocks `depot.cs-play.net` and `renewal.cs-play.net` to prevent the auto-updater from reverting the patch.

## Configuration

### MasterServers.vdf

Edit `platform/config/MasterServers.vdf` to add custom master servers:

```
"MasterServers"
{
    "hl1"
    {
        "0"
        {
            "addr"    "ms.cs16.net:27010"
        }
        "1"
        {
            "addr"    "hl1master.steampowered.com:27011"
        }
    }
}
```

Add as many entries as needed. The server browser queries all configured masters and merges results.

If the VDF file is missing, the default master `ms.cs16.net:27010` is used.

## Building from Source

Requires MinGW cross-compiler (i686-w64-mingw32-g++), Python 3, and a Steam account that owns CS 1.6.

The original `ServerBrowser.dll` binaries are not stored in this repository. They are fetched from Steam's CDN at build time using [DepotDownloader](https://github.com/SteamRE/DepotDownloader), pulling from the CS 1.6 client depots (app 10, branches `steam_legacy` and `public`).

```bash
# Install dependencies (Debian/Ubuntu)
sudo apt-get install gcc-mingw-w64-i686 g++-mingw-w64-i686 python3

# Create .env with your Steam credentials (gitignored)
echo 'STEAM_USERNAME=your_username' > .env
echo 'STEAM_PASSWORD=your_password' >> .env

# Build everything — fetches originals, compiles, patches, packages
bash build.sh

# Output in dist/
#   cs16-masterservers-pre-anniversary.zip
#   cs16-masterservers-anniversary.zip
#   cs16-masterservers-goldclient.zip
```

`build.sh` installs DepotDownloader automatically if not found. The CI workflow uses GitHub Actions secrets for credentials.

## Technical Details

### mastersrv.dll Exports

The proxy DLL exports all functions needed by both ServerBrowser.dll versions:

| Function | Pre-Anniversary | Anniversary |
|----------|:-:|:-:|
| SteamMatchmakingServers | x | |
| SteamInternal_ContextInit | | x |
| SteamInternal_FindOrCreateUserInterface | | x |
| SteamAPI_GetHSteamUser | | x |
| SteamAPI_Init | x | x |
| SteamAPI_Shutdown | x | x |
| SteamAPI_RunCallbacks | x | x |
| SteamAPI_RegisterCallback | x | x |
| SteamAPI_UnregisterCallback | x | x |
| SteamFriends | x | |
| SteamApps | x | |
| SteamMatchmaking | x | |

### Master Server Protocol

The Valve HL1 master server UDP protocol:

- **Request**: `0x31` + region (1 byte) + last IP:port string + filter string
- **Response**: `0xFFFFFFFF` + `0x0A66` + repeated (IP:4B + port:2B) big-endian, terminated by `0.0.0.0:0`
- Pagination: last received address used as cursor for next request


