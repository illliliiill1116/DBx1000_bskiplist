set -uo pipefail

ok=0
missing=0

check_tool() {
    local name="$1"
    if command -v "$name" >/dev/null 2>&1; then
        printf "  OK      %s (%s)\n" "$name" "$(command -v "$name")"
        ok=$((ok + 1))
    else
        printf "  MISSING %s\n" "$name"
        missing=$((missing + 1))
    fi
}

check_header() {
    local label="$1"; shift
    for h in "$@"; do
        local found
        found=$(find /usr/include /usr/local/include -path "*/$h" 2>/dev/null | head -1)
        if [ -n "$found" ]; then
            printf "  OK      %-28s %s\n" "$label" "$found"
            ok=$((ok + 1))
            return
        fi
    done
    printf "  MISSING %-28s (looked for: %s)\n" "$label" "$*"
    missing=$((missing + 1))
}

check_lib() {
    local label="$1"; shift
    for l in "$@"; do
        if ldconfig -p 2>/dev/null | grep -qi "$l"; then
            printf "  OK      %-28s (found %s via ldconfig)\n" "$label" "$l"
            ok=$((ok + 1))
            return
        fi
    done
    printf "  MISSING %-28s (looked for: %s)\n" "$label" "$*"
    missing=$((missing + 1))
}

echo "== build tools =="
check_tool cmake
check_tool ninja
check_tool pkg-config
check_tool git
check_tool g++

echo ""
echo "== headers (dev packages) =="
check_header "boost (>=1.69)"       "boost/version.hpp"
check_header "boost/context"        "boost/context/detail/fcontext.hpp"
check_header "boost/filesystem"     "boost/filesystem.hpp"
check_header "boost/program_options" "boost/program_options.hpp"
check_header "boost/regex"          "boost/regex.hpp"
check_header "double-conversion"    "double-conversion/double-conversion.h"
check_header "libevent"             "event.h" "event2/event.h"
check_header "gflags"               "gflags/gflags.h"
check_header "glog"                 "glog/logging.h"
check_header "icu"                  "unicode/utypes.h"
check_header "lz4"                  "lz4.h"
check_header "lzma"                 "lzma.h"
check_header "snappy"               "snappy.h"
check_header "openssl"              "openssl/ssl.h"
check_header "zlib"                 "zlib.h"
check_header "fmt"                  "fmt/format.h"
echo "  --      fast_float                   not checked -- setup_folly.sh vendors this automatically if missing"

echo ""
echo "== runtime libs =="
check_lib "boost_context"           "libboost_context"
check_lib "boost_filesystem"        "libboost_filesystem"
check_lib "boost_program_options"   "libboost_program_options"
check_lib "boost_regex"             "libboost_regex"
check_lib "double-conversion"       "libdouble-conversion"
check_lib "event"                   "libevent"
check_lib "gflags"                  "libgflags"
check_lib "glog"                    "libglog"
check_lib "icuuc / icudata"         "libicuuc" "libicudata"
check_lib "lz4"                     "liblz4"
check_lib "lzma"                    "liblzma"
check_lib "snappy"                  "libsnappy"
check_lib "ssl / crypto"            "libssl" "libcrypto"
check_lib "z"                       "libz.so"
check_lib "fmt"                     "libfmt"

echo ""
echo "================================================================"
echo "OK: $ok, MISSING: $missing"
