#include "sio/buffer.hpp"
#include "sio/mutable_buffer.hpp"
#include "sio/mutable_buffer_span.hpp"

#include <catch2/catch_all.hpp>
#include <cstring>

// TEST_CASE("mutable_buffer_span - Constructors") {
//   std::string s1("hello ");
//   std::string s2("world");
//   std::array<sio::mutable_buffer, 2> buffers{sio::buffer(s1), sio::buffer(s2)};
//   auto span = sio::mutable_buffer_span{buffers};
//
//   CHECK(span.size() == 2);
//   CHECK(span.buffer_size() == 11);
// }
//
// TEST_CASE("mutable_buffer_span - Iterate") {
//   std::string s1("hello ");
//   std::string s2("world");
//   std::array<sio::mutable_buffer, 2> buffers{sio::buffer(s1), sio::buffer(s2)};
//   auto span = sio::mutable_buffer_span{buffers};
//
//   CHECK(span.size() == 2);
//
//   auto first_buffer = *span.begin();
//   CHECK(first_buffer.size() == 6);
//   CHECK(::memcmp(first_buffer.data(), "hello ", 6) == 0);
//
//   auto last_buffer = *++span.begin();
//   CHECK(last_buffer.size() == 5);
//   CHECK(::memcmp(last_buffer.data(), "world", 5) == 0);
// }
//
// TEST_CASE("mutable_buffer_span - prefix") {
//   std::string s1("hello "); // byte size == 6
//   std::string s2("world");  // byte size == 5
//   std::array<sio::mutable_buffer, 2> buffers{sio::buffer(s1), sio::buffer(s2)};
//   auto span = sio::mutable_buffer_span{buffers};
//
//   CHECK(span.size() == 2);
//   CHECK(span.buffer_size() == 11);
//
//   SECTION("prefix within one buffer") {
//     auto data = span.prefix(5);
//     CHECK(data.size() == 1);
//     CHECK(data.buffer_size() == 5);
//   }
//
//   SECTION("Just get all the data from the first buffer") {
//     auto data = span.prefix(6);
//     CHECK(data.size() == 1);
//     CHECK(data.buffer_size() == 6);
//   }
//
//   SECTION("Fetch data from more than one buffer") {
//     auto data = span.prefix(7);
//     CHECK(data.size() == 2);
//     CHECK(data.buffer_size() == 7);
//   }
//
//   SECTION("Fetch data from all buffers") {
//     auto data = span.prefix(100);
//     CHECK(data.size() == 2);
//     CHECK(data.buffer_size() == 11);
//   }
// }
//
// TEST_CASE("mutable_buffer_span - suffix") {
//   std::string s1("hello "); // byte size == 6
//   std::string s2("world");  // byte size == 5
//   std::array<sio::mutable_buffer, 2> buffers{sio::buffer(s1), sio::buffer(s2)};
//   auto span = sio::mutable_buffer_span{buffers};
//
//   CHECK(span.size() == 2);
//   CHECK(span.buffer_size() == 11);
//
//   SECTION("suffix within one buffer") {
//     auto data = span.suffix(4);
//     CHECK(data.size() == 1);
//     CHECK(data.buffer_size() == 4);
//   }
//
//   SECTION("Just get all the data from the last buffer") {
//     auto data = span.suffix(5);
//     CHECK(data.size() == 1);
//     CHECK(data.buffer_size() == 5);
//   }
//
//   SECTION("Fetch data from more than one buffer") {
//     auto data = span.suffix(6);
//     CHECK(data.size() == 2);
//     CHECK(data.buffer_size() == 6);
//   }
//
//   SECTION("Fetch data from all buffers") {
//     auto data = span.suffix(100);
//     CHECK(data.size() == 2);
//     CHECK(data.buffer_size() == 11);
//   }
// }
//
// TEST_CASE("mutable_buffer_span - advance") {
//   std::string s1("hello "); // byte size == 6
//   std::string s2("world");  // byte size == 5
//   std::array<sio::mutable_buffer, 2> buffers{sio::buffer(s1), sio::buffer(s2)};
//   auto span = sio::mutable_buffer_span{buffers};
//
//   CHECK(span.size() == 2);
//   CHECK(span.buffer_size() == 11);
//
//   SECTION("advance within one buffer") {
//     span.advance(5);
//     CHECK(span.size() == 2);
//     CHECK(span.buffer_size() == 6);
//     CHECK(::memcmp((*span.begin()).data(), " ", 1) == 0);
//   }
//
//   SECTION("Exactly skip the first buffer") {
//     span.advance(6);
//     CHECK(span.size() == 1);
//     CHECK(span.buffer_size() == 5);
//   }
//
//   SECTION("Advance more than one buffer") {
//     span.advance(7);
//     CHECK(span.size() == 1);
//     CHECK(span.buffer_size() == 4);
//     CHECK(::memcmp((*span.begin()).data(), "orld", 1) == 0);
//   }
//
//   SECTION("Advance all buffers") {
//     span.advance(100);
//     CHECK(span.empty());
//   }
// }
//
// TEST_CASE("mutable_buffer_span - move and fetch") {
//   std::string s1("hello "); // byte size == 6
//   std::string s2("world");  // byte size == 5
//   std::array<sio::mutable_buffer, 2> buffers{sio::buffer(s1), sio::buffer(s2)};
//   auto span = sio::mutable_buffer_span{buffers};
//
//   CHECK(span.size() == 2);
//   CHECK(span.buffer_size() == 11);
//
//   SECTION("get prefix data then advance") {
//     auto data = span.prefix(5);
//     CHECK(data.size() == 1);
//     CHECK(data.buffer_size() == 5);
//     data.advance(2);
//     CHECK(data.size() == 1);
//     CHECK(data.buffer_size() == 3);
//   }
//
//   SECTION("get suffix data then advance") {
//     auto data = span.suffix(10);
//     CHECK(data.size() == 2);
//     CHECK(data.buffer_size() == 10);
//     data.advance(5);
//     CHECK(data.size() == 1);
//     CHECK(data.buffer_size() == 5);
//     auto iter = data.begin();
//     CHECK(::memcmp((*iter).data(), "world", 5) == 0);
//   }
// }
