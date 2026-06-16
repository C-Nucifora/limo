#!/usr/bin/env bash
#
# repro-capture.sh — build an instrumented Limo and capture everything needed to
# diagnose the runtime-only bugs (crashes, hangs, silent failures) that can't be
# reproduced without running the app.
#
# It builds a debug build with AddressSanitizer + UndefinedBehaviorSanitizer and
# libstdc++ assertions, launches Limo, and records:
#   - full stdout/stderr (incl. ASan/UBSan reports and Qt warnings)
#   - a crash backtrace (ASan prints one automatically; --gdb adds a gdb one)
#   - Limo's own rotating log files
#   - environment info (distro, Qt, libloot, flatpak?, filesystem types)
# into a timestamped folder, then tars it up so you can hand the archive back.
#
# Usage:
#   scripts/repro-capture.sh [--gdb] [--strace] [--bug <id>]
#
#   (no flag)   run the ASan/UBSan build directly (best for crashes/UB/leaks)
#   --gdb       run under gdb; on a crash it auto-prints a full backtrace, and for
#               a HANG you press Ctrl-C in the terminal then type:  thread apply all bt
#   --strace    also record syscalls (useful for hangs / "stuck" issues, large)
#   --bug <id>  just prints the repro steps for that issue id and exits
#
# Reproduce the bug in the GUI, then quit Limo (or Ctrl-C). The script prints the
# path to the captured archive at the end.
set -uo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEPS_PREFIX="$REPO_DIR/../limo-deps/prefix"
BUILD_DIR="$REPO_DIR/build-repro"
STAMP="$(date +%Y%m%d-%H%M%S)"
OUT_DIR="$REPO_DIR/repro-captures/$STAMP"

MODE="run"
STRACE=0
for arg in "$@"; do
  case "$arg" in
    --gdb) MODE="gdb" ;;
    --strace) STRACE=1 ;;
    --bug) shift; ;;  # handled below
  esac
done

