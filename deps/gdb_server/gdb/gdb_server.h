#ifndef GDB_SERVER_IMPL

#ifndef GDB_SERVER_H
#define GDB_SERVER_H

#include <stdint.h>
#include <stdlib.h>

//
// cross-platform Berkeley sockets shim
//
#if PLATFORM_WINDOWS

#include <winsock2.h>
#include <ws2tcpip.h>

typedef int socklen_t;
typedef u_long ioctlarg_t;

#define sockerrno WSAGetLastError()

#define SHUT_RD SD_RECEIVE
#define SHUT_WR SD_SEND
#define SHUT_RDWR SD_BOTH

#else

#include <sys/select.h>
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
typedef int ioctlarg_t;

#define INVALID_SOCKET -1
#define SOCKET_ERROR -1

#define closesocket close
#define ioctlsocket ioctl
#define sockerrno errno

#endif

//
// target machine interface
//
typedef enum {
  GDB_LITTLE_ENDIAN,
  GDB_BIG_ENDIAN,
} endianness_t;

typedef struct {
  void *ctx;
  endianness_t endian;
  int num_regs;
  void (*detach)(void *);
  void (*stop)(void *);
  void (*resume)(void *);
  void (*step)(void *);
  void (*add_bp)(void *, int type, intmax_t addr);
  void (*rem_bp)(void *, int type, intmax_t addr);
  void (*read_mem)(void *, intmax_t addr, uint8_t *value, int size);
  void (*read_reg)(void *, int n, intmax_t *value, int *size);
} gdb_target_t;

//
// gdb server interface
//
#define GDB_MAX_PACKET_SIZE (1024 * 128)
#define GDB_MAX_DATA_SIZE (GDB_MAX_PACKET_SIZE - 5)

enum {
  GDB_SIGNAL_0,
  GDB_SIGNAL_HUP,
  GDB_SIGNAL_INT,
  GDB_SIGNAL_QUIT,
  GDB_SIGNAL_ILL,
  GDB_SIGNAL_TRAP,
  GDB_SIGNAL_ABRT,
  GDB_SIGNAL_EMT,
  GDB_SIGNAL_FPE,
  GDB_SIGNAL_KILL,
  GDB_SIGNAL_BUS,
  GDB_SIGNAL_SEGV,
  GDB_SIGNAL_SYS,
  GDB_SIGNAL_PIPE,
  GDB_SIGNAL_ALRM,
  GDB_SIGNAL_TERM,
  GDB_SIGNAL_URG,
  GDB_SIGNAL_STOP,
  GDB_SIGNAL_TSTP,
  GDB_SIGNAL_CONT,
  GDB_SIGNAL_CHLD,
  GDB_SIGNAL_TTIN,
  GDB_SIGNAL_TTOU,
  GDB_SIGNAL_IO,
  GDB_SIGNAL_XCPU,
  GDB_SIGNAL_XFSZ,
  GDB_SIGNAL_VTALRM,
  GDB_SIGNAL_PROF,
  GDB_SIGNAL_WINCH,
  GDB_SIGNAL_LOST,
  GDB_SIGNAL_USR1,
  GDB_SIGNAL_USR2,
  GDB_SIGNAL_PWR,
  GDB_SIGNAL_POLL,
  GDB_SIGNAL_WIND,
  GDB_SIGNAL_PHONE,
  GDB_SIGNAL_WAITING,
  GDB_SIGNAL_LWP,
  GDB_SIGNAL_DANGER,
  GDB_SIGNAL_GRANT,
  GDB_SIGNAL_RETRACT,
  GDB_SIGNAL_MSG,
  GDB_SIGNAL_SOUND,
  GDB_SIGNAL_SAK,
  GDB_SIGNAL_PRIO,
};

enum {
  GDB_BP_SW,  // software breakpoint
  GDB_BP_HW,  // hardware breakpoint
  GDB_BP_W,   // write watchpoint
  GDB_BP_R,   // read watchpoint
  GDB_BP_A    // access watchpoint
};

