#include "program.h"

float random_float() {
    // Returns a random real in [0,1).
    return std::rand() / (RAND_MAX + 1.0f);
}

float random_float(float min, float max) {
    // Returns a random real in [min,max).
    return min + (max-min)*random_float();
}

DirectX::XMFLOAT3 random_vector() {
    return DirectX::XMFLOAT3(random_float(), random_float(), random_float());
}

DirectX::XMFLOAT3 random_vector(float min, float max) {
    return DirectX::XMFLOAT3(random_float(min, max), random_float(min, max), random_float(min, max));
}

UINT objectTypeToHitGroupIndex(OBJECT_TYPE objectType, MATERIAL_TYPE materialType)
{
    static const std::map<std::pair<OBJECT_TYPE, MATERIAL_TYPE>, UINT> hitGroupIndices = {
        { { OBJECT_TYPE_SPHERE, MATERIAL_TYPE_LAMBERTIAN }, 0 },
        { { OBJECT_TYPE_SPHERE, MATERIAL_TYPE_METAL }, 1 },
        { { OBJECT_TYPE_SPHERE, MATERIAL_TYPE_DIELECTRIC }, 2 },
        { { OBJECT_TYPE_SPHERE, MATERIAL_TYPE_DIFFUSE_LIGHT }, 3 },
        { { OBJECT_TYPE_SPHERE, MATERIAL_TYPE_SMOKE }, 4 },
        { { OBJECT_TYPE_QUAD, MATERIAL_TYPE_LAMBERTIAN }, 5 },
        { { OBJECT_TYPE_QUAD, MATERIAL_TYPE_METAL }, 6 },
        { { OBJECT_TYPE_QUAD, MATERIAL_TYPE_DIELECTRIC }, 7 },
        { { OBJECT_TYPE_QUAD, MATERIAL_TYPE_DIFFUSE_LIGHT }, 8 },
        { { OBJECT_TYPE_VOLUMETRIC_CUBE, MATERIAL_TYPE_SMOKE }, 9 },
        { { OBJECT_TYPE_VOLUMETRIC_CUBE, MATERIAL_TYPE_DIELECTRIC }, 10 }
    };

    if (hitGroupIndices.find({ objectType, materialType }) == hitGroupIndices.end())
    {
        throw std::runtime_error("Unimplemented object type/material type combination");
    }

    return hitGroupIndices.at({ objectType, materialType });
}

void addProceduralObject(DirectX::XMMATRIX transform, ObjectData& obj, bool isPDFLightSource)
{
    UINT instanceIDCounter = (UINT)proceduralInstances.size();
    proceduralInstances.push_back({ .transform = transform, .instanceID = instanceIDCounter, .hitGroupIndex = objectTypeToHitGroupIndex(obj.type, obj.material.type), .type = obj.type });

    if (isPDFLightSource)
    {
        lightsList.push_back((UINT)objectList.size());
    }

    objectList.push_back(obj);
}

void addSphere(DirectX::XMFLOAT3 position, float r, MaterialData& mat, bool isPDFLightSource = false)
{
    // We always assume that inside the AABB, sphere is centered in 0,0,0 and has a radius of 1.
    // We will use position and R to scale/move it properly
    ObjectData objectData = { .material = mat, .type = OBJECT_TYPE_SPHERE, .center = position, .radius = r};
    addProceduralObject(DirectX::XMMatrixScaling(r, r, r) * DirectX::XMMatrixTranslation(position.x, position.y, position.z), objectData, isPDFLightSource);
}

void addQuad(DirectX::XMFLOAT3 position, DirectX::XMFLOAT3 u, DirectX::XMFLOAT3 v, MaterialData& mat, bool isPDFLightSource = false)
{
    // We always assume that quad inside AABB is axis-aligned, facing -z, taking whole AABB space (-1, 1) in its z=0 plane.
    // Position specifies left-bottom vertex of the quad
    // U is a vector pointing upwards from position to the top-left vertex of the quad, V is a vector pointing right from position to the bottom-right vertex.
    // We will use position, U and V to make a rotation/shear/scaling matrix to transform it to the desired position.

    // Convert to XMVECTOR for easier math
    DirectX::XMVECTOR uvec = DirectX::XMLoadFloat3(&u);
    DirectX::XMVECTOR vvec = DirectX::XMLoadFloat3(&v);
    DirectX::XMVECTOR pos = DirectX::XMLoadFloat3(&position);

    // Calculate the basis vectors for our transformation matrix
    // Scaling by half because the quad spans from -1 to 1 (total range of 2)
    DirectX::XMVECTOR xBasis = DirectX::XMVectorScale(vvec, 0.5f);  // x basis is v/2
    DirectX::XMVECTOR yBasis = DirectX::XMVectorScale(uvec, 0.5f);  // y basis is u/2

    // The z axis is perpendicular to the quad
    DirectX::XMVECTOR zBasis = DirectX::XMVector3Cross(uvec, vvec);
    zBasis = DirectX::XMVector3Normalize(zBasis);

    // Calculate the translation that maps (-1,-1,0) to position
    // We need to offset by xBasis + yBasis to map the bottom-left corner properly
    DirectX::XMVECTOR translation = DirectX::XMVectorAdd(
        pos,
        DirectX::XMVectorAdd(xBasis, yBasis)
    );

    // Create the transformation matrix
    DirectX::XMMATRIX transform;
    transform.r[0] = DirectX::XMVectorSetW(xBasis, 0.0f);        // x basis
    transform.r[1] = DirectX::XMVectorSetW(yBasis, 0.0f);        // y basis
    transform.r[2] = DirectX::XMVectorSetW(zBasis, 0.0f);        // z basis
    transform.r[3] = DirectX::XMVectorSetW(translation, 1.0f);   // translation

    ObjectData objectData = { .material = mat, .type = OBJECT_TYPE_QUAD, .Q = position, .U = u, .V = v };
    addProceduralObject(transform, objectData, isPDFLightSource);
}

