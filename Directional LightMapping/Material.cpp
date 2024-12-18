#include "Material.h"
#include "FileSystemUtils.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <glm/gtc/type_ptr.hpp>

// Include your shader compilation function or adjust accordingly
extern GLuint compileShader(const char* vertexSrc, const char* fragmentSrc, const std::string& shaderName);
extern GLuint loadTextureFromFile(const char* path, const std::string& directory);
extern GLuint loadCubemap(const std::vector<std::string>& faces);

std::map<std::string, GLuint> Material::textureCache;

// Initialize the static sampler unit mapping
const std::unordered_map<std::string, GLint> Material::samplerUnitMap = {
    {"diffuseTexture", 0},
    {"bumpMap", 1},
    {"lightmap0", 2},
    {"lightmap1", 3},
    {"lightmap2", 4},
    {"environmentMap", 5},
    {"detailMap", 6},
    {"detailMap2", 7},
    {"detailMap3", 8},
    {"blendMap", 9}
};

GLenum Material::parseBlendFactor(const std::string& factor) {
    if (factor == "GL_ZERO") return GL_ZERO;
    if (factor == "GL_ONE") return GL_ONE;
    if (factor == "GL_SRC_ALPHA") return GL_SRC_ALPHA;
    return GL_ONE; // Default
}

GLenum Material::parseBlendEquation(const std::string& equation) {
    if (equation == "GL_FUNC_ADD") return GL_FUNC_ADD;
    if (equation == "GL_FUNC_SUBTRACT") return GL_FUNC_SUBTRACT;
    return GL_FUNC_ADD; // Default
}

Material::Material(const std::string& xmlFilePath) {
    // Parse XML and load material properties
    tinyxml2::XMLDocument doc;
    if (doc.LoadFile(xmlFilePath.c_str()) != tinyxml2::XML_SUCCESS) {
        std::cerr << "Failed to load material XML: " << xmlFilePath << std::endl;
        return;
    }

    tinyxml2::XMLElement* root = doc.FirstChildElement("material");
    if (!root) {
        std::cerr << "No <material> root element found in XML." << std::endl;
        return;
    }

    // Get material name
    const char* matName = root->Attribute("name");
    if (matName)
        name = matName;

    // Load textures
    tinyxml2::XMLElement* texturesElement = root->FirstChildElement("textures");
    if (texturesElement) {
        for (tinyxml2::XMLElement* texElement = texturesElement->FirstChildElement();
            texElement != nullptr;
            texElement = texElement->NextSiblingElement()) {

            if (strcmp(texElement->Name(), "texture") == 0) {
                // Handle 2D textures
                Texture texture;
                texture.unit = texElement->IntAttribute("unit");
                texture.type = texElement->Attribute("type");
                texture.path = FileSystemUtils::getAssetFilePath(texElement->Attribute("path"));
                texture.isCubemap = false;

                // Parse tiling if present
                tinyxml2::XMLElement* tilingElement = texElement->FirstChildElement("tiling");
                if (tilingElement) {
                    float u = tilingElement->FloatAttribute("u", 1.0f);
                    float v = tilingElement->FloatAttribute("v", 1.0f);
                    texture.tiling = glm::vec2(u, v);
                }
                else {
                    texture.tiling = glm::vec2(1.0f);
                }

                // Check if the texture is already loaded
                auto it = textureCache.find(texture.path);
                if (it != textureCache.end()) {
                    texture.id = it->second;
                }
                else {
                    texture.id = loadTextureFromFile(texture.path.c_str(), "");
                    textureCache[texture.path] = texture.id;
                }

                textures.push_back(texture);
            }
            else if (strcmp(texElement->Name(), "cubemap") == 0) {
                // Handle cubemaps
                Texture texture;
                texture.unit = texElement->IntAttribute("unit");
                texture.type = texElement->Attribute("type");
                texture.isCubemap = true;
                texture.tiling = glm::vec2(1.0f); // Default tiling for cubemaps

                // Load cubemap faces
                std::vector<std::string> faces;
                for (tinyxml2::XMLElement* faceElement = texElement->FirstChildElement("face");
                    faceElement != nullptr;
                    faceElement = faceElement->NextSiblingElement("face")) {

                    std::string facePath = FileSystemUtils::getAssetFilePath(faceElement->Attribute("path"));
                    faces.push_back(facePath);
                }

                // Generate a key for caching based on the cubemap paths
                std::string cubemapKey = faces[0]; // Assuming the first face path is unique enough

                // Check if the cubemap is already loaded
                auto it = textureCache.find(cubemapKey);
                if (it != textureCache.end()) {
                    texture.id = it->second;
                }
                else {
                    texture.id = loadCubemap(faces);
                    textureCache[cubemapKey] = texture.id;
                }

                textures.push_back(texture);
            }
        }
    }

    // Load parameters
    tinyxml2::XMLElement* paramsElement = root->FirstChildElement("parameters");
    if (paramsElement) {
        for (tinyxml2::XMLElement* paramElement = paramsElement->FirstChildElement("parameter");
            paramElement != nullptr;
            paramElement = paramElement->NextSiblingElement("parameter")) {
            std::string paramName = paramElement->Attribute("name");
            std::string type = paramElement->Attribute("type");
            std::string value = paramElement->Attribute("value");

            if (type == "float") {
                floatParams[paramName] = std::stof(value);
            }
            else if (type == "int") {
                intParams[paramName] = std::stoi(value);
            }
            else if (type == "vec3") {
                glm::vec3 vecValue;
                std::istringstream ss(value);
                ss >> vecValue.x >> vecValue.y >> vecValue.z;
                vec3Params[paramName] = vecValue;
            }
        }
    }

    tinyxml2::XMLElement* blendingElement = root->FirstChildElement("blending");
    if (blendingElement) {
        blendingEnabled = blendingElement->BoolAttribute("enabled", false);
        const char* srcFactorStr = blendingElement->Attribute("srcFactor");
        const char* dstFactorStr = blendingElement->Attribute("dstFactor");
        const char* equationStr = blendingElement->Attribute("equation");

        if (srcFactorStr)
            srcBlendFactor = parseBlendFactor(srcFactorStr);
        if (dstFactorStr)
            dstBlendFactor = parseBlendFactor(dstFactorStr);
        if (equationStr)
            blendEquation = parseBlendEquation(equationStr);
    }

    // Load shaders
    tinyxml2::XMLElement* shaderElement = root->FirstChildElement("shader");
    if (shaderElement) {
        vertexShaderPath = FileSystemUtils::getAssetFilePath(shaderElement->Attribute("vertex"));
        fragmentShaderPath = FileSystemUtils::getAssetFilePath(shaderElement->Attribute("fragment"));
        loadShaders();
    }
}