enum {
  PARSE_WAIT,
  PARSE_DATA,
  PARSE_CHECKSUM_HIGH,
  PARSE_CHECKSUM_LOW,
  PARSE_DONE,
};

typedef struct {
  int recv_state;
  char recv_data[GDB_MAX_DATA_SIZE];
  int recv_length;
  uint8_t recv_checksum;

  char last_sent[GDB_MAX_PACKET_SIZE];

  int ack_disabled;
} gdb_connection_t;

typedef struct {
  gdb_target_t target;
  SOCKET listen;
  SOCKET client;
  gdb_connection_t conn;
} gdb_server_t;

gdb_server_t *gdb_server_create(gdb_target_t *target, int port);
void gdb_server_interrupt(gdb_server_t *sv, int signal);
void gdb_server_pump(gdb_server_t *sv);
void gdb_server_destroy(gdb_server_t *sv);

#endif  // #ifndef GDB_SERVER_H

#else  // #ifndef GDB_SERVER_IMPL

#undef GDB_SERVER_IMPL

#ifndef GDB_SERVER_ASSERT
#include <assert.h>
#define GDB_SERVER_ASSERT assert
#endif

#ifndef GDB_SERVER_LOG
#define GDB_SERVER_LOG(x, ...) printf(x "\n", ##__VA_ARGS__)
#endif

#ifndef GDB_SERVER_MALLOC
#define GDB_SERVER_MALLOC malloc
#endif

#ifndef GDB_SERVER_ALLOCA
#if PLATFORM_WINDOWS
#include <malloc.h>
#define GDB_SERVER_ALLOCA _alloca
#else
#define GDB_SERVER_ALLOCA alloca
#endif
#endif

#ifndef GDB_SERVER_FREE
#define GDB_SERVER_FREE free
#endif

#define GDB_SERVER_UNUSED(x) ((void)x)

//
// gdb server implementation
//
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "gdb_server.h"

// used by 'q' and 'Q' packets
typedef struct {
  const char *name;
  void (*callback)(gdb_server_t *sv, const char *data);
} query_handler_t;

static const char GDB_PACKET_BEGIN = '$';
static const char GDB_PACKET_END = '#';
static const char *GDB_PACKET_ACK = "+";
static const char *GDB_PACKET_NACK = "-";
static char scratch_buffer[GDB_MAX_DATA_SIZE] = {0};

static int gdb_server_create_listen(gdb_server_t *sv, int port);
static void gdb_server_destroy_listen(gdb_server_t *sv);

static void gdb_server_accept_client(gdb_server_t *sv);
static void gdb_server_destroy_client(gdb_server_t *sv);

static int gdb_server_data_available(gdb_server_t *sv);
static const char *gdb_server_recv_packet(gdb_server_t *sv);

static int gdb_server_send_raw(gdb_server_t *sv, const char *data);
static int gdb_server_send_packet(gdb_server_t *sv, const char *data);

static void gdb_server_handle_packet(gdb_server_t *sv, const char *data);

//
// packet parsing and formatting helpers
//
static int xtoi(char c) {
  int i = -1;
  c = tolower(c);
  if (c >= 'a' && c <= 'f') {
    i = 0xa + (c - 'a');
  } else if (c >= '0' && c <= '9') {
    i = c - '0';
  }
  return i;
}

static const char *parse_hex(const char *buffer, int *value) {
  while (isxdigit(*buffer)) {
    *value <<= 4;
    *value |= xtoi(*buffer);
    buffer++;
  }
  return buffer;
}

static const char *parse_tid(const char *buffer, int *value) {
  if (!strncmp(buffer, "-1", 2)) {
    *value = -1;
    return buffer + 2;
  }

  return parse_hex(buffer, value);
}

