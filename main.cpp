#include "hello_imgui/hello_imgui.h"
#include "hello_imgui/hello_imgui_include_opengl.h"
#include "shaderUtil.hpp"
#include "imgui_stdlib.h"
#include <iostream>
#include <memory>
#include <list>
#include <algorithm>
#include "piCalc/parser/ptParse/ptParse.hpp"
#include "piCalc/mathEngine/expr.hpp"
#include "piCalc/mathEngine/simplify.hpp"
#include "piCalc/mathEngine/simplifications/evaluateDerivatives.hpp"


/******************************************************************************
 *
 * Shader code
 *
******************************************************************************/

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

std::string_view GFragShaderTop = R"(#version 100
precision mediump float;

varying vec2 TexCoord;

uniform vec2 iResolution;  // Window resolution
uniform float iTime;      // Shader elapsed time
uniform vec2 iMouse;      // Mouse position

uniform vec2 viewStart;
uniform vec2 viewSize;

uniform float EPSILON;


void main()
{
	vec2 fragCoord = TexCoord * iResolution;

	// Normalized pixel coordinates (from 0 to 1)
	vec2 uv = fragCoord/iResolution.xy;

	vec2 pos = viewStart + vec2(uv.x * viewSize.x, uv.y * viewSize.y);
	float x = pos.x;
	float y = pos.y;

	vec3 col = vec3(1.0, 1.0, 1.0);

	//render grid
	float gridSize = 1.0;
	float gridSizePx = gridSize / viewSize.x * iResolution.x;
	//sorta janky code here, but works alright
	for(int i=0;i<1000;i++){//choosing 1000 as a number that is "probably enough"
		if(!(gridSizePx < 10.0)){
			break;
		}
		gridSize *= 2.0;
		gridSizePx = gridSize / viewSize.x * iResolution.x;
	}
	for(int i=0;i<1000;i++){
		if(!(gridSizePx > 40.0)){
			break;
		}
		gridSize /= 2.0;
		gridSizePx = gridSize / viewSize.x * iResolution.x;
	}
	//minor grid
	if(mod(pos.x, gridSize) < EPSILON || mod(pos.y, gridSize) < EPSILON){
		col = vec3(0.8, 0.8, 0.8);
	}
	//major grid
	if(mod(pos.x, gridSize * 5.0) < EPSILON || mod(pos.y, gridSize * 5.0) < EPSILON){
		col = vec3(0.3, 0.3, 0.3);
	}
	//axes
	if(abs(pos.x) < EPSILON * 1.5 || abs(pos.y) < EPSILON * 1.5){
		col = vec3(0.0, 0.0, 0.0);
	}
)";

std::string_view GFragShaderBottom = R"(
	gl_FragColor = vec4(col,1.0);
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
	std::optional<std::variant<mathEngine::equation, std::shared_ptr<mathEngine::expr>>> parsedEq = std::nullopt;
	std::optional<std::variant<mathEngine::equation, std::shared_ptr<mathEngine::expr>>> reducedEq = std::nullopt;
	bool guiFocused = false;
    };
    std::list<calcEntry> entries;
    float viewZoom = 5.0f;
    float graphThickness = 2.0f;
    float majorLineThickness = 2.0f;
    float minorLineThickness = 1.0f;

    std::string genFragShader() const{
	  std::string newFragShader;
	  newFragShader += GFragShaderTop;
	  for(const auto& entry : entries){
		if(!entry.reducedEq)
			continue;
		std::string codeEntry = "\t{\n";
		codeEntry += "\t\tfloat val = ";
		if(std::holds_alternative<mathEngine::equation>(*entry.reducedEq)){
			codeEntry += std::get<mathEngine::equation>(*entry.reducedEq).getDiff()->toCode({"x", "y"}) + ";\n";
			codeEntry += "\t\tif(abs(val) < EPSILON * " + std::to_string(graphThickness) + "){\n";
		}else if(std::holds_alternative<std::shared_ptr<mathEngine::expr>>(*entry.reducedEq)){
			auto& expr = std::get<std::shared_ptr<mathEngine::expr>>(*entry.reducedEq);
			codeEntry += expr->toCode({"x"}) + ";\n"; //single exprs are treated as y=..., so no y terms allowed
			auto derivativeTry = mathEngine::simplification::evaluateDerivative(expr->clone(), "x");
			if(derivativeTry){
				auto derivative = mathEngine::fullySimplify(*derivativeTry)->toCode({"x"});
				codeEntry += "\t\tif(abs(y-val) < EPSILON * " + std::to_string(graphThickness) + " * max(abs(" + derivative + "), 1.0) ){\n";//note.  this maybe should be rethought for functions where the derivative isn't asways defined, for example Dx 1/x = ln(x) isn't defined for x < 0
			}else{
				codeEntry += "\t\tif(abs(y-val) < EPSILON * " + std::to_string(graphThickness) + "){\n";
			}
		}else{
			continue;
		}
		codeEntry += "\t\t\tcol = vec3(" + std::to_string(entry.color.x) + ", " + std::to_string(entry.color.y) + ", " + std::to_string(entry.color.z) + ");\n\t\t}\n\t}\n";
		newFragShader += codeEntry;
	  }

	  newFragShader += GFragShaderBottom;
	  return newFragShader;
    }

    AppState()
    {
        Uniforms.AddUniform("viewStart", ImVec2{-2.5f, -2.5f});
        Uniforms.AddUniform("viewSize", ImVec2{5.0f, 5.0f});
	Uniforms.AddUniform("EPSILON", 0.01f);

        Uniforms.AddUniform("iResolution", ImVec2{100.f, 100.f});
        Uniforms.AddUniform("iTime", 0.0f);
        Uniforms.AddUniform("iMouse", ImVec2{0.f, 0.f});
    }

    // Transmit new uniforms values to the shader
    void ApplyUniforms() { Uniforms.ApplyUniforms(); }

    // Get uniforms locations in the shader program
    void StoreUniformLocations() { Uniforms.StoreUniformLocations(ShaderProgram); }
};


