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
#include <SDL3/SDL_opengl.h>


#include <Windows.h>
#include <Ole2.h>
#include <NuiApi.h>

#include <glm/glm.hpp>

#include <winsock2.h>
#include <ws2tcpip.h>
#include "json.hpp"

using json = nlohmann::json;
json skeletonJson;
bool skeletonJsonChanged = false;

bool headCube = true;
bool headIco = true;
bool handCube = false;
bool footCube = false;
bool kinectConnected = false;

float slide = 3;

glm::vec3 headCubeRotation = { 0,0,0 };
glm::vec3 headIcoRotation = { 0,0,0 };

glm::vec3 handCubeRotationL = { 0,0,0 };
glm::vec3 handCubeRotationR = { 0,0,0 };

glm::vec3 footCubeRotationL = { 0,0,0 };
glm::vec3 footCubeRotationR = { 0,0,0 };

WSADATA wsaData = { 0 };
SOCKET orsock = INVALID_SOCKET;

#define camW 640
#define camH 480

GLuint texIDD;
GLuint texIDC;
GLubyte dataD[camW * camH * 4];
GLubyte dataC[camW * camH * 4];

HANDLE rgbStream;
HANDLE depthStream;
INuiSensor* sensor;

int activeSkeletons = 0;
Vector4 skeletonPosition[NUI_SKELETON_POSITION_COUNT];
Vector4 skeletonPosition2[NUI_SKELETON_POSITION_COUNT];

void SDLCleanup(SDL_GLContext gl_context, SDL_Window* window) {
    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DestroyContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

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
                    skeletonPosition[i].z = skeletonPosition[i].z + slide;
                    if (skeleton.eSkeletonPositionTrackingState[i] == NUI_SKELETON_POSITION_NOT_TRACKED) {
                        skeletonPosition[i].w = -1;
                    }
                    else skeletonPosition[i].w = 1;
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
                    skeletonPosition2[i].z = skeletonPosition2[i].z + slide;
                    if (skeleton.eSkeletonPositionTrackingState[i] == NUI_SKELETON_POSITION_NOT_TRACKED) {
                        skeletonPosition2[i].w = -1;
                    }
                    else skeletonPosition2[i].w = 1;
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

void getKinectData(GLubyte* destD, GLubyte* destC) {
    if (!kinectConnected) return;
    getKinectDataDepth(destD);
    getKinectDataColor(destC);
    getSkeletonData();
}

void lineBetween(Vector4 start, Vector4 end) {
    glVertex3f(start.x, start.y, -start.z);
    glVertex3f(end.x, end.y, -end.z);
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
    glEnd();
}

void drawKinectData() {
    if (!kinectConnected) return;
    // OpenGL setup
    glClearColor(0, 0, 0, 0);
    glClearDepth(1.0f);
    glEnable(GL_TEXTURE_2D);

    // Depth Viewport
    glBindTexture(GL_TEXTURE_2D, texIDD);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, camW, camH,
        GL_BGRA, GL_UNSIGNED_BYTE, (GLvoid*)dataD);

    glViewport(0, 0, 640, 480);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, camW, camH, 0, 1, -1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glClear(GL_DEPTH_BUFFER_BIT);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f);
    glVertex3f(0, 0, 0);
    glTexCoord2f(1.0f, 0.0f);
    glVertex3f(camW, 0, 0);
    glTexCoord2f(1.0f, 1.0f);
    glVertex3f(camW, camH, 0.0f);
    glTexCoord2f(0.0f, 1.0f);
    glVertex3f(0, camH, 0.0f);
    glEnd();
    glBindTexture(GL_TEXTURE_2D, 0);

    // Color Viewport
    glBindTexture(GL_TEXTURE_2D, texIDC);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, camW, camH,
        GL_BGRA, GL_UNSIGNED_BYTE, (GLvoid*)dataC);

    glViewport(640, 0, 640, 480);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, camW, camH, 0, 1, -1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glClear(GL_DEPTH_BUFFER_BIT);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f);
    glVertex3f(0, 0, 0);
    glTexCoord2f(1.0f, 0.0f);
    glVertex3f(camW, 0, 0);
    glTexCoord2f(1.0f, 1.0f);
    glVertex3f(camW, camH, 0.0f);
    glTexCoord2f(0.0f, 1.0f);
    glVertex3f(0, camH, 0.0f);
    glEnd();
    glBindTexture(GL_TEXTURE_2D, 0);

    // Skeleton Viewport
    glViewport(640, 480, 640, 480);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-4.f/30, 4.f/30, -0.1, 0.1, 0.3, 100);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glClear(GL_DEPTH_BUFFER_BIT);

    glColor3f(0.f, 1.f, 0.f);
    if (activeSkeletons > 0) drawSkeleton(skeletonPosition);
    glColor3f(0.f, 0.f, 1.f);
    if (activeSkeletons > 1) drawSkeleton(skeletonPosition2);
    glColor3f(1.f, 1.f, 1.f);
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

