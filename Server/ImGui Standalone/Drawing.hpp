#define _CRT_SECURE_NO_WARNINGS
#define STB_IMAGE_IMPLEMENTATION

#include <winsock2.h>
#include <Windows.h>
#include <d3d11.h>
#include <tchar.h>
#include <map>
#include <thread>
#include <mutex>
#include <vector>
#include <string>
#include <algorithm>
#include <iostream>
#include "ImGui/imgui.h"
#include "ImGui/imgui_impl_dx11.h"
#include "ImGui/imgui_impl_win32.h"
#include "stb_image.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "Ws2_32.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

ID3D11Device* pd3dDevice = nullptr;
ID3D11DeviceContext* pd3dDeviceContext = nullptr;
IDXGISwapChain* pSwapChain = nullptr;
ID3D11RenderTargetView* pMainRenderTargetView = nullptr;
ImVec2 winSize = { 400, 300 };
bool bDraw = true;

bool LoadTextureFromMemory(const void* data, size_t data_size, ID3D11ShaderResourceView** out_srv, int* out_width, int* out_height) {
    int image_width = 0, image_height = 0;
    unsigned char* image_data = stbi_load_from_memory((const unsigned char*)data, (int)data_size, &image_width, &image_height, NULL, 4);
    if (image_data == NULL)
        return false;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = image_width;
    desc.Height = image_height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA subResource = {};
    subResource.pSysMem = image_data;
    subResource.SysMemPitch = image_width * 4;

    ID3D11Texture2D* pTexture = nullptr;
    if (FAILED(pd3dDevice->CreateTexture2D(&desc, &subResource, &pTexture))) {
        stbi_image_free(image_data);
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    if (FAILED(pd3dDevice->CreateShaderResourceView(pTexture, &srvDesc, out_srv))) {
        pTexture->Release();
        stbi_image_free(image_data);
        return false;
    }

    pTexture->Release();
    stbi_image_free(image_data);

    *out_width = image_width;
    *out_height = image_height;

    return true;
}

struct ClientInfo {
    SOCKET sock;
    std::string name;
    bool viewingScreen = false;
    std::vector<unsigned char> lastImage;
    std::mutex imageMutex;
    ID3D11ShaderResourceView* texture = nullptr;
    int width = 0;
    int height = 0;

    ClientInfo(const ClientInfo&) = delete;
    ClientInfo& operator=(const ClientInfo&) = delete;

    ClientInfo(ClientInfo&& other) noexcept { *this = std::move(other); }

    ClientInfo& operator=(ClientInfo&& other) noexcept {
        sock = other.sock;
        name = std::move(other.name);
        viewingScreen = other.viewingScreen;
        lastImage = std::move(other.lastImage);
        texture = other.texture;
        other.texture = nullptr;
        return *this;
    }

    ClientInfo() = default;

    ~ClientInfo() {
        if (texture) texture->Release();
    }
};

static std::vector<ClientInfo> g_clients;
static std::mutex g_clientsMutex;

void SendToClient(SOCKET sock, const std::string& msg) {
    send(sock, msg.c_str(), (int)msg.size(), 0);
}

void startServer() {
    std::thread([]() {
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);

        SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in service = {};
        service.sin_family = AF_INET;
        service.sin_addr.s_addr = INADDR_ANY;
        service.sin_port = htons(59819);

        bind(listenSock, (SOCKADDR*)&service, sizeof(service));
        listen(listenSock, SOMAXCONN);

        while (true) {
            SOCKET clientSock = accept(listenSock, NULL, NULL);
            if (clientSock == INVALID_SOCKET) continue;

            std::thread([clientSock]() {
                char buf[256];
                int ret = recv(clientSock, buf, sizeof(buf) - 1, 0);
                if (ret <= 0) {
                    closesocket(clientSock);
                    return;
                }
                buf[ret] = '\0';

                ClientInfo client;
                client.sock = clientSock;
                client.name = buf;

                {
                    std::lock_guard<std::mutex> lock(g_clientsMutex);
                    g_clients.push_back(std::move(client));
                }

                while (true) {
                    int size = 0;
                    int received = recv(clientSock, (char*)&size, sizeof(int), 0);
                    if (received != sizeof(int) || size <= 0 || size > 10 * 1024 * 1024) break;

                    std::vector<unsigned char> buffer(size);
                    int total = 0;
                    while (total < size) {
                        int r = recv(clientSock, (char*)buffer.data() + total, size - total, 0);
                        if (r <= 0) break;
                        total += r;
                    }

                    if (total != size) break;

                    std::lock_guard<std::mutex> lock(g_clientsMutex);
                    auto it = std::find_if(g_clients.begin(), g_clients.end(),
                        [&](const ClientInfo& ci) { return ci.sock == clientSock; });
                    if (it != g_clients.end()) {
                        std::lock_guard<std::mutex> imgLock(it->imageMutex);
                        it->lastImage = std::move(buffer);

                        if (it->texture) {
                            it->texture->Release();
                            it->texture = nullptr;
                        }

                        LoadTextureFromMemory(it->lastImage.data(), it->lastImage.size(), &it->texture, &it->width, &it->height);
                    }
                }

                {
                    std::lock_guard<std::mutex> lock(g_clientsMutex);
                    auto it = std::remove_if(g_clients.begin(), g_clients.end(),
                        [&](const ClientInfo& ci) { return ci.sock == clientSock; });
                    g_clients.erase(it, g_clients.end());
                }

                closesocket(clientSock);
                }).detach();
        }

        WSACleanup();
        }).detach();
}

void Draw() {
    ImGui::SetNextWindowSize(winSize, ImGuiCond_Once);
    ImGui::Begin("Clientes Conectados", &bDraw);
    {
        std::lock_guard<std::mutex> lock(g_clientsMutex);
        for (auto& ci : g_clients) {
            ImGui::Text("%s", ci.name.c_str());
            ImGui::SameLine();

            std::string btnLabel = ci.viewingScreen ? "Parar Screen View" : "Screen View";
            if (ImGui::Button((btnLabel + "##" + ci.name).c_str())) {
                ci.viewingScreen = !ci.viewingScreen;
                SendToClient(ci.sock, ci.viewingScreen ? "START_SCREEN" : "STOP_SCREEN");
                if (!ci.viewingScreen) {
                    std::lock_guard<std::mutex> imgLock(ci.imageMutex);
                    if (ci.texture) {
                        ci.texture->Release();
                        ci.texture = nullptr;
                    }
                    ci.lastImage.clear();
                }
            }

            if (ci.viewingScreen && ci.texture) {
                ImGui::Image(ci.texture, ImVec2(ci.width / 2, ci.height / 2));
            }
        }
    }
    ImGui::End();
}