void InitAppResources3D(AppState& appState)
{
	appState.ShaderProgram = CreateShaderProgram(GVertexShaderSource, appState.genFragShader().c_str());
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
    ImVec2& viewStart = uniforms.UniformValue<ImVec2>("viewStart");
    ImVec2& viewSize = uniforms.UniformValue<ImVec2>("viewSize");
    float& EPSILON = uniforms.UniformValue<float>("EPSILON");

    static bool wasWindowFocusedLastFrame = false;
    if(!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused() && !ImGui::IsAnyItemHovered() && !wasWindowFocusedLastFrame){
	    auto rawMousePos = ImGui::GetMousePos();
	    //mouse input is wrong!!! on highdpi
	    auto mouseUV = ImVec2(rawMousePos.x / ScaledDisplaySize().x, rawMousePos.y / ScaledDisplaySize().y);
	    mouseUV.y = 1.0f - mouseUV.y;
	    //std::cout<<"mouseUV: "<<mouseUV.x<<", "<<mouseUV.y<<std::endl;
	    ImVec2 mousePos = {viewStart.x + mouseUV.x * viewSize.x, viewStart.y + mouseUV.y * viewSize.y};
	    //std::cout<<"mousePos: "<<mousePos.x<<", "<<mousePos.y<<std::endl;

	    static bool mouseLeftDownLastFrame = false;
	    static std::optional<std::pair<ImVec2, ImVec2>> dragStartPos = std::nullopt; //first is mouse pos, second is viewStart
	    if(ImGui::IsMouseDown(0)){
		    if(!dragStartPos){
			    dragStartPos = {mouseUV, viewStart};
		    }else{
			    viewStart = ImVec2(dragStartPos->second.x + (dragStartPos->first.x- mouseUV.x)*viewSize.x, dragStartPos->second.y + (dragStartPos->first.y - mouseUV.y)*viewSize.y);
		    }
		    mouseLeftDownLastFrame = true;
	    }else{
		    mouseLeftDownLastFrame = false;
		    dragStartPos = std::nullopt;
	    }

	    float wheel = -ImGui::GetIO().MouseWheel;
	    if(wheel != 0){
		//now we want the mouse pos to be in the old mouse pos
	    	appState.viewZoom *= (1+(wheel*0.1));
    		viewSize = ImVec2(appState.viewZoom, appState.viewZoom * (ScaledDisplaySize().y / ScaledDisplaySize().x));
	    	ImVec2 newMousePos = {viewStart.x + mouseUV.x * viewSize.x, viewStart.y + mouseUV.y * viewSize.y};
		viewStart = ImVec2(viewStart.x + mousePos.x - newMousePos.x, viewStart.y + mousePos.y - newMousePos.y);
	    }
    }
    wasWindowFocusedLastFrame = ImGui::IsAnyItemActive() || ImGui::IsAnyItemFocused() || ImGui::IsAnyItemHovered();

    EPSILON = appState.viewZoom / ScaledDisplaySize().x;
    viewSize = ImVec2(appState.viewZoom, appState.viewZoom * (ScaledDisplaySize().y / ScaledDisplaySize().x));

    ImGui::Text("FPS: %.1f", HelloImGui::FrameRate());

    bool someEntryChanged = false;
    //note:  the entryNum names are matching so focus stays after inputting a new eq
    unsigned int entryNum = 1;
    for(auto& entry : appState.entries){
	    if(ImGui::InputText(("entry " + std::to_string(entryNum) + "###entryNum" + std::to_string(entryNum)).c_str(), &entry.eq)){
		    //try to compile the new code
		    auto parsed = parser::ptParse::parse(entry.eq);
		    if(parsed){
			    entry.parsedEq = parsed->value;
			    if(std::holds_alternative<mathEngine::equation>(*entry.parsedEq)){
				const auto& eq = std::get<mathEngine::equation>(*entry.parsedEq);
				entry.reducedEq = mathEngine::fullySimplify(eq.clone());	
			    }
			    if(std::holds_alternative<std::shared_ptr<mathEngine::expr>>(*entry.parsedEq)){
				const auto& eq = std::get<std::shared_ptr<mathEngine::expr>>(*entry.parsedEq);
				entry.reducedEq = mathEngine::fullySimplify(eq->clone());	
			    }
		    }else{
			entry.parsedEq = std::nullopt;
			entry.reducedEq = std::nullopt;
		    }
		    someEntryChanged = true;
	    }
	    entry.guiFocused = ImGui::IsItemFocused();//make ImGuiTextEditCallbackData* datasure to delete entries only if they are not being currently worked on
	    if(entry.parsedEq){
		    if(std::holds_alternative<mathEngine::equation>(*entry.parsedEq))
			    ImGui::Text("Parsed input: %s", std::get<mathEngine::equation>(*entry.parsedEq).toLatex().c_str());
		    if(std::holds_alternative<std::shared_ptr<mathEngine::expr>>(*entry.parsedEq))
			    ImGui::Text("Parsed input: %s", std::get<std::shared_ptr<mathEngine::expr>>(*entry.parsedEq)->toLatex().c_str());
	    }
	    if(entry.reducedEq){
		    if(std::holds_alternative<mathEngine::equation>(*entry.reducedEq))
			    ImGui::Text("Reduced: %s", std::get<mathEngine::equation>(*entry.reducedEq).toLatex().c_str());
		    if(std::holds_alternative<std::shared_ptr<mathEngine::expr>>(*entry.reducedEq))
			    ImGui::Text("Reduced: %s", std::get<std::shared_ptr<mathEngine::expr>>(*entry.reducedEq)->toLatex().c_str());
	    }
	    if(ImGui::ColorEdit3(("draw color###colorNum" + std::to_string(entryNum)).c_str(), (float*)&entry.color)){//janky pointer hack, works well enough
	    	someEntryChanged = true;
	    }
	    ImGui::SeparatorText("");
	    entryNum++;
    }

    std::string next = {};
    ImGui::InputText(("new entry###entryNum"+std::to_string(entryNum)).c_str(), &next);
    if(!next.empty()){
		appState.entries.push_back({MyVec3{1, 0, 1}, next});
		auto& entry = appState.entries.back();
		next.clear();
		auto parsed = parser::ptParse::parse(entry.eq);
		if(parsed){
			entry.parsedEq = parsed->value;
			if(std::holds_alternative<mathEngine::equation>(*entry.parsedEq)){
				const auto& eq = std::get<mathEngine::equation>(*entry.parsedEq);
				entry.reducedEq = mathEngine::fullySimplify(eq.clone());
			}
			if(std::holds_alternative<std::shared_ptr<mathEngine::expr>>(*entry.parsedEq)){
				const auto& eq = std::get<std::shared_ptr<mathEngine::expr>>(*entry.parsedEq);
				entry.reducedEq = mathEngine::fullySimplify(eq->clone());
			}
		}else{
			entry.parsedEq = std::nullopt;
			entry.reducedEq = std::nullopt;
		}
		someEntryChanged = true;
	}

    std::erase_if(appState.entries, [&](const auto& entry)mutable{
		    if(entry.eq.empty() && !entry.guiFocused){
			someEntryChanged = true;
			return true;
		    }else{
			return false;
		    }});

    if(someEntryChanged){
	  std::string newFragShader = appState.genFragShader();
	  std::cout<<"Recompiling shader: "<<newFragShader<<std::endl;
  	  appState.ShaderProgram = CreateShaderProgram(GVertexShaderSource, newFragShader.c_str());
	  appState.StoreUniformLocations();
	  appState.ApplyUniforms();
    }

    /*
    //draw labels to background
    auto bgDrawList = ImGui::GetForegroundDrawList();
    const auto drawAxislabel = [&](ImVec2 pos, ImVec4 color, float val){
	    ImVec2 rlPos = {(pos.x - viewSize.x) / viewSize.x * ScaledDisplaySize().x, (pos.y - viewSize.y) / viewSize.y * ScaledDisplaySize().y};
	    bgDrawList->AddText(rlPos, ImGui::ColorConvertFloat4ToU32(color), std::to_string(val).c_str());
    };

    drawAxislabel({0, 0}, {0, 0, 1, 1}, 3.14);
    */

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
