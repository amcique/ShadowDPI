#include "application.hpp"
#include "service.hpp"
#include "win_headers.hpp"

int main(int argc, char* argv[])
{
    // Only load DLLs from safe locations, never from the current directory.
    SetDllDirectoryA("");
    SetSearchPathMode(BASE_SEARCH_PATH_ENABLE_SAFE_SEARCHMODE |
                      BASE_SEARCH_PATH_PERMANENT);

    if (sdp::win::run_as_service_if_requested(argc, argv)) {
        return 0;
    }

    return sdp::win::run_application(argc, argv);
}
