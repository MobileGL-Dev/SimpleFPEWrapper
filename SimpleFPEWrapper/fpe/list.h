// SimpleFPEWrapper - SimpleFPEWrapper/fpe/list.h
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#pragma once

#include <GL/gl.h>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_map>

#include <vector>
#include <memory>
#include <tuple>
#include <cstring>
#include <utility>
#include <type_traits>

#define DEBUG 0

// Todo: record more functions

template <typename K, typename V>
using unordered_map = std::unordered_map<K, V>;

class GLCmd {
public:
    virtual ~GLCmd() = default;
    GLCmd() = default;
    GLCmd(GLCmd&&) = default;
    GLCmd& operator=(GLCmd&&) = default;
    virtual void execute() const = 0;
    virtual bool tryMerge(const GLCmd&) { return false; }
    virtual bool isCapturedDraw() const { return false; }
    virtual bool bakePositionTranslation(const glm::vec3&) { return false; }
    virtual const GLCmd* capturedDrawForBatch(glm::mat4*) const { return nullptr; }

    GLCmd(const GLCmd&) = delete;
    GLCmd& operator=(const GLCmd&) = delete;
};
using DisplayList = std::vector<std::unique_ptr<GLCmd>>;

void optimizeDisplayListCommands(DisplayList& commands);

template <auto FuncPtr, typename... Args>
class GLFuncCmd : public GLCmd {
    using StoredArgs = std::tuple<std::decay_t<Args>...>;
    StoredArgs args;
    std::vector<std::vector<uint8_t>> argBuffers;

public:
    explicit GLFuncCmd(Args&&... processedArgs, std::vector<std::vector<uint8_t>>&& buffers)
        : args(std::forward<Args>(processedArgs)...), argBuffers(std::move(buffers)) {}

    void execute() const override {
        std::apply([](auto&&... args) { FuncPtr(std::forward<decltype(args)>(args)...); }, args);
    }
};

class DisplayListManager {
    inline static GLuint nextListId = 1;
    inline static GLenum listMode = GL_COMPILE;
    inline static GLuint callingDepth = 0;
    inline static uint64_t mutationGeneration = 1;

    inline static unordered_map<GLuint, DisplayList> lists;
    inline static GLuint currentListID = 0;

    template <auto Func, typename... ProcessedArgs>
    void recordImpl(std::vector<std::vector<uint8_t>>&& buffers, ProcessedArgs&&... args) {
        bumpMutationGeneration();
        lists[currentListID].emplace_back(std::make_unique<GLFuncCmd<Func, ProcessedArgs...>>(
            std::forward<ProcessedArgs>(args)..., std::move(buffers)));
    }

    static void bumpMutationGeneration() {
        ++mutationGeneration;
        // Zero is reserved for an uninitialised consumer cache. Unsigned
        // wraparound is defined, so skip it if this process lives long enough
        // to rebuild 2^64 display-list commands.
        if (mutationGeneration == 0) ++mutationGeneration;
    }

public:
    static GLuint genDisplayList(GLsizei range) {
        // glGenLists: zero or negative ranges allocate nothing and return 0.
        if (range <= 0) return 0;
        GLuint first = nextListId;
        nextListId += range;
        for (GLsizei i = 0; i < range; ++i) {
            lists[first + i] = std::vector<std::unique_ptr<GLCmd>>{};
        }
        bumpMutationGeneration();
        return first;
    }

    static void deleteDisplayList(GLuint list, GLsizei range) {
        // A negative range previously wrapped to ~2^32 iterations here.
        if (range <= 0) return;
        for (GLsizei i = 0; i < range; ++i) {
            lists.erase(list + i);
        }
        bumpMutationGeneration();
    }

    static GLboolean isDisplayList(GLuint list) { return lists.find(list) != lists.end() ? GL_TRUE : GL_FALSE; }

    static void startRecord(GLuint listID, GLenum mode) {
        bumpMutationGeneration();
        currentListID = listID;
        listMode = mode;
        lists.try_emplace(listID).first->second.clear();
    }

    static void endRecord() {
        auto it = lists.find(currentListID);
        if (it != lists.end()) optimizeDisplayListCommands(it->second);
        bumpMutationGeneration();
        currentListID = 0;
        listMode = GL_COMPILE;
    }

    static int isRecording() { return currentListID != 0 ? 1 : 0; }

    static int isCalling() { return callingDepth != 0; }

    static int shouldRecord() { return callingDepth == 0 && currentListID != 0; }

    static int shouldFinish() { return (currentListID != 0 && listMode == GL_COMPILE) ? 1 : 0; }