static int format_register(intmax_t value, int width, endianness_t endian,
                           char *buffer, int size) {
  uint8_t *ptr = (uint8_t *)&value;
  int remaining = size;

  for (int i = 0; i < width; i++) {
    uint8_t c = endian == GDB_LITTLE_ENDIAN ? ptr[i] : ptr[width - 1 - i];
    int n = snprintf(buffer, remaining, "%02x", c);
    GDB_SERVER_ASSERT(n);
    buffer += n;
    remaining -= n;
  }

  return size - remaining;
}

//
// generate checksum for data of gdb server packet. the checksum is calculated
// as the modulo 256 of the sum of all characters in the data portion of the
// packet
//
static uint8_t packet_data_checksum(const char *data) {
  int checksum = 0;
  int len = (int)strlen(data);
  for (int i = 0; i < len; i++) {
    checksum += data[i];
  }
  return (uint8_t)(checksum % 256);
}

//
// create an instance of the gdb server for the supplied target
//
gdb_server_t *gdb_server_create(gdb_target_t *target, int port) {
  if (!target) {
    return NULL;
  }

#if PLATFORM_WINDOWS
  WSADATA wsadata;
  int r = WSAStartup(MAKEWORD(2, 2), &wsadata);
  if (r) {
    GDB_SERVER_LOG("Failed to initialize WinSock");
    return NULL;
  }
#endif

  gdb_server_t *sv = (gdb_server_t *)GDB_SERVER_MALLOC(sizeof(gdb_server_t));
  memset(sv, 0, sizeof(*sv));
  sv->target = *target;
  sv->listen = INVALID_SOCKET;
  sv->client = INVALID_SOCKET;

  if (gdb_server_create_listen(sv, port) == -1) {
    gdb_server_destroy(sv);
    return NULL;
  }

  return sv;
}

//
// tell the client that we've halted due to an interrupt
//
void gdb_server_interrupt(gdb_server_t *sv, int signal) {
  snprintf(scratch_buffer, sizeof(scratch_buffer), "T%02x", signal);
  gdb_server_send_packet(sv, scratch_buffer);
}

//
// check for a new connection, or handle pending messages from an existing
// connection
//
void gdb_server_pump(gdb_server_t *sv) {
  const char *data = NULL;

  gdb_server_accept_client(sv);

  while ((data = gdb_server_recv_packet(sv))) {
    gdb_server_handle_packet(sv, data);
  }
}

//
// shutdown and free the server instance
//
void gdb_server_destroy(gdb_server_t *sv) {
  gdb_server_destroy_listen(sv);

  GDB_SERVER_FREE(sv);

#if PLATFORM_WINDOWS
  int r = WSACleanup();
  GDB_SERVER_ASSERT(r == 0);
#endif
}

//
// create a TCP-based listen server
//
static int gdb_server_create_listen(gdb_server_t *sv, int port) {
  int ret = 0;

  for (;;) {
    // create the socket to monitor for connections on
    sv->listen = socket(PF_INET, SOCK_STREAM, 0);
    if (sv->listen == INVALID_SOCKET) {
      GDB_SERVER_LOG("Failed to create gdb socket");
      ret = -1;
      break;
    }

    // enable reusing of the address / port
    int on = 1;
    if (setsockopt(sv->listen, SOL_SOCKET, SO_REUSEADDR, (char *)&on, sizeof(on)) ==
        SOCKET_ERROR) {
      GDB_SERVER_LOG("Failed to set socket options for gdb socket");
      ret = -1;
      break;
    }

    // bind the socket to an address / port
    struct sockaddr_in listen_addr_in;
    memset(&listen_addr_in, 0, sizeof(listen_addr_in));
    listen_addr_in.sin_family = AF_INET;
    listen_addr_in.sin_port = htons(port);
    listen_addr_in.sin_addr.s_addr = INADDR_ANY;

    const struct sockaddr *listen_addr =
        (const struct sockaddr *)&listen_addr_in;
    socklen_t listen_addr_in_len = sizeof(listen_addr_in);

    if (bind(sv->listen, listen_addr, listen_addr_in_len) == SOCKET_ERROR) {
      GDB_SERVER_LOG("Failed to bind gdb socket");
      ret = -1;
      break;
    }

    // start listening
    if (listen(sv->listen, 1) == SOCKET_ERROR) {
      GDB_SERVER_LOG("Failed to listen to gdb socket");
      ret = -1;
      break;
    }

    GDB_SERVER_LOG("GDB server started on localhost:%d", port);

    break;
  }

  // cleanup if necessary
  if (ret == -1) {
    shutdown(sv->listen, SHUT_RDWR);
    sv->listen = INVALID_SOCKET;
  }

  return ret;
}

