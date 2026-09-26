# RNNoise (vendored)

- Source: https://github.com/xiph/rnnoise, tag `v0.1.1` (model weights included in `src/rnn_data.c`)
- Licence: BSD-3-Clause (see `COPYING`), which allows closed-source commercial use.
  Keep `COPYING` with the product (e.g. in the installer's licences page / manual).
- OJU modification: `rnnoise_get_band_gains()` / `rnnoise_get_num_bands()` added to
  `src/denoise.c` and `include/rnnoise.h` so OJU can apply the network's band gains
  at the session's own sample rate. Nothing else was changed.
- Upgrade path: RNNoise v0.2 has a better model but its weights are downloaded from
  media.xiph.org at build time; swap the sources and keep the same two functions.
