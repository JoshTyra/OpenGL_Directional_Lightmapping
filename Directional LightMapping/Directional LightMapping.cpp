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
bool useHL2Shader = false;

Camera camera(glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), -180.0f, 0.0f, 6.0f, 0.1f, 45.0f, 0.1f, 500.0f);

const char* vertexShaderSource = R"(
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

const char* fragmentShaderSource = R"(
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

    void main()
    {
        if (renderLightmapOnly) {
            vec3 lm0 = texture(lightmap0, LightmapTexCoords).rgb;
            FragColor = vec4(lm0, 1.0);
            return;
        }

        // Normal Mapping
        vec3 tangentNormal = texture(normalMap, TexCoords).rgb;
        tangentNormal.g = 1.0 - tangentNormal.g; // Flip Y for DirectX to OpenGL conversion
        tangentNormal = normalize(tangentNormal * 2.0 - 1.0);
        vec3 N = normalize(TBN * tangentNormal);

        // View Vector
        vec3 V = normalize(viewPos - FragPos);

        // Reflection Vector
        vec3 R = reflect(-V, N);

        // Sample Environment Map (Cubemap)
        vec3 reflectionColor = texture(environmentMap, R).rgb;

        // Sample RNM lightmaps for diffuse lighting
        vec3 lm0 = texture(lightmap0, LightmapTexCoords).rgb;
        vec3 lm1 = texture(lightmap1, LightmapTexCoords).rgb;
        vec3 lm2 = texture(lightmap2, LightmapTexCoords).rgb;

        float l0 = max(tangentNormal.x, 0.0);
        float l1 = max(tangentNormal.y, 0.0);
        float l2 = max(tangentNormal.z, 0.0);

        vec3 diffuseLighting = lm0 * l0 + lm1 * l1 + lm2 * l2;

        // Fetch diffuse color
        vec3 albedo = texture(diffuseTexture, TexCoords).rgb;

        // Combine Diffuse and Reflection
        vec3 diffuse = albedo * diffuseLighting;
        float reflectionIntensity = 0.25; // Adjust between 0.0 and 1.0
        vec3 finalColor = diffuse + reflectionColor * reflectionIntensity;

        FragColor = vec4(finalColor, 1.0);
    }
)";

