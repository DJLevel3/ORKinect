// Dear ImGui: standalone example application for SDL3 + OpenGL
// (SDL is a cross-platform general purpose library for handling windows, inputs, OpenGL/Vulkan/Metal graphics context creation, etc.)

// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_opengl3.h"
#include <stdio.h>
#include <iostream>
#include <SDL3/SDL.h>
#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <SDL3/SDL_opengles2.h>
#else
#include <SDL3/SDL_opengl.h>
#endif

#define DISPLAYDEPTH

#include <Windows.h>
#include <Ole2.h>
#include <NuiApi.h>

#include <glm/glm.hpp>
//#include <glm/gtc/matrix_transform.hpp>
//#include <glm/gtc/type_ptr.hpp>

#include <winsock2.h>
#include <ws2tcpip.h>
#include "json.hpp"

using json = nlohmann::json;
json skeletonJson;
bool skeletonJsonChanged = false;

bool headCube = true;
bool handCube = false;
bool footCube = false;

glm::vec3 headCubeRotation = { 0,0,0 };

glm::vec3 handCubeRotationL = { 0,0,0 };
glm::vec3 handCubeRotationR = { 0,0,0 };

glm::vec3 footCubeRotationL = { 0,0,0 };
glm::vec3 footCubeRotationR = { 0,0,0 };

WSADATA wsaData = { 0 };
SOCKET orsock = INVALID_SOCKET;

#define camW 640
#define camH 480

GLuint texID;
GLubyte data[camW * camH * 4];

HANDLE rgbStream;
HANDLE depthStream;
INuiSensor* sensor;

int activeSkeletons = 0;
Vector4 skeletonPosition[NUI_SKELETON_POSITION_COUNT];
Vector4 skeletonPosition2[NUI_SKELETON_POSITION_COUNT];

bool initKinect() {
    // Get a working kinect sensor
    int numSensors = 0;
    if (NuiGetSensorCount(&numSensors) < 0 || numSensors < 1) return false;
    if (NuiCreateSensorByIndex(0, &sensor) < 0) return false;

    // Initialize sensor
    sensor->NuiInitialize(NUI_INITIALIZE_FLAG_USES_DEPTH | NUI_INITIALIZE_FLAG_USES_COLOR | NUI_INITIALIZE_FLAG_USES_SKELETON);

    sensor->NuiSkeletonTrackingEnable(
        NULL,
        0     // NUI_SKELETON_TRACKING_FLAG_ENABLE_SEATED_SUPPORT for only upper body
    );

    sensor->NuiImageStreamOpen(
        NUI_IMAGE_TYPE_DEPTH,
        NUI_IMAGE_RESOLUTION_640x480,    // Image resolution
        0,      // Image stream flags, e.g. near mode
        2,      // Number of frames to buffer
        NULL,   // Event handle
        &depthStream);
    sensor->NuiImageStreamOpen(
        NUI_IMAGE_TYPE_COLOR,                     // Depth camera or rgb camera?
        NUI_IMAGE_RESOLUTION_640x480,             // Image resolution
        0,      // Image stream flags, e.g. near mode
        2,      // Number of frames to buffer
        NULL,   // Event handle
        &rgbStream);
    return sensor;
}

void getSkeletonData() {
    NUI_SKELETON_FRAME sF = { 0 };
    if (sensor->NuiSkeletonGetNextFrame(0, &sF) >= 0) {
        sensor->NuiTransformSmooth(&sF, NULL);
        activeSkeletons = 0;
        int sk = NUI_SKELETON_COUNT;
        if (sk > 0) {
            int z = 0;
            const NUI_SKELETON_DATA& skeleton = sF.SkeletonData[z];
            if (skeleton.eTrackingState == NUI_SKELETON_TRACKED) {
                for (int i = 0; i < NUI_SKELETON_POSITION_COUNT; i++) {
                    skeletonPosition[i] = skeleton.SkeletonPositions[i];
                    skeletonPosition[i].z = skeletonPosition[i].z + 1;
                    if (skeleton.eSkeletonPositionTrackingState[i] == NUI_SKELETON_POSITION_NOT_TRACKED) {
                        skeletonPosition[i].w = -1;
                    }
                }
                activeSkeletons++;
            }
        }
        if (sk > 1) {
            int z = 1;
            const NUI_SKELETON_DATA& skeleton = sF.SkeletonData[z];
            if (skeleton.eTrackingState == NUI_SKELETON_TRACKED) {
                for (int i = 0; i < NUI_SKELETON_POSITION_COUNT; i++) {
                    skeletonPosition2[i] = skeleton.SkeletonPositions[i];
                    skeletonPosition2[i].z = skeletonPosition2[i].z + 1;
                    if (skeleton.eSkeletonPositionTrackingState[i] == NUI_SKELETON_POSITION_NOT_TRACKED) {
                        skeletonPosition2[i].w = -1;
                    }
                }
                activeSkeletons++;
            }
        }
    }
}