//
// destroy the listen server
//
static void gdb_server_destroy_listen(gdb_server_t *sv) {
  if (sv->listen == INVALID_SOCKET) {
    return;
  }

  gdb_server_destroy_client(sv);

  shutdown(sv->listen, SHUT_RDWR);
  sv->listen = INVALID_SOCKET;
}

//
// attempt to accept a new client connection. if a client is already connected,
// destroy it in favor of the new one
//
static void gdb_server_accept_client(gdb_server_t *sv) {
  if (sv->listen == INVALID_SOCKET) {
    return;
  }

  // check to see if there is a pending connection
  fd_set fd_read;
  FD_ZERO(&fd_read);
  FD_SET(sv->listen, &fd_read);

  // return immediately
  struct timeval t;
  t.tv_sec = 0;
  t.tv_usec = 0;

  if (select((int)(sv->listen + 1), &fd_read, NULL, NULL, &t) == SOCKET_ERROR) {
    return;
  }

  // no new connections
  if (!FD_ISSET(sv->listen, &fd_read)) {
    return;
  }

  // new connection, shut down the existing one and accept it
  if (sv->client != INVALID_SOCKET) {
    gdb_server_destroy_client(sv);
  }

  sv->client = accept(sv->listen, NULL, NULL);
}

//
// destroy the current client connection
//
static void gdb_server_destroy_client(gdb_server_t *sv) {
  if (sv->client == INVALID_SOCKET) {
    return;
  }

  shutdown(sv->client, SHUT_RDWR);
  sv->client = INVALID_SOCKET;

  memset(&sv->conn, 0, sizeof(sv->conn));
}

//
// check if any data is available to be read from the client
//
static int gdb_server_data_available(gdb_server_t *sv) {
  if (sv->client == INVALID_SOCKET) {
    return 0;
  }

  fd_set fd_read;
  FD_ZERO(&fd_read);
  FD_SET(sv->client, &fd_read);

  // return immediately
  struct timeval t;
  t.tv_sec = 0;
  t.tv_usec = 0;

  if (select((int)(sv->client + 1), &fd_read, NULL, NULL, &t) == SOCKET_ERROR) {
    return -1;
  }

  if (!FD_ISSET(sv->client, &fd_read)) {
    return 0;
  }

  return 1;
}