glm::vec3 icovert(int n, int stroke, glm::vec3 rotation, float scale) {
    const float stroke1[2][3] = { { -0.4472150206565857, -0.5257200002670288, -0.7235999703407288 },{ 0.4472149908542633,-0.8506399989128113,-0.27638503909111023 } };
    const float stroke2[4][3] = { {0.4472149908542633,-0.8506399989128113,-0.27638503909111023},{-0.4472149908542633,-0.8506399989128113,0.27638503909111023},{-0.4472149610519409,0.0,0.8944249749183655},{-1.0,0.0,4.371138828673793e-08} };
    const float stroke3[2][3] = { {-1.0,0.0,4.371138828673793e-08},{-0.4472149908542633,0.8506399989128113,0.27638503909111023} };
    const float stroke4[4][3] = { {-0.4472149908542633,0.8506399989128113,0.27638503909111023},{-0.4472150206565857,0.5257200002670288,-0.7235999703407288},{0.4472149610519409,0.0,-0.8944249749183655},{-0.4472150206565857,-0.5257200002670288,-0.7235999703407288} };
    const float stroke5[2][3] = { {-0.4472150206565857,-0.5257200002670288,-0.7235999703407288},{-0.4472150206565857,0.5257200002670288,-0.7235999703407288} };
    const float stroke6[4][3] = { {-0.4472150206565857,0.5257200002670288,-0.7235999703407288},{-1.0,0.0,4.371138828673793e-08},{-0.4472150206565857,-0.5257200002670288,-0.7235999703407288},{-0.4472149908542633,-0.8506399989128113,0.27638503909111023} };
    const float stroke7[2][3] = { {-0.4472149908542633,-0.8506399989128113,0.27638503909111023},{-1.0,0.0,4.371138828673793e-08} };
    const float stroke8[2][3] = { {-0.4472149908542633,-0.8506399989128113,0.27638503909111023},{0.4472150206565857,-0.5257200002670288,0.7235999703407288} };
    const float stroke9[4][3] = { {0.4472150206565857,-0.5257200002670288,0.7235999703407288},{-0.4472149610519409,0.0,0.8944249749183655},{-0.4472149908542633,0.8506399989128113,0.27638503909111023},{0.4472150206565857,0.5257200002670288,0.7235999703407288} };
    const float stroke10[2][3] = { {0.4472150206565857,0.5257200002670288,0.7235999703407288},{-0.4472149610519409,0.0,0.8944249749183655} };
    const float stroke11[2][3] = { {-0.4472149908542633,0.8506399989128113,0.27638503909111023},{0.4472149908542633,0.8506399989128113,-0.27638503909111023} };
    const float stroke12[2][3] = { {0.4472149908542633,0.8506399989128113,-0.27638503909111023},{-0.4472150206565857,0.5257200002670288,-0.7235999703407288} };
    const float stroke13[2][3] = { {0.4472149610519409,0.0,-0.8944249749183655},{1.0,0.0,-4.371138828673793e-08} };
    const float stroke14[2][3] = { {1.0,0.0,-4.371138828673793e-08},{0.4472149908542633,-0.8506399989128113,-0.27638503909111023} };
    const float stroke15[4][3] = { {0.4472149908542633,-0.8506399989128113,-0.27638503909111023},{0.4472150206565857,-0.5257200002670288,0.7235999703407288},{0.4472150206565857,0.5257200002670288,0.7235999703407288},{0.4472149908542633,0.8506399989128113,-0.27638503909111023} };
    const float stroke16[3][3] = { {0.4472149908542633,0.8506399989128113,-0.27638503909111023},{0.4472149610519409,0.0,-0.8944249749183655},{0.4472149908542633,-0.8506399989128113,-0.27638503909111023} };
    const float stroke17[3][3] = { {0.4472150206565857,0.5257200002670288,0.7235999703407288},{1.0,0.0,-4.371138828673793e-08},{0.4472150206565857,-0.5257200002670288,0.7235999703407288} };
    const float stroke18[2][3] = { {1.0,0.0,-4.371138828673793e-08},{0.4472149908542633,0.8506399989128113,-0.27638503909111023} };

    glm::vec3 v = { 0,0,0 };

    switch (stroke) {
    case 0:
        v = { stroke1[n][0], stroke1[n][1], stroke1[n][2]};
        break;
    case 1:
        v = { stroke2[n][0], stroke2[n][1], stroke2[n][2]};
        break;
    case 2:
        v = { stroke3[n][0], stroke3[n][1], stroke3[n][2]};
        break;
    case 3:
        v = { stroke4[n][0], stroke4[n][1], stroke4[n][2]};
        break;
    case 4:
        v = { stroke5[n][0], stroke5[n][1], stroke5[n][2]};
        break;
    case 5:
        v = { stroke6[n][0], stroke6[n][1], stroke6[n][2]};
        break;
    case 6:
        v = { stroke7[n][0], stroke7[n][1], stroke7[n][2]};
        break;
    case 7:
        v = { stroke8[n][0], stroke8[n][1], stroke8[n][2]};
        break;
    case 8:
        v = { stroke9[n][0], stroke9[n][1], stroke9[n][2]};
        break;
    case 9:
        v = { stroke10[n][0], stroke10[n][1], stroke10[n][2]};
        break;
    case 10:
        v = { stroke11[n][0], stroke11[n][1], stroke11[n][2]};
        break;
    case 11:
        v = { stroke12[n][0], stroke12[n][1], stroke12[n][2]};
        break;
    case 12:
        v = { stroke13[n][0], stroke13[n][1], stroke13[n][2]};
        break;
    case 13:
        v = { stroke14[n][0], stroke14[n][1], stroke14[n][2]};
        break;
    case 14:
        v = { stroke15[n][0], stroke15[n][1], stroke15[n][2]};
        break;
    case 15:
        v = { stroke16[n][0], stroke16[n][1], stroke16[n][2]};
        break;
    case 16:
        v = { stroke17[n][0], stroke17[n][1], stroke17[n][2]};
        break;
    case 17:
        v = { stroke18[n][0], stroke18[n][1], stroke18[n][2]};
        break;
    }

    const glm::vec3 unitX = { 1.f, 0.f, 0.f };
    const glm::vec3 unitY = { 0.f, 1.f, 0.f };
    const glm::vec3 unitZ = { 0.f, 0.f, 1.f };

    v = v * scale;
    v = rotate(v, unitX, rotation.x);
    v = rotate(v, unitY, rotation.y);
    v = rotate(v, unitZ, rotation.z);
    return v;
}

