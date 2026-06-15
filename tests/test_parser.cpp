#include "yahs/parser.hpp"
#include <gtest/gtest.h>
#include <string>

TEST(ParserTest, GetRequest) {
  std::string req = "GET /index.html HTTP/1.1\r\n"
                    "Host: localhost\r\n"
                    "Connection: close\r\n"
                    "\r\n";
  yahs::parsedOut out;
  auto res = yahs::parse(req, out);
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res.value(), req.size());
  EXPECT_EQ(out.m, yahs::method::GET);
  EXPECT_EQ(out.path, "/index.html");
  EXPECT_EQ(out.v, yahs::version::v1_1);
  ASSERT_EQ(out.numHeader, 2);
  EXPECT_EQ(out.headers[0].first, "Host");
  EXPECT_EQ(out.headers[0].second, "localhost");
  EXPECT_EQ(out.headers[1].first, "Connection");
  EXPECT_EQ(out.headers[1].second, "close");
  EXPECT_TRUE(out.body.empty());
}

TEST(ParserTest, PostRequest) {
  std::string req = "POST /api/v1/users HTTP/1.0\r\n"
                    "Content-Type: application/json\r\n"
                    "Content-Length: 15\r\n"
                    "\r\n"
                    "{\"name\": \"test\"}";
  yahs::parsedOut out;
  auto res = yahs::parse(req, out);
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(out.m, yahs::method::POST);
  EXPECT_EQ(out.path, "/api/v1/users");
  EXPECT_EQ(out.v, yahs::version::v1_0);
  ASSERT_EQ(out.numHeader, 2);
  EXPECT_EQ(out.headers[0].first, "Content-Type");
  EXPECT_EQ(out.headers[0].second, "application/json");
  EXPECT_EQ(out.body, "{\"name\": \"test\"}");
}

TEST(ParserTest, LfOnlyLineEndings) {
  std::string req = "GET /path HTTP/1.1\n"
                    "Host: localhost\n"
                    "X-Header: value\n"
                    "\n";
  yahs::parsedOut out;
  auto res = yahs::parse(req, out);
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(out.m, yahs::method::GET);
  EXPECT_EQ(out.path, "/path");
  ASSERT_EQ(out.numHeader, 2);
  EXPECT_EQ(out.headers[1].first, "X-Header");
  EXPECT_EQ(out.headers[1].second, "value");
}

TEST(ParserTest, HeaderValueSpacesPreserved) {
  // not removing trailing whitespaces
  std::string req = "GET / HTTP/1.1\r\n"
                    "Host:   localhost \r\n"
                    "X-Tab-Trim:\tvalue\t\r\n"
                    "\r\n";
  yahs::parsedOut out;
  auto res = yahs::parse(req, out);
  ASSERT_TRUE(res.has_value());
  ASSERT_EQ(out.numHeader, 2);
  EXPECT_EQ(out.headers[0].first, "Host");
  EXPECT_EQ(out.headers[0].second, "  localhost ");
  EXPECT_EQ(out.headers[1].first, "X-Tab-Trim");
  EXPECT_EQ(out.headers[1].second, "value\t");
}

TEST(ParserTest, UnknownMethod) {
  std::string req = "INVALID_METHOD / HTTP/1.1\r\n"
                    "Host: localhost\r\n"
                    "\r\n";
  yahs::parsedOut out;
  auto res = yahs::parse(req, out);
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(out.m, yahs::method::UNKNOWN);
}

TEST(ParserTest, BadVersion) {
  std::string req = "GET / HTTP/1.2\r\n"
                    "Host: localhost\r\n"
                    "\r\n";
  yahs::parsedOut out;
  auto res = yahs::parse(req, out);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), yahs::error::invalidRequest);
}

TEST(ParserTest, IncompleteRequest) {
  std::string req = "GET / HTTP/1.1\r\n"
                    "Host: localhost";
  yahs::parsedOut out;
  auto res = yahs::parse(req, out);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), yahs::error::invalidRequest);
}

TEST(ParserTest, TooManyHeadersLimit) {
  std::string req = "GET / HTTP/1.1\r\n"
                    "H1: v\r\n"
                    "H2: v\r\n"
                    "H3: v\r\n"
                    "H4: v\r\n"
                    "H5: v\r\n"
                    "H6: v\r\n"
                    "H7: v\r\n"
                    "H8: v\r\n"
                    "H9: v\r\n"
                    "H10: v\r\n"
                    "H11: v\r\n"
                    "H12: v\r\n"
                    "H13: v\r\n"
                    "H14: v\r\n"
                    "H15: v\r\n"
                    "H16: v\r\n"
                    "H17: v\r\n"
                    "H18: v\r\n"
                    "H19: v\r\n"
                    "H20: v\r\n"
                    "H21: v\r\n"
                    "H22: v\r\n"
                    "H23: v\r\n"
                    "H24: v\r\n"
                    "H25: v\r\n"
                    "H26: v\r\n"
                    "\r\n";
  yahs::parsedOut out;
  auto res = yahs::parse(req, out);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), yahs::error::tooManyHeaders);
}