//
// read and parse new data from the socket, dispatching it once a complete
// packet has been parsed
//
static const char *gdb_server_recv_packet(gdb_server_t *sv) {
  if (sv->client == INVALID_SOCKET) {
    return NULL;
  }

  int parsed_ack = 0;

  while (gdb_server_data_available(sv) && sv->conn.recv_state != PARSE_DONE) {
    // read the next byte from the stream
    char c = 0;
    int n = (int)recv(sv->client, &c, 1, 0);

    if (n == 0) {
      // client disconnected
      gdb_server_destroy_client(sv);
      break;
    } else if (n == SOCKET_ERROR) {
      GDB_SERVER_LOG("gdb server recv failed, %d", n);
      break;
    }

    GDB_SERVER_ASSERT(n == 1);

    // process the byte into the current packet
    switch (sv->conn.recv_state) {
      case PARSE_WAIT: {
        if (c == GDB_PACKET_ACK[0] || c == GDB_PACKET_NACK[0] || c == 0x3) {
          GDB_SERVER_ASSERT(sv->conn.recv_length < GDB_MAX_DATA_SIZE);
          sv->conn.recv_data[sv->conn.recv_length++] = c;
          sv->conn.recv_data[sv->conn.recv_length] = 0;
          sv->conn.recv_state = PARSE_DONE;
          parsed_ack = 1;
        } else if (c == GDB_PACKET_BEGIN) {
          sv->conn.recv_state = PARSE_DATA;
        } else {
          GDB_SERVER_ASSERT(0);
        }
      } break;

      case PARSE_DATA: {
        if (c == GDB_PACKET_END) {
          sv->conn.recv_state = PARSE_CHECKSUM_HIGH;
        } else {
          GDB_SERVER_ASSERT(sv->conn.recv_length < GDB_MAX_DATA_SIZE);
          sv->conn.recv_data[sv->conn.recv_length++] = c;
          sv->conn.recv_data[sv->conn.recv_length] = 0;
        }
      } break;

      case PARSE_CHECKSUM_HIGH: {
        sv->conn.recv_checksum = xtoi(c) << 4;
        sv->conn.recv_state = PARSE_CHECKSUM_LOW;
      } break;

      case PARSE_CHECKSUM_LOW: {
        sv->conn.recv_checksum |= xtoi(c);
        sv->conn.recv_state = PARSE_DONE;
      } break;

      case PARSE_DONE:
        GDB_SERVER_ASSERT(0);
        break;
    }
  }

  if (sv->conn.recv_state == PARSE_DONE) {
    // validate and ack non-notification packets
    if (!sv->conn.ack_disabled && !parsed_ack) {
      uint8_t expected_checksum = packet_data_checksum(sv->conn.recv_data);

      if (sv->conn.recv_checksum == expected_checksum) {
        gdb_server_send_raw(sv, GDB_PACKET_ACK);
      } else {
        gdb_server_send_raw(sv, GDB_PACKET_NACK);
      }
    }

    // reset parse state
    sv->conn.recv_state = PARSE_WAIT;
    sv->conn.recv_length = 0;

    return sv->conn.recv_data;
  }

  return NULL;
}

//
// send a raw packet to the client
//
static int gdb_server_send_raw(gdb_server_t *sv, const char *data) {
  int len = (int)strlen(data);
  int n = send(sv->client, data, len, 0);

  if (n != len) {
    GDB_SERVER_LOG("gdb server failed to send raw packet %s, %d", data, n);
    return -1;
  }

  return 0;
}

//
// send a formatted packet to the client. all gdb commands and responses (other
// than acks and nacks) are sent as a packet. a packet starts with the character
// '$', the actual packet data, and the terminating character '#' followed by a
// two-digit checksum. for example:
//   $packet-data#checksum
//
static int gdb_server_send_packet(gdb_server_t *sv, const char *data) {
  uint8_t cs = packet_data_checksum(data);

  // make sure the data won't overflow our buffer
  int length = (int)strlen(data);
  if (length > GDB_MAX_DATA_SIZE) {
    return -1;
  }

  snprintf(sv->conn.last_sent, sizeof(sv->conn.last_sent), "%c%s%c%02x",
           GDB_PACKET_BEGIN, data, GDB_PACKET_END, cs);

  return gdb_server_send_raw(sv, sv->conn.last_sent);
}

static int gdb_server_handle_ack(gdb_server_t *sv, const char *data) {
  GDB_SERVER_ASSERT(!sv->conn.ack_disabled);

  // nothing to do

  return 1;
}

static int gdb_server_handle_nack(gdb_server_t *sv, const char *data) {
  GDB_SERVER_ASSERT(!sv->conn.ack_disabled);

  // resend last packet
  gdb_server_send_raw(sv, sv->conn.last_sent);

  return 1;
}

static int gdb_server_handle_int3(gdb_server_t *sv, const char *data) {
  // halt the target
  sv->target.stop(sv->target.ctx);

  // let the client know we've stopped
  gdb_server_interrupt(sv, GDB_SIGNAL_TRAP);

  return 1;
}

