#pragma once

#include <array>
#include <stdexcept>
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

struct parsedOut {
  method m;
  std::string_view path;
  version v;
  std::array<std::pair<std::string_view, std::string_view>, 25> headers;
  size_t num_header;
};

static int parse(std::string_view request, parsedOut &out) {
  out.m = method::UNKNOWN;
  out.v = version::v1_0;
  out.num_header = 0;

  size_t cursor = 0;
  const size_t size = request.size();

  // 1. Parse Method
  size_t next = request.find(' ', cursor);
  if (next == std::string_view::npos) [[unlikely]] {
    throw std::runtime_error("invalid request\n");
  }
  std::string_view method_sv = request.substr(cursor, next - cursor);

  if (method_sv.size() == 3) {
    if (method_sv[0] == 'G' && method_sv[1] == 'E' && method_sv[2] == 'T')
        [[likely]] {
      out.m = method::GET;
    } else if (method_sv[0] == 'P' && method_sv[1] == 'U' &&
               method_sv[2] == 'T') {
      out.m = method::PUT;
    }
  } else if (method_sv.size() == 4) {
    if (method_sv[0] == 'P' && method_sv[1] == 'O' && method_sv[2] == 'S' &&
        method_sv[3] == 'T') {
      out.m = method::POST;
    } else if (method_sv[0] == 'H' && method_sv[1] == 'E' &&
               method_sv[2] == 'A' && method_sv[3] == 'D') {
      out.m = method::HEAD;
    }
  } else if (method_sv.size() == 5) {
    if (method_sv == "PATCH") {
      out.m = method::PATCH;
    } else if (method_sv == "TRACE") {
      out.m = method::TRACE;
    }
  } else if (method_sv.size() == 6) {
    if (method_sv == "DELETE") {
      out.m = method::DELETE;
    }
  } else if (method_sv.size() == 7) {
    if (method_sv == "OPTIONS") {
      out.m = method::OPTIONS;
    } else if (method_sv == "CONNECT") {
      out.m = method::CONNECT;
    }
  }

  cursor = next + 1;

  // 2. Parse Path
  next = request.find(' ', cursor);
  if (next == std::string_view::npos) [[unlikely]] {
    throw std::runtime_error("invalid request\n");
  }
  out.path = request.substr(cursor, next - cursor);
  cursor = next + 1;

  // 3. Parse Version
  next = request.find('\n', cursor);
  if (next == std::string_view::npos) [[unlikely]] {
    throw std::runtime_error("invalid request\n");
  }
  size_t version_end = next;
  if (version_end > cursor && request[version_end - 1] == '\r') {
    version_end--;
  }
  std::string_view version_sv = request.substr(cursor, version_end - cursor);
  if (version_sv == "HTTP/1.1") [[likely]] {
    out.v = version::v1_1;
  } else {
    out.v = version::v1_0;
  }
  cursor = next + 1;

  // 4. Parse Headers
  while (cursor < size) {
    // Check for end of headers
    if (request[cursor] == '\r') {
      if (cursor + 1 < size && request[cursor + 1] == '\n') {
        cursor += 2;
      } else {
        cursor += 1;
      }
      break;
    }
    if (request[cursor] == '\n') {
      cursor += 1;
      break;
    }

    size_t lf = request.find('\n', cursor);
    if (lf == std::string_view::npos) [[unlikely]] {
      lf = size;
    }
    size_t line_end = lf;
    if (line_end > cursor && request[line_end - 1] == '\r') {
      line_end--;
    }

    size_t colon = request.find(':', cursor);
    if (colon == std::string_view::npos || colon >= line_end) [[unlikely]] {
      throw std::runtime_error("invalid request\n");
    }

    std::string_view name = request.substr(cursor, colon - cursor);
    // Trim trailing spaces/tabs from name
    while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) {
      name.remove_suffix(1);
    }
    // Trim leading spaces/tabs from name
    while (!name.empty() && (name.front() == ' ' || name.front() == '\t')) {
      name.remove_prefix(1);
    }

    size_t val_start = colon + 1;
    while (val_start < line_end &&
           (request[val_start] == ' ' || request[val_start] == '\t')) {
      val_start++;
    }
    std::string_view value = request.substr(val_start, line_end - val_start);
    // Trim trailing spaces/tabs from value
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
      value.remove_suffix(1);
    }

    if (out.num_header < out.headers.size()) [[likely]] {
      out.headers[out.num_header++] = {name, value};
    } else {
      throw std::runtime_error("too many headers\n");
    }

    cursor = lf + 1;
  }

  return 0;
}

} // namespace yahs
