#include "hello_imgui/hello_imgui.h"
#include "hello_imgui/hello_imgui_include_opengl.h"
#include "imgui_stdlib.h"
#include <iostream>
#include <memory>
#include <unordered_map>
#include <list>
#include <algorithm>

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

void FailOnShaderLinkError(GLuint shaderProgram)
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

void FailOnShaderCompileError(GLuint shader)
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

void FailOnOpenGlError()
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

GLuint CompileShader(GLuint type, const char* source)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    FailOnShaderCompileError(shader);
    return shader;
}

GLuint CreateFullScreenQuadVAO()
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

GLuint CreateShaderProgram(const char* vertexShaderSource, const char* fragmentShaderSource)
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


/******************************************************************************
 *
 * Shader code
 *
******************************************************************************/

// See https://www.shadertoy.com/view/Ms2SD1 / Many thanks to Alexander Alekseev aka TDM
// This is an old shader, so it uses GLSL 100


const char* GVertexShaderSource = R"(#version 100
precision mediump float;
attribute vec3 aPos;
attribute vec2 aTexCoord;

varying vec2 TexCoord;

void main()
{
    gl_Position = vec4(aPos, 1.0);
    TexCoord = aTexCoord;
}
)";

const char* GFragmentShaderSource = R"(#version 100
precision mediump float;

varying vec2 TexCoord;

uniform vec2 iResolution;  // Window resolution
uniform float iTime;      // Shader elapsed time
uniform vec2 iMouse;      // Mouse position

uniform vec3 cameraOffset;
uniform float cameraZoom;

void main()
{
    vec2 fragCoord = TexCoord * iResolution;
    float time = iTime * 0.3 + iMouse.x * 0.01;

    // Compute uv based on fragCoord
    //    vec2 uv = fragCoord / iResolution.xy;
    //    uv = uv * 2.0 - 1.0;
    //    uv.x *= iResolution.x / iResolution.y;

// Normalized pixel coordinates (from 0 to 1)
    vec2 uv = fragCoord/iResolution.xy;

    // Time varying pixel color
    vec3 col = 0.5 + 0.5*cos(iTime+uv.xyx+vec3(0,2,4));

    // Output to screen
    gl_FragColor = vec4(col,1.0);
}
)";

//temporary
const char* GFragmentShaderSource2 = R"(#version 100
precision mediump float;

varying vec2 TexCoord;

uniform vec2 iResolution;  // Window resolution
uniform float iTime;      // Shader elapsed time
uniform vec2 iMouse;      // Mouse position

uniform vec3 cameraOffset;
uniform float cameraZoom;

void main()
{
    // Output to screen
    gl_FragColor = vec4(1.0, 0.0, 0.0,1.0);
}
)";

/******************************************************************************
 *
 * Our App starts here
 *
******************************************************************************/

// Our global app state
struct AppState
{
    GLuint ShaderProgram;     // the shader program that is compiled and linked at startup
    GLuint FullScreenQuadVAO; // the VAO of a full-screen quad
    UniformsList Uniforms;    // the uniforms of the shader program, that enable to modify the shader parameters

    struct calcEntry{
	MyVec3 color;
	std::string eq;
	bool guiFocused = false;
    };
    std::list<calcEntry> entries;

    AppState()
    {
        Uniforms.AddUniform("cameraOffset", MyVec3{0, 0, 0});
        Uniforms.AddUniform("cameraZoom", 1);

        Uniforms.AddUniform("iResolution", ImVec2{100.f, 100.f});
        Uniforms.AddUniform("iTime", 0.f);
        Uniforms.AddUniform("iMouse", ImVec2{0.f, 0.f});
    }

    // Transmit new uniforms values to the shader
    void ApplyUniforms() { Uniforms.ApplyUniforms(); }

    // Get uniforms locations in the shader program
    void StoreUniformLocations() { Uniforms.StoreUniformLocations(ShaderProgram); }
};


void InitAppResources3D(AppState& appState)
{
    appState.ShaderProgram = CreateShaderProgram(GVertexShaderSource, GFragmentShaderSource);
    appState.FullScreenQuadVAO = CreateFullScreenQuadVAO();
    appState.StoreUniformLocations();
}


void DestroyAppResources3D(AppState& appState)
{
    glDeleteProgram(appState.ShaderProgram);
    glDeleteVertexArrays(1, &appState.FullScreenQuadVAO);
}


// ScaledDisplaySize() is a helper function that returns the size of the window in pixels:
//     for retina displays, io.DisplaySize is the size of the window in points (logical pixels)
//     but we need the size in pixels. So we scale io.DisplaySize by io.DisplayFramebufferScale
ImVec2 ScaledDisplaySize()
{
    auto& io = ImGui::GetIO();
    auto r = ImVec2(io.DisplaySize.x * io.DisplayFramebufferScale.x,
                    io.DisplaySize.y * io.DisplayFramebufferScale.y);
    return r;
}


