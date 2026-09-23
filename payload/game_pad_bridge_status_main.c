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

    char report[8192];
    int length = snprintf(
        report, sizeof(report),
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
        "bridge_event_ring_head=%u\n"
        "--- supervisor ---\n%s",
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
        bridge.event_ring_head,
        supervisor);

    int success = length > 0;
    size_t used = success && (size_t)length < sizeof(report)
        ? (size_t)length : sizeof(report) - 1u;
    size_t offset = 0;
    int fd = open(STATUS_REPORT_TMP, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        success = 0;
    } else {
        while (success && offset < used) {
            ssize_t written = write(fd, report + offset, used - offset);
            if (written > 0) {
                offset += (size_t)written;
                continue;
            }
            if (written < 0 && errno == EINTR)
                continue;
            success = 0;
        }
        if (close(fd) != 0)
            success = 0;
    }
    if (success && rename(STATUS_REPORT_TMP, STATUS_REPORT) != 0)
        success = 0;
    if (!success)
        (void)unlink(STATUS_REPORT_TMP);
    payload_exit(success && supervisor_length >= 0 ? 0 : 1);
    return 0;
}
