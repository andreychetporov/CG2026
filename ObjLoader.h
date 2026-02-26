#pragma once

#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

// Must match the Vertex layout in D3DApp.h
struct Vertex
{
    float pos[3];
    float normal[3];
    float color[4];  // rgba — will be set to a default if not in OBJ
};

struct MeshData
{
    std::vector<Vertex>   vertices;
    std::vector<uint32_t> indices;   // uint32 to support large meshes
};

// -----------------------------------------------------------------------
//  Minimal Wavefront OBJ loader
//  Supports: v, vn, f  (triangles and simple quads → triangulated)
//  Does NOT require vt (UVs). Normals are used if present; otherwise
//  computed per-face (flat shading).
// -----------------------------------------------------------------------
inline MeshData LoadOBJ(const std::string& path, float r = 0.8f, float g = 0.8f, float b = 0.8f)
{
    std::ifstream file(path);
    if (!file.is_open())
        throw std::runtime_error("ObjLoader: cannot open file: " + path);

    std::vector<float[3]> rawPos;
    std::vector<float[3]> rawNorm;

    // Temporary storage (dynamic arrays)
    struct Vec3 { float x, y, z; };
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;

    // Raw faces: each entry is a list of (posIdx, normIdx) pairs
    // normIdx == -1 means no normal in file
    struct FaceVert { int p, n; };
    std::vector<std::vector<FaceVert>> faces;

    std::string line;
    while (std::getline(file, line))
    {
        if (line.empty() || line[0] == '#') continue;

        std::istringstream ss(line);
        std::string token;
        ss >> token;

        if (token == "v")
        {
            Vec3 v{};
            ss >> v.x >> v.y >> v.z;
            positions.push_back(v);
        }
        else if (token == "vn")
        {
            Vec3 v{};
            ss >> v.x >> v.y >> v.z;
            normals.push_back(v);
        }
        else if (token == "f")
        {
            // Each face token can be:  p   p/t   p/t/n   p//n
            std::vector<FaceVert> face;
            std::string faceToken;
            while (ss >> faceToken)
            {
                FaceVert fv{ 0, -1 };
                // parse p[/t[/n]] or p//n
                size_t s1 = faceToken.find('/');
                fv.p = std::stoi(faceToken.substr(0, s1)) - 1; // OBJ is 1-based

                if (s1 != std::string::npos)
                {
                    size_t s2 = faceToken.find('/', s1 + 1);
                    // skip texcoord index
                    if (s2 != std::string::npos && s2 > s1 + 1)
                    {
                        // there IS a normal index
                        fv.n = std::stoi(faceToken.substr(s2 + 1)) - 1;
                    }
                    else if (s2 != std::string::npos)
                    {
                        // p//n
                        fv.n = std::stoi(faceToken.substr(s2 + 1)) - 1;
                    }
                }
                face.push_back(fv);
            }
            if (face.size() >= 3)
                faces.push_back(face);
        }
    }

    // ----------------------------------------------------------------
    // Build vertex/index buffers
    // We use a map to de-duplicate identical (posIdx, normIdx) combos.
    // ----------------------------------------------------------------
    MeshData mesh;

    // key: "posIdx_normIdx"
    std::unordered_map<std::string, uint32_t> vertCache;

    auto getOrAdd = [&](int pi, int ni, const Vec3& flatNorm) -> uint32_t
        {
            std::string key = std::to_string(pi) + "_" + std::to_string(ni);
            auto it = vertCache.find(key);
            if (it != vertCache.end()) return it->second;

            Vertex vert{};
            vert.pos[0] = positions[pi].x;
            vert.pos[1] = positions[pi].y;
            vert.pos[2] = positions[pi].z;

            if (ni >= 0 && ni < (int)normals.size())
            {
                vert.normal[0] = normals[ni].x;
                vert.normal[1] = normals[ni].y;
                vert.normal[2] = normals[ni].z;
            }
            else
            {
                vert.normal[0] = flatNorm.x;
                vert.normal[1] = flatNorm.y;
                vert.normal[2] = flatNorm.z;
            }

            vert.color[0] = r;
            vert.color[1] = g;
            vert.color[2] = b;
            vert.color[3] = 1.0f;

            uint32_t idx = (uint32_t)mesh.vertices.size();
            mesh.vertices.push_back(vert);
            vertCache[key] = idx;
            return idx;
        };

    for (auto& face : faces)
    {
        // Compute flat normal for this face (used if OBJ has no normals)
        Vec3 flatNorm{ 0, 1, 0 };
        if (face.size() >= 3)
        {
            auto& p0 = positions[face[0].p];
            auto& p1 = positions[face[1].p];
            auto& p2 = positions[face[2].p];

            float ax = p1.x - p0.x, ay = p1.y - p0.y, az = p1.z - p0.z;
            float bx = p2.x - p0.x, by = p2.y - p0.y, bz = p2.z - p0.z;

            flatNorm.x = ay * bz - az * by;
            flatNorm.y = az * bx - ax * bz;
            flatNorm.z = ax * by - ay * bx;

            float len = sqrtf(flatNorm.x * flatNorm.x + flatNorm.y * flatNorm.y + flatNorm.z * flatNorm.z);
            if (len > 1e-6f) { flatNorm.x /= len; flatNorm.y /= len; flatNorm.z /= len; }
        }

        // Fan-triangulate the face (works for convex polygons)
        uint32_t i0 = getOrAdd(face[0].p, face[0].n, flatNorm);
        for (size_t i = 1; i + 1 < face.size(); ++i)
        {
            uint32_t i1 = getOrAdd(face[i].p, face[i].n, flatNorm);
            uint32_t i2 = getOrAdd(face[i + 1].p, face[i + 1].n, flatNorm);
            mesh.indices.push_back(i0);
            mesh.indices.push_back(i1);
            mesh.indices.push_back(i2);
        }
    }

    if (mesh.vertices.empty() || mesh.indices.empty())
        throw std::runtime_error("ObjLoader: no geometry found in: " + path);

    return mesh;
}