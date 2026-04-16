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
    TEST_AI_AWARE
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
    NAME sqlite_history
    SOURCES src/plugins/sqlite_history_provider.cpp
    REQUIRES TASH_SQLITE_ENABLED
    TEST_SOURCES tests/unit/test_sqlite_history.cpp src/plugins/sqlite_history_provider.cpp
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
