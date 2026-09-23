/* SPDX-License-Identifier: GPL-3.0-or-later
 * Read-only status snapshot for the persistent wireless DS4 game bridge.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <ps5/klog.h>
#include <ps5/payload.h>

#include "wireless_ds4.h"

#define SUPERVISOR_REPORT "/data/poords4/game-pad-bridge-supervisor.txt"
#define STATUS_REPORT     "/data/poords4/game-pad-bridge-status.txt"
#define STATUS_REPORT_TMP STATUS_REPORT ".tmp"

void
poords4_log_reset(void)
{
}

void
poords4_log(const char *format, ...)
{
    char buffer[512];
    va_list arguments;
    va_start(arguments, format);
    (void)vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);
    klog_printf("%s", buffer);
}

static ssize_t
read_text(const char *path, char *buffer, size_t capacity)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;
    ssize_t length = read(fd, buffer, capacity - 1u);
    close(fd);
    if (length < 0)
        return -1;
    buffer[length] = '\0';
    return length;
}

static int
parse_reader_identity(const char *buffer, pid_t *out_pid,
                      intptr_t *out_args)
{
    long pid_value = -1;
    unsigned long args_value = 0;
    const char *line = buffer;
    while (line && *line) {
        if (!strncmp(line, "reader_pid=", 11))
            pid_value = strtol(line + 11, NULL, 0);
        else if (!strncmp(line, "reader_args=", 12))
            args_value = strtoul(line + 12, NULL, 0);
        const char *next = strchr(line, '\n');
        line = next ? next + 1 : NULL;
    }
    if (pid_value <= 0 || args_value == 0)
        return -1;
    *out_pid = (pid_t)pid_value;
    *out_args = (intptr_t)args_value;
    return 0;
}

static int
parse_bridge_identity(const char *buffer, pid_t *out_pid,
                      intptr_t *out_args)
{
    long pid_value = -1;
    unsigned long args_value = 0;
    const char *line = buffer;
    while (line && *line) {
        if (!strncmp(line, "game_pid=", 9))
            pid_value = strtol(line + 9, NULL, 0);
        else if (!strncmp(line, "bridge_args=", 12))
            args_value = strtoul(line + 12, NULL, 0);
        const char *next = strchr(line, '\n');
        line = next ? next + 1 : NULL;
    }
    if (pid_value <= 0 || args_value == 0)
        return -1;
    *out_pid = (pid_t)pid_value;
    *out_args = (intptr_t)args_value;
    return 0;
}

int
main(void)
{
    char supervisor[4096];
    ssize_t supervisor_length = read_text(
        SUPERVISOR_REPORT, supervisor, sizeof(supervisor));
    if (supervisor_length < 0)
        supervisor[0] = '\0';

    pid_t reader_pid = -1;
    intptr_t reader_args = 0;
    int identity_result = supervisor_length >= 0
        ? parse_reader_identity(supervisor, &reader_pid, &reader_args) : -1;
    PoorDS4RemoteReaderStatus reader;
    memset(&reader, 0, sizeof(reader));
    int reader_result = identity_result == 0
        ? wireless_ds4_remote_reader_status(
              reader_pid, reader_args, &reader) : -1;

    pid_t bridge_pid = -1;
    intptr_t bridge_args = 0;
    int bridge_identity_result = supervisor_length >= 0
        ? parse_bridge_identity(supervisor, &bridge_pid, &bridge_args) : -1;
    PoorDS4GameBridgeStatus bridge;
    memset(&bridge, 0, sizeof(bridge));
    int bridge_result = bridge_identity_result == 0
        ? wireless_ds4_game_bridge_status(
              bridge_pid, bridge_args, &bridge) : -1;

    FILE *f = fopen(STATUS_REPORT_TMP, "w");
    if (!f) {
        payload_exit(1);
        return 0;
    }

    fprintf(f,
        "mode=read-only-game-bridge-status\n"
        "supervisor_read_result=%d\n"
        "reader_identity_result=%d\nreader_pid=%d\n"
        "reader_args=0x%lx\nreader_snapshot_result=%d\n"
        "reader_ready=%d\nreader_stop=%d\n"
        "reader_last_result=%d\nreader_seq=%u\n"
        "reader_pad_handle=0x%08x\nreader_owner_pid=%d\n"
        "reader_owner_check_interval=%u\n"
        "reader_owner_miss_count=%u\n"
        "reader_owner_watchdog_exits=%u\n"
        "reader_close_pad_on_exit=%d\nreader_connected=%u\n"
        "reader_mode=%u\n"
        "reader_last_queued_result=0x%08x\n"
        "reader_queued_success_frames=%u\n"
        "reader_queued_empty_frames=%u\n"
        "reader_queued_error_frames=%u\n"
        "reader_state_fallback_frames=%u\n"
        "reader_buttons=0x%08x\n"
        "reader_sticks=%u,%u,%u,%u\n"
        "reader_triggers=%u,%u\n"
        "reader_timestamp=%llu\n"
        "reader_count=%u\n"
        "--- bridge telemetry ---\n"
        "bridge_identity_result=%d\nbridge_pid=%d\n"
        "bridge_args=0x%lx\nbridge_snapshot_result=%d\n"
        "bridge_ready=%d\nbridge_active=%u\nbridge_seq=%u\n"
        "bridge_pad_handle=0x%08x\nbridge_game_pad_index=%d\n"
        "bridge_last_caller_handle=0x%08x\nbridge_last_mismatched_handle=0x%08x\n"
        "bridge_handle_match_calls=%llu\nbridge_handle_mismatch_calls=%llu\n"
        "bridge_read_state_calls=%llu\nbridge_read_state_ext_calls=%llu\n"
        "bridge_read_calls=%llu\nbridge_read_ext_calls=%llu\n"
        "bridge_data_internal_calls=%llu\nbridge_controller_info_calls=%llu\n"
        "bridge_read_nonzero_returns=%llu\nbridge_read_zero_returns=%llu\n"
        "bridge_max_read_streak=%u\n"
        "bridge_published_packets=%llu\nbridge_direct_fallback_frames=%llu\n"
        "bridge_native_backing_calls=%llu\nbridge_native_backing_errors=%llu\n"
        "bridge_native_passthrough_frames=%llu\nbridge_native_connected_frames=%llu\n"
        "bridge_event_ring_head=%u\n",
        (int)supervisor_length, identity_result, reader_pid,
        (unsigned long)reader_args, reader_result,
        reader.ready, reader.stop, reader.last_result, reader.seq,
        (uint32_t)reader.pad_handle, reader.owner_pid,
        reader.owner_check_interval, reader.owner_miss_count,
        reader.owner_watchdog_exits, reader.close_pad_on_exit,
        reader.connected, reader.reader_mode,
        (uint32_t)reader.last_read_result,
        reader.read_success_frames, reader.read_empty_frames,
        reader.read_error_frames, reader.state_fallback_frames,
        reader.buttons,
        reader.left_x, reader.left_y,
        reader.right_x, reader.right_y,
        reader.left_trigger, reader.right_trigger,
        (unsigned long long)reader.timestamp, reader.count,
        bridge_identity_result, bridge_pid,
        (unsigned long)bridge_args, bridge_result,
        bridge.bridge_ready, bridge.active, bridge.seq,
        (uint32_t)bridge.pad_handle, bridge.game_pad_index,
        (uint32_t)bridge.last_caller_handle, (uint32_t)bridge.last_mismatched_handle,
        (unsigned long long)bridge.handle_match_calls,
        (unsigned long long)bridge.handle_mismatch_calls,
        (unsigned long long)bridge.read_state_calls,
        (unsigned long long)bridge.read_state_ext_calls,
        (unsigned long long)bridge.read_calls,
        (unsigned long long)bridge.read_ext_calls,
        (unsigned long long)bridge.data_internal_calls,
        (unsigned long long)bridge.controller_info_calls,
        (unsigned long long)bridge.read_nonzero_returns,
        (unsigned long long)bridge.read_zero_returns,
        bridge.max_read_streak,
        (unsigned long long)bridge.published_packets,
        (unsigned long long)bridge.direct_fallback_frames,
        (unsigned long long)bridge.native_backing_calls,
        (unsigned long long)bridge.native_backing_errors,
        (unsigned long long)bridge.native_passthrough_frames,
        (unsigned long long)bridge.native_connected_frames,
        bridge.event_ring_head);

    fprintf(f, "--- observed handles ---\n");
    for (unsigned i = 0; i < 4u; ++i) {
        fprintf(f, "observed_handle[%u]=0x%08x (calls=%llu)\n",
                i, (uint32_t)bridge.observed_handles[i],
                (unsigned long long)bridge.observed_handle_calls[i]);
    }

    fprintf(f, "--- last events (ring buffer) ---\n");
    fprintf(f, "Seq    | Timestamp (ms) | Delta | API Stub        | Handle     | Flags | Sticks (L/R)        | Triggers | Buttons\n");
    fprintf(f, "-------+----------------+-------+-----------------+------------+-------+---------------------+----------+-------------------------\n");

    uint32_t head = bridge.event_ring_head;
    uint32_t start_seq = (head >= POORDS4_GAME_BRIDGE_EVENT_RING_SIZE)
        ? (head - POORDS4_GAME_BRIDGE_EVENT_RING_SIZE) : 0u;
    uint32_t prev_time = 0;

    for (uint32_t s = start_seq; s < head; ++s) {
        uint32_t idx = s % POORDS4_GAME_BRIDGE_EVENT_RING_SIZE;
        const PoorDS4InputEvent *evt = &bridge.event_ring[idx];
        if (evt->seq != s && head >= POORDS4_GAME_BRIDGE_EVENT_RING_SIZE)
            continue;

        uint32_t delta = (prev_time > 0 && evt->timestamp_ms >= prev_time)
            ? (evt->timestamp_ms - prev_time) : 0u;
        prev_time = evt->timestamp_ms;

        char flag_str[16] = {0};
        snprintf(flag_str, sizeof(flag_str), "%s%s%s%s",
                 (evt->flags & POORDS4_EVT_FLAG_DIRECT) ? "D" : "",
                 (evt->flags & POORDS4_EVT_FLAG_NATIVE) ? "N" : "",
                 (evt->flags & POORDS4_EVT_FLAG_CHANGED) ? "C" : "",
                 (evt->flags & POORDS4_EVT_FLAG_MISMATCH) ? "M" : "");
        if (flag_str[0] == '\0')
            snprintf(flag_str, sizeof(flag_str), "-");

        const char *stub_name = "unknown";
        switch (evt->stub_kind) {
        case POORDS4_EVT_KIND_READ_STATE: stub_name = "read_state"; break;
        case POORDS4_EVT_KIND_READ_STATE_EXT: stub_name = "read_state_ext"; break;
        case POORDS4_EVT_KIND_READ: stub_name = "read"; break;
        case POORDS4_EVT_KIND_READ_EXT: stub_name = "read_ext"; break;
        case POORDS4_EVT_KIND_DATA_INTERNAL: stub_name = "data_internal"; break;
        case POORDS4_EVT_KIND_CONTROLLER_INFO: stub_name = "controller_info"; break;
        }

        char btn_str[128] = "NONE";
        if (evt->buttons != 0) {
            btn_str[0] = '\0';
            struct { uint32_t mask; const char *name; } map[] = {
                { 0x00000001u, "SELECT" }, { 0x00000002u, "L3" }, { 0x00000004u, "R3" },
                { 0x00000008u, "START" },  { 0x00000010u, "UP" }, { 0x00000020u, "RIGHT" },
                { 0x00000040u, "DOWN" },   { 0x00000080u, "LEFT" }, { 0x00000100u, "L2" },
                { 0x00000200u, "R2" },     { 0x00000400u, "L1" }, { 0x00000800u, "R1" },
                { 0x00001000u, "TRIANGLE"},{ 0x00002000u, "CIRCLE"}, { 0x00004000u, "CROSS" },
                { 0x00008000u, "SQUARE" }, { 0x00100000u, "TOUCHPAD" }
            };
            size_t written = 0;
            for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
                if (evt->buttons & map[i].mask) {
                    int n = snprintf(btn_str + written, sizeof(btn_str) - written, "%s%s",
                                     written > 0 ? "|" : "", map[i].name);
                    if (n > 0) written += (size_t)n;
                    if (written >= sizeof(btn_str) - 1u) break;
                }
            }
        }

        fprintf(f, "%-6u | %-14u | +%-4u | %-15s | 0x%08x | %-5s | (%3u,%3u)(%3u,%3u) | L2:%-3u R2:%-3u | %s\n",
                evt->seq, evt->timestamp_ms, delta,
                stub_name, (uint32_t)evt->handle, flag_str,
                evt->lx, evt->ly, evt->rx, evt->ry,
                evt->l2, evt->r2, btn_str);
    }

    fprintf(f, "--- supervisor ---\n%s", supervisor);
    fclose(f);
    (void)rename(STATUS_REPORT_TMP, STATUS_REPORT);
    payload_exit(supervisor_length >= 0 ? 0 : 1);
    return 0;
}
