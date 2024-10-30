#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <vector>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include "Camera.h"
#include "FileSystemUtils.h"
#include <random>

// Asset Importer
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <map>

void APIENTRY MessageCallback(GLenum source,
    GLenum type,
    GLuint id,
    GLenum severity,
    GLsizei length,
    const GLchar* message,
    const void* userParam)
{
    std::cerr << "GL CALLBACK: " << (type == GL_DEBUG_TYPE_ERROR ? "** GL ERROR **" : "")
        << " type = " << type
        << ", severity = " << severity
        << ", message = " << message << std::endl;
}

// Constants and global variables
const int WIDTH = 1600;
const int HEIGHT = 900;
float lastX = WIDTH / 2.0f;
float lastY = HEIGHT / 2.0f;
bool firstMouse = true;
float deltaTime = 0.0f; // Time between current frame and last frame
float lastFrame = 0.0f; // Time of last frame
double previousTime = 0.0;
int frameCount = 0;
bool useSSBump = true; // Flag to toggle between SSBump and Normal Map shaders
bool keyPressed = false; // To handle single key press events

Camera camera(glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), -180.0f, 0.0f, 6.0f, 0.1f, 45.0f, 0.1f, 500.0f);

const char* LightMappingShaderSource = R"(
    #version 430 core
    layout (location = 0) in vec3 aPos;
    layout (location = 1) in vec3 aNormal;
    layout (location = 2) in vec2 aTexCoords;
    layout (location = 3) in vec2 aLightmapTexCoords;
    layout (location = 4) in vec3 aTangent;
    layout (location = 5) in vec3 aBitangent;

    out vec2 TexCoords;
    out vec2 LightmapTexCoords;
    out vec3 FragPos;
    out mat3 TBN;

    uniform mat4 model;
    uniform mat4 view;
    uniform mat4 projection;

    void main() {
        TexCoords = aTexCoords;
        LightmapTexCoords = aLightmapTexCoords;
        FragPos = vec3(model * vec4(aPos, 1.0));

        mat3 modelMatrix3x3 = mat3(model);
        vec3 T = normalize(modelMatrix3x3 * aTangent);
        vec3 B = normalize(modelMatrix3x3 * aBitangent);
        vec3 N = normalize(mat3(transpose(inverse(model))) * aNormal);

        // Orthonormalize T and B
        T = normalize(T - dot(T, N) * N);
        B = cross(N, T);

        TBN = mat3(T, B, N);

        gl_Position = projection * view * vec4(FragPos, 1.0);
    }
)";

