#include "UI.hpp"

void AlocarConsole() 
{
    AllocConsole();
    FILE* fp_out;
    freopen_s(&fp_out, "CONOUT$", "w", stdout);
    std::cout.clear();
    FILE* fp_in;
    freopen_s(&fp_in, "CONIN$", "r", stdin);
    std::cin.clear();
    FILE* fp_err;
    freopen_s(&fp_err, "CONOUT$", "w", stderr);
    std::cerr.clear();
    std::clog.clear();
}

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nShowCmd)
{
    AlocarConsole();

    startServer();
    Render();

    return 0;
}
