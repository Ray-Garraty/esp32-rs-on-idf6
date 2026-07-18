#include "application/handlers/serial.hpp"

#include <cstdio>
#include <cstring>

#include "application/command.hpp"
#include "domain/memory.hpp"

namespace ecotiter::application::handlers::serial
{

std::expected<CommandResponse, domain::AppError> handlePing()
{
    return makeSingleResponse(std::string_view(R"({"status":"ok"})"),
                              std::string_view(R"({"status":"ok"})").size());
}

} // namespace ecotiter::application::handlers::serial
