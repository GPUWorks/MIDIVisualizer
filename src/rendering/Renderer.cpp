#include <stdio.h>
#include <iostream>
#include <vector>
#include <glm/gtc/matrix_transform.hpp>
#include <cstring>
#include <nfd/nfd.h>
#include <imgui/imgui.h>
#include "../helpers/ProgramUtilities.h"
#include "../helpers/ResourcesManager.h"

#include "Renderer.h"
#include <algorithm>


Renderer::Renderer(){ }

Renderer::~Renderer(){}

void Renderer::init(int width, int height){
	_showGUI = true;
	_showParticles = true;
	_showFlashes = true;
	_showBlur = true;
	_showBlurNotes = false;
	_showDigits = true;
	_showHLines = true;
	_showVLines = true;
	_showKeys = true;
	_lockParticleColor = true;
 	_particlesSpeed = 0.2f;
 	_particlesExpansion = 1.0f;
	ResourcesManager::loadResources();
	
	// GL options
	glEnable(GL_CULL_FACE);
	glFrontFace(GL_CCW);
	glCullFace(GL_BACK);
	glDisable(GL_DEPTH_TEST);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	
	// Setup projection matrix.
	_camera.screen(width, height);
	
	// Setup framebuffers.
	_particlesFramebuffer = std::shared_ptr<Framebuffer>(new Framebuffer(_camera._screenSize[0],_camera._screenSize[1], GL_RGBA,GL_UNSIGNED_BYTE,GL_LINEAR,GL_CLAMP_TO_EDGE));
	_blurFramebuffer = std::shared_ptr<Framebuffer>(new Framebuffer(_camera._screenSize[0],_camera._screenSize[1], GL_RGBA,GL_UNSIGNED_BYTE,GL_LINEAR,GL_CLAMP_TO_EDGE));
	
	_blurringScreen.init(_particlesFramebuffer->textureId(), ResourcesManager::getStringForShader("particlesblur_frag"));
	_blurryScreen.init(_blurFramebuffer->textureId(), ResourcesManager::getStringForShader("screenquad_frag"));
	_minorsWidth = 0.8f;
	// Check setup errors.
	checkGLError();
}

void Renderer::loadFile(const std::string & midiFilePath, const glm::vec3& baseColor, const float scale){
	// Player.
	_timer = 0.0;
	_shouldPlay = false;
	_scale = scale;
	// Init objects.
	_scene = std::make_shared<MIDIScene>(midiFilePath, baseColor, _scale);
	_background = std::make_shared<Background>(_scene->midiFile().tracks[0].secondsPerMeasure, _scale);
	_scene->setScale(_scale);
	_background->setScale(_scale);
	_scene->setMinorWidth(_minorsWidth);
	_background->setMinorWidth(_minorsWidth);
	_background->setDisplay(_showDigits, _showHLines, _showVLines, _showKeys);
	_scene->setParticlesParameters(_particlesSpeed, _particlesExpansion);
}


