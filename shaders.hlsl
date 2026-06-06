
#include "shaders_helpers.hlsli"

[shader("raygeneration")]
void RayGeneration()
{
    const float2 size = DispatchRaysDimensions().xy;
    const uint2 idx = DispatchRaysIndex().xy;
    // Camera
    const float3 lookfrom = g_camera.lookfrom;
    const float3 lookat = g_camera.lookat;
    const float focusDist = g_camera.focusDist;
    const float defocusAngle = g_camera.defocusAngle;
    const float vfow = g_camera.vfov;
    const float3 vup = float3(0, 1, 0);
    
    const float3 direction = lookfrom - lookat;
    
    // Determine viewport dimensions.
    const float theta = DegreesToRadians(vfow);
    const float h = tan(theta / 2);
    const float viewport_height = 2 * h * focusDist;
    const float viewport_width = viewport_height * (size.x / size.y);
    
     // Calculate the u,v,w unit basis vectors for the camera coordinate frame.
    const float3 w = normalize(direction);
    const float3 u = normalize(cross(vup, w));
    const float3 v = cross(w, u);

    // Calculate the vectors across the horizontal and down the vertical viewport edges.
    const float3 viewport_u = viewport_width * -u; // Vector across viewport horizontal edge
    const float3 viewport_v = viewport_height * -v; // Vector down viewport vertical edge

    // Calculate the horizontal and vertical delta vectors from pixel to pixel.
    const float3 pixel_delta_u = viewport_u / size.x;
    const float3 pixel_delta_v = viewport_v / size.y;

    // Calculate the location of the upper left pixel.
    const float3 viewport_upper_left = lookfrom - focusDist * w - viewport_u / 2 - viewport_v / 2;
    const float3 pixel00_loc = viewport_upper_left + 0.5 * (pixel_delta_u + pixel_delta_v);
    
    // Calculate the camera defocus disk basis vectors.
    const float defocus_radius = focusDist * tan(DegreesToRadians(defocusAngle / 2));
    const float3 defocus_disk_u = u * defocus_radius;
    const float3 defocus_disk_v = v * defocus_radius;
    
    const uint randomSeedGlob = FrameSetupSeed();
    
    // Used for stratification.
    const uint sqrtSpp = uint(sqrt(g_camera.samplesPerPixel));
    
    const uint numSamplesX = (g_camera.doStratify) ? sqrtSpp : g_camera.samplesPerPixel;
    const uint numSamplesY = (g_camera.doStratify) ? sqrtSpp : 1;
    
    float3 accumulatedColor = 0;
    for (uint sampleY = 0; sampleY < numSamplesY; ++sampleY)
    {
        for (uint sampleX = 0; sampleX < numSamplesX; ++sampleX)
        {
            uint randomSeed = SetupSeed(SetupSeed(randomSeedGlob, sampleX), sampleY);
            const float idxShiftX = (g_camera.doStratify) 
                                            ? ((sampleX + RandomFloat(randomSeed)) / sqrtSpp) - 0.5
                                            : RandomFloat(randomSeed, -0.5, 0.5);
            const float idxShiftY = (g_camera.doStratify)
                                            ? ((sampleY + RandomFloat(randomSeed)) / sqrtSpp) - 0.5
                                            : RandomFloat(randomSeed, -0.5, 0.5);
        
            const float2 shiftedIdx = float2(idx.x + idxShiftX, idx.y + idxShiftY);
            const float3 pixelSample = pixel00_loc + (shiftedIdx.x * pixel_delta_u) + (shiftedIdx.y * pixel_delta_v);

            float3 rayOrigin = lookfrom;
            if (defocusAngle > 0)
            {
                const float2 diskPoint = RandomInUnitDisk(randomSeed);
                rayOrigin += diskPoint.x * defocus_disk_u + diskPoint.y * defocus_disk_v;
            }
        
            const float3 rayDirection = pixelSample - rayOrigin;
        
            Payload payload;
        
            RayDesc ray;
            ray.Origin = rayOrigin;
            ray.Direction = rayDirection;
            ray.TMin = 0.001;
            ray.TMax = 1000;
        
            float3 gatheredAttenuation = float3(1, 1, 1);
            float3 lastColor;
            uint remainingReflections = 256;
            while (true)
            {
                if (remainingReflections == 0 || length(gatheredAttenuation) < 0.0001)
                {
                    lastColor = float3(0, 0, 0);
                    break;
                }
            
                payload.missed = false;
                payload.seed = randomSeed;
            
                randomSeedBuffer[0] = randomSeed; // This one is used in intersection shader. What will happen is that all the instances
                                                  // of raygen will put their data into this structure all over again in random manner (due
                                                  // to data races) which is perfectly fine and random enough for me.

                TraceRay(g_scene, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);
            
                randomSeed = payload.seed;

                if (payload.missed)
                {
                    // Missed or fully absorbed or emits light.
                    lastColor = payload.color;
                    break;
                }

                // Reflection.
                ray.Direction = normalize(payload.scatterDirection);
                ray.Origin = payload.p + ray.Direction * 0.001;
                    
                if (payload.skipPdf)
                {
                    payload.pdfScatter = 1.0f;
                    payload.pdfValue = 1.0f;
                }
                
                // pdfValue is the PDF of the distribution we actually sampled from, so for a
                // validly sampled direction it must be strictly positive and finite. If a
                // degenerate sphere-light PDF still makes it non-positive or non-finite (e.g. the
                // discriminant oscillating around 0 producing a 0/0, or a vanishingly small solid
                // angle), the path's Monte Carlo weight is taken as 0 and the path dies on the next
                // loop iteration via the gatheredAttenuation check above. The comparison also handles
                // NaN (NaN > 0 is false) and Inf (x / Inf == 0) for free. The previously problematic
                // inside-the-sphere NaN is now fixed at its source in RandomToSphere and
                // HittablePDFValue, which fall back to uniform sphere sampling when the origin is
                // inside the light sphere (e.g. overlapping spheres in the final scene of part I).
                float pdfRatio = (payload.pdfValue > 0.0f) ? payload.pdfScatter / payload.pdfValue : 0.0f;

                gatheredAttenuation *= payload.color * pdfRatio;
                --remainingReflections;
            }

            accumulatedColor += gatheredAttenuation * lastColor;
        }
    }

    uav[idx] = float4(sqrt(accumulatedColor / (numSamplesX * numSamplesY)), 1);
}