void getKinectDataColor(GLubyte* dest) {
    NUI_IMAGE_FRAME imageFrame;
    NUI_LOCKED_RECT LockedRect;
    if (sensor->NuiImageStreamGetNextFrame(rgbStream, 0, &imageFrame) < 0) return;
    INuiFrameTexture* texture = imageFrame.pFrameTexture;
    texture->LockRect(0, &LockedRect, NULL, 0);

    if (LockedRect.Pitch != 0)
    {
        const BYTE* curr = (const BYTE*)LockedRect.pBits;
        const BYTE* dataEnd = curr + (camW * camH) * 4;

        while (curr < dataEnd) {
            *dest++ = *curr++;
        }
    }

    texture->UnlockRect(0);
    sensor->NuiImageStreamReleaseFrame(rgbStream, &imageFrame);
}

void getKinectDataDepth(GLubyte* dest) {
    NUI_IMAGE_FRAME imageFrame;
    NUI_LOCKED_RECT LockedRect;
    if (sensor->NuiImageStreamGetNextFrame(depthStream, 0, &imageFrame) < 0) return;
    INuiFrameTexture* texture = imageFrame.pFrameTexture;
    texture->LockRect(0, &LockedRect, NULL, 0);

    if (LockedRect.Pitch != 0)
    {
        const USHORT* curr = (const USHORT*)LockedRect.pBits;
        const USHORT* dataEnd = curr + (camW * camH);

        while (curr < dataEnd) {
            // Get depth in millimeters
            USHORT depth = NuiDepthPixelToDepth(*curr++);

            // Draw a grayscale image of the depth:
            // B,G,R are all set to depth%256, alpha set to 1.
            for (int i = 0; i < 3; ++i)
                *dest++ = (BYTE)(depth % 256);
            *dest++ = 0xff;
        }
    }

    texture->UnlockRect(0);
    sensor->NuiImageStreamReleaseFrame(depthStream, &imageFrame);
}

void getKinectData(GLubyte* dest) {
#if defined(DISPLAYDEPTH)
    getKinectDataDepth(dest);
#else
    getKinectDataColor(dest);
#endif
    getSkeletonData();
}

void lineBetween(Vector4 start, Vector4 end) {
    glVertex3f(start.x, start.y, start.z);
    glVertex3f(end.x, end.y, end.z);
}

