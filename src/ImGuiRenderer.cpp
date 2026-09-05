#include "ImGuiRenderer.h"

#include "VkContext.h"

#include <TypedBuffer.h>

#include <glm/vec2.hpp>

#include <imgui/imgui.h>
#include <volk.h>

#include <cstddef>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>

#include <imgui/fa-solid-900.h>


namespace nebula {

static ImGuiMouseButton buttonToImgui(int button) {
    switch(button) {
        case GLFW_MOUSE_BUTTON_LEFT: return ImGuiMouseButton_Left;
        case GLFW_MOUSE_BUTTON_RIGHT: return ImGuiMouseButton_Right;
        case GLFW_MOUSE_BUTTON_MIDDLE: return ImGuiMouseButton_Middle;
        default:
            return ImGuiMouseButton_COUNT;
    }
}

// https://github.com/ocornut/imgui/blob/master/backends/imguiImplGlfw.cpp#L137
static ImGuiKey keyToImgui(int key) {
    switch(key) {
        case GLFW_KEY_TAB: return ImGuiKey_Tab;
        case GLFW_KEY_LEFT: return ImGuiKey_LeftArrow;
        case GLFW_KEY_RIGHT: return ImGuiKey_RightArrow;
        case GLFW_KEY_UP: return ImGuiKey_UpArrow;
        case GLFW_KEY_DOWN: return ImGuiKey_DownArrow;
        case GLFW_KEY_PAGE_UP: return ImGuiKey_PageUp;
        case GLFW_KEY_PAGE_DOWN: return ImGuiKey_PageDown;
        case GLFW_KEY_HOME: return ImGuiKey_Home;
        case GLFW_KEY_END: return ImGuiKey_End;
        case GLFW_KEY_INSERT: return ImGuiKey_Insert;
        case GLFW_KEY_DELETE: return ImGuiKey_Delete;
        case GLFW_KEY_BACKSPACE: return ImGuiKey_Backspace;
        case GLFW_KEY_SPACE: return ImGuiKey_Space;
        case GLFW_KEY_ENTER: return ImGuiKey_Enter;
        case GLFW_KEY_ESCAPE: return ImGuiKey_Escape;
        case GLFW_KEY_APOSTROPHE: return ImGuiKey_Apostrophe;
        case GLFW_KEY_COMMA: return ImGuiKey_Comma;
        case GLFW_KEY_MINUS: return ImGuiKey_Minus;
        case GLFW_KEY_PERIOD: return ImGuiKey_Period;
        case GLFW_KEY_SLASH: return ImGuiKey_Slash;
        case GLFW_KEY_SEMICOLON: return ImGuiKey_Semicolon;
        case GLFW_KEY_EQUAL: return ImGuiKey_Equal;
        case GLFW_KEY_LEFT_BRACKET: return ImGuiKey_LeftBracket;
        case GLFW_KEY_BACKSLASH: return ImGuiKey_Backslash;
        case GLFW_KEY_RIGHT_BRACKET: return ImGuiKey_RightBracket;
        case GLFW_KEY_GRAVE_ACCENT: return ImGuiKey_GraveAccent;
        case GLFW_KEY_CAPS_LOCK: return ImGuiKey_CapsLock;
        case GLFW_KEY_SCROLL_LOCK: return ImGuiKey_ScrollLock;
        case GLFW_KEY_NUM_LOCK: return ImGuiKey_NumLock;
        case GLFW_KEY_PRINT_SCREEN: return ImGuiKey_PrintScreen;
        case GLFW_KEY_PAUSE: return ImGuiKey_Pause;
        case GLFW_KEY_KP_0: return ImGuiKey_Keypad0;
        case GLFW_KEY_KP_1: return ImGuiKey_Keypad1;
        case GLFW_KEY_KP_2: return ImGuiKey_Keypad2;
        case GLFW_KEY_KP_3: return ImGuiKey_Keypad3;
        case GLFW_KEY_KP_4: return ImGuiKey_Keypad4;
        case GLFW_KEY_KP_5: return ImGuiKey_Keypad5;
        case GLFW_KEY_KP_6: return ImGuiKey_Keypad6;
        case GLFW_KEY_KP_7: return ImGuiKey_Keypad7;
        case GLFW_KEY_KP_8: return ImGuiKey_Keypad8;
        case GLFW_KEY_KP_9: return ImGuiKey_Keypad9;
        case GLFW_KEY_KP_DECIMAL: return ImGuiKey_KeypadDecimal;
        case GLFW_KEY_KP_DIVIDE: return ImGuiKey_KeypadDivide;
        case GLFW_KEY_KP_MULTIPLY: return ImGuiKey_KeypadMultiply;
        case GLFW_KEY_KP_SUBTRACT: return ImGuiKey_KeypadSubtract;
        case GLFW_KEY_KP_ADD: return ImGuiKey_KeypadAdd;
        case GLFW_KEY_KP_ENTER: return ImGuiKey_KeypadEnter;
        case GLFW_KEY_KP_EQUAL: return ImGuiKey_KeypadEqual;
        case GLFW_KEY_LEFT_SHIFT: return ImGuiKey_LeftShift;
        case GLFW_KEY_LEFT_CONTROL: return ImGuiKey_LeftCtrl;
        case GLFW_KEY_LEFT_ALT: return ImGuiKey_LeftAlt;
        case GLFW_KEY_LEFT_SUPER: return ImGuiKey_LeftSuper;
        case GLFW_KEY_RIGHT_SHIFT: return ImGuiKey_RightShift;
        case GLFW_KEY_RIGHT_CONTROL: return ImGuiKey_RightCtrl;
        case GLFW_KEY_RIGHT_ALT: return ImGuiKey_RightAlt;
        case GLFW_KEY_RIGHT_SUPER: return ImGuiKey_RightSuper;
        case GLFW_KEY_MENU: return ImGuiKey_Menu;
        case GLFW_KEY_0: return ImGuiKey_0;
        case GLFW_KEY_1: return ImGuiKey_1;
        case GLFW_KEY_2: return ImGuiKey_2;
        case GLFW_KEY_3: return ImGuiKey_3;
        case GLFW_KEY_4: return ImGuiKey_4;
        case GLFW_KEY_5: return ImGuiKey_5;
        case GLFW_KEY_6: return ImGuiKey_6;
        case GLFW_KEY_7: return ImGuiKey_7;
        case GLFW_KEY_8: return ImGuiKey_8;
        case GLFW_KEY_9: return ImGuiKey_9;
        case GLFW_KEY_A: return ImGuiKey_A;
        case GLFW_KEY_B: return ImGuiKey_B;
        case GLFW_KEY_C: return ImGuiKey_C;
        case GLFW_KEY_D: return ImGuiKey_D;
        case GLFW_KEY_E: return ImGuiKey_E;
        case GLFW_KEY_F: return ImGuiKey_F;
        case GLFW_KEY_G: return ImGuiKey_G;
        case GLFW_KEY_H: return ImGuiKey_H;
        case GLFW_KEY_I: return ImGuiKey_I;
        case GLFW_KEY_J: return ImGuiKey_J;
        case GLFW_KEY_K: return ImGuiKey_K;
        case GLFW_KEY_L: return ImGuiKey_L;
        case GLFW_KEY_M: return ImGuiKey_M;
        case GLFW_KEY_N: return ImGuiKey_N;
        case GLFW_KEY_O: return ImGuiKey_O;
        case GLFW_KEY_P: return ImGuiKey_P;
        case GLFW_KEY_Q: return ImGuiKey_Q;
        case GLFW_KEY_R: return ImGuiKey_R;
        case GLFW_KEY_S: return ImGuiKey_S;
        case GLFW_KEY_T: return ImGuiKey_T;
        case GLFW_KEY_U: return ImGuiKey_U;
        case GLFW_KEY_V: return ImGuiKey_V;
        case GLFW_KEY_W: return ImGuiKey_W;
        case GLFW_KEY_X: return ImGuiKey_X;
        case GLFW_KEY_Y: return ImGuiKey_Y;
        case GLFW_KEY_Z: return ImGuiKey_Z;
        case GLFW_KEY_F1: return ImGuiKey_F1;
        case GLFW_KEY_F2: return ImGuiKey_F2;
        case GLFW_KEY_F3: return ImGuiKey_F3;
        case GLFW_KEY_F4: return ImGuiKey_F4;
        case GLFW_KEY_F5: return ImGuiKey_F5;
        case GLFW_KEY_F6: return ImGuiKey_F6;
        case GLFW_KEY_F7: return ImGuiKey_F7;
        case GLFW_KEY_F8: return ImGuiKey_F8;
        case GLFW_KEY_F9: return ImGuiKey_F9;
        case GLFW_KEY_F10: return ImGuiKey_F10;
        case GLFW_KEY_F11: return ImGuiKey_F11;
        case GLFW_KEY_F12: return ImGuiKey_F12;
        default:
            return ImGuiKey_None;
    }
}

static std::unique_ptr<Texture> createFont() {
    ImFontAtlas* fonts = ImGui::GetIO().Fonts;
    fonts->AddFontDefault();

    ImFontConfig config;
    {
        config.MergeMode = true;
        config.PixelSnapH = true;
        config.OversampleV = 2;
        config.OversampleH = 2;
        config.FontDataOwnedByAtlas = false;
    }
    const ImWchar iconRanges[] = {ICON_MIN_FA, ICON_MAX_FA, 0};
    fonts->AddFontFromMemoryCompressedTTF(font_awesome_compressed_data, font_awesome_compressed_size, 13.0f, &config, iconRanges);

    u8* fontData = nullptr;
    int width = 0;
    int height = 0;
    fonts->GetTexDataAsRGBA32(&fontData, &width, &height);

    const size_t bytes = 4 * width * height;

    TextureData data;
    data.format = ImageFormat::RGBA8_UNORM;
    data.size = glm::uvec2(width, height);
    data.data = std::make_unique<u8[]>(bytes);
    std::copy_n(fontData, bytes, data.data.get());

    return std::make_unique<Texture>(data);
}


static void charCallback(GLFWwindow*, unsigned characted) {
    ImGui::GetIO().AddInputCharacter(characted);
}


static void keyCallback(GLFWwindow*, int key, int, int action, int mods) {
    auto& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiKey_ModCtrl, (mods & GLFW_MOD_CONTROL) != 0);
    io.AddKeyEvent(ImGuiKey_ModShift, (mods & GLFW_MOD_SHIFT) != 0);
    io.AddKeyEvent(ImGuiKey_ModAlt, (mods & GLFW_MOD_ALT) != 0);
    io.AddKeyEvent(ImGuiKey_ModSuper, (mods & GLFW_MOD_SUPER) != 0);
    io.AddKeyEvent(keyToImgui(key), action == GLFW_PRESS);

}

