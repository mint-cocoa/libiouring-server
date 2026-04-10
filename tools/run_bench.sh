#!/bin/bash
# run_bench.sh — ServerCore v4 전체 부하 테스트 자동화 스크립트
#
# Usage:
#   ./tools/run_bench.sh                    # 전체 테스트 (echo + game, 모든 아키텍처)
#   ./tools/run_bench.sh echo               # echo 벤치만
#   ./tools/run_bench.sh game               # game 벤치만
#   ./tools/run_bench.sh game integrated    # game 벤치 Integrated만
#   ./tools/run_bench.sh game separated     # game 벤치 Separated만
#
# 결과는 stdout + docs/benchmarks/v4-results/ 디렉토리에 로그 파일로 저장됩니다.

set -euo pipefail

# ── 경로 ─────────────────────────────────────────────────────────

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${PROJECT_DIR}/build"
BIN="${BUILD_DIR}/bin"
RESULTS_DIR="${PROJECT_DIR}/docs/benchmarks/v4-results"

# ── 설정 ─────────────────────────────────────────────────────────

ECHO_PORT=9100
GAME_PORT=7777
SERVER_STARTUP_WAIT=2   # 서버 시작 대기 (초)
SERVER_STOP_WAIT=2      # 서버 종료 대기 (초)
RAMP_DELAY=5            # 접속 간 지연 (ms)

# 스케일 정의: "LABEL:THREADS:CONNS:DURATION:WARMUP"
ECHO_SCALES=(
    "10:2:5:10:2"
    "50:5:10:15:3"
    "100:5:20:15:5"
    "200:10:20:15:5"
)

# 스케일 정의: "LABEL:THREADS:CONNS:DURATION:WARMUP:ROOMS"
GAME_SCALES=(
    # ── Baseline ──
    "40:1:40:15:3:0"
    "40:4:10:15:3:0"

    # ── Medium ──
    "200:2:100:15:5:0"
    "200:4:50:15:5:0"

    # ── High ──
    "400:4:100:20:5:0"
    "400:8:50:20:5:0"

    # ── Stress ──
    "800:4:200:20:5:0"
    "800:8:100:20:5:0"

    # ── Room split (200 bots → 10 rooms, 20 per room) ──
    "200:4:50:15:5:10"

    # ── Room split (400 bots → 20 rooms, 20 per room) ──
    "400:4:100:20:5:20"
)

# ── 유틸리티 ─────────────────────────────────────────────────────

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

log()      { echo -e "${CYAN}[$(date +%H:%M:%S)]${NC} $*"; }
log_ok()   { echo -e "${GREEN}[$(date +%H:%M:%S)] ✓${NC} $*"; }
log_err()  { echo -e "${RED}[$(date +%H:%M:%S)] ✗${NC} $*"; }
log_warn() { echo -e "${YELLOW}[$(date +%H:%M:%S)] !${NC} $*"; }

separator() {
    echo ""
    echo -e "${BOLD}══════════════════════════════════════════════════════════════${NC}"
    echo -e "${BOLD}  $*${NC}"
    echo -e "${BOLD}══════════════════════════════════════════════════════════════${NC}"
    echo ""
}

# ── 서버 관리 ────────────────────────────────────────────────────

SERVER_PID=""

start_server() {
    local binary="$1"
    local name="$(basename "$binary")"

    if [ ! -x "$binary" ]; then
        log_err "$name not found at $binary"
        return 1
    fi

    log "Starting $name..."
    "$binary" >/dev/null 2>&1 &
    SERVER_PID=$!
    sleep "$SERVER_STARTUP_WAIT"

    if kill -0 "$SERVER_PID" 2>/dev/null; then
        log_ok "$name started (PID: $SERVER_PID)"
        return 0
    else
        log_err "$name failed to start"
        SERVER_PID=""
        return 1
    fi
}

stop_server() {
    if [ -z "$SERVER_PID" ]; then return; fi

    local name="$1"
    log "Stopping $name (PID: $SERVER_PID)..."
    kill "$SERVER_PID" 2>/dev/null || true
    sleep "$SERVER_STOP_WAIT"
    kill -9 "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
    SERVER_PID=""
    log_ok "$name stopped"
}