const char* DirectionalLightmapSSBumpFragment = R"(
    #version 430 core
    out vec4 FragColor;

    in vec2 TexCoords;
    in vec2 LightmapTexCoords;
    in vec3 FragPos;
    in mat3 TBN;

    uniform sampler2D diffuseTexture;
    uniform sampler2D ssbumpMap;
    uniform sampler2D lightmap0;
    uniform sampler2D lightmap1;
    uniform sampler2D lightmap2;
    uniform samplerCube environmentMap;

    uniform vec3 viewPos;

    uniform bool renderLightmapOnly;

    uniform float bumpStrength;
    uniform float specularIntensity;
    uniform float shininess;

    void main()
    {
        if (renderLightmapOnly) {
            vec3 lm0 = texture(lightmap0, LightmapTexCoords).rgb;
            FragColor = vec4(lm0, 1.0);
            return;
        }

        // --- Extract basis vectors from TBN matrix ---
        vec3 basis0 = normalize(TBN[0]); // Tangent
        vec3 basis1 = normalize(TBN[1]); // Bitangent
        vec3 basis2 = normalize(TBN[2]); // Normal

        // --- Sample and process SSBUMP map ---
        vec3 ssbump = texture(ssbumpMap, TexCoords).rgb;
        ssbump.g = 1.0 - ssbump.g;  // Flip green channel for DirectX to OpenGL conversion
        // The Z-flip is only needed for SSBump maps because they work with basis coefficients rather than direct normal vectors.
        ssbump.b = 1.0 - ssbump.b;
    
        // Convert from [0,1] to [-1,1] range
        ssbump = ssbump * 2.0 - 1.0;

        // Create default normal coefficients (no bump)
        vec3 defaultCoeffs = vec3(0.0, 0.0, 1.0);
    
        // Blend between default and SSBUMP coefficients based on bump strength
        vec3 finalCoeffs = normalize(mix(defaultCoeffs, ssbump, bumpStrength));

        // --- Construct the normal using the blended coefficients ---
        vec3 N = normalize(
            basis0 * finalCoeffs.x +
            basis1 * finalCoeffs.y +
            basis2 * finalCoeffs.z
        );

        // --- View Vector ---
        vec3 V = normalize(viewPos - FragPos);

        // --- Reflection Vector for Environment Map ---
        vec3 R = reflect(-V, N);
        vec3 reflectionColor = texture(environmentMap, R).rgb;

        // --- Sample RNM Lightmaps ---
        vec3 lm0 = texture(lightmap0, LightmapTexCoords).rgb;
        vec3 lm1 = texture(lightmap1, LightmapTexCoords).rgb;
        vec3 lm2 = texture(lightmap2, LightmapTexCoords).rgb;

        // --- Compute Luminance of Each Lightmap Sample ---
        float lum0 = dot(lm0, vec3(0.2126, 0.7152, 0.0722));
        float lum1 = dot(lm1, vec3(0.2126, 0.7152, 0.0722));
        float lum2 = dot(lm2, vec3(0.2126, 0.7152, 0.0722));

        // --- Compute lighting coefficients using the blended normal ---
        float c0 = max(0.0, dot(N, basis0));
        float c1 = max(0.0, dot(N, basis1));
        float c2 = max(0.0, dot(N, basis2));

        // --- Calculate per-basis lighting contribution ---
        vec3 lighting0 = lm0 * c0;
        vec3 lighting1 = lm1 * c1;
        vec3 lighting2 = lm2 * c2;

        // --- Combine lighting from all bases ---
        vec3 diffuseLighting = lighting0 + lighting1 + lighting2;

        // --- Compute Dominant Light Direction ---
        vec3 dominantDir = normalize(
            basis0 * lum0 +
            basis1 * lum1 +
            basis2 * lum2
        );

        // --- Fetch Diffuse Color and Alpha (Specular Mask) ---
        vec4 albedoWithAlpha = texture(diffuseTexture, TexCoords);
        vec3 albedo = albedoWithAlpha.rgb;
        float specularMask = albedoWithAlpha.a;

        // --- Calculate Diffuse Component ---
        vec3 diffuse = albedo * diffuseLighting;

        // --- Specular Lighting Calculation ---
        vec3 H = normalize(V + dominantDir);
        float NdotH = max(dot(N, H), 0.0);
        float specularFactor = pow(NdotH, shininess);

        // Compute irradiance in dominant direction for specular
        float s0 = max(dot(dominantDir, basis0), 0.0);
        float s1 = max(dot(dominantDir, basis1), 0.0);
        float s2 = max(dot(dominantDir, basis2), 0.0);

        vec3 lightColor = lm0 * s0 + lm1 * s1 + lm2 * s2;

        // Apply specular mask to the specular intensity
        float maskedSpecularIntensity = specularIntensity * specularMask;

        // Calculate Specular Component
        vec3 specular = lightColor * specularFactor * maskedSpecularIntensity;

        // --- Apply Specular Mask to Reflection ---
        float reflectionIntensity = 0.25;
        vec3 maskedReflection = reflectionColor * reflectionIntensity * specularMask;

        // --- Combine Diffuse, Specular, and Reflection ---
        vec3 finalColor = diffuse + specular + maskedReflection;

        FragColor = vec4(finalColor, 1.0);
    }
)";

        const char* DirectionalLightmapNormalBumpFragmentShader = R"(
    #version 430 core
    out vec4 FragColor;

    in vec2 TexCoords;
    in vec2 LightmapTexCoords;
    in vec3 FragPos;
    in mat3 TBN;

    uniform sampler2D diffuseTexture;
    uniform sampler2D normalMap;
    uniform sampler2D lightmap0;
    uniform sampler2D lightmap1;
    uniform sampler2D lightmap2;
    uniform samplerCube environmentMap;

    uniform vec3 viewPos;

    uniform bool renderLightmapOnly;

    uniform float specularIntensity;
    uniform float shininess;
    uniform float bumpStrength; // Added bumpStrength uniform

    void main()
    {
        if (renderLightmapOnly) {
            vec3 lm0 = texture(lightmap0, LightmapTexCoords).rgb;
            FragColor = vec4(lm0, 1.0);
            return;
        }

        // --- Normal Mapping ---
        vec3 tangentNormal = texture(normalMap, TexCoords).rgb;
        tangentNormal.g = 1.0 - tangentNormal.g; // Flip Y for DirectX to OpenGL conversion
        tangentNormal = normalize(tangentNormal * 2.0 - 1.0);

        // Blend between the default normal and the normal map based on bumpStrength
        vec3 baseNormal = normalize(TBN[2]); // Default normal from TBN matrix
        vec3 mappedNormal = normalize(TBN * tangentNormal);
        vec3 N = normalize(mix(baseNormal, mappedNormal, bumpStrength)); // Interpolated normal

        // --- View Vector ---
        vec3 V = normalize(viewPos - FragPos);

        // --- Reflection Vector for Environment Map ---
        vec3 R = reflect(-V, N);
        vec3 reflectionColor = texture(environmentMap, R).rgb;

        // --- Sample RNM Lightmaps ---
        vec3 lm0 = texture(lightmap0, LightmapTexCoords).rgb;
        vec3 lm1 = texture(lightmap1, LightmapTexCoords).rgb;
        vec3 lm2 = texture(lightmap2, LightmapTexCoords).rgb;

        // --- Compute Luminance of Each Lightmap Sample ---
        float lum0 = dot(lm0, vec3(0.2126, 0.7152, 0.0722));
        float lum1 = dot(lm1, vec3(0.2126, 0.7152, 0.0722));
        float lum2 = dot(lm2, vec3(0.2126, 0.7152, 0.0722));

        // --- Extract Basis Vectors from TBN Matrix ---
        vec3 basis0 = normalize(TBN[0]); // Tangent
        vec3 basis1 = normalize(TBN[1]); // Bitangent
        vec3 basis2 = normalize(TBN[2]); // Normal

        // --- Compute Dominant Light Direction ---
        vec3 dominantDir = lum0 * basis0 + lum1 * basis1 + lum2 * basis2;
        dominantDir = normalize(dominantDir);

        // --- Diffuse Lighting Calculation ---
        float l0 = max(dot(N, basis0), 0.0);
        float l1 = max(dot(N, basis1), 0.0);
        float l2 = max(dot(N, basis2), 0.0);

        vec3 diffuseLighting = lm0 * l0 + lm1 * l1 + lm2 * l2;

        // --- Fetch Diffuse Color and Alpha for Masking ---
        vec4 albedo = texture(diffuseTexture, TexCoords);
        vec3 diffuseColor = albedo.rgb;
        float mask = albedo.a;  // Use alpha as the mask for specular and reflection

        // --- Calculate Diffuse Component ---
        vec3 diffuse = diffuseColor * diffuseLighting;

        // --- Specular Lighting Calculation ---
        // Blinn-Phong Specular Model
        vec3 H = normalize(V + dominantDir);
        float NdotH = max(dot(N, H), 0.0);
        float specularFactor = pow(NdotH, shininess);

        // Compute irradiance in dominant direction for specular
        float s0 = max(dot(dominantDir, basis0), 0.0);
        float s1 = max(dot(dominantDir, basis1), 0.0);
        float s2 = max(dot(dominantDir, basis2), 0.0);

        vec3 lightColor = lm0 * s0 + lm1 * s1 + lm2 * s2;

        // Calculate Specular Component (scaled by mask)
        vec3 specular = lightColor * specularFactor * specularIntensity * mask;

        // --- Combine Diffuse, Specular, and Reflection ---
        float reflectionIntensity = 0.1 * mask; // Reflection intensity scaled by mask
        vec3 finalColor = diffuse + specular + reflectionColor * reflectionIntensity;

        FragColor = vec4(finalColor, 1.0);
    }
)";

