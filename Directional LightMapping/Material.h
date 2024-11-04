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

class Texture {
public:
    GLuint id;
    std::string type;
    std::string path;                // For 2D textures
    std::vector<std::string> faces;  // For cubemap faces
    GLuint unit;
    bool isCubemap;
};

class Material {
public:
    std::string name;
    GLuint shaderProgram;
    std::string vertexShaderPath;
    std::string fragmentShaderPath;

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
};

#endif