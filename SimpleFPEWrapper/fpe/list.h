// SimpleFPEWrapper - SimpleFPEWrapper/fpe/list.h
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#pragma once

#include <GL/gl.h>
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

    GLCmd(const GLCmd&) = delete;
    GLCmd& operator=(const GLCmd&) = delete;
};
using DisplayList = std::vector<std::unique_ptr<GLCmd>>;

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

    inline static unordered_map<GLuint, DisplayList> lists;
    inline static GLuint currentListID = 0;

    template <auto Func, typename... ProcessedArgs>
    void recordImpl(std::vector<std::vector<uint8_t>>&& buffers, ProcessedArgs&&... args) {
        lists[currentListID].emplace_back(std::make_unique<GLFuncCmd<Func, ProcessedArgs...>>(
            std::forward<ProcessedArgs>(args)..., std::move(buffers)));
    }

public:
    static GLuint genDisplayList(GLsizei range) {
        GLuint first = nextListId;
        nextListId += range;
        for (GLuint i = first; i < first + range; ++i) {
            lists[i] = std::vector<std::unique_ptr<GLCmd>>{};
        }
        return first;
    }

    static void deleteDisplayList(GLuint list, GLsizei range) {
        for (GLuint i = 0; i < range; ++i) {
            lists.erase(list + i);
        }
    }

    static GLboolean isDisplayList(GLuint list) { return lists.find(list) != lists.end() ? GL_TRUE : GL_FALSE; }

    static void startRecord(GLuint listID, GLenum mode) {
        currentListID = listID;
        listMode = mode;
        lists.try_emplace(listID).first->second.clear();
    }

    static void endRecord() {
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
        if (command != nullptr) lists[currentListID].emplace_back(std::move(command));
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
};

inline DisplayListManager displayListManager;

inline GLboolean disableRecording = GL_FALSE;

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
