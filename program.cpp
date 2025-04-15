#include "program.h"

LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_KEYDOWN:
        OnKeyDown(static_cast<UINT8>(wparam));
        break;
    case WM_INPUT:
        {
            UINT dwSize;
            GetRawInputData((HRAWINPUT)lparam, RID_INPUT, NULL, &dwSize, sizeof(RAWINPUTHEADER));
            LPBYTE lpb = new BYTE[dwSize];

            if (GetRawInputData((HRAWINPUT)lparam, RID_INPUT, lpb, &dwSize, sizeof(RAWINPUTHEADER)) != dwSize)
                OutputDebugString(TEXT("GetRawInputData does not return correct size !\n"));

            RAWINPUT* raw = (RAWINPUT*)lpb;

            if (raw->header.dwType == RIM_TYPEMOUSE)
            {
                OnMouseMove(raw->data.mouse.lLastX, raw->data.mouse.lLastY);
            }
        }
        break;
    case WM_CLOSE:
    case WM_DESTROY: PostQuitMessage(0); [[fallthrough]];
    case WM_SIZING:
    case WM_SIZE: Resize(hwnd); [[fallthrough]];
    default: {}
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

int main()
{
    // Alternatively, DPI_AWARENESS_CONTEXT_UNAWARE
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    WNDCLASSW wcw = { .lpfnWndProc = &WndProc,
                 .hCursor = LoadCursor(nullptr, IDC_ARROW),
                 .lpszClassName = L"DxrTutorialClass" };
    RegisterClassW(&wcw);
    HWND hwnd = CreateWindowExW(0, L"DxrTutorialClass", L"DXR tutorial",
        WS_VISIBLE | WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        /*width=*/CW_USEDEFAULT, /*height=*/CW_USEDEFAULT,
        nullptr, nullptr, nullptr, nullptr);

    SetupNextScene();

    RAWINPUTDEVICE Rid[1];
    Rid[0].usUsagePage = 0x01;          // HID_USAGE_PAGE_GENERIC
    Rid[0].usUsage = 0x02;              // HID_USAGE_GENERIC_MOUSE
    Rid[0].dwFlags = RIDEV_NOLEGACY;    // adds mouse and also ignores legacy mouse messages
    Rid[0].hwndTarget = 0;

    RegisterRawInputDevices(Rid, 1, sizeof(Rid[0]));

    RECT rect;
    GetClientRect(hwnd, &rect);

    POINT ul;
    ul.x = rect.left;
    ul.y = rect.top;

    POINT lr;
    lr.x = rect.right;
    lr.y = rect.bottom;

    MapWindowPoints(hwnd, nullptr, &ul, 1);
    MapWindowPoints(hwnd, nullptr, &lr, 1);

    rect.left = ul.x;
    rect.top = ul.y;

    rect.right = lr.x;
    rect.bottom = lr.y;

    ClipCursor(&rect);
    SetCapture(hwnd);
    SetCursor(NULL);

    Init(hwnd);

    for (MSG msg;;)
    {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
                return 0;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Render(); // Render the next frame
    }
}