void Material::load() {
    // Additional loading steps if necessary
}

void Material::loadShaders() {
    // Read shader code from files
    std::ifstream vShaderFile(vertexShaderPath);
    std::stringstream vShaderStream;
    vShaderStream << vShaderFile.rdbuf();
    std::string vertexCode = vShaderStream.str();

    std::ifstream fShaderFile(fragmentShaderPath);
    std::stringstream fShaderStream;
    fShaderStream << fShaderFile.rdbuf();
    std::string fragmentCode = fShaderStream.str();

    shaderProgram = compileShader(vertexCode.c_str(), fragmentCode.c_str(), name);
}

void Material::apply(const glm::mat4& modelMatrix, const Camera& camera, float aspectRatio) const {
    glUseProgram(shaderProgram);

    // Set model, view, and projection matrices
    GLint modelLoc = glGetUniformLocation(shaderProgram, "model");
    GLint viewLoc = glGetUniformLocation(shaderProgram, "view");
    GLint projLoc = glGetUniformLocation(shaderProgram, "projection");

    glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(modelMatrix));
    glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(camera.getViewMatrix()));
    glUniformMatrix4fv(projLoc, 1, GL_FALSE,  glm::value_ptr(camera.getProjectionMatrix(aspectRatio)));

    // Set camera position
    GLint viewPosLoc = glGetUniformLocation(shaderProgram, "viewPos");
    glUniform3fv(viewPosLoc, 1, glm::value_ptr(camera.getPosition()));

    // Set custom uniforms
    setUniforms(shaderProgram);

    // Bind textures specified in the material
    for (const auto& texture : textures) {
        glActiveTexture(GL_TEXTURE0 + texture.unit);
        if (texture.isCubemap) {
            glBindTexture(GL_TEXTURE_CUBE_MAP, texture.id);
        }
        else {
            glBindTexture(GL_TEXTURE_2D, texture.id);
        }
    }

    // Automatically assign texture units to sampler uniforms
    for (const auto& [samplerName, unit] : samplerUnitMap) {
        GLint loc = glGetUniformLocation(shaderProgram, samplerName.c_str());
        if (loc != -1) {
            glUniform1i(loc, unit);
        }
    }

    // Pass tiling parameters
    for (const auto& texture : textures) {
        // Construct the uniform name (e.g., "diffuseTextureTiling")
        std::string uniformName = texture.type + "Tiling";
        GLint loc = glGetUniformLocation(shaderProgram, uniformName.c_str());
        if (loc != -1) {
            glUniform2fv(loc, 1, glm::value_ptr(texture.tiling));
        }
    }

    // Pass detailBlendFactor if it exists
    GLint blendFactorLoc = glGetUniformLocation(shaderProgram, "detailBlendFactor");
    if (blendFactorLoc != -1) {
        auto it = floatParams.find("detailBlendFactor");
        if (it != floatParams.end()) {
            glUniform1f(blendFactorLoc, it->second);
        }
        else {
            glUniform1f(blendFactorLoc, 0.0f); // Default to 0 if not specified
        }
    }

    // Set blending mode
    if (blendingEnabled) {
        glEnable(GL_BLEND);
        glBlendFunc(srcBlendFactor, dstBlendFactor);
        glBlendEquation(blendEquation);
    }
    else {
        glDisable(GL_BLEND);
    }
}

void Material::setUniforms(GLuint program) const {
    for (const auto& [name, value] : floatParams) {
        GLint loc = glGetUniformLocation(program, name.c_str());
        if (loc != -1) {
            glUniform1f(loc, value);
        }
    }
    for (const auto& [name, value] : intParams) {
        GLint loc = glGetUniformLocation(program, name.c_str());
        if (loc != -1) {
            glUniform1i(loc, value);
        }
    }
    for (const auto& [name, value] : vec3Params) {
        GLint loc = glGetUniformLocation(program, name.c_str());
        if (loc != -1) {
            glUniform3fv(loc, 1, &value[0]);
        }
    }
}

void Material::setIntParam(const std::string& name, int value) {
    intParams[name] = value;
}

void Material::setFloatParam(const std::string& name, float value) {
    floatParams[name] = value;
}