static void mousePosCallback(GLFWwindow*, double xpos, double ypos) {
    ImGui::GetIO().AddMousePosEvent(float(xpos), float(ypos));
}

static void mouseButtonCallback(GLFWwindow*, int button, int action, int) {
    ImGui::GetIO().AddMouseButtonEvent(buttonToImgui(button), action == GLFW_PRESS);
}

ImGuiRenderer::ImGuiRenderer(GLFWwindow* window) : _window(window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    _material.setProgram(Program::fromFiles("imgui.slang", "imgui.slang"));
    _material.setDepthTestMode(DepthTestMode::None);
    _material.setBlendMode(BlendMode::Alpha);
    _material.setDoubleSided(true);

    _font = createFont();

    glfwSetKeyCallback(_window, keyCallback);
    glfwSetCharCallback(_window, charCallback);
    glfwSetCursorPosCallback(_window, mousePosCallback);
    glfwSetMouseButtonCallback(_window, mouseButtonCallback);
}

void ImGuiRenderer::start() {
    auto& io = ImGui::GetIO();

    int w, h;
    glfwGetWindowSize(_window, &w, &h);
    io.DisplaySize = ImVec2(float(w), float(h));
    io.DeltaTime = updateDeltaTime();
    io.Fonts->TexID = _font.get();

    ImGui::NewFrame();
}