void OnKeyDown(UINT8 key)
{
    using namespace DirectX;
    XMFLOAT3 eyeDir = {
        cameraData.lookat.x - cameraData.lookfrom.x,
        cameraData.lookat.y - cameraData.lookfrom.y,
        cameraData.lookat.z - cameraData.lookfrom.z
    };

    XMVECTOR eyeDirNormalized = XMVector3Normalize(XMLoadFloat3(&eyeDir));

    // If ESC, then leave.
    if (key == VK_ESCAPE)
    {
        PostQuitMessage(0);
    }
    else if (key == ' ')
    {
        sceneChangeRequested = true;
    }
    else if (key == 'W')
    {
        cameraMomentum.x += eyeDirNormalized.m128_f32[0] * 0.1f;
        cameraMomentum.y += eyeDirNormalized.m128_f32[1] * 0.1f;
        cameraMomentum.z += eyeDirNormalized.m128_f32[2] * 0.1f;
    }
    else if (key == 'S')
    {
        cameraMomentum.x -= eyeDirNormalized.m128_f32[0] * 0.1f;
        cameraMomentum.y -= eyeDirNormalized.m128_f32[1] * 0.1f;
        cameraMomentum.z -= eyeDirNormalized.m128_f32[2] * 0.1f;
    }
    else if (key == 'A')
    {
        XMVECTOR up{0.0f, 1.0f, 0.0f, 0.0f};
        XMVECTOR cross = XMVector3Cross(eyeDirNormalized, up);
        cameraMomentum.x += cross.m128_f32[0] * 0.1f;
        cameraMomentum.y += cross.m128_f32[1] * 0.1f;
        cameraMomentum.z += cross.m128_f32[2] * 0.1f;
    }
    else if (key == 'D')
    {
        XMVECTOR up{0.0f, 1.0f, 0.0f, 0.0f};
        XMVECTOR cross = XMVector3Cross(eyeDirNormalized, up);
        cameraMomentum.x -= cross.m128_f32[0] * 0.1f;
        cameraMomentum.y -= cross.m128_f32[1] * 0.1f;
        cameraMomentum.z -= cross.m128_f32[2] * 0.1f;
    }
    else if (key == 'Z')
    {
        if (!autoAdaptSamplesCount)
        {
            savedAALevel = cameraData.samplesPerPixel;
        }
        else
        {
            cameraData.samplesPerPixel = savedAALevel;
        }
        autoAdaptSamplesCount = !autoAdaptSamplesCount;
    }
    else if (key == 'M')
    {
        cameraData.doStratify = !cameraData.doStratify;
    }
    else if (key == 'X')
    {
        cameraData.samplesPerPixel *= 2;
    }
    else if (key == 'C')
    {
        cameraData.samplesPerPixel /= (cameraData.samplesPerPixel > 1) ? 2 : 1;
    }
}

void OnMouseMove(int xPosDiff, int yPosDiff)
{
    using namespace DirectX;

    XMFLOAT3 eyeDir = {
        cameraData.lookat.x - cameraData.lookfrom.x,
        cameraData.lookat.y - cameraData.lookfrom.y,
        cameraData.lookat.z - cameraData.lookfrom.z
    };

    float horizontalRotationAmount = static_cast<float>(xPosDiff * 0.003 * XMConvertToRadians(90.0f));
    float verticalRotationAmount = static_cast<float>(yPosDiff * 0.003 * XMConvertToRadians(90.0f));

    XMVECTOR up{ 0.0f, 1.0f, 0.0f, 0.0f };
    XMVECTOR currentEyeDirectionRight = XMVector4Normalize(XMVector3Cross(XMLoadFloat3(&eyeDir), up));
    XMVECTOR currentEyeDirectionUp = XMVector4Normalize(XMVector3Cross(XMLoadFloat3(&eyeDir), currentEyeDirectionRight));

    XMMATRIX rotateX = XMMatrixRotationAxis(currentEyeDirectionUp, -horizontalRotationAmount);
    XMMATRIX rotateY = XMMatrixRotationAxis(currentEyeDirectionRight, -verticalRotationAmount);

    auto prenormalized = XMVector4Normalize(XMVector4Transform(XMLoadFloat3(&eyeDir), rotateY * rotateX));

    float verticalAngleRads = XMVector4AngleBetweenVectors(prenormalized, up).m128_f32[0];

    if (verticalAngleRads >= 0.17f && verticalAngleRads <= 2.96f)
    {
        eyeDir.x = prenormalized.m128_f32[0];
        eyeDir.y = prenormalized.m128_f32[1];
        eyeDir.z = prenormalized.m128_f32[2];

        cameraData.lookat.x = cameraData.lookfrom.x + eyeDir.x;
        cameraData.lookat.y = cameraData.lookfrom.y + eyeDir.y;
        cameraData.lookat.z = cameraData.lookfrom.z + eyeDir.z;
    }
}

void Init(HWND hwnd)
{
    InitDevice();
    InitSurfaces(hwnd);
    InitSeedBuffer();
    InitCommand();
    InitBuffers();
    InitBottomLevel();
    InitScene();
    InitTopLevel();
    InitRootSignature();
    InitPipeline();
}

void InitDevice()
{
    if (FAILED(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&factory))))
        CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));

    if (ID3D12Debug* debug; SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
        debug->EnableDebugLayer(), debug->Release();

    IDXGIAdapter* adapter = nullptr;
    // Uncomment the following line to use software rendering with WARP:
    // factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter));
    D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_2, IID_PPV_ARGS(&device));

    D3D12_COMMAND_QUEUE_DESC cmdQueueDesc = {.Type = D3D12_COMMAND_LIST_TYPE_DIRECT,};
    device->CreateCommandQueue(&cmdQueueDesc, IID_PPV_ARGS(&cmdQueue));

    device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
}