cleanup() {
    if [ -n "$SERVER_PID" ]; then
        kill "$SERVER_PID" 2>/dev/null || true
        kill -9 "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT

# ── 빌드 확인 ────────────────────────────────────────────────────

check_build() {
    local missing=()

    for bin in echo_bench game_bench EchoServer \
               GameServerIntegrated GameServerSeparated GameServerEpoll; do
        if [ ! -x "${BIN}/${bin}" ]; then
            missing+=("$bin")
        fi
    done

    if [ ${#missing[@]} -gt 0 ]; then
        log_warn "Missing binaries: ${missing[*]}"
        log "Building..."
        cmake --build "$BUILD_DIR" -j"$(nproc)" 2>&1 | tail -3
        echo ""
    fi
}

# ── Echo 벤치마크 ────────────────────────────────────────────────

run_echo_bench() {
    local server_name="$1"
    local server_bin="${BIN}/${server_name}"
    local logfile="${RESULTS_DIR}/echo_${server_name}.log"

    separator "Echo Benchmark: ${server_name}"

    echo "# Echo Benchmark: ${server_name}" > "$logfile"
    echo "# Date: $(date -Iseconds)" >> "$logfile"
    echo "" >> "$logfile"

    for scale in "${ECHO_SCALES[@]}"; do
        IFS=':' read -r total threads conns duration warmup <<< "$scale"

        # 매 스케일마다 서버 재시작
        stop_server "$server_name" 2>/dev/null || true
        if ! start_server "$server_bin"; then
            log_err "Skipping $server_name @ ${total} clients"
            continue
        fi

        local label="${server_name}-${total}"
        log "Running: ${threads}T x ${conns}C = ${total} clients, ${duration}s (+${warmup}s warmup)"

        local output
        output=$("${BIN}/echo_bench" \
            --port "$ECHO_PORT" \
            --threads "$threads" \
            --conns "$conns" \
            --duration "$duration" \
            --warmup "$warmup" \
            --pipeline 1 \
            --label "$label" 2>&1)

        echo "$output" >> "$logfile"
        echo "" >> "$logfile"

        # 핵심 결과만 추출하여 터미널에 표시
        echo "$output" | grep -E "(=== |Config:|Connections:|Throughput:|p50|p99|min:)" | head -8
        echo ""
    done

    stop_server "$server_name"
    log_ok "Results saved: $logfile"
}

run_all_echo() {
    if [ -x "${BIN}/EchoServer" ]; then
        run_echo_bench "EchoServer"
    else
        log_warn "Skipping EchoServer (binary not found)"
    fi
}

# ── Game 벤치마크 ────────────────────────────────────────────────

run_game_bench() {
    local server_name="$1"
    local server_bin="${BIN}/${server_name}"
    local logfile="${RESULTS_DIR}/game_${server_name}.log"

    separator "Game Benchmark: ${server_name}"

    echo "# Game Benchmark: ${server_name}" > "$logfile"
    echo "# Date: $(date -Iseconds)" >> "$logfile"
    echo "" >> "$logfile"

    for scale in "${GAME_SCALES[@]}"; do
        IFS=':' read -r total threads conns duration warmup rooms <<< "$scale"
        rooms="${rooms:-0}"

        # 매 스케일마다 서버 재시작
        stop_server "$server_name" 2>/dev/null || true
        if ! start_server "$server_bin"; then
            log_err "Skipping $server_name @ ${total} clients"
            continue
        fi

        local room_tag=""
        if [ "$rooms" -gt 0 ]; then
            room_tag="_${rooms}rooms"
        fi
        local label="${server_name}-${total}_${threads}t${room_tag}"
        log "Running: ${threads}T x ${conns}C = ${total} bots, rooms=${rooms}, ${duration}s (+${warmup}s warmup)"

        local bench_cmd="${BIN}/game_bench"
        bench_cmd+=" --port $GAME_PORT"
        bench_cmd+=" --threads $threads --conns $conns"
        bench_cmd+=" --duration $duration --warmup $warmup"
        bench_cmd+=" --ramp-delay $RAMP_DELAY"
        bench_cmd+=" --no-live"
        bench_cmd+=" --label $label"
        if [ "$rooms" -gt 0 ]; then
            bench_cmd+=" --rooms $rooms"
        fi

        local output
        output=$($bench_cmd 2>&1) || true

        echo "$output" >> "$logfile"
        echo "" >> "$logfile"

        # 핵심 결과만 추출
        echo "$output" | grep -E "(=== |Config:|Connected:|Login:|Enter:|S_MOVE:|TX:|RX:|Broadcast)" | head -12
        echo ""
    done

    stop_server "$server_name"
    log_ok "Results saved: $logfile"
}

run_all_game() {
    local filter="${1:-all}"

    if [ "$filter" = "all" ] || [ "$filter" = "integrated" ]; then
        if [ -x "${BIN}/GameServerIntegrated" ]; then
            run_game_bench "GameServerIntegrated"
        fi
    fi

    if [ "$filter" = "all" ] || [ "$filter" = "separated" ]; then
        if [ -x "${BIN}/GameServerSeparated" ]; then
            run_game_bench "GameServerSeparated"
        fi
    fi

    if [ "$filter" = "all" ] || [ "$filter" = "epoll" ]; then
        if [ -x "${BIN}/GameServerEpoll" ]; then
            run_game_bench "GameServerEpoll"
        fi
    fi
}

# ── 비교 요약 ────────────────────────────────────────────────────

print_summary() {
    separator "Summary"

    echo -e "${BOLD}Echo Results:${NC}"
    for f in "${RESULTS_DIR}"/echo_*.log; do
        [ -f "$f" ] || continue
        local name="$(basename "$f" .log)"
        echo -e "  ${CYAN}${name}${NC}"
        grep -E "(p50:|Throughput:)" "$f" | sed 's/^/    /'
    done

    echo ""
    echo -e "${BOLD}Game Results:${NC}"
    for f in "${RESULTS_DIR}"/game_*.log; do
        [ -f "$f" ] || continue
        local name="$(basename "$f" .log)"
        echo -e "  ${CYAN}${name}${NC}"
        grep -E "(S_MOVE:|Broadcast factor)" "$f" | sed 's/^/    /'
    done

    echo ""
    echo -e "Full logs: ${RESULTS_DIR}/"
}

# ── 메인 ─────────────────────────────────────────────────────────

main() {
    local mode="${1:-all}"
    local filter="${2:-all}"

    separator "ServerCore v4 Benchmark Suite"
    log "Mode: $mode  Filter: $filter"
    log "Build dir: $BUILD_DIR"
    echo ""

    # 빌드 확인
    check_build

    # 결과 디렉토리
    mkdir -p "$RESULTS_DIR"

    # ulimit 확인
    local fd_limit
    fd_limit=$(ulimit -n)
    if [ "$fd_limit" -lt 4096 ]; then
        log_warn "fd limit is $fd_limit. For 500+ clients, run: ulimit -n 65536"
    fi

    # 실행
    case "$mode" in
        all)
            run_all_echo
            run_all_game "$filter"
            print_summary
            ;;
        echo)
            run_all_echo
            ;;
        game)
            run_all_game "$filter"
            ;;
        summary)
            print_summary
            ;;
        *)
            echo "Usage: $0 [all|echo|game|summary] [symmetric|separated|epoll|all]"
            echo ""
            echo "Examples:"
            echo "  $0                     # 전체 테스트"
            echo "  $0 echo               # echo 벤치만"
            echo "  $0 game               # game 벤치만 (Integrated + Separated + Epoll)"
            echo "  $0 game integrated    # Integrated game 벤치만"
            echo "  $0 game separated     # Separated game 벤치만"
            echo "  $0 game epoll         # Epoll game 벤치만"
            echo "  $0 summary            # 기존 결과 요약만"
            exit 1
            ;;
    esac

    separator "Done"
    log_ok "All benchmarks completed at $(date +%H:%M:%S)"
}

main "$@"
