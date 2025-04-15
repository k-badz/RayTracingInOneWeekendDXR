struct Payload
{
    float3 color;
    float3 p;
    float3 scatterDirection;
    float pdfScatter;
    float pdfValue;
    uint seed;
    bool missed;
    bool skipPdf;
};

struct ProceduralPrimitiveAttributes
{
    float3 normal;
    bool front_face;
};

enum MATERIAL_TYPE {
    MATERIAL_TYPE_LAMBERTIAN = 0,
    MATERIAL_TYPE_METAL = 1,
    MATERIAL_TYPE_DIELECTRIC = 2,
    MATERIAL_TYPE_DIFFUSE_LIGHT = 3,
    MATERIAL_TYPE_SMOKE = 4,
    MATERIAL_TYPE_COUNT
};

enum OBJECT_TYPE {
    OBJECT_TYPE_SPHERE = 0,
    OBJECT_TYPE_QUAD = 1,
    OBJECT_TYPE_VOLUMETRIC_CUBE = 2,
    OBJECT_TYPE_COUNT
};

// Attributes per primitive type.
struct MaterialData
{
    float3 albedo;
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
    float3 Q;
    float3 U;
    float3 V;
    float3 normal;
    
    // Sphere specific.
    float3 center;
    float  radius;
};

struct CameraData
{
    float3 lookfrom;
    float3 lookat;
    float3 backgroundColor;
    float vfov;
    float focusDist;
    float defocusAngle;
    uint frameIndex;
    uint samplesPerPixel;
    uint doStratify;
    uint numLights;
};

RaytracingAccelerationStructure g_scene : register(t0);
StructuredBuffer<ObjectData> g_objects : register(t1);
StructuredBuffer<uint> g_lights : register(t2);
ConstantBuffer<CameraData> g_camera : register(b0);
RWTexture2D<float4> uav : register(u0);
RWStructuredBuffer<uint> randomSeedBuffer : register(u1);

float PI()
{
    return 3.1415926535897932385f;
}

bool HasNaN(float3 vec)
{
    return isnan(vec.x) || isnan(vec.y) || isnan(vec.z);
}

bool HasInf(float3 vec)
{
    return isinf(vec.x) || isinf(vec.y) || isinf(vec.z);
}

// https://github.com/NVIDIAGameWorks/GettingStartedWithRTXRayTracing/blob/master/DXR-RayTracingInOneWeekend/Data/RayTraceInAWeekend/randomUtils.hlsli
uint SetupSeed(uint val0, uint val1, uint backoff = 16)
{
    uint v0 = val0, v1 = val1, s0 = 0;
    for (uint n = 0; n < backoff; n++)
    {
        s0 += 0x9e3779b9;
        v0 += ((v1 << 4) + 0xa341316c) ^ (v1 + s0) ^ ((v1 >> 5) + 0xc8013ea4);
        v1 += ((v0 << 4) + 0xad90777d) ^ (v0 + s0) ^ ((v0 >> 5) + 0x7e95761e);
    }
    return v0;
}
uint FrameSetupSeed(uint backoff = 16)
{
    uint seed = SetupSeed(DispatchRaysIndex().x, DispatchRaysIndex().y, backoff);
    seed = SetupSeed(seed, g_camera.frameIndex, backoff);
    return seed;
}

float RandomFloat(inout uint seed, float minValue = 0.0f, float maxValue = 1.0f)
{
    seed = 1664525 * seed + 1013904223;
    float random = float(seed & 0x00FFFFFF) / float(0x01000000);
    return lerp(minValue, maxValue, random);
}

uint RandomInt(inout uint seed, uint minValue = 0, uint maxValue = 0xFFFFFFFF)
{
    seed = 1664525 * seed + 1013904223;
    return minValue + (seed % (maxValue - minValue + 1));
}

float3 RandomUnitVector(inout uint seed)
{
    while (true)
    {
        float3 p = float3(RandomFloat(seed, -1, 1), RandomFloat(seed, -1, 1), RandomFloat(seed, -1, 1));
        float lensq = dot(p, p);
        if (0.01 < lensq && lensq <= 1)
            return p / sqrt(lensq);
    }
}

float SpherePDFValue()
{
    return 1 / (4 * PI());
}

float3 SpherePDFGenerate(inout uint seed)
{
    return RandomUnitVector(seed);
}

void GetTransformONBAxes(float3 n, out float3 axes[3])
{
    axes[2] = normalize(n);
    float3 a = (abs(axes[2].x) > 0.9) ? float3(0, 1, 0) : float3(1, 0, 0);
    axes[1] = normalize(cross(axes[2], a));
    axes[0] = cross(axes[2], axes[1]);
}

