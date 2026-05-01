# TypeScript firmware (Moddable SDK) — spec

This directory holds the original TypeScript firmware that targets the
Moddable SDK on ESP32. It is **kept as the executable spec** for the
deployed C firmware in [`../c/`](../c/), not as the actively-flashed code.

The pure-logic modules (`modbus.ts`, `tuf2000m.ts`, `ring-buffer.ts`,
`provisioning.ts`) have Node-runnable tests that verify behavior — when
porting to C, they're the source of truth for correct behavior. Run:

```bash
npm test           # from repo root — 41 tests across the pure modules
```

## Why we moved off Moddable

Moddable required ESP-IDF v6.0; the v6.0 toolchain ships an updated newlib
that broke Moddable's `xs` sources (`__FILE`, `_REENT` undefined). Rather
than wait on upstream Moddable to catch up — and accept ongoing fragility
between two moving codebases — we ported to native ESP-IDF C, which is
what production ESP32 firmware almost always uses.

## Reviving the Moddable build (if upstream is fixed)

```bash
npm run build:firmware:moddable      # cd src/firmware/ts && mcconfig -d -m -p esp32 LANGUAGE=es2024
```

This still works in principle — it's the same code that compiled cleanly
through TypeScript and partway through `mcconfig`. The blocker is purely
the Moddable + ESP-IDF v6.0 toolchain compatibility.

## Module → C-port mapping

| TS module | C equivalent | Status |
|---|---|---|
| `modbus.ts` | `modbus.c/.h` | not yet ported |
| `tuf2000m.ts` | `tuf2000m.c/.h` | not yet ported |
| `ring-buffer.ts` | `ring_buffer.c/.h` | not yet ported |
| `provisioning.ts` | `provisioning.c/.h` | not yet ported |
| `nvs.ts` | `nvs_config.c/.h` | not yet ported |
| `wifi.ts` | `wifi_mgr.c/.h` | not yet ported |
| `uplink.ts` | `uplink.c/.h` | not yet ported |
| `main.ts` | `main.c` | scaffolded (Hello-World) |
