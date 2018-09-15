#include "MainMode.hpp"

#include "MenuMode.hpp"
#include "Load.hpp"
#include "Sound.hpp"
#include "MeshBuffer.hpp"
#include "gl_errors.hpp" //helper for dumpping OpenGL error messages
#include "read_chunk.hpp" //helper for reading a vector of structures from a file
#include "data_path.hpp" //helper to get paths relative to executable
#include "compile_program.hpp" //helper to compile opengl shader programs
#include "draw_text.hpp" //helper to... um.. draw text
#include "vertex_color_program.hpp"

#include <glm/gtc/type_ptr.hpp>

#include <iostream>
#include <fstream>
#include <map>
#include <cstddef>
#include <random>

Load< MeshBuffer > phonebank_meshes(LoadTagDefault, []() {
  return new MeshBuffer(data_path("phone-bank.pnc"));
});

Load< GLuint > phonebank_meshes_for_vertex_color_program(LoadTagDefault, [](){
	return new GLuint(phonebank_meshes->make_vao_for_program(vertex_color_program->program));
});

Load< Sound::Sample > sample_ring(LoadTagDefault, [](){
	return new Sound::Sample(data_path("ring.wav"));
});
Load< Sound::Sample > sample_loop(LoadTagDefault, [](){
	return new Sound::Sample(data_path("music.wav"));
});

MainMode::MainMode() {
	//----------------
	//set up scene:
	//TODO: this should load the scene from a file!

	auto attach_object = [this](Scene::Transform *transform, std::string const &name) {
		Scene::Object *object = scene.new_object(transform);
		object->program = vertex_color_program->program;
		object->program_mvp_mat4 = vertex_color_program->object_to_clip_mat4;
		object->program_mv_mat4x3 = vertex_color_program->object_to_light_mat4x3;
		object->program_itmv_mat3 = vertex_color_program->normal_to_light_mat3;
		object->vao = *phonebank_meshes_for_vertex_color_program;
		MeshBuffer::Mesh const &mesh = phonebank_meshes->lookup(name);
		object->start = mesh.start;
		object->count = mesh.count;
		return object;
	};

  std::ifstream blob(data_path("phone-bank.scene"), std::ios::binary);
  //Scene file format:
  // str0 len < char > *[strings chunk]
  // xfh0 len < ... > *[transform hierarchy]
  // msh0 len < uint uint uint >[hierarchy point + mesh name]
  // cam0 len < uint params >[hierarchy point + camera params]
  // lmp0 len < uint params >[hierarchy point + light params]

  std::vector< char > names;
  read_chunk(blob, "str0", &names);

  struct NameRef {
    uint32_t begin; //index into names
    uint32_t end; //index into names
  };
  static_assert(sizeof(NameRef) == 8, "NameRef should be packed.");

  struct TransformInfo {
    int parent_ref; //index into transforms of parent
    NameRef name; //name of transform
    glm::vec3 position;
    glm::quat rotation;
    glm::vec3 scale;
  };
  static_assert(sizeof(TransformInfo) == 52, "TransformInfo should be packed.");

  std::vector<TransformInfo> transform_infos;
  read_chunk(blob, "xfh0", &transform_infos);

  struct MeshInfo {
    int hierarchy_ref; //index into transforms
    NameRef mesh_name; //name of mesh- index into names
  };
  static_assert(sizeof(MeshInfo) == 12, "MeshInfo should be packed.");

  std::vector< MeshInfo > mesh_infos;
  read_chunk(blob, "msh0", &mesh_infos);

  std::map<int, Scene::Transform*> transforms;
  for (MeshInfo const &mesh_info : mesh_infos) {
    int current_ref = mesh_info.hierarchy_ref;
    TransformInfo transform_info = transform_infos[current_ref];
    Scene::Transform *transform = scene.new_transform();
    transforms[current_ref] = transform;
    transform->position = transform_info.position;
    transform->rotation = transform_info.rotation;
    transform->scale = transform_info.scale;
    if (transform_info.parent_ref >= 0) {
      transform->set_parent(transforms[transform_info.parent_ref]);
    }
    std::string mesh_name = std::string(names.begin() + mesh_info.mesh_name.begin, names.begin() + mesh_info.mesh_name.end);
    Scene::Object *obj_mesh = attach_object(transform, mesh_name);
    //scene_objects.push_back(obj_mesh);
  }

  { //Camera looking at the origin:
    Scene::Transform *transform = scene.new_transform();
    transform->position = glm::vec3(0.0f, -10.0f, 1.0f);
    //Cameras look along -z, so rotate view to look at origin:
    transform->rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    camera = scene.new_camera(transform);
  }

	/*{ //build some sort of content:
		//Crate at the origin:
		Scene::Transform *transform1 = scene.new_transform();
		transform1->position = glm::vec3(1.0f, 0.0f, 0.0f);
		large_crate = attach_object(transform1, "Phone");
		//smaller crate on top:
		Scene::Transform *transform2 = scene.new_transform();
		transform2->set_parent(transform1);
		transform2->position = glm::vec3(0.0f, 0.0f, 1.5f);
		transform2->scale = glm::vec3(0.5f);
		small_crate = attach_object(transform2, "Phone");
	}

	{ //Camera looking at the origin:
		Scene::Transform *transform = scene.new_transform();
		transform->position = glm::vec3(0.0f, -10.0f, 1.0f);
		//Cameras look along -z, so rotate view to look at origin:
		transform->rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
		camera = scene.new_camera(transform);
	}*/
	
	//start the 'loop' sample playing at the camera:
	loop = sample_loop->play(camera->transform->position, 1.0f, Sound::Loop);
}

