# Audio Editor module

Waveform-oriented audio editing for Material Everything. The core owns tracks, clipboard,
selection operations, and mixing; a pluggable `DspBackend` owns decoding, encoding, and DSP.
The shipped native backend reads and writes 16-bit PCM WAV. MP3, OGG, and FLAC adapters can
register through `AudioEditor(std::unique_ptr<DspBackend>)`; unsupported formats fail closed
with `last_error()` instead of producing partial audio.

Operations: open, cut/copy/paste/delete, fade in/out, amplify, normalize, conservative noise
smoothing placeholder, multi-track mixing, and export. The M3 UI contract exposes toolbar and
waveform visibility, zoom, selection, and active-track state.
