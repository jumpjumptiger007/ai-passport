#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
repo="$(cd -- "$root/.." && pwd)"
emcc_bin="${EMCC:-emcc}"
emxx_bin="${EMXX:-em++}"
mkdir -p "$root/public/wasm"
common=(-O2 -DNDEBUG -I"$repo/components/sonic_core/include")
"$emcc_bin" -std=c11 "${common[@]}" \
  "$root/src/wasm/sonic-core-wrapper.c" "$repo/components/sonic_core/sonic_core.c" \
  -sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=web,worker,node -sALLOW_MEMORY_GROWTH=1 \
  -sEXPORTED_FUNCTIONS='["_malloc","_free","_sl_validate_payload","_sl_fragment","_sl_reassembly_reset","_sl_reassembly_create","_sl_reassembly_free","_sl_reassembly_accept","_sl_reassembly_poll"]' \
  -sEXPORTED_RUNTIME_METHODS='["HEAPU8"]' -o "$root/public/wasm/sonic-core.js"
"$emxx_bin" -std=c++11 -O2 -DNDEBUG \
  -I"$repo/ggwave_vendor/include" -I"$repo/ggwave_vendor/src" \
  -I"$repo/ggwave_vendor/src/reed-solomon" -I"$repo/components/sonic_ggwave_profile/include" \
  "$root/src/wasm/ggwave-web-wrapper.cpp" "$repo/components/sonic_ggwave_profile/sonic_ggwave_profile.cpp" \
  "$repo/ggwave_vendor/src/ggwave.cpp" \
  -sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=web,worker,node -sALLOW_MEMORY_GROWTH=1 \
  -sEXPORTED_FUNCTIONS='["_malloc","_free","_sl_ggwave_quiet","_sl_ggwave_create_rx","_sl_ggwave_create_tx","_sl_ggwave_free","_sl_ggwave_create_input_resampler","_sl_ggwave_resample_input","_sl_ggwave_free_input_resampler","_sl_ggwave_encode","_sl_ggwave_free_samples","_sl_ggwave_decode"]' \
  -sEXPORTED_RUNTIME_METHODS='["HEAPU8","HEAP16","HEAPU32"]' -o "$root/public/wasm/ggwave.js"
printf 'Built local WASM modules from sonic_core.c and pinned ggwave donor.\n'
