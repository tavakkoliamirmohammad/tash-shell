# Append-only list of tash plugins.
#
# To add a plugin, add one `tash_register_plugin(...)` call at the bottom.
# See cmake/plugins.cmake for the full argument reference.

tash_register_plugin(
    NAME plugin_registry
    SOURCES src/plugins/plugin_registry.cpp
    TEST_SOURCES tests/unit/test_plugin_registry.cpp
    TEST_PREFIX "unit/plugins/"
)

tash_register_plugin(
    NAME config_resolver
    TEST_SOURCES tests/unit/test_config_resolver.cpp src/util/config_resolver.cpp
    TEST_PREFIX "unit/util/"
    TEST_STANDALONE
)

tash_register_plugin(
    NAME config_file
    TEST_SOURCES tests/unit/test_config_file.cpp src/util/config_file.cpp src/util/config_resolver.cpp src/util/io.cpp
    TEST_INCLUDES ${nlohmann_json_SOURCE_DIR}/include
    TEST_PREFIX "unit/util/"
    TEST_STANDALONE
)

tash_register_plugin(
    NAME fish_completion
    SOURCES src/plugins/fish_completion_provider.cpp
    TEST_SOURCES tests/unit/test_fish_completion.cpp
    TEST_INCLUDES ${nlohmann_json_SOURCE_DIR}/include
    TEST_PREFIX "unit/fish/"
)

tash_register_plugin(
    NAME fig_completion
    SOURCES src/plugins/fig_completion_provider.cpp
    TEST_SOURCES tests/unit/test_fig_completion.cpp
    TEST_INCLUDES ${nlohmann_json_SOURCE_DIR}/include
    TEST_PREFIX "unit/fig/"
)

tash_register_plugin(
    NAME theme
    SOURCES src/plugins/theme_provider.cpp
    TEST_SOURCES tests/unit/test_theme.cpp src/plugins/theme_provider.cpp
    TEST_DEFS TASH_THEMES_DIR="${CMAKE_SOURCE_DIR}/data/themes"
    TEST_PREFIX "unit/theme/"
    TEST_STANDALONE
)

tash_register_plugin(
    NAME starship
    SOURCES src/plugins/starship_prompt_provider.cpp
    TEST_SOURCES tests/unit/test_starship.cpp
    TEST_PREFIX "unit/starship/"
)

tash_register_plugin(
    NAME fuzzy_finder
    SOURCES src/ui/fuzzy_finder.cpp
    TEST_SOURCES tests/unit/test_fuzzy_finder.cpp
    TEST_PREFIX "unit/ui/"
)

tash_register_plugin(
    NAME ai
    SOURCES
        src/ai/ai_handler.cpp
        src/ai/ai_abort.cpp
        src/ai/llm_client.cpp
        src/ai/llm_diagnostics.cpp
        src/ai/llm_registry.cpp
        src/ai/ai_config.cpp
        src/ai/context_suggest.cpp
    TEST_SOURCES tests/unit/test_ai.cpp
    TEST_PREFIX "unit/ai/"
)

tash_register_plugin(
    NAME llm_diagnostics
    TEST_SOURCES tests/unit/test_llm_diagnostics.cpp
                 src/ai/llm_diagnostics.cpp
                 src/util/io.cpp
    TEST_PREFIX "unit/ai/"
    TEST_STANDALONE
)

tash_register_plugin(
    NAME ai_error_hook
    SOURCES src/plugins/ai_error_hook_provider.cpp
    TEST_SOURCES tests/unit/test_ai_error_hook.cpp
    TEST_PREFIX "unit/plugins/ai/"
)

tash_register_plugin(
    NAME key_file_perms
    TEST_SOURCES tests/unit/test_key_file_perms.cpp
    TEST_PREFIX "unit/ai/"
)

tash_register_plugin(
    NAME config_dir_migration
    TEST_SOURCES tests/unit/test_config_dir_migration.cpp
    TEST_PREFIX "unit/ai/"
)