void Renderer::draw(){
	
	// Compute the time elapsed since last frame, or keep the same value if playback is disabled.
	_timer = _shouldPlay ? (glfwGetTime() - _timerStart) : _timer;
	
	// Update active notes listing (for particles).
	_scene->updatesActiveNotes(_timer);
	
	// Set viewport.
	glViewport(0,0,_camera._screenSize[0],_camera._screenSize[1]);
	
	if(_showBlur){
		// Bind particles buffer.
		_particlesFramebuffer->bind();
		// Draw blurred particles from previous frames.
		_blurryScreen.draw(_timer, 1.0f/ _camera._screenSize);
		if(_showParticles){
			// Draw the new particles.
			_scene->drawParticles(_timer, 1.0f/ _camera._screenSize, true);
		}
		if(_showBlurNotes){
			// Draw the notes.
			_scene->draw(_timer, 1.0f/ _camera._screenSize, true);
		}
		
		_particlesFramebuffer->unbind();
	
	
		// Bind blur framebuffer.
		_blurFramebuffer->bind();
		// Perform box blur on result from particles pass.
		_blurringScreen.draw(_timer, 1.0f/ _camera._screenSize);
		_blurFramebuffer->unbind();
	}
	
	// Final pass (directly on screen).
	
	glClear(GL_COLOR_BUFFER_BIT);
	// Draw the blurred particles.
	if(_showBlur){
		_blurryScreen.draw(_timer, 1.0f/ _camera._screenSize);
	}
	// Draw the particles.
	if(_showParticles){
		_scene->drawParticles(_timer, 1.0f/ _camera._screenSize, false);
	}
	// Draw the keys, grid, and measure numbers.
	_background->draw(_timer, 1.0f/ _camera._screenSize);
	// Draw the notes.
	_scene->draw(_timer, 1.0f/ _camera._screenSize, false);
	
	if(_showFlashes){
		// Draw the flashes.
		_scene->drawFlashes(_timer, 1.0f/ _camera._screenSize);
	}
	
	if(_showGUI){
		//ImGui::ShowDemoWindow();
		if(ImGui::Begin("Settings", NULL, ImGuiWindowFlags_NoResize)){
			ImGui::Text("Welcome in MIDIVisualizer v2.2!");
			ImGui::Text("Keys:");
			ImGui::Text("\tp: start/stop playing");
			ImGui::Text("\tr: reset track");
			ImGui::Text("\ti: hide this panel");
			
			if(ImGui::Button("Load file...")){
				// Read arguments.
				std::string midiFilePath;
				nfdchar_t *outPath = NULL;
				nfdresult_t result = NFD_OpenDialog( NULL, NULL, &outPath );
				if(result == NFD_OKAY){
					midiFilePath = std::string(outPath);
					loadFile(midiFilePath, _scene->getColor(), _scale);
				}
			}
			ImGui::SameLine();
			ImGui::PushItemWidth(100);
			if(ImGui::InputFloat("Scale", &_scale, 0.01f, 0.1f)){
				_scale = std::max(_scale, 0.01f);
				_scene->setScale(_scale);
				_background->setScale(_scale);
			}
			
			if(ImGui::InputFloat("Minor keys width", &_minorsWidth, 0.1f, 1.0f)){
				_minorsWidth = std::min(std::max(_minorsWidth, 0.1f), 1.0f);
				_scene->setMinorWidth(_minorsWidth);
				_background->setMinorWidth(_minorsWidth);
			}
			ImGui::PopItemWidth();
			ImGui::Checkbox("Particles ", &_showParticles); 
			if(_showParticles){
				ImGui::SameLine();
				ImGui::PushID("ParticlesSettings");
				ImGui::PushItemWidth(100);
				ImGui::SliderInt("Count", &(_scene->getParticlesCountRef()), 1, 512);
				
				bool mp0 = ImGui::InputFloat("Speed", &_particlesSpeed, 0.001f, 1.0f);
				ImGui::SameLine();
				bool mp1 = ImGui::InputFloat("Expansion", &_particlesExpansion, 0.1f, 5.0f);

				if(mp1 || mp0){
					_scene->setParticlesParameters(_particlesSpeed, _particlesExpansion);
				}
				ImGui::PopID();
				ImGui::PopItemWidth();
			}
			ImGui::Checkbox("Blur      ", &_showBlur); ImGui::SameLine();
			ImGui::Checkbox("Blur notes", &_showBlurNotes); 

			ImGui::Checkbox("Flashes   ", &_showFlashes); ImGui::SameLine();
			bool m1 = ImGui::Checkbox("Digits", &_showDigits); 
			bool m2 = ImGui::Checkbox("H. lines  ", &_showHLines);  ImGui::SameLine();
			bool m3 = ImGui::Checkbox("V. lines", &_showVLines);
			bool m4 = ImGui::Checkbox("Keyboard", &_showKeys);
			
			if(m1 || m2 || m3 || m4){
				_background->setDisplay(_showDigits, _showHLines, _showVLines, _showKeys);
			}


			bool colNotesEdit = ImGui::ColorEdit3("Notes", &_scene->getColorRef()[0], ImGuiColorEditFlags_NoInputs); ImGui::SameLine();
			bool colPartsEdit = ImGui::ColorEdit3("Effects", &_scene->getParticlesColorRef()[0], ImGuiColorEditFlags_NoInputs);
			ImGui::SameLine();
			if(ImGui::Checkbox("Lock colors", &_lockParticleColor)){
				// If we enable the lock, make sure the colors are synched.
				colNotesEdit = true;
			}

			// Keep the colors in sync if needed.
			if(_lockParticleColor){
				if(colNotesEdit){
					_scene->setParticlesColor(_scene->getColor());
				} else if(colPartsEdit){
					_scene->setColor(_scene->getParticlesColor());
				}
			}
			ImGui::Text("Written by S. Rodriguez (kosua20)");
			ImGui::Text("github.com/kosua20/MIDIVisualizer");
		}
		ImGui::End();
		
	}
}


void Renderer::clean(){
	
	// Clean objects.
	_scene->clean();
	_background->clean();
	_blurringScreen.clean();
	_blurryScreen.clean();
	_particlesFramebuffer->clean();
	_blurFramebuffer->clean();
	
}


void Renderer::resize(int width, int height){
	
	//Update the size of the viewport.
	glViewport(0, 0, width, height);
	// Update the projection matrix.
	_camera.screen(width, height);
	// Resize the framebuffers.
	_particlesFramebuffer->resize(_camera._screenSize);
	_blurFramebuffer->resize(_camera._screenSize);
	
}

void Renderer::keyPressed(int key, int action){
	
	if(action == GLFW_PRESS){
		if(key == GLFW_KEY_P){
			_shouldPlay = !_shouldPlay;
			_timerStart = glfwGetTime() - _timer;
		} else if (key == GLFW_KEY_R){
			_timer = 0;
			_timerStart = glfwGetTime();
		} else if (key == GLFW_KEY_L){
			
		} else if (key == GLFW_KEY_I){
			_showGUI = !_showGUI;
		}
	}
	
}


