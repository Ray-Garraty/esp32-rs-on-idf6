#include "application/handlers/serial.hpp"

#include <cstdio>
#include <cstring>

#include "application/command.hpp"
#include "domain/memory.hpp"

namespace ecotiter::application::handlers::serial {

std::expected<CommandResponse, domain::AppError> handlePing() {
  CommandResponse rsp;
  rsp.kind = ResponseKind::Single;
  rsp.bodySize = static_cast<size_t>(
      std::snprintf(rsp.body.data(), rsp.body.size(),
                    R"({"cmd":"serial.ping","result":"pong"})"));
  return rsp;
}

} // namespace ecotiter::application::handlers::serial