tash_register_plugin(
    NAME sqlite_history
    SOURCES src/plugins/sqlite_history_provider.cpp
    REQUIRES TASH_SQLITE_ENABLED
    TEST_SOURCES tests/unit/test_sqlite_history.cpp src/plugins/sqlite_history_provider.cpp src/util/config_resolver.cpp src/util/io.cpp
    TEST_INCLUDES ${SQLite3_INCLUDE_DIRS}
    TEST_LIBS ${SQLite3_LIBRARIES}
    TEST_PREFIX "unit/sqlite/"
    TEST_STANDALONE
)

tash_register_plugin(
    NAME contextual_ai
    SOURCES src/ai/contextual_ai.cpp
    TEST_SOURCES tests/unit/test_contextual_ai.cpp
    TEST_PREFIX "unit/ai/"
)

tash_register_plugin(
    NAME safety_hook
    SOURCES src/plugins/safety_hook_provider.cpp
    TEST_SOURCES tests/unit/test_safety_hook.cpp
    TEST_PREFIX "unit/plugins/safety/"
)

tash_register_plugin(
    NAME inline_docs
    SOURCES src/ui/inline_docs.cpp
    TEST_SOURCES tests/unit/test_inline_docs.cpp
    TEST_PREFIX "unit/ui/"
)

tash_register_plugin(
    NAME alias_suggest
    SOURCES src/plugins/alias_suggest_provider.cpp
    TEST_SOURCES tests/unit/test_alias_suggest.cpp
    TEST_PREFIX "unit/plugins/"
)

tash_register_plugin(
    NAME clipboard
    SOURCES src/ui/clipboard.cpp
    TEST_SOURCES tests/unit/test_clipboard.cpp
    TEST_PREFIX "unit/ui/"
)

tash_register_plugin(
    NAME benchmark
    SOURCES src/util/benchmark.cpp
    TEST_SOURCES tests/unit/test_startup_benchmark.cpp
    TEST_PREFIX "unit/util/"
)

tash_register_plugin(
    NAME manpage_completion
    SOURCES src/plugins/manpage_completion_provider.cpp
    TEST_SOURCES tests/unit/test_manpage_completion.cpp
    TEST_PREFIX "unit/plugins/"
)

tash_register_plugin(
    NAME rich_output
    SOURCES src/ui/rich_output.cpp
    TEST_SOURCES tests/unit/test_rich_output.cpp
    TEST_PREFIX "unit/ui/"
)

tash_register_plugin(
    NAME block_renderer
    SOURCES src/ui/block_renderer.cpp
    TEST_SOURCES tests/unit/test_block_renderer.cpp
    TEST_PREFIX "unit/ui/"
)

tash_register_plugin(
    NAME session
    SOURCES src/core/session.cpp
    TEST_SOURCES tests/unit/test_session.cpp
    TEST_PREFIX "unit/session/"
)

tash_register_plugin(
    NAME config_sync
    SOURCES src/core/config_sync.cpp
    TEST_SOURCES tests/unit/test_config_sync.cpp
    TEST_PREFIX "unit/core/"
)

tash_register_plugin(
    NAME structured_pipe
    SOURCES src/core/structured_pipe.cpp
    TEST_SOURCES tests/unit/test_pipeline.cpp
    TEST_PREFIX "unit/core/"
)

tash_register_plugin(
    NAME heredoc_large
    TEST_SOURCES tests/unit/test_heredoc_large.cpp
    TEST_PREFIX "unit/core/"
)

tash_register_plugin(
    NAME hooked_capture
    TEST_SOURCES tests/unit/test_hooked_capture.cpp
    TEST_PREFIX "unit/core/"
)

tash_register_plugin(
    NAME hook_ordering
    TEST_SOURCES tests/unit/test_hook_ordering.cpp
    TEST_PREFIX "unit/core/"
)

tash_register_plugin(
    NAME safe_exec
    TEST_SOURCES tests/unit/test_safe_exec.cpp src/util/safe_exec.cpp
    TEST_PREFIX "unit/util/"
    TEST_STANDALONE
)

tash_register_plugin(
    NAME safe_tmpdir
    TEST_SOURCES tests/unit/test_safe_tmpdir.cpp src/util/safe_tmpdir.cpp
    TEST_PREFIX "unit/util/"
)

