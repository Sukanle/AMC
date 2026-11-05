#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct M7Lease M7Lease;

M7Lease *m7_lease_load(const char *module_path);
void *m7_lease_object(M7Lease *lease);
void *m7_lease_token(M7Lease *lease);
void m7_lease_release(void *token);
int32_t m7_lease_destroy(M7Lease *lease);

#ifdef __cplusplus
}
#endif
