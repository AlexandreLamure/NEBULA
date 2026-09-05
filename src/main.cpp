#include <volk.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <graphics.h>
#include <VkContext.h>
#include <Program.h>
#include <Scene.h>
#include <Texture.h>
#include <RenderPass.h>
#include <TimestampQuery.h>
#include <ImGuiRenderer.h>

#include <imgui/imgui.h>

#include <iostream>
#include <vector>
#include <filesystem>

using namespace nebula;


static float deltaTime = 0.0f;
static float sunAltitude = 45.0f;
static float sunAzimuth = 45.0f;
static float sunIntensity = 7.0f;
static float iblIntensity = 1.0f;
static float exposure = 0.33f;

static std::unique_ptr<Scene> scene;
static std::shared_ptr<Texture> envmap;

void glfwCheck(bool cond) {
    if(!cond) {
        const char* err = nullptr;
        glfwGetError(&err);
        std::cerr << "GLFW error: " << err << std::endl;
        std::exit(EXIT_FAILURE);
    }
}

void updateDeltaTime() {
    static double time = 0.0;
    const double newTime = programTime();
    deltaTime = float(newTime - time);
    time = newTime;
}

void processInputs(GLFWwindow* window, Camera& camera) {
    static glm::dvec2 mousePos;

    glm::dvec2 newMousePos;
    glfwGetCursorPos(window, &newMousePos.x, &newMousePos.y);

    {
        glm::vec3 movement = {};
        if(glfwGetKey(window, 'W') == GLFW_PRESS) {
            movement += camera.forward();
        }
        if(glfwGetKey(window, 'S') == GLFW_PRESS) {
            movement -= camera.forward();
        }
        if(glfwGetKey(window, 'D') == GLFW_PRESS) {
            movement += camera.right();
        }
        if(glfwGetKey(window, 'A') == GLFW_PRESS) {
            movement -= camera.right();
        }
        if(glfwGetKey(window, 'E') == GLFW_PRESS) {
            movement += camera.up();
        }
        if(glfwGetKey(window, 'Q') == GLFW_PRESS) {
            movement -= camera.up();
        }

        float speed = 10.0f;
        if(glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) {
            speed *= 10.0f;
        }

        if(movement.length() > 0.0f) {
            const glm::vec3 newPos = camera.position() + movement * deltaTime * speed;
            camera.setView(glm::lookAt(newPos, newPos + camera.forward(), camera.up()));
        }
    }

    if(glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
        const glm::vec2 delta = glm::vec2(mousePos - newMousePos) * 0.01f;
        if(delta.length() > 0.0f) {
            glm::mat4 rot = glm::rotate(glm::mat4(1.0f), delta.x, glm::vec3(0.0f, 1.0f, 0.0f));
            rot = glm::rotate(rot, delta.y, camera.right());
            camera.setView(glm::lookAt(camera.position(), camera.position() + (glm::mat3(rot) * camera.forward()), (glm::mat3(rot) * camera.up())));
        }
    }

    {
        int width = 0;
        int height = 0;
        glfwGetWindowSize(window, &width, &height);
        camera.setRatio(float(width) / float(height));
    }

    mousePos = newMousePos;
}

void loadEnvmap(const std::string& filename) {
    if(auto res = TextureData::fromFile(filename); res.isOk) {
        envmap = std::make_shared<Texture>(Texture::cubemapFromEquirec(res.value));
        scene->setEnvmap(envmap);
    } else {
        std::cerr << "Unable to load envmap (" << filename << ")" << std::endl;
    }
}

void loadScene(const std::string& filename) {
    if(auto res = Scene::fromGltf(filename); res.isOk) {
        scene = std::move(res.value);
        scene->setEnvmap(envmap);
        scene->setIblIntensity(iblIntensity);
        scene->setSun(sunAltitude, sunAzimuth, glm::vec3(sunIntensity));
    } else {
        std::cerr << "Unable to load scene (" << filename << ")" << std::endl;
    }
}

std::vector<std::string> listDataFiles(Span<const std::string> extensions = {}) {
    std::vector<std::string> files;
    for(auto&& entry : std::filesystem::directory_iterator(NEBULA_DATA_PATH)) {
        if(entry.status().type() == std::filesystem::file_type::regular) {
            const auto ext = entry.path().extension();

            bool extMatch = extensions.isEmpty();
            for(const std::string& e : extensions) {
                extMatch |= (ext == e);
            }

            if(extMatch) {
                files.emplace_back(entry.path().string());
            }
        }
    }
    return files;
}