void addBox(DirectX::XMFLOAT3 a, DirectX::XMFLOAT3 b, MaterialData& mat, float rotateX = 0, float rotateY = 0,float rotateZ = 0, bool isPDFLightSource = false)
{
    using namespace DirectX;

    if (mat.type == MATERIAL_TYPE_SMOKE || mat.type == MATERIAL_TYPE_DIELECTRIC)
    {
        // Smoke/glass is a special case, we need to add a rotated volumetric cube.
        // Initial assumption was to expect A point to have all dims values smaller than B and create axis aligned box out of it.
        // But it turned out it's not that complicated to support also other cases.
        // Book rotates around A, not origin. We will do the same, let's reposition A point into (0,0,0) in object-space and do
        // necessary transforms.
        ObjectData objectData = { .material = mat, .type = OBJECT_TYPE_VOLUMETRIC_CUBE };
        addProceduralObject(XMMatrixTranslation(
            a.x < b.x ? 1.0f : -1.0f, 
            a.y < b.y ? 1.0f : -1.0f,  
            a.z < b.z ? 1.0f : -1.0f) *  
            XMMatrixRotationRollPitchYaw(
                XMConvertToRadians(rotateX),
                XMConvertToRadians(rotateY),
                XMConvertToRadians(rotateZ)) *
            XMMatrixScaling(std::abs(b.x - a.x) / 2, std::abs(b.y - a.y) / 2, std::abs(b.z - a.z) / 2) *
            XMMatrixTranslation(a.x, a.y, a.z), 
            objectData, isPDFLightSource);
        return;
    }

    // For other cases, just create an empty box from quads.
    XMVECTOR vA = XMLoadFloat3(&a);
    XMVECTOR vB = XMLoadFloat3(&b);

    XMVECTOR vMin = XMVectorMin(vA, vB);
    XMVECTOR vMax = XMVectorMax(vA, vB);

    XMFLOAT3 min, max;
    XMStoreFloat3(&min, vMin);
    XMStoreFloat3(&max, vMax);

    // Calculate dimension vectors
    XMFLOAT3 dx(max.x - min.x, 0.0f, 0.0f);
    XMFLOAT3 dy(0.0f, max.y - min.y, 0.0f);
    XMFLOAT3 dz(0.0f, 0.0f, max.z - min.z);

    XMFLOAT3 minusdx(-dx.x, -dx.y, -dx.z);
    XMFLOAT3 minusdz(-dz.x, -dz.y, -dz.z);

    // Rotation around A point.
    XMVECTOR rotationOrigin = vA;
    XMMATRIX rotationMatrix = DirectX::XMMatrixRotationRollPitchYaw(
        XMConvertToRadians(rotateX),
        XMConvertToRadians(rotateY),
        XMConvertToRadians(rotateZ)
    );

    auto rotatePoint = [&](XMFLOAT3 point) {
        XMFLOAT3 ret;
        XMStoreFloat3(&ret, XMVectorAdd(XMVector3Transform(XMVectorSubtract(XMLoadFloat3(&point), rotationOrigin), rotationMatrix), rotationOrigin));
        return ret;
        };

    auto rotateVec = [&](XMFLOAT3 vec) {
        XMFLOAT3 ret;
        XMStoreFloat3(&ret, XMVector3Transform(XMLoadFloat3(&vec), rotationMatrix));
        return ret;
        };

    addQuad(rotatePoint({min.x, min.y, max.z}), rotateVec(     dx), rotateVec(     dy), mat, isPDFLightSource); // front
    addQuad(rotatePoint({max.x, min.y, max.z}), rotateVec(minusdz), rotateVec(     dy), mat, isPDFLightSource); // right
    addQuad(rotatePoint({max.x, min.y, min.z}), rotateVec(minusdx), rotateVec(     dy), mat, isPDFLightSource); // back
    addQuad(rotatePoint({min.x, min.y, min.z}), rotateVec(     dz), rotateVec(     dy), mat, isPDFLightSource); // left
    addQuad(rotatePoint({min.x, max.y, max.z}), rotateVec(     dx), rotateVec(minusdz), mat, isPDFLightSource); // top
    addQuad(rotatePoint({min.x, min.y, min.z}), rotateVec(     dx), rotateVec(     dz), mat, isPDFLightSource); // bottom
}

