#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string_view>

#include "interface/serial.hpp"

using namespace ecotiter::interface;

TEST_CASE("SerialReader: single line", "[serial]") {
    SerialReader reader;
    auto line = reader.process("hello\n");
    REQUIRE(line);
    CHECK(*line == "hello");
}

TEST_CASE("SerialReader: two lines in one call", "[serial]") {
    SerialReader reader;
    auto line1 = reader.process("line1\nline2\n");
    REQUIRE(line1);
    CHECK(*line1 == "line1");

    auto line2 = reader.process("");
    REQUIRE(line2);
    CHECK(*line2 == "line2");
}

TEST_CASE("SerialReader: line across multiple calls", "[serial]") {
    SerialReader reader;
    auto part1 = reader.process("hel");
    CHECK_FALSE(part1.has_value());

    auto part2 = reader.process("lo\n");
    REQUIRE(part2);
    CHECK(*part2 == "hello");
}

TEST_CASE("SerialReader: CR handling", "[serial]") {
    SerialReader reader;
    auto line = reader.process("line1\r\nline2\r\n");
    REQUIRE(line);
    CHECK(*line == "line1");

    auto line2 = reader.process("");
    REQUIRE(line2);
    CHECK(*line2 == "line2");
}

TEST_CASE("SerialReader: CR in middle of line", "[serial]") {
    SerialReader reader;
    auto line = reader.process("he\rllo\n");
    REQUIRE(line);
    CHECK(*line == "hello");
}

TEST_CASE("SerialReader: empty input", "[serial]") {
    SerialReader reader;
    auto line = reader.process("");
    CHECK_FALSE(line.has_value());
}

TEST_CASE("SerialReader: multiple empty lines", "[serial]") {
    SerialReader reader;
    auto line = reader.process("\n\n\n");
    CHECK_FALSE(line.has_value());
}

TEST_CASE("SerialReader: consecutive non-empty lines", "[serial]") {
    SerialReader reader;
    auto line1 = reader.process("a\nb\nc\n");
    REQUIRE(line1);
    CHECK(*line1 == "a");

    auto line2 = reader.process("");
    REQUIRE(line2);
    CHECK(*line2 == "b");

    auto line3 = reader.process("");
    REQUIRE(line3);
    CHECK(*line3 == "c");

    auto line4 = reader.process("");
    CHECK_FALSE(line4.has_value());
}

TEST_CASE("SerialReader: overflow reset", "[serial]") {
    SerialReader reader;
    // Build a line longer than MAX_CMD_SIZE
    std::array<char, ecotiter::domain::memory::MAX_CMD_SIZE + 10> longLine{};
    longLine.fill('x');
    longLine[ecotiter::domain::memory::MAX_CMD_SIZE + 6] = '\n';
    longLine[ecotiter::domain::memory::MAX_CMD_SIZE + 7] = 'o';
    longLine[ecotiter::domain::memory::MAX_CMD_SIZE + 8] = 'k';
    longLine[ecotiter::domain::memory::MAX_CMD_SIZE + 9] = '\n';

    std::string_view input(longLine.data(), longLine.size());
    // First chunk (at the overflow point) should discard
    auto chunk = input.substr(0, ecotiter::domain::memory::MAX_CMD_SIZE - 5);
    auto result = reader.process(chunk);
    CHECK_FALSE(result.has_value());

    // Feed the overflow data and newline
    auto overflow = input.substr(ecotiter::domain::memory::MAX_CMD_SIZE - 5);
    result = reader.process(overflow);
    CHECK_FALSE(result.has_value());

    // "ok" should be the next line
    result = reader.process("ok\n");
    REQUIRE(result);
    CHECK(*result == "ok");
}

TEST_CASE("SerialReader: no newline no output", "[serial]") {
    SerialReader reader;
    auto result = reader.process("no newline here");
    CHECK_FALSE(result.has_value());

    // Nothing accumulated from previous call
    result = reader.process("");
    CHECK_FALSE(result.has_value());
}

TEST_CASE("SerialReader: exact max size without overflow", "[serial]") {
    SerialReader reader;
    std::array<char, ecotiter::domain::memory::MAX_CMD_SIZE> exact{};
    for (size_t i = 0; i + 1 < exact.size(); ++i) {
        exact[i] = 'a';
    }
    exact[exact.size() - 1] = '\n';

    auto result = reader.process(std::string_view(exact.data(), exact.size()));
    REQUIRE(result);
    CHECK(result->size() == ecotiter::domain::memory::MAX_CMD_SIZE - 1);
}
