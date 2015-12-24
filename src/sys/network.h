#ifndef SOCKET_H
#define SOCKET_H

#include "core/platform.h"

#if PLATFORM_WINDOWS
#include <winsock2.h>
#include <ws2tcpip.h>

typedef int socklen_t;
#ifdef ADDRESS_FAMILY
#define sa_family_t ADDRESS_FAMILY
#else
typedef unsigned short sa_family_t;
#endif

#define EAGAIN WSAEWOULDBLOCK
#define EADDRNOTAVAIL WSAEADDRNOTAVAIL
#define EAFNOSUPPORT WSAEAFNOSUPPORT
#define ECONNRESET WSAECONNRESET
typedef u_long ioctlarg_t;
#define socketError WSAGetLastError()

#else

#include <sys/socket.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>

typedef int SOCKET;
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#define ioctlsocket ioctl
typedef int ioctlarg_t;
#define socketError errno

#endif

class Network {
 public:
  static bool Init();
  static void Shutdown();
};

#endif
