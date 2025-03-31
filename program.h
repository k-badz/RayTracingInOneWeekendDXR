#pragma once

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <algorithm>
#include <vector>
#include <map>
#include <stdexcept>
#include <cmath>
#include <numbers>
#include <chrono>
#include <DirectXMath.h>
#include <Windows.h>
#include <windowsx.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include "shaders.fxh"

#pragma comment(lib, "user32")
#pragma comment(lib, "d3d12")
#pragma comment(lib, "dxgi")

constexpr DXGI_SAMPLE_DESC NO_AA = { .Count = 1, .Quality = 0 };
constexpr D3D12_HEAP_PROPERTIES UPLOAD_HEAP = { .Type = D3D12_HEAP_TYPE_UPLOAD };
constexpr D3D12_HEAP_PROPERTIES DEFAULT_HEAP = { .Type = D3D12_HEAP_TYPE_DEFAULT };
constexpr D3D12_RESOURCE_DESC BASIC_BUFFER_DESC = {
    .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
    .Width = 0, // Will be changed in copies
    .Height = 1,
    .DepthOrArraySize = 1,
    .MipLevels = 1,
    .SampleDesc = NO_AA,
    .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR };

enum OBJECT_TYPE {
    OBJECT_TYPE_SPHERE = 0,
    OBJECT_TYPE_QUAD = 1,
    OBJECT_TYPE_VOLUMETRIC_CUBE = 2,
    OBJECT_TYPE_COUNT
};

struct ProceduralInstance
{
    DirectX::XMMATRIX transform;
    UINT instanceID;
    UINT hitGroupIndex;
    OBJECT_TYPE type;
};

enum MATERIAL_TYPE {
    MATERIAL_TYPE_LAMBERTIAN = 0,
    MATERIAL_TYPE_METAL = 1,
    MATERIAL_TYPE_DIELECTRIC = 2,
    MATERIAL_TYPE_DIFFUSE_LIGHT = 3,
    MATERIAL_TYPE_SMOKE = 4,
    MATERIAL_TYPE_COUNT
};

#pragma pack(4)
struct MaterialData
{
    DirectX::XMFLOAT3 albedo;
    float fuzz;
    float refractionIndex;
    float density;
    MATERIAL_TYPE type;
};

struct ObjectData
{
    MaterialData material;
    OBJECT_TYPE type;

    // Quad specific.
    DirectX::XMFLOAT3 Q;
    DirectX::XMFLOAT3 U;
    DirectX::XMFLOAT3 V;
    DirectX::XMFLOAT3 normal;

    // Sphere specific.
    DirectX::XMFLOAT3 center;
    float             radius;
};

struct CameraData
{
    DirectX::XMFLOAT3 lookfrom;
    float padding1;
    DirectX::XMFLOAT3 lookat;
    float padding2;
    DirectX::XMFLOAT3 backgroundColor;
    float vfov;
    float focusDist;
    float defocusAngle;
    UINT frameIndex;
    UINT samplesPerPixel;
    UINT doStratify;
    UINT numLights;
};

inline CameraData cameraData;
inline IDXGIFactory4* factory = nullptr;
inline ID3D12Device5* device = nullptr;
inline ID3D12CommandQueue* cmdQueue = nullptr;
inline ID3D12Fence* fence = nullptr;
inline IDXGISwapChain3* swapChain = nullptr;
inline ID3D12DescriptorHeap* uavHeap = nullptr;
inline ID3D12Resource* renderTarget = nullptr;
inline ID3D12CommandAllocator* cmdAlloc = nullptr;
inline ID3D12GraphicsCommandList4* cmdList = nullptr;
inline ID3D12Resource* objectsView = nullptr;
inline ID3D12Resource* lightsView = nullptr;

inline ID3D12Resource* cameraConstantBuffer = nullptr;
inline void* cameraMappedData = nullptr;

inline ID3D12Resource* cubeAABB = nullptr;
inline ID3D12Resource* quadAABB = nullptr;
inline ID3D12Resource* cubeProceduralBlas = nullptr;
inline ID3D12Resource* quadProceduralBlas = nullptr;

inline ID3D12Resource* seedBuffer = nullptr;

inline ID3D12Resource* instances = nullptr;
inline D3D12_RAYTRACING_INSTANCE_DESC* instanceData = nullptr;

inline ID3D12Resource* tlas = nullptr;
inline ID3D12Resource* tlasUpdateScratch = nullptr;

inline ID3D12RootSignature* rootSignature = nullptr;

inline ID3D12StateObject* pso = nullptr;
inline ID3D12Resource* shaderIDs = nullptr;

inline bool sceneChangeRequested = false;

inline bool autoAdaptSamplesCount = false;
inline UINT savedAALevel = 0;

inline DirectX::XMFLOAT3 cameraMomentum;

inline std::vector<ProceduralInstance> proceduralInstances;
inline std::vector<ObjectData> objectList;
inline std::vector<UINT> lightsList;

inline UINT getNumInstances()
{
    return (UINT)proceduralInstances.size();
}

void UpdateTransforms();
void Resize(HWND);
void Init(HWND);
void Render();
void InitDevice();
void InitSurfaces(HWND);
void InitCommand();
void InitSeedBuffer();
void InitBuffers();
void InitBottomLevel();
void InitScene();
void InitTopLevel();
void InitRootSignature();
void InitPipeline();
void OnKeyDown(UINT8);
void OnMouseMove(int xPos, int yPos);
void ChangeScene();
void SetupNextScene();

ID3D12Resource* makeAndCopy(void* ptr, size_t size, void** mappedPtr = nullptr);

ID3D12Resource* MakeAccelerationStructure(
    const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& inputs,
    UINT64* updateScratchSize = nullptr);
ID3D12Resource* MakeBLAS(ID3D12Resource* vertexBuffer, UINT vertexFloats,
    ID3D12Resource* indexBuffer = nullptr, UINT indices = 0);
ID3D12Resource* MakeProceduralBLAS(ID3D12Resource* aabbBuffer);
ID3D12Resource* MakeTLAS(ID3D12Resource* instances, UINT numInstances,
    UINT64* updateScratchSize);