static int gdb_server_handle_detach(gdb_server_t *sv, const char *data) {
  sv->target.detach(sv->target.ctx);

  gdb_server_destroy_client(sv);

  return 1;
}

// 'c [addr]'
// Continue at addr, which is the address to resume. If addr is omitted, resume
// at current address.
static int gdb_server_handle_c(gdb_server_t *sv, const char *data) {
  int addr = 0;
  data = parse_hex(&data[1], &addr);

  if (addr != 0) {
    return 0;
  }

  sv->target.resume(sv->target.ctx);

  return 1;
}

// 'g'
// Read general registers.
// Reply:
// 'XX...'
// Each byte of register data is described by two hex digits. The bytes with the
// register are transmitted in target byte order. The size of each register and
// their position within the 'g' packet are determined by the gdb internal
// gdbarch functions DEPRECATED_REGISTER_RAW_SIZE and gdbarch_register_name. The
// specification of several standard 'g' packets is specified below.
// When reading registers from a trace frame (see Using the Collected Data), the
// stub may also return a string of literal 'x''s in place of the register data
// digits, to indicate that the corresponding register has not been collected,
// thus its value is unavailable. For example, for an architecture with 4
// registers of 4 bytes each, the following reply indicates to gdb that
// registers 0 and 2 have not been collected, while registers 1 and 3 have been
// collected, and both have zero value:
//                -> g
//                <- xxxxxxxx00000000xxxxxxxx00000000
// 'E NN'
// for an error.
static int gdb_server_handle_g(gdb_server_t *sv, const char *data) {
  char *buffer = scratch_buffer;
  int remaining = (int)sizeof(scratch_buffer);

  // reset buffer
  buffer[0] = 0;

  // read all registers
  for (int i = 0; i < sv->target.num_regs; i++) {
    // read the register
    intmax_t value = 0;
    int size = 0;
    sv->target.read_reg(sv->target.ctx, i, &value, &size);

    // format the register
    int n = format_register(value, size, sv->target.endian, buffer, remaining);
    buffer += n;
    remaining -= n;
  }

  // send the reply
  gdb_server_send_packet(sv, scratch_buffer);

  return 1;
}

// 'H op thread-id'
// Set thread for subsequent operations ('m', 'M', 'g', 'G', et.al.). Depending
// on the operation to be performed, op should be 'c' for step and continue
// operations (note that this is deprecated, supporting the 'vCont' command is a
// better option), and 'g' for other operations. The thread designator thread-id
// has the format and interpretation described in thread-id syntax.
// Reply:
// 'OK'
// for success
// 'E NN'
// for an error
static int gdb_server_handle_H(gdb_server_t *sv, const char *data) {
  // TODO proper thread support
  int op = data[1];
  GDB_SERVER_UNUSED(op);

  int thread = 0;
  data = parse_tid(&data[2], &thread);

  if (thread != -1 && thread != 0) {
    gdb_server_send_packet(sv, "E01");
  } else {
    gdb_server_send_packet(sv, "OK");
  }

  return 1;
}

// 'm addr,length'
// Read length addressable memory units starting at address addr (see
// addressable memory unit). Note that addr may not be aligned to any particular
// boundary.
// The stub need not use any particular size or alignment when gathering data
// from memory for the response; even if addr is word-aligned and length is a
// multiple of the word size, the stub is free to use byte accesses, or not. For
// this reason, this packet may not be suitable for accessing memory-mapped I/O
// devices. Reply:
// 'XX...'
// Memory contents; each byte is transmitted as a two-digit hexadecimal number.
// The reply may contain fewer addressable memory units than requested if the
// server was able to read only part of the region of memory.
// 'E NN'
// NN is errno
static int gdb_server_handle_m(gdb_server_t *sv, const char *data) {
  int addr = 0;
  data = parse_hex(&data[1], &addr);

  int length = 0;
  data = parse_hex(&data[1], &length);

  // read bytes from the target
  uint8_t *memory = (uint8_t *)GDB_SERVER_ALLOCA(length);
  memset(memory, 0, length);
  sv->target.read_mem(sv->target.ctx, addr, memory, length);

  // format bytes into response
  char *buffer = scratch_buffer;
  int remaining = (int)sizeof(scratch_buffer);
  GDB_SERVER_ASSERT((length * 2 + 1) < remaining);

  // reset buffer
  buffer[0] = 0;

  for (int i = 0; i < length; i++) {
    int n = snprintf(buffer, remaining, "%02x", memory[i]);
    GDB_SERVER_ASSERT(n);
    buffer += n;
    remaining -= n;
  }

  gdb_server_send_packet(sv, scratch_buffer);

  return 1;
}

