#include <assert.h>
#include <fcntl.h>
#include <liburing.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <atomic>
#include <cctype>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

constexpr const char *SERVER_STRING = "Server: yahs/0.1\r\n";
constexpr int DEFAULT_SERVER_PORT = 8000;
constexpr int QUEUE_DEPTH = 512;
constexpr int READ_SZ = 8192;

constexpr int EVENT_TYPE_ACCEPT = 0;
constexpr int EVENT_TYPE_READ = 1;
constexpr int EVENT_TYPE_WRITE = 2;

constexpr int MIN_KERNEL_VERSION = 5;
constexpr int MIN_MAJOR_VERSION = 5;

/* Prebuilt static responses */
static const char *unimplemented_content =
    "HTTP/1.0 400 Bad Request\r\n"
    "Content-type: text/html\r\n"
    "\r\n"
    "<html>"
    "<head><title>yahs: Unimplemented</title></head>"
    "<body>"
    "<h1>Bad Request (Unimplemented)</h1>"
    "<p>Your client sent a request yahs did not understand and it is "
    "probably not your fault.</p>"
    "</body>"
    "</html>";

static const char *http_404_content =
    "HTTP/1.0 404 Not Found\r\n"
    "Content-type: text/html\r\n"
    "\r\n"
    "<html>"
    "<head><title>yahs: Not Found</title></head>"
    "<body>"
    "<h1>Not Found (404)</h1>"
    "<p>Your client is asking for an object that was not found on this "
    "server.</p>"
    "</body>"
    "</html>";

struct Request {
  int eventType;
  int iovecCount;    // number of iovecs in 'iov'
  int clientSocket;  // valid for READ/WRITE
  struct iovec *iov; // dynamically allocated array of iovec

  Request(int ev, int sock = -1)
      : eventType(ev), iovecCount(0), clientSocket(sock), iov(nullptr) {}
};

static void fatalError(const char *syscall) {
  perror(syscall);
  std::exit(1);
}

static bool checkKernelVersion() {
  struct utsname buffer;
  if (uname(&buffer) != 0) {
    perror("uname");
    std::exit(EXIT_FAILURE);
  }

  int verMajor = 0, verMinor = 0;
  {
    const char *p = buffer.release;
    if (std::isdigit(*p)) {
      verMajor = std::strtol(p, const_cast<char **>(&p), 10);
      if (*p == '.') {
        ++p;
        verMinor = std::strtol(p, nullptr, 10);
      }
    }
  }

  std::cout << "Minimum kernel version required is: " << MIN_KERNEL_VERSION
            << "." << MIN_MAJOR_VERSION << "\n";
  std::cout << "Your kernel version is: " << verMajor << "." << verMinor
            << "\n";

  if (verMajor < MIN_KERNEL_VERSION ||
      (verMajor == MIN_KERNEL_VERSION && verMinor < MIN_MAJOR_VERSION)) {
    std::cerr << "Error: your kernel version is too old (" << verMajor << "."
              << verMinor << ")\n";
    return false;
  }
  return true;
}

static void checkForIndexFile() {
  struct stat st;
  if (stat("public/index.html", &st) < 0) {
    std::cerr << "yahs needs the \"public\" directory to be "
              << "present in the current directory.\n";
    fatalError("stat: public/index.html");
  }
}

static void toLowerCase(char *str) {
  for (; *str; ++str) {
    *str = static_cast<char>(std::tolower(*str));
  }
}

static void addAcceptRequest(int serverSocket, struct sockaddr_in *clientAddr,
                             socklen_t *clientAddrLen, io_uring *ringPtr) {
  struct io_uring_sqe *sqe = io_uring_get_sqe(ringPtr);
  if (!sqe)
    fatalError("io_uring_get_sqe (accept)");

  io_uring_prep_accept(sqe, serverSocket,
                       reinterpret_cast<struct sockaddr *>(clientAddr),
                       clientAddrLen, 0);

  Request *req = new Request(EVENT_TYPE_ACCEPT);
  io_uring_sqe_set_data(sqe, req);

  int ret = io_uring_submit(ringPtr);
  if (ret < 0)
    fatalError("io_uring_submit (accept)");
}

static void addReadRequest(int clientSocket, io_uring *ringPtr) {
  struct io_uring_sqe *sqe = io_uring_get_sqe(ringPtr);
  if (!sqe)
    fatalError("io_uring_get_sqe (read)");

  Request *req = new Request(EVENT_TYPE_READ, clientSocket);
  req->iovecCount = 1;
  req->iov = new iovec[1];

  char *buf = new char[READ_SZ];
  std::memset(buf, 0, READ_SZ);
  req->iov[0].iov_base = buf;
  req->iov[0].iov_len = READ_SZ;

  io_uring_prep_readv(sqe, clientSocket, req->iov, 1, 0);
  io_uring_sqe_set_data(sqe, req);

  int ret = io_uring_submit(ringPtr);
  if (ret < 0)
    fatalError("io_uring_submit (read)");
}

