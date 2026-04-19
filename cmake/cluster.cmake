# tash cluster subsystem — SLURM-backed remote launcher.
#
# Declares:
#   TASH_CLUSTER                 user option (default ON)
#   TASH_CLUSTER_ENABLED         derived flag; used by plugin_list.cmake's
#                                REQUIRES gate so cluster sources drop out
#                                cleanly when -DTASH_CLUSTER=OFF
#   tomlplusplus (fetched)       v3.4.0 — header-only TOML parser used by
#                                src/cluster/config.cpp
#
# Plugin and builtin sources for the cluster subsystem are registered in
# cmake/plugin_list.cmake via tash_register_plugin(REQUIRES TASH_CLUSTER_ENABLED …)
# so the guard is applied in one place.

option(TASH_CLUSTER "Build cluster (SLURM) support" ON)

if(TASH_CLUSTER)
    include(FetchContent)
    FetchContent_Declare(
        tomlplusplus
        URL https://github.com/marzer/tomlplusplus/archive/refs/tags/v3.4.0.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    # Skip toml++'s own tests/examples — we only need the headers.
    set(TOMLPLUSPLUS_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(TOMLPLUSPLUS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(tomlplusplus)
    set(TASH_CLUSTER_ENABLED TRUE)
    message(STATUS "Cluster (SLURM) support enabled (toml++ fetched)")
else()
    set(TASH_CLUSTER_ENABLED FALSE)
    message(STATUS "Cluster (SLURM) support disabled (-DTASH_CLUSTER=OFF)")
endif()