void setupSceneBasic(float defocusAngle)
{
    cameraData = { .lookfrom = { 0, 0, -0.5 },
        .lookat = { 0, 0, 1 },
        .backgroundColor = { 0.4f, 0.6f, 0.8f }, 
        .vfov = 90.0f,
        .focusDist = 3.4f,
        .defocusAngle = defocusAngle,
        .samplesPerPixel = 16,
        .doStratify = false
    };

    MaterialData materialCenter = { .albedo = { 0.1f, 0.2f, 0.5f},                               .type = MATERIAL_TYPE_LAMBERTIAN };
    MaterialData materialLeft =   { .albedo = { 1.0f, 1.0f, 1.0f}, .refractionIndex = 1.5f,      .type = MATERIAL_TYPE_DIELECTRIC };
    MaterialData materialBubble = { .albedo = { 1.0f, 1.0f, 1.0f}, .refractionIndex = 1.0f/1.5f, .type = MATERIAL_TYPE_DIELECTRIC };
    MaterialData materialRight =  { .albedo = { 0.8f, 0.6f, 0.2f}, .fuzz = 0.1f,                 .type = MATERIAL_TYPE_METAL };
    MaterialData materialGround = { .albedo = { 0.8f, 0.8f, 0.0f},                               .type = MATERIAL_TYPE_LAMBERTIAN };

    MaterialData light =          { .albedo = {   15,   15,   10},                               .type = MATERIAL_TYPE_DIFFUSE_LIGHT };
    addSphere({ 0, 50, 1 }, 5, light, true);

    addSphere({ 0, 0, 1 }, 0.49f, materialCenter);
    addSphere({ 0, -100.5, 1 }, 100, materialGround);
    addSphere({ -1, 0, 1 }, 0.5f, materialLeft);
    addSphere({ -1, 0, 1 }, 0.4f, materialBubble);
    addSphere({ 1, 0, 1 }, 0.5f, materialRight);
}

void setupSceneBasicClose(float defocusAngle)
{
    cameraData = { 
        .lookfrom = { -2, 2, -1 },
        .lookat = { 0, 0, 1 },
        .backgroundColor = { 0.4f, 0.6f, 0.8f }, 
        .vfov = 20.0f,
        .focusDist = 3.4f,
        .defocusAngle = defocusAngle,
        .samplesPerPixel = 16,
        .doStratify = false
    };

    MaterialData materialCenter = { .albedo = { 0.1f, 0.2f, 0.5f},                               .type = MATERIAL_TYPE_LAMBERTIAN };
    MaterialData materialLeft =   { .albedo = { 1.0f, 1.0f, 1.0f}, .refractionIndex = 1.5f,      .type = MATERIAL_TYPE_DIELECTRIC };
    MaterialData materialBubble = { .albedo = { 1.0f, 1.0f, 1.0f}, .refractionIndex = 1.0f/1.5f, .type = MATERIAL_TYPE_DIELECTRIC };
    MaterialData materialRight =  { .albedo = { 0.8f, 0.6f, 0.2f}, .fuzz = 0.1f,                 .type = MATERIAL_TYPE_METAL };
    MaterialData materialGround = { .albedo = { 0.8f, 0.8f, 0.0f},                               .type = MATERIAL_TYPE_LAMBERTIAN };

    MaterialData light =          { .albedo = {   15,   15,   10},                               .type = MATERIAL_TYPE_DIFFUSE_LIGHT };
    addSphere({ 0, 50, 1 }, 5, light, true);

    addSphere({ 0, 0, 1 }, 0.49f, materialCenter);
    addSphere({ 0, -100.5, 1 }, 100, materialGround);
    addSphere({ -1, 0, 1 }, 0.5f, materialLeft);
    addSphere({ -1, 0, 1 }, 0.4f, materialBubble);
    addSphere({ 1, 0, 1 }, 0.5f, materialRight);
}