    template <auto Func, typename... Args>
    void record(const std::vector<std::pair<size_t, size_t>>& pointerArgs, Args&&... args) {
        std::vector<std::vector<uint8_t>> argBuffers;
        auto argsTuple = std::make_tuple(std::forward<Args>(args)...);

        [&]<size_t... Is>(std::index_sequence<Is...>) {
            (([&] {
                 for (const auto& [index, size] : pointerArgs) {
                     if (index == Is) {
                         auto& arg = std::get<Is>(argsTuple);
                         using ArgType = std::decay_t<decltype(arg)>;

                         if constexpr (std::is_pointer_v<ArgType>) {
                             if (arg != nullptr) {
                                 std::vector<uint8_t> buffer(size);
                                 std::memcpy(buffer.data(), arg, size);
                                 argBuffers.emplace_back(std::move(buffer));
                                 arg = reinterpret_cast<ArgType>(argBuffers.back().data());
                             }
                         }
                         break;
                     }
                 }
             })(),
             ...);
        }(std::index_sequence_for<Args...>{});

        std::apply(
            [&](auto&&... processedArgs) {
                this->template recordImpl<Func>(std::move(argBuffers),
                                                std::forward<decltype(processedArgs)>(processedArgs)...);
            },
            argsTuple);
    }

    void recordCommand(std::unique_ptr<GLCmd> command) {
        if (command == nullptr) return;
        bumpMutationGeneration();
        auto& commands = lists[currentListID];
        // A command may fold an immediately adjacent command only when it can
        // prove that doing so preserves the display-list state boundary.
        if (!commands.empty() && commands.back()->tryMerge(*command)) return;
        commands.emplace_back(std::move(command));
    }

    static void callList(GLuint listID) {
        auto it = lists.find(listID);
        if (it == lists.end()) return;

        ++callingDepth;
        for (auto& cmd : it->second) {
            cmd->execute();
        }
        --callingDepth;
    }

    static bool callSingleCaptured(GLuint listID) {
        if (callingDepth != 0) return false;

        struct cache_entry_t {
            uint64_t generation = 0;
            GLuint listID = 0;
            const GLCmd* command = nullptr;
        };
        constexpr size_t kCacheSize = 256;
        thread_local std::array<cache_entry_t, kCacheSize> cache;

        const uint64_t currentGeneration = mutationGeneration;
        auto& entry = cache[(static_cast<size_t>(listID) * 2654435761u) & (kCacheSize - 1u)];
        const GLCmd* command = nullptr;
        if (entry.generation == currentGeneration && entry.listID == listID) {
            command = entry.command;
        } else {
            const auto it = lists.find(listID);
            if (it == lists.end() || it->second.size() != 1) return false;
            command = it->second.front().get();
            if (command == nullptr || !command->isCapturedDraw()) return false;
            entry = {currentGeneration, listID, command};
        }

        ++callingDepth;
        command->execute();
        --callingDepth;
        return true;
    }

    static const DisplayList* findList(GLuint listID) {
        const auto it = lists.find(listID);
        return it == lists.end() ? nullptr : &it->second;
    }

    static uint64_t generation() { return mutationGeneration; }
};

inline DisplayListManager displayListManager;

inline GLboolean disableRecording = GL_FALSE;

bool tryExecuteCapturedDisplayLists(const GLuint* listIds, size_t listCount);

#define SELF_CALL(func, ...)                                                                                           \
    {                                                                                                                  \
        GLboolean alreadyDisabled = disableRecording;                                                                  \
        disableRecording = GL_TRUE;                                                                                    \
        func(__VA_ARGS__);                                                                                             \
        disableRecording = alreadyDisabled;                                                                            \
    }

#define LIST_RECORD(func, pointers, ...)                                                                               \
    if (!disableRecording && DisplayListManager::shouldRecord()) {                                                     \
        displayListManager.record<func>(pointers, ##__VA_ARGS__);                                                      \
        if (DisplayListManager::shouldFinish()) return;                                                                \
    }

GLAPI GLAPIENTRY GLuint glGenLists(GLsizei range);
GLAPI GLAPIENTRY void glDeleteLists(GLuint list, GLsizei range);
GLAPI GLAPIENTRY GLboolean glIsList(GLuint list);
GLAPI GLAPIENTRY void glNewList(GLuint list, GLenum mode);
GLAPI GLAPIENTRY void glEndList();
GLAPI GLAPIENTRY void glCallList(GLuint list);
GLAPI GLAPIENTRY void glCallLists(GLsizei n, GLenum type, const GLvoid* lists);
GLAPI GLAPIENTRY void glListBase(GLuint base);
