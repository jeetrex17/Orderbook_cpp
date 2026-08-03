#!/usr/bin/env bash
#
# Cache and instruction profiling for the order book.
#
#   ./tools/profile/run.sh <tag> [--orders N]
#
# Writes cachegrind/callgrind output plus annotated text reports to
# tools/profile/results/<tag>/. Run it once before an optimisation and once
# after, then diff the summaries.
#
# Cachegrind SIMULATES a cache -- it does not read hardware counters. That makes
# its numbers deterministic and comparable across runs and machines, which is
# exactly what an A/B comparison needs, but it is not wall-clock truth. Always
# pair it with a native timed run of the same benchmark.

set -euo pipefail

TAG="${1:?usage: run.sh <tag> [--orders N]}"
shift || true

ORDERS=100000
while [ $# -gt 0 ]; do
  case "$1" in
    --orders) ORDERS="$2"; shift 2 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="${REPO_ROOT}/tools/profile/results/${TAG}"
mkdir -p "${OUT_DIR}"

# The simulated cache geometry is pinned rather than autodetected. Cachegrind
# guesses from the host CPU, which is unreliable inside a container and differs
# between machines -- and a before/after comparison is meaningless if the two
# runs modelled different caches. These values (32K L1i, 64K L1d, 8M LL, 64-byte
# lines) approximate an Apple M-series performance core.
CACHE_ARGS=(--I1=32768,8,64 --D1=65536,8,64 --LL=8388608,16,64)

# The naive baseline is O(N) per order and would dominate every profile, so the
# profiling runs measure OrderBook alone.
#
# Valgrind profiles the whole process, including the one-off order generation
# that happens before the timed section -- at --runs 1 the Mersenne twister was
# 12% of all instructions. Replaying the same orders several times amortises
# that down to a few percent so the numbers are dominated by matching.
BENCH_ARGS=(--orders "${ORDERS}" --runs 3 --baseline no)

cd "${REPO_ROOT}"

echo "==> Configuring (RelWithDebInfo, frame pointers)"
cmake --preset profile >/dev/null
cmake --build --preset profile >/dev/null
BENCH="${REPO_ROOT}/build/profile/orderbook_bench"

echo "==> cachegrind"
valgrind --tool=cachegrind --cache-sim=yes "${CACHE_ARGS[@]}" \
  --cachegrind-out-file="${OUT_DIR}/cachegrind.out" \
  "${BENCH}" "${BENCH_ARGS[@]}" 2> "${OUT_DIR}/cachegrind.log"
cg_annotate "${OUT_DIR}/cachegrind.out" > "${OUT_DIR}/cachegrind.txt"

echo "==> callgrind"
valgrind --tool=callgrind \
  --callgrind-out-file="${OUT_DIR}/callgrind.out" \
  "${BENCH}" "${BENCH_ARGS[@]}" 2> "${OUT_DIR}/callgrind.log"
callgrind_annotate "${OUT_DIR}/callgrind.out" > "${OUT_DIR}/callgrind.txt"

echo
echo "==> Summary for '${TAG}' (${ORDERS} orders)"
# Valgrind prefixes every line with ==<pid>==; strip it for readability.
sed -n 's/^==[0-9]*== \(\(I\|D\|LL\)[a-z0-9]* *\(refs\|misses\|miss rate\).*\)/  \1/p' \
  "${OUT_DIR}/cachegrind.log" || true

echo
echo "Full reports in ${OUT_DIR}/"