void Flush()
{
    static UINT64 value = 1;
    cmdQueue->Signal(fence, value);
    fence->SetEventOnCompletion(value++, nullptr);
}

void InitSurfaces(HWND hwnd)
{
    DXGI_SWAP_CHAIN_DESC1 scDesc = {.Format = DXGI_FORMAT_R8G8B8A8_UNORM,
                                    .SampleDesc = NO_AA,
                                    .BufferCount = 2,
                                    .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD };
    IDXGISwapChain1* swapChain1;
    factory->CreateSwapChainForHwnd(cmdQueue, hwnd, &scDesc, nullptr, nullptr,
        &swapChain1);
    swapChain1->QueryInterface(&swapChain);
    swapChain1->Release();

    factory->Release();

    D3D12_DESCRIPTOR_HEAP_DESC uavHeapDesc = {.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                                              .NumDescriptors = 2,
                                              .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE };
    device->CreateDescriptorHeap(&uavHeapDesc, IID_PPV_ARGS(&uavHeap));

    Resize(hwnd);
}

void Resize(HWND hwnd)
{
    if (!swapChain) [[unlikely]]
        return;

    RECT rect;
    GetClientRect(hwnd, &rect);
    auto width = std::max<UINT>(rect.right - rect.left, 1);
    auto height = std::max<UINT>(rect.bottom - rect.top, 1);

    Flush();

    swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);

    if (renderTarget) [[likely]]
        renderTarget->Release();

    D3D12_RESOURCE_DESC rtDesc = {.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
                                  .Width = width,
                                  .Height = height,
                                  .DepthOrArraySize = 1,
                                  .MipLevels = 1,
                                  .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
                                  .SampleDesc = NO_AA,
                                  .Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS };

    device->CreateCommittedResource(&DEFAULT_HEAP, D3D12_HEAP_FLAG_NONE, &rtDesc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            nullptr, IID_PPV_ARGS(&renderTarget));

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {.Format = DXGI_FORMAT_R8G8B8A8_UNORM,
                                                .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D };

    device->CreateUnorderedAccessView(
        renderTarget, nullptr, &uavDesc,
        uavHeap->GetCPUDescriptorHandleForHeapStart());
}

void InitSeedBuffer()
{
    D3D12_RESOURCE_DESC bufferDesc = {
                            .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
                            .Width = sizeof(UINT), // Total buffer size in bytes
                            .Height = 1,
                            .DepthOrArraySize = 1,
                            .MipLevels = 1,
                            .Format = DXGI_FORMAT_UNKNOWN,
                            .SampleDesc = NO_AA,
                            .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
                            .Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS };

    device->CreateCommittedResource(&DEFAULT_HEAP, D3D12_HEAP_FLAG_NONE, &bufferDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr, IID_PPV_ARGS(&seedBuffer));

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {.Format = DXGI_FORMAT_UNKNOWN,
                                                .ViewDimension = D3D12_UAV_DIMENSION_BUFFER,
                                                .Buffer = {.FirstElement = 0,
                                                           .NumElements = 1,
                                                           .StructureByteStride = sizeof(UINT),
                                                           .CounterOffsetInBytes = 0,
                                                           .Flags = D3D12_BUFFER_UAV_FLAG_NONE } };

    D3D12_CPU_DESCRIPTOR_HANDLE handle = uavHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    device->CreateUnorderedAccessView(
        seedBuffer, nullptr, &uavDesc,
        handle);
}

void InitCommand()
{
    device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(&cmdAlloc));
    device->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        D3D12_COMMAND_LIST_FLAG_NONE,
        IID_PPV_ARGS(&cmdList));
}

ID3D12Resource* makeAndCopy(void* ptr, size_t size, void** mappedPtr) {
    auto desc = BASIC_BUFFER_DESC;
    desc.Width = size;

    ID3D12Resource* res;
    device->CreateCommittedResource(&UPLOAD_HEAP, D3D12_HEAP_FLAG_NONE,
        &desc, D3D12_RESOURCE_STATE_COMMON,
        nullptr, IID_PPV_ARGS(&res));

    void* mapped;
    res->Map(0, nullptr, &mapped);
    memcpy(mapped, ptr, size);

    if (mappedPtr)
        *mappedPtr = mapped;
    else
        res->Unmap(0, nullptr);

    return res;
};

