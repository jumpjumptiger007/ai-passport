#!/usr/bin/env bash
set -euo pipefail

mode="${1:---all}"
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    echo "Usage: $0 [--all|--static|--firmware]" >&2
}

run_static_checks() {
    local actionlint_bin
    local test_dir
    local gc_sections_ld_flag="-Wl,--gc-sections"

    if [[ "$(uname -s)" == "Darwin" ]]; then
        gc_sections_ld_flag="-Wl,-dead_strip"
    fi

    python3 tools/check_repo.py

    actionlint_bin="${ACTIONLINT_BIN:-}"
    if [[ -z "${actionlint_bin}" ]]; then
        actionlint_bin="$(command -v actionlint || true)"
    fi
    if [[ -z "${actionlint_bin}" || ! -x "${actionlint_bin}" ]]; then
        actionlint_bin="$(./tools/install-actionlint.sh)"
    fi
    "${actionlint_bin}" -color .github/workflows/*.yml

    test_dir="$(mktemp -d /tmp/ai-passport-host-tests.XXXXXX)"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_ui_pixel_math.c main/ui_pixel_math.c \
        -o "${test_dir}/test_ui_pixel_math"
    "${test_dir}/test_ui_pixel_math"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_demo_navigation.c main/demo_navigation.c \
        -o "${test_dir}/test_demo_navigation"
    "${test_dir}/test_demo_navigation"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Icomponents/bsp/src \
        tests/test_bsp_display_rounding.c components/bsp/src/bsp_display_rounding.c \
        -o "${test_dir}/test_bsp_display_rounding"
    "${test_dir}/test_bsp_display_rounding"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Icomponents/bsp/src \
        tests/test_bsp_es8311_sleep_check.c components/bsp/src/bsp_es8311_sleep_check.c \
        -o "${test_dir}/test_bsp_es8311_sleep_check"
    "${test_dir}/test_bsp_es8311_sleep_check"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/bsp_stubs -Icomponents/bsp/include \
        tests/test_bsp_button.c -o "${test_dir}/test_bsp_button"
    "${test_dir}/test_bsp_button"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/bsp_stubs -Icomponents/bsp/include \
        tests/test_bsp_lvgl_init.c components/bsp/src/bsp_display_rounding.c \
        -o "${test_dir}/test_bsp_lvgl_init"
    "${test_dir}/test_bsp_lvgl_init"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/audio_stubs -Icomponents/bsp/include -Icomponents/bsp/src \
        tests/test_bsp_audio_recovery.c components/bsp/src/bsp_es8311_sleep_check.c \
        -o "${test_dir}/test_bsp_audio_recovery"
    "${test_dir}/test_bsp_audio_recovery"
    for demo in audio low_power ble wifi; do
        "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
            -ffunction-sections -fdata-sections -Itests/demo_stubs -Imain \
            "tests/test_demo_${demo}_runtime.c" "${gc_sections_ld_flag}" \
            -o "${test_dir}/test_demo_${demo}_runtime"
        "${test_dir}/test_demo_${demo}_runtime"
    done
    for sonic_test in frame fragmentation reassembly device_info; do
        "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
            -Icomponents/sonic_core/include -Itests \
            "tests/test_sonic_${sonic_test}.c" tests/sonic_golden_vectors.c \
            components/sonic_core/sonic_core.c \
            -o "${test_dir}/test_sonic_${sonic_test}"
        "${test_dir}/test_sonic_${sonic_test}"
    done
    PYTHONDONTWRITEBYTECODE=1 python3 tools/test_sonic_demo_url.py
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Icomponents/sonic_runtime/include -Icomponents/sonic_core/include \
        tests/test_sonic_runtime.c components/sonic_runtime/sonic_runtime.c \
        components/sonic_core/sonic_core.c \
        -o "${test_dir}/test_sonic_runtime"
    "${test_dir}/test_sonic_runtime"
    echo "test_sonic_runtime: PASS"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Imain -Icomponents/sonic_core/include \
        tests/test_sonic_ui_paging.c main/sonic_ui_paging.c \
        -o "${test_dir}/test_sonic_ui_paging"
    "${test_dir}/test_sonic_ui_paging"
    echo "test_sonic_ui_paging: PASS"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Imain tests/test_sonic_app_fingerprint.c \
        main/sonic_app_fingerprint.c \
        -o "${test_dir}/test_sonic_app_fingerprint"
    "${test_dir}/test_sonic_app_fingerprint"
    echo "test_sonic_app_fingerprint: PASS"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Imain tests/test_sonic_memory_gate.c main/sonic_memory_gate.c \
        -o "${test_dir}/test_sonic_memory_gate"
    "${test_dir}/test_sonic_memory_gate"
    echo "test_sonic_memory_gate: PASS"
    "${CXX:-c++}" -std=c++11 -Wall -Wextra -Werror \
        -Icomponents/sonic_ggwave_profile/include -Iggwave_vendor/include \
        -Iggwave_vendor/src -Iggwave_vendor/src/reed-solomon \
        -c tests/test_ggwave_profile.cpp -o "${test_dir}/test_ggwave_profile_test.o"
    "${CXX:-c++}" -std=c++11 -Wall -Wextra -Werror \
        -Icomponents/sonic_ggwave_profile/include -Iggwave_vendor/include \
        -Iggwave_vendor/src -Iggwave_vendor/src/reed-solomon \
        -c components/sonic_ggwave_profile/sonic_ggwave_profile.cpp \
        -o "${test_dir}/sonic_ggwave_profile.o"
    "${CXX:-c++}" -std=c++11 \
        -Iggwave_vendor/include -Iggwave_vendor/src -Iggwave_vendor/src/reed-solomon \
        -c ggwave_vendor/src/ggwave.cpp -o "${test_dir}/ggwave.o"
    "${CXX:-c++}" "${test_dir}/test_ggwave_profile_test.o" \
        "${test_dir}/sonic_ggwave_profile.o" "${test_dir}/ggwave.o" \
        -o "${test_dir}/test_ggwave_profile"
    "${test_dir}/test_ggwave_profile"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Icomponents/sonic_core/include -Itests \
        -c tests/sonic_golden_vectors.c -o "${test_dir}/sonic_golden_vectors.o"
    "${CXX:-c++}" -std=c++11 -Wall -Wextra -Werror \
        -Icomponents/sonic_tone_renderer/include -Icomponents/sonic_ggwave_profile/include \
        -Icomponents/sonic_core/include -Itests -Iggwave_vendor/include \
        -Iggwave_vendor/src -Iggwave_vendor/src/reed-solomon \
        -c tests/test_tone_renderer.cpp -o "${test_dir}/test_tone_renderer.o"
    "${CXX:-c++}" -std=c++11 -Wall -Wextra -Werror \
        -Icomponents/sonic_tone_renderer/include -Iggwave_vendor/include \
        -Iggwave_vendor/src -Iggwave_vendor/src/reed-solomon \
        -c components/sonic_tone_renderer/sonic_tone_renderer.cpp \
        -o "${test_dir}/sonic_tone_renderer.o"
    "${CXX:-c++}" "${test_dir}/test_tone_renderer.o" \
        "${test_dir}/sonic_tone_renderer.o" "${test_dir}/sonic_ggwave_profile.o" \
        "${test_dir}/ggwave.o" "${test_dir}/sonic_golden_vectors.o" \
        -o "${test_dir}/test_tone_renderer"
    "${test_dir}/test_tone_renderer"
    sonic_cpp_includes=(
        -Icomponents/sonic_audio/include -Icomponents/sonic_codec/include
        -Icomponents/sonic_tone_renderer/include -Icomponents/sonic_ggwave_profile/include
        -Icomponents/sonic_core/include -Itests -Iggwave_vendor/include
        -Iggwave_vendor/src -Iggwave_vendor/src/reed-solomon
    )
    "${CXX:-c++}" -std=c++11 -Wall -Wextra -Werror "${sonic_cpp_includes[@]}" \
        -c components/sonic_codec/sonic_codec.cpp -o "${test_dir}/sonic_codec.o"
    "${CXX:-c++}" -std=c++11 -Wall -Wextra -Werror "${sonic_cpp_includes[@]}" \
        -c components/sonic_audio/sonic_audio_engine.cpp -o "${test_dir}/sonic_audio_engine.o"
    "${CXX:-c++}" -std=c++11 -Wall -Wextra -Werror "${sonic_cpp_includes[@]}" \
        -c tests/test_sonic_codec.cpp -o "${test_dir}/test_sonic_codec.o"
    "${CXX:-c++}" -std=c++11 "${test_dir}/test_sonic_codec.o" \
        "${test_dir}/sonic_codec.o" "${test_dir}/sonic_tone_renderer.o" \
        "${test_dir}/sonic_ggwave_profile.o" "${test_dir}/ggwave.o" \
        "${test_dir}/sonic_golden_vectors.o" -o "${test_dir}/test_sonic_codec"
    "${test_dir}/test_sonic_codec"
    "${CXX:-c++}" -std=c++11 -Wall -Wextra -Werror "${sonic_cpp_includes[@]}" \
        -c tests/test_sonic_audio.cpp -o "${test_dir}/test_sonic_audio.o"
    "${CXX:-c++}" -std=c++11 "${test_dir}/test_sonic_audio.o" \
        "${test_dir}/sonic_audio_engine.o" "${test_dir}/sonic_codec.o" \
        "${test_dir}/sonic_tone_renderer.o" "${test_dir}/sonic_ggwave_profile.o" \
        "${test_dir}/ggwave.o" "${test_dir}/sonic_golden_vectors.o" \
        -o "${test_dir}/test_sonic_audio"
    "${test_dir}/test_sonic_audio"
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_deep_sleep_contract.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_check_repo.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_verify_firmware.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_archive_firmware.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_install_passport_skills.py
    rm -rf "${test_dir}"
    echo "Host tests: PASS"
}

run_firmware_checks() (
    local validation_build_dir

    if ! command -v idf.py >/dev/null 2>&1; then
        echo "ERROR: idf.py is not available; activate ESP-IDF 5.5.3 first." >&2
        return 1
    fi

    validation_build_dir="$(mktemp -d /tmp/ai-passport-firmware.XXXXXX)"
    trap 'case "${validation_build_dir}" in /tmp/ai-passport-firmware.*) rm -rf -- "${validation_build_dir}" ;; esac' EXIT

    SDKCONFIG_DEFAULTS="${repo_root}/sdkconfig.defaults" \
        idf.py -B "${validation_build_dir}" \
        -D "SDKCONFIG=${validation_build_dir}/sdkconfig" build
    idf.py -B "${validation_build_dir}" merge-bin \
        -o "${validation_build_dir}/FoloToy-AI-Passport-full.bin"
    python3 tools/verify_firmware.py "${validation_build_dir}"
    PYTHONDONTWRITEBYTECODE=1 python3 tools/archive_firmware.py create \
        "${validation_build_dir}" --archive-root "${repo_root}/build/firmware"
    mkdir -p "${repo_root}/build"
    install -m 0644 \
        "${validation_build_dir}/FoloToy-AI-Passport-full.bin" \
        "${repo_root}/build/FoloToy-AI-Passport-full.bin"
    echo "Firmware build: PASS"
)

cd "${repo_root}"
case "${mode}" in
    --all)
        run_static_checks
        run_firmware_checks
        ;;
    --static)
        run_static_checks
        ;;
    --firmware)
        run_firmware_checks
        ;;
    *)
        usage
        exit 2
        ;;
esac
