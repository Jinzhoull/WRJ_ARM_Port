#include "m4_matlab_flow.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static size_t decode_hex(const char *hex, uint8_t *out, size_t capacity)
{
    const size_t n = strlen(hex);
    size_t k;
    if ((n & 1U) != 0U || n / 2U > capacity) return (size_t)-1;
    for (k = 0U; k < n / 2U; ++k) {
        const int hi = hex_digit(hex[2U * k]), lo = hex_digit(hex[2U * k + 1U]);
        if (hi < 0 || lo < 0) return (size_t)-1;
        out[k] = (uint8_t)((hi << 4) | lo);
    }
    return n / 2U;
}

static void print_frame(const m4_matlab_frame_t *frame)
{
    size_t k;
    for (k = 0U; k < frame->count; ++k) printf("%02X", frame->bytes[k]);
    putchar('\n');
}

static int run_batch(void)
{
    char line[2048];
    m4_matlab_rng_t rng;
    uint32_t line_number = 0U;
    m4_matlab_rng_seed(&rng, 20260724U);
    while (fgets(line, sizeof(line), stdin) != NULL) {
        char *fields[16], *token;
        uint8_t raw[WRJ_MAX_BYTES];
        size_t n, k = 0U;
        m4_matlab_frame_t frame;
        ++line_number;
        token = strtok(line, "\t\r\n");
        while (token != NULL && k < 16U) {
            fields[k++] = token;
            token = strtok(NULL, "\t\r\n");
        }
        if (k != 16U) {
            fprintf(stderr, "batch line %u has %u fields\n", line_number, (unsigned)k);
            return 2;
        }
        n = decode_hex(fields[2], raw, sizeof(raw));
        if (n == (size_t)-1) {
            fprintf(stderr, "batch line %u has invalid hex\n", line_number);
            return 2;
        }
        if (strcmp(fields[0], "RemoteID_BLE") == 0) {
            m4_result_t remote;
            memset(&remote, 0, sizeof(remote));
            remote.parse_complete = (uint8_t)strtoul(fields[9], NULL, 10);
            snprintf(remote.uas_id, sizeof(remote.uas_id), "%s", fields[10]);
            remote.latitude_deg = strtod(fields[11], NULL);
            remote.longitude_deg = strtod(fields[12], NULL);
            remote.altitude_m = strtod(fields[13], NULL);
            remote.speed_mps = strtod(fields[14], NULL);
            remote.heading_deg = strtod(fields[15], NULL);
            if (m4_matlab_build_remoteid_frame(&remote, &frame) != WRJ_OK) return 1;
        } else if (m4_matlab_build_validation_frame(fields[0], raw, (uint16_t)n,
                   fields[1], (uint8_t)strtoul(fields[3], NULL, 10),
                   (uint8_t)strtoul(fields[4], NULL, 10),
                   (uint8_t)strtoul(fields[5], NULL, 10),
                   (uint32_t)strtoul(fields[6], NULL, 10),
                   (uint32_t)strtoul(fields[7], NULL, 10), &frame) != WRJ_OK) return 1;
        m4_matlab_apply_validation_channel(&frame, strtod(fields[8], NULL), fields[0], &rng);
        m4_matlab_crc16_recover_unique_single_bit(frame.bytes, frame.count);
        print_frame(&frame);
    }
    return 0;
}

int main(int argc, char **argv)
{
    uint8_t raw[WRJ_MAX_BYTES];
    m4_matlab_frame_t frame;
    size_t n, k;
    if (argc == 2 && strcmp(argv[1], "--batch") == 0) return run_batch();
    if (argc != 9) {
        fprintf(stderr, "category source rawHex sequence type state timestamp observation\n");
        return 2;
    }
    n = strlen(argv[3]);
    if ((n & 1U) != 0U || n / 2U > sizeof(raw)) return 2;
    for (k = 0U; k < n / 2U; ++k) {
        int hi = hex_digit(argv[3][2U * k]);
        int lo = hex_digit(argv[3][2U * k + 1U]);
        if (hi < 0 || lo < 0) return 2;
        raw[k] = (uint8_t)((hi << 4) | lo);
    }
    if (m4_matlab_build_validation_frame(argv[1], raw, (uint16_t)(n / 2U), argv[2],
        (uint8_t)strtoul(argv[4], NULL, 10), (uint8_t)strtoul(argv[5], NULL, 10),
        (uint8_t)strtoul(argv[6], NULL, 10), (uint32_t)strtoul(argv[7], NULL, 10),
        (uint32_t)strtoul(argv[8], NULL, 10), &frame) != WRJ_OK) return 1;
    for (k = 0U; k < frame.count; ++k) printf("%02X", frame.bytes[k]);
    putchar('\n');
    return 0;
}
