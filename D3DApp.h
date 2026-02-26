#pragma once
#include <windows.h>
#include <wrl.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <DirectXMath.h>
#include <d3dcompiler.h>
#include <cstdint>
#include "ObjLoader.h"
#include "Camera.h"

using Microsoft::WRL::ComPtr;

struct alignas(256) PerObjectCB
{
    DirectX::XMFLOAT4X4 WorldViewProj;
    DirectX::XMFLOAT4X4 World;
    DirectX::XMFLOAT3   LightPosW; float pad0;
    DirectX::XMFLOAT3   EyePosW;   float pad1;
    DirectX::XMFLOAT4   DiffuseColor;
    DirectX::XMFLOAT4   SpecColorPower;
};

class D3DApp
{
public:
    D3DApp(HWND hwnd);
    void Draw();
    void UpdateCB(float dt);

    void OnMouseDown(WPARAM btn, int x, int y) { mCamera.OnMouseDown(btn, x, y); }
    void OnMouseUp(WPARAM btn) { mCamera.OnMouseUp(btn); }
    void OnMouseMove(WPARAM btn, int x, int y) { mCamera.OnMouseMove(btn, x, y); }
    void OnMouseWheel(int delta) { mCamera.OnMouseWheel(delta); }

private:
    OrbitalCamera mCamera{ 6.0f, 0.5f, 0.4f };

    UINT mIndexCount = 0;
    HWND m_hWnd;

    static const int SwapChainBufferCount = 2;
    int mCurrBackBuffer = 0;

    ComPtr<ID3D12Device>              mDevice;
    ComPtr<ID3D12CommandQueue>        mCommandQueue;
    ComPtr<ID3D12CommandAllocator>    mCmdAllocator;
    ComPtr<ID3D12GraphicsCommandList> mCmdList;

    ComPtr<IDXGISwapChain4>           mSwapChain;
    ComPtr<ID3D12DescriptorHeap>      mRTVHeap;
    ComPtr<ID3D12Resource>            mSwapChainBuffer[SwapChainBufferCount];
    UINT                              mRTVDescriptorSize = 0;

    // ?? Depth buffer ??????????????????????????????????????????
    ComPtr<ID3D12DescriptorHeap>      mDSVHeap;
    ComPtr<ID3D12Resource>            mDepthStencilBuffer;
    // ?????????????????????????????????????????????????????????

    ComPtr<ID3D12Fence>               mFence;
    UINT64                            mFenceValue = 0;

    ComPtr<ID3D12RootSignature>       mRootSig;
    ComPtr<ID3D12PipelineState>       mPSO;

    ComPtr<ID3D12Resource>            mVB;
    ComPtr<ID3D12Resource>            mIB;
    D3D12_VERTEX_BUFFER_VIEW          mVBV{};
    D3D12_INDEX_BUFFER_VIEW           mIBV{};

    ComPtr<ID3D12DescriptorHeap>      mCbvHeap;
    ComPtr<ID3D12Resource>            mConstBuffer;
    uint8_t* mCbvMappedData = nullptr;

    D3D12_VIEWPORT mViewport{};
    D3D12_RECT     mScissor{};

    int mClientWidth = 1280;
    int mClientHeight = 720;

    void InitD3D();
    void CreateRTV();
    void CreateDepthStencil();   // ? новый метод
    void BuildRootSignature();
    void BuildPSO();
    void BuildGeometry();
    void BuildConstantBuffer();
    void BuildViewportScissor();
    void FlushCommandQueue();

    ComPtr<ID3DBlob> CompileShader(const wchar_t* filename,
        const char* entry,
        const char* target);
};