void drawSkeleton(Vector4 sp[NUI_SKELETON_POSITION_COUNT]) {
    const Vector4& lHand = sp[NUI_SKELETON_POSITION_HAND_LEFT];
    const Vector4& lElbow = sp[NUI_SKELETON_POSITION_ELBOW_LEFT];
    const Vector4& lShoulder = sp[NUI_SKELETON_POSITION_SHOULDER_LEFT];
    const Vector4& rHand = sp[NUI_SKELETON_POSITION_HAND_RIGHT];
    const Vector4& rElbow = sp[NUI_SKELETON_POSITION_ELBOW_RIGHT];
    const Vector4& rShoulder = sp[NUI_SKELETON_POSITION_SHOULDER_RIGHT];
    const Vector4& head = sp[NUI_SKELETON_POSITION_HEAD];
    const Vector4& neck = sp[NUI_SKELETON_POSITION_SHOULDER_CENTER];
    const Vector4& spine = sp[NUI_SKELETON_POSITION_SPINE];
    const Vector4& hip = sp[NUI_SKELETON_POSITION_HIP_CENTER];
    const Vector4& hipL = sp[NUI_SKELETON_POSITION_HIP_LEFT];
    const Vector4& hipR = sp[NUI_SKELETON_POSITION_HIP_RIGHT];
    const Vector4& kneeL = sp[NUI_SKELETON_POSITION_KNEE_LEFT];
    const Vector4& kneeR = sp[NUI_SKELETON_POSITION_KNEE_RIGHT];
    const Vector4& footL = sp[NUI_SKELETON_POSITION_FOOT_LEFT];
    const Vector4& footR = sp[NUI_SKELETON_POSITION_FOOT_RIGHT];

    glBegin(GL_LINES);
    glColor3f(1.f, 1.f, 1.f);
    if (lHand.w > 0 && lElbow.w > 0 && lShoulder.w > 0 && neck.w > 0) {
        lineBetween(lHand, lElbow);
        lineBetween(lElbow, lShoulder);
        lineBetween(lShoulder, neck);
    }
    if (rHand.w > 0 && rElbow.w > 0 && rShoulder.w > 0 && neck.w > 0) {
        lineBetween(rHand, rElbow);
        lineBetween(rElbow, rShoulder);
        lineBetween(rShoulder, neck);
    }
    if (head.w > 0 && neck.w > 0 && spine.w > 0 && hip.w > 0) {
        lineBetween(head, neck);
        lineBetween(neck, spine);
        lineBetween(spine, hip);
    }
    if (hip.w > 0 && hipL.w > 0 && kneeL.w > 0 && footL.w > 0) {
        lineBetween(hip, hipL);
        lineBetween(hipL, kneeL);
        lineBetween(kneeL, footL);
    }
    if (hip.w > 0 && hipR.w > 0 && kneeR.w > 0 && footR.w > 0) {
        lineBetween(hip, hipR);
        lineBetween(hipR, kneeR);
        lineBetween(kneeR, footR);
    }
    glColor3f(1.f, 1.f, 1.f);
    glEnd();
}

void drawKinectData() {
    // Initialize textures
    glGenTextures(1, &texID);
    glBindTexture(GL_TEXTURE_2D, texID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, camW, camH,
        0, GL_BGRA, GL_UNSIGNED_BYTE, (GLvoid*)data);
    glBindTexture(GL_TEXTURE_2D, 0);

    // OpenGL setup
    glClearColor(0, 0, 0, 0);
    glClearDepth(1.0f);
    glEnable(GL_TEXTURE_2D);

    // Camera setup
    glViewport(0, 0, 1280, 960);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-8 / 45.f, 8 / 45.f, -0.1, 0.1, 0.1, 100);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    getKinectData(data);
    /*
    glBindTexture(GL_TEXTURE_2D, texID);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, camW, camH, GL_BGRA, GL_UNSIGNED_BYTE, (GLvoid*)data);
    
    glClear(GL_DEPTH_BUFFER_BIT);
    glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f);
        glVertex3f(-camW/100, camH/100, -5.0);
        glTexCoord2f(1.0f, 0.0f);
        glVertex3f(camW/100, camH/100, -5.0);
        glTexCoord2f(1.0f, 1.0f);
        glVertex3f(camW/100, -camH/100, -5.0f);
        glTexCoord2f(0.0f, 1.0f);
        glVertex3f(-camW/100, -camH/100, -5.0f);
    glEnd();
    glBindTexture(GL_TEXTURE_2D, 0);
    */
    glClear(GL_DEPTH_BUFFER_BIT);

    if (activeSkeletons > 0) drawSkeleton(skeletonPosition);
    if (activeSkeletons > 1) drawSkeleton(skeletonPosition2);
}

json vertate(Vector4 vert) {
    json j;
    j["x"] = vert.x;
    j["y"] = vert.y;
    j["z"] = vert.z;
    return j;
}

json vertate(Vector4 root, glm::vec3 vert) {
    json j;
    j["x"] = root.x + vert.x;
    j["y"] = root.y + vert.y;
    j["z"] = root.z + vert.z;
    return j;
}

glm::vec3 rotate(glm::vec3 v, glm::vec3 k, float theta) {
    float sinTheta = sin(theta);
    float cosTheta = cos(theta);
    v = (v * cosTheta) + (glm::cross(k, v) * sinTheta) + (k * glm::dot(k, v)) * (1 - cosTheta);
    return v;
}

