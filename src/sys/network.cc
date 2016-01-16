#include "core/core.h"
#include "sys/network.h"

bool Network::Init() {
#ifdef PLATFORM_WINDOWS
  WSADATA wsadata = {};

  int r = WSAStartup(MAKEWORD(1, 1), &wsadata);
  if (r) {
    LOG_WARNING("Winsock initialization failed, %d", r);
    return false;
  }

  LOG_INFO("Winsock initialized");
#endif

  return true;
}

void Network::Shutdown() {
#ifdef PLATFORM_WINDOWS
  WSACleanup();
#endif
}