void processInput(GLFWwindow* window) {
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        camera.processKeyboardInput(GLFW_KEY_W, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        camera.processKeyboardInput(GLFW_KEY_S, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        camera.processKeyboardInput(GLFW_KEY_A, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        camera.processKeyboardInput(GLFW_KEY_D, deltaTime);

    // Toggle shader on 'L' key press
    if (glfwGetKey(window, GLFW_KEY_L) == GLFW_PRESS && !keyPressed) {
        useSSBump = !useSSBump;
        keyPressed = true;
        std::cout << "Switched to " << (useSSBump ? "SSBump" : "Normal Map") << " shader." << std::endl;
    }

    if (glfwGetKey(window, GLFW_KEY_L) == GLFW_RELEASE) {
        keyPressed = false;
    }
}

void mouseCallback(GLFWwindow* window, double xpos, double ypos) {
    static bool firstMouse = true;
    static float lastX = WIDTH / 2.0f;
    static float lastY = HEIGHT / 2.0f;

    if (firstMouse) {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
    }

    float xoffset = xpos - lastX;
    float yoffset = lastY - ypos; // Reversed since y-coordinates range from bottom to top
    lastX = xpos;
    lastY = ypos;

    camera.processMouseMovement(xoffset, yoffset);
}

void scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    camera.processMouseScroll(static_cast<float>(yoffset));
}

// Utility function to load textures using stb_image or similar
GLuint loadTextureFromFile(const char* path, const std::string& directory);

std::string getFilenameFromPath(const std::string& path) {
    size_t pos = path.find_last_of("/\\");
    if (pos != std::string::npos)
        return path.substr(pos + 1);
    else
        return path;
}

struct MaterialNormalMaps {
    std::string ssbumpPath;
    std::string normalMapPath;
};

// Define this mapping globally or pass it to your loadModel function
std::map<std::string, MaterialNormalMaps> materialNormalMapPaths = {
    {"example_tutorial_ground", {"textures/default_bump_SSBump.tga", "textures/default_bump.tga"}},
    {"example_tutorial_metal", {"textures/default_bump_SSBump.tga", "textures/default_bump.tga"}},
    {"example_tutorial_metal_floor", {"textures/panels_generic_outdoor_bump_SSBump.png", "textures/panels_generic_outdoor_bump.png"}},
    {"example_tutorial_plate_floor", {"textures/metal plate floor bump_SSBump.png", "textures/metal plate floor bump.tga"}},
    {"example_tutorial_panels", {"textures/default_bump_SSBump.tga", "textures/default_bump.tga"}},
    {"boulder_grey", {"textures/default_bump_SSBump.tga", "textures/default_bump.tga"}},
    {"example_tutorial_lights_blue", {"textures/default_bump_SSBump.tga", "textures/default_bump.tga"}},
    {"example_tutorial_lights_red", {"textures/default_bump_SSBump.tga", "textures/default_bump.tga"}}
};

GLuint createFlatNormalMap() {
    // Create a 1x1 texture with RGB value (0.5, 0.5, 1.0)
    unsigned char flatNormalData[3] = { 128, 128, 255 }; // (0.5 * 255, 0.5 * 255, 1.0 * 255)

    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    // Generate the texture
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, flatNormalData);

    // Set texture parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    return textureID;
}

struct Vertex {
    glm::vec3 Position;
    glm::vec3 Normal;
    glm::vec2 TexCoords;         // For diffuse texture
    glm::vec2 LightmapTexCoords; // For lightmap texture
    glm::vec3 Tangent;
    glm::vec3 Bitangent;
};

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    mutable unsigned int VAO;
    GLuint diffuseTexture;
    GLuint ssbumpMapTexture;    // SSBump map texture
    GLuint normalMapTexture;    // Traditional normal map texture

    Mesh(std::vector<Vertex> vertices, std::vector<unsigned int> indices, GLuint diffuseTexture, GLuint ssbumpMapTexture, GLuint normalMapTexture)
        : vertices(vertices), indices(indices), diffuseTexture(diffuseTexture), ssbumpMapTexture(ssbumpMapTexture), normalMapTexture(normalMapTexture) {
        setupMesh();
    }

    void setupMesh() const {
        // Set up the VAO, VBO, and EBO as before
        glGenVertexArrays(1, &VAO);
        glBindVertexArray(VAO);

        unsigned int VBO, EBO;
        glGenBuffers(1, &VBO);
        glGenBuffers(1, &EBO);

        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), &vertices[0], GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), &indices[0], GL_STATIC_DRAW);

        // Vertex Attributes setup
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);

        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Normal));

        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, TexCoords));

        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, LightmapTexCoords));

        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Tangent));

        glEnableVertexAttribArray(5);
        glVertexAttribPointer(5, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Bitangent));

        glBindVertexArray(0);
    }

    void Draw(GLuint shaderProgram, bool useSSBump) const {
        // Bind diffuse texture (use texture unit 0)
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, diffuseTexture);
        glUniform1i(glGetUniformLocation(shaderProgram, "diffuseTexture"), 0);

        if (useSSBump) {
            // Bind SSBump map to texture unit 1
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, ssbumpMapTexture);
            glUniform1i(glGetUniformLocation(shaderProgram, "ssbumpMap"), 1);
        }
        else {
            // Bind normal map to texture unit 1
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, normalMapTexture);
            glUniform1i(glGetUniformLocation(shaderProgram, "normalMap"), 1);
        }

        // Bind VAO and draw the mesh
        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, indices.size(), GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
    }
};