static void addWriteRequest(Request *req, io_uring *ringPtr) {
  struct io_uring_sqe *sqe = io_uring_get_sqe(ringPtr);
  if (!sqe)
    fatalError("io_uring_get_sqe (write)");

  req->eventType = EVENT_TYPE_WRITE;
  io_uring_prep_writev(sqe, req->clientSocket, req->iov, req->iovecCount, 0);
  io_uring_sqe_set_data(sqe, req);

  int ret = io_uring_submit(ringPtr);
  if (ret < 0)
    fatalError("io_uring_submit (write)");
}

static void sendStaticString(const char *str, int clientSocket,
                             io_uring *ringPtr) {
  size_t slen = std::strlen(str);
  Request *req = new Request(EVENT_TYPE_WRITE, clientSocket);
  req->iovecCount = 1;
  req->iov = new iovec[1];

  char *buf = new char[slen];
  std::memcpy(buf, str, slen);
  req->iov[0].iov_base = buf;
  req->iov[0].iov_len = slen;

  addWriteRequest(req, ringPtr);
}

static void handleUnimplementedMethod(int clientSocket, io_uring *ringPtr) {
  sendStaticString(unimplemented_content, clientSocket, ringPtr);
}

static void handleHttp404(int clientSocket, io_uring *ringPtr) {
  sendStaticString(http_404_content, clientSocket, ringPtr);
}

/* Read entire file into a newly allocated buffer, attach it to iovec */
static void copyFileContents(const std::string &filePath, off_t fileSize,
                             iovec *outIov) {
  int fd = open(filePath.c_str(), O_RDONLY);
  if (fd < 0) {
    fatalError("open (copyFileContents)");
  }

  char *buf = new char[fileSize];
  ssize_t bytesRead = ::read(fd, buf, fileSize);
  if (bytesRead < 0) {
    close(fd);
    fatalError("read (copyFileContents)");
  }
  if (static_cast<off_t>(bytesRead) < fileSize) {
    std::cerr << "Encountered a short read (" << bytesRead << " of " << fileSize
              << ")\n";
  }
  close(fd);

  outIov->iov_base = buf;
  outIov->iov_len = fileSize;
}

static std::string getFilenameExt(const std::string &filename) {
  size_t pos = filename.find_last_of('.');
  if (pos == std::string::npos || pos + 1 >= filename.size())
    return "";
  return filename.substr(pos + 1);
}

/* Send HTTP/1.0 200 OK headers into the first 5 iovecs.
 * iov must have space for at least 5 entries. */
static void sendHeaders(const std::string &path, off_t len, iovec *iov) {
  std::string lowerPath = path;
  for (auto &c : lowerPath)
    c = static_cast<char>(std::tolower(c));

  // iov[0]: "HTTP/1.0 200 OK\r\n"
  {
    const char *hdr = "HTTP/1.0 200 OK\r\n";
    size_t slen = std::strlen(hdr);
    char *buf = new char[slen];
    std::memcpy(buf, hdr, slen);
    iov[0].iov_base = buf;
    iov[0].iov_len = slen;
  }

  // iov[1]: SERVER_STRING
  {
    size_t slen = std::strlen(SERVER_STRING);
    char *buf = new char[slen];
    std::memcpy(buf, SERVER_STRING, slen);
    iov[1].iov_base = buf;
    iov[1].iov_len = slen;
  }

  // iov[2]: Content-Type based on extension
  std::string ext = getFilenameExt(lowerPath);
  std::string contentType = "Content-Type: text/plain\r\n"; // fallback
  if (ext == "jpg" || ext == "jpeg") {
    contentType = "Content-Type: image/jpeg\r\n";
  } else if (ext == "png") {
    contentType = "Content-Type: image/png\r\n";
  } else if (ext == "gif") {
    contentType = "Content-Type: image/gif\r\n";
  } else if (ext == "htm" || ext == "html") {
    contentType = "Content-Type: text/html\r\n";
  } else if (ext == "js") {
    contentType = "Content-Type: application/javascript\r\n";
  } else if (ext == "css") {
    contentType = "Content-Type: text/css\r\n";
  } else if (ext == "txt") {
    contentType = "Content-Type: text/plain\r\n";
  }

  {
    size_t slen = contentType.size();
    char *buf = new char[slen];
    std::memcpy(buf, contentType.c_str(), slen);
    iov[2].iov_base = buf;
    iov[2].iov_len = slen;
  }

  // iov[3]: "Content-Length: <len>\r\n"
  {
    char lenBuf[64];
    int written =
        std::snprintf(lenBuf, sizeof(lenBuf), "Content-Length: %lld\r\n",
                      static_cast<long long>(len));
    size_t slen = static_cast<size_t>(written);
    char *buf = new char[slen];
    std::memcpy(buf, lenBuf, slen);
    iov[3].iov_base = buf;
    iov[3].iov_len = slen;
  }

  // iov[4]: blank line "\r\n"
  {
    const char *brk = "\r\n";
    size_t slen = std::strlen(brk);
    char *buf = new char[slen];
    std::memcpy(buf, brk, slen);
    iov[4].iov_base = buf;
    iov[4].iov_len = slen;
  }
}