float3 TransformONB(float3 vec, float3 axes[3])
{
    return (vec.x * axes[0]) + (vec.y * axes[1]) + (vec.z * axes[2]);
}

float3 RandomCosineDirection(inout uint seed)
{
    float r1 = RandomFloat(seed);
    float r2 = RandomFloat(seed);

    float phi = 2.0f * PI() * r1;
    float x = cos(phi) * sqrt(r2);
    float y = sin(phi) * sqrt(r2);
    float z = sqrt(1.0f - r2);

    return float3(x, y, z);
}

float3 RandomToSphere(inout uint seed, float radius, float distanceSquared)
{
    float r1 = RandomFloat(seed);
    float r2 = RandomFloat(seed);
    float z = 1 + r2 * (sqrt(1 - radius * radius / distanceSquared) - 1);

    float phi = 2 * PI() * r1;
    float x = cos(phi) * sqrt(1 - z * z);
    float y = sin(phi) * sqrt(1 - z * z);

    return float3(x, y, z);
}

float HittablePDFValue(float3 hittablePdfOrigin, float3 scatterDirection)
{
    float accumulatedPDFValue = 0.0f;
    for (uint light = 0; light < g_camera.numLights; light++)
    {
        uint object = g_lights[light];
        float hittablePDFValue = 0.0f;
        
        // Manual intersection code. I don't see a reason to use the whole acceleration stuff here
        // because we just check one specific geometry which is just a bunch of vector operations.
        if (g_objects[object].type == OBJECT_TYPE_QUAD)
        {
            float3 lightQuadQ = g_objects[object].Q;
            float3 lightQuadU = g_objects[object].U;
            float3 lightQuadV = g_objects[object].V;
    
            float3 lightQuadNormal = cross(lightQuadU, lightQuadV);
            float3 lightQuadW = lightQuadNormal / dot(lightQuadNormal, lightQuadNormal);
            float  lightQuadArea = length(lightQuadNormal);
            lightQuadNormal = normalize(lightQuadNormal);
            float  lightQuadD = dot(lightQuadNormal, lightQuadQ);

            float  lightQuadDenom = dot(lightQuadNormal, scatterDirection);
            
            if (abs(lightQuadDenom) > 1e-8)
            {
                float  lightQuadT = (lightQuadD - dot(lightQuadNormal, hittablePdfOrigin)) / lightQuadDenom;

                float3 intersection = hittablePdfOrigin + lightQuadT * scatterDirection;
                float3 planarHitptVector = intersection - lightQuadQ;
                float alpha = dot(lightQuadW, cross(planarHitptVector, lightQuadV));
                float beta = dot(lightQuadW, cross(lightQuadU, planarHitptVector));
                
                if (0.0f <= alpha && alpha <= 1.0f && 0.0f <= beta && beta <= 1.0f)
                {
                    float distanceSquared = lightQuadT * lightQuadT * dot(scatterDirection, scatterDirection);
                    float cosine = abs(dot(scatterDirection, lightQuadNormal) / length(scatterDirection));

                    hittablePDFValue = distanceSquared / (cosine * lightQuadArea);
                }
            }
        }
        else if (g_objects[object].type == OBJECT_TYPE_SPHERE)
        {
            float3 lightSphereCenter = g_objects[object].center;
            float  lightSphereRadius = g_objects[object].radius;

            float3 oc = lightSphereCenter - hittablePdfOrigin;
            float  a = dot(scatterDirection, scatterDirection);
            float  h = dot(scatterDirection, oc);
            float  distSquared = dot(oc, oc);
            float  c = distSquared - lightSphereRadius * lightSphereRadius;

            float  discriminant = h * h - a * c;
            if (discriminant >= 0)
            {
                float cosThetaMax = sqrt(1 - lightSphereRadius * lightSphereRadius / distSquared);
                float solidAngle = 2 * PI() * (1 - cosThetaMax);

                hittablePDFValue = 1 / solidAngle;
            }
        }
    
        accumulatedPDFValue += hittablePDFValue;
    }

    return (g_camera.numLights > 0) ? accumulatedPDFValue / (float) g_camera.numLights : 0;
}

