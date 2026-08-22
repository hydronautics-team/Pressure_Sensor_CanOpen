#!/usr/bin/env bash

set -euo pipefail

readonly project_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_directory="${1:-${project_root}/build/Debug}"

if [[ "${build_directory}" != /* ]]; then
    build_directory="${project_root}/${build_directory}"
fi

readonly compile_commands="${build_directory}/compile_commands.json"
readonly clang_tidy="${CLANG_TIDY_EXECUTABLE:-clang-tidy}"
readonly arm_compiler="${ARM_CXX_COMPILER:-arm-none-eabi-g++}"

if ! command -v "${clang_tidy}" >/dev/null 2>&1; then
    echo "clang-tidy executable not found: ${clang_tidy}" >&2
    exit 1
fi

if ! command -v "${arm_compiler}" >/dev/null 2>&1; then
    echo "ARM C++ compiler not found: ${arm_compiler}" >&2
    exit 1
fi

if [[ ! -f "${compile_commands}" ]]; then
    echo "Compilation database not found: ${compile_commands}" >&2
    echo "Run 'cmake --preset Debug' first." >&2
    exit 1
fi

mapfile -t arm_system_includes < <(
    "${arm_compiler}" -mcpu=cortex-m3 -E -x c++ - -v </dev/null 2>&1 \
        | awk '/search starts here:/{capture=1; next} /End of search list/{capture=0} capture{sub(/^ /, ""); print}'
)

if (( ${#arm_system_includes[@]} == 0 )); then
    echo "Failed to determine system include directories from ${arm_compiler}." >&2
    exit 1
fi

extra_arguments=()
for include_directory in "${arm_system_includes[@]}"; do
    extra_arguments+=("--extra-arg=-isystem${include_directory}")
done

readonly source_files=(
    "${project_root}/Core/Src/ADS1220.cpp"
    "${project_root}/Core/Src/main.cpp"
)

exec "${clang_tidy}" \
    --quiet \
    --config-file="${project_root}/.clang-tidy" \
    -p "${build_directory}" \
    "${extra_arguments[@]}" \
    "${source_files[@]}"