/* Handle a GET request: determine final path, stat it, and serve or 404 */
static void handleGetMethod(const std::string &rawPath, int clientSocket,
                            io_uring *ringPtr) {
  std::string finalPath = "public";
  if (!rawPath.empty() && rawPath.back() == '/') {
    finalPath += rawPath + "index.html";
  } else {
    finalPath += rawPath;
  }

  struct stat st;
  if (stat(finalPath.c_str(), &st) < 0) {
    // std::cout << "404 Not Found: " << finalPath << " (" << rawPath <<
    // ")\n";
    handleHttp404(clientSocket, ringPtr);
    return;
  }

  if (S_ISREG(st.st_mode)) {
    // Prepare a Request with 6 iovecs: 5 for headers, 1 for file body
    Request *req = new Request(EVENT_TYPE_WRITE, clientSocket);
    req->iovecCount = 6;
    req->iov = new iovec[6];

    // Send headers into iov[0..4]
    sendHeaders(finalPath, st.st_size, req->iov);

    // iov[5] = file contents
    copyFileContents(finalPath, st.st_size, &req->iov[5]);

    // std::cout << "200 " << finalPath << " " << st.st_size << " bytes\n";
    addWriteRequest(req, ringPtr);
  } else {
    // Not a regular file (e.g., directory), treat as 404
    handleHttp404(clientSocket, ringPtr);
  }
}

static void handleHttpMethod(char *methodBuffer, int clientSocket,
                             io_uring *ringPtr) {
  // methodBuffer contains something like "GET /path HTTP/1.1\r\n..."
  // Extract method and path
  char *saveptr = nullptr;
  char *method = strtok_r(methodBuffer, " ", &saveptr);
  if (!method) {
    handleUnimplementedMethod(clientSocket, ringPtr);
    return;
  }
  for (char *p = method; *p; ++p)
    *p = static_cast<char>(std::tolower(*p));

  char *path = strtok_r(nullptr, " ", &saveptr);
  if (!path) {
    handleUnimplementedMethod(clientSocket, ringPtr);
    return;
  }

  if (std::strcmp(method, "get") == 0) {
    handleGetMethod(path, clientSocket, ringPtr);
  } else {
    handleUnimplementedMethod(clientSocket, ringPtr);
  }
}

static bool getRequestLine(const char *src, char *dest, int destSz) {
  for (int i = 0; i + 1 < destSz; ++i) {
    dest[i] = src[i];
    if (src[i] == '\r' && src[i + 1] == '\n') {
      dest[i] = '\0';
      return true;
    }
  }
  return false;
}

static void handleClientRequest(Request *req, io_uring *ringPtr) {
  // Buffer from the read is in req->iov[0].iov_base
  char httpRequest[1024];
  if (!getRequestLine(reinterpret_cast<const char *>(req->iov[0].iov_base),
                      httpRequest, sizeof(httpRequest))) {
    std::cerr << "Malformed request\n";
    return;
  }
  handleHttpMethod(httpRequest, req->clientSocket, ringPtr);
}

