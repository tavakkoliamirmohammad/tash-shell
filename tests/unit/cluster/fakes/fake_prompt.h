// FakePrompt — scripted answers for IPrompt::choice in engine tests.

#ifndef TASH_CLUSTER_FAKE_PROMPT_H
#define TASH_CLUSTER_FAKE_PROMPT_H

#include "tash/cluster/cluster_engine.h"

#include <deque>
#include <string>
#include <vector>

namespace tash::cluster::testing {

class FakePrompt : public IPrompt {
public:
    struct Call { std::string message; std::string choices; };

    std::vector<Call> calls;
    std::deque<char>  scripted_answers;     // FIFO of answers

    void queue_answer(char c) { scripted_answers.push_back(c); }

    char choice(const std::string& message, const std::string& choices) override {
        calls.push_back({message, choices});
        if (scripted_answers.empty()) return '\0';
        char c = scripted_answers.front();
        scripted_answers.pop_front();
        return c;
    }

    void reset() {
        calls.clear();
        scripted_answers.clear();
    }
};

}  // namespace tash::cluster::testing

#endif  // TASH_CLUSTER_FAKE_PROMPT_H
