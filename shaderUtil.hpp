#include "hello_imgui/hello_imgui_include_opengl.h"
#include "hello_imgui/hello_imgui.h"
#include <iostream>
#include <memory>
#include <unordered_map>

struct MyVec3{float x,y,z;};

// Helper struct for checking if a type is supported (typical C++ shenanigans)
template<class T> struct UnsupportedType : std::false_type {};

// Transmit any uniform type to the shader
template<typename T>
void ApplyUniform(GLint location, const T& value)
{
    if constexpr (std::is_same<T, int>::value)
        glUniform1i(location, value);
    else if constexpr (std::is_same<T, float>::value)
        glUniform1f(location, value);
    else if constexpr (std::is_same<T, double>::value)
        glUniform1d(location, value);
    else if constexpr (std::is_same<T, MyVec3>::value)
        glUniform3fv(location, 1, &value.x);
    else if constexpr (std::is_same<T, ImVec2>::value)
        glUniform2fv(location, 1, &value.x);
    else if constexpr (std::is_same<T, ImVec4>::value)
        glUniform4fv(location, 1, &value.x);
    else
        static_assert(UnsupportedType<T>::value, "Unsupported type");
}


// Base uniform class: can be used to store a uniform of any type
struct IUniform
{
    GLint location = 0;

    void StoreLocation(GLuint shaderProgram, const std::string& name)
    {
        location = glGetUniformLocation(shaderProgram, name.c_str());
    }

    virtual ~IUniform() {}

    virtual void Apply() = 0;
};


// Concrete uniform class: can be used to store a uniform of a specific type
template<typename T>
struct Uniform : public IUniform
{
    T value;
    Uniform(const T& initialValue): IUniform(), value(initialValue) {}
    void Apply() override { ApplyUniform(location, value); }
};


// Helper struct to store a list of uniforms
struct UniformsList
{
    std::unordered_map<std::string, std::unique_ptr<IUniform>> Uniforms;

    template<typename T> void AddUniform(const std::string& name, const T& initialValue)
    {
        Uniforms[name] = std::make_unique<Uniform<T>>(initialValue);
    }

    void StoreUniformLocations(GLuint shaderProgram)
    {
        for (auto& uniform : Uniforms)
            uniform.second->StoreLocation(shaderProgram, uniform.first);
    }

    void ApplyUniforms()
    {
        for (auto& uniform : Uniforms)
            uniform.second->Apply();
    }

    // UniformValue returns a modifiable reference to the uniform value
    template<typename T> T& UniformValue(const std::string& name)
    {
        IM_ASSERT(Uniforms.find(name) != Uniforms.end());
        auto& uniform = Uniforms[name];
        Uniform<T>* asT = dynamic_cast<Uniform<T>*>(uniform.get());
        IM_ASSERT(asT != nullptr);
        return asT->value;
    }

    template<typename T> void SetUniformValue(const std::string& name, const T& value)
    {
        IM_ASSERT(Uniforms.find(name) != Uniforms.end());
        auto& uniform = Uniforms[name];
        Uniform<T>* asT = dynamic_cast<Uniform<T>*>(uniform.get());
        IM_ASSERT(asT != nullptr);
        asT->value = value;
    }
};


/******************************************************************************
 *
 * Shader Utilities
 *
******************************************************************************/

inline void FailOnShaderLinkError(GLuint shaderProgram)
{
    GLint isLinked;
    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &isLinked);
    if (!isLinked) {
        GLchar infoLog[512];
        glGetProgramInfoLog(shaderProgram, 512, NULL, infoLog);
        std::cerr << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n" << infoLog << std::endl;
        IM_ASSERT(isLinked);
    }
}

inline void FailOnShaderCompileError(GLuint shader)
{
    GLint shaderCompileSuccess;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &shaderCompileSuccess);
    if (!shaderCompileSuccess)
    {
        GLchar infoLog[512];
        glGetShaderInfoLog(shader, 512, NULL, infoLog);
        std::cerr << "ERROR::SHADER::VERTEX::COMPILATION_FAILED\n" << infoLog << std::endl;
        IM_ASSERT(shaderCompileSuccess);
    }
}

inline void FailOnOpenGlError()
{
    int countOpenGlError = 0;
    GLenum err;
    while ((err = glGetError()) != GL_NO_ERROR)
    {
        std::cerr << "OpenGL error: " << err << std::endl;
        ++countOpenGlError;
    }
    IM_ASSERT(countOpenGlError == 0);
}

inline GLuint CompileShader(GLuint type, const char* source)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    FailOnShaderCompileError(shader);
    return shader;
}

inline GLuint CreateFullScreenQuadVAO()
{
    // Define the vertex data for a full-screen quad
    float vertices[] = {
        // positions   // texCoords
        -1.0f, -1.0f,  0.0f, 0.0f, // bottom left  (0)
        1.0f, -1.0f,  1.0f, 0.0f, // bottom right (1)
        -1.0f,  1.0f,  0.0f, 1.0f, // top left     (2)
        1.0f,  1.0f,  1.0f, 1.0f  // top right    (3)
    };
    // Generate and bind the VAO
    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    // Generate and bind the VBO
    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    // Fill the VBO with vertex data
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    // Set the vertex attribute pointers
    // Position attribute
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // Texture coordinate attribute
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // Unbind the VAO (and VBO)
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    // Check for any OpenGL errors
    FailOnOpenGlError();

    return vao;
}

inline GLuint CreateShaderProgram(const char* vertexShaderSource, const char* fragmentShaderSource)
{
    GLuint vertexShader = CompileShader(GL_VERTEX_SHADER, vertexShaderSource);
    GLuint fragmentShader = CompileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);

    // Create shader program
    GLuint shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);
    FailOnShaderLinkError(shaderProgram);

    // Delete shader objects once linked
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    // Check for any OpenGL errors
    FailOnOpenGlError();

    return shaderProgram;
}