void setupSceneFinal(float defocusAngle, bool isNight)
{
    cameraData = { 
        .lookfrom = { 13, 2, -3 },
        .lookat = { 0, 0, 0 },
        .backgroundColor = (isNight) ? DirectX::XMFLOAT3{ 0, 0, 0 } : DirectX::XMFLOAT3{ 0.4f, 0.6f, 0.8f },
        .vfov = 20.0f,
        .focusDist = 10.0f,
        .defocusAngle = defocusAngle,
        .samplesPerPixel = 16,
        .doStratify = false
    };

    MaterialData light =          { .albedo = {   15,   15,   10},                               .type = MATERIAL_TYPE_DIFFUSE_LIGHT };
    addSphere({ 0, 50, 1 }, 10, light, true);

    MaterialData materialGround = { .albedo = { 0.5f, 0.5f, 0.5f},                               .type = MATERIAL_TYPE_LAMBERTIAN };
    addSphere({ 0, -1000, 0 }, 1000, materialGround);

    MaterialData material1 =   { .albedo = { 1.0f, 1.0f, 1.0f}, .refractionIndex = 1.5f,      .type = MATERIAL_TYPE_DIELECTRIC };
    addSphere({ 0, 1, 0 }, 1.0, material1);

    MaterialData material2 =   { .albedo = { 0.4f, 0.2f, 0.1f}, .refractionIndex = 1.5f,      .type = MATERIAL_TYPE_LAMBERTIAN };
    addSphere({ -4, 1, 0 }, 1.0, material2);

    MaterialData material3 =   { .albedo = { 0.7f, 0.6f, 0.5f}, .fuzz = 0.0f,                 .type = MATERIAL_TYPE_METAL };
    addSphere({ 4, 1, 0 }, 1.0, material3);

    for (int a = -11; a < 11; a++) {
        for (int b = -11; b < 11; b++) {
            auto choose_mat = random_float();
            DirectX::XMFLOAT3 center(a + 0.9f*random_float(), 0.2f, b + 0.9f*random_float());
            DirectX::XMFLOAT3 somepoint(4, 0.2f, 0);
            DirectX::XMFLOAT3 result(center.x - somepoint.x, center.y - somepoint.y, center.z - somepoint.z);
            float distance = std::sqrt(result.x * result.x + result.y * result.y + result.z * result.z);

            if (distance > 0.9) {
                if (choose_mat < 0.8) {
                    // diffuse
                    DirectX::XMFLOAT3 vec1 = random_vector();
                    DirectX::XMFLOAT3 vec2 = random_vector();
                    MaterialData sphere_material = { .albedo = {vec1.x * vec2.x, vec1.y * vec2.y, vec1.z * vec2.z}, .type = MATERIAL_TYPE_LAMBERTIAN };
                    addSphere(center, 0.2f, sphere_material);
                } else if (choose_mat < 0.95) {
                    // metal
                    MaterialData sphere_material = { .albedo = random_vector(0.5, 1), .fuzz = random_float(0, 0.5), .type = MATERIAL_TYPE_METAL };
                    addSphere(center, 0.2f, sphere_material);
                } else {
                    // glass
                    MaterialData sphere_material =   { .albedo = { 1.0f, 1.0f, 1.0f}, .refractionIndex = 1.5f, .type = MATERIAL_TYPE_DIELECTRIC };
                    addSphere(center, 0.2f, sphere_material);
                }
            }
        }
    }
}

void setupSceneQuads(float defocusAngle)
{
    cameraData = { 
        .lookfrom = { 0, 0, -9 }, 
        .lookat = { 0, 0, 0 }, 
        .backgroundColor = { 0, 0, 0 }, 
        .vfov = 80.0f, 
        .focusDist = 10.0f, 
        .defocusAngle = defocusAngle, 
        .samplesPerPixel = 16,
        .doStratify = false
    };

    MaterialData leftRed     = { .albedo = { 1.0f, 0.2f, 0.2f}, .type = MATERIAL_TYPE_LAMBERTIAN };
    MaterialData backGreen   = { .albedo = { 0.2f, 1.0f, 0.2f}, .type = MATERIAL_TYPE_LAMBERTIAN };
    MaterialData rightBlue   = { .albedo = { 0.2f, 0.2f, 1.0f}, .type = MATERIAL_TYPE_LAMBERTIAN };
    MaterialData upperOrange = { .albedo = { 5.0f, 2.5f, 0.1f}, .type = MATERIAL_TYPE_DIFFUSE_LIGHT };
    MaterialData lowerTeal   = { .albedo = { 0.2f, 0.8f, 0.8f}, .type = MATERIAL_TYPE_LAMBERTIAN };

    addQuad({ -3,-2, -5 }, { 0, 0,  4 }, { 0, 4,  0 }, leftRed);
    addQuad({ -2,-2, -0 }, { 4, 0,  0 }, { 0, 4,  0 }, backGreen);
    addQuad({  3,-2, -1 }, { 0, 0, -4 }, { 0, 4,  0 }, rightBlue);
    addQuad({ -2, 3, -1 }, { 4, 0,  0 }, { 0, 0, -4 }, upperOrange, true);
    addQuad({ -2,-3, -5 }, { 4, 0,  0 }, { 0, 0,  4 }, lowerTeal);
}