const char* hl2FragmentShaderSource = R"(
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

    uniform bool renderLightmapOnly;

    void main()
    {
        if (renderLightmapOnly) {
            vec3 lm0 = texture(lightmap0, LightmapTexCoords).rgb;
            FragColor = vec4(lm0, 1.0);
            return;
        }

        // Normal Mapping
        vec3 tangentNormal = texture(normalMap, TexCoords).rgb;
        tangentNormal.g = 1.0 - tangentNormal.g; // Flip Y for DirectX to OpenGL conversion
        tangentNormal = normalize(tangentNormal * 2.0 - 1.0);
        vec3 N = normalize(TBN * tangentNormal);

        // Sample RNM lightmaps for diffuse lighting
        vec3 lm0 = texture(lightmap0, LightmapTexCoords).rgb;
        vec3 lm1 = texture(lightmap1, LightmapTexCoords).rgb;
        vec3 lm2 = texture(lightmap2, LightmapTexCoords).rgb;

        // Combine the lightmaps using the normal components
        float l0 = N.x * 0.5 + 0.5;
        float l1 = N.y * 0.5 + 0.5;
        float l2 = N.z * 0.5 + 0.5;

        vec3 diffuseLighting = lm0 * l0 + lm1 * l1 + lm2 * l2;

        // Fetch diffuse color
        vec3 albedo = texture(diffuseTexture, TexCoords).rgb;

        // Final Color
        vec3 finalColor = albedo * diffuseLighting;

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

    // Shader toggle key
    static bool f1Pressed = false;
    if (glfwGetKey(window, GLFW_KEY_F1) == GLFW_PRESS) {
        if (!f1Pressed) {
            useHL2Shader = !useHL2Shader;
            std::cout << "Shader toggled: " << (useHL2Shader ? "HL2 RNM Shader" : "Current Shader") << std::endl;
            f1Pressed = true;
        }
    }
    if (glfwGetKey(window, GLFW_KEY_F1) == GLFW_RELEASE) {
        f1Pressed = false;
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

// Define this mapping globally or pass it to your loadModel function
std::map<std::string, std::string> materialNormalMapPaths = {
    {"example_tutorial_ground", "textures/metal flat generic bump.png"},
    {"example_tutorial_metal", "textures/metal flat generic bump.png"},
    {"example_tutorial_metal_floor", "textures/metal flat generic bump.png"},
    {"example_tutorial_plate_floor", "textures/metal plate floor bump_SSBump.png"},
    {"example_tutorial_panels", "textures/metal flat generic bump.png"},
    {"boulder_grey", "textures/metal flat generic bump.png"}
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
    GLuint normalMapTexture;

    // Updated constructor (no lightmap here)
    Mesh(std::vector<Vertex> vertices, std::vector<unsigned int> indices, GLuint diffuseTexture, GLuint normalMapTexture)
        : vertices(vertices), indices(indices), diffuseTexture(diffuseTexture), normalMapTexture(normalMapTexture) {
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

    void Draw(GLuint shaderProgram) const {
        // Bind diffuse texture (use texture unit 0)
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, diffuseTexture);
        glUniform1i(glGetUniformLocation(shaderProgram, "diffuseTexture"), 0);

        // Bind normal map texture (use texture unit 1)
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, normalMapTexture);
        glUniform1i(glGetUniformLocation(shaderProgram, "normalMap"), 1);

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

        // Load normal map texture (with fallback to flat normal map)
        if (material->GetTextureCount(aiTextureType_NORMALS) > 0) {
            aiString str;
            material->GetTexture(aiTextureType_NORMALS, 0, &str);
            std::string textureFilename = getFilenameFromPath(str.C_Str());
            std::string texturePath = FileSystemUtils::getAssetFilePath("textures/" + textureFilename);
            normalMapTexture = loadTextureFromFile(texturePath.c_str(), "");
        }
        else if (materialNormalMapPaths.find(matName) != materialNormalMapPaths.end()) {
            std::string normalMapPath = FileSystemUtils::getAssetFilePath(materialNormalMapPaths[matName]);
            normalMapTexture = loadTextureFromFile(normalMapPath.c_str(), "");
        }
        else {
            normalMapTexture = flatNormalMap; // Default normal map if none specified
        }

        if (normalMapTexture == 0) {
            std::cerr << "Failed to load normal map for material: " << matName << std::endl;
        }

        meshes.push_back(Mesh(vertices, indices, diffuseTexture, normalMapTexture));
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

    // Build and compile the shader program
   // Vertex Shader
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
    glCompileShader(vertexShader);

    // Check for shader compile errors
    int success;
    char infoLog[512];
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
        std::cerr << "ERROR::VERTEX_SHADER::COMPILATION_FAILED\n" << infoLog << std::endl;
    }

    // Fragment Shader
    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
    glCompileShader(fragmentShader);

    // Check for shader compile errors
    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(fragmentShader, 512, NULL, infoLog);
        std::cerr << "ERROR::FRAGMENT_SHADER::COMPILATION_FAILED\n" << infoLog << std::endl;
    }

    // Link shaders
    GLuint shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);

    // Check for linking errors
    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(shaderProgram, 512, NULL, infoLog);
        std::cerr << "ERROR::SHADER_PROGRAM::LINKING_FAILED\n" << infoLog << std::endl;
    }
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    // Compile HL2 RNM Fragment Shader
    GLuint hl2FragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(hl2FragmentShader, 1, &hl2FragmentShaderSource, NULL);
    glCompileShader(hl2FragmentShader);

    // Check for compile errors
    glGetShaderiv(hl2FragmentShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(hl2FragmentShader, 512, NULL, infoLog);
        std::cerr << "ERROR::HL2_FRAGMENT_SHADER::COMPILATION_FAILED\n" << infoLog << std::endl;
    }

    // Create HL2 Shader Program
    GLuint hl2ShaderProgram = glCreateProgram();
    glAttachShader(hl2ShaderProgram, vertexShader);
    glAttachShader(hl2ShaderProgram, hl2FragmentShader);
    glLinkProgram(hl2ShaderProgram);

    // Check for linking errors
    glGetProgramiv(hl2ShaderProgram, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(hl2ShaderProgram, 512, NULL, infoLog);
        std::cerr << "ERROR::HL2_SHADER_PROGRAM::LINKING_FAILED\n" << infoLog << std::endl;
    }

    // Clean up
    glDeleteShader(hl2FragmentShader);

    // Load the cubemap textures
    std::vector<std::string> faces = {
        FileSystemUtils::getAssetFilePath("textures/cubemaps/water_right.tga"),
        FileSystemUtils::getAssetFilePath("textures/cubemaps/water_left.tga"),
        FileSystemUtils::getAssetFilePath("textures/cubemaps/water_up.tga"),
        FileSystemUtils::getAssetFilePath("textures/cubemaps/water_down.tga"),
        FileSystemUtils::getAssetFilePath("textures/cubemaps/water_front.tga"),
        FileSystemUtils::getAssetFilePath("textures/cubemaps/water_back.tga")
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

        // Use the selected shader program
        GLuint currentShaderProgram = useHL2Shader ? hl2ShaderProgram : shaderProgram;
        glUseProgram(currentShaderProgram);

        // Set up view and projection matrices
        glm::mat4 view = camera.getViewMatrix();
        glm::mat4 projection = camera.getProjectionMatrix((float)WIDTH / (float)HEIGHT);

        // Pass matrices and view position to the shader
        glUniformMatrix4fv(glGetUniformLocation(currentShaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(currentShaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
        glUniform3fv(glGetUniformLocation(currentShaderProgram, "viewPos"), 1, glm::value_ptr(camera.getPosition()));

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

        // Set up the cubemap only if using the current shader
        if (!useHL2Shader) {
            glActiveTexture(GL_TEXTURE5);
            glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapTexture);
            glUniform1i(glGetUniformLocation(currentShaderProgram, "environmentMap"), 5);
        }

        // Set the debug flag
        glUniform1i(glGetUniformLocation(currentShaderProgram, "renderLightmapOnly"), renderLightmapOnly);

        // Render all objects
        for (const auto& mesh : meshes) {
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::scale(model, glm::vec3(0.01f));
            model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
            glUniformMatrix4fv(glGetUniformLocation(currentShaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));
            mesh.Draw(currentShaderProgram);
        }

        // Swap buffers and poll IO events
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // Clean up
    glDeleteProgram(shaderProgram);
    glDeleteProgram(hl2ShaderProgram);

    glfwTerminate();
    return 0;
}