// Our custom background callback: it displays the sea shader
void CustomBackground(AppState& appState)
{
    ImVec2 displaySize = ScaledDisplaySize();
    glViewport(0, 0, (GLsizei)displaySize.x, (GLsizei)displaySize.y);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(appState.ShaderProgram);

    // Set uniforms values that can be computed automatically
    // (other uniforms values are modifiable in the Gui() function)
    appState.Uniforms.SetUniformValue("iResolution", displaySize);
    appState.Uniforms.SetUniformValue("iTime", (float)ImGui::GetTime());
    // Optional: Set the iMouse uniform if you use it
    //     appState.Uniforms.SetUniformValue("iMouse", ImGui::IsMouseDown(0) ? ImGui::GetMousePos() : ImVec2(0.f, 0.f));
    // Here, we set it to zero, because the mouse uniforms does not lead to visually pleasing results
    appState.Uniforms.SetUniformValue("iMouse", ImVec2(0.f, 0.f));

    appState.ApplyUniforms();

    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(appState.FullScreenQuadVAO); // Render a full-screen quad (Bind the VAO)
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4); // Draw the quad
    glEnable(GL_DEPTH_TEST);
    glBindVertexArray(0); // Unbind the VAO
    glUseProgram(0); // Unbind the shader program
}


void Gui(AppState& appState)
{
    ImGui::SetNextWindowPos(HelloImGui::EmToVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize({HelloImGui::EmToVec2(25.0f, 100.0f).x, ScaledDisplaySize().y}, ImGuiCond_Appearing);
    ImGui::Begin("Shader parameters");

    // Modify the uniforms values:
    // Note:
    //     `uniforms.UniformValue<T>(name)`
    //     returns a modifiable reference to a uniform value
    auto& uniforms = appState.Uniforms;

    ImGui::Text("FPS: %.1f", HelloImGui::FrameRate());

    if(ImGui::Button("refresh shader program"))
  	  appState.ShaderProgram = CreateShaderProgram(GVertexShaderSource, GFragmentShaderSource2);

    //note:  the entryNum names are matching so focus stays after inputting a new eq
    unsigned int entryNum = 1;
    for(auto& entry : appState.entries){
	    ImGui::InputText(("entry " + std::to_string(entryNum) + "###entryNum" + std::to_string(entryNum)).c_str(), &entry.eq);
	    entry.guiFocused = ImGui::IsItemFocused();//make sure to delete entries only if they are not being currently worked on
	    ImGui::ColorEdit3(("draw color###colorNum" + std::to_string(entryNum)).c_str(), (float*)&entry.color);//janky pointer hack, works well enough
	    ImGui::SeparatorText("");
	    entryNum++;
    }

    std::string next = {};
    ImGui::InputText(("new entry###entryNum"+std::to_string(entryNum)).c_str(), &next);
    if(!next.empty()){
		appState.entries.push_back({MyVec3{1, 0, 1}, next});
		next.clear();
    }

    std::erase_if(appState.entries, [](const auto& entry){return entry.eq.empty() && !entry.guiFocused;});

    ImGui::End();
}


int main(int , char *[])
{
    // Our global app state
    AppState appState;

    // Hello ImGui parameters
    HelloImGui::RunnerParams runnerParams;

    // disable idling so that the shader runs at full speed
    runnerParams.fpsIdling.enableIdling = false;
    runnerParams.appWindowParams.windowGeometry.size = {1200, 720};
    runnerParams.appWindowParams.windowTitle = "piGraph";
    // Do not create a default ImGui window, so that the shader occupies the whole display
    runnerParams.imGuiWindowParams.defaultImGuiWindowType = HelloImGui::DefaultImGuiWindowType::NoDefaultWindow;

    //
    // Callbacks
    //
    // PostInit is called after the ImGui context is created, and after OpenGL is initialized
    runnerParams.callbacks.PostInit = [&appState]() { InitAppResources3D(appState); };
    // BeforeExit is called before the ImGui context is destroyed, and before OpenGL is deinitialized
    runnerParams.callbacks.BeforeExit = [&appState]() { DestroyAppResources3D(appState); };
    // ShowGui is called every frame, and is used to display the ImGui widgets
    runnerParams.callbacks.ShowGui = [&appState]() { Gui(appState); };
    // CustomBackground is called every frame, and is used to display the custom background
    runnerParams.callbacks.CustomBackground = [&appState]() { CustomBackground(appState); };

    // Let's go!
    HelloImGui::Run(runnerParams);
    return 0;
}