[shader("intersection")]
void IntersectionProceduralSphere()
{
    float enterT;
    float exitT; // unused here
    ProceduralPrimitiveAttributes attr;
    if (IntersectionProceduralSphere(ObjectRayOrigin(), ObjectRayDirection(), false, enterT, exitT, attr))
    {
        ReportHit(enterT, 0, attr);
    }
}

[shader("intersection")]
void IntersectionProceduralQuad()
{
    float enterT;
    ProceduralPrimitiveAttributes attr;
    if (IntersectionProceduralQuad(ObjectRayOrigin(), ObjectRayDirection(), enterT, attr))
    {
        ReportHit(enterT, 0, attr);
    }
}

[shader("intersection")]
void IntersectionProceduralSmokeSphere()
{
    float enterT;
    float exitT;
    ProceduralPrimitiveAttributes attr;
    if (IntersectionProceduralSphere(ObjectRayOrigin(), ObjectRayDirection(), true, enterT, exitT, attr))
    {
        float distanceInsideBoundary = exitT - enterT;
        float negInvDensity = -1 / g_objects[NonUniformResourceIndex(InstanceID())].material.density;
        uint seed = SetupSeed(FrameSetupSeed(), randomSeedBuffer[0]);
        float hitDistance = negInvDensity * log(RandomFloat(seed));
        
        if (hitDistance <= distanceInsideBoundary)
        {
            attr.normal = float3(0, 0, 1); // These two won't be used anyway
            attr.front_face = true; // by the smoke material shader.
            ReportHit(enterT + hitDistance, 0, attr);
        }
    }
}

[shader("intersection")]
void IntersectionProceduralGlassCube()
{
    float enterT;
    float exitT;
    ProceduralPrimitiveAttributes attr;
    if (IntersectionProceduralCube(ObjectRayOrigin(), ObjectRayDirection(), false, enterT, exitT, attr))
    {
        ReportHit(enterT, 0, attr);
    }
}

