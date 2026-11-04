// Host test fixture only. Never deploy as a real DSoftBus SDK library.
#include <socket.h>
#include <cstdio>
#include <cstring>

namespace { unsigned shutdown_calls = 0; }

extern "C" {
int32_t Socket(SocketInfo info)
{
#ifdef SOCKET_SUCCEEDS
    if (!info.name || !info.pkgName || std::strcmp(info.pkgName, "com.kaihong.mdds") != 0 ||
            std::strncmp(info.name, "com.kaihong.mdds.probe.", 22) != 0 ||
            info.dataType != DATA_TYPE_BYTES || info.peerName || info.peerNetworkId)
        return -205;
    return 42;
#else
    (void)info;
    return -201;
#endif
}
int32_t Listen(int32_t, const QosTV*, uint32_t, const ISocketListener*) { return -202; }
int32_t Bind(int32_t, const QosTV*, uint32_t, const ISocketListener*) { return -203; }
#ifndef OMIT_SEND_BYTES
int32_t SendBytes(int32_t, const void*, uint32_t) { return -204; }
#endif
void Shutdown(int32_t id)
{
    std::printf("fixture=Shutdown id=%d calls=%u\n", id, ++shutdown_calls);
}
}
