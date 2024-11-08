#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <vector>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <random>
#include <fstream>
#include "Camera.h"
#include "FileSystemUtils.h"
#include "Material.h"

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
bool lKeyPressed = false;
bool nKeyPressed = false;
bool iKeyPressed = false;
static bool visualizeNormals = false;
static bool visualizeshadowIntensity = false;

Camera camera(glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), -180.0f, 0.0f, 6.0f, 0.1f, 45.0f, 0.1f, 500.0f);

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
    std::shared_ptr<Material> material;

    Mesh(std::vector<Vertex> vertices, std::vector<unsigned int> indices, std::shared_ptr<Material> material)
        : vertices(vertices), indices(indices), material(material) {
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

    void Draw(const Camera& camera, const glm::mat4& modelMatrix, float aspectRatio) const {
        // Apply the material
        material->apply(modelMatrix, camera, aspectRatio);

        // Bind VAO and draw the mesh
        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices.size()), GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
    }
};

std::vector<Mesh> meshes;

void processInput(GLFWwindow* window) {
    // Handle movement keys
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        camera.processKeyboardInput(GLFW_KEY_W, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        camera.processKeyboardInput(GLFW_KEY_S, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        camera.processKeyboardInput(GLFW_KEY_A, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        camera.processKeyboardInput(GLFW_KEY_D, deltaTime);

    // Handle 'N' key toggle with its own flag
    if (glfwGetKey(window, GLFW_KEY_N) == GLFW_PRESS && !nKeyPressed) {
        visualizeNormals = !visualizeNormals;
        nKeyPressed = true;
        std::cout << "Visualize Normals: " << (visualizeNormals ? "ON" : "OFF") << std::endl;

        // Update the parameter in all materials
        for (auto& mesh : meshes) {
            mesh.material->setIntParam("visualizeNormals", visualizeNormals ? 1 : 0);
        }
    }
    if (glfwGetKey(window, GLFW_KEY_N) == GLFW_RELEASE) {
        nKeyPressed = false;
    }

    // Handle 'I' key toggle with its own flag
    if (glfwGetKey(window, GLFW_KEY_I) == GLFW_PRESS && !iKeyPressed) {
        visualizeshadowIntensity = !visualizeshadowIntensity;
        iKeyPressed = true;
        std::cout << "Visualize Shadow Intensity: " << (visualizeshadowIntensity ? "ON" : "OFF") << std::endl;

        // Update the parameter in all materials
        for (auto& mesh : meshes) {
            mesh.material->setIntParam("visualizeShadowIntensity", visualizeshadowIntensity ? 1 : 0);
        }
    }
    if (glfwGetKey(window, GLFW_KEY_I) == GLFW_RELEASE) {
        iKeyPressed = false;
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

std::map<std::string, std::shared_ptr<Material>> loadMaterialsFromList(const std::string& modelPath) {
    std::map<std::string, std::shared_ptr<Material>> materials;

    // Extract the model name without the path and extension
    size_t lastSlash = modelPath.find_last_of("/\\");
    std::string modelNameWithExtension = (lastSlash != std::string::npos) ? modelPath.substr(lastSlash + 1) : modelPath;
    std::string modelName = modelNameWithExtension.substr(0, modelNameWithExtension.find_last_of('.'));

    // Construct the materials list file path in the 'materials' folder
    std::string materialsListPath = FileSystemUtils::getAssetFilePath("materials/" + modelName + ".txt");

    std::ifstream materialsFile(materialsListPath);
    if (!materialsFile.is_open()) {
        std::cerr << "Materials list file not found: " << materialsListPath << std::endl;
        return materials;
    }

    std::string materialName;
    while (std::getline(materialsFile, materialName)) {
        // Trim any whitespace
        materialName.erase(0, materialName.find_first_not_of(" \t"));
        materialName.erase(materialName.find_last_not_of(" \t") + 1);

        if (!materialName.empty()) {
            // Construct the path to the material XML file without appending '.xml'
            std::string materialFilePath = FileSystemUtils::getAssetFilePath("materials/" + materialName);

            // Load the material using a shared_ptr
            std::shared_ptr<Material> material = std::make_shared<Material>(materialFilePath);

            // Remove the '.xml' extension from materialName for the map key
            std::string materialKey = materialName;
            size_t pos = materialKey.find_last_of('.');
            if (pos != std::string::npos) {
                materialKey = materialKey.substr(0, pos);
            }

            // Store in the map with the material name without extension
            materials[materialKey] = material;
        }
    }

    materialsFile.close();
    return materials;
}

std::vector<Mesh> loadModel(const std::string& path);
std::vector<Mesh> loadModel(const std::string& path, std::shared_ptr<Material> singleMaterial);

std::vector<Mesh> loadModel(const std::string& path) {
    // Load materials from the materials list file
    auto materials = loadMaterialsFromList(path);

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(path,
        aiProcess_Triangulate | aiProcess_FlipUVs |
        aiProcess_GenSmoothNormals | aiProcess_JoinIdenticalVertices |
        aiProcess_CalcTangentSpace);

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        std::cerr << "ERROR::ASSIMP::" << importer.GetErrorString() << std::endl;
        return {};
    }

    // Load a default material in case some materials are missing
    std::shared_ptr<Material> defaultMaterial = std::make_shared<Material>(FileSystemUtils::getAssetFilePath("materials/DefaultMaterial.xml"));

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
                glm::vec3 up = std::abs(vertex.Normal.y) < 0.999f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
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

        // Get the material assigned to this mesh
        aiMaterial* aiMaterial = scene->mMaterials[mesh->mMaterialIndex];
        aiString aiMatName;
        aiMaterial->Get(AI_MATKEY_NAME, aiMatName);
        std::string matName(aiMatName.C_Str());

        // Find the corresponding Material object
        std::shared_ptr<Material> meshMaterial;
        auto it = materials.find(matName);
        if (it != materials.end()) {
            meshMaterial = it->second;
        }
        else {
            std::cerr << "Material not found for mesh: " << matName << ". Using default material." << std::endl;
            meshMaterial = defaultMaterial;
        }

        // Create the Mesh object with the material
        meshes.push_back(Mesh(vertices, indices, meshMaterial));
    }

    return meshes;
}

std::vector<Mesh> loadModel(const std::string& path, std::shared_ptr<Material> singleMaterial) {
    // Initialize Assimp importer
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(path,
        aiProcess_Triangulate | aiProcess_FlipUVs |
        aiProcess_GenSmoothNormals | aiProcess_JoinIdenticalVertices |
        aiProcess_CalcTangentSpace);

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        std::cerr << "ERROR::ASSIMP::" << importer.GetErrorString() << std::endl;
        return {};
    }

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
                glm::vec3 up = std::abs(vertex.Normal.y) < 0.999f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
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

        // Create the Mesh object with the material
        meshes.push_back(Mesh(vertices, indices, singleMaterial));
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

GLuint loadCubemap(const std::vector<std::string>& faces) {
    GLuint textureID;
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

    GLFWwindow* window = glfwCreateWindow(WIDTH, HEIGHT, "OpenGL Directional LightMapping Example", nullptr, nullptr);
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
    meshes = loadModel(FileSystemUtils::getAssetFilePath("models/tutorial_map.fbx"));

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

        float aspectRatio = static_cast<float>(WIDTH) / HEIGHT;

        // Render all objects
        for (const auto& mesh : meshes) {
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::scale(model, glm::vec3(0.01f));
            model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));

            // Draw the mesh with the model matrix and camera
            mesh.Draw(camera, model, aspectRatio);
        }

        // Swap buffers and poll IO events
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}