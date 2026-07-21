/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: LicenseRef-NvidiaProprietary
 *
 * NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
 * property and proprietary rights in and to this material, related
 * documentation and any modifications thereto. Any use, reproduction,
 * disclosure or distribution of this material and related documentation
 * without an express license agreement from NVIDIA CORPORATION or
 * its affiliates is strictly prohibited.
 */

#include "Presenter.h"

#if defined(_WIN32)
#include <Windows.h>
#endif

#include <GLFW/glfw3.h>
#include <imgui_impl_glfw.h>

#include "imgui.h"

#if defined( VIEWER_API_D3D12 )
#include "imgui_impl_dx12.h"
#else
#include "imgui_impl_vulkan.h"
#endif

#include "Timer.h"

void PopulateUi(PresenterNewFrameDesc& desc);

bool PollWindowEvents(void* window)
{
    glfwPollEvents();
    GLFWwindow* glfwWindow = static_cast<GLFWwindow*>(window);
    return glfwWindowShouldClose(glfwWindow) != GLFW_TRUE;
}

void PrepareNewFrame(PresenterNewFrameDesc& desc)
{
    static bool isMouseMoving = false;
    static ImVec2 mousePosToReturn = {};

    Camera& camera = *desc.cbUpdateDesc.camera;
    ExposedSettings& settings = *desc.cbUpdateDesc.settings;
    ConstantBufferHelper& constantBufferHelper = *desc.constantBufferHelper;

    void* window = desc.window;
    uint32_t renderWidth = desc.cbUpdateDesc.renderWidth;
    uint32_t renderHeight = desc.cbUpdateDesc.renderHeight;

    ImGuiIO& io = ImGui::GetIO();
    io.WantSetMousePos = false;

    CameraDesc cameraDesc = {};
    cameraDesc.aspectRatio = float(renderWidth) / float(renderHeight);
    cameraDesc.up = settings.cameraUp;
    cameraDesc.timeScale = 1.0f * (io.DeltaTime * 1000.0f);
    cameraDesc.flipX = settings.flipCameraX;

    GLFWwindow* glfwWindow = static_cast<GLFWwindow*>(window);
    bool anyUiInteraction = ImGui::IsAnyItemHovered() || ImGui::IsAnyItemFocused() || ImGui::IsAnyItemActive();
    if (!io.MouseDown[ImGuiMouseButton_Right] && !(io.MouseDown[ImGuiMouseButton_Left] && !anyUiInteraction))
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
        glfwSetInputMode(glfwWindow, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        if (isMouseMoving)
            glfwSetCursorPos(glfwWindow, mousePosToReturn.x, mousePosToReturn.y);
        isMouseMoving = false;
    }
    else
    {
        if (isMouseMoving == false)
            mousePosToReturn = io.MousePos;

        if (io.MouseWheel > 0.0f)
            camera.state.motionScale *= 1.1f;
        else if (io.MouseWheel < 0.0f)
            camera.state.motionScale /= 1.1f;

        float motionScale = camera.state.motionScale * (ImGui::IsKeyDown(ImGuiKey_LeftShift)? 10.0f : 1.0f);
        {
            cameraDesc.dYaw = -io.MouseDelta.x * settings.mouseSensitivity;
            cameraDesc.dPitch = -io.MouseDelta.y * settings.mouseSensitivity;

            if (ImGui::IsKeyDown(ImGuiKey_W))
                cameraDesc.dLocalForward += motionScale;
            if (ImGui::IsKeyDown(ImGuiKey_S))
                cameraDesc.dLocalForward -= motionScale;

            if (ImGui::IsKeyDown(ImGuiKey_D))
                cameraDesc.dLocalSide += motionScale;
            if (ImGui::IsKeyDown(ImGuiKey_A))
                cameraDesc.dLocalSide -= motionScale;

            if (ImGui::IsKeyDown(ImGuiKey_E))
                cameraDesc.dLocalVertical += motionScale;
            if (ImGui::IsKeyDown(ImGuiKey_Q))
                cameraDesc.dLocalVertical -= motionScale;
        }

        if (io.MouseDown[ImGuiMouseButton_Right])
        {
            isMouseMoving = true;
            ImGui::SetMouseCursor(ImGuiMouseCursor_None);
            glfwSetInputMode(glfwWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        }
        else
            ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
    }

    camera.Update(cameraDesc);
    constantBufferHelper.Update(desc.cbUpdateDesc);
}

void PresenterNewFrame(PresenterNewFrameDesc& desc)
{
#if defined( VIEWER_API_D3D12 )
    ImGui_ImplDX12_NewFrame();
#else
    ImGui_ImplVulkan_NewFrame();
#endif

    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    {
        PrepareNewFrame(desc);
        PopulateUi(desc);
    }
    ImGui::End();
    ImGui::Render();
}

void BusyWait(float fpsLimit, bool enableLimit)
{
    static int64_t frameTimestamp = Timer::GetTimestamp();

    if (enableLimit && frameTimestamp != 0)
    {
        double delta = 0.0;
        double limit = 1000.0 / double(fpsLimit);
        do 
            delta = Timer::GetDeltaMs(frameTimestamp);
        while (delta < limit);
    }
    frameTimestamp = Timer::GetTimestamp();
}

bool SkipPresent(bool isNewAppFrame, bool enableFpsLimit)
{
    static int64_t lastTimestapm = Timer::GetTimestamp();
    bool result = false;
    if(!isNewAppFrame && !enableFpsLimit)
    {
        const double noNewFrameThreshold = 16.66666;
        const double delta = Timer::GetDeltaMs(lastTimestapm);
        result = delta < noNewFrameThreshold;
    }
    lastTimestapm = result ? lastTimestapm : Timer::GetTimestamp();
    return result;
}

inline void AddColorHint(const ImVec4& color, const char* name, const char* desc)
{
    ImGui::ColorButton(name, color, 0x7FF);
    ImGui::SameLine();
    ImGui::Text("%s", desc);
};

inline void PrintInstanceFlags(uint32_t flags)
{
    ImGui::Text("Instance Flags: ");
    if (flags == 0)
    {
        ImGui::SameLine();
        ImGui::Text("None");
        return;
    }

    for(uint32_t i = 0; i < INSTANCE_FLAG_NUM; ++i)
    {
        if(flags & g_RtInstanceFlagDescs[i].val)
            ImGui::Text("\t%s", g_RtInstanceFlagDescs[i].str);
    }
}

inline void PrintGeometryFlags(uint32_t flags)
{
    ImGui::Text("Geometry Flags: ");
    if (flags == 0)
    {
        ImGui::SameLine();
        ImGui::Text("None");
        return;
    }

    for (uint32_t i = 0; i < GEOMETRY_FLAG_NUM; ++i)
    {
        if (flags & g_RtGeometryFlagDescs[i].val)
            ImGui::Text("\t%s", g_RtGeometryFlagDescs[i].str);
    }
}

inline void PrintBuildFlags(uint32_t flags)
{
    ImGui::Text("Build Flags: ");
    if (flags == 0)
    {
        ImGui::SameLine();
        ImGui::Text("None");
        return;
    }

    if (flags == AS_BUILD_FLAG_UNKNOWN)
    {
        ImGui::SameLine();
        ImGui::Text("Unknown");
        return;
    }

    const char* performUpdateStr = nullptr;
    for (uint32_t i = 0; i < AS_BUILD_FLAG_NUM; ++i)
    {
        if (flags & g_RtBuildFlagDesc[i].val)
        {
            if(AS_BUILD_FLAG_PERFORM_UPDATE == g_RtBuildFlagDesc[i].val)
            {
                performUpdateStr = g_RtBuildFlagDesc[i].str;
                continue;
            }
            ImGui::Text("\t%s", g_RtBuildFlagDesc[i].str);
        }
    }
    if(performUpdateStr)
    {
        ImGui::SameLine();
        ImGui::Text("\t%s", performUpdateStr);
    }
}

inline void AddToolTip(const char* text)
{
    ImGui::SameLine();
    ImGui::TextDisabled("[?]");
    if (ImGui::BeginItemTooltip())
    {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void PopulateUi(PresenterNewFrameDesc& desc)
{
    ExposedSettings& settings = *desc.cbUpdateDesc.settings;
    BlasTracker* blasTracker = desc.blasTracker;
    const uint32_t tlasFlags = desc.tlasFlags;
    const uint64_t tlasAddress = desc.tlasAddress;
    const uint32_t numTlasesInFrame = desc.numTlasesInFrame;
    const uint32_t numInstancesInTlas = desc.numInstancesInTlas;
    const float renderWidth = static_cast<float>(desc.cbUpdateDesc.renderWidth);
    const float renderHeight = static_cast<float>(desc.cbUpdateDesc.renderHeight);
    const void* feedbackBuffer = desc.feedbackData;
    const bool allowWireAndFrame = desc.enablePositionFetch;
    GLFWwindow* glfwWindow = static_cast<GLFWwindow*>(desc.window);

    constexpr ImVec4 c_Red = ImVec4(0.9f, 0.0f, 0.0f, 1.0f);
    constexpr ImVec4 c_Grey = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
    constexpr ImVec4 c_DarkGrey = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
    constexpr ImVec4 c_Orange = ImVec4(0.9f, 0.5f, 0.0f, 1.0f);
    constexpr ImVec4 c_Green = ImVec4(0.2f, 0.5f, 0.2f, 1.0f);
    constexpr ImVec4 c_Yellow = ImVec4(0.75f, 0.75f, 0.0f, 1.0f);

    ImGuiIO& io = ImGui::GetIO();
    static ImVec2 windowRectMin = {};
    static ImVec2 windowRectMax = {};
    static const ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings;
    {
        static bool isFirstFrame = true;
        if (isFirstFrame)
        {
            ImVec2 pos = ImVec2(float(0.0f + 5.0f / renderWidth), float(0.0f + 5.0f / renderHeight));
            ImGui::SetNextWindowPos(pos);
            isFirstFrame = false;
        }
        ImGui::SetNextWindowSize(ImVec2(0.0f, 0.0f));
        ImGui::Begin("Nv BVH Viewer", NULL, windowFlags);
        ImGui::Text("%7.4fms (%5.2f fps)", io.DeltaTime * 1000.0f, io.Framerate);
        bool show = false;

        { // RENDER
            ImGui::NewLine();
            show = ImGui::CollapsingHeader("RENDER", ImGuiTreeNodeFlags_CollapsingHeader | ImGuiTreeNodeFlags_DefaultOpen);
            ImGui::PushID("render");
            if (show)
            {
                ImGui::Combo("Color by", &settings.visualizationMode, g_VisualizationModeNames, int(VIS_MODE_NUM));

                if (settings.visualizationMode == int(VIS_MODE_HEAT_MAP))
                {
                    ImGui::DragFloat("Heatmap Scaling", &settings.heatmapScaling, 0.1f, 0.1f, 100.0f, "%.2f", ImGuiSliderFlags_::ImGuiSliderFlags_Logarithmic);
                    ImGui::PushStyleColor(0, c_Grey);
                    ImGui::Text("timestampDelta = saturate(timestampDelta / %.1f)", 65000.0f * settings.heatmapScaling);
                    ImGui::PopStyleColor();
                    ImGui::SliderInt("Num Samples", &settings.numSamples, 1, 64);
                }
                if (settings.visualizationMode == int(VIS_MODE_UPDATE_STATE))
                {
                    AddColorHint(c_Red, "Rebuild", " - full rebuild");
                    AddColorHint(c_Orange, "Refit", " - update / refit");
                    AddColorHint(c_Yellow, "DynamicNoUpdate", " - has allow_update flag, but not updated");
                    AddColorHint(c_Green, "Static", " - static");
                }
                {
                    for (uint32_t i = 0; i < 8; ++i)
                        settings.perBitInstanceMaskValue[i] = bool((settings.instanceInclusionMask >> i) & 0x1);

                    for (int32_t i = 7; i >= 0; --i)
                    {
                        ImGui::PushID(i + 0xC0BA);
                        if (ImGui::RadioButton("", settings.perBitInstanceMaskValue[i]))
                            settings.perBitInstanceMaskValue[i] = !settings.perBitInstanceMaskValue[i];
                        ImGui::PopID();
                        ImGui::SameLine();
                    }
                    ImGui::NewLine();

                    uint8_t newValue = 0x0;
                    for (uint32_t i = 0; i < 8; ++i)
                        newValue |= uint8_t((settings.perBitInstanceMaskValue[i] & 0x1) << i);
                    settings.instanceInclusionMask = newValue;

                    float totalWidth = ImGui::CalcItemWidth();
                    float beginX = ImGui::GetCursorPosX();
                    if (ImGui::Button("<<"))
                        settings.instanceInclusionMask = settings.instanceInclusionMask == 0 ? 0x1 : settings.instanceInclusionMask << 1;

                    ImGui::SameLine();
                    float buttonWidth = ImGui::GetCursorPosX() - beginX;

                    if (ImGui::Button("00"))
                        settings.instanceInclusionMask = 0x0;
                    ImGui::SameLine();

                    if (ImGui::Button("ff"))
                        settings.instanceInclusionMask = 0xFF;
                    ImGui::SameLine();

                    if (ImGui::Button(">>"))
                        settings.instanceInclusionMask = settings.instanceInclusionMask == 0 ? 0x80 : settings.instanceInclusionMask >> 1;
                    ImGui::SameLine();

                    ImGui::PushItemWidth(totalWidth - (buttonWidth * 3.0f));
                    ImGui::InputScalar("Ray Mask", ImGuiDataType_U8, &settings.instanceInclusionMask, nullptr, nullptr, "%x", ImGuiInputTextFlags_CharsHexadecimal);
                    ImGui::PopItemWidth();
                }

                const bool isFocused = glfwGetWindowAttrib(glfwWindow, GLFW_FOCUSED) == GLFW_TRUE;
                ImGui::Checkbox("Async Render", &settings.allowLastKnownBvhRender);
                settings.allowLastKnownBvhRender = settings.allowLastKnownBvhRender && (isFocused || settings.windowOnTop);
                AddToolTip("Allow the window thread to render the last known TLAS.\n"
                    "Designed for apps that pause render when out of focus.\n"
                    "May be unsafe.\n"
                    "Automatically gets disabled when viewer window is out of focus.");
                ImGui::SameLine();

                bool windowOnTop = settings.windowOnTop;
                ImGui::Checkbox("\tTop Most", &windowOnTop);
                if (settings.windowOnTop != windowOnTop)
                {
                    const int topmost = windowOnTop ? GLFW_TRUE : GLFW_FALSE;
                    glfwSetWindowAttrib(glfwWindow, GLFW_FLOATING, topmost);
                    settings.windowOnTop = windowOnTop;
                }

                if (allowWireAndFrame)
                {
                    ImGui::Checkbox("Bary", &settings.enableBarycentricShading);

                    ImGui::SameLine();
                    ImGui::Checkbox("\tWire", &settings.enableWireShading);

                    ImGui::SameLine();
                    ImGui::Checkbox("\tFrame", &settings.enableWireframeMode);

                    settings.enableWireframeMode = settings.enableWireShading ? settings.enableWireframeMode : false;
                }
                else
                    settings.enableBarycentricShading = true;

                ImGui::PushID("fps limit");
                {
                    float totalWidth = ImGui::CalcItemWidth();
                    const char* label = settings.enableFpsLimit ? "" : "Enable FPS Limit";
                    bool enableFpsLimit = settings.enableFpsLimit;
                    ImGui::Checkbox(label, &settings.enableFpsLimit);

                    if (enableFpsLimit)
                    {
                        float widthOffset = ImGui::CalcItemWidth();
                        ImGui::SameLine();
                        ImGui::PushItemWidth(totalWidth - widthOffset);
                        ImGui::DragFloat("FPS Limit", &settings.fpsLimit, 1.0f, 20.0f, 500.0f, "%.0f");
                        ImGui::PopItemWidth();
                    }
                }
                ImGui::PopID();
            }
            ImGui::PopID();
        }

        { // TLAS
            ImGui::NewLine();
            show = ImGui::CollapsingHeader("TLAS", ImGuiTreeNodeFlags_CollapsingHeader | ImGuiTreeNodeFlags_DefaultOpen);
            ImGui::PushID("tlas");
            if (show)
            {
                if (!numInstancesInTlas)
                {
                    ImGui::SameLine();
                    ImGui::PushStyleColor(0, c_Red);
                    ImGui::Text(" NOTHING TO RENDER");
                    ImGui::PopStyleColor();
                }
                {
                    float totalWidth = ImGui::CalcItemWidth();
                    float smaller = ImGui::CalcItemWidth() * 0.4f;
                    float bigger = totalWidth - smaller;
                    ImGui::PushItemWidth(bigger);
                    ImGui::Combo("Sort", &settings.tlasSortMode, g_TlasSortModeNames, int(TlasSortModes::Count));
                    ImGui::PopItemWidth();

                    ImGui::PushItemWidth(smaller);
                    ImGui::SameLine();
                    ImGui::InputInt("Id", &settings.tlasId);
                    ImGui::PopItemWidth();

                    settings.tlasId = settings.tlasId < 0 ? 0 : settings.tlasId;
                    int tlasNumLimit = numTlasesInFrame < c_MaxTlasPerFrame ? numTlasesInFrame : c_MaxTlasPerFrame;
                    settings.tlasId = settings.tlasId >= tlasNumLimit ? tlasNumLimit - 1 : settings.tlasId;

                    ImGui::Text("TLAS[0x%llX]", tlasAddress);
                    ImGui::SameLine();
                    ImGui::Text(" Count: %u", numInstancesInTlas);
                    PrintBuildFlags(tlasFlags);
                }
            }
            ImGui::PopID();
        }

        { // CAMERA
            Camera& camera = *desc.cbUpdateDesc.camera;
            ImGui::NewLine();
            show = ImGui::CollapsingHeader("CAMERA [RMB + WASDEQ | Scroll - Speed]", ImGuiTreeNodeFlags_CollapsingHeader | ImGuiTreeNodeFlags_DefaultOpen);
            ImGui::PushID("camera");
            if (show)
            {
                float totalWidth = ImGui::CalcItemWidth();
                ImGui::PushItemWidth((totalWidth * 0.5f));
                ImGui::PushID("Sensitivity");
                float mouseSensitivity = settings.mouseSensitivity;
                ImGui::DragFloat("", &mouseSensitivity, 0.1f, 0.1f, 10.0f, "Sensitivity %.1f");
                settings.mouseSensitivity = mouseSensitivity > 0.00001f ? mouseSensitivity : settings.mouseSensitivity;
                ImGui::PopItemWidth();
                ImGui::PopID();

                ImGui::SameLine();

                static float3 posInput = {};
                ImGui::SameLine();
                ImGui::PushItemWidth((totalWidth * 0.5f) - ImGui::GetStyle().ItemSpacing.x);
                ImGui::PushID("CameraUp");
                {
                    static bool yUp = settings.cameraUp == CameraUp::Y;
                    static bool zUp = settings.cameraUp == CameraUp::Z;

                    if(ImGui::RadioButton("Y Up", yUp))
                    {
                        yUp = !yUp;
                        zUp = yUp ? false : zUp;
                    }

                    ImGui::SameLine();
                    if (ImGui::RadioButton("Z Up", zUp))
                    {
                        zUp = !zUp;
                        yUp = zUp ? false : yUp;
                    }

                    settings.cameraUp = zUp ? CameraUp::Z : settings.cameraUp;
                    settings.cameraUp = yUp ? CameraUp::Y : settings.cameraUp;
                }
                ImGui::PopItemWidth();
                ImGui::PopID();

                ImGui::SameLine();
                ImGui::Checkbox("MirrorX", &settings.flipCameraX);

                static float3 currentPos = float3(camera.state.globalPosition);
                currentPos = float3(camera.state.globalPosition);

                ImGui::PushID("currentPos");
                ImGui::PushStyleColor(0, c_Grey);
                ImGui::InputFloat3("", (float*)&currentPos, "%.2f", ImGuiInputTextFlags_ReadOnly);
                ImGui::PopStyleColor();
                ImGui::PopID();

                ImGui::SameLine();
                if (ImGui::Button("Apply"))
                    posInput = currentPos;

                ImGui::PushID("teleportCoords");
                ImGui::InputFloat3("", (float*)&posInput, "%.2f");
                ImGui::PopID();

                ImGui::SameLine();
                if (ImGui::Button("Go"))
                    camera.state.globalPosition = double3(posInput);

                if (ImGui::Button("Centre"))
                    camera.state.globalPosition = *blasTracker->GetWorldCenter();

                ImGui::SameLine();
                if (ImGui::Button("Closest"))
                    camera.state.globalPosition = *blasTracker->GetWorldClosest();
            }
            ImGui::PopID();
        }

        { // GEOMETRY
            ImGui::NewLine();
            show = ImGui::CollapsingHeader("GEOMETRY", ImGuiTreeNodeFlags_CollapsingHeader | ImGuiTreeNodeFlags_DefaultOpen);
            ImGui::PushID("geometry");
            if (show)
            {
                ImGui::Text("Ray Culling:");
                {
                    ImGui::Checkbox("Opaque", &settings.cullOpaque);
                    ImGui::SameLine();
                    ImGui::Checkbox("NonOpaque", &settings.cullNonOpaque);
                    ImGui::SameLine();
                    if(ImGui::RadioButton("BackFace", settings.cullBackFace))
                    {
                        settings.cullBackFace = !settings.cullBackFace;
                        settings.cullFrontFace = settings.cullBackFace ? false : settings.cullFrontFace;
                    }
                    ImGui::SameLine();
                    if (ImGui::RadioButton("FrontFace", settings.cullFrontFace))
                    {
                        settings.cullFrontFace = !settings.cullFrontFace;
                        settings.cullBackFace = settings.cullFrontFace ? false : settings.cullBackFace;
                    }
                }
                ImGui::Separator();

                ImGui::Text("RT Primitive Types:");
                {
                    if (ImGui::BeginTable("primitives", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_Borders))
                    {
                        for (uint32_t i = 0; i < uint32_t(RtGeometryPrimitiveTypes::Count); ++i)
                        {
                            ImGui::TableNextColumn();
                            ImGui::Selectable(g_GeometryPrimitiveNames[i], &settings.primitiveTypeToggles[i]);
                        }
                        ImGui::EndTable();
                    }
                }
                ImGui::Separator();

                {
                    float width = ImGui::CalcTextSize(g_FlagVisualizationModes[settings.flagVisualizationMode]).x + ImGui::GetStyle().FramePadding.x * 8.0f;
                    ImGui::PushItemWidth(width);
                    ImGui::PushID(0xCEBA + 0);
                    ImGui::Combo("", &settings.flagVisualizationMode, g_FlagVisualizationModes, std::size(g_FlagVisualizationModes));
                    ImGui::PopID();
                    ImGui::PopItemWidth();
                    ImGui::SameLine();

                    width = ImGui::CalcTextSize(g_FlagInclusionModes[settings.flagInclusionMode]).x + ImGui::GetStyle().FramePadding.x * 8.0f;
                    ImGui::PushItemWidth(width);
                    ImGui::PushID(0xCEBA + 1);
                    ImGui::Combo("", &settings.flagInclusionMode, g_FlagInclusionModes, std::size(g_FlagInclusionModes));
                    ImGui::PopID();
                    ImGui::SameLine();
                    ImGui::PopItemWidth();

                    width = ImGui::CalcTextSize(g_FlagLogicalOperatorModes[settings.flagLogicalOperator]).x + ImGui::GetStyle().FramePadding.x * 8.0f;
                    ImGui::PushItemWidth(width);
                    ImGui::PushID(0xCEBA + 2);
                    ImGui::Combo("", &settings.flagLogicalOperator, g_FlagLogicalOperatorModes, std::size(g_FlagLogicalOperatorModes));
                    ImGui::PopID();
                    ImGui::PopItemWidth();

                    bool anyFlagSet = false;
                    auto ConstructRtFlagTable = [&anyFlagSet](const char* id, const RtFlagDesc* flagList, bool* flagState, uint32_t flagNum)
                        {
                            constexpr int numColumns = 2;
                            ImGui::Text("%s", id);
                            if (ImGui::BeginTable(id, numColumns, ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_Borders))
                            {
                                for (uint32_t i = 0; i < flagNum; ++i)
                                {
                                    ImGui::TableNextColumn();
                                    ImGui::Selectable(flagList[i].str, &flagState[i]);
                                    anyFlagSet |= flagState[i];
                                }
                                ImGui::EndTable();
                            }
                        };

                    ConstructRtFlagTable("Instance Flags:", g_RtInstanceFlagDescs, settings.instanceFlagStates, INSTANCE_FLAG_NUM);
                    ConstructRtFlagTable("Build Flags:", g_RtBuildFlagDesc, settings.buildFlagStates, AS_BUILD_FLAG_NUM);
                    ConstructRtFlagTable("Geometry Flags:", g_RtGeometryFlagDescs, settings.geometryFlagStates, GEOMETRY_FLAG_NUM);

                    if (anyFlagSet)
                        AddColorHint(c_DarkGrey, "No Data", " - no build data or can't resolve");
                }

#if _DEBUG
                ImGui::InputInt("Debug", &settings.debugUint, 1, 1);
                settings.debugUint = settings.debugUint < 0 ? 0 : settings.debugUint;
#endif

                if (feedbackBuffer)
                {
                    static ViewerBlasDesc blasDesc = {};
                    static ViewerBlasDesc* blasDescPtr = nullptr;
                    static uint32_t instanceFlags = 0;
                    static uint32_t instanceMask = 0;
                    bool isInsideWindow = ImGui::IsMouseHoveringRect(windowRectMin, windowRectMax);
                    bool isMouseDrag = ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && !isInsideWindow;

                    if ((io.MouseDoubleClicked[0] || isMouseDrag) && !isInsideWindow)
                    {
                        FeedbackBufferLayout* mappedData = (FeedbackBufferLayout*)feedbackBuffer;
                        settings.lastSelectedBlasHash = mappedData->blasHash;
                        settings.lastSelectedGeometryId = mappedData->data >> 16;
                        instanceFlags = (mappedData->data) & 0x000000FF;
                        instanceMask = (mappedData->data >> 8) & 0x000000FF;
                        blasDescPtr = nullptr;
                        blasDesc = {};

#if defined( VIEWER_API_D3D12 )
                        if(mappedData->clusteredBlasAddr)
                        {
                            blasDesc.address = mappedData->clusteredBlasAddr;
                            blasDesc.hash = HashUint64(mappedData->clusteredBlasAddr);
                            const ClusteredBlasDescGPU& clusteredDesc = mappedData->clusteredBlas;
                            blasDesc.geometries.resize(2);
                            blasDesc.geometries[0].data[0] = clusteredDesc.numClusters;
                            blasDesc.geometries[0].data[1] = clusteredDesc.numOmmClusters;
                            blasDesc.geometries[0].data[2] = clusteredDesc.numVertices;
                            blasDesc.geometries[1].data[0] = clusteredDesc.numTriangles;
                            blasDesc.buildFlags = clusteredDesc.flags | AS_BUILD_FLAG_CLUSTERED;
                            blasDescPtr = &blasDesc;
                        }
#endif //  VIEWER_API_D3D12
                    }
                    if(blasDescPtr == nullptr)
                    {
                        blasDescPtr = blasTracker->GetBlasDesc(settings.lastSelectedBlasHash);
                        if(blasDescPtr) blasDesc = *blasDescPtr;
                    }
                    {
                        ImGui::Separator();
                        ImGui::Text("Geometry Stats (LMB Double Click / Ctrl + Point)");
                        ImGui::Separator();
                        ImGui::Text("BLAS: %u", settings.lastSelectedBlasHash);
                        if (blasDescPtr == nullptr)
                        {
                            ImGui::Text("GeometryId: %u / ?", settings.lastSelectedGeometryId);
                            ImGui::SameLine();

                            if (ImGui::SmallButton("<"))
                                settings.lastSelectedGeometryId = std::min(settings.lastSelectedGeometryId, settings.lastSelectedGeometryId - 1);
                            ImGui::SameLine();
                            if (ImGui::SmallButton(">"))
                                ++settings.lastSelectedGeometryId;

                            ImGui::Text("No build data available.");
                        }
                        else
                        {
                            PrintBuildFlags(blasDesc.buildFlags);
                            ImGui::Separator();
                            uint32_t numGeometries = uint32_t(blasDesc.geometries.size());

                            if((blasDesc.buildFlags & AS_BUILD_FLAG_CLUSTERED) != 0 && numGeometries > 0)
                            {
                                ViewerGeometryDesc& a = blasDesc.geometries[0];
                                ViewerGeometryDesc& b = blasDesc.geometries[1];
                                ImGui::Text("Num Clusters: %u",     a.data[0]);
                                if(a.data[1])
                                    ImGui::Text("Num Omm Clusters: %u", a.data[1]);
                                ImGui::Text("Vertex Count: %u",     a.data[2]);
                                ImGui::Text("Primitive Count: %u",  b.data[0]);
                            }
                            else
                            {
                                settings.lastSelectedGeometryId = std::min(settings.lastSelectedGeometryId, numGeometries - 1);
                                ViewerGeometryDesc& geometry = blasDesc.geometries[settings.lastSelectedGeometryId];
                                ImGui::Text("Geometry: %lu / %zu", settings.lastSelectedGeometryId + 1lu, blasDesc.geometries.size());

                                if (numGeometries > 1)
                                {
                                    ImGui::SameLine();
                                    if (ImGui::SmallButton("<"))
                                        settings.lastSelectedGeometryId = std::min(settings.lastSelectedGeometryId, settings.lastSelectedGeometryId - 1);
                                    ImGui::SameLine();
                                    if (ImGui::SmallButton(">"))
                                        settings.lastSelectedGeometryId = std::min(numGeometries - 1, settings.lastSelectedGeometryId + 1);
                                }

                                RtGeometryTypes geometryType = RtGeometryTypes(geometry.type);
                                switch (geometryType)
                                {
                                case RtGeometryTypes::Triangles:
                                {
                                    ImGui::Text("Type: Triangles");
                                    ImGui::Text("Index Count: %u", geometry.data[0]);
                                    ImGui::Text("Vertex Count: %u", geometry.data[1]);
                                    break;
                                }
                                case RtGeometryTypes::OmmTriangles:
                                {
                                    ImGui::Text("Type: OMM Triangles");
                                    ImGui::Text("Index Count: %u", geometry.data[0]);
                                    ImGui::Text("Vertex Count: %u", geometry.data[1]);
                                    break;
                                }
                                case RtGeometryTypes::DmmTriangles:
                                {
                                    ImGui::Text("Type: DMM Triangles");
                                    ImGui::Text("Index Count: %u", geometry.data[0]);
                                    ImGui::Text("Vertex Count: %u", geometry.data[1]);
                                    break;
                                }
                                case RtGeometryTypes::AABB:
                                {
                                    ImGui::Text("Type: AABB");
                                    ImGui::Text("AABB Count: %u", geometry.data[0]);
                                    break;
                                }
                                case RtGeometryTypes::LSS:
                                {
                                    ImGui::Text("Type: LSS");
                                    ImGui::Text("Vertex Count: %u", geometry.data[0]);
                                    ImGui::Text("Index Count: %u", geometry.data[1]);
                                    ImGui::Text("Primitive Count: %u", geometry.data[2]);
                                    break;
                                }
                                case RtGeometryTypes::Spheres:
                                {
                                    ImGui::Text("Type: Sphere");
                                    ImGui::Text("Vertex Count: %u", geometry.data[0]);
                                    ImGui::Text("Index Count: %u", geometry.data[1]);
                                    break;
                                }
                                case RtGeometryTypes::None:
                                case RtGeometryTypes::Count:
                                default:
                                    ImGui::Text("Type: UNKNOWN");
                                    break;
                                }
                                PrintGeometryFlags(geometry.flags);
                            }
                        }

                        ImGui::Separator();
                        ImGui::Text("InstanceMask: %#2x", instanceMask);
                        if (instanceMask)
                        {
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Apply"))
                                settings.instanceInclusionMask = static_cast<uint8_t>(instanceMask);
                        }
                        PrintInstanceFlags(instanceFlags);
                    }
                }
            }
            ImGui::PopID();
        }

        windowRectMin = ImGui::GetWindowPos();
        windowRectMax.x = ImGui::GetWindowWidth();
        windowRectMax.y = ImGui::GetWindowHeight();
    }
}