glm::vec3 cubevert(int n, glm::vec3 rotation, float scale) {
    const float cVerts[16][3] =
    {   {-1.f, -1.f, -1.f},
        {-1.f, -1.f, 1.f},
        {-1.f, 1.f, 1.f},
        {1.f, 1.f, 1.f},
        {1.f, 1.f, -1.f},
        {-1.f, 1.f, -1.f},
        {-1.f, -1.f, -1.f},
        {1.f, -1.f, -1.f},
        {1.f, -1.f, 1.f},
        {-1.f, -1.f, 1.f},
        {-1.f, 1.f, 1.f},
        {-1.f, 1.f, -1.f},
        {1.f, 1.f, -1.f},
        {1.f, -1.f, -1.f},
        {1.f, -1.f, 1.f},
        {1.f, 1.f, 1.f}
    };

    const glm::vec3 unitX = { 1.f, 0.f, 0.f };
    const glm::vec3 unitY = { 0.f, 1.f, 0.f };
    const glm::vec3 unitZ = { 0.f, 0.f, 1.f };

    glm::vec3 v = { cVerts[n][0] * scale, cVerts[n][1] * scale, cVerts[n][2] * scale };
    v = rotate(v, unitX, rotation.x);
    v = rotate(v, unitY, rotation.y);
    v = rotate(v, unitZ, rotation.z);
    return v;
}

json genCube(json j, Vector4 root, glm::vec3 cubeRotation, float scale) {
    scale = scale * 0.1;
    if (root.w > 0) {
        json stroke;
        stroke.push_back(vertate(root, cubevert(0, cubeRotation, scale)));
        stroke.push_back(vertate(root, cubevert(1, cubeRotation, scale)));
        stroke.push_back(vertate(root, cubevert(2, cubeRotation, scale)));
        stroke.push_back(vertate(root, cubevert(3, cubeRotation, scale)));
        stroke.push_back(vertate(root, cubevert(4, cubeRotation, scale)));
        stroke.push_back(vertate(root, cubevert(5, cubeRotation, scale)));
        stroke.push_back(vertate(root, cubevert(6, cubeRotation, scale)));
        stroke.push_back(vertate(root, cubevert(7, cubeRotation, scale)));
        stroke.push_back(vertate(root, cubevert(8, cubeRotation, scale)));
        stroke.push_back(vertate(root, cubevert(9, cubeRotation, scale)));
        stroke.push_back(vertate(root, cubevert(10, cubeRotation, scale)));
        stroke.push_back(vertate(root, cubevert(11, cubeRotation, scale)));
        stroke.push_back(vertate(root, cubevert(12, cubeRotation, scale)));
        stroke.push_back(vertate(root, cubevert(13, cubeRotation, scale)));
        stroke.push_back(vertate(root, cubevert(14, cubeRotation, scale)));
        stroke.push_back(vertate(root, cubevert(15, cubeRotation, scale)));
        j["vertices"].push_back(stroke);
    }
    return j;
}

