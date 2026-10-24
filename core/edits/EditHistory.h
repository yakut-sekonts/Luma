#pragma once
#include "core/edits/EditDocument.h"
#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace luma {
// Source labels are translated by the UI; subject is user-authored mask text.
struct EditHistoryChange {
    std::string label;
    std::optional<double> value;
    std::string subject;
};
std::vector<EditHistoryChange> describeEditChange(const EditDocument &before,
                                                  const EditDocument &after);

struct EditHistoryItem {
    std::uint64_t id = 0;
    std::string action;
    std::vector<EditHistoryChange> changes;
    bool current = false;
    bool redo = false;
};

class EditHistory {
  public:
    explicit EditHistory(EditDocument initial = {}, std::string action = "Opened photo")
        : current_{std::move(initial), 0, std::move(action), {}} {
        current_.document.validate();
    }
    const EditDocument &current() const {
        return current_.document;
    }
    // Pure observations: never finish or mutate an active gesture.
    bool gestureActive() const noexcept {
        return gesture_.has_value();
    }
    bool canUndo() const {
        return !undo_.empty() || (gesture_ && gesture_->document != current_.document);
    }
    bool canRedo() const {
        return !redo_.empty() && !gesture_;
    }
    std::uint64_t revision() const noexcept {
        return revision_;
    }
    void begin() {
        if (!gesture_)
            gesture_ = current_;
    }
    void end() {
        if (!gesture_)
            return;
        if (gesture_->document != current_.document) {
            auto changes = describeEditChange(gesture_->document, current_.document);
            push(undo_, std::move(*gesture_));
            current_.id = nextID_++;
            current_.action.clear();
            current_.changes = std::move(changes);
            redo_.clear();
            ++revision_;
        }
        gesture_.reset();
    }
    void apply(EditDocument next, std::string action = {}) {
        next.validate();
        if (next == current_.document)
            return;
        if (gesture_) {
            current_.document = std::move(next);
            return;
        }
        auto changes = describeEditChange(current_.document, next);
        push(undo_, std::move(current_));
        current_ = {std::move(next), nextID_++, std::move(action), std::move(changes)};
        redo_.clear();
        ++revision_;
    }
    void undo() {
        end();
        if (undo_.empty())
            return;
        push(redo_, std::move(current_));
        current_ = std::move(undo_.back());
        undo_.pop_back();
        ++revision_;
    }
    void redo() {
        end();
        if (redo_.empty())
            return;
        push(undo_, std::move(current_));
        current_ = std::move(redo_.back());
        redo_.pop_back();
        ++revision_;
    }
    // Chronological metadata only: no document copies or diffing on UI reads.
    // During a gesture show its committed base, not a provisional new state.
    std::vector<EditHistoryItem> items() const {
        std::vector<EditHistoryItem> result;
        result.reserve(undo_.size() + redo_.size() + 1);
        const auto append = [&](const State &state, bool current, bool redo) {
            result.push_back({state.id, state.action, state.changes, current, redo});
        };
        for (const auto &state : undo_)
            append(state, false, false);
        append(gesture_ ? *gesture_ : current_, true, false);
        for (auto it = redo_.rbegin(); it != redo_.rend(); ++it)
            append(*it, false, true);
        return result;
    }
    // Callers must finish the gesture before resolving a stable state handle.
    const EditDocument *document(std::uint64_t id) const {
        if (gesture_)
            return nullptr;
        if (current_.id == id)
            return &current_.document;
        for (const auto &state : undo_)
            if (state.id == id)
                return &state.document;
        for (const auto &state : redo_)
            if (state.id == id)
                return &state.document;
        return nullptr;
    }
    bool seek(std::uint64_t id) {
        end();
        if (!document(id))
            return false;
        if (std::any_of(undo_.begin(), undo_.end(),
                        [id](const auto &state) { return state.id == id; })) {
            while (current_.id != id)
                undo();
        } else {
            while (current_.id != id)
                redo();
        }
        return true;
    }
    void clear() {
        end();
        undo_.clear();
        redo_.clear();
        current_.id = nextID_++;
        current_.action = "History cleared";
        current_.changes.clear();
        ++revision_;
    }

  private:
    struct State {
        EditDocument document;
        std::uint64_t id;
        std::string action;
        std::vector<EditHistoryChange> changes;
    };
    State current_;
    std::vector<State> undo_, redo_;
    std::optional<State> gesture_;
    std::uint64_t nextID_ = 1, revision_ = 0;
    static void push(std::vector<State> &stack, State value) {
        if (stack.size() == 200)
            stack.erase(stack.begin());
        stack.push_back(std::move(value));
    }
};
} // namespace luma
