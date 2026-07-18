// Build metadata — compiled separately so ccache can cache all other files

#include "version.h"
#include "version_build.h"

namespace ecotiter
{

const char* build_date = BUILD_DATE;
const char* git_hash = GIT_HASH;
const char* app_version = APP_VERSION;

} // namespace ecotiter