json skeletate(json skelet, Vector4 sp[NUI_SKELETON_POSITION_COUNT]) {
    const Vector4& lHand = sp[NUI_SKELETON_POSITION_HAND_LEFT];
    const Vector4& lElbow = sp[NUI_SKELETON_POSITION_ELBOW_LEFT];
    const Vector4& lShoulder = sp[NUI_SKELETON_POSITION_SHOULDER_LEFT];
    const Vector4& rHand = sp[NUI_SKELETON_POSITION_HAND_RIGHT];
    const Vector4& rElbow = sp[NUI_SKELETON_POSITION_ELBOW_RIGHT];
    const Vector4& rShoulder = sp[NUI_SKELETON_POSITION_SHOULDER_RIGHT];
    const Vector4& head = sp[NUI_SKELETON_POSITION_HEAD];
    const Vector4& neck = sp[NUI_SKELETON_POSITION_SHOULDER_CENTER];
    const Vector4& spine = sp[NUI_SKELETON_POSITION_SPINE];
    const Vector4& hip = sp[NUI_SKELETON_POSITION_HIP_CENTER];
    const Vector4& hipL = sp[NUI_SKELETON_POSITION_HIP_LEFT];
    const Vector4& hipR = sp[NUI_SKELETON_POSITION_HIP_RIGHT];
    const Vector4& kneeL = sp[NUI_SKELETON_POSITION_KNEE_LEFT];
    const Vector4& kneeR = sp[NUI_SKELETON_POSITION_KNEE_RIGHT];
    const Vector4& footL = sp[NUI_SKELETON_POSITION_FOOT_LEFT];
    const Vector4& footR = sp[NUI_SKELETON_POSITION_FOOT_RIGHT];

    if (lHand.w > 0 && lElbow.w > 0 && lShoulder.w > 0 && neck.w > 0) {
        json stroke;
        stroke.push_back(vertate(lHand));
        stroke.push_back(vertate(lElbow));
        stroke.push_back(vertate(lShoulder));
        stroke.push_back(vertate(neck));
        skelet["vertices"].push_back(stroke);
    }
    if (rHand.w > 0 && rElbow.w > 0 && rShoulder.w > 0 && neck.w > 0) {
        json stroke;
        stroke.push_back(vertate(neck));
        stroke.push_back(vertate(rShoulder));
        stroke.push_back(vertate(rElbow));
        stroke.push_back(vertate(rHand));
        skelet["vertices"].push_back(stroke);
    }
    if (head.w > 0 && neck.w > 0 && spine.w > 0 && hip.w > 0) {
        json stroke;
        stroke.push_back(vertate(head));
        stroke.push_back(vertate(neck));
        stroke.push_back(vertate(spine));
        stroke.push_back(vertate(hip));
        skelet["vertices"].push_back(stroke);
    }
    if (hip.w > 0 && hipL.w > 0 && kneeL.w > 0 && footL.w > 0) {
        json stroke;
        stroke.push_back(vertate(footL));
        stroke.push_back(vertate(kneeL));
        stroke.push_back(vertate(hipL));
        stroke.push_back(vertate(hip));
        skelet["vertices"].push_back(stroke);
    }
    if (hip.w > 0 && hipR.w > 0 && kneeR.w > 0 && footR.w > 0) {
        json stroke;
        stroke.push_back(vertate(hip));
        stroke.push_back(vertate(hipR));
        stroke.push_back(vertate(kneeR));
        stroke.push_back(vertate(footR));
        skelet["vertices"].push_back(stroke);
    }
    if (headCube && head.w > 0) {
        skelet = genCube(skelet, head, headCubeRotation, 2);
    }
    if (handCube && lHand.w > 0) {
        skelet = genCube(skelet, lHand, handCubeRotationL, 1);
    }
    if (handCube && rHand.w > 0) {
        skelet = genCube(skelet, rHand, handCubeRotationR, 1);
    }
    if (footCube && footL.w > 0) {
        skelet = genCube(skelet, footL, footCubeRotationL, 1);
    }
    if (footCube && footR.w > 0) {
        skelet = genCube(skelet, footR, footCubeRotationR, 1);
    }
    return skelet;
}

void makeJson() {
    getSkeletonData();
    json newJson = skeletonJson;
    json skelet1, skelet2, matrix;

    matrix = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, -1, -1,
        0, 0, -1, 1 };

    skelet1["name"] = "Skeleton 1";
    skelet1 = skeletate(skelet1, skeletonPosition);
    skelet1["matrix"] = matrix;

    skelet2["name"] = "Skeleton 2";
    skelet2 = skeletate(skelet2, skeletonPosition2);
    skelet2["matrix"] = matrix;

    if (activeSkeletons > 0) {
        newJson = json();
        newJson["objects"].push_back(skelet1);
        if (activeSkeletons > 1) newJson["objects"].push_back(skelet2);
        newJson["focalLength"] = -2.5;
        skeletonJsonChanged = newJson != skeletonJson;
        skeletonJson = newJson;
    }
}

