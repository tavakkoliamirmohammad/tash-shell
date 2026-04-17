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
        src/ai/llm_client.cpp
        src/ai/ai_config.cpp
        src/ai/context_suggest.cpp
    REQUIRES TASH_AI_ENABLED
    TEST_SOURCES tests/unit/test_ai.cpp
    TEST_PREFIX "unit/ai/"
    TEST_AI_AWARE
)

tash_register_plugin(
    NAME ai_error_hook
    SOURCES src/plugins/ai_error_hook_provider.cpp
    REQUIRES TASH_AI_ENABLED
    TEST_SOURCES tests/unit/test_ai_error_hook.cpp
    TEST_PREFIX "unit/plugins/ai/"
    TEST_AI_AWARE
)

tash_register_plugin(
    NAME key_file_perms
    REQUIRES TASH_AI_ENABLED
    TEST_SOURCES tests/unit/test_key_file_perms.cpp
    TEST_PREFIX "unit/ai/"
    TEST_AI_AWARE
)

tash_register_plugin(
    NAME config_dir_migration
    REQUIRES TASH_AI_ENABLED
    TEST_SOURCES tests/unit/test_config_dir_migration.cpp
    TEST_PREFIX "unit/ai/"
    TEST_AI_AWARE
)

tash_register_plugin(
    NAME sqlite_history
    SOURCES src/plugins/sqlite_history_provider.cpp
    REQUIRES TASH_SQLITE_ENABLED
    TEST_SOURCES tests/unit/test_sqlite_history.cpp src/plugins/sqlite_history_provider.cpp src/util/config_resolver.cpp
    TEST_INCLUDES ${SQLite3_INCLUDE_DIRS}
    TEST_LIBS ${SQLite3_LIBRARIES}
    TEST_PREFIX "unit/sqlite/"
    TEST_STANDALONE
)

tash_register_plugin(
    NAME contextual_ai
    SOURCES src/ai/contextual_ai.cpp
    REQUIRES TASH_AI_ENABLED
    TEST_SOURCES tests/unit/test_contextual_ai.cpp
    TEST_PREFIX "unit/ai/"
    TEST_AI_AWARE
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
    REQUIRES TASH_AI_ENABLED
    TEST_SOURCES tests/unit/test_pipeline.cpp
    TEST_PREFIX "unit/core/"
    TEST_AI_AWARE
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
