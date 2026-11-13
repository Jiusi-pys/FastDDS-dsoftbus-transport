#pragma once
namespace c4 {
struct ShutdownResult {
    bool closed;
    bool healthy;
    bool success() const {return closed&&healthy;}
};
// close must finish/join the progress worker before its health is inspected.
// Preserve both results: clean resource closure does not erase a latched error.
template<class Close,class Health>
ShutdownResult finish_transport(Close close,Health health) {
    const bool closed=close();
    return {closed,health()};
}
}
