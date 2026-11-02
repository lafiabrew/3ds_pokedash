# PokeDash (`3ds_pokedash`)

> **Pokemon Save Editor** — Nintendo 3DS Homebrew (`CTR-P-PKDS`, UniqueId `0x0D3E1`) by [**lafiabrew**](https://github.com/orgs/lafiabrew/repositories)

PKHaX-inspired Gen 3–7 & ROM-Hack surgical Pokémon save editor + Type/Nature/Shiny/EV companion for Nintendo 3DS.

## Build & Verify

```bash
# Run host C11 ASan/UBSan unit & UI simulation tests
make test

# Build ARM11 3DS .elf, .smdh, .3dsx, and .cia (via devkitARM or rootless Podman container)
make 3ds && make verify-3ds

# Serve FBI QR code or push .cia over LAN
make qr
```
