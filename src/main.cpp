#include "Abi.hpp"
#include "Mod.hpp"

namespace {
buffers::BuffersMod gMod;

template <bool (buffers::BuffersMod::*Fn)()>
bool thunk(void*, void*) {
    try {
        return (gMod.*Fn)();
    } catch (...) {
        return false;
    }
}
}

extern "C" __attribute__((visibility("default"))) ModRegistration* PLGetModRegistration() {
    static ModRegistration reg{&gMod, &thunk<&buffers::BuffersMod::load>, &thunk<&buffers::BuffersMod::enable>,
                               &thunk<&buffers::BuffersMod::disable>, &thunk<&buffers::BuffersMod::unload>};
    return &reg;
}