void setupSceneCornellBox(float defocusAngle)
{
    // Make everything 10x smaller than in article so its easier to navigate...
    cameraData = { 
        .lookfrom = { 27.8f, 27.8f, 80.0f }, 
        .lookat = { 27.8f, 27.8f, 0 }, 
        .backgroundColor = { 0, 0, 0 }, 
        .vfov = 40, 
        .focusDist = 10.0f, 
        .defocusAngle = defocusAngle, 
        .samplesPerPixel = 16,
        .doStratify = false
    };

    MaterialData red   = { .albedo = { .65f, .05f, .05f}, .type = MATERIAL_TYPE_LAMBERTIAN };
    MaterialData white = { .albedo = { .73f, .73f, .73f}, .type = MATERIAL_TYPE_LAMBERTIAN };
    MaterialData green = { .albedo = { .12f, .45f, .15f}, .type = MATERIAL_TYPE_LAMBERTIAN };
    MaterialData light = { .albedo = {   15,   15,   15}, .type = MATERIAL_TYPE_DIFFUSE_LIGHT };

    addQuad({ 55.5f,   .0f,    .0f }, {    .0f, 55.5f, .0f }, { .0f,   .0f, -55.5f }, green);
    addQuad({   .0f,   .0f,    .0f }, {    .0f, 55.5f, .0f }, { .0f,   .0f, -55.5f },   red);
    addQuad({ 34.3f, 55.4f, -33.2f }, { -13.0f,   .0f, .0f }, { .0f,   .0f,  10.5f }, light, true);
    addQuad({   .0f,   .0f,    .0f }, {  55.5f,   .0f, .0f }, { .0f,   .0f, -55.5f }, white);
    addQuad({ 55.5f, 55.5f, -55.5f }, { -55.5f,   .0f, .0f }, { .0f,   .0f,  55.5f }, white);
    addQuad({   .0f,   .0f, -55.5f }, {  55.5f,   .0f, .0f }, { .0f, 55.5f,    .0f }, white);

    addBox({  13.0f,   .0f,  -6.5f }, {  29.5f, 16.5f, -23.0f}, white, 0,  15, 0);
    addBox({  26.5f,   .0f, -29.5f }, {  43.0f, 33.0f, -46.0f}, white, 0, -18, 0);
}

void setupSceneCornellBoxGlass(float defocusAngle)
{
    // Make everything 10x smaller than in article so its easier to navigate...
    cameraData = { 
        .lookfrom = { 27.8f, 27.8f, 80.0f }, 
        .lookat = { 27.8f, 27.8f, 0 }, 
        .backgroundColor = { 0, 0, 0 }, 
        .vfov = 40, 
        .focusDist = 10.0f, 
        .defocusAngle = defocusAngle, 
        .samplesPerPixel = 16,
        .doStratify = false
    };

    MaterialData red   = { .albedo = { .65f, .05f, .05f}, .type = MATERIAL_TYPE_LAMBERTIAN };
    MaterialData white = { .albedo = { .73f, .73f, .73f}, .type = MATERIAL_TYPE_LAMBERTIAN };
    MaterialData green = { .albedo = { .12f, .45f, .15f}, .type = MATERIAL_TYPE_LAMBERTIAN };
    MaterialData light = { .albedo = {   15,   15,   15}, .type = MATERIAL_TYPE_DIFFUSE_LIGHT };

    addQuad({ 55.5f,   .0f,    .0f }, {    .0f, 55.5f, .0f }, { .0f,   .0f, -55.5f }, green);
    addQuad({   .0f,   .0f,    .0f }, {    .0f, 55.5f, .0f }, { .0f,   .0f, -55.5f },   red);
    addQuad({ 34.3f, 55.4f, -33.2f }, { -13.0f,   .0f, .0f }, { .0f,   .0f,  10.5f }, light, true);
    addQuad({   .0f,   .0f,    .0f }, {  55.5f,   .0f, .0f }, { .0f,   .0f, -55.5f }, white);
    addQuad({ 55.5f, 55.5f, -55.5f }, { -55.5f,   .0f, .0f }, { .0f,   .0f,  55.5f }, white);
    addQuad({   .0f,   .0f, -55.5f }, {  55.5f,   .0f, .0f }, { .0f, 55.5f,    .0f }, white);


    MaterialData whiteGlass = { .albedo = { 1, 1, 1}, .refractionIndex = 1.5f, .type = MATERIAL_TYPE_DIELECTRIC };
    addBox({  13.0f,   .0f,  -6.5f }, {  29.5f, 16.5f, -23.0f}, whiteGlass, 0,  15, 0);
    addBox({  26.5f,   .0f, -29.5f }, {  43.0f, 33.0f, -46.0f}, whiteGlass, 0, -18, 0);
}