// Main code
int main(int, char**)
{
    int frameCounter = 0;

    int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        wprintf(L"WSAStartup Failed! %d\n", iResult);
        return 1;
    }

    struct addrinfo* result = NULL,
        * ptr = NULL,
        hints;

    ZeroMemory( &hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    iResult = getaddrinfo("localhost", "51677", &hints, &result);
    if (iResult != 0) {
        printf("getaddrinfo failed with error: %d\n", iResult);
        WSACleanup();
        return 1;
    }

    // Attempt to connect to an address until one succeeds
    for (ptr = result; ptr != NULL; ptr = ptr->ai_next) {

        // Create a SOCKET for connecting to server
        orsock = socket(ptr->ai_family, ptr->ai_socktype,
            ptr->ai_protocol);
        if (orsock == INVALID_SOCKET) {
            printf("socket failed with error: %ld\n", WSAGetLastError());
            WSACleanup();
            return 1;
        }

        // Connect to server.
        iResult = connect(orsock, ptr->ai_addr, (int)ptr->ai_addrlen);
        if (iResult == SOCKET_ERROR) {
            closesocket(orsock);
            orsock = INVALID_SOCKET;
            continue;
        }
        break;
    }

    freeaddrinfo(result);

    if (orsock == INVALID_SOCKET) {
        printf("Unable to connect to server!\n");
        WSACleanup();
        return 1;
    }

    // Initialize Kinect
    initKinect();

    // Setup SDL
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
    {
        printf("Error: SDL_Init(): %s\n", SDL_GetError());
        return -1;
    }

    // GL 3.0 + GLSL 130
    const char* glsl_version = "#version 130";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

    // Create window with graphics context
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    Uint32 window_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN;
    SDL_Window* window = SDL_CreateWindow("Dear ImGui SDL3+OpenGL3 example", 1280, 960, window_flags);
    if (window == nullptr)
    {
        printf("Error: SDL_CreateWindow(): %s\n", SDL_GetError());
        return -1;
    }
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1); // Enable vsync
    SDL_ShowWindow(window);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();

    // Setup Platform/Renderer backends
    ImGui_ImplSDL3_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Our state
    ImVec4 clear_color = ImVec4(0.f, 0.f, 0.f, 1.00f);
    float headCSpeedX = 0.0321;
    float headCSpeedY = 0.0321;
    float headCSpeedZ = 0.0321;

    float handCSpeedX = 0.0213;
    float handCSpeedY = 0.0213;
    float handCSpeedZ = 0.0213;

    float footCSpeedX = 0.0123;
    float footCSpeedY = 0.0123;
    float footCSpeedZ = 0.0123;

    // Main loop
    bool done = false;
    while (!done)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT)
                done = true;
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window))
                done = true;
        }
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)
        {
            SDL_Delay(10);
            continue;
        }

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        // Show the config window
        {
            static float f = 0.0f;
            static bool counter = 0;

            ImGui::Begin("Connection Manager");                          // Create a window called "Hello, world!" and append into it.

            ImGui::Text("Connected to osci-render");               // Display some text (you can use a format strings too)

            ImGui::Checkbox("Head Cube", &headCube);
            ImGui::Checkbox("Hand Cubes", &handCube);
            ImGui::Checkbox("Foot Cubes", &footCube);

            ImGui::Text("Skeletons Tracked: %d", activeSkeletons);
            ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
            ImGui::End();
        }

        // Rendering
        ImGui::Render();
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
        glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        // Draw Kinect Data
        drawKinectData();
        const int divider = 4;
        frameCounter = (frameCounter + 1) % divider;
        if (frameCounter == 0) {
            headCubeRotation = headCubeRotation + glm::vec3({ headCSpeedX * divider, headCSpeedY * divider, headCSpeedZ * divider });

            handCubeRotationL = handCubeRotationL + glm::vec3({ handCSpeedX * divider, handCSpeedY * divider, handCSpeedZ * divider });
            handCubeRotationR = handCubeRotationR + glm::vec3({ -handCSpeedX * divider, -handCSpeedY * divider, -handCSpeedZ * divider });

            footCubeRotationL = footCubeRotationL + glm::vec3({ footCSpeedX * divider, footCSpeedY * divider, footCSpeedZ * divider });
            footCubeRotationR = footCubeRotationR + glm::vec3({ -footCSpeedX * divider, -footCSpeedY * divider, -footCSpeedZ * divider });

            makeJson();
            if (skeletonJsonChanged) {
                std::cout << skeletonJson << std::endl;
                std::string j = skeletonJson.dump();
                iResult = send(orsock, j.c_str(), j.length(), 0);
                if (iResult == SOCKET_ERROR) {
                    wprintf(L"Sending data failed! code %d\n", iResult);
                    closesocket(orsock);
                    WSACleanup();
                    return 1;
                }
            }
        }
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    std::string close = "CLOSE\n";
    send(orsock, close.c_str(), close.length(), 0);
    shutdown(orsock, 2);
    iResult = closesocket(orsock);
    if (iResult == SOCKET_ERROR) {
        wprintf(L"closesocket failed with error = %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }
    WSACleanup();

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DestroyContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
