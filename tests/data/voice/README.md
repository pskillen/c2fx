<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 The c2fx Authors -->

# Test voice set

Synthesized speech for the Codec 2 3200 checks. Each file is 16-bit signed
little-endian PCM, 8 kHz, mono, no header, and a whole number of 160-sample
(20 ms) frames. Nothing here is a recording.

| File | Frames | Duration | Source |
|---|---:|---:|---|
| `test_transmission.pcm` | 104 | 2.08 s | Copied from RadioFoundry `tests/data/voice/test_transmission.pcm` |
| `ten_second_countdown.pcm` | 540 | 10.80 s | Copied from RadioFoundry `tests/data/voice/ten_second_countdown.pcm` |
| `low_voice.pcm` | 307 | 6.14 s | `tools/make_voices.sh`, male US English, low pitch |
| `high_voice.pcm` | 309 | 6.18 s | `tools/make_voices.sh`, female US English, high pitch |

The two copied phrases are MIT, copyright 2026 The RadioFoundry Authors.
RadioFoundry's voice README describes them as synthesized speech and does not
name the synthesizer. `low_voice.pcm` and `high_voice.pcm` are MIT, copyright
2026 The c2fx Authors.

## How the new voices are made

`tools/make_voices.sh` regenerates `low_voice.pcm` and `high_voice.pcm`. The
committed files were made with espeak-ng 1.52.0 and SoX_ng 14.7.0.9 (`sox`).
Both phrases speak the same text, with no callsigns or names:

> This is a test transmission. One two three four five. The signal is clear and steady.

```
espeak-ng -v en-us+m3 -p 15 -s 155 -D -w low_voice.wav "<text>"
espeak-ng -v en-us+f3 -p 85 -s 155 -D -w high_voice.wav "<text>"
sox -D <wav> -t raw -r 8000 -c 1 -e signed-integer -b 16 <raw>
sox -D -t raw -r 8000 -e signed-integer -b 16 -c 1 <raw> -t raw <trimmed> trim 0 <N>s
sox -D -t raw -r 8000 -e signed-integer -b 16 -c 1 <trimmed> -t raw <out.pcm> vol <gain>
```

`<N>` is the 8 kHz sample count rounded down to a multiple of 160. `<gain>` is
the RMS of `test_transmission.pcm` divided by the RMS of the trimmed phrase
(12 digits). The script exits if a phrase is outside 4–8 s or its RMS is more
than 3 dB from `test_transmission.pcm`. espeak-ng `-D` is its deterministic
mode. sox `-D` turns dither off.

On the committed files the low voice sits near 90 Hz and the high voice near
300 Hz (median of voiced 40 ms windows). The copied phrases are left as they
are; this script does not rebuild them.