# ---------------------------------------------------------------------------
# Per-bug repro steps (so a tester knows exactly what to do for each issue).
# ---------------------------------------------------------------------------
print_bug_steps() {
  case "${1:-}" in
    95)  echo "#95 API-key crash: Settings -> NexusMods -> enter an API key and confirm. Capture the crash." ;;
    120) echo "#120 infinite import loop: drag/import F4SE (0.6.23) or SKSE per the wiki; watch for the log repeating the same import. Quit once it loops." ;;
    121) echo "#121 forgets settings: set up an application + staging dir, close Limo, reopen. Note if it prompts for a new application again." ;;
    122) echo "#122 OpenMW disable: with an OpenMW app, disable an .esp (e.g. from Remiros' Groundcover), close & reopen Limo; note if it re-enabled." ;;
    136) echo "#136 Subnautica link-only: deploy a BepInEx mod (e.g. Nautilus) via Hard Link or Sym Link; launch the game; capture the BepInEx error." ;;
    138) echo "#138 staging-dir hang: add an application and pick a staging directory; if it hangs, run this script with --gdb and grab a backtrace." ;;
    93)  echo "#93 reverse-dependency case match: install a mod, then its dependency AFTER it, then deploy once (Skyrim SE, Case Matching Deployer)." ;;
    96)  echo "#96 orphaned files: deploy a mod, reinstall it from a different archive (fewer files), deploy again; note leftover/failed files." ;;
    *)   echo "Unknown bug id '$1'. Known: 95 120 121 122 136 138 93 96" ;;
  esac
}
args=("$@")
for ((i = 0; i < ${#args[@]}; i++)); do
  if [[ "${args[$i]}" == "--bug" ]]; then print_bug_steps "${args[$((i + 1))]:-}"; exit 0; fi
done

mkdir -p "$OUT_DIR"
echo "==> Capturing to: $OUT_DIR"

# ---------------------------------------------------------------------------
# 1. Environment info
# ---------------------------------------------------------------------------
{
  echo "date: $(date -u +%FT%TZ)"
  echo "kernel: $(uname -a)"
  echo "distro: $( (. /etc/os-release 2>/dev/null && echo "$PRETTY_NAME") || echo unknown )"
  echo "flatpak: $([ -f /.flatpak-info ] && echo yes || echo no)"
  echo "qt: $(pkg-config --modversion Qt6Core 2>/dev/null || echo '?')"
  echo "libloot: $(pacman -Q libloot 2>/dev/null || ls /usr/lib/libloot.so* 2>/dev/null || echo 'not found')"
  echo "compiler: $(c++ --version | head -1)"
  echo "--- limo config/log dirs ---"
  ls -la ~/.config/limo 2>/dev/null
  ls -la ~/.var/app/io.github.limo_app.limo/config/limo 2>/dev/null
  echo "--- mounts (for hard-link/cross-fs issues) ---"
  findmnt -t ext4,btrfs,xfs,ntfs,ntfs3,fuse.fuse-overlayfs,overlay -o TARGET,SOURCE,FSTYPE 2>/dev/null
} > "$OUT_DIR/environment.txt" 2>&1

# ---------------------------------------------------------------------------
# 2. Build an instrumented Limo (ASan + UBSan + libstdc++ assertions)
# ---------------------------------------------------------------------------
if [[ ! -x "$BUILD_DIR/Limo" ]]; then
  echo "==> Configuring instrumented build (ASan/UBSan + _GLIBCXX_ASSERTIONS)..."
  # ASan+UBSan give crash/UB backtraces. (_GLIBCXX_ASSERTIONS is intentionally NOT used: the
  # codebase has a constexpr std::array<std::string> that is ill-formed under those assertions.)
  SAN_FLAGS="-g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined"
  cmake -G Ninja -S "$REPO_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DLIMO_WITH_LOOT=ON \
    -DCMAKE_PREFIX_PATH="$DEPS_PREFIX" \
    -DCMAKE_CXX_FLAGS="$SAN_FLAGS" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" \
    > "$OUT_DIR/cmake-configure.log" 2>&1
fi
echo "==> Building Limo (this can take a few minutes)..."
if ! ninja -C "$BUILD_DIR" Limo > "$OUT_DIR/build.log" 2>&1; then
  echo "!! Build failed — see $OUT_DIR/build.log"; tail -20 "$OUT_DIR/build.log"; exit 1
fi

# ---------------------------------------------------------------------------
# 3. Run Limo, capturing everything
# ---------------------------------------------------------------------------
export ASAN_OPTIONS="abort_on_error=0:detect_leaks=0:log_path=$OUT_DIR/asan:halt_on_error=0:print_stacktrace=1"
export UBSAN_OPTIONS="print_stacktrace=1:log_path=$OUT_DIR/ubsan"
export QT_LOGGING_RULES="*=true"   # capture all Qt categorized logging
export LD_LIBRARY_PATH="$DEPS_PREFIX/lib:${LD_LIBRARY_PATH:-}"

RUN_LOG="$OUT_DIR/limo-stdout-stderr.txt"
echo "==> Launching Limo (debug mode). Reproduce the bug, then quit the app (or Ctrl-C)."
echo "    Output -> $RUN_LOG"

if [[ "$MODE" == "gdb" ]]; then
  if ! command -v gdb >/dev/null; then echo "gdb not installed (pacman -S gdb)"; exit 1; fi
  echo "    [gdb] On a crash a backtrace is printed automatically."
  echo "    [gdb] For a HANG: press Ctrl-C, then type:  thread apply all bt   then:  quit"
  gdb -q \
    -ex "set pagination off" \
    -ex "handle SIGPIPE nostop noprint pass" \
    -ex "run --debug" \
    -ex "thread apply all bt" \
    --args "$BUILD_DIR/Limo" --debug 2>&1 | tee "$RUN_LOG"
elif [[ "$STRACE" == "1" ]]; then
  command -v strace >/dev/null || { echo "strace not installed"; exit 1; }
  strace -f -tt -o "$OUT_DIR/strace.txt" "$BUILD_DIR/Limo" --debug 2>&1 | tee "$RUN_LOG"
else
  "$BUILD_DIR/Limo" --debug 2>&1 | tee "$RUN_LOG"
fi

# ---------------------------------------------------------------------------
# 4. Collect Limo's own logs and package the capture
# ---------------------------------------------------------------------------
for d in ~/.config/limo/logs ~/.var/app/io.github.limo_app.limo/config/limo/logs; do
  [ -d "$d" ] && cp -a "$d" "$OUT_DIR/limo-logs" 2>/dev/null && break
done

TARBALL="$REPO_DIR/repro-captures/limo-repro-$STAMP.tar.gz"
tar -czf "$TARBALL" -C "$REPO_DIR/repro-captures" "$STAMP" 2>/dev/null

echo
echo "==> Capture complete."
echo "    Folder:  $OUT_DIR"
echo "    Archive: $TARBALL"
echo "    Contents: environment.txt, build.log, limo-stdout-stderr.txt, asan.*/ubsan.* (if any), limo-logs/"
echo "    Send me the archive (or paste limo-stdout-stderr.txt + any asan.* file) for the bug you reproduced."
