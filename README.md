[![Build and Release](https://github.com/coffeegrind123/cs16-masterservers-restored/actions/workflows/build.yml/badge.svg)](https://github.com/coffeegrind123/cs16-masterservers-restored/actions/workflows/build.yml)
# CS 1.6 Master Servers Restored

Restores custom master server support for Counter-Strike 1.6, allowing server browsers to query independent master servers instead of Valve's deprecated infrastructure.

Works with all three CS 1.6 client variants:
- **Steam Legacy** (pre-25th anniversary update)
- **Anniversary Update** (25th anniversary build)
- **GoldClient/CSPro** (non-Steam client)

## Screenshots

### Steam Legacy
![Steam Legacy](screenshots/steam_legacy.png)

### Anniversary Update
![Anniversary](screenshots/anniversary.png)

### GoldClient/CSPro
![GoldClient](screenshots/goldclient.png)

## What is this?

A drop-in proxy DLL (`mastersrv.dll`) that intercepts Steam's matchmaking API to query custom HL1 master servers. The original `ServerBrowser.dll` is patched to load `mastersrv.dll` instead of `steam_api.dll` for matchmaking — all other Steam API calls pass through untouched.

Features:
- Incremental server list population (servers appear as they respond, like the original Steam behavior)
- Sliding window A2S queries (128 concurrent, handles 3000+ server lists)
- Enhanced `setmaster` console command with validation and VDF config writing
- Automatic heartbeats — master server loaded from VDF on server start, no manual `setmaster` needed
- Public IP auto-detection and display (queries master server, falls back to api.ipify.org)
- Built-in FastDL HTTP file server for map/resource downloads (TCP port 27015)
- ReUnion-compatible non-Steam client support (RevEmu, SteamEmu, OldRevEmu, Xash3D, etc.)
- Automatic `sv_lan 0` when master server is configured
- Engine integration via runtime pattern scanning — no engine files modified
- One universal `mastersrv.dll` works with both pre-anniversary and anniversary builds

## Installation

### Steam Legacy & Anniversary

Download the `pre-anniversary` or `anniversary` release and copy into your Half-Life folder:

```
Half-Life/
├── mastersrv.dll                      <- new
└── platform/
    ├── config/
    │   ├── MasterServers.vdf          <- new
    │   └── reunion.cfg                <- new (non-Steam client config)
    └── servers/
        └── ServerBrowser.dll          <- replace
```

Launch with `-insecure` flag. Forward port 27015 (TCP+UDP) in your router.

### GoldClient/CSPro

Download the `goldclient` release:

1. Replace `steam_api.dll` in your game folder with the patched version
2. Append contents of `hosts_append.txt` to `C:\Windows\System32\drivers\etc\hosts` (run as Administrator)

The hosts file blocks `depot.cs-play.net` and `renewal.cs-play.net` to prevent the auto-updater from reverting the patch.

## Configuration

### MasterServers.vdf

Edit `platform/config/MasterServers.vdf` to configure master servers:

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

Masters are tried top-down — the first one that responds is used. If none respond, the local server cache is used as fallback. If the VDF file is missing, the default master `ms.cs16.net:27010` is used.

The first master in the VDF is automatically loaded for heartbeats when a listen server starts — no manual `setmaster` required.

### reunion.cfg

Controls non-Steam client authentication. Uses the same format and options as [ReUnion](https://github.com/rehlds/ReUnion). The default config shipped with the release accepts all common non-Steam emulator types.

Key options:

| Option | Default | Description |
|--------|---------|-------------|
| `AuthVersion` | 4 | Auth protocol version (1-4, higher = newer) |
| `SteamIdHashSalt` | (empty) | Salt for SteamID hashing (16+ chars for AuthVersion >= 3) |
| `cid_RevEmu` | 1 | RevEmu clients: 1=accept, 5=reject |
| `cid_NoSteam48` | 5 | Unknown protocol 48: 1=accept, 3=IP-based ID, 5=reject |
| `LoggingMode` | 3 | 0=none, 1=console, 2=logfile, 3=both |

Config search order:
1. `cstrike/reunion.cfg`
2. `platform/config/reunion.cfg`
3. `reunion.cfg` (Half-Life root)

### Console Commands

| Command | Description |
|---------|-------------|
| `setmaster <ip[:port]>` | Set master server (validates, writes VDF, starts heartbeats) |
| `fastdl_port [port]` | Show or set FastDL HTTP port (default 27015, takes effect on restart) |
| `heartbeat` | Send an extra heartbeat on demand |
| `status` | Shows server info including public IP |

`setmaster` and `fastdl_port` also work from launch options (`+setmaster`, `+fastdl_port`) and `config.cfg`.

## Hosting a Listen Server

Just install the mod, configure `MasterServers.vdf`, and start a game. Everything else is automatic:

1. Install the mod as described above
2. Configure your master server in `MasterServers.vdf`
3. Forward port **27015 TCP+UDP** in your router
4. Start a game (New Game or `map <mapname>` in console)
5. On the Anniversary edition, untick "Enable Steam Networking" in the server creation dialog

On server start, mastersrv.dll automatically:
- Loads the master server from `MasterServers.vdf`
- Sets `sv_lan 0` for internet visibility
- Detects your public IP (via master server or api.ipify.org)
- Starts sending heartbeats every 30 seconds
- Starts the FastDL HTTP server for map downloads
- Sets `sv_downloadurl` to your public IP
- Patches the engine's IP display to show your public IP

Your server will appear in other players' server browsers, and non-Steam clients can join and download maps automatically.

### Heartbeat Fields

All heartbeat fields are resolved from the engine at runtime:

| Field | Source |
|-------|--------|
| `protocol` | Parsed from `sv_version` cvar |
| `version` | Parsed from `sv_version` cvar |
| `map` | Engine server structure (pattern scanned) |
| `maxplayers` | Engine server structure (pattern scanned) |
| `players` / `bots` | Client array iteration |
| `gamedir` | Engine gamedir buffer (pattern scanned) |
| `password` | `sv_password` cvar |
| `lan` | `sv_lan` cvar |
| `secure` | `-insecure` command line detection |
| `type` | `l` (listen) or `d` (dedicated) via module check |

## FastDL (HTTP File Server)

mastersrv.dll embeds a lightweight HTTP file server that serves game files (maps, models, resources) to clients. This enables non-Steam clients (Xash3D, browser-based) that lack UDP file transfer to download missing content.

- **Port**: TCP 27015 by default
- **Auto-start**: Starts when the server starts, stops when the server stops
- **sv_downloadurl**: Automatically set to `http://<public_ip>:27015`
- **File search**: Serves from `cstrike/`, `cstrike_downloads/`, and `valve/` directories
- **Security**: Extension whitelist (bsp, mdl, wav, spr, wad, etc.), path traversal prevention, no config files served
- **Performance**: Thread-per-connection (up to 4 concurrent), 64KB streaming
- **Configurable**: `fastdl_port` console command, `+fastdl_port` launch option, or `fastdl_port` in config.cfg

## Non-Steam Client Support

mastersrv.dll includes [ReUnion](https://github.com/rehlds/ReUnion)-compatible authentication for non-Steam clients on listen servers. Non-Steam players can join your server without being rejected by Steam validation.

Supported emulator types:
- RevEmu (all variants including 2013, Xash3D)
- SteamClient 2009
- SteamEmu
- OldRevEmu
- AVSMP
- sXe Injected
- NoSteam protocol 47/48

Each non-Steam client gets a unique persistent SteamID derived from their emulator ticket data (HDD serial hash, volume ID, etc.). Steam clients are unaffected and keep their real SteamIDs.

Runtime patches applied to hw.dll (no files modified on disk):
- Steam validation function detour via trampoline
- `CreateUnauthenticatedUserConnection` for bot Steam sessions
- Auth callback flag patches to prevent async Steam kicks
- Certificate length check bypass for non-Steam tickets
- Server IP display patch (shows public IP instead of local)

Configure via `reunion.cfg` — uses the same format as the [ReUnion plugin](https://github.com/rehlds/ReUnion).

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

### Architecture

```
ServerBrowser.dll (patched: steam_api.dll → mastersrv.dll)
    ↓ loads
mastersrv.dll (proxy DLL)
    ├── Intercepts: SteamMatchmakingServers, SteamAPI_RunCallbacks, etc.
    ├── Forwards: SteamFriends, SteamApps, SteamMatchmaking → real steam_api.dll
    ├── Queries: MasterServers.vdf → HL1 UDP master protocol
    ├── A2S_INFO: Sliding window (128 concurrent, 2s per-server timeout)
    ├── Caches: platform/cache/servers.dat
    ├── Engine hook: pattern scans hw.dll for console commands, cvars, server state
    ├── Heartbeat: auto-load from VDF, challenge-response via engine's server socket
    ├── FastDL: embedded HTTP file server (TCP 27015, thread-per-connection)
    ├── Public IP: cascade detection (master /ip → api.ipify.org → engine cvar)
    └── Reunion: emulator detection chain + CreateUnauthenticatedUserConnection
```

### Proxy DLL Exports

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

### Engine Integration (Runtime Pattern Scanning)

mastersrv.dll hooks into the engine without modifying any files. During the first `SteamAPI_RunCallbacks`, it scans `hw.dll` memory to resolve:

| Function/Address | Discovery Method |
|-----------------|-----------------|
| `Cmd_AddCommand` | String `"Cmd_AddCommand: %s already defined as a var"` → xref → function start |
| `Cmd_Argc` / `Cmd_Argv` / `Con_Printf` | From original `setmaster` handler's call targets |
| `Cvar_FindVar` | String `"Cvar_RegisterVariable: %s is a command"` → first CALL in that function |
| Server state pointer | `MOV ECX, [addr]` or `CMP [addr], 0` in setmaster handler |
| Map name buffer | String `"map     :  %s at"` → PUSH before it |
| Maxplayers pointer | String `"players :  %i active (%i max)"` → MOV/PUSH before it |
| Client array base | `maxplayers_addr - 4` |
| Client stride | `ADD reg, imm32` in player iteration loop |
| `ip_sockets[]` array | String `"NET_SendPacket: bad address type"` → `MOV ESI,[reg*4+base]` in that function |
| Command linked list head | `MOV reg, [addr]` in `Cmd_AddCommand` body |
| Steam validation function | String `"STEAM validation rejected"` → preceding CALL target |
| Auth callback handler | Byte pattern `8A 81 86 00 00 00 84 C0` (flag check in callback) |
| Server netadr_t global | String `"Server IP address %s"` → `MOV ESI, imm32` before PUSH |

### Protocols

**Master Server Query** (client → master):
- Request: `0x31` + region + last IP:port + filter string
- Response: `0xFFFFFFFF 0x66 0x0A` + (IP:4B + port:2B)* + `0.0.0.0:0`

**Heartbeat** (server → master):
- Challenge: `0xFFFFFFFF 0x71` → master replies `0xFFFFFFFF 0x73 0x0A` + 4-byte challenge
- Registration: `0\n\protocol\48\challenge\<N>\players\<N>\max\<N>\bots\<N>\gamedir\<S>\map\<S>\type\<l|d>\password\<0|1>\os\w\secure\<0|1>\lan\<0|1>\version\<S>\region\255\product\<S>\n`

**A2S_INFO** (client → game server):
- Request: `0xFFFFFFFF 0x54 "Source Engine Query\0"`
- Response: Server name, map, players, maxplayers, bots, ping, etc.

**FastDL** (client → server HTTP):
- Request: `GET /maps/de_dust2.bsp HTTP/1.1`
- Response: File contents with `Content-Length` header
- Searches: `cstrike/` → `cstrike_downloads/` → `valve/`
