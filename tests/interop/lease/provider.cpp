// Minimal module used by the lease interop fixture. The object's destroy code
// lives here, so the module cannot unload until the consumer releases its lease.
#include <stdint.h>

struct LeaseObject {
    int32_t value;
};

static int32_t live_objects = 0;

extern "C" {

LeaseObject *lease_object_create(const char *name) {
    auto *object = new LeaseObject();
    object->value = name != nullptr ? 1 : 0;
    ++live_objects;
    return object;
}

int32_t lease_object_get(LeaseObject *object) { return object != nullptr ? object->value : -1; }

int32_t lease_object_set(LeaseObject *object, int32_t value) {
    if (object == nullptr) return -1;
    object->value = value;
    return 0;
}

int32_t lease_object_destroy(LeaseObject *object) {
    if (object != nullptr) {
        delete object;
        --live_objects;
    }
    return 0;
}

int32_t lease_object_live(void) { return live_objects; }

}   // extern "C"
