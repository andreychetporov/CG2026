#include "D3DApp.h"
#include <stdexcept>
#include <dxgi1_6.h>
#include <d3d12.h>
#include <DirectXMath.h>
#include <d3dcompiler.h>

#pragma comment(lib, "d3dcompiler.lib")

#define ThrowIfFailed(x) if(FAILED(x)) throw std::runtime_error("DX12 Error");

using namespace DirectX;

// ================================================================

D3DApp::D3DApp(HWND hwnd) : m_hWnd(hwnd)
{
    InitD3D();
}

void D3DApp::InitD3D()
{
    ComPtr<IDXGIFactory4> factory;
    ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));

    ThrowIfFailed(D3D12CreateDevice(
        nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&mDevice)));

    D3D12_COMMAND_QUEUE_DESC qDesc{};
    qDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    qDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    ThrowIfFailed(mDevice->CreateCommandQueue(&qDesc, IID_PPV_ARGS(&mCommandQueue)));

    ThrowIfFailed(mDevice->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&mCmdAllocator)));

    ThrowIfFailed(mDevice->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        mCmdAllocator.Get(), nullptr, IID_PPV_ARGS(&mCmdList)));
    mCmdList->Close();

    // SwapChain
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = SwapChainBufferCount;
    sd.BufferDesc.Width = mClientWidth;
    sd.BufferDesc.Height = mClientHeight;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.OutputWindow = m_hWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;

    ComPtr<IDXGISwapChain> swapChain;
    ThrowIfFailed(factory->CreateSwapChain(mCommandQueue.Get(), &sd, &swapChain));
    ThrowIfFailed(swapChain.As(&mSwapChain));

    // RTV heap
    mRTVDescriptorSize =
        mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
    rtvDesc.NumDescriptors = SwapChainBufferCount;
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    ThrowIfFailed(mDevice->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&mRTVHeap)));

    CreateRTV();
    CreateDepthStencil();   // ? создаём depth buffer

    BuildRootSignature();
    BuildPSO();
    BuildGeometry();
    BuildConstantBuffer();
    BuildViewportScissor();

    ThrowIfFailed(mDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&mFence)));
    mFenceValue = 0;
}

void D3DApp::CreateRTV()
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle =
        mRTVHeap->GetCPUDescriptorHandleForHeapStart();

    for (int i = 0; i < SwapChainBufferCount; ++i)
    {
        ThrowIfFailed(mSwapChain->GetBuffer(i, IID_PPV_ARGS(&mSwapChainBuffer[i])));
        mDevice->CreateRenderTargetView(mSwapChainBuffer[i].Get(), nullptr, handle);
        handle.ptr += (SIZE_T)mRTVDescriptorSize;
    }
}

// ?? Новый метод: создание depth/stencil буфера ??????????????????
void D3DApp::CreateDepthStencil()
{
    // DSV heap (1 дескриптор)
    D3D12_DESCRIPTOR_HEAP_DESC dsvDesc{};
    dsvDesc.NumDescriptors = 1;
    dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(mDevice->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&mDSVHeap)));

    // Ресурс depth buffer (D24S8)
    D3D12_RESOURCE_DESC depthDesc{};
    depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthDesc.Width = (UINT64)mClientWidth;
    depthDesc.Height = (UINT)mClientHeight;
    depthDesc.DepthOrArraySize = 1;
    depthDesc.MipLevels = 1;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE optClear{};
    optClear.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    optClear.DepthStencil.Depth = 1.0f;
    optClear.DepthStencil.Stencil = 0;

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    ThrowIfFailed(mDevice->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &depthDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &optClear,
        IID_PPV_ARGS(&mDepthStencilBuffer)));

    // DSV
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvViewDesc{};
    dsvViewDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsvViewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvViewDesc.Flags = D3D12_DSV_FLAG_NONE;

    mDevice->CreateDepthStencilView(
        mDepthStencilBuffer.Get(),
        &dsvViewDesc,
        mDSVHeap->GetCPUDescriptorHandleForHeapStart());
}