void setupSceneCornellBoxSmoke(float defocusAngle)
{
    cameraData = {
        .lookfrom = { 278, 278, 800 },
        .lookat = { 278, 278, 0 },
        .backgroundColor = { 0, 0, 0 },
        .vfov = 40,
        .focusDist = 10.0f,
        .defocusAngle = defocusAngle,
        .samplesPerPixel = 16,
        .doStratify = false
    };

    MaterialData red   = { .albedo = { .65f, .05f, .05f}, .type = MATERIAL_TYPE_LAMBERTIAN };
    MaterialData white = { .albedo = { .73f, .73f, .73f}, .type = MATERIAL_TYPE_LAMBERTIAN };
    MaterialData green = { .albedo = { .12f, .45f, .15f}, .type = MATERIAL_TYPE_LAMBERTIAN };
    MaterialData light = { .albedo = {   7,   7,   7}, .type = MATERIAL_TYPE_DIFFUSE_LIGHT };

    MaterialData whiteSmoke = { .albedo = { 1, 1, 1}, .density = 0.02f, .type = MATERIAL_TYPE_SMOKE };
    MaterialData blackSmoke = { .albedo = { 0, 0, 0}, .density = 0.02f, .type = MATERIAL_TYPE_SMOKE };

    addQuad({ 555,   0,    0 }, {    0, 555, 0 }, { 0,   0, -555 }, green);
    addQuad({   0,   0,    0 }, {    0, 555, 0 }, { 0,   0, -555 },   red);
    addQuad({ 113, 554, -127 }, {  330,   0, 0 }, { 0,   0, -305 }, light, true);
    addQuad({   0,   0,    0 }, {  555,   0, 0 }, { 0,   0, -555 }, white);
    addQuad({ 555, 555, -555 }, { -555,   0, 0 }, { 0,   0,  555 }, white);
    addQuad({   0,   0, -555 }, {  555,   0, 0 }, { 0, 555,    0 }, white);

    addBox({  130,   0,  -65 }, {  295, 165, -230}, whiteSmoke, 0,  15, 0);
    addBox({  265,   0, -295 }, {  430, 330, -460}, blackSmoke, 0, -18, 0);
}

void setupSceneCornellBoxMetal(float defocusAngle)
{
    // Make everything 10x smaller than in article so its easier to navigate...
    cameraData = { 
        .lookfrom = { 27.8f, 27.8f, 80.0f }, 
        .lookat = { 27.8f, 27.8f, 0 }, 
        .backgroundColor = { 0, 0, 0 }, 
        .vfov = 40, 
        .focusDist = 10.0f, 
        .defocusAngle = defocusAngle, 
        .samplesPerPixel = 16,
        .doStratify = false
    };

    MaterialData red   = { .albedo = { .65f, .05f, .05f}, .type = MATERIAL_TYPE_LAMBERTIAN };
    MaterialData white = { .albedo = { .73f, .73f, .73f}, .type = MATERIAL_TYPE_LAMBERTIAN };
    MaterialData green = { .albedo = { .12f, .45f, .15f}, .type = MATERIAL_TYPE_LAMBERTIAN };
    MaterialData light = { .albedo = {   15,   15,   15}, .type = MATERIAL_TYPE_DIFFUSE_LIGHT };

    MaterialData aluminum = { .albedo = { 0.8f, 0.85f, 0.88f}, .fuzz = 0.0f, .type = MATERIAL_TYPE_METAL };

    addQuad({ 55.5f,   .0f,    .0f }, {    .0f, 55.5f, .0f }, { .0f,   .0f, -55.5f }, green);
    addQuad({   .0f,   .0f,    .0f }, {    .0f, 55.5f, .0f }, { .0f,   .0f, -55.5f },   red);
    addQuad({ 34.3f, 55.4f, -33.2f }, { -13.0f,   .0f, .0f }, { .0f,   .0f,  10.5f }, light, true);
    addQuad({   .0f,   .0f,    .0f }, {  55.5f,   .0f, .0f }, { .0f,   .0f, -55.5f }, white);
    addQuad({ 55.5f, 55.5f, -55.5f }, { -55.5f,   .0f, .0f }, { .0f,   .0f,  55.5f }, white);
    addQuad({   .0f,   .0f, -55.5f }, {  55.5f,   .0f, .0f }, { .0f, 55.5f,    .0f }, white);

    addBox({  13.0f,   .0f,  -6.5f }, {  29.5f, 16.5f, -23.0f}, white, 0,  15, 0);
    addBox({  26.5f,   .0f, -29.5f }, {  43.0f, 33.0f, -46.0f}, aluminum, 0, -18, 0, true);
}

