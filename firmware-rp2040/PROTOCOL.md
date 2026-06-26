# Inter-chip protocol — ESP32-S3 ↔ RP2040

The RP2040 on the SenseCAP Indicator D1 owns the things physically wired to
it: the buzzer, and the I2C ambient sensor (temperature + humidity) on the
Grove port. It never touches WiFi, Supabase, or the clock — that all lives on
the ESP32-S3 side, which is the only chip with enough context (time, alarm
settings, network) to decide *when* to buzz and where to display readings.

So the protocol has two halves:
- **Buzzer** — fire-and-forget commands the S3 sends, each acked with a single
  `0xAA` byte (3-byte frames, below).
- **Sensors** — the S3 asks for the latest temperature/humidity with `READ_TH`
  and the RP2040 replies with a fixed 7-byte data frame (see *Sensor response*).

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
| 0x20 | READ_TH       | Request latest temperature + humidity. Unlike the others, this is **not** acked with `0xAA` — the RP2040 replies with the 7-byte *Sensor response* frame below. |
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

### Sensor response (RP2040 → ESP32-S3, reply to `READ_TH`)

A fixed 7-byte frame:

```
[0x7E] [0x20] [tempHi] [tempLo] [humHi] [humLo] [checksum]
```

- `0x7E` — start byte, same as commands.
- `0x20` — echoes the `READ_TH` command so the S3 can tell this frame apart
  from a stray `0xAA` ack.
- `temp` — **signed** 16-bit, big-endian, in **centi-degrees Celsius**
  (e.g. `2345` = 23.45 °C; can be negative). The sentinel `0x8000`
  (−32768) means *"no valid reading"* — sensor missing, not yet warmed up,
  or an I2C error. The S3 shows `--°` in that case rather than a bogus number.
- `hum` — **unsigned** 16-bit, big-endian, in **centi-percent** (0–10000,
  i.e. `5012` = 50.12 % RH).
- `checksum` — XOR of the six preceding bytes (`0x7E ^ 0x20 ^ tempHi ^ tempLo
  ^ humHi ^ humLo`).

The RP2040 reads the sensor on its own ~2 s cadence into a cache and answers
`READ_TH` from that cache, so the reply is instant and never blocks the buzzer
loop. The S3 polls this every `SENSOR_POLL_INTERVAL_MS` (see `config.h`).

> **Hardware note.** Temperature/humidity comes from the **AHT20** on the
> RP2040's Grove I2C port (`SDA=GP20`, `SCL=GP21`, per Seeed's reference
> example). If a unit has no AHT20 plugged in, every `READ_TH` returns the
> `0x8000` sentinel and the S3 simply shows `--`. A unit whose temp/humidity
> comes from a built-in SCD41 instead would swap only the read in
> `readSensorInto()` on the RP2040 — the wire format stays identical.

## Extending this later

If you ever want richer audio (melodies instead of a fixed beep pattern,
volume control, etc.), the cleanest extension is adding new command bytes
with a payload length byte after them, rather than overloading the
existing 3-byte fixed frame. Keep PLAY_ALARM/STOP_ALARM/PING exactly as-is
so old and new firmware on either chip stay compatible during an OTA
rollout where the two chips might briefly be on different versions.