void InitBuffers()
{
    if (objectsView)
        objectsView->Release();

    objectsView = makeAndCopy(objectList.data(), objectList.size() * sizeof(ObjectData));

    if (lightsView)
        lightsView->Release();

    lightsView = makeAndCopy(lightsList.data(), lightsList.size() * sizeof(UINT));

    // All our procedural primitives will be using this AABB and we will use instance
    // transforms to resize/move them around.

    if (cubeAABB == nullptr)
    {
        const D3D12_RAYTRACING_AABB aabbs = { -1, -1, -1, 1, 1, 1 };
        cubeAABB = makeAndCopy((void*)&aabbs, sizeof(aabbs));
    }

    if (quadAABB == nullptr)
    {
        const D3D12_RAYTRACING_AABB aabbs = { -1, -1, -0.00001f, 1, 1, 0.00001f };
        quadAABB = makeAndCopy((void*)&aabbs, sizeof(aabbs));
    }

    if (cameraConstantBuffer)
        cameraConstantBuffer->Release();

    cameraConstantBuffer = makeAndCopy(&cameraData, sizeof(cameraData), &cameraMappedData);
}

ID3D12Resource* MakeAccelerationStructure(
    const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& inputs,
    UINT64* updateScratchSize)
{
    auto makeBuffer = [](UINT64 size, auto initialState) {
        auto desc = BASIC_BUFFER_DESC;
        desc.Width = size;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        ID3D12Resource* buffer;
        device->CreateCommittedResource(&DEFAULT_HEAP, D3D12_HEAP_FLAG_NONE,
            &desc, initialState, nullptr,
            IID_PPV_ARGS(&buffer));
        return buffer;
    };

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo;
    device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs,
        &prebuildInfo);
    if (updateScratchSize)
        *updateScratchSize = prebuildInfo.UpdateScratchDataSizeInBytes;

    auto* scratch = makeBuffer(prebuildInfo.ScratchDataSizeInBytes,
        D3D12_RESOURCE_STATE_COMMON);
    auto* as = makeBuffer(prebuildInfo.ResultDataMaxSizeInBytes,
        D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {.DestAccelerationStructureData = as->GetGPUVirtualAddress(),
                                                                    .Inputs = inputs,
                                                                    .ScratchAccelerationStructureData = scratch->GetGPUVirtualAddress() };

    cmdAlloc->Reset();
    cmdList->Reset(cmdAlloc, nullptr);
    cmdList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);
    cmdList->Close();
    cmdQueue->ExecuteCommandLists(
        1, reinterpret_cast<ID3D12CommandList**>(&cmdList));

    Flush();
    scratch->Release();
    return as;
}

ID3D12Resource* MakeBLAS(ID3D12Resource* vertexBuffer, UINT vertexFloats,
    ID3D12Resource* indexBuffer, UINT indices)
{
    D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc = {.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES,
                                                   .Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE,
                                                   .Triangles = {.Transform3x4 = 0,
                                                                 .IndexFormat = indexBuffer ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_UNKNOWN,
                                                                 .VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT,
                                                                 .IndexCount = indices,
                                                                 .VertexCount = vertexFloats / 3,
                                                                 .IndexBuffer = indexBuffer ? indexBuffer->GetGPUVirtualAddress() : 0,
                                                                 .VertexBuffer = {.StartAddress = vertexBuffer->GetGPUVirtualAddress(),
                                                                                  .StrideInBytes = sizeof(float) * 3 } } };

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL,
                                                                   .Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE,
                                                                   .NumDescs = 1,
                                                                   .DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
                                                                   .pGeometryDescs = &geometryDesc };
    return MakeAccelerationStructure(inputs);
}

ID3D12Resource* MakeProceduralBLAS(ID3D12Resource* aabbBuffer)
{
    D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc[] = {
                                    {.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS,
                                    .Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE,
                                    .AABBs = {.AABBCount = 1,
                                                .AABBs = {.StartAddress = aabbBuffer->GetGPUVirtualAddress(),
                                                        .StrideInBytes = sizeof(D3D12_RAYTRACING_AABB) } } },
    };

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL,
                                                                   .Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE,
                                                                   .NumDescs = 1,
                                                                   .DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
                                                                   .pGeometryDescs = geometryDesc };
    return MakeAccelerationStructure(inputs);
}

void InitBottomLevel()
{
    if (cubeProceduralBlas)
        cubeProceduralBlas->Release();
    cubeProceduralBlas = MakeProceduralBLAS(cubeAABB);

    if (quadProceduralBlas)
        quadProceduralBlas->Release();
    quadProceduralBlas = MakeProceduralBLAS(quadAABB);
}

