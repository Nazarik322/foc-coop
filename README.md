# foc-coop (work in progress, local only)

Runtime layer for Transformers: Fall of Cybertron (Steam build 12831, TFOC.exe SHA-256
c1dab177f81503ecb831d63a3d799f928dd9823242ca3689482c684396acfedc), built to add co-op.

`src/proxy_dinput8/dinput8.cpp` is a 32-bit `dinput8.dll` proxy dropped next to TFOC.exe. Options in
`foc_coop.ini` next to the exe (`[foc_coop]`):

| key | effect |
|---|---|
| `instance=B` | appended to the single-instance mutex name so several copies run on one PC |
| `tcpdriver=1` | replaces `IpDrv.TDwNetDriver` (Demonware) with `IpDrv.TcpNetDriver` in the loaded config strings |
| `dump=1` | writes `foc_dump.bin` (memory image of the exe) after 12 s |
| `scan=1`, `scandelay=<ms>` | finds GNames / GObjObjects and logs object layout facts |
| `trace=1` | experimental vtable slot tracer |
| `patchcap=1`, `cfg=1` | experiments (script bytecode patch, config values) |

It always writes `foc_coop.log` (network calls, mutex, exceptions). Build: `build.bat` (VS 2022 Build Tools, x86).
Findings and open problems: see games/transformers-fall-of-cybertron/NOTES.md in the coop-anything checkout.
