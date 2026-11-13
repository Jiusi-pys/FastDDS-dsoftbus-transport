#ifndef STANDARDS_B4_H
#define STANDARDS_B4_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Version-bound C SDK for Standards::BoundedRecord. No borrowed data or loans.
 * Handles belong to this library instance; callers serialize operations on each
 * handle and must not use it after destroy. NULL destroy is a successful no-op.
 * Failed sample operations leave the destination unchanged. Open requires
 * *out=NULL. On an open cleanup failure, B4_ERROR may return a non-NULL
 * endpoint solely for retrying b4_close. After any close attempt the endpoint
 * permits only close retries; failed close retains ownership. */
typedef struct b4_sample b4_sample;
typedef struct b4_endpoint b4_endpoint;
typedef enum b4_result { B4_OK=0, B4_BAD_PARAMETER=1, B4_BOUNDS=2,
    B4_NO_DATA=3, B4_TIMEOUT=4, B4_ERROR=5, B4_UNSUPPORTED=6 } b4_result;
typedef struct b4_record {
    uint32_t id, sequence_number;
    size_t label_size, payload_size;
    char label[64]; /* length-delimited bytes; embedded NUL rejected */
    uint8_t payload[256];
} b4_record;
typedef struct b4_options {
    uint32_t domain;
    const char *topic;
    int writer; /* exactly 0 or 1 */
    int representation; /* 0=XCDR1, 2=XCDR2 */
} b4_options;
b4_result b4_sample_create(b4_sample **out);
b4_result b4_sample_destroy(b4_sample *sample);
b4_result b4_sample_set(b4_sample *sample, const b4_record *value);
b4_result b4_sample_get(const b4_sample *sample, b4_record *out);
b4_result b4_open(const b4_options *options, b4_endpoint **out);
b4_result b4_close(b4_endpoint *endpoint);
b4_result b4_wait_match(b4_endpoint *endpoint, uint32_t timeout_ms);
b4_result b4_write(b4_endpoint *endpoint, const b4_sample *sample);
b4_result b4_take(b4_endpoint *endpoint, b4_sample *sample);
b4_result b4_wait_ack(b4_endpoint *endpoint, uint32_t timeout_ms);
typedef struct b4_transport_counters { uint64_t sdk_sends, received_records; } b4_transport_counters;
/* Board adapter only; UDP returns B4_UNSUPPORTED without changing out. */
b4_result b4_transport_stats(b4_endpoint *endpoint, b4_transport_counters *out);
/* Optional board adapter: values copied during open. Not retained by SDK. */
typedef struct b4_dsoftbus_options {
    const char *token, *run, *local_udid, *peer_udid, *peer_network, *gate_file;
    unsigned side; /* exactly 0 or 1 */
} b4_dsoftbus_options;
b4_result b4_open_dsoftbus(const b4_options *options,
    const b4_dsoftbus_options *transport, b4_endpoint **out);
#ifdef __cplusplus
}
#endif
#endif