ID3D12Resource* MakeTLAS(ID3D12Resource* instances, UINT numInstances,
    UINT64* updateScratchSize)
{
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL,
                                                                   .Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE,
                                                                   .NumDescs = numInstances,
                                                                   .DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
                                                                   .InstanceDescs = instances->GetGPUVirtualAddress() };

    return MakeAccelerationStructure(inputs, updateScratchSize);
}

void InitScene()
{
    if (instances)
        instances->Release();

    auto instancesDesc = BASIC_BUFFER_DESC;
    instancesDesc.Width = sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * getNumInstances();
    device->CreateCommittedResource(&UPLOAD_HEAP, D3D12_HEAP_FLAG_NONE,
        &instancesDesc, D3D12_RESOURCE_STATE_COMMON,
        nullptr, IID_PPV_ARGS(&instances));
    instances->Map(0, nullptr, reinterpret_cast<void**>(&instanceData));

    UINT id = 0;
    for (auto& instance : proceduralInstances)
    {
        ID3D12Resource* accelerationStructure = nullptr;
        switch (instance.type)
        {
        case OBJECT_TYPE_SPHERE:
        case OBJECT_TYPE_VOLUMETRIC_CUBE:
            accelerationStructure = cubeProceduralBlas;
            break;
        case OBJECT_TYPE_QUAD:
            accelerationStructure = quadProceduralBlas;
            break;
        }

        instanceData[id] = { .InstanceID = instance.instanceID,
                             .InstanceMask = 1,
                             .InstanceContributionToHitGroupIndex = instance.hitGroupIndex,
                             .AccelerationStructure = accelerationStructure->GetGPUVirtualAddress() };

        auto* ptr = reinterpret_cast<DirectX::XMFLOAT3X4*>(&instanceData[id].Transform);
        DirectX::XMStoreFloat3x4(ptr, instance.transform);
        ++id;
    }

    instances->Unmap(0, nullptr);

    UpdateTransforms();
}

void UpdateTransforms()
{
    if (cameraMomentum.x < 0.0001f && cameraMomentum.x > -0.0001f)
    {
        cameraMomentum.x = 0.0f;
    }
    else {
        cameraMomentum.x /= 1.1f;
    }

    if (cameraMomentum.y < 0.0001f && cameraMomentum.y > -0.0001f)
    {
        cameraMomentum.y = 0.0f;
    }
    else {
        cameraMomentum.y /= 1.1f;
    }

    if (cameraMomentum.z < 0.0001f && cameraMomentum.z > -0.0001f)
    {
        cameraMomentum.z = 0.0f;
    }
    else {
        cameraMomentum.z /= 1.1f;
    }

    cameraData.lookfrom.x += cameraMomentum.x;
    cameraData.lookfrom.y += cameraMomentum.y;
    cameraData.lookfrom.z += cameraMomentum.z;

    cameraData.lookat.x += cameraMomentum.x;
    cameraData.lookat.y += cameraMomentum.y;
    cameraData.lookat.z += cameraMomentum.z;


    static std::chrono::high_resolution_clock clock;
    static auto t0 = clock.now();
    auto t1 = clock.now();
    auto elapsedMilliseconds = (t1 - t0).count() * 1e-6;
    t0 = t1;
    if (autoAdaptSamplesCount)
    {
        if (elapsedMilliseconds > 30)
        {
            cameraData.samplesPerPixel /= (cameraData.samplesPerPixel > 1) ? 2 : 1;
        }
        else if (elapsedMilliseconds < 10)
        {
            cameraData.samplesPerPixel *= 2;
        }
    }
    printf("elapsedMilliseconds: %d at aa: %d\n", (UINT)elapsedMilliseconds, cameraData.samplesPerPixel);

    cameraData.frameIndex++;
    cameraData.numLights = (UINT)lightsList.size();

    memcpy(cameraMappedData, &cameraData, sizeof(cameraData));
}

void InitTopLevel()
{
    if (tlas)
        tlas->Release();

    if (tlasUpdateScratch)
        tlasUpdateScratch->Release();

    UINT64 updateScratchSize;
    tlas = MakeTLAS(instances, getNumInstances(), &updateScratchSize);

    auto desc = BASIC_BUFFER_DESC;
    // WARP bug workaround: use 8 if the required size was reported as less
    desc.Width = std::max(updateScratchSize, 8ULL);
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    device->CreateCommittedResource(&DEFAULT_HEAP, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_COMMON, nullptr,
        IID_PPV_ARGS(&tlasUpdateScratch));
}

