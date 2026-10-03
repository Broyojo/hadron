#!/usr/bin/env bash
# Run a Khronos conformance suite against Hadron's drivers, with deqp-runner: tests run in
# parallel in groups, each test has a timeout, and a crash or hang costs one group.
#
#   scripts/cts.sh vk    [options]   Vulkan CTS on KosmicKrisp           (build/vk-gl-cts)
#   scripts/cts.sh vk-full [options] the same, one run per list of the must-pass set: a stop loses
#                                    one list, and the same command continues where it stopped
#   scripts/cts.sh gles2 [options]   OpenGL ES 2 CTS on Zink/KosmicKrisp (build/gl-cts)
#   scripts/cts.sh gles3 [options]   OpenGL ES 3 CTS on Zink/KosmicKrisp
#   scripts/cts.sh gles31 [options]  OpenGL ES 3.1 CTS on Zink/KosmicKrisp
#   scripts/cts.sh gl33 [options]    OpenGL 3.3 CTS on Zink/KosmicKrisp (gl30 to gl46: that version's list)
#   scripts/cts.sh gles3-khr [options]  the Khronos half of the ES suites (gles2-khr, gles3-khr, gles31-khr)
#
# Options:
#   --name NAME        results go to build/cts-results/NAME (default: the suite and the date)
#   --fraction N       run one test in N, for a quick sample or to measure the rate
#   --caselist FILE    run these tests instead of the suite's must-pass list
#   --jobs N           parallel test processes (default: half the cores, the GPU is shared)
#   --baseline FILE    a failures.csv from an earlier run: only changes against it are reported
#   --env NAME=VALUE   one more environment variable for the test processes (repeatable)
#
# Results: results.csv (one line per test), failures.csv (everything that did not pass or skip).
# A full Vulkan run is about 3.2 million tests: run it when the Mac is not in use.
#
# scripts/build-cts.sh builds the suites and deqp-runner. docs/conformance.md has the results.
source "$(dirname "$0")/env.sh"

suite="${1:-}"; shift || true
name= fraction=1 caselists=() jobs=$(( $(sysctl -n hw.ncpu) / 2 )) baseline= extra_env=()
while (( $# )); do
    case "$1" in
        --name) name="$2"; shift 2 ;;
        --fraction) fraction="$2"; shift 2 ;;
        --caselist) caselists+=("$2"); shift 2 ;;
        --jobs) jobs="$2"; shift 2 ;;
        --baseline) baseline="$2"; shift 2 ;;
        --env) extra_env+=("$2"); shift 2 ;;
        *) die "unknown option $1" ;;
    esac
done

runner="$BUILD/deqp-runner/bin/deqp-runner"
[[ -x "$runner" ]] || die "missing $runner, run scripts/build-cts.sh"
# VK_DRIVER_FILES tests another KosmicKrisp build, one that is not installed for instance
icd="${VK_DRIVER_FILES:-$DIST/mesa/share/vulkan/icd.d/kosmickrisp_mesa_icd.aarch64.json}"
[[ -f "$icd" ]] || die "missing $icd, run scripts/build-vulkan.sh mesa"
cts="$SRC/vk-gl-cts"
env=(VK_DRIVER_FILES="$icd" MESA_KK_EXPERIMENTAL="${MESA_KK_EXPERIMENTAL:-dgc,xfb,sparse}")
args=(--deqp-log-images=disable --deqp-log-shader-sources=disable)