void ImGuiRenderer::finish() {
    ImGui::Render();
    render(ImGui::GetDrawData());
}


float ImGuiRenderer::updateDeltaTime() {
    const auto now = std::chrono::high_resolution_clock::now();
    const float dt = std::chrono::duration_cast<std::chrono::duration<float>>(now - _last).count();
    _last = now;
    return dt;
}

void ImGuiRenderer::render(const ImDrawData* drawData) {
    static_assert(sizeof(ImDrawVert) == 20);
    static_assert(offsetof(ImDrawVert, pos) == 0);
    static_assert(offsetof(ImDrawVert, uv) == 8);
    static_assert(offsetof(ImDrawVert, col) == 16);

    if(!drawData->TotalIdxCount || !drawData->TotalVtxCount) {
        return;
    }

    const float width = (drawData->DisplaySize.x * drawData->FramebufferScale.x);
    const float height = (drawData->DisplaySize.y * drawData->FramebufferScale.y);

    if(width <= 0.0f || height <= 0.0f) {
        return;
    }

    if(!ctx().frameActive) {
        return;
    }

    const ImVec2 clipOff = drawData->DisplayPos;
    const ImVec2 clipScale = drawData->FramebufferScale;

    PushConstants push = _material.buildPushConstants();
    push.set(HASH("viewportSize"), glm::vec2(drawData->DisplaySize.x, drawData->DisplaySize.y));
    const RasterState raster = _material.rasterState();

    TypedBuffer<ImDrawIdx> indexBuffer(nullptr, drawData->TotalIdxCount);
    TypedBuffer<ImDrawVert> vertexBuffer(nullptr, drawData->TotalVtxCount);

    {
        auto indices = indexBuffer.map(AccessType::WriteOnly);
        auto vertices = vertexBuffer.map(AccessType::WriteOnly);

        size_t indexOffset = 0;
        size_t vertexOffset = 0;
        for(int c = 0; c != drawData->CmdListsCount; ++c) {
            const ImDrawList* cmdList = drawData->CmdLists[c];
            std::copy_n(cmdList->IdxBuffer.Data, cmdList->IdxBuffer.Size, &indices[indexOffset]);
            std::copy_n(cmdList->VtxBuffer.Data, cmdList->VtxBuffer.Size, &vertices[vertexOffset]);
            vertexOffset += cmdList->VtxBuffer.Size;
            indexOffset += cmdList->IdxBuffer.Size;
        }
    }

    const VkBuffer vbo = vertexBuffer.vkBuffer();
    const VkBuffer ibo = indexBuffer.vkBuffer();
    const VkIndexType indexType = sizeof(ImDrawIdx) == 2 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
    const VkCommandBuffer cmdBuf = vkCommandBuffer();

    u32 idxBase = 0;
    i32 vtxBase = 0;
    for(int c = 0; c != drawData->CmdListsCount; ++c) {
        const ImDrawList* cmdList = drawData->CmdLists[c];

        for(int i = 0; i != cmdList->CmdBuffer.Size; ++i) {
            const ImDrawCmd& cmd = cmdList->CmdBuffer[i];

            ALWAYS_ASSERT(!cmd.UserCallback, "User callback not supported");

            const ImVec2 clipMin((cmd.ClipRect.x - clipOff.x) * clipScale.x, (cmd.ClipRect.y - clipOff.y) * clipScale.y);
            const ImVec2 clipMax((cmd.ClipRect.z - clipOff.x) * clipScale.x, (cmd.ClipRect.w - clipOff.y) * clipScale.y);
            if(!cmd.ElemCount || clipMax.x <= clipMin.x || clipMax.y <= clipMin.y) {
                continue;
            }

            // Vulkan scissors are top-left origin.
            // The Y-flipped viewport does not affect scissor coordinates.
            const i32 minX = std::max(i32(clipMin.x), 0);
            const i32 minY = std::max(i32(clipMin.y), 0);
            const i32 maxX = std::min(i32(clipMax.x), i32(width));
            const i32 maxY = std::min(i32(clipMax.y), i32(height));
            if(maxX <= minX || maxY <= minY) {
                continue;
            }

            const VkRect2D scissor{
                .offset = {minX, minY},
                .extent = {u32(maxX - minX), u32(maxY - minY)},
            };
            vkCmdSetScissor(cmdBuf, 0, 1, &scissor);

            PassResources pass{};
            if(Texture* tex = static_cast<Texture*>(cmd.TextureId)) {
                pass.textures[0] = tex;
            }

            drawIndexed(
                _material.program(),
                VertexLayout::ImGui,
                raster,
                pass,
                push,
                vbo,
                ibo,
                cmd.ElemCount,
                idxBase + cmd.IdxOffset,
                vtxBase + i32(cmd.VtxOffset),
                indexType
            );
        }

        idxBase += u32(cmdList->IdxBuffer.Size);
        vtxBase += i32(cmdList->VtxBuffer.Size);
    }
}

}
