# Sets OPENSSL_armcap for the Wine processes Hadron starts, unless it is set already. Sourced by
# scripts/play and, for the helpers it runs without play, scripts/steam-run.
#
# OpenSSL 1.1, which steamclient.dylib has built in, finds out what the CPU can do when it loads by
# blocking signals with sigprocmask() and trying instructions. On macOS sigprocmask() sets the mask
# of every thread in the process, so a Wine thread that saved its own mask meanwhile sets the
# probe's back afterwards and keeps SIGUSR1, SIGUSR2 and the rest blocked (docs/findings.md #80).
# Given OPENSSL_armcap, OpenSSL takes the features from it and doesn't probe. The value is NEON,
# AES, SHA-1, SHA-256 and PMULL as the kernel reports them: the probe's findings for those (it
# doesn't try SHA-512 on a Mac). The probe would also set ARMV7_TICK when the counter register
# can be read, which OpenSSL 1.1 uses only as a timer for its entropy, falling back to
# clock_gettime() without it; it is left out.
if [ -z "${OPENSSL_armcap:-}" ]; then
    hadron_armcap=0
    hadron_features=$(sysctl hw.optional.arm.AdvSIMD hw.optional.arm.FEAT_AES hw.optional.arm.FEAT_SHA1 \
        hw.optional.arm.FEAT_SHA256 hw.optional.arm.FEAT_PMULL 2>/dev/null)
    while read -r hadron_name hadron_value; do
        [ "$hadron_value" = 1 ] || continue
        case $hadron_name in
            hw.optional.arm.AdvSIMD:) hadron_armcap=$((hadron_armcap | 0x1)) ;;
            hw.optional.arm.FEAT_AES:) hadron_armcap=$((hadron_armcap | 0x4)) ;;
            hw.optional.arm.FEAT_SHA1:) hadron_armcap=$((hadron_armcap | 0x8)) ;;
            hw.optional.arm.FEAT_SHA256:) hadron_armcap=$((hadron_armcap | 0x10)) ;;
            hw.optional.arm.FEAT_PMULL:) hadron_armcap=$((hadron_armcap | 0x20)) ;;
        esac
    done <<EOF
$hadron_features
EOF
    OPENSSL_armcap=$(printf '0x%x' "$hadron_armcap")
    unset hadron_armcap hadron_features hadron_name hadron_value
fi
