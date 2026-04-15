#include <windows.h>
#include <spdlog/spdlog.h>
#include "context.h"

ID3D11Device*           g_device           = nullptr;
ID3D11DeviceContext*    g_context          = nullptr;
IDXGISwapChain*         g_swapChain        = nullptr;
ID3D11RenderTargetView* g_renderTargetView = nullptr;
ContextUPtr             g_appContext;

uint32_t g_width  = WINDOW_WIDTH;
uint32_t g_height = WINDOW_HEIGHT;

void RenderFrame();

void Resize(UINT width, UINT height) {
    if (!g_device || !g_context || !g_swapChain || width == 0 || height == 0) return;

    g_context->OMSetRenderTargets(0, nullptr, nullptr);
    if (g_renderTargetView) { g_renderTargetView->Release(); g_renderTargetView = nullptr; }

    g_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);

    ID3D11Texture2D* backBuffer = nullptr;
    g_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
    g_device->CreateRenderTargetView(backBuffer, nullptr, &g_renderTargetView);
    backBuffer->Release();

    if (g_appContext)
        g_appContext->OnResize(g_device, width, height);

    SPDLOG_INFO("Window Resized: {}x{}", width, height);
}

LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    static bool  isRightMouseDown = false;
    static POINT lastMousePos;
    static bool  isInternalMove   = false;

    switch (message) {
    case WM_RBUTTONDOWN:
        isRightMouseDown = true;
        GetCursorPos(&lastMousePos);
        return 0;
    case WM_RBUTTONUP:
        isRightMouseDown = false;
        return 0;
    case WM_MOUSEMOVE:
        if (isRightMouseDown && g_appContext) {
            if (isInternalMove) { isInternalMove = false; break; }
            POINT curr;
            GetCursorPos(&curr);
            int dx = curr.x - lastMousePos.x;
            int dy = curr.y - lastMousePos.y;
            if (dx != 0 || dy != 0) {
                g_appContext->ProcessMouseMenu((float)dx, (float)dy);
                isInternalMove = true;
                SetCursorPos(lastMousePos.x, lastMousePos.y);
            }
        }
        break;
    case WM_SIZE:
        g_width  = LOWORD(lParam);
        g_height = HIWORD(lParam);
        if (g_device && g_width > 0 && g_height > 0) {
            Resize(g_width, g_height);
            RenderFrame();
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, message, wParam, lParam);
}

bool InitDirectX(HWND hWnd) {
    // [수정] 클라이언트 영역 크기를 정확히 가져옴
    RECT clientRect;
    GetClientRect(hWnd, &clientRect);
    g_width  = clientRect.right  - clientRect.left;
    g_height = clientRect.bottom - clientRect.top;
    SPDLOG_INFO("Client size: {}x{}", g_width, g_height);

    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount                        = 1;
    sd.BufferDesc.Width                   = g_width;
    sd.BufferDesc.Height                  = g_height;
    sd.BufferDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator   = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage                        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow                       = hWnd;
    sd.SampleDesc.Count                   = 1;
    sd.Windowed                           = TRUE;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION, &sd,
        &g_swapChain, &g_device, nullptr, &g_context
    );
    if (FAILED(hr)) {
        SPDLOG_ERROR("Failed to create device and swap chain. HRESULT: 0x{:08x}", (uint32_t)hr);
        return false;
    }

    ID3D11Texture2D* pBackBuffer = nullptr;
    g_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
    g_device->CreateRenderTargetView(pBackBuffer, nullptr, &g_renderTargetView);
    pBackBuffer->Release();

    return true;
}

void RenderFrame() {
    if (!g_context || !g_swapChain || !g_renderTargetView || !g_appContext) return;

    static uint64_t lastTime = GetTickCount64();
    uint64_t currTime  = GetTickCount64();
    float    deltaTime = (currTime - lastTime) / 1000.0f;
    lastTime = currTime;

    g_appContext->ProcessKeyboard(deltaTime);
    g_appContext->Render(g_context, g_width, g_height);
    g_appContext->Present(g_context, g_renderTargetView);

    g_swapChain->Present(1, 0);
}

int main() {
    spdlog::set_level(spdlog::level::debug);
    HINSTANCE   hInstance = GetModuleHandle(NULL);
    const char* className = "PathTracingWindowClass";

    WNDCLASSEX wc = {
        sizeof(WNDCLASSEX), CS_HREDRAW | CS_VREDRAW,
        WindowProc, 0, 0, hInstance,
        NULL, LoadCursor(NULL, IDC_ARROW),
        NULL, NULL, className, NULL
    };
    RegisterClassEx(&wc);

    // [수정] 클라이언트 영역이 정확히 WINDOW_WIDTH x WINDOW_HEIGHT가 되도록 조정
    RECT rect = { 0, 0, (LONG)WINDOW_WIDTH, (LONG)WINDOW_HEIGHT };
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    HWND hWnd = CreateWindowEx(
        0, className, WINDOW_NAME,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right  - rect.left,
        rect.bottom - rect.top,
        NULL, NULL, hInstance, NULL
    );
    ShowWindow(hWnd, SW_SHOW);

    if (!InitDirectX(hWnd)) return -1;

    g_appContext = Context::Create(g_device, g_context);
    if (!g_appContext) return -1;

    // [추가] 초기 렌더링 강제 실행
    RenderFrame();

    MSG msg = { 0 };
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        } else {
            RenderFrame();
        }
    }
    return 0;
}