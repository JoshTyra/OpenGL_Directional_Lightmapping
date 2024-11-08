#ifndef MATERIAL_H
#define MATERIAL_H

#include <string>
#include <map>
#include <vector>
#include <glm/glm.hpp>
#include <GL/glew.h>
#include <unordered_map>
#include "tinyxml2.h"
#include "Camera.h"

struct Texture {
    GLuint id;
    GLint unit;
    std::string type;
    std::string path;
    bool isCubemap;
    glm::vec2 tiling = glm::vec2(1.0f); // Default tiling factors (U and V)
};

class Material {
public:
    std::string name;
    GLuint shaderProgram;
    std::string vertexShaderPath;
    std::string fragmentShaderPath;

    // Blending parameters
    bool blendingEnabled = false;
    GLenum srcBlendFactor = GL_ONE;
    GLenum dstBlendFactor = GL_ZERO;
    GLenum blendEquation = GL_FUNC_ADD;

    std::map<std::string, float> floatParams;
    std::map<std::string, int> intParams;
    std::map<std::string, glm::vec3> vec3Params;

    std::vector<Texture> textures;

    // Static mapping from sampler names to texture units
    static const std::unordered_map<std::string, GLint> samplerUnitMap;

    Material(const std::string& xmlFilePath);
    void load();
    void apply(const glm::mat4& modelMatrix, const Camera& camera, float aspectRatio) const;
    void setIntParam(const std::string& name, int value);
    void setFloatParam(const std::string& name, float value);

private:
    void loadShaders();
    void loadTextures();
    void setUniforms(GLuint program) const;
    static std::map<std::string, GLuint> textureCache;
    GLenum parseBlendFactor(const std::string& factor);
    GLenum parseBlendEquation(const std::string& equation);
};

#endif