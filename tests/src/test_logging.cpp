// Regression test: verify AckThen result logging present in source code.
// These patterns MUST be present after the ISSUE-009 fix (AckThen result logging).
// Since ESP-IDF headers are not available in the host test build,
// we verify the source code patterns directly via file I/O.

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <string>
#include <vector>

static constexpr auto MOTION_SRC =
    TESTS_SOURCE_DIR "/../components/infrastructure/src/motor/motion.cpp";
static constexpr auto TASK_SRC =
    TESTS_SOURCE_DIR "/../components/infrastructure/src/motor/task.cpp";
static constexpr auto MAIN_SRC =
    TESTS_SOURCE_DIR "/../main/main.cpp";

static std::vector<std::string> readFileLines(const std::string& path)
{
    std::vector<std::string> lines;
    std::ifstream file(path);
    if (!file.is_open())
        return lines;
    std::string line;
    while (std::getline(file, line))
        lines.push_back(line);
    return lines;
}

TEST_CASE("motion.cpp: store_result logs Motor complete", "[motion][logging][regression]")
{
    auto lines = readFileLines(MOTION_SRC);
    REQUIRE_FALSE(lines.empty());
    bool found = false;
    for (const auto& l : lines)
        if (l.find("Motor complete") != std::string::npos && l.find("ESP_LOGI") != std::string::npos)
            found = true;
    REQUIRE(found);
}

TEST_CASE("task.cpp: handleSetValvePosition logs Valve settled", "[task][logging][regression]")
{
    auto lines = readFileLines(TASK_SRC);
    REQUIRE_FALSE(lines.empty());
    bool found = false;
    for (const auto& l : lines)
        if (l.find("Valve settled") != std::string::npos && l.find("ESP_LOGI") != std::string::npos)
            found = true;
    REQUIRE(found);
}

TEST_CASE("task.cpp: handleReadTmcRegister logs SG result", "[task][logging][regression]")
{
    auto lines = readFileLines(TASK_SRC);
    REQUIRE_FALSE(lines.empty());
    bool found = false;
    for (const auto& l : lines)
        if (l.find("SG result") != std::string::npos && l.find("ESP_LOGI") != std::string::npos)
            found = true;
    REQUIRE(found);
}

TEST_CASE("main.cpp: waitResult loop logs SM result", "[main][logging][regression]")
{
    auto lines = readFileLines(MAIN_SRC);
    REQUIRE_FALSE(lines.empty());
    bool found = false;
    for (const auto& l : lines)
        if (l.find("SM result") != std::string::npos && l.find("ESP_LOGI") != std::string::npos)
            found = true;
    REQUIRE(found);
}