MainMode::~MainMode() {
	if (loop) loop->stop();
}

bool MainMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {
	//ignore any keys that are the result of automatic key repeat:
	if (evt.type == SDL_KEYDOWN && evt.key.repeat) {
		return false;
	}
	//handle tracking the state of WSAD for movement control:
	if (evt.type == SDL_KEYDOWN || evt.type == SDL_KEYUP) {
		if (evt.key.keysym.scancode == SDL_SCANCODE_W) {
			controls.forward = (evt.type == SDL_KEYDOWN);
			return true;
		} else if (evt.key.keysym.scancode == SDL_SCANCODE_S) {
			controls.backward = (evt.type == SDL_KEYDOWN);
			return true;
		} else if (evt.key.keysym.scancode == SDL_SCANCODE_A) {
			controls.left = (evt.type == SDL_KEYDOWN);
			return true;
		} else if (evt.key.keysym.scancode == SDL_SCANCODE_D) {
			controls.right = (evt.type == SDL_KEYDOWN);
			return true;
		}
	}
	//handle tracking the mouse for rotation control:
	if (!mouse_captured) {
		if (evt.type == SDL_MOUSEBUTTONDOWN) {
			SDL_SetRelativeMouseMode(SDL_TRUE);
			mouse_captured = true;
			return true;
		}
	} else if (mouse_captured) {
		if (evt.type == SDL_KEYDOWN && evt.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
      //SDL_SetRelativeMouseMode(SDL_FALSE);
      //mouse_captured = false;
      show_pause_menu();
			return true;
		}
		if (evt.type == SDL_MOUSEMOTION) {
			//Note: float(window_size.y) * camera->fovy is a pixels-to-radians conversion factor
			float yaw = evt.motion.xrel / float(window_size.y) * camera->fovy;
			float pitch = evt.motion.yrel / float(window_size.y) * camera->fovy;
			yaw = -yaw;
			pitch = -pitch;
			camera->transform->rotation = glm::normalize(
				camera->transform->rotation
				* glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f))
				* glm::angleAxis(pitch, glm::vec3(1.0f, 0.0f, 0.0f))
			);
			return true;
		}
	}
	return false;
}

void MainMode::update(float elapsed) {
	glm::mat3 directions = glm::mat3_cast(camera->transform->rotation);
	float amt = 5.0f * elapsed;
	if (controls.right) camera->transform->position += amt * directions[0];
	if (controls.left) camera->transform->position -= amt * directions[0];
	if (controls.backward) camera->transform->position += amt * directions[2];
	if (controls.forward) camera->transform->position -= amt * directions[2];

	{ //set sound positions:
		glm::mat4 cam_to_world = camera->transform->make_local_to_world();
		Sound::listener.set_position( cam_to_world[3] );
		//camera looks down -z, so right is +x:
		Sound::listener.set_right( glm::normalize(cam_to_world[0]) );

		if (loop) {
			glm::mat4 camera_to_world = camera->transform->make_local_to_world();
			loop->set_position(camera_to_world * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
		}
	}

	dot_countdown -= elapsed;
	if (dot_countdown <= 0.0f) {
		dot_countdown = (rand() / float(RAND_MAX) * 2.0f) + 0.5f;
    glm::mat4 camera_to_world = camera->transform->make_local_to_world();
		sample_ring->play(camera_to_world * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
	}
}

void MainMode::draw(glm::uvec2 const &drawable_size) {
	//set up basic OpenGL state:
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendEquation(GL_FUNC_ADD);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	//set up light position + color:
	glUseProgram(vertex_color_program->program);
	glUniform3fv(vertex_color_program->sun_color_vec3, 1, glm::value_ptr(glm::vec3(0.81f, 0.81f, 0.76f)));
	glUniform3fv(vertex_color_program->sun_direction_vec3, 1, glm::value_ptr(glm::normalize(glm::vec3(-0.2f, 0.2f, 1.0f))));
	glUniform3fv(vertex_color_program->sky_color_vec3, 1, glm::value_ptr(glm::vec3(0.4f, 0.4f, 0.45f)));
	glUniform3fv(vertex_color_program->sky_direction_vec3, 1, glm::value_ptr(glm::vec3(0.0f, 1.0f, 0.0f)));
	glUseProgram(0);

	//fix aspect ratio of camera
	camera->aspect = drawable_size.x / float(drawable_size.y);

	scene.draw(camera);

	if (Mode::current.get() == this) {
		glDisable(GL_DEPTH_TEST);
		std::string message;
		if (!mouse_captured) {
      if (!mouse_captured) {
        message = "CLICK TO GRAB MOUSE";
      }
      float height = 0.06f;
      float width = text_width(message, height);
      draw_text(message, glm::vec2(-0.5f * width, -0.99f), height, glm::vec4(0.0f, 0.0f, 0.0f, 0.5f));
      draw_text(message, glm::vec2(-0.5f * width, -1.0f), height, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
		}

		glUseProgram(0);
	}

	GL_ERRORS();
}


void MainMode::show_pause_menu() {
	std::shared_ptr< MenuMode > menu = std::make_shared< MenuMode >();

	std::shared_ptr< Mode > game = shared_from_this();
	menu->background = game;

	menu->choices.emplace_back("PAUSED");
	menu->choices.emplace_back("RESUME", [game](){
		Mode::set_current(game);
	});
	menu->choices.emplace_back("QUIT", [](){
		Mode::set_current(nullptr);
	});

	menu->selected = 1;

	Mode::set_current(menu);
}
