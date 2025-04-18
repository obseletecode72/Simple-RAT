#include "UI.hpp"

void AlocarConsole() {
    // Aloca o console
    AllocConsole();

    // Redireciona std::cout para o console
    FILE* fp_out;
    freopen_s(&fp_out, "CONOUT$", "w", stdout);
    std::cout.clear();

    // Redireciona std::cin para o console
    FILE* fp_in;
    freopen_s(&fp_in, "CONIN$", "r", stdin);
    std::cin.clear();

    // (Opcional) Redireciona std::cerr e std::clog também
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