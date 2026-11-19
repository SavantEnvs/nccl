// NCCL proxy service (src/proxy.cc: ncclProxyService -> proxyServiceInitOp -> proxyProgressAsync ->
// proxyConnInit). Every rank runs this thread on a TCP listen socket that local peers -- and, under
// NCCL_COMM_ID bootstrap, anyone who knows the magic -- connect to; the service reads
// type / connection / reqSize / respSize / request / opId frames off the socket.
//
// Per input the harness stands up a fresh proxy the way init.cc does (ncclProxyInit +
// ncclProxyCreate on a loopback listen socket, 2 ranks, 2 local ranks, the built-in socket net
// plugin), connects one client with the ncclSocket handshake (magic + ncclSocketTypeProxy, the
// connection preamble every proxy client sends) and writes the input as the raw byte stream that
// follows it. It then half-closes, drains the replies until the service drops the connection, and
// shuts the proxy down through a second, control connection that was opened first (so the service
// cannot exit before the fuzzed connection has been fully processed, whatever the input contains).
// Nothing survives the input: proxy state, sockets and threads are torn down every time.
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>

#include "comm.h"
#include "proxy.h"
#include "socket.h"
#include "ipcsocket.h"
#include "net.h"

static const uint64_t kMagic = NCCL_MAGIC;
static uint64_t gUdsHash;

extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv) {
  // Unique per process: the UDS name is an abstract socket shared by every process on the host.
  gUdsHash = ((uint64_t)getpid() << 20) ^ 0x6e63636cULL;
  return 0;
}

static void drain(int fd) {
  char buf[4096];
  for (;;) {
    ssize_t n = recv(fd, buf, sizeof(buf), 0);
    if (n > 0) continue;
    if (n < 0 && errno == EINTR) continue;
    break;
  }
}

static bool sendAll(int fd, const uint8_t* p, size_t n) {
  while (n) {
    ssize_t w = send(fd, p, n, MSG_NOSIGNAL);
    if (w < 0 && errno == EINTR) continue;
    if (w <= 0) return false;  // the service closed the connection early
    p += w;
    n -= (size_t)w;
  }
  return true;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  struct ncclComm* comm = new ncclComm();
  comm->sharedRes = new ncclSharedResources();
  comm->abortFlag = (uint32_t*)calloc(1, sizeof(uint32_t));
  comm->rank = 0;
  comm->nRanks = 2;
  comm->localRanks = 2;
  comm->cudaDev = 0;
  comm->ncclNet = &ncclNetSocket;
  comm->sharedRes->magic = kMagic;

  union ncclSocketAddress addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin.sin_family = AF_INET;
  addr.sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin.sin_port = 0;

  struct ncclSocket* listenSock = (struct ncclSocket*)calloc(1, sizeof(struct ncclSocket));
  union ncclSocketAddress* peerAddresses = (union ncclSocketAddress*)calloc(comm->nRanks, sizeof(union ncclSocketAddress));
  uint64_t* peerAddressesUDS = (uint64_t*)calloc(comm->nRanks, sizeof(uint64_t));
  peerAddressesUDS[0] = gUdsHash;
  peerAddressesUDS[1] = gUdsHash + 1;
  if (ncclSocketInit(listenSock, &addr, kMagic, ncclSocketTypeProxy, comm->abortFlag) != ncclSuccess ||
      ncclSocketListen(listenSock) != ncclSuccess) {
    fprintf(stderr, "fuzz_proxy_service: cannot listen on loopback\n");
    abort();
  }
  memcpy(&peerAddresses[0], &listenSock->addr, sizeof(union ncclSocketAddress));
  if (ncclProxyInit(comm, listenSock, peerAddresses, peerAddressesUDS) != ncclSuccess ||
      ncclProxyCreate(comm) != ncclSuccess) {
    fprintf(stderr, "fuzz_proxy_service: cannot start the proxy service\n");
    abort();
  }

  uint32_t clientAbort = 0;
  struct ncclSocket ctrl, client;
  if (ncclSocketInit(&ctrl, &listenSock->addr, kMagic, ncclSocketTypeProxy, &clientAbort) != ncclSuccess ||
      ncclSocketConnect(&ctrl) != ncclSuccess ||
      ncclSocketInit(&client, &listenSock->addr, kMagic, ncclSocketTypeProxy, &clientAbort) != ncclSuccess ||
      ncclSocketConnect(&client) != ncclSuccess) {
    fprintf(stderr, "fuzz_proxy_service: cannot connect to the proxy service\n");
    abort();
  }

  int fd = -1;
  ncclSocketGetFd(&client, &fd);
  sendAll(fd, data, size);
  shutdown(fd, SHUT_WR);
  drain(fd);  // returns once the service has closed this connection
  ncclSocketClose(&client);

  int type = ncclProxyMsgStop;
  ncclSocketSend(&ctrl, &type, sizeof(type));
  int ctrlFd = -1;
  ncclSocketGetFd(&ctrl, &ctrlFd);
  shutdown(ctrlFd, SHUT_WR);
  drain(ctrlFd);
  ncclSocketClose(&ctrl);

  struct ncclProxyState* proxyState = comm->proxyState;
  proxyState->thread.join();  // the service closes listenSock and frees it on exit

  // Same teardown as ncclProxyStop() with no peer sockets: drop the ref and flag the UDS thread,
  // then wake its poll() with one datagram so it sees the flag now rather than after its timeout.
  proxyState->refCount = 0;
  COMPILER_ATOMIC_STORE(&proxyState->stop, 1, std::memory_order_release);
  struct ncclIpcSocket wake = {0};
  if (ncclIpcSocketInit(&wake, 1, gUdsHash + 1, &clientAbort) == ncclSuccess) {
    uint64_t dummy = 0;
    (void)ncclIpcSocketSendMsg(&wake, &dummy, sizeof(dummy), NCCL_INVALID_IPC_FD, 0, gUdsHash);
    ncclIpcSocketClose(&wake);
  }
  proxyState->threadUDS.join();
  ncclProxyDestroy(comm);  // frees peerAddresses / peerAddressesUDS / proxyState

  free(comm->abortFlag);
  delete comm->sharedRes;
  delete comm;
  return 0;
}