// 'p n'
// Read the value of register n; n is in hex. See read registers packet, for a
// description of how the returned register value is encoded.
// Reply:
// 'XX...'
// the register's value
// 'E NN'
// for an error
// ''
// Indicating an unrecognized query.
static int gdb_server_handle_p(gdb_server_t *sv, const char *data) {
  int reg = 0;
  data = parse_hex(&data[1], &reg);

  // read the register
  intmax_t value = 0;
  int size = 0;
  sv->target.read_reg(sv->target.ctx, reg, &value, &size);

  // reset buffer
  scratch_buffer[0] = 0;

  // format the register
  format_register(value, size, sv->target.endian, scratch_buffer,
                  sizeof(scratch_buffer));

  // send the reply
  gdb_server_send_packet(sv, scratch_buffer);

  return 1;
}

// 'qAttached:pid'
// Return an indication of whether the remote server attached to an existing
// process or created a new process. When the multiprocess protocol extensions
// are supported (see multiprocess extensions), pid is an integer in hexadecimal
// format identifying the target process. Otherwise, gdb will omit the pid field
// and the query packet will be simplified as 'qAttached'.
// This query is used, for example, to know whether the remote process should be
// detached or killed when a gdb session is ended with the quit command.
static void gdb_server_handle_qAttached(gdb_server_t *sv, const char *data) {
  gdb_server_send_packet(sv, "1");
}

// 'qC'
// Return the current thread ID.
static void gdb_server_handle_qC(gdb_server_t *sv, const char *data) {
  // TODO proper thread support
  gdb_server_send_packet(sv, "QC0");
}

// 'qfThreadInfo'
// Obtain a list of all active thread IDs from the target (OS). Since there
// may be too many active threads to fit into one reply packet, this query works
// iteratively: it may require more than one query/reply sequence to obtain the
// entire list of threads. The first query of the sequence will be the
// ‘qfThreadInfo’ query; subsequent queries in the sequence will be the
// ‘qsThreadInfo’ query.
static void gdb_server_handle_qfThreadInfo(gdb_server_t *sv, const char *data) {
  // TODO proper thread support
  gdb_server_send_packet(sv, "m0");
}

// 'qsThreadInfo'
static void gdb_server_handle_qsThreadInfo(gdb_server_t *sv, const char *data) {
  // TODO proper thread support
  gdb_server_send_packet(sv, "l");
}

// 'q name params...'
// General query packets.
static int gdb_server_handle_q(gdb_server_t *sv, const char *data) {
  static query_handler_t query_handlers[] = {
      {"qAttached", gdb_server_handle_qAttached},
      {"qC", gdb_server_handle_qC},
      {"qfThreadInfo", gdb_server_handle_qfThreadInfo},
      {"qsThreadInfo", gdb_server_handle_qsThreadInfo},
  };

  static int num_query_handlers =
      (int)(sizeof(query_handlers) / sizeof(query_handler_t));

  // check to see if there is a handler registered for the incoming query
  int handled = 0;

  for (int i = 0; i < num_query_handlers; i++) {
    query_handler_t *handler = &query_handlers[i];

    if (!strcmp(data, handler->name)) {
      handler->callback(sv, data);
      handled = 1;
      break;
    }
  }

  return handled;
}

// 'Q name params...'
// General set packets.
static int gdb_server_handle_Q(gdb_server_t *sv, const char *data) { return 0; }

// 's [addr]'
// Single step, resuming at addr. If addr is omitted, resume at same address.
static int gdb_server_handle_s(gdb_server_t *sv, const char *data) {
  int addr = 0;
  data = parse_hex(&data[1], &addr);

  if (addr != 0) {
    return 0;
  }

  sv->target.step(sv->target.ctx);

  return 1;
}

// 'z type,addr,kind'
// Remove a type breakpoint or watchpoint starting at address address of kind
// kind.
static int gdb_server_handle_z(gdb_server_t *sv, const char *data) {
  int type = 0;
  data = parse_hex(&data[1], &type);

  int addr = 0;
  data = parse_hex(&data[1], &addr);

  int kind = 0;
  data = parse_hex(&data[1], &kind);
  GDB_SERVER_UNUSED(kind);

  sv->target.rem_bp(sv->target.ctx, type, addr);

  gdb_server_send_packet(sv, "OK");

  return 1;
}

// 'Z type,addr,kind'
// Insert a type breakpoint or watchpoint starting at address address of kind
// kind.
static int gdb_server_handle_Z(gdb_server_t *sv, const char *data) {
  int type = 0;
  data = parse_hex(&data[1], &type);

  int addr = 0;
  data = parse_hex(&data[1], &addr);

  int kind = 0;
  data = parse_hex(&data[1], &kind);
  GDB_SERVER_UNUSED(kind);

  sv->target.add_bp(sv->target.ctx, type, addr);

  gdb_server_send_packet(sv, "OK");

  return 1;
}

// '?'
// Indicate the reason the target halted. The reply is only returned once the
// target halts.
static int gdb_server_handle_question(gdb_server_t *sv, const char *data) {
  // halt the target
  sv->target.stop(sv->target.ctx);

  // send the reply, letting gdb know we're halted
  gdb_server_interrupt(sv, GDB_SIGNAL_0);

  return 1;
}

//
// handle a received packet, dispatching it to the appropriate handler
//
static void gdb_server_handle_packet(gdb_server_t *sv, const char *data) {
  int handled = 0;

  if (!strcmp(data, GDB_PACKET_ACK)) {
    handled = gdb_server_handle_ack(sv, data);
  } else if (!strcmp(data, GDB_PACKET_NACK)) {
    handled = gdb_server_handle_nack(sv, data);
  } else if (data[0] == 0x3) {
    handled = gdb_server_handle_int3(sv, data);
  } else if (data[0] == 'D') {
    handled = gdb_server_handle_detach(sv, data);
  } else if (data[0] == 'c') {
    handled = gdb_server_handle_c(sv, data);
  } else if (data[0] == 'g') {
    handled = gdb_server_handle_g(sv, data);
  } else if (data[0] == 'H') {
    handled = gdb_server_handle_H(sv, data);
  } else if (data[0] == 'm') {
    handled = gdb_server_handle_m(sv, data);
  } else if (data[0] == 'p') {
    handled = gdb_server_handle_p(sv, data);
  } else if (data[0] == 'q') {
    handled = gdb_server_handle_q(sv, data);
  } else if (data[0] == 'Q') {
    handled = gdb_server_handle_Q(sv, data);
  } else if (data[0] == 's') {
    handled = gdb_server_handle_s(sv, data);
  } else if (data[0] == 'z') {
    handled = gdb_server_handle_z(sv, data);
  } else if (data[0] == 'Z') {
    handled = gdb_server_handle_Z(sv, data);
  } else if (!strcmp(data, "?")) {
    handled = gdb_server_handle_question(sv, data);
  }

  if (!handled) {
    GDB_SERVER_LOG("Unsupported packet %s", data);

    gdb_server_send_packet(sv, "");
  }
}

#endif
