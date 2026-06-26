# Inter-chip protocol — ESP32-S3 ↔ RP2040

The RP2040 on the SenseCAP Indicator D1 only does one job in this firmware:
drive the buzzer when told to. It never touches WiFi, Supabase, or the
clock — that all lives on the ESP32-S3 side, which is the only chip with
enough context (time, alarm settings, network) to decide *when* to buzz.

## Why a separate chip needs a protocol at all

Keeping the buzzer logic on a second MCU means a crash or a slow HTTP call
on the ESP32-S3 side can never *block* the alarm from sounding — the
RP2040 just keeps running its own tiny, dumb loop regardless of what the
S3 is doing. The tradeoff is they need to agree on what bytes mean what.

## Wire format

UART, 115200 baud, 8N1. Every message is exactly 3 bytes:

```
[0x7E] [command] [checksum]
```

- `0x7E` — start byte, constant.
- `command` — one of the values below.
- `checksum` — `0x7E XOR command` (catches framing bugs, not malicious
  tampering; this is a few centimeters of PCB trace, not a hostile link).

## Commands (ESP32-S3 → RP2040)

| Byte | Name          | Meaning |
|------|---------------|---------|
| 0x01 | PLAY_ALARM    | Start the alarm beep pattern at **volume 2** (soft default). Legacy command — use 0x11–0x15 for explicit volume control. |
| 0x02 | STOP_ALARM    | Stop immediately, e.g. because the user tapped "stop" on the touchscreen |
| 0x03 | PLAY_CHIME    | One short beep, for UI feedback (button taps, confirmations) |
| 0xF0 | PING          | Health check |
| 0x11 | PLAY_ALARM_V1 | Start alarm at volume 1 — whisper (~3% duty cycle) |
| 0x12 | PLAY_ALARM_V2 | Start alarm at volume 2 — soft default (~10% duty cycle) |
| 0x13 | PLAY_ALARM_V3 | Start alarm at volume 3 — medium (~24% duty cycle) |
| 0x14 | PLAY_ALARM_V4 | Start alarm at volume 4 — loud (~39% duty cycle) |
| 0x15 | PLAY_ALARM_V5 | Start alarm at volume 5 — max (~50% duty cycle, the passive buzzer ceiling) |

### Volume implementation note

The RP2040 uses `analogWriteFreq()` + `analogWrite()` to drive the passive buzzer
rather than `tone()`. This lets the duty cycle be varied independently of frequency,
which is the only way to change perceived loudness on a passive buzzer. Beyond 50%
duty cycle the waveform stays loud so 128/255 (~50%) is the hard ceiling.

Old ESP32 firmware that only knows command `0x01` will still work — the RP2040
treats it as volume 2 (soft default), maintaining backward compatibility during
OTA rollouts where the two chips might briefly be on different versions.

## Responses (RP2040 → ESP32-S3)

| Byte | Meaning |
|------|---------|
| 0xAA | ACK — sent once, immediately after receiving any valid command |

The S3 side's `ping()` (see `rp2040_link.h`) waits up to 200ms for this
byte. If it doesn't arrive, the S3 assumes the RP2040 is unresponsive —
worth logging/surfacing somewhere visible rather than silently failing,
since "the alarm chip stopped responding" is exactly the kind of failure
you want to notice before it matters at 7am.

## Extending this later

If you ever want richer audio (melodies instead of a fixed beep pattern,
volume control, etc.), the cleanest extension is adding new command bytes
with a payload length byte after them, rather than overloading the
existing 3-byte fixed frame. Keep PLAY_ALARM/STOP_ALARM/PING exactly as-is
so old and new firmware on either chip stay compatible during an OTA
rollout where the two chips might briefly be on different versions.