float3 HittablePDFGenerate(float3 hittablePdfOrigin, inout uint seed)
{
    uint light = RandomInt(seed, 0, g_camera.numLights - 1);
    uint object = g_lights[light];
    
    if (g_objects[object].type == OBJECT_TYPE_QUAD)
    {
        float3 lightQuadQ = g_objects[object].Q;
        float3 lightQuadU = g_objects[object].U;
        float3 lightQuadV = g_objects[object].V;
        return lightQuadQ + (RandomFloat(seed) * lightQuadU) + (RandomFloat(seed) * lightQuadV) - hittablePdfOrigin;
    }
    else if (g_objects[object].type == OBJECT_TYPE_SPHERE)
    {
        float3 lightSphereCenter = g_objects[object].center;
        float  lightSphereRadius = g_objects[object].radius;
        
        float3 oc = lightSphereCenter - hittablePdfOrigin;
        float  distSquared = dot(oc, oc);
        
        float3 onbAxes[3];
        GetTransformONBAxes(oc, onbAxes);
        return TransformONB(RandomToSphere(seed, lightSphereRadius, distSquared), onbAxes);
    }
    
    // We should never reach this point.
    return float3(0, 0, 0);
}

float CosinePDFValue(float3 normal, float3 direction)
{
    float cosineTheta = dot(normalize(direction), normalize(normal));
    return max(0, cosineTheta / PI());
}
float3 CosinePDFGenerate(float3 normal, inout uint seed)
{
    float3 onbAxes[3];
    GetTransformONBAxes(normal, onbAxes);
    return TransformONB(RandomCosineDirection(seed), onbAxes);
}

float MixedCosineHittablePDFValue(float3 normal, float3 hittablePdfOrigin, float3 scatterDirection)
{
    return 0.5f * HittablePDFValue(hittablePdfOrigin, scatterDirection) +
           0.5f * CosinePDFValue(normal, scatterDirection);
}

float3 MixedCosineHittablePDFGenerate(float3 normal, float3 hittablePdfOrigin, float3 scatterDirection, inout uint seed)
{
    if (RandomFloat(seed) < 0.5f)
    {
        return HittablePDFGenerate(hittablePdfOrigin, seed);
    }
    else
    {
        return CosinePDFGenerate(normal, seed);
    }
}

float MixedSphereHittablePDFValue(float3 hittablePdfOrigin, float3 scatterDirection)
{
    return 0.5f * HittablePDFValue(hittablePdfOrigin, scatterDirection) +
           0.5f * SpherePDFValue();
}

float3 MixedSphereHittablePDFGenerate(float3 hittablePdfOrigin, float3 scatterDirection, inout uint seed)
{
    if (RandomFloat(seed) < 0.5f)
    {
        return HittablePDFGenerate(hittablePdfOrigin, seed);
    }
    else
    {
        return SpherePDFGenerate(seed);
    }
}

float DegreesToRadians(float degrees)
{
    return degrees * PI() / 180.0;
}

bool NearZero(float3 direction)
{
        // Return true if the vector is close to zero in all dimensions.
        float s = 1e-7;
        return (abs(direction.x) < s) && (abs(direction.y) < s) && (abs(direction.z) < s);
}

float2 RandomInUnitDisk(inout uint seed)
{
    while (true)
    {
        float2 p = float2(RandomFloat(seed, -1, 1), RandomFloat(seed, -1, 1));
        if (dot(p, p) < 1)
            return p;
    }
}

float3 Refract(float3 uv, float3 n, float3 etai_over_etat)
{
    float cos_theta = min(dot(-uv, n), 1.0);
    float3 r_out_perp = etai_over_etat * (uv + cos_theta * n);
    float3 r_out_parallel = -sqrt(abs(1.0 - dot(r_out_perp, r_out_perp))) * n;
    return r_out_perp + r_out_parallel;
}

float Reflectance(float cosine, float refractionIndex)
{
    // Use Schlick's approximation for reflectance.
    float r0 = (1 - refractionIndex) / (1 + refractionIndex);
    r0 = r0*r0;
    return r0 + (1-r0)*pow((1 - cosine),5);
}

