// Reverie/Studio/App/EditorCommand.h - the editor's undo/redo command stack.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// A real transaction stack (not UI-state snapshots): every mutation of the authoring model goes
// through Execute() with a redo + undo closure, so File>Undo/Redo, Ctrl+Z/Y and menu items all
// share one path and no business logic lives in an ImGui callback. Drags are coalesced by the
// caller (push one command on release, not per frame).
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace reverie::studio {

class CommandStack {
public:
    void Execute(std::string name, std::function<void()> redo, std::function<void()> undo) {
        if (redo) redo();
        undo_.push_back({std::move(name), std::move(redo), std::move(undo)});
        redo_.clear();
        if (undo_.size() > kMax) undo_.erase(undo_.begin());
    }

    bool CanUndo() const { return !undo_.empty(); }
    bool CanRedo() const { return !redo_.empty(); }
    const char* UndoName() const { return undo_.empty() ? "" : undo_.back().name.c_str(); }
    const char* RedoName() const { return redo_.empty() ? "" : redo_.back().name.c_str(); }

    void Undo() {
        if (undo_.empty()) return;
        Command c = std::move(undo_.back());
        undo_.pop_back();
        if (c.undo) c.undo();
        redo_.push_back(std::move(c));
    }
    void Redo() {
        if (redo_.empty()) return;
        Command c = std::move(redo_.back());
        redo_.pop_back();
        if (c.redo) c.redo();
        undo_.push_back(std::move(c));
    }
    void Clear() {
        undo_.clear();
        redo_.clear();
    }

private:
    struct Command {
        std::string name;
        std::function<void()> redo;
        std::function<void()> undo;
    };
    static constexpr std::size_t kMax = 256;
    std::vector<Command> undo_;
    std::vector<Command> redo_;
};

} // namespace reverie::studio