// ????????????????????????????????????????????????????????????????
void D3DApp::Draw()
{
    ThrowIfFailed(mCmdAllocator->Reset());
    ThrowIfFailed(mCmdList->Reset(mCmdAllocator.Get(), nullptr));

    // PRESENT ? RENDER_TARGET
    {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = mSwapChainBuffer[mCurrBackBuffer].Get();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        mCmdList->ResourceBarrier(1, &barrier);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
        mRTVHeap->GetCPUDescriptorHandleForHeapStart();
    rtvHandle.ptr += (SIZE_T)mCurrBackBuffer * (SIZE_T)mRTVDescriptorSize;

    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle =
        mDSVHeap->GetCPUDescriptorHandleForHeapStart();

    // Привязываем RTV + DSV
    mCmdList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

    // Очищаем цвет и глубину
    float clearColor[] = { 0.18f, 0.18f, 0.22f, 1.0f };
    mCmdList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    mCmdList->ClearDepthStencilView(dsvHandle,
        D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
        1.0f, 0, 0, nullptr);

    mCmdList->RSSetViewports(1, &mViewport);
    mCmdList->RSSetScissorRects(1, &mScissor);

    mCmdList->SetGraphicsRootSignature(mRootSig.Get());
    mCmdList->SetPipelineState(mPSO.Get());

    ID3D12DescriptorHeap* heaps[] = { mCbvHeap.Get() };
    mCmdList->SetDescriptorHeaps(1, heaps);
    mCmdList->SetGraphicsRootDescriptorTable(
        0, mCbvHeap->GetGPUDescriptorHandleForHeapStart());

    mCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    mCmdList->IASetVertexBuffers(0, 1, &mVBV);
    mCmdList->IASetIndexBuffer(&mIBV);
    mCmdList->DrawIndexedInstanced(mIndexCount, 1, 0, 0, 0);

    // RENDER_TARGET ? PRESENT
    {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = mSwapChainBuffer[mCurrBackBuffer].Get();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        mCmdList->ResourceBarrier(1, &barrier);
    }

    ThrowIfFailed(mCmdList->Close());

    ID3D12CommandList* cmds[] = { mCmdList.Get() };
    mCommandQueue->ExecuteCommandLists(1, cmds);

    ThrowIfFailed(mSwapChain->Present(1, 0));
    mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

    FlushCommandQueue();
}

void D3DApp::FlushCommandQueue()
{
    mFenceValue++;
    ThrowIfFailed(mCommandQueue->Signal(mFence.Get(), mFenceValue));

    if (mFence->GetCompletedValue() < mFenceValue)
    {
        HANDLE ev = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        ThrowIfFailed(mFence->SetEventOnCompletion(mFenceValue, ev));
        WaitForSingleObject(ev, INFINITE);
        CloseHandle(ev);
    }
}

ComPtr<ID3DBlob> D3DApp::CompileShader(const wchar_t* filename,
    const char* entry,
    const char* target)
{
    UINT flags = 0;
#if defined(_DEBUG)
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    ComPtr<ID3DBlob> bytecode, errors;
    HRESULT hr = D3DCompileFromFile(
        filename, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entry, target, flags, 0, &bytecode, &errors);
    if (errors) OutputDebugStringA((char*)errors->GetBufferPointer());
    ThrowIfFailed(hr);
    return bytecode;
}

void D3DApp::BuildRootSignature()
{
    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    range.NumDescriptors = 1;
    range.BaseShaderRegister = 0;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER param{};
    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    param.DescriptorTable.NumDescriptorRanges = 1;
    param.DescriptorTable.pDescriptorRanges = &range;
    param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.NumParameters = 1;
    rsDesc.pParameters = &param;
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> serialized, errors;
    ThrowIfFailed(D3D12SerializeRootSignature(
        &rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors));
    ThrowIfFailed(mDevice->CreateRootSignature(
        0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
        IID_PPV_ARGS(&mRootSig)));
}

void D3DApp::BuildPSO()
{
    auto vs = CompileShader(L"Shaders.hlsl", "VSMain", "vs_5_0");
    auto ps = CompileShader(L"Shaders.hlsl", "PSMain", "ps_5_0");

    D3D12_INPUT_ELEMENT_DESC inputLayout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0,  0,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_RASTERIZER_DESC rast{};
    rast.FillMode = D3D12_FILL_MODE_SOLID;
    rast.CullMode = D3D12_CULL_MODE_BACK;
    rast.FrontCounterClockwise = FALSE;
    rast.DepthClipEnable = TRUE;

    D3D12_BLEND_DESC blend{};
    blend.AlphaToCoverageEnable = FALSE;
    blend.IndependentBlendEnable = FALSE;
    const D3D12_RENDER_TARGET_BLEND_DESC rtBlend =
    {
        FALSE, FALSE,
        D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
        D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
        D3D12_LOGIC_OP_NOOP,
        D3D12_COLOR_WRITE_ENABLE_ALL
    };
    for (int i = 0; i < 8; i++) blend.RenderTarget[i] = rtBlend;

    // ?? ИСПРАВЛЕНО: включаем depth test ?????????????????????
    D3D12_DEPTH_STENCIL_DESC ds{};
    ds.DepthEnable = TRUE;                              // ? было FALSE
    ds.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;        // ? было ZERO
    ds.DepthFunc = D3D12_COMPARISON_FUNC_LESS;        // ? было ALWAYS
    ds.StencilEnable = FALSE;
    // ????????????????????????????????????????????????????????

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.InputLayout = { inputLayout, _countof(inputLayout) };
    pso.pRootSignature = mRootSig.Get();
    pso.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    pso.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    pso.RasterizerState = rast;
    pso.BlendState = blend;
    pso.DepthStencilState = ds;
    pso.SampleMask = UINT_MAX;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;  // ? обязательно!
    pso.SampleDesc.Count = 1;

    ThrowIfFailed(mDevice->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&mPSO)));
}

