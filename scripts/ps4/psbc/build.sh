#!/bin/sh
# Builds host tools for compiling PS4 shaders: opengnm-psbc (SPIR-V -> PS4 shader binary)
# with our patch, and glslang (GLSL -> SPIR-V). Only needed to change shaders, compiled
# shaders are kept in the repository.
#
# Usage: scripts/ps4/psbc/build.sh [output directory]
#   Prints paths to use as PSBC and GLSLANG for scripts/ps4/gnm/compile-shaders.sh.
#
# psbc-ps4.patch:
#   - builds descriptor set 0 layout from shader resources, without it radv turns
#     every uniform buffer or texture access into a load through a null descriptor;
#     binding N is at offset N * 64, see psbc_build_set_layout()
#   - assigns VS parameter exports and fragment shader input attribute indices,
#     radv does both when linking a pipeline
#   - GFX7/GFX8 register limits as in Mesa, old ones failed register allocation
#   - no NEON blake3 source on x86
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
OUT=${1:-$ROOT/build-psbc}

PSBC_REPO=https://github.com/PS4-OpenGNM/opengnm-psbc
PSBC_COMMIT=a92a1228ea3a64e4be9f0e61c2a65a5aa7ffed92
OPENGNM_REPO=https://github.com/PS4-OpenGNM/opengnm
OPENGNM_COMMIT=4b295ca54c82c83acf308d1c646a2dfa9ae57350
VKHEADERS_REPO=https://github.com/KhronosGroup/Vulkan-Headers
VKHEADERS_COMMIT=c46850864f4661461b0f6cb9922c058ffea4915e
SPIRVHEADERS_REPO=https://github.com/KhronosGroup/SPIRV-Headers
SPIRVHEADERS_COMMIT=86f980c731e62ae4eaf383d320449d71687936bf
GLSLANG_REPO=https://github.com/KhronosGroup/glslang
GLSLANG_COMMIT=37b5bd7264c2c4acfa6f11826c29a16ab6a2a699

JOBS=$(nproc 2>/dev/null || echo 2)

fetch()
{
	dir="$OUT/$1"
	if [ "$(git -C "$dir" rev-parse HEAD 2>/dev/null)" != "$3" ]; then
		rm -rf "$dir"
		git init -q "$dir"
		git -C "$dir" fetch -q --depth 1 "$2" "$3"
		git -C "$dir" checkout -q FETCH_HEAD
	fi
	echo "  $1 $(git -C "$dir" rev-parse --short HEAD)"
}

python3 -c "import mako" 2>/dev/null || {
	echo "Python mako module is needed for Mesa code generators (pip install mako)" >&2
	exit 1
}

echo "=== Fetching sources"
mkdir -p "$OUT"
# psbc expects opengnm and Khronos headers next to it
fetch opengnm-psbc "$PSBC_REPO" "$PSBC_COMMIT"
fetch opengnm "$OPENGNM_REPO" "$OPENGNM_COMMIT"
fetch Vulkan-Headers "$VKHEADERS_REPO" "$VKHEADERS_COMMIT"
fetch SPIRV-Headers "$SPIRVHEADERS_REPO" "$SPIRVHEADERS_COMMIT"
fetch glslang "$GLSLANG_REPO" "$GLSLANG_COMMIT"

echo "=== Building glslang"
if [ ! -x "$OUT/glslang/build/StandAlone/glslang" ]; then
	cmake -S "$OUT/glslang" -B "$OUT/glslang/build" -DCMAKE_BUILD_TYPE=Release \
		-DENABLE_OPT=OFF -DGLSLANG_TESTS=OFF -DENABLE_HLSL=OFF > "$OUT/glslang-build.log" 2>&1
	cmake --build "$OUT/glslang/build" --target glslang-standalone -j "$JOBS" >> "$OUT/glslang-build.log" 2>&1 || {
		tail -n 30 "$OUT/glslang-build.log"
		exit 1
	}
fi

echo "=== Building opengnm-psbc"
PSBC="$OUT/opengnm-psbc"
cd "$PSBC"
if ! git apply --reverse --check "$HERE/psbc-ps4.patch" 2>/dev/null; then
	git checkout -q -- .
	git apply "$HERE/psbc-ps4.patch"
fi
cp "$HERE/config-host.mak" config.mak

# Mesa generated sources which upstream Makefile has no rules for (see meson.build files)
S=src
P=$S/amd/packets
python3 $S/util/format/u_format_table.py $S/util/format/u_format.yaml --enums > $S/util/format/u_format_gen.h
python3 $S/util/format/u_format_table.py $S/util/format/u_format.yaml --header > $S/util/format/u_format_pack.h
python3 $S/util/format/u_format_table.py $S/util/format/u_format.yaml > $S/util/format/u_format_table.c
python3 $S/util/format_srgb.py > $S/util/format_srgb.c
python3 $S/util/process_shader_stats.py $S/util/shader_stats.rnc $S/util/shader_stats.xml > $S/util/shader_stats.h 2>/dev/null
python3 $S/compiler/builtin_types_h.py $S/compiler/builtin_types.h
python3 $S/compiler/builtin_types_c.py $S/compiler/builtin_types.c
python3 $S/vulkan/util/vk_struct_type_cast_gen.py --xml ../Vulkan-Headers/registry/vk.xml --out $S/vulkan/util/vk_struct_type_cast.h --beta false
python3 $S/amd/common/gfx10_format_table.py $S/util/format/u_format.yaml $S/amd/registers/gfx10-rsrc.json $S/amd/registers/gfx11-rsrc.json > $S/amd/common/gfx10_format_table.c
for g in gfx11 gfx12; do
	python3 $P/parse_cp_pm4_table_data_json.py $P/cp_pm4_table_data_gfx11.json $P/pm4_it_opcodes_gfx11.h \
		$P/cp_pm4_table_data_gfx12.json $P/pm4_it_opcodes_gfx12.h $g packets_h > $S/amd/common/amd_cp_packets_$g.h
done

# generated headers have to exist before parallel compilation starts
make generated > "$OUT/psbc-build.log" 2>&1
make -j "$JOBS" >> "$OUT/psbc-build.log" 2>&1 || {
	grep -E "error" "$OUT/psbc-build.log" | head -n 20
	echo "opengnm-psbc build failed, full log: $OUT/psbc-build.log" >&2
	exit 1
}

echo "Done:"
echo "  PSBC=$PSBC/opengnm-psbc"
echo "  GLSLANG=$OUT/glslang/build/StandAlone/glslang"
