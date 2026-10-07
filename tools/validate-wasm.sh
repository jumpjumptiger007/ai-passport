#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cc_bin="${CC:-cc}"
cxx_bin="${CXX:-c++}"
emcc_bin="${EMCC:-emcc}"
emxx_bin="${EMXX:-em++}"
node_bin="${NODE:-node}"

for tool in "$cc_bin" "$cxx_bin" "$emcc_bin" "$emxx_bin" "$node_bin"; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "ERROR: required tool is unavailable: $tool" >&2
        exit 2
    fi
done

build_dir="$(mktemp -d /tmp/sonic-link-wasm.XXXXXX)"
trap 'case "$build_dir" in /tmp/sonic-link-wasm.*) rm -rf -- "$build_dir" ;; esac' EXIT
cd "$repo_root"

core_sources=(tests/sonic_parity_harness.c tests/sonic_golden_vectors.c components/sonic_core/sonic_core.c)
core_includes=(-Icomponents/sonic_core/include -Itests)

"$cc_bin" -std=c11 -O2 -Wall -Wextra -Werror "${core_includes[@]}" \
    "${core_sources[@]}" -o "$build_dir/sonic_parity_native"
"$build_dir/sonic_parity_native" > "$build_dir/native.txt"

"$emcc_bin" -std=c11 -O2 "${core_includes[@]}" "${core_sources[@]}" \
    -sENVIRONMENT=node -sEXIT_RUNTIME=1 -sINITIAL_MEMORY=33554432 \
    -o "$build_dir/sonic_parity_wasm.js"
"$node_bin" "$build_dir/sonic_parity_wasm.js" > "$build_dir/wasm.txt"
if ! cmp -s "$build_dir/native.txt" "$build_dir/wasm.txt"; then
    diff -u "$build_dir/native.txt" "$build_dir/wasm.txt" || true
    echo "G05: native/WASM canonical transcript mismatch" >&2
    exit 1
fi
echo "G05: native/WASM canonical transcript parity PASS"

vendor_file=ggwave_vendor/src/ggwave.cpp
vendor_header=ggwave_vendor/include/ggwave/ggwave.h
vendor_commit=060aec73dd7123ccac200442f75bdc7369795ffe
if [[ ! -f "$vendor_file" || ! -f "$vendor_header" ]]; then
    echo "ERROR: pinned ggwave donor source or header is missing" >&2
    exit 2
fi
if ! rg -q "$vendor_commit" docs/development/sonic-ggwave-profile.md; then
    echo "ERROR: ggwave donor pin differs from the Sonic Link requirement" >&2
    exit 2
fi

vendor_includes=(-Icomponents/sonic_ggwave_profile/include -Icomponents/sonic_core/include \
    -Itests -Iggwave_vendor/include -Iggwave_vendor/src -Iggwave_vendor/src/reed-solomon)
"$emcc_bin" -std=c11 -O2 -Icomponents/sonic_core/include -Itests \
    -c tests/sonic_golden_vectors.c -o "$build_dir/sonic_golden_vectors.o"
"$emcc_bin" -std=c11 -O2 -Icomponents/sonic_core/include \
    -c components/sonic_core/sonic_core.c -o "$build_dir/sonic_core.o"
"$emxx_bin" -std=c++11 -O2 "${vendor_includes[@]}" \
    tests/test_ggwave_wasm.cpp components/sonic_ggwave_profile/sonic_ggwave_profile.cpp \
    ggwave_vendor/src/ggwave.cpp "$build_dir/sonic_golden_vectors.o" "$build_dir/sonic_core.o" \
    -sENVIRONMENT=node -sEXIT_RUNTIME=1 -sINITIAL_MEMORY=33554432 \
    -o "$build_dir/test_ggwave_wasm.js"
"$node_bin" "$build_dir/test_ggwave_wasm.js"
echo "G06: pinned ggwave stock WASM binary round-trip PASS"
