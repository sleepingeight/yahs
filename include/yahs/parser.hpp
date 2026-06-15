#pragma once

#include <array>
#include <cstddef>
#include <expected>
#include <string_view>
#include <utility>

namespace yahs {

enum class method {
  UNKNOWN,
  GET,
  HEAD,
  POST,
  PUT,
  DELETE,
  CONNECT,
  OPTIONS,
  TRACE,
  PATCH
};

enum class version { v1_0, v1_1 };

const int MAX_HEADERS = 25;

struct parsedOut {
  method m;
  std::string_view path;
  version v;
  std::array<std::pair<std::string_view, std::string_view>, MAX_HEADERS>
      headers;
  size_t numHeader;
  std::string_view body;
};

enum class error { invalidRequest, tooManyHeaders };

static std::expected<size_t, error> parse(std::string_view request,
                                          parsedOut &out) {
  out.m = method::UNKNOWN;
  out.v = version::v1_0;
  out.numHeader = 0;
  out.body = {};

  const char *const req_data = request.data();
  const size_t size = request.size();
  const char *const end = req_data + size;
  const char *cursor = req_data;

  // 1. parse method
  const char *space = cursor;
  while (space < end && *space != ' ') {
    space++;
  }
  if (space == end) [[unlikely]] {
    return std::unexpected(error::invalidRequest);
  }
  size_t method_len = space - cursor;
  if (method_len == 3) {
    if (cursor[0] == 'G' && cursor[1] == 'E' && cursor[2] == 'T') [[likely]] {
      out.m = method::GET;
    } else if (cursor[0] == 'P' && cursor[1] == 'U' && cursor[2] == 'T') {
      out.m = method::PUT;
    }
  } else if (method_len == 4) {
    if (cursor[0] == 'P' && cursor[1] == 'O' && cursor[2] == 'S' &&
        cursor[3] == 'T') {
      out.m = method::POST;
    } else if (cursor[0] == 'H' && cursor[1] == 'E' && cursor[2] == 'A' &&
               cursor[3] == 'D') {
      out.m = method::HEAD;
    }
  } else if (method_len == 5) {
    if (cursor[0] == 'P' && cursor[1] == 'A' && cursor[2] == 'T' &&
        cursor[3] == 'C' && cursor[4] == 'H') {
      out.m = method::PATCH;
    } else if (cursor[0] == 'T' && cursor[1] == 'R' && cursor[2] == 'A' &&
               cursor[3] == 'C' && cursor[4] == 'E') {
      out.m = method::TRACE;
    }
  } else if (method_len == 6) {
    if (cursor[0] == 'D' && cursor[1] == 'E' && cursor[2] == 'L' &&
        cursor[3] == 'E' && cursor[4] == 'T' && cursor[5] == 'E') {
      out.m = method::DELETE;
    }
  } else if (method_len == 7) {
    if (cursor[0] == 'O' && cursor[1] == 'P' && cursor[2] == 'T' &&
        cursor[3] == 'I' && cursor[4] == 'O' && cursor[5] == 'N' &&
        cursor[6] == 'S') {
      out.m = method::OPTIONS;
    } else if (cursor[0] == 'C' && cursor[1] == 'O' && cursor[2] == 'N' &&
               cursor[3] == 'N' && cursor[4] == 'E' && cursor[5] == 'C' &&
               cursor[6] == 'T') {
      out.m = method::CONNECT;
    }
  }
  cursor = space + 1;

  // 2. Parse Path
  space =
      static_cast<const char *>(__builtin_memchr(cursor, ' ', end - cursor));
  if (!space) [[unlikely]] {
    return std::unexpected(error::invalidRequest);
  }
  out.path = std::string_view(cursor, space - cursor);
  cursor = space + 1;

  // 3. Parse Version
  const char *lf =
      static_cast<const char *>(__builtin_memchr(cursor, '\n', end - cursor));
  if (!lf) [[unlikely]] {
    return std::unexpected(error::invalidRequest);
  }
  const char *version_end = lf;
  if (version_end > cursor && *(version_end - 1) == '\r') [[likely]] {
    version_end--;
  }
  size_t version_len = version_end - cursor;
  if (version_len == 8 && cursor[0] == 'H' && cursor[1] == 'T' &&
      cursor[2] == 'T' && cursor[3] == 'P' && cursor[4] == '/' &&
      cursor[5] == '1' && cursor[6] == '.') [[likely]] {
    if (cursor[7] == '1')
      out.v = version::v1_1;
    else if (cursor[7] == '0')
      out.v = version::v1_0;
    else
      return std::unexpected(error::invalidRequest);
  } else {
    return std::unexpected(error::invalidRequest);
  }
  cursor = lf + 1;

  size_t &headCnt = out.numHeader;
  while (cursor < end && headCnt < MAX_HEADERS) {
    // check for request end
    if (*cursor == '\r' && (cursor + 1 < end) && (*(cursor + 1) == '\n')) {
      cursor += 2;
      break;
    } else if (*cursor == '\n') {
      cursor += 1;
      break;
    }
    lf =
        static_cast<const char *>(__builtin_memchr(cursor, '\n', end - cursor));
    if (!lf) [[unlikely]] {
      return std::unexpected(error::invalidRequest);
    }
    const char *col =
        static_cast<const char *>(__builtin_memchr(cursor, ':', lf - cursor));
    if (!col) [[unlikely]] {
      return std::unexpected(error::invalidRequest);
    }
    const char *headEnd = lf;
    if (headEnd > cursor && *(headEnd - 1) == '\r') {
      headEnd--;
    }
    auto &header = out.headers[headCnt++];
    header.first = std::string_view(cursor, col - cursor);
    header.second = std::string_view(col + 2, headEnd - (col + 2));
    cursor = lf + 1;
  }

  if (headCnt == MAX_HEADERS) [[unlikely]] {
    return std::unexpected(error::tooManyHeaders);
  }

  out.body = std::string_view(cursor, end - cursor);
  return (end - req_data);
}

} // namespace yahs