std::vector<Mesh> loadModel(const std::string& path) {
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(path,
        aiProcess_Triangulate | aiProcess_FlipUVs |
        aiProcess_GenSmoothNormals | aiProcess_JoinIdenticalVertices |
        aiProcess_CalcTangentSpace);

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        std::cerr << "ERROR::ASSIMP::" << importer.GetErrorString() << std::endl;
        return {};
    }

    GLuint diffuseTexture = 0;
    GLuint normalMapTexture = 0;

    // Create the default flat normal map texture once
    static GLuint flatNormalMap = createFlatNormalMap();
    std::vector<Mesh> meshes;

    for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
        aiMesh* mesh = scene->mMeshes[i];
        std::vector<Vertex> vertices;
        std::vector<unsigned int> indices;

        // Process vertices and indices
        for (unsigned int j = 0; j < mesh->mNumVertices; j++) {
            Vertex vertex;
            vertex.Position = glm::vec3(mesh->mVertices[j].x, mesh->mVertices[j].y, mesh->mVertices[j].z);
            vertex.Normal = glm::vec3(mesh->mNormals[j].x, mesh->mNormals[j].y, mesh->mNormals[j].z);

            // First UV set (for diffuse textures)
            if (mesh->mTextureCoords[0]) {
                vertex.TexCoords = glm::vec2(mesh->mTextureCoords[0][j].x, mesh->mTextureCoords[0][j].y);
            }
            else {
                vertex.TexCoords = glm::vec2(0.0f, 0.0f);
            }

            // Second UV set (for lightmap textures)
            if (mesh->mTextureCoords[1]) {
                vertex.LightmapTexCoords = glm::vec2(mesh->mTextureCoords[1][j].x, mesh->mTextureCoords[1][j].y);
            }
            else {
                vertex.LightmapTexCoords = glm::vec2(0.0f, 0.0f); // Default to (0,0) if not available
            }

            // Tangent and Bitangent
            if (mesh->HasTangentsAndBitangents()) {
                vertex.Tangent = glm::vec3(mesh->mTangents[j].x, mesh->mTangents[j].y, mesh->mTangents[j].z);
                vertex.Bitangent = glm::vec3(mesh->mBitangents[j].x, mesh->mBitangents[j].y, mesh->mBitangents[j].z);
            }
            else {
                // Approximate tangent and bitangent based on the normal
                glm::vec3 up = abs(vertex.Normal.y) < 0.999 ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
                vertex.Tangent = normalize(cross(up, vertex.Normal));
                vertex.Bitangent = cross(vertex.Normal, vertex.Tangent);
            }

            vertices.push_back(vertex);
        }

        // Process indices
        for (unsigned int j = 0; j < mesh->mNumFaces; j++) {
            aiFace face = mesh->mFaces[j];
            for (unsigned int k = 0; k < face.mNumIndices; k++) {
                indices.push_back(face.mIndices[k]);
            }
        }

        // Load the material
        aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];

        aiString name;
        material->Get(AI_MATKEY_NAME, name);
        std::string matName(name.C_Str());

        // Debugging
        std::cout << "Material Name: " << matName << std::endl;

        // Load diffuse texture
        if (material->GetTextureCount(aiTextureType_DIFFUSE) > 0) {
            aiString str;
            material->GetTexture(aiTextureType_DIFFUSE, 0, &str);
            std::string textureFilename = getFilenameFromPath(str.C_Str());
            std::string texturePath = FileSystemUtils::getAssetFilePath("textures/" + textureFilename);
            diffuseTexture = loadTextureFromFile(texturePath.c_str(), "");
        }

        // Load SSBump map texture
        GLuint ssbumpMapTexture = 0;
        if (material->GetTextureCount(aiTextureType_NORMALS) > 0) {
            aiString str;
            material->GetTexture(aiTextureType_NORMALS, 0, &str);
            std::string textureFilename = getFilenameFromPath(str.C_Str());
            std::string texturePath = FileSystemUtils::getAssetFilePath("textures/" + textureFilename);
            ssbumpMapTexture = loadTextureFromFile(texturePath.c_str(), "");
        }
        else if (materialNormalMapPaths.find(matName) != materialNormalMapPaths.end()) {
            std::string ssbumpMapPath = FileSystemUtils::getAssetFilePath(materialNormalMapPaths[matName].ssbumpPath);
            ssbumpMapTexture = loadTextureFromFile(ssbumpMapPath.c_str(), "");
        }
        else {
            ssbumpMapTexture = flatNormalMap; // Default SSBump map if none specified
        }

        // Load traditional normal map texture
        GLuint normalMapTexture = 0;
        if (material->GetTextureCount(aiTextureType_HEIGHT) > 0) { // aiTextureType_HEIGHT often used for normal maps
            aiString str;
            material->GetTexture(aiTextureType_HEIGHT, 0, &str);
            std::string textureFilename = getFilenameFromPath(str.C_Str());
            std::string texturePath = FileSystemUtils::getAssetFilePath("textures/" + textureFilename);
            normalMapTexture = loadTextureFromFile(texturePath.c_str(), "");
        }
        else if (materialNormalMapPaths.find(matName) != materialNormalMapPaths.end()) {
            std::string normalMapPath = FileSystemUtils::getAssetFilePath(materialNormalMapPaths[matName].normalMapPath);
            normalMapTexture = loadTextureFromFile(normalMapPath.c_str(), "");
        }
        else {
            normalMapTexture = flatNormalMap; // Default normal map if none specified
        }

        if (ssbumpMapTexture == 0) {
            std::cerr << "Failed to load SSBump map for material: " << matName << std::endl;
        }

        if (normalMapTexture == 0) {
            std::cerr << "Failed to load normal map for material: " << matName << std::endl;
        }

        meshes.push_back(Mesh(vertices, indices, diffuseTexture, ssbumpMapTexture, normalMapTexture));
    }

    return meshes;
}