[shader("intersection")]
void IntersectionProceduralSmokeCube()
{
    float enterT;
    float exitT;
    ProceduralPrimitiveAttributes attr;
    if (IntersectionProceduralCube(ObjectRayOrigin(), ObjectRayDirection(), true, enterT, exitT, attr))
    {
        float distanceInsideBoundary = exitT - enterT;
        float negInvDensity = -1 / g_objects[NonUniformResourceIndex(InstanceID())].material.density;
        uint seed = SetupSeed(FrameSetupSeed(), randomSeedBuffer[0]);
        float hitDistance = negInvDensity * log(RandomFloat(seed));
        
        if (hitDistance <= distanceInsideBoundary)
        {
            ReportHit(enterT + hitDistance, 0, attr);
        }
    }
}

[shader("closesthit")]
void ClosestHitProceduralLambertian(
                            inout Payload payload,
                            ProceduralPrimitiveAttributes attrib)
{
    float3 normal = attrib.normal;
    payload.color = g_objects[NonUniformResourceIndex(InstanceID())].material.albedo.xyz;
    payload.p = WorldRayOrigin() + RayTCurrent() * WorldRayDirection();
    payload.missed = false;
    
    payload.scatterDirection = MixedCosineHittablePDFGenerate(normal, payload.p, payload.scatterDirection, payload.seed);
    payload.pdfValue = MixedCosineHittablePDFValue(normal, payload.p, payload.scatterDirection);
    
    payload.pdfScatter = CosinePDFValue(normal, payload.scatterDirection);
    payload.skipPdf = false;
}

[shader("closesthit")]
void ClosestHitProceduralMetal(
                            inout Payload payload,
                            ProceduralPrimitiveAttributes attrib)
{
    payload.color = g_objects[NonUniformResourceIndex(InstanceID())].material.albedo.xyz;
    payload.p = WorldRayOrigin() + RayTCurrent() * WorldRayDirection();
    payload.scatterDirection = normalize(reflect(WorldRayDirection(), attrib.normal));
    payload.scatterDirection += g_objects[NonUniformResourceIndex(InstanceID())].material.fuzz * RandomUnitVector(payload.seed);
    payload.missed = false;
    
    payload.skipPdf = true;
}

[shader("closesthit")]
void ClosestHitProceduralDielectric(
                            inout Payload payload,
                            ProceduralPrimitiveAttributes attrib)
{
    payload.color = g_objects[NonUniformResourceIndex(InstanceID())].material.albedo.xyz;
    payload.p = WorldRayOrigin() + RayTCurrent() * WorldRayDirection();
    
    const float refractionIndex = g_objects[NonUniformResourceIndex(InstanceID())].material.refractionIndex;
    const float ri = attrib.front_face ? (1.0 / refractionIndex) : refractionIndex;
        
    const float3 unitDirection = normalize(WorldRayDirection());
    const float cosTheta = min(dot(-unitDirection, attrib.normal), 1.0);
    const float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    const bool cannotRefract = ri * sinTheta > 1.0;
        
    float3 direction;
    if (cannotRefract || Reflectance(cosTheta, ri) > RandomFloat(payload.seed))
    {
        direction = reflect(unitDirection, attrib.normal);
    }
    else
    {
        direction = refract(unitDirection, attrib.normal, ri);
    }

    payload.scatterDirection = direction;
    payload.missed = false;
    
    payload.skipPdf = true;
}

[shader("closesthit")]
void ClosestHitProceduralDiffuseLight(
                            inout Payload payload,
                            ProceduralPrimitiveAttributes attrib)
{
    if (attrib.front_face)
    {
        payload.color = g_objects[NonUniformResourceIndex(InstanceID())].material.albedo.xyz;
    }
    else
    {
        payload.color = float3(0, 0, 0);
    }
    
    payload.missed = true;
}

[shader("closesthit")]
void ClosestHitProceduralSmoke(
                            inout Payload payload,
                            ProceduralPrimitiveAttributes attrib)
{
    payload.color = g_objects[NonUniformResourceIndex(InstanceID())].material.albedo.xyz;
    payload.p = WorldRayOrigin() + RayTCurrent() * WorldRayDirection();
    payload.missed = false;
    
    payload.scatterDirection = MixedSphereHittablePDFGenerate(payload.p, payload.scatterDirection, payload.seed);
    payload.pdfValue = MixedSphereHittablePDFValue(payload.p, payload.scatterDirection);
    
    payload.pdfScatter = SpherePDFValue();
    payload.skipPdf = false;
}

[shader("miss")]
void Miss(inout Payload payload)
{
    payload.color = g_camera.backgroundColor;
    payload.missed = true;
}