void setupSceneCornellBoxGlassSphere(float defocusAngle, bool isGlassSphereALight)
{
    // Make everything 10x smaller than in article so its easier to navigate...
    cameraData = { 
        .lookfrom = { 27.8f, 27.8f, 80.0f }, 
        .lookat = { 27.8f, 27.8f, 0 }, 
        .backgroundColor = { 0, 0, 0 }, 
        .vfov = 40, 
        .focusDist = 10.0f, 
        .defocusAngle = defocusAngle, 
        .samplesPerPixel = 16,
        .doStratify = false
    };

    MaterialData red   = { .albedo = { .65f, .05f, .05f}, .type = MATERIAL_TYPE_LAMBERTIAN };
    MaterialData white = { .albedo = { .73f, .73f, .73f}, .type = MATERIAL_TYPE_LAMBERTIAN };
    MaterialData green = { .albedo = { .12f, .45f, .15f}, .type = MATERIAL_TYPE_LAMBERTIAN };
    MaterialData light = { .albedo = {   15,   15,   15}, .type = MATERIAL_TYPE_DIFFUSE_LIGHT };

    MaterialData glass = { .albedo = { 1, 1, 1}, .fuzz = 0.0f, .refractionIndex = 1.5f, .type = MATERIAL_TYPE_DIELECTRIC };

    addQuad({ 55.5f,   .0f,    .0f }, {    .0f, 55.5f, .0f }, { .0f,   .0f, -55.5f }, green);
    addQuad({   .0f,   .0f,    .0f }, {    .0f, 55.5f, .0f }, { .0f,   .0f, -55.5f },   red);
    addQuad({ 34.3f, 55.4f, -33.2f }, { -13.0f,   .0f, .0f }, { .0f,   .0f,  10.5f }, light, true);
    addQuad({   .0f,   .0f,    .0f }, {  55.5f,   .0f, .0f }, { .0f,   .0f, -55.5f }, white);
    addQuad({ 55.5f, 55.5f, -55.5f }, { -55.5f,   .0f, .0f }, { .0f,   .0f,  55.5f }, white);
    addQuad({   .0f,   .0f, -55.5f }, {  55.5f,   .0f, .0f }, { .0f, 55.5f,    .0f }, white);


    addSphere({19,9,-19}, 9, glass, isGlassSphereALight);
    addBox({  26.5f,   .0f, -29.5f }, {  43.0f, 33.0f, -46.0f}, white, 0, -18, 0);
}

void setupSceneCornellBoxMetalBoxGlassSphere()
{
    // Make everything 10x smaller than in article so its easier to navigate...
    cameraData = { 
        .lookfrom = { 27.8f, 27.8f, 80.0f }, 
        .lookat = { 27.8f, 27.8f, 0 }, 
        .backgroundColor = { 0, 0, 0 }, 
        .vfov = 40, 
        .focusDist = 10.0f, 
        .defocusAngle = 0.0f, 
        .samplesPerPixel = 16,
        .doStratify = false
    };

    MaterialData red   = { .albedo = { .65f, .05f, .05f}, .type = MATERIAL_TYPE_LAMBERTIAN };
    MaterialData white = { .albedo = { .73f, .73f, .73f}, .type = MATERIAL_TYPE_LAMBERTIAN };
    MaterialData green = { .albedo = { .12f, .45f, .15f}, .type = MATERIAL_TYPE_LAMBERTIAN };
    MaterialData light = { .albedo = {   15,   15,   15}, .type = MATERIAL_TYPE_DIFFUSE_LIGHT };
    MaterialData aluminum = { .albedo = { 0.8f, 0.85f, 0.88f}, .fuzz = 0.0f, .type = MATERIAL_TYPE_METAL };
    MaterialData glass = { .albedo = { 1, 1, 1}, .fuzz = 0.0f, .refractionIndex = 1.5f, .type = MATERIAL_TYPE_DIELECTRIC };

    addQuad({ 55.5f,   .0f,    .0f }, {    .0f, 55.5f, .0f }, { .0f,   .0f, -55.5f }, green);
    addQuad({   .0f,   .0f,    .0f }, {    .0f, 55.5f, .0f }, { .0f,   .0f, -55.5f },   red);
    addQuad({ 34.3f, 55.4f, -33.2f }, { -13.0f,   .0f, .0f }, { .0f,   .0f,  10.5f }, light, true);
    addQuad({   .0f,   .0f,    .0f }, {  55.5f,   .0f, .0f }, { .0f,   .0f, -55.5f }, white);
    addQuad({ 55.5f, 55.5f, -55.5f }, { -55.5f,   .0f, .0f }, { .0f,   .0f,  55.5f }, white);
    addQuad({   .0f,   .0f, -55.5f }, {  55.5f,   .0f, .0f }, { .0f, 55.5f,    .0f }, white);


    addSphere({19,9,-19}, 9, glass, true);
    addBox({  26.5f,   .0f, -29.5f }, {  43.0f, 33.0f, -46.0f}, aluminum, 0, -18, 0, true);
}