json genIco(json j, Vector4 root, glm::vec3 rotation, float scale) {
    scale = scale * 0.1;
    json stroke;
    int k = 0;

    {
        for (int i = 0; i < 2; i++) stroke.push_back(vertate(root, icovert(i, k, rotation, scale)));
        j["vertices"].push_back(stroke);
        stroke = json();
        k++;

        for (int i = 0; i < 4; i++) stroke.push_back(vertate(root, icovert(i, k, rotation, scale)));
        j["vertices"].push_back(stroke);
        stroke = json();
        k++;

        for (int i = 0; i < 2; i++) stroke.push_back(vertate(root, icovert(i, k, rotation, scale)));
        j["vertices"].push_back(stroke);
        stroke = json();
        k++;

        for (int i = 0; i < 4; i++) stroke.push_back(vertate(root, icovert(i, k, rotation, scale)));
        j["vertices"].push_back(stroke);
        stroke = json();
        k++;

        for (int i = 0; i < 2; i++) stroke.push_back(vertate(root, icovert(i, k, rotation, scale)));
        j["vertices"].push_back(stroke);
        stroke = json();
        k++;

        for (int i = 0; i < 4; i++) stroke.push_back(vertate(root, icovert(i, k, rotation, scale)));
        j["vertices"].push_back(stroke);
        stroke = json();
        k++;

        for (int i = 0; i < 2; i++) stroke.push_back(vertate(root, icovert(i, k, rotation, scale)));
        j["vertices"].push_back(stroke);
        stroke = json();
        k++;

        for (int i = 0; i < 2; i++) stroke.push_back(vertate(root, icovert(i, k, rotation, scale)));
        j["vertices"].push_back(stroke);
        stroke = json();
        k++;

        for (int i = 0; i < 4; i++) stroke.push_back(vertate(root, icovert(i, k, rotation, scale)));
        j["vertices"].push_back(stroke);
        stroke = json();
        k++;

        for (int i = 0; i < 2; i++) stroke.push_back(vertate(root, icovert(i, k, rotation, scale)));
        j["vertices"].push_back(stroke);
        stroke = json();
        k++;

        for (int i = 0; i < 2; i++) stroke.push_back(vertate(root, icovert(i, k, rotation, scale)));
        j["vertices"].push_back(stroke);
        stroke = json();
        k++;

        for (int i = 0; i < 2; i++) stroke.push_back(vertate(root, icovert(i, k, rotation, scale)));
        j["vertices"].push_back(stroke);
        stroke = json();
        k++;

        for (int i = 0; i < 2; i++) stroke.push_back(vertate(root, icovert(i, k, rotation, scale)));
        j["vertices"].push_back(stroke);
        stroke = json();
        k++;

        for (int i = 0; i < 2; i++) stroke.push_back(vertate(root, icovert(i, k, rotation, scale)));
        j["vertices"].push_back(stroke);
        stroke = json();
        k++;

        for (int i = 0; i < 4; i++) stroke.push_back(vertate(root, icovert(i, k, rotation, scale)));
        j["vertices"].push_back(stroke);
        stroke = json();
        k++;

        for (int i = 0; i < 3; i++) stroke.push_back(vertate(root, icovert(i, k, rotation, scale)));
        j["vertices"].push_back(stroke);
        stroke = json();
        k++;

        for (int i = 0; i < 3; i++) stroke.push_back(vertate(root, icovert(i, k, rotation, scale)));
        j["vertices"].push_back(stroke);
        stroke = json();
        k++;

        for (int i = 0; i < 2; i++) stroke.push_back(vertate(root, icovert(i, k, rotation, scale)));
        j["vertices"].push_back(stroke);
        stroke = json();
    }

    return j;
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

    glm::vec3 v = { cVerts[n][0], cVerts[n][1], cVerts[n][2] };
    v = v * scale;
    v = rotate(v, unitX, rotation.x);
    v = rotate(v, unitY, rotation.y);
    v = rotate(v, unitZ, rotation.z);
    return v;
}

