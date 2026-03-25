[![Build and Release](https://github.com/coffeegrind123/cs16-masterservers-restored/actions/workflows/build.yml/badge.svg)](https://github.com/coffeegrind123/cs16-masterservers-restored/actions/workflows/build.yml)
# CS 1.6 Master Servers Restored

Brings back working server browsers and listen server hosting for Counter-Strike 1.6. Supports Steam Legacy, Anniversary Update, and non-Steam (GoldClient) editions.

| Steam Legacy | Anniversary | GoldClient |
|:---:|:---:|:---:|
| ![Steam Legacy](screenshots/steam_legacy.png) | ![Anniversary](screenshots/anniversary.png) | ![GoldClient](screenshots/goldclient.png) |

## Player Guide

### Download

Grab the latest release for your version:

**[Download Latest Release](https://github.com/coffeegrind123/cs16-masterservers-restored/releases/latest)**

| Build | For |
|-------|-----|
| **Pre-Anniversary** | Steam Legacy (before 25th anniversary update) |
| **Anniversary** | 25th Anniversary Update |
| **GoldClient** | CSPro / GoldClient (non-Steam) |

### Installation — Steam (Legacy & Anniversary)

1. Extract the zip into your Half-Life folder:
   ```
   C:\Program Files (x86)\Steam\steamapps\common\Half-Life\
   ```
   Replace `ServerBrowser.dll` when asked.

2. Add `-insecure` to your CS 1.6 launch options:
   Right-click CS 1.6 in Steam → Properties → Launch Options → add `-insecure`

3. Launch the game — the server browser should now work.

### Installation — GoldClient / CSPro

1. Replace `steam_api.dll` in your game folder with the one from the zip
2. Open `C:\Windows\System32\drivers\etc\hosts` in Notepad (run as Administrator)
3. Paste the contents of `hosts_append.txt` at the bottom and save

### Configuration

Edit `platform/config/MasterServers.vdf` to set which master servers to query:

```
"MasterServers"
{
    "hl1"
    {
        "0" { "addr"    "ms.cs16.net:27010" }
    }
}
```

Add as many entries as needed — the server browser queries all configured masters and merges results. If the file is missing, `ms.cs16.net:27010` is used by default.

---

## Hosting a Listen Server

Host a game that other players can find and join — including non-Steam clients.

### Setup

1. Install the mod as described above
2. Forward port **27015 (TCP+UDP)** in your router
3. On Anniversary edition: untick "Enable Steam Networking" in the server creation dialog
4. Start a game

Everything else is automatic:
- Master server loaded from `MasterServers.vdf`
- `sv_lan` set to 0 for internet visibility
- Public IP detected and displayed
- Heartbeats sent every 30 seconds
- FastDL HTTP server started for map downloads
- Non-Steam clients can connect with unique SteamIDs

### Console Commands

| Command | What it does |
|---------|-------------|
| `setmaster <ip[:port]>` | Set master server manually (also works in `config.cfg` or as `+setmaster` launch option) |
| `fastdl_port [port]` | Show or change the FastDL HTTP port (default: 27015) |
| `heartbeat` | Send an extra heartbeat immediately |
| `status` | Show server info including public IP |

---

## Server Administrator Guide

### Adding your ReHLDS server to this master server list

Your game server needs two plugins installed on [ReHLDS](https://github.com/dreamstalker/rehlds):

- **[ReUnion](https://github.com/rehlds/ReUnion)** — accepts non-Steam clients (protocol 47/48)
- **[rehlmaster](https://github.com/your-link-here)** — custom master server registration

#### reunion.cfg

Key settings to verify (defaults already allow p47/p48 clients):

```
AuthVersion = 4
SteamIdHashSalt = YourSecretSaltHere16PlusChars
ServerInfoAnswerType = 0
FixBuggedQuery = 1
EnableQueryLimiter = 1
```

`SteamIdHashSalt` is required with `AuthVersion >= 3`. Use a random string of 16+ characters. This prevents SteamID theft between servers.

#### reauthcheck.cfg

If you have ReAuthCheck installed, disable the per-IP connection limit to avoid kicking legitimate players from shared networks (internet cafes, NAT, VPN):

```
CheckMaxIp = 0
```

#### server.cfg

Add these lines to register with the master server:

```
sv_master1 ms.cs16.net:27010
sv_master2 master1.cs16.net:27010
```

Once configured, your server will send heartbeats automatically and appear in the server list within a few minutes.

---

## Building from Source

Requires MinGW cross-compiler, Python 3, and a Steam account that owns CS 1.6.

```bash
sudo apt-get install gcc-mingw-w64-i686 g++-mingw-w64-i686 python3

echo 'STEAM_USERNAME=your_username' > .env
echo 'STEAM_PASSWORD=your_password' >> .env

bash build.sh
```

Output in `dist/`: three release zips (pre-anniversary, anniversary, goldclient).

The build fetches original `ServerBrowser.dll` binaries from Steam's CDN via [DepotDownloader](https://github.com/SteamRE/DepotDownloader) and the GoldClient `steam_api.dll` from [gold-plus/builds](https://github.com/gold-plus/builds).

---

## Technical Details

<details>
<summary>Click to expand</summary>

### Architecture

```
ServerBrowser.dll (patched import: steam_api.dll → mastersrv.dll)
    ↓ loads
mastersrv.dll (proxy DLL)
    ├── Server browser: queries HL1 master servers via UDP
    ├── A2S_INFO: sliding window (128 concurrent, 2s timeout per server)
    ├── Heartbeat: auto-load from VDF, challenge-response, 30s interval
    ├── FastDL: embedded HTTP file server (TCP 27015)
    ├── Public IP: detection cascade (master /ip → api.ipify.org → engine cvar)
    ├── Reunion: emulator detection chain + CreateUnauthenticatedUserConnection
    └── Engine hooks: runtime pattern scanning of hw.dll (no files modified)
```

### Non-Steam Client Support

ReUnion-compatible authentication detects 11 emulator types:
RevEmu (all variants), SteamClient 2009, SteamEmu, OldRevEmu, AVSMP, sXe Injected, Setti, HLTV, NoSteam 47/48.

Each client gets a unique persistent SteamID derived from their emulator ticket data. Configurable via `reunion.cfg` (same format as [ReUnion](https://github.com/rehlds/ReUnion)).

### Engine Integration

All hooks applied at runtime via pattern scanning — no engine files modified on disk. Resolves console commands, cvars, server state, network sockets, validation functions, and auth callbacks from `hw.dll` memory. Works on both pre-anniversary and anniversary builds with adaptive pattern matching.

### Protocols

| Protocol | Direction | Format |
|----------|-----------|--------|
| Master Query | client → master | `0x31` + region + last addr + filter |
| Master Response | master → client | `0xFF×4 0x66 0x0A` + IP:port pairs |
| Heartbeat | server → master | Challenge-response + KV registration |
| A2S_INFO | client → server | `0xFF×4 0x54` "Source Engine Query" |
| FastDL | client → server | HTTP/1.1 GET on TCP 27015 |

</details>