void setupSceneFinal2(float defocusAngle)
{
    cameraData = {
        .lookfrom = { 478, 278, 600 },
        .lookat = { 278, 278, 0 },
        .backgroundColor = { 0, 0, 0 },
        .vfov = 40,
        .focusDist = 10.0f,
        .defocusAngle = defocusAngle,
        .samplesPerPixel = 16,
        .doStratify = false
    };

    MaterialData ground   = { .albedo = { 0.48f, 0.83f, 0.53f}, .type = MATERIAL_TYPE_LAMBERTIAN };

    int boxes_per_side = 20;
    for (int i = 0; i < boxes_per_side; i++) {
        for (int j = 0; j < boxes_per_side; j++) {
            auto w = 100.0f;
            auto x0 = -1000.0f + i*w;
            auto z0 = -1000.0f + j*w;
            auto y0 = 0.0f;
            auto x1 = x0 + w;
            auto y1 = random_float(1,101);
            auto z1 = z0 + w;

            addBox({ x0,y0,-z0 }, { x1, y1, -z1 }, ground);
        }
    }

    MaterialData light = { .albedo = {   7,   7,   7}, .type = MATERIAL_TYPE_DIFFUSE_LIGHT };
    addQuad({ 123, 554, -147 }, { 300, 0, 0 }, { 0,   0, -265 }, light, true);

    // No motion blur for this one.
    MaterialData sphereMaterial1 = { .albedo = { .7f, .3f, .1f}, .type = MATERIAL_TYPE_LAMBERTIAN };
    addSphere({ 400, 400, -200 }, 50, sphereMaterial1);

    MaterialData sphereMaterial2 =   { .albedo = { 1.0f, 1.0f, 1.0f}, .refractionIndex = 1.5f, .type = MATERIAL_TYPE_DIELECTRIC };
    addSphere({ 260, 150, -45 }, 50, sphereMaterial2);

    MaterialData sphereMaterial3 =   { .albedo = { .8f, .8f, .9f}, .fuzz = 1, .type = MATERIAL_TYPE_METAL };
    addSphere({ 0, 150, -145 }, 50, sphereMaterial3);

    addSphere({ 360, 150, -145 }, 70, sphereMaterial2);
    MaterialData sphereMaterial4 = { .albedo = { 0.2f, 0.4f, 0.9f}, .density = 0.2f, .type = MATERIAL_TYPE_SMOKE };
    addSphere({ 360, 150, -145 }, 69.9f, sphereMaterial4);


    addSphere({ 0, 0, 0 }, 5000, sphereMaterial2);
    MaterialData sphereMaterial5 = { .albedo = { 1, 1, 1}, .density = 0.0002f, .type = MATERIAL_TYPE_SMOKE };
    addSphere({ 0, 0, 0 }, 4999, sphereMaterial5);

    // No world texturing for this one.
    MaterialData sphereMaterial6 = { .albedo = { 0.05f, 0.1f, 0.225f}, .type = MATERIAL_TYPE_LAMBERTIAN };
    addSphere({ 400, 200, -400 }, 100, sphereMaterial6);

    // No noise texturing for this one.
    addSphere({ 220, 280, -300 }, 80, sphereMaterial3);

    MaterialData sphereMaterial7 = { .albedo = { .73f, .73f, .73f}, .type = MATERIAL_TYPE_LAMBERTIAN };
    int ns = 1000;
    for (int j = 0; j < ns; j++) {
        // No rotation here , I'm too lazy.
        addSphere({ 165 * random_float() -100, 165 * random_float() +270, 165 * random_float() -395 }, 10, sphereMaterial7);
    }
}

void SetupNextScene()
{
    static UINT scene = 3;
    scene = (scene + 1) % 16;

    // Reset.
    proceduralInstances.clear();
    objectList.clear();
    lightsList.clear();
    autoAdaptSamplesCount = false;

    switch (scene)
    {
    case 0: setupSceneBasic(0.0f); break;
    case 1: setupSceneBasic(10.0f); break;
    case 2: setupSceneBasicClose(0.0f); break;
    case 3: setupSceneBasicClose(10.0f); break;
    case 4: setupSceneFinal(0.0f, false); break;
    case 5: setupSceneFinal(0.0f, true); break;
    case 6: setupSceneFinal(0.6f, false); break;
    case 7: setupSceneQuads(0.0f); break;
    case 8: setupSceneCornellBox(0.0f); break;
    case 9: setupSceneCornellBoxSmoke(0.0f); break;
    case 10: setupSceneCornellBoxGlass(0.0f); break;
    case 11: setupSceneCornellBoxMetal(0.0f); break;
    case 12: setupSceneCornellBoxGlassSphere(0.0f, false); break;
    case 13: setupSceneCornellBoxGlassSphere(0.0f, true); break;
    case 14: setupSceneCornellBoxMetalBoxGlassSphere(); break;
    case 15: setupSceneFinal2(0.0f); break;
    }
}