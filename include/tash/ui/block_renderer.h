#ifndef TASH_UI_BLOCK_RENDERER_H
#define TASH_UI_BLOCK_RENDERER_H

#include <string>
#include <vector>
#include <cstdint>

// ── Block data structure ──────────────────────────────────────

struct Block {
    std::string command;
    std::string output;
    int exit_code;
    double duration_seconds;
    int64_t timestamp;
    bool folded;

    Block()
        : exit_code(0)
        , duration_seconds(0.0)
        , timestamp(0)
        , folded(false) {}
};

// ── Block formatting functions ────────────────────────────────

std::string render_block_header(const Block &block);
std::string render_block_separator();
std::string format_duration(double seconds);
int get_terminal_width();

// ── Block manager ─────────────────────────────────────────────

class BlockManager {
public:
    void start_block(const std::string &command);
    void end_block(const std::string &output, int exit_code, double duration);
    const std::vector<Block> &blocks() const;
    void fold(size_t index);
    void unfold(size_t index);
    void fold_all();
    void unfold_all();
    std::string render_block(size_t index) const;
    size_t block_count() const;

private:
    std::vector<Block> blocks_;
};

#endif // TASH_UI_BLOCK_RENDERER_H