void InitRootSignature()
{
    D3D12_DESCRIPTOR_RANGE uavRange = {.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
                                       .NumDescriptors = 2 };

    D3D12_ROOT_PARAMETER params[] = {
                                        {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
                                         .DescriptorTable = {.NumDescriptorRanges = 1,
                                                            .pDescriptorRanges = &uavRange } },
                                        {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV,
                                         .Descriptor = {.ShaderRegister = 0,
                                                        .RegisterSpace = 0} },
                                        {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV,
                                         .Descriptor = {.ShaderRegister = 1,
                                                        .RegisterSpace = 0} },
                                        {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV,
                                         .Descriptor = {.ShaderRegister = 2,
                                                        .RegisterSpace = 0} },
                                        {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV,
                                         .Descriptor = {.ShaderRegister = 0,
                                                        .RegisterSpace = 0} }
                                    };

    D3D12_ROOT_SIGNATURE_DESC desc = {.NumParameters = (UINT)std::size(params),
                                      .pParameters = params };

    ID3DBlob* blob;
    D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1_0, &blob,
        nullptr);
    device->CreateRootSignature(0, blob->GetBufferPointer(),
        blob->GetBufferSize(),
        IID_PPV_ARGS(&rootSignature));
    blob->Release();
}

void InitPipeline()
{
    D3D12_DXIL_LIBRARY_DESC lib = {.DXILLibrary = {.pShaderBytecode = compiledShader,
                                                   .BytecodeLength = std::size(compiledShader) } };

    D3D12_HIT_GROUP_DESC hitGroupProceduralLambertianSphere = {   .HitGroupExport = L"HitGroupProceduralLambertianSphere",
                                                                  .Type = D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE,
                                                                  .ClosestHitShaderImport = L"ClosestHitProceduralLambertian",
                                                                  .IntersectionShaderImport = L"IntersectionProceduralSphere" };

    D3D12_HIT_GROUP_DESC hitGroupProceduralMetalSphere = {        .HitGroupExport = L"HitGroupProceduralMetalSphere",
                                                                  .Type = D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE,
                                                                  .ClosestHitShaderImport = L"ClosestHitProceduralMetal",
                                                                  .IntersectionShaderImport = L"IntersectionProceduralSphere" };

    D3D12_HIT_GROUP_DESC hitGroupProceduralDielectricSphere = {   .HitGroupExport = L"HitGroupProceduralDielectricSphere",
                                                                  .Type = D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE,
                                                                  .ClosestHitShaderImport = L"ClosestHitProceduralDielectric",
                                                                  .IntersectionShaderImport = L"IntersectionProceduralSphere" };

    D3D12_HIT_GROUP_DESC hitGroupProceduralDiffuseLightSphere = { .HitGroupExport = L"HitGroupProceduralDiffuseLightSphere",
                                                                  .Type = D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE,
                                                                  .ClosestHitShaderImport = L"ClosestHitProceduralDiffuseLight",
                                                                  .IntersectionShaderImport = L"IntersectionProceduralSphere" };

    D3D12_HIT_GROUP_DESC hitGroupProceduralSmokeSphere = {        .HitGroupExport = L"HitGroupProceduralSmokeSphere",
                                                                  .Type = D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE,
                                                                  .ClosestHitShaderImport = L"ClosestHitProceduralSmoke",
                                                                  .IntersectionShaderImport = L"IntersectionProceduralSmokeSphere" };

    D3D12_HIT_GROUP_DESC hitGroupProceduralLambertianQuad = {     .HitGroupExport = L"HitGroupProceduralLambertianQuad",
                                                                  .Type = D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE,
                                                                  .ClosestHitShaderImport = L"ClosestHitProceduralLambertian",
                                                                  .IntersectionShaderImport = L"IntersectionProceduralQuad" };

    D3D12_HIT_GROUP_DESC hitGroupProceduralMetalQuad = {          .HitGroupExport = L"HitGroupProceduralMetalQuad",
                                                                  .Type = D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE,
                                                                  .ClosestHitShaderImport = L"ClosestHitProceduralMetal",
                                                                  .IntersectionShaderImport = L"IntersectionProceduralQuad" };

    D3D12_HIT_GROUP_DESC hitGroupProceduralDielectricQuad = {     .HitGroupExport = L"HitGroupProceduralDielectricQuad",
                                                                  .Type = D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE,
                                                                  .ClosestHitShaderImport = L"ClosestHitProceduralDielectric",
                                                                  .IntersectionShaderImport = L"IntersectionProceduralQuad" };

    D3D12_HIT_GROUP_DESC hitGroupProceduralDiffuseLightQuad = {   .HitGroupExport = L"HitGroupProceduralDiffuseLightQuad",
                                                                  .Type = D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE,
                                                                  .ClosestHitShaderImport = L"ClosestHitProceduralDiffuseLight",
                                                                  .IntersectionShaderImport = L"IntersectionProceduralQuad" };

    D3D12_HIT_GROUP_DESC hitGroupProceduralSmokeCube = {          .HitGroupExport = L"HitGroupProceduralSmokeCube",
                                                                  .Type = D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE,
                                                                  .ClosestHitShaderImport = L"ClosestHitProceduralSmoke",
                                                                  .IntersectionShaderImport = L"IntersectionProceduralSmokeCube" };

    D3D12_HIT_GROUP_DESC hitGroupProceduralGlassCube = {          .HitGroupExport = L"HitGroupProceduralGlassCube",
                                                                  .Type = D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE,
                                                                  .ClosestHitShaderImport = L"ClosestHitProceduralDielectric",
                                                                  .IntersectionShaderImport = L"IntersectionProceduralGlassCube" };

    D3D12_RAYTRACING_SHADER_CONFIG shaderCfg = {.MaxPayloadSizeInBytes = 56,
                                                .MaxAttributeSizeInBytes = 16};

    D3D12_GLOBAL_ROOT_SIGNATURE globalSig = { rootSignature };

    D3D12_RAYTRACING_PIPELINE_CONFIG pipelineCfg = { .MaxTraceRecursionDepth = 1 };
    D3D12_STATE_SUBOBJECT subobjects[] = {
                                            {.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, .pDesc = &lib},
                                            {.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, .pDesc = &hitGroupProceduralLambertianSphere},
                                            {.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, .pDesc = &hitGroupProceduralMetalSphere},
                                            {.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, .pDesc = &hitGroupProceduralDielectricSphere},
                                            {.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, .pDesc = &hitGroupProceduralDiffuseLightSphere},
                                            {.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, .pDesc = &hitGroupProceduralSmokeSphere},
                                            {.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, .pDesc = &hitGroupProceduralLambertianQuad},
                                            {.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, .pDesc = &hitGroupProceduralMetalQuad},
                                            {.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, .pDesc = &hitGroupProceduralDielectricQuad},
                                            {.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, .pDesc = &hitGroupProceduralDiffuseLightQuad},
                                            {.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, .pDesc = &hitGroupProceduralSmokeCube},
                                            {.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, .pDesc = &hitGroupProceduralGlassCube},
                                            {.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG, .pDesc = &shaderCfg},
                                            {.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE, .pDesc = &globalSig},
                                            {.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG, .pDesc = &pipelineCfg}
                                         };

    D3D12_STATE_OBJECT_DESC desc = {.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE,
                                    .NumSubobjects = (UINT)std::size(subobjects),
                                    .pSubobjects = subobjects };
    device->CreateStateObject(&desc, IID_PPV_ARGS(&pso));

    auto idDesc = BASIC_BUFFER_DESC;
    idDesc.Width = 13 * D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
    device->CreateCommittedResource(&UPLOAD_HEAP, D3D12_HEAP_FLAG_NONE, &idDesc,
        D3D12_RESOURCE_STATE_COMMON, nullptr,
        IID_PPV_ARGS(&shaderIDs));

    ID3D12StateObjectProperties* props;
    pso->QueryInterface(&props);

    UINT8* data;
    auto writeId = [&](const wchar_t* name) {
        void* id = props->GetShaderIdentifier(name);
        memcpy(data, id, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
        data = data + D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
    };

    shaderIDs->Map(0, nullptr, (void**)&data);
    writeId(L"RayGeneration");
    writeId(L"Miss");
    writeId(L"HitGroupProceduralLambertianSphere");
    writeId(L"HitGroupProceduralMetalSphere");
    writeId(L"HitGroupProceduralDielectricSphere");
    writeId(L"HitGroupProceduralDiffuseLightSphere");
    writeId(L"HitGroupProceduralSmokeSphere");
    writeId(L"HitGroupProceduralLambertianQuad");
    writeId(L"HitGroupProceduralMetalQuad");
    writeId(L"HitGroupProceduralDielectricQuad");
    writeId(L"HitGroupProceduralDiffuseLightQuad");
    writeId(L"HitGroupProceduralSmokeCube");
    writeId(L"HitGroupProceduralGlassCube");

    shaderIDs->Unmap(0, nullptr);

    props->Release();
}

void ChangeScene()
{
    if (sceneChangeRequested)
    {
        SetupNextScene();

        InitBuffers();
        InitBottomLevel();
        InitScene();
        InitTopLevel();

        sceneChangeRequested = false;
    }
}

void UpdateScene()
{
    UpdateTransforms();

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc = {.DestAccelerationStructureData = tlas->GetGPUVirtualAddress(),
                                                               .Inputs = {
                                                                   .Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL,
                                                                   .Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE,
                                                                   .NumDescs = getNumInstances(),
                                                                   .DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
                                                                   .InstanceDescs = instances->GetGPUVirtualAddress()},
                                                               .SourceAccelerationStructureData = tlas->GetGPUVirtualAddress(),
                                                               .ScratchAccelerationStructureData = tlasUpdateScratch->GetGPUVirtualAddress()};
    cmdList->BuildRaytracingAccelerationStructure(&desc, 0, nullptr);

    D3D12_RESOURCE_BARRIER barrier = {.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV,
                                      .UAV = {.pResource = tlas} };
    cmdList->ResourceBarrier(1, &barrier);
}

void Render()
{
    ChangeScene();

    cmdAlloc->Reset();
    cmdList->Reset(cmdAlloc, nullptr);

    UpdateScene();

    cmdList->SetPipelineState1(pso);
    cmdList->SetComputeRootSignature(rootSignature);

    cmdList->SetDescriptorHeaps(1, &uavHeap);

    auto uavTable = uavHeap->GetGPUDescriptorHandleForHeapStart();
    cmdList->SetComputeRootDescriptorTable(0, uavTable); // u0
    cmdList->SetComputeRootShaderResourceView(1, tlas->GetGPUVirtualAddress()); // t0
    cmdList->SetComputeRootShaderResourceView(2, objectsView->GetGPUVirtualAddress()); // t1
    cmdList->SetComputeRootShaderResourceView(3, lightsView->GetGPUVirtualAddress()); // t2
    cmdList->SetComputeRootConstantBufferView(4, cameraConstantBuffer->GetGPUVirtualAddress()); // b0

    auto rtDesc = renderTarget->GetDesc();
    D3D12_DISPATCH_RAYS_DESC dispatchDesc = {.RayGenerationShaderRecord = {
                                                 .StartAddress = shaderIDs->GetGPUVirtualAddress(),
                                                 .SizeInBytes = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES},
                                             .MissShaderTable = {
                                                 .StartAddress = shaderIDs->GetGPUVirtualAddress() + D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT,
                                                 .SizeInBytes = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES},
                                             .HitGroupTable = {
                                                 .StartAddress = shaderIDs->GetGPUVirtualAddress() + 2 * D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT,
                                                 .SizeInBytes = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES * 22,
                                                 .StrideInBytes = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT},
                                             .Width = static_cast<UINT>(rtDesc.Width),
                                             .Height = rtDesc.Height,
                                             .Depth = 1 };
    cmdList->DispatchRays(&dispatchDesc);

    ID3D12Resource* backBuffer;
    swapChain->GetBuffer(swapChain->GetCurrentBackBufferIndex(),
        IID_PPV_ARGS(&backBuffer));

    auto barrier = [](auto* resource, auto before, auto after) {
        D3D12_RESOURCE_BARRIER rb = {
            .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
            .Transition = {.pResource = resource,
                           .StateBefore = before,
                           .StateAfter = after},
        };
        cmdList->ResourceBarrier(1, &rb);
    };

    barrier(renderTarget, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    barrier(backBuffer, D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_COPY_DEST);

    cmdList->CopyResource(backBuffer, renderTarget);

    barrier(backBuffer, D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PRESENT);
    barrier(renderTarget, D3D12_RESOURCE_STATE_COPY_SOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    backBuffer->Release();

    cmdList->Close();
    cmdQueue->ExecuteCommandLists(
        1, reinterpret_cast<ID3D12CommandList**>(&cmdList));

    Flush();
    swapChain->Present(1, 0);
}