void D3DApp::BuildGeometry()
{
    MeshData mesh = LoadOBJ("model.obj", 0.8f, 0.8f, 0.8f);

    mIndexCount = (UINT)mesh.indices.size();

    UINT vbSize = (UINT)(mesh.vertices.size() * sizeof(Vertex));
    UINT ibSize = (UINT)(mesh.indices.size() * sizeof(uint32_t));

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC vbDesc{};
    vbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    vbDesc.Width = vbSize;
    vbDesc.Height = 1;
    vbDesc.DepthOrArraySize = 1;
    vbDesc.MipLevels = 1;
    vbDesc.SampleDesc.Count = 1;
    vbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ThrowIfFailed(mDevice->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE,
        &vbDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(&mVB)));

    void* mapped = nullptr;
    ThrowIfFailed(mVB->Map(0, nullptr, &mapped));
    memcpy(mapped, mesh.vertices.data(), vbSize);
    mVB->Unmap(0, nullptr);

    mVBV.BufferLocation = mVB->GetGPUVirtualAddress();
    mVBV.StrideInBytes = sizeof(Vertex);
    mVBV.SizeInBytes = vbSize;

    D3D12_RESOURCE_DESC ibDesc = vbDesc;
    ibDesc.Width = ibSize;

    ThrowIfFailed(mDevice->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE,
        &ibDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(&mIB)));

    ThrowIfFailed(mIB->Map(0, nullptr, &mapped));
    memcpy(mapped, mesh.indices.data(), ibSize);
    mIB->Unmap(0, nullptr);

    mIBV.BufferLocation = mIB->GetGPUVirtualAddress();
    mIBV.Format = DXGI_FORMAT_R32_UINT;
    mIBV.SizeInBytes = ibSize;
}

void D3DApp::BuildConstantBuffer()
{
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.NumDescriptors = 1;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(mDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&mCbvHeap)));

    UINT cbSize = (sizeof(PerObjectCB) + 255) & ~255u;

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC cbDesc{};
    cbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    cbDesc.Width = cbSize;
    cbDesc.Height = 1;
    cbDesc.DepthOrArraySize = 1;
    cbDesc.MipLevels = 1;
    cbDesc.SampleDesc.Count = 1;
    cbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ThrowIfFailed(mDevice->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE,
        &cbDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(&mConstBuffer)));

    ThrowIfFailed(mConstBuffer->Map(0, nullptr, (void**)&mCbvMappedData));

    D3D12_CONSTANT_BUFFER_VIEW_DESC cbv{};
    cbv.BufferLocation = mConstBuffer->GetGPUVirtualAddress();
    cbv.SizeInBytes = cbSize;

    mDevice->CreateConstantBufferView(
        &cbv, mCbvHeap->GetCPUDescriptorHandleForHeapStart());
}

void D3DApp::BuildViewportScissor()
{
    mViewport.TopLeftX = 0;
    mViewport.TopLeftY = 0;
    mViewport.Width = (float)mClientWidth;
    mViewport.Height = (float)mClientHeight;
    mViewport.MinDepth = 0.0f;
    mViewport.MaxDepth = 1.0f;

    mScissor = { 0, 0, mClientWidth, mClientHeight };
}

void D3DApp::UpdateCB(float dt)
{
    mCamera.Update(dt);

    XMMATRIX world = XMMatrixIdentity();
    XMMATRIX view = mCamera.GetViewMatrix();
    XMMATRIX proj = XMMatrixPerspectiveFovLH(
        XM_PIDIV4,
        (float)mClientWidth / (float)mClientHeight,
        0.1f, 1000.0f);

    PerObjectCB cb{};
    XMStoreFloat4x4(&cb.World, XMMatrixTranspose(world));
    XMStoreFloat4x4(&cb.WorldViewProj, XMMatrixTranspose(world * view * proj));

    cb.LightPosW = { 5.0f, 8.0f, -5.0f };
    cb.EyePosW = mCamera.GetEyePosW();
    cb.DiffuseColor = { 0.8f, 0.8f, 0.8f, 1.0f };
    cb.SpecColorPower = { 1.0f, 1.0f, 1.0f, 32.0f };

    memcpy(mCbvMappedData, &cb, sizeof(cb));
}