/* Each worker thread runs this loop. It has its own ring. */
static void workerLoop(int serverSocket) {
  io_uring ring;
  if (io_uring_queue_init(QUEUE_DEPTH, &ring, 0) < 0) {
    fatalError("io_uring_queue_init (worker)");
  }

  // Used for accepts:
  struct sockaddr_in clientAddr {};
  socklen_t clientAddrLen = sizeof(clientAddr);

  // Submit the very first accept on *this* ring
  addAcceptRequest(serverSocket, &clientAddr, &clientAddrLen, &ring);

  while (true) {
    struct io_uring_cqe *cqe;
    int ret = io_uring_wait_cqe(&ring, &cqe);
    if (ret < 0) {
      fatalError("io_uring_wait_cqe (worker)");
    }

    Request *req = reinterpret_cast<Request *>(io_uring_cqe_get_data(cqe));
    if (cqe->res < 0) {
      std::cerr << "Async request failed: "
                << std::strerror(static_cast<int>(-cqe->res))
                << " for event: " << req->eventType << "\n";
      std::exit(1);
    }

    switch (req->eventType) {
    case EVENT_TYPE_ACCEPT: {
      // cqe->res is the new client socket
      int clientSock = cqe->res;

      // Immediately re‐submit ACCEPT on *this* ring for the next
      // incoming connection
      addAcceptRequest(serverSocket, &clientAddr, &clientAddrLen, &ring);

      // Now submit a READ for this newly accepted client
      addReadRequest(clientSock, &ring);

      // We don't have any buffers to free in an ACCEPT req
      delete req;
      break;
    }

    case EVENT_TYPE_READ: {
      // cqe->res > 0 means number of bytes read (or 0 if connection
      // closed)
      if (cqe->res == 0) {
        // Client closed immediately; just clean up
      } else {
        handleClientRequest(req, &ring);
      }
      // Free the read buffer and the Request struct
      char *readBuf = reinterpret_cast<char *>(req->iov[0].iov_base);
      delete[] readBuf;
      delete[] req->iov;
      delete req;
      break;
    }

    case EVENT_TYPE_WRITE: {
      // After writing, free all buffers in req->iov[], then close the
      // socket
      for (int i = 0; i < req->iovecCount; ++i) {
        char *buf = reinterpret_cast<char *>(req->iov[i].iov_base);
        delete[] buf;
      }
      delete[] req->iov;
      close(req->clientSocket);
      delete req;
      break;
    }

    default:
      std::cerr << "Unknown event type: " << req->eventType << "\n";
      delete req;
      std::exit(1);
    }

    io_uring_cqe_seen(&ring, cqe);
  }

  // Unreachable, but clean up if we ever exit:
  io_uring_queue_exit(&ring);
}

static std::atomic<bool> g_terminate{false};
static void sigintHandler(int /*signo*/) {
  // std::cout << "\n^C pressed. Shutting down all threads...\n";
  g_terminate = true;
}

int main() {
  if (!checkKernelVersion()) {
    return EXIT_FAILURE;
  }
  checkForIndexFile();

  // one listening socket (shared by all workers)
  int serverSocket = socket(PF_INET, SOCK_STREAM, 0);
  if (serverSocket < 0) {
    fatalError("socket()");
  }

  // enabling both SO_REUSEADDR and SO_REUSEPORT so multiple threads can
  // bind/accept on same port
  int enable = 1;
  if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &enable,
                 sizeof(enable)) < 0) {
    fatalError("setsockopt(SO_REUSEADDR)");
  }
  if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEPORT, &enable,
                 sizeof(enable)) < 0) {
    fatalError("setsockopt(SO_REUSEPORT)");
  }

  struct sockaddr_in srvAddr {};
  std::memset(&srvAddr, 0, sizeof(srvAddr));
  srvAddr.sin_family = AF_INET;
  srvAddr.sin_port = htons(DEFAULT_SERVER_PORT);
  srvAddr.sin_addr.s_addr = htonl(INADDR_ANY);

  if (bind(serverSocket, reinterpret_cast<const struct sockaddr *>(&srvAddr),
           sizeof(srvAddr)) < 0) {
    fatalError("bind()");
  }

  if (listen(serverSocket, 512) < 0) {
    fatalError("listen()");
  }

  // std::cout << "yahs listening on port: " << DEFAULT_SERVER_PORT <<
  // "\n";

  std::signal(SIGINT, sigintHandler);

  // 4 worker threads; each one will do accept/read/write on its own
  // io_uring

  constexpr int NUM_WORKERS = 4;

  std::vector<cpu_set_t> cpus(NUM_WORKERS);
  for (int i = 0; i < NUM_WORKERS; i++) {
    CPU_ZERO(&cpus[i]);
    CPU_SET(i, &cpus[i]);
  }

  std::vector<std::thread> workers;
  workers.reserve(NUM_WORKERS);

  for (int i = 0; i < NUM_WORKERS; ++i) {
    workers.emplace_back([serverSocket]() { workerLoop(serverSocket); });
    assert(pthread_setaffinity_np(workers[i].native_handle(), sizeof(cpu_set_t),
                                  &cpus[i]) == 0);
  }

  for (;;) {
    if (g_terminate.load()) {
      std::exit(0);
    }
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  return 0;
}