GLuint loadTextureFromFile(const char* path, const std::string&) {
    GLuint textureID;
    glGenTextures(1, &textureID);

    int width, height, nrComponents;
    unsigned char* data = stbi_load(path, &width, &height, &nrComponents, 0);
    if (data) {
        GLenum format;
        if (nrComponents == 1)
            format = GL_RED;
        else if (nrComponents == 3)
            format = GL_RGB;
        else if (nrComponents == 4)
            format = GL_RGBA;

        glBindTexture(GL_TEXTURE_2D, textureID);
        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        stbi_image_free(data);
    }
    else {
        std::cerr << "Texture failed to load at path: " << path << std::endl;
        stbi_image_free(data);
    }

    return textureID;
}

unsigned int loadCubemap(std::vector<std::string> faces) {
    unsigned int textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_CUBE_MAP, textureID);

    int width, height, nrChannels;
    for (unsigned int i = 0; i < faces.size(); i++) {
        unsigned char* data = stbi_load(faces[i].c_str(), &width, &height, &nrChannels, 0);
        if (data) {
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i,
                0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
            stbi_image_free(data);
        }
        else {
            std::cerr << "Cubemap texture failed to load at path: " << faces[i] << std::endl;
            stbi_image_free(data);
        }
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    return textureID;
}

