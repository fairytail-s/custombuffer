#pragma once

namespace buffers {

class BuffersMod {
public:
    bool load();
    bool enable();
    bool disable();
    bool unload();

private:
    bool hooked_{};
};

}