tash_register_plugin(
    NAME expansion_caps
    TEST_SOURCES tests/unit/test_expansion_caps.cpp
    TEST_PREFIX "unit/core/"
)

tash_register_plugin(
    NAME io
    TEST_SOURCES tests/unit/test_io.cpp src/util/io.cpp
    TEST_PREFIX "unit/util/"
    TEST_STANDALONE
)

tash_register_plugin(
    NAME fd
    TEST_SOURCES tests/unit/test_fd.cpp
    TEST_PREFIX "unit/util/"
    TEST_STANDALONE
)

tash_register_plugin(
    NAME crash_dump
    TEST_SOURCES tests/unit/test_crash_dump.cpp src/util/crash_dump.cpp
    TEST_DEFS TASH_CRASH_DUMP_TESTS
    TEST_PREFIX "unit/util/"
    TEST_STANDALONE
)

tash_register_plugin(
    NAME sqlite_history_like_escape
    REQUIRES TASH_SQLITE_ENABLED
    TEST_SOURCES tests/unit/test_sqlite_history_like_escape.cpp
                 src/plugins/sqlite_history_provider.cpp
                 src/util/config_resolver.cpp
                 src/util/io.cpp
    TEST_INCLUDES ${SQLite3_INCLUDE_DIRS}
    TEST_LIBS ${SQLite3_LIBRARIES}
    TEST_PREFIX "unit/sqlite/"
    TEST_STANDALONE
)

# ── Cluster subsystem (M0-scaffolded; built out in M1+) ────────
# A smoke test to prove the cluster unit-test target is wired up
# before any real engine/config/registry code lands. Later tasks
# add sibling tash_register_plugin() entries for config, registry,
# cluster_engine_*, watcher, etc.
tash_register_plugin(
    NAME cluster_smoke
    REQUIRES TASH_CLUSTER_ENABLED
    TEST_SOURCES tests/unit/cluster/smoke_test.cpp
    TEST_PREFIX "unit/cluster/"
)

tash_register_plugin(
    NAME cluster_config
    REQUIRES TASH_CLUSTER_ENABLED
    SOURCES src/cluster/config.cpp
    TEST_SOURCES tests/unit/cluster/config_test.cpp
    TEST_PREFIX "unit/cluster/"
    TEST_DEFS TASH_CLUSTER_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures/configs"
)

tash_register_plugin(
    NAME cluster_registry
    REQUIRES TASH_CLUSTER_ENABLED
    SOURCES src/cluster/registry.cpp
    TEST_SOURCES tests/unit/cluster/registry_test.cpp
    TEST_INCLUDES ${nlohmann_json_SOURCE_DIR}/include
    TEST_PREFIX "unit/cluster/"
)

tash_register_plugin(
    NAME cluster_presets
    REQUIRES TASH_CLUSTER_ENABLED
    SOURCES src/cluster/presets.cpp
    TEST_SOURCES tests/unit/cluster/presets_test.cpp
    TEST_PREFIX "unit/cluster/"
    TEST_DEFS TASH_CLUSTER_STOP_HOOKS_DIR="${CMAKE_SOURCE_DIR}/data/cluster/stop-hooks"
)

# ── Cluster seam fakes (header-only test doubles; shared by later
#    ClusterEngine / watcher tests) ────────────────────────────────
tash_register_plugin(
    NAME cluster_fakes
    REQUIRES TASH_CLUSTER_ENABLED
    TEST_SOURCES tests/unit/cluster/fakes/fakes_test.cpp
    TEST_PREFIX "unit/cluster/"
)

tash_register_plugin(
    NAME cluster_engine
    REQUIRES TASH_CLUSTER_ENABLED
    SOURCES src/cluster/cluster_engine.cpp
    TEST_SOURCES tests/unit/cluster/cluster_engine_up_test.cpp
    TEST_INCLUDES ${CMAKE_SOURCE_DIR}/tests/unit/cluster
    TEST_PREFIX "unit/cluster/"
)