bool IntersectionProceduralSphere(
    float3 rayOrigin, float3 rayDirection, bool isConstantMedium, 
    out float rayTEnter, out float rayTExit, out ProceduralPrimitiveAttributes attr)
{
    float3 sphereCenter = float3(0, 0, 0);
    float sphereRadius = 1.0f;

    float3 oc = rayOrigin - sphereCenter;
    float a = dot(rayDirection, rayDirection);
    float b = 2.0 * dot(oc, rayDirection);
    float c = dot(oc, oc) - sphereRadius * sphereRadius;
    float discriminant = b * b - 4 * a * c;
    
    if (discriminant < 0)
        return false;
    
    float sqrtd = sqrt(discriminant);

    float root1 = (-b - sqrtd) / (2.0 * a);
    float root2 = (-b + sqrtd) / (2.0 * a);
    
    if (root1 > RayTCurrent() || root2 < 0)
        return false;
    
    if (root1 < 0)
    {
        if (isConstantMedium)
        {
            // For volume cases, if we are inside the sphere, then we want to start from the current point.
            root1 = 0;
        }
        else
        {
            // For non-volume cases we want to find the second (exiting) intersection.
            root1 = root2;
        }  
    }

    float t = root1;
        
    float3 p = rayOrigin + t * rayDirection;
    float3 pWorld = mul(float4(p, 1), (float4x3) ObjectToWorld4x3());
    float3 sphereCenterWorld = mul(float4(sphereCenter, 1), (float4x3) ObjectToWorld4x3());
    float3 normal = normalize(pWorld - sphereCenterWorld);
    bool front_face = dot(rayDirection, normal) < 0;
          
    attr.normal = front_face ? normal : -normal;
    attr.front_face = front_face;
    rayTEnter = t;
    
    if (isConstantMedium)
    {
        rayTExit = min(root2, RayTCurrent());
    }

    return true;
}

bool IntersectionProceduralCube(
        float3 rayOrigin, float3 rayDirection, bool isConstantMedium, 
        out float rayTEnter, out float rayTExit, out ProceduralPrimitiveAttributes attr)
{
    // AABB bounds.
    float3 boxMin = float3(-1, -1, -1);
    float3 boxMax = float3(1, 1, 1);
    
    // Calculate ray intersection
    float3 invRayDir = 1.0 / rayDirection;
    float3 t0 = (boxMin - rayOrigin) * invRayDir;
    float3 t1 = (boxMax - rayOrigin) * invRayDir;
    
    float3 tmin = min(t0, t1);
    float3 tmax = max(t0, t1);
    
    float entryT = max(max(tmin.x, tmin.y), tmin.z);
    float exitT = min(min(tmax.x, tmax.y), tmax.z);

    if (entryT > exitT || exitT < 0)
        return false;
    
    if (entryT > RayTCurrent())
        return false;
 
    
    float t = entryT;
    if (t < 0)
    {
        if (isConstantMedium)
        {
            // For volume cases, if we are inside the box, then we want to start from the current point.
            t = 0;
        }
        else
        {
            // For non-volume cases we want to find the second (exiting) intersection.
            t = exitT;
        }
    }

    rayTEnter = t;
    
    if (isConstantMedium)
    {
        rayTExit = min(exitT, RayTCurrent());
    }
    else
    {
        float3 p = rayOrigin + rayDirection * rayTEnter;
        float3 boxVec = p / max(max(abs(p.x), abs(p.y)), abs(p.z));
        float3 normal = float3(
            abs(boxVec.x) > 0.9999 ? sign(boxVec.x) : 0,
            abs(boxVec.y) > 0.9999 ? sign(boxVec.y) : 0,
            abs(boxVec.z) > 0.9999 ? sign(boxVec.z) : 0
        );
        
        bool front_face = dot(rayDirection, normal) < 0;
        normal = normalize(mul(normal, (float3x3) ObjectToWorld4x3()));
    
        attr.normal = front_face ? normal : -normal;
        attr.front_face = front_face;
    }

    return true;
}

bool IntersectionProceduralQuad(float3 rayOrigin, float3 rayDirection, out float rayT, out ProceduralPrimitiveAttributes attr)
{
    // AABB bounds.
    float3 boxMin = float3(-1, -1, -0.00001);
    float3 boxMax = float3(1, 1, 0.00001);
    
    // Calculate ray intersection
    float3 invRayDir = 1.0 / rayDirection;
    float3 t0 = (boxMin - rayOrigin) * invRayDir;
    float3 t1 = (boxMax - rayOrigin) * invRayDir;
    
    float3 tmin = min(t0, t1);
    float3 tmax = max(t0, t1);
    
    float entryT = max(max(tmin.x, tmin.y), tmin.z);
    float exitT = min(min(tmax.x, tmax.y), tmax.z);

    if (entryT > exitT || exitT < 0)
        return false;
 
    float t = max(0, entryT);

    if (t > RayTCurrent())
        return false;
    
    float3 normal = float3(0, 0, -1);
    bool front_face = dot(rayDirection, normal) < 0;
    normal = normalize(mul(normal, (float3x3) ObjectToWorld4x3()));
    
    attr.normal = front_face ? normal : -normal;
    attr.front_face = front_face;
    rayT = t;
    return true;
}