#include "sonic_app_fingerprint.h"

#include <assert.h>
#include <string.h>

int main(void)
{
    const uint8_t raw_prefix[4] = {0xCAu, 0x34u, 0xF4u, 0xB7u};
    const uint8_t expected_wire[4] = {0xCAu, 0x34u, 0xF4u, 0xB7u};
    uint8_t wire[4] = {0u};
    char printable[9] = {0};

    sonic_app_fingerprint_format(raw_prefix, wire, printable);

    assert(memcmp(wire, expected_wire, sizeof(expected_wire)) == 0);
    assert(strcmp(printable, "CA34F4B7") == 0);

    sonic_app_fingerprint_format(0, wire, printable);
    assert(wire[0] == 0u && wire[1] == 0u && wire[2] == 0u && wire[3] == 0u);
    assert(strcmp(printable, "00000000") == 0);
    return 0;
}
