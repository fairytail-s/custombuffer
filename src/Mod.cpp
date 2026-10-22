#include "Mod.hpp"

#include "GlHooks.hpp"

namespace buffers {
bool BuffersMod::load() { return true; }

bool BuffersMod::enable() {
    if (!installGlHooks()) return false;
    hooked_ = true;
    return true;
}

bool BuffersMod::disable() {
    if (hooked_) {
        removeGlHooks();
        hooked_ = false;
    }
    return true;
}

bool BuffersMod::unload() {
    disable();
    return true;
}
}