template<typename F>
bool loadFileWindow(Span<std::string> files, F&& loadFunc) {
    char buffer[1024] = {};
    if(ImGui::InputText("Load file", buffer, sizeof(buffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
        loadFunc(buffer);
        return true;
    }

    if(!files.isEmpty()) {
        for(const std::string& p : files) {
            const auto abs = std::filesystem::absolute(p).string();
            if(ImGui::MenuItem(abs.c_str())) {
                loadFunc(p);
                return true;
            }
        }
    }

    return false;
}

void gui(ImGuiRenderer& imgui) {

    static bool openProfiler = false;

    imgui.start();
    DEFER(imgui.finish());


    static std::vector<std::string> loadFiles;

    // ImGui::ShowDemoWindow();

    bool openScenePopup = false;
    bool loadEnvmapPopup = false;
    if(ImGui::BeginMainMenuBar()) {
        if(ImGui::BeginMenu("File")) {
            if(ImGui::MenuItem("Open Scene")) {
                openScenePopup = true;
            }
            if(ImGui::MenuItem("Open Envmap")) {
                loadEnvmapPopup = true;
            }
            ImGui::EndMenu();
        }

        if(ImGui::BeginMenu("Lighting")) {
            ImGui::DragFloat("Exposure", &exposure, 0.01f, 0.01f, 10.0f, "%.2f", ImGuiSliderFlags_Logarithmic);

            ImGui::Separator();

            ImGui::DragFloat("IBL intensity", &iblIntensity, 0.01f, 0.0f, 1.0f);
            scene->setIblIntensity(iblIntensity);

            ImGui::Separator();

            ImGui::DragFloat("Sun Altitude", &sunAltitude, 0.1f, 0.0f, 90.0f, "%.0f");
            ImGui::DragFloat("Sun Azimuth", &sunAzimuth, 0.1f, 0.0f, 360.0f, "%.0f");
            ImGui::DragFloat("Sun Intensity", &sunIntensity, 0.05f, 0.0f, 100.0f, "%.1f");
            scene->setSun(sunAltitude, sunAzimuth, glm::vec3(sunIntensity));

            ImGui::EndMenu();
        }

        if(scene && ImGui::BeginMenu("Scene Info")) {
            ImGui::Text("%u objects", u32(scene->objects().size()));
            ImGui::Text("%u point lights", u32(scene->pointLights().size()));
            ImGui::EndMenu();
        }

        if(ImGui::MenuItem("Profiler")) {
            openProfiler = true;
        }

        ImGui::Separator();
        ImGui::TextUnformatted(deviceName().c_str());

        ImGui::Separator();
        const float fps = (deltaTime > 0.0f) ? (1.0f / deltaTime) : 0.0f;
        const float fpsT = std::clamp(1.0f - fps / 60.0f, 0.0f, 1.0f);
        ImGui::TextColored(ImVec4(fpsT, 1.0f - fpsT, 0.0f, 1.0f), "%.0f FPS", fps);

#ifdef NEBULA_DEBUG
        ImGui::Separator();
        const ImVec4 warningTextColor = ImVec4(1.0f, 0.8f, 0.4f, 1.0f);
        ImGui::TextColored(warningTextColor, ICON_FA_BUG " (DEBUG)");
#endif

        ImGui::EndMainMenuBar();
    }

    if(openScenePopup) {
        ImGui::OpenPopup("###openscenepopup");

        const std::array<std::string, 2> extensions = {".gltf", ".glb"};
        loadFiles = listDataFiles(extensions);
    }

    if(ImGui::BeginPopup("###openscenepopup", ImGuiWindowFlags_AlwaysAutoResize)) {
        if(loadFileWindow(loadFiles, loadScene)) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    if(loadEnvmapPopup) {
        ImGui::OpenPopup("###openenvmappopup");

        const std::array<std::string, 3> extensions = {".png", ".jpg", ".tga"};
        loadFiles = listDataFiles(extensions);
    }

    if(ImGui::BeginPopup("###openenvmappopup", ImGuiWindowFlags_AlwaysAutoResize)) {
        if(loadFileWindow(loadFiles, loadEnvmap)) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    if(openProfiler) {
        if(ImGui::Begin(ICON_FA_CLOCK " Profiler")) {
            ImGui::Text("Total frame time: %.2f ms", deltaTime * 1000.0f);

            const ImGuiTableFlags tableFlags =
                ImGuiTableFlags_SortTristate |
                ImGuiTableFlags_NoSavedSettings |
                ImGuiTableFlags_SizingFixedFit |
                ImGuiTableFlags_BordersInnerV |
                ImGuiTableFlags_Resizable |
                ImGuiTableFlags_RowBg;

            ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, ImVec4(1, 1, 1, 0.01f));
            DEFER(ImGui::PopStyleColor());

            if(ImGui::BeginTable("##timetable", 3, tableFlags)) {
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("CPU (ms)", ImGuiTableColumnFlags_NoResize, 70.0f);
                ImGui::TableSetupColumn("GPU (ms)", ImGuiTableColumnFlags_NoResize, 70.0f);
                ImGui::TableHeadersRow();

                std::vector<u32> indents;
                for(const auto& zone : retrieveProfile()) {
                    auto colorFromTime = [](float time) {
                        const float t = std::min(time / 0.008f, 1.0f); // 8ms = red
                        return ImVec4(t, 1.0f - t, 0.0f, 1.0f);
                    };

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(zone.name.data());

                    ImGui::TableSetColumnIndex(1);
                    ImGui::PushStyleColor(ImGuiCol_Text, colorFromTime(zone.cpuTime));
                    ImGui::Text("%.2f", zone.cpuTime * 1000.0f);

                    ImGui::TableSetColumnIndex(2);
                    ImGui::PushStyleColor(ImGuiCol_Text, colorFromTime(zone.gpuTime));
                    ImGui::Text("%.2f", zone.gpuTime * 1000.0f);

                    ImGui::PopStyleColor(2);

                    if(!indents.empty() && --indents.back() == 0) {
                        indents.pop_back();
                        ImGui::Unindent();
                    }

                    if(zone.containedZones) {
                        indents.push_back(zone.containedZones);
                        ImGui::Indent();
                    }
                }

                ImGui::EndTable();
            }
        }
        ImGui::End();
    }
}




void loadDefaultScene() {
    loadScene(std::string(NEBULA_DATA_PATH) + "DamagedHelmet.glb");
    loadEnvmap(std::string(NEBULA_DATA_PATH) + "pretoria_gardens.jpg");

    // Add lights
    {
        PointLight light;
        light.setPosition(glm::vec3(1.0f, 2.0f, 4.0f));
        light.setColor(glm::vec3(0.0f, 50.0f, 0.0f));
        light.setRadius(100.0f);
        scene->addLight(std::move(light));
    }
    {
        PointLight light;
        light.setPosition(glm::vec3(1.0f, 2.0f, -4.0f));
        light.setColor(glm::vec3(50.0f, 0.0f, 0.0f));
        light.setRadius(50.0f);
        scene->addLight(std::move(light));
    }
}

struct RendererState {
    void resize(glm::uvec2 size) {
        this->size = size;
        if(size.x > 0 && size.y > 0) {
            depthTexture = Texture(size, ImageFormat::Depth32_FLOAT, WrapMode::Clamp);
            litHdrTexture = Texture(size, ImageFormat::RGBA16_FLOAT, WrapMode::Clamp);
            toneMappedTexture = Texture(size, ImageFormat::RGBA8_UNORM, WrapMode::Clamp);
        }
    }

    glm::uvec2 size = {};

    Texture depthTexture;
    Texture litHdrTexture;
    Texture toneMappedTexture;
};





int main() {
    DEBUG_ASSERT([] { std::cout << "Debug asserts enabled" << std::endl; return true; }());

    glfwCheck(glfwInit());
    DEFER(glfwTerminate());
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(1600, 900, "nebula", nullptr, nullptr);
    glfwCheck(window);
    DEFER(glfwDestroyWindow(window));

    initGraphics(window);

    std::unique_ptr<ImGuiRenderer> imgui = std::make_unique<ImGuiRenderer>(window);

    loadDefaultScene();

    auto tonemapProgram = Program::fromFiles("screen.slang", "tonemap.slang");
    RendererState renderer;

    for(;;) {
        glfwPollEvents();
        if(glfwWindowShouldClose(window) || glfwGetKey(window, GLFW_KEY_ESCAPE)) {
            break;
        }

        processProfileMarkers();

        {
            int width = 0;
            int height = 0;
            glfwGetWindowSize(window, &width, &height);

            if(renderer.size != glm::uvec2(width, height)) {
                // GPU may still be sampling last frame's HDR/depth; wait before rebuilding them.
                waitForGpuIdle();
                renderer.resize(glm::uvec2(width, height));
            }
        }

        updateDeltaTime();

        if(const auto& io = ImGui::GetIO(); !io.WantCaptureMouse && !io.WantCaptureKeyboard) {
            processInputs(window, scene->camera());
        }

        beginFrame();
        {
            PROFILE_GPU("Frame");

            // Render the scene
            RenderPass(&renderer.depthTexture, std::array{&renderer.litHdrTexture}, true, true, "Main pass", [&] {
                scene->render();
            });

            // Apply a tonemap as a full screen pass
            RenderPass(nullptr, std::array{&renderer.toneMappedTexture}, false, true, "Tonemap", [&] {
                PushConstants push;
                push.set(HASH("exposure"), exposure);
                PassResources pass{};
                pass.textures[0] = &renderer.litHdrTexture;
                const RasterState raster{
                    .depthTestEnable = false,
                    .cullMode = VK_CULL_MODE_NONE,
                };
                drawFullscreen(*tonemapProgram, raster, pass, push);
            });

            RenderPass(RenderPass::Swapchain{}, false, "Blit", [&] {
                blitToScreen(renderer.toneMappedTexture);
            });

            RenderPass(RenderPass::Swapchain{}, false, "GUI", [&] {
                gui(*imgui);
            });
        }
        endFrame();
    }


    // Destroy scene and child graphics objects before tearing down the device.
    scene = nullptr;
    envmap = nullptr;
    imgui = nullptr;
    tonemapProgram = nullptr;
    renderer = {};
    destroyGraphics();
}
