#ifndef CAPABILITIES_H
#define CAPABILITIES_H

#include <stdint.h>

typedef struct Capability {
    uint32_t object_id;
    uint32_t permissions_mask;
    uint64_t nonce;
} Capability;

#endif /* CAPABILITIES_H */