json genCube(json j, Vector4 root, glm::vec3 cubeRotation, float scale) {
    scale = scale * 0.1;
    json stroke;
    for (int i = 0; i < 16; i++) stroke.push_back(vertate(root, cubevert(i, cubeRotation, scale)));
    j["vertices"].push_back(stroke);
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
    if (headIco && head.w > 0) {
        skelet = genIco(skelet, head, headIcoRotation, 1.5);
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
    json newJson = skeletonJson;
    json skelet1, skelet2, ico, matrix;

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

    newJson = json();
    if (activeSkeletons > 0) {
        newJson["objects"].push_back(skelet1);
        if (activeSkeletons > 1) newJson["objects"].push_back(skelet2);
    }
    skeletonJsonChanged = newJson != skeletonJson;
    newJson["focalLength"] = -2.5;
    skeletonJson = newJson;
}

int sendOsciRender() {
    if (activeSkeletons == 0) {
        std::string j = "{\"objects\": [{\"name\":\"Line Art\", \"vertices\" : [[{\"x\":-0.5, \"y\" : -0.5, \"z\" : 8.610005378723145}, {\"x\":0.5,\"y\" : -0.5,\"z\" : 8.610005378723145}, {\"x\":0.5,\"y\" : 0.5,\"z\" : 8.610005378723145}, {\"x\":-0.5,\"y\" : 0.5,\"z\" : 8.610005378723145}, {\"x\":-0.5,\"y\" : -0.5,\"z\" : 8.610005378723145}]], \"matrix\" : [1.1111111640930176, 0.0, 0.0, 0.0, 0.0, 1.1111111640930176, 0.0, 0.0, 0.0, 0.0, 1.1111111640930176, -11.111111640930176, 0.0, 0.0, 0.0, 1.0] }] , \"focalLength\" : -2.5}";
        int iResult = send(orsock, j.c_str(), j.length(), 0);
        if (iResult == SOCKET_ERROR) {
            return 1;
        }
    }
    else if (skeletonJsonChanged) {
        std::string j = skeletonJson.dump();
        std::cout << "JSON Sending" << std::endl;
        int iResult = send(orsock, j.c_str(), j.length(), 0);
        if (iResult == SOCKET_ERROR) {
            return 1;
        }
    }
    return 0;
}

// Main code
int main(int, char**)
{
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

    // Setup SDL
    if (!SDL_Init(SDL_INIT_VIDEO))
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

    // Initialize Kinect
    kinectConnected = initKinect();

    // Our state
    ImVec4 clear_color = ImVec4(0.f, 0.f, 0.f, 1.00f);
    float headCSpeedX = 3.21;
    float headCSpeedY = 3.21;
    float headCSpeedZ = 3.21;

    float headISpeedX = -3.21/2;
    float headISpeedY = -3.21/2;
    float headISpeedZ = -3.21/2;

    float handCSpeedX = 2.13;
    float handCSpeedY = 2.13;
    float handCSpeedZ = 2.13;

    float footCSpeedX = 2.23;
    float footCSpeedY = 2.23;
    float footCSpeedZ = 2.23;

    // Initialize textures
    glGenTextures(1, &texIDD);
    glBindTexture(GL_TEXTURE_2D, texIDD);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, camW, camH,
        0, GL_BGRA, GL_UNSIGNED_BYTE, (GLvoid*)dataD);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenTextures(1, &texIDC);
    glBindTexture(GL_TEXTURE_2D, texIDC);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, camW, camH,
        0, GL_BGRA, GL_UNSIGNED_BYTE, (GLvoid*)dataC);
    glBindTexture(GL_TEXTURE_2D, 0);

    int frameCounter = 0;
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

        const static int divider = 4;
        float delt = io.DeltaTime * divider;
        frameCounter = (frameCounter + 1) % divider;

        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)
        {
            getKinectData(dataD, dataC);
            if (frameCounter == 0) {
                headCubeRotation = headCubeRotation + glm::vec3({ headCSpeedX * delt, headCSpeedY * delt, headCSpeedZ * delt });
                headIcoRotation = headIcoRotation + glm::vec3({ headISpeedX * delt, headISpeedY * delt, headISpeedZ * delt });

                handCubeRotationL = handCubeRotationL + glm::vec3({ handCSpeedX * delt, handCSpeedY * delt, handCSpeedZ * delt });
                handCubeRotationR = handCubeRotationR + glm::vec3({ -handCSpeedX * delt, -handCSpeedY * delt, -handCSpeedZ * delt });

                footCubeRotationL = footCubeRotationL + glm::vec3({ footCSpeedX * delt, footCSpeedY * delt, footCSpeedZ * delt });
                footCubeRotationR = footCubeRotationR + glm::vec3({ -footCSpeedX * delt, -footCSpeedY * delt, -footCSpeedZ * delt });

                makeJson();
                iResult = sendOsciRender();
                if (iResult != 0) {
                    wprintf(L"Sending data failed! code %d\n", iResult);
                    closesocket(orsock);
                    WSACleanup();
                    SDLCleanup(gl_context, window);
                    return 1;
                }
            }
            SDL_Delay(10);
            continue;
        }

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        // Show the config window
        {
            ImGui::Begin("Connection Manager");

            ImGui::Text("Connected to osci-render");

            ImGui::Checkbox("Head Cube", &headCube);
            ImGui::Checkbox("Hand Cubes", &handCube);
            ImGui::Checkbox("Foot Cubes", &footCube);

            ImGui::SliderFloat("Skeleton Z Distance", &slide, -5, 5);

            ImGui::SetWindowFontScale(1.5);
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
        getKinectData(dataD, dataC);
        drawKinectData();

        if (frameCounter == 0) {
            headCubeRotation = headCubeRotation + glm::vec3({ headCSpeedX * delt, headCSpeedY * delt, headCSpeedZ * delt });
            headIcoRotation = headIcoRotation + glm::vec3({ headISpeedX * delt, headISpeedY * delt, headISpeedZ * delt });

            handCubeRotationL = handCubeRotationL + glm::vec3({ handCSpeedX * delt, handCSpeedY * delt, handCSpeedZ * delt });
            handCubeRotationR = handCubeRotationR + glm::vec3({ -handCSpeedX * delt, -handCSpeedY * delt, -handCSpeedZ * delt });

            footCubeRotationL = footCubeRotationL + glm::vec3({ footCSpeedX * delt, footCSpeedY * delt, footCSpeedZ * delt });
            footCubeRotationR = footCubeRotationR + glm::vec3({ -footCSpeedX * delt, -footCSpeedY * delt, -footCSpeedZ * delt });

            makeJson();
            iResult = sendOsciRender();
            if (iResult != 0) {
                wprintf(L"Sending data failed! code %d\n", iResult);
                closesocket(orsock);
                WSACleanup();
                SDLCleanup(gl_context, window);
                return 1;
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
        SDLCleanup(gl_context, window);
        return 1;
    }
    WSACleanup();

    SDLCleanup(gl_context, window);

    return 0;
}