tash_register_plugin(
    NAME cluster_engine_launch
    REQUIRES TASH_CLUSTER_ENABLED
    TEST_SOURCES tests/unit/cluster/cluster_engine_launch_test.cpp
    TEST_INCLUDES ${CMAKE_SOURCE_DIR}/tests/unit/cluster
    TEST_PREFIX "unit/cluster/"
)

tash_register_plugin(
    NAME cluster_engine_attach
    REQUIRES TASH_CLUSTER_ENABLED
    TEST_SOURCES tests/unit/cluster/cluster_engine_attach_test.cpp
    TEST_INCLUDES ${CMAKE_SOURCE_DIR}/tests/unit/cluster
    TEST_PREFIX "unit/cluster/"
)

tash_register_plugin(
    NAME cluster_engine_list
    REQUIRES TASH_CLUSTER_ENABLED
    TEST_SOURCES tests/unit/cluster/cluster_engine_list_test.cpp
    TEST_INCLUDES ${CMAKE_SOURCE_DIR}/tests/unit/cluster
    TEST_PREFIX "unit/cluster/"
)
tash_register_plugin(
    NAME cluster_engine_down
    REQUIRES TASH_CLUSTER_ENABLED
    TEST_SOURCES tests/unit/cluster/cluster_engine_down_test.cpp
    TEST_INCLUDES ${CMAKE_SOURCE_DIR}/tests/unit/cluster
    TEST_PREFIX "unit/cluster/"
)
tash_register_plugin(
    NAME cluster_engine_kill
    REQUIRES TASH_CLUSTER_ENABLED
    TEST_SOURCES tests/unit/cluster/cluster_engine_kill_test.cpp
    TEST_INCLUDES ${CMAKE_SOURCE_DIR}/tests/unit/cluster
    TEST_PREFIX "unit/cluster/"
)
tash_register_plugin(
    NAME cluster_engine_sync
    REQUIRES TASH_CLUSTER_ENABLED
    TEST_SOURCES tests/unit/cluster/cluster_engine_sync_test.cpp
    TEST_INCLUDES ${CMAKE_SOURCE_DIR}/tests/unit/cluster
    TEST_PREFIX "unit/cluster/"
)
tash_register_plugin(
    NAME cluster_engine_probe
    REQUIRES TASH_CLUSTER_ENABLED
    TEST_SOURCES tests/unit/cluster/cluster_engine_probe_test.cpp
    TEST_INCLUDES ${CMAKE_SOURCE_DIR}/tests/unit/cluster
    TEST_PREFIX "unit/cluster/"
)
tash_register_plugin(
    NAME cluster_engine_import
    REQUIRES TASH_CLUSTER_ENABLED
    TEST_SOURCES tests/unit/cluster/cluster_engine_import_test.cpp
    TEST_INCLUDES ${CMAKE_SOURCE_DIR}/tests/unit/cluster
    TEST_PREFIX "unit/cluster/"
)

tash_register_plugin(
    NAME cluster_watcher
    REQUIRES TASH_CLUSTER_ENABLED
    SOURCES src/cluster/watcher.cpp
    TEST_SOURCES tests/unit/cluster/watcher_test.cpp
    TEST_INCLUDES ${CMAKE_SOURCE_DIR}/tests/unit/cluster ${nlohmann_json_SOURCE_DIR}/include
    TEST_PREFIX "unit/cluster/"
)

tash_register_plugin(
    NAME cluster_builtin_dispatch
    REQUIRES TASH_CLUSTER_ENABLED
    SOURCES src/cluster/builtin_dispatch.cpp
    TEST_SOURCES tests/unit/cluster/cluster_builtin_test.cpp
    TEST_INCLUDES ${CMAKE_SOURCE_DIR}/tests/unit/cluster
    TEST_PREFIX "unit/cluster/"
)

tash_register_plugin(
    NAME cluster_demo_mode
    REQUIRES TASH_CLUSTER_ENABLED
    SOURCES src/cluster/demo_mode.cpp
    TEST_SOURCES tests/unit/cluster/demo_mode_test.cpp
    TEST_INCLUDES ${CMAKE_SOURCE_DIR}/tests/unit/cluster
    TEST_PREFIX "unit/cluster/"
)