case "$suite" in
    vk-full)
        name="${name:-vk-full}"
        while read -r f; do
            [[ -n "$f" ]] || continue
            list="${f%.txt}"; list="${list//\//-}"
            [[ -f "$BUILD/cts-results/$name/$list/results.csv" ]] && continue
            "$0" vk --name "$name/$list" --jobs "$jobs" --fraction "$fraction" \
                --caselist "$cts/external/vulkancts/mustpass/main/$f" || true
        done < "$cts/external/vulkancts/mustpass/main/vk-default.txt"
        cat "$BUILD/cts-results/$name"/*/results.csv > "$BUILD/cts-results/$name/results.csv"
        cat "$BUILD/cts-results/$name"/*/failures.csv > "$BUILD/cts-results/$name/failures.csv"
        log "statuses: $(cut -d, -f2 "$BUILD/cts-results/$name/results.csv" | sort | uniq -c | tr '\n' ' ')"
        exit 0
        ;;
    vk)
        deqp="$BUILD/vk-gl-cts/external/vulkancts/modules/vulkan/deqp-vk"
        if (( ${#caselists[@]} == 0 )); then
            while read -r f; do
                [[ -n "$f" ]] && caselists+=("$cts/external/vulkancts/mustpass/main/$f")
            done < "$cts/external/vulkancts/mustpass/main/vk-default.txt"
        fi
        env+=(DYLD_FALLBACK_LIBRARY_PATH="$BREW/lib")
        ;;
    gles2|gles3|gles31|gl[34][0-9]|gles*-khr)
        [[ -f "$DIST/mesa-zink/lib/libEGL.1.dylib" ]] || die "missing Zink, run scripts/build-vulkan.sh zink"
        khr="$cts/external/openglcts/data/gl_cts/data/mustpass"
        case "$suite" in
            gles*-khr) deqp="$BUILD/gl-cts/external/openglcts/modules/glcts"
                       default=("$khr/gles/khronos_mustpass/main/$suite-main.txt") ;;
            gl[34][0-9]) deqp="$BUILD/gl-cts/external/openglcts/modules/glcts"
                       default=("$khr/gl/khronos_mustpass/main/$suite-main.txt") ;;
            # the must-pass list is split by the year its tests were added
            *)         deqp="$BUILD/gl-cts/modules/$suite/deqp-$suite"
                       default=("$cts"/android/cts/main/$suite-main-*.txt) ;;
        esac
        if (( ${#caselists[@]} == 0 )); then
            for f in "${default[@]}"; do [[ -s "$f" ]] && caselists+=("$f"); done
        fi
        # The suite's EGL platform loads these names; build/gl-cts/lib links them to Zink's.
        env+=(MESA_LOADER_DRIVER_OVERRIDE=zink DYLD_FALLBACK_LIBRARY_PATH="$BUILD/gl-cts/lib:$BREW/lib")
        args+=(--deqp-surface-type=pbuffer --deqp-surface-width=256 --deqp-surface-height=256
               --deqp-gl-config-name=rgba8888d24s8ms0)
        ;;
    *) die "usage: $0 vk|gles2|gles3|gles31|gl33|gles3-khr [--name NAME] [--fraction N] [--caselist FILE] [--jobs N] [--baseline FILE]" ;;
esac
[[ -x "$deqp" ]] || die "missing $deqp, run scripts/build-cts.sh"
(( ${#caselists[@]} )) || die "no case list for $suite"

out="$BUILD/cts-results/${name:-$suite-$(date +%Y%m%d-%H%M)}"
mkdir -p "$out"
# One list without repeats: a test named in two lists stops the group of tests it is run in.
awk '!seen[$0]++' "${caselists[@]}" > "$out/caselist.txt"
cmd=("$runner" run --deqp "$deqp" --output "$out" --jobs "$jobs" --timeout 120 --fraction "$fraction"
     --caselist "$out/caselist.txt")
for e in "${env[@]}" ${extra_env[@]+"${extra_env[@]}"}; do cmd+=(--env "$e"); done
[[ -n "$baseline" ]] && cmd+=(--baseline "$baseline")
log "$suite: ${#caselists[@]} case list(s), 1 test in $fraction, $jobs jobs, results in $out"
status=0
nice -n 10 "${cmd[@]}" -- "${args[@]}" || status=$?
log "statuses: $(cut -d, -f2 "$out/results.csv" 2>/dev/null | sort | uniq -c | tr '\n' ' ')"
exit $status