GLuint compileShader(const char* vertexSrc, const char* fragmentSrc, const std::string& shaderName) {
    // Compile Vertex Shader
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexSrc, NULL);
    glCompileShader(vertexShader);

    // Check Vertex Shader
    int success;
    char infoLog[512];
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
        std::cerr << "ERROR::SHADER::VERTEX::COMPILATION_FAILED of " << shaderName << "\n" << infoLog << std::endl;
    }

    // Compile Fragment Shader
    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentSrc, NULL);
    glCompileShader(fragmentShader);

    // Check Fragment Shader
    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(fragmentShader, 512, NULL, infoLog);
        std::cerr << "ERROR::SHADER::FRAGMENT::COMPILATION_FAILED of " << shaderName << "\n" << infoLog << std::endl;
    }

    // Link Shaders
    GLuint shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);

    // Check Linking
    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(shaderProgram, 512, NULL, infoLog);
        std::cerr << "ERROR::SHADER::PROGRAM::LINKING_FAILED of " << shaderName << "\n" << infoLog << std::endl;
    }

    // Clean up shaders as they're linked into the program now and no longer necessary
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return shaderProgram;
}

int main() {
    // Initialize GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return -1;
    }

    // Create a GLFW window
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3); // Request OpenGL 4.3 or newer
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(WIDTH, HEIGHT, "OpenGL Basic Application", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable VSync to cap frame rate to monitor's refresh rate
    glfwSetCursorPosCallback(window, mouseCallback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwSetScrollCallback(window, scrollCallback);

    // Initialize GLEW
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::cerr << "Failed to initialize GLEW" << std::endl;
        return -1;
    }

    // Clear any GLEW errors
    glGetError(); // Clear error flag set by GLEW

    // Enable OpenGL debugging if supported
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(MessageCallback, nullptr);

    // Optionally filter which types of messages you want to log
    glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);

    // Define the viewport dimensions
    glViewport(0, 0, WIDTH, HEIGHT);

    glEnable(GL_DEPTH_TEST);

    glCullFace(GL_BACK); // Cull back faces (default)

    // Load the model
    std::vector<Mesh> meshes = loadModel(FileSystemUtils::getAssetFilePath("models/tutorial_map.fbx"));

    // Compile SSBump Shader Program
    GLuint shaderProgramSSBump = compileShader(LightMappingShaderSource, DirectionalLightmapSSBumpFragment, "SSBump");

    // Compile Normal Map Shader Program
    GLuint shaderProgramNormalBump = compileShader(LightMappingShaderSource, DirectionalLightmapNormalBumpFragmentShader, "NormalBump");

    // Load the cubemap textures
    std::vector<std::string> faces = {
        FileSystemUtils::getAssetFilePath("textures/cubemaps/right.jpg"),
        FileSystemUtils::getAssetFilePath("textures/cubemaps/left.jpg"),
        FileSystemUtils::getAssetFilePath("textures/cubemaps/top.jpg"),
        FileSystemUtils::getAssetFilePath("textures/cubemaps/bottom.jpg"),
        FileSystemUtils::getAssetFilePath("textures/cubemaps/front.jpg"),
        FileSystemUtils::getAssetFilePath("textures/cubemaps/back.jpg")
    };

    GLuint cubemapTexture = loadCubemap(faces);

    // Load the RNM lightmaps
    GLuint lightmap0 = loadTextureFromFile(FileSystemUtils::getAssetFilePath("textures/tutorialLightingMapX.tga").c_str(), "");
    GLuint lightmap1 = loadTextureFromFile(FileSystemUtils::getAssetFilePath("textures/tutorialLightingMapY.tga").c_str(), "");
    GLuint lightmap2 = loadTextureFromFile(FileSystemUtils::getAssetFilePath("textures/tutorialLightingMapZ.tga").c_str(), "");

    // Check if the textures loaded correctly
    if (!lightmap0 || !lightmap1 || !lightmap2) {
        std::cerr << "Failed to load one or more RNM lightmaps." << std::endl;
        return -1;
    }

    bool renderLightmapOnly = false;  // Set to true to debug the lightmap

    // Set specular parameters
    float specularIntensityValue = 0.5f; // Adjust as needed
    float shininessValue = 32.0f;        // Higher values for sharper highlights

    // Set bump strength
    float bumpStrengthValue = 1.0f; // Adjust between 0.0 and 1.0

    // Render loop
    while (!glfwWindowShouldClose(window)) {
        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        processInput(window);

        // Input handling
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, true);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        glClearColor(0.3f, 0.3f, 0.4f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Select the active shader program
        GLuint currentShaderProgram = useSSBump ? shaderProgramSSBump : shaderProgramNormalBump;
        glUseProgram(currentShaderProgram);

        // Set up view and projection matrices
        glm::mat4 view = camera.getViewMatrix();
        glm::mat4 projection = camera.getProjectionMatrix((float)WIDTH / (float)HEIGHT);

        // Pass matrices and view position to the shader
        glUniformMatrix4fv(glGetUniformLocation(currentShaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(currentShaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
        glUniform3fv(glGetUniformLocation(currentShaderProgram, "viewPos"), 1, glm::value_ptr(camera.getPosition()));

        glUniform1f(glGetUniformLocation(currentShaderProgram, "specularIntensity"), specularIntensityValue);
        glUniform1f(glGetUniformLocation(currentShaderProgram, "shininess"), shininessValue);

        if (!useSSBump)
        {
            bumpStrengthValue = 1.25f;
        }

        glUniform1f(glGetUniformLocation(currentShaderProgram, "bumpStrength"), bumpStrengthValue);

        // Set up the RNM lightmaps
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, lightmap0);
        glUniform1i(glGetUniformLocation(currentShaderProgram, "lightmap0"), 2);

        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, lightmap1);
        glUniform1i(glGetUniformLocation(currentShaderProgram, "lightmap1"), 3);

        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, lightmap2);
        glUniform1i(glGetUniformLocation(currentShaderProgram, "lightmap2"), 4);

        glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapTexture);
        glUniform1i(glGetUniformLocation(currentShaderProgram, "environmentMap"), 5);

        // Set the debug flag
        glUniform1i(glGetUniformLocation(currentShaderProgram, "renderLightmapOnly"), renderLightmapOnly);

        // Render all objects
        for (const auto& mesh : meshes) {
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::scale(model, glm::vec3(0.01f));
            model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
            glUniformMatrix4fv(glGetUniformLocation(currentShaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));
            mesh.Draw(currentShaderProgram, useSSBump);
        }

        // Swap buffers and poll IO events
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // Clean up
    glDeleteProgram(shaderProgramSSBump);
    glDeleteProgram(shaderProgramNormalBump);

    glfwTerminate();
    return 0;
}