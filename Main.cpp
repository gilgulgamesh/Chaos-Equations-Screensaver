#include <SFML/Graphics.hpp>
#include <SFML/OpenGL.hpp>
#include <iostream>
#include <random>
#include <sstream>
#include <cassert>
#include <fstream>
#include "resource.h"
#include <chrono>

//The dreaded windows include file...
#define WIN32_LEAN_AND_MEAN //Reduce compile time of windows.h
#include <Windows.h>
#undef min
#undef max

//Global constants
static const int num_params = 18;
static const int iters = 800; //800
static const int steps_per_frame = 1000; //500
static const double delta_per_step = 1e-5;
static const double delta_minimum = 1e-7;
static const double t_start = -3.0;
static const double t_end = 3.0;
static bool fullscreen = false;
static int equCount = 0;
static int zoomCount = 0;
static int iterCount = 0; //haven't added this yet
static int skipCount = 0;
static bool show_rendering = true;
static double frame_ms = 0.0;



//Global variables
static int window_w = 1600;
static int window_h = 900;
static int window_bits = 24;
static float plot_scale = 0.5f;
static float plot_x = 0.0f;
static float plot_y = 0.0f;
static std::mt19937 rand_gen;
static sf::Font font;
static sf::Text equ_text;
static std::string equ_code;
static sf::RectangleShape equ_box;
static sf::Text t_text;
static sf::RectangleShape t_box;
static sf::RenderTexture render_tex;
static bool upscale = true;
int auto_center_timer = 0;


static sf::Color GetRandColor(int i) {
  i += 1;
  int r = std::min(255, 50 + (i * 11909) % 256);
  int g = std::min(255, 50 + (i * 52973) % 256);
  int b = std::min(255, 50 + (i * 44111) % 256);
  return sf::Color(r, g, b, 16);
}
static sf::Vector2f ToScreen(double x, double y) {
    const float s = plot_scale * (upscale ? float(window_h) : float(window_h / 2));
    const float cx = upscale ? float(window_w) : float(window_w) * 0.5f;
    const float cy = upscale ? float(window_h) : float(window_h) * 0.5f;
    const float nx = cx + (float(x) - plot_x) * s;
    const float ny = cy + (float(y) - plot_y) * s;
    return sf::Vector2f(nx, ny);
}

static void RandParams(double* params) {
  std::uniform_int_distribution<int> rand_int(0, 3);
  for (int i = 0; i < num_params; ++i) {
    const int r = rand_int(rand_gen);
    if (r == 0) {
      params[i] = 1.0f;
    } else if (r == 1) {
      params[i] = -1.0f;
    } else {
      params[i] = 0.0f;
    }
  }
}

static std::string ParamsToString(const double* params) {
  const char base27[] = "_ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  static_assert(num_params % 3 == 0, "Params must be a multiple of 3");
  int a = 0;
  int n = 0;
  std::string result;
  for (int i = 0; i < num_params; ++i) {
    a = a*3 + int(params[i]) + 1;
    n += 1;
    if (n == 3) {
      result += base27[a];
      a = 0;
      n = 0;
    }
  }
  return result;
}

static void StringToParams(const std::string& str, double* params) {
  for (int i = 0; i < num_params/3; ++i) {
    int a = 0;
    const char c = (i < str.length() ? str[i] : '_');
    if (c >= 'A' && c <= 'Z') {
      a = int(c - 'A') + 1;
    } else if (c >= 'a' && c <= 'z') {
      a = int(c - 'a') + 1;
    }
    params[i*3 + 2] = double(a % 3) - 1.0;
    a /= 3;
    params[i*3 + 1] = double(a % 3) - 1.0;
    a /= 3;
    params[i*3 + 0] = double(a % 3) - 1.0;
  }
}

static sf::RectangleShape MakeBoundsShape(const sf::Text& text) {
  sf::RectangleShape blackBox;
  const sf::FloatRect textBounds = text.getGlobalBounds();
  blackBox.setPosition(textBounds.left, textBounds.top);
  blackBox.setSize(sf::Vector2f(textBounds.width, textBounds.height));
  blackBox.setFillColor(sf::Color::Black);
  return blackBox;
}

#define SIGN_OR_SKIP(i, x) \
  if (params[i] != 0.0) { \
    if (isFirst) { \
      if (params[i] == -1.0) ss << "-"; \
    } else { \
      if (params[i] == -1.0) ss << " - "; \
      else ss << " + "; \
    } \
    ss << x; \
    isFirst = false; \
  } 
static std::string MakeEquationStr(double* params) {
  std::stringstream ss;
  bool isFirst = true;
  SIGN_OR_SKIP(0, "x\u00b2");
  SIGN_OR_SKIP(1, "y\u00b2");
  SIGN_OR_SKIP(2, "t\u00b2");
  SIGN_OR_SKIP(3, "xy");
  SIGN_OR_SKIP(4, "xt");
  SIGN_OR_SKIP(5, "yt");
  SIGN_OR_SKIP(6, "x");
  SIGN_OR_SKIP(7, "y");
  SIGN_OR_SKIP(8, "t");
  return ss.str();
}

static void ResetPlot() {

    if (fullscreen) {
        std::uniform_real_distribution<float> rand_offset(-1.0f, 1.0f);
        std::uniform_real_distribution<float> rand_scale( -2.5f, 2.5f);
        plot_scale = 0.5f; //rand_scale(rand_gen);
        plot_x = 0.0f; // 3 * rand_offset(rand_gen);
        plot_y = 0.0f; // 2 * rand_offset(rand_gen);
        auto_center_timer = 0;

    }
    else {
        plot_scale = 0.5f;
        plot_x = 0.0f;
        plot_y = 0.0f;
        auto_center_timer = 0;

    }

}

static void GenerateNew(sf::RenderWindow& window, double& t, double* params) {
    equCount++;
    zoomCount = 0;
    iterCount = 0;
    skipCount = 0;

    t = t_start;
  equ_code = ParamsToString(params);
  const std::string equation_str =
    "x' = " + MakeEquationStr(params) + "\n"
    "y' = " + MakeEquationStr(params + num_params / 2) + "\n"
    "Code: " + equ_code;
  equ_text.setCharacterSize(30);
  equ_text.setFont(font);
  equ_text.setString(equation_str);
  equ_text.setFillColor(fullscreen ? sf::Color::Yellow : sf::Color::White);
  equ_text.setPosition(10.0f, 10.0f);
  equ_box = MakeBoundsShape(equ_text);
  if (fullscreen) equ_box.setFillColor(sf::Color::Black);
  if (upscale) render_tex.clear();
  window.clear();
}

static void MakeTText(double t) {
  t_text.setCharacterSize(30);
  t_text.setFont(font);
  t_text.setString("t = " + std::to_string(t));
  t_text.setFillColor(fullscreen ? sf::Color::Yellow : sf::Color::White);
  t_text.setPosition(window_w - 200.0f, 10.0f);
  t_box = MakeBoundsShape(t_text);
  if (fullscreen) t_box.setFillColor(sf::Color::Black);
}

static void CreateRenderWindow(sf::RenderWindow& window) {
  //GL settings
  sf::ContextSettings settings;
  settings.depthBits = 24;
  settings.stencilBits = 8;
  settings.antialiasingLevel = 8;
  settings.majorVersion = 3;
  settings.minorVersion = 0;
  const sf::VideoMode desktopMode = sf::VideoMode::getDesktopMode();
  if (fullscreen) {
      window_w = desktopMode.width;
      window_h = desktopMode.height;
      window_bits = desktopMode.bitsPerPixel;
  }
  else {
      window_w = 1600;
      window_h = 900;
  }

  //Create the window
  const sf::VideoMode screenSize(window_w, window_h, window_bits);
  window.create(screenSize, "Chaos Equations", (fullscreen ? sf::Style::Fullscreen : sf::Style::Close), settings);
  window.setVerticalSyncEnabled(true);
  window.setActive(false);
  window.requestFocus();
  window.setMouseCursorVisible(!fullscreen);
  if (upscale) {
      render_tex.create(window_w * 2, window_h * 2);
      render_tex.setSmooth(true);
  }
}
static void CenterPlot(const std::vector<sf::Vector2f>& history, float coverage) {
    float min_x = FLT_MAX;
    float max_x = -FLT_MAX;
    float min_y = FLT_MAX;
    float max_y = -FLT_MAX;
    for (size_t i = 0; i < history.size(); ++i) {
        if (std::abs(history[i].x) > 2.0f || std::abs(history[i].y) > 2.0f) continue;
        min_x = std::fmin(min_x, history[i].x);
        max_x = std::fmax(max_x, history[i].x);
        min_y = std::fmin(min_y, history[i].y);
        max_y = std::fmax(max_y, history[i].y);
    }
    float mean_x = 0.0f, mean_y = 0.0f;
    int valid_count = 0;
    for (size_t i = 0; i < history.size(); ++i) {
        if (std::abs(history[i].x) > 2.0f || std::abs(history[i].y) > 2.0f) continue;
        mean_x += history[i].x;
        mean_y += history[i].y;
        valid_count++;
    }
    if (valid_count > 0) {
        plot_x = mean_x / valid_count;
        plot_y = mean_y / valid_count;
        zoomCount++;

    }
   // plot_x = (max_x + min_x) * 0.5f;
    //plot_y = (max_y + min_y) * 0.5f;

    float scale_factor = upscale ? 1.0f : 0.5f;
    plot_scale = scale_factor / std::max(std::max(max_x - min_x, max_y - min_y) * coverage, 0.1f);
}

struct Res {
  Res(int id) {
    HRSRC src = ::FindResource(NULL, MAKEINTRESOURCE(id), RT_RCDATA);
    ptr = ::LockResource(::LoadResource(NULL, src));
    size = (size_t)::SizeofResource(NULL, src);
  }
  void* ptr;
  size_t size;
};

int main(int argc, char *argv[]) {
  std::cout << "=========================================================" << std::endl;
  std::cout << std::endl;
  std::cout << "                      Chaos Equations" << std::endl;
  std::cout << std::endl;
  std::cout << "    These are plots of random recursive equations, which" << std::endl;
  std::cout << "often produce chaos, and results in beautiful patterns." << std::endl;
  std::cout << "For every time t, a point (x,y) is initialized to (t,t)." << std::endl;
  std::cout << "The equation is applied to the point many times, and each" << std::endl;
  std::cout << "iteration is drawn in a unique color." << std::endl;
  std::cout << std::endl;
  std::cout << "=========================================================" << std::endl;
  std::cout << std::endl;
  std::cout << "Controls:" << std::endl;
  std::cout << "      'A' - Automatic Mode (randomize equations)" << std::endl;
  std::cout << "      'R' - Repeat Mode (keep same equation)" << std::endl;
  std::cout << std::endl;
  std::cout << "      'C' - Center points" << std::endl;
  std::cout << "      'D' - Dot size Toggle" << std::endl;
  std::cout << "      'I' - Iteration Limit Toggle" << std::endl;
  std::cout << "      'T' - Trail Toggle" << std::endl;
  std::cout << std::endl;
  std::cout << "      'P' - Pause" << std::endl;
  std::cout << " 'LShift' - Slow Down" << std::endl;
  std::cout << " 'RShift' - Speed Up" << std::endl;
  std::cout << "  'Space' - Reverse" << std::endl;
  std::cout << std::endl;
  std::cout << "     'N' - New Equation (random)" << std::endl;
  std::cout << "     'L' - Load Equation" << std::endl;
  std::cout << "     'S' - Save Equation" << std::endl;
  std::cout << std::endl;

  //Set random seed
  rand_gen.seed((unsigned int)time(0));

  //Load the font
  const Res res_font(IDR_FONT);
  if (!font.loadFromMemory(res_font.ptr, res_font.size)) {
    std::cerr << "FATAL: Failed to load font." << std::endl;
    system("pause");
    return 1;
  }

  //Create the window
  sf::RenderWindow window;
  CreateRenderWindow(window);

  //Simulation variables
  double t = t_start;
  std::vector<sf::Vector2f> history(iters);
  double rolling_delta = delta_per_step;
  double params[num_params];
  double speed_mult = 1.0;
  bool paused = false;
  int trail_type = 0;
  int dot_type = 0;
  bool load_started = false;
  bool shuffle_equ = false;
  bool iteration_limit = false;
  int skip_timeout = 0;
  int center_timeout = 0;

  //Setup the vertex array
  std::vector<sf::Vertex> vertex_array(iters*steps_per_frame);
  for (size_t i = 0; i < vertex_array.size(); ++i) {
    vertex_array[i].color = GetRandColor(i % iters);
  }

  //Initialize random parameters
  ResetPlot();
  RandParams(params);
  GenerateNew(window, t, params);

  //Main Loop
  while (true) {
    while (window.isOpen()) {
      sf::Event event;
      static bool ready = false;
      static int frame_count = 0;
      if (!ready) { frame_count++; if (frame_count > 80) ready = true; }

      auto frame_start = std::chrono::high_resolution_clock::now();


      while (window.pollEvent(event)) {
          if (event.type == sf::Event::Closed) {
              window.close();
              break;
      }
      else if (fullscreen && ready && event.type == sf::Event::MouseButtonPressed) {
          window.close();
          break;
        } else if (event.type == sf::Event::KeyPressed) {
          const sf::Keyboard::Key keycode = event.key.code;
          if (keycode == sf::Keyboard::Escape) {
            window.close();
            break;

          }
          else if (fullscreen && keycode == sf::Keyboard::Space) {
              window.close();
              break;
          } else if (keycode == sf::Keyboard::A) {
            shuffle_equ = true;
          } else if (keycode == sf::Keyboard::C) {
            CenterPlot(history, 0.6f);
          } else if (keycode == sf::Keyboard::D) {
            dot_type = (dot_type + 1) % 3;
          }
          else if (keycode == sf::Keyboard::F) {
              fullscreen = !fullscreen;
              CreateRenderWindow(window);
              GenerateNew(window, t, params);
          }
          else if (keycode == sf::Keyboard::G) {
              show_rendering = !show_rendering;
          
          
          } else if (keycode == sf::Keyboard::I) {
            iteration_limit = !iteration_limit;
          } else if (keycode == sf::Keyboard::L) {
            shuffle_equ = false;
            load_started = true;
            paused = false;
            window.close();
          } else if (keycode == sf::Keyboard::N) {
            ResetPlot();
            RandParams(params);
            GenerateNew(window, t, params);
          } else if (keycode == sf::Keyboard::P) {
            paused = !paused;
          } else if (keycode == sf::Keyboard::R) {
            shuffle_equ = false;
          } else if (keycode == sf::Keyboard::S) {
            std::ofstream fout("saved.txt", std::ios::app);
            fout << equ_code << std::endl;
            std::cout << "Saved: " << equ_code << std::endl;
          } else if (keycode == sf::Keyboard::T) {
            trail_type = (trail_type + 1) % 4;
          }
          else if (keycode == sf::Keyboard::U) {
              upscale = !upscale;
              CreateRenderWindow(window);
              GenerateNew(window, t, params);
          }
        }
      }

      //Change simulation speed if using shift modifiers
      if (sf::Keyboard::isKeyPressed(sf::Keyboard::LShift)) {
        speed_mult = 0.1;
      } else if (sf::Keyboard::isKeyPressed(sf::Keyboard::RShift)) {
        speed_mult = 10.0;
      } else {
        speed_mult = 1.0;
      }
      if (sf::Keyboard::isKeyPressed(sf::Keyboard::Space)) {
        speed_mult = -speed_mult;
      }

      //Skip all drawing if paused
      if (paused) {
        window.display();
        continue;
      }

      //Automatic restart
      if (t > t_end) {
        if (shuffle_equ) {
          ResetPlot();
          RandParams(params);
        }
        GenerateNew(window, t, params);
      }

      sf::BlendMode fade(sf::BlendMode::One, sf::BlendMode::One, sf::BlendMode::ReverseSubtract);
      sf::RenderStates renderBlur(fade);

      sf::RectangleShape fullscreen_rect;
      fullscreen_rect.setPosition(0.0f, 0.0f);
      fullscreen_rect.setSize(sf::Vector2f(upscale ? window_w * 2 : window_w, upscale ? window_h * 2 : window_h));

      static const sf::Uint8 fade_speeds[] = { 10,4,0,255 };
      const sf::Uint8 fade_speed = fade_speeds[trail_type];
      if (fade_speed >= 1) {
          fullscreen_rect.setFillColor(sf::Color(fade_speed, fade_speed, fade_speed, 0));
          if (upscale) render_tex.draw(fullscreen_rect, renderBlur);
          else window.draw(fullscreen_rect, renderBlur);
      }


      //Smooth out the stepping speed.
      const int steps = steps_per_frame;
      const double delta = delta_per_step * speed_mult;
      rolling_delta = rolling_delta*0.99 + delta*0.01;

      //Apply chaos
      int inBoxCount = 0;
      int visibleCount = 0;


      for (int step = 0; step < steps; ++step) {
          double x = t;
          double y = t;

          for (int iter = 0; iter < iters; ++iter) {
              const double xx = x * x;
              const double yy = y * y;
              const double tt = t * t;
              const double xy = x * y;
              const double xt = x * t;
              const double yt = y * t;
              const double nx = xx * params[0] + yy * params[1] + tt * params[2] + xy * params[3] + xt * params[4] + yt * params[5] + x * params[6] + y * params[7] + t * params[8];
              const double ny = xx * params[9] + yy * params[10] + tt * params[11] + xy * params[12] + xt * params[13] + yt * params[14] + x * params[15] + y * params[16] + t * params[17];
              x = nx;
              y = ny;
              sf::Vector2f screenPt = ToScreen(x, y);
              if (iteration_limit && iter < 100) {
                  screenPt.x = FLT_MAX;
                  screenPt.y = FLT_MAX;
                  iterCount++;
              }
              vertex_array[step * iters + iter].position = screenPt;

              //Check if dynamic delta should be adjusted

              float bound_w = upscale ? window_w * 2 : window_w;
              float bound_h = upscale ? window_h * 2 : window_h;
     
              if (std::abs(float(x)) < 2.0f && std::abs(float(y)) < 2.0f) {
                  const float dx = history[iter].x - float(x);
                  const float dy = history[iter].y - float(y);
                  const double dist = double(500.0f * std::sqrt(dx * dx + dy * dy));
                  rolling_delta = std::min(rolling_delta, std::max(delta / (dist + 1e-5), delta_minimum * speed_mult));
                  inBoxCount++;
              }
              if (screenPt.x > 0.0f && screenPt.y > 0.0f && screenPt.x < bound_w && screenPt.y < bound_h) {
                  visibleCount++;
              }
              history[iter].x = float(x);
              history[iter].y = float(y);
          }

          //Update the t variable
          if (inBoxCount == 0) {
              rolling_delta = delta;
              t += 0.01;
              skipCount++;
          }
          else if (visibleCount <= inBoxCount - visibleCount) {
              ResetPlot();
              t += rolling_delta;
          }
          else {
              t += rolling_delta;
          }
          


      }


      auto_center_timer++;
      if (auto_center_timer >= 60 * 10) {
          CenterPlot(history, 0.4);
          auto_center_timer = 0;
      }

      if (show_rendering) {
          // all existing draw calls


          //Draw new points
          static const float dot_sizes[] = { 2.0f, 5.0f, 1.0f };
          glEnable(GL_POINT_SMOOTH);
          glPointSize(dot_sizes[dot_type]);
          if (upscale) render_tex.draw(vertex_array.data(), vertex_array.size(), sf::PrimitiveType::Points);
          else window.draw(vertex_array.data(), vertex_array.size(), sf::PrimitiveType::Points);




          //Draw the equation
          if (upscale) {
              render_tex.display();
              sf::Sprite sprite(render_tex.getTexture());
              sprite.setScale(0.5f, 0.5f);
              window.draw(sprite);
          }
      }


      auto frame_end = std::chrono::high_resolution_clock::now();
      frame_ms = frame_ms * 0.95 + std::chrono::duration<double, std::milli>(frame_end - frame_start).count() * 0.05;


      window.draw(equ_box);
      window.draw(equ_text);

      //Draw the current t-value
      MakeTText(t);
      window.draw(t_box);
      window.draw(t_text);


      sf::Text debug_text;
      
      debug_text.setCharacterSize(20);
      debug_text.setFont(font);
      debug_text.setString("scale: " + std::to_string(plot_scale) + "\ncx: " + std::to_string(plot_x) + "\ncy: " + std::to_string(plot_y));
      debug_text.setFillColor(sf::Color::Yellow);
      debug_text.setPosition(window_w - 250.0f, window_h - debug_text.getGlobalBounds().height - 150.0f);
      debug_text.setString("scale: " + std::to_string(plot_scale) +
          "\nVisible: " + std::to_string(visibleCount / steps_per_frame) +
          "\nMissing: " + std::to_string((inBoxCount - visibleCount) / steps_per_frame) +
          "\nequ: " + std::to_string(equCount) +
          "\nzooms: " + std::to_string(zoomCount) +
          "\nframe: " + std::to_string(frame_ms) + "ms" +
          "\nsteps per" + std::to_string(steps_per_frame) +

          "\niskips " + std::to_string(skipCount) +
          "\niskips " + std::to_string(iterCount / 100000));

      sf::RectangleShape debug_bg;
      debug_bg.setPosition(window_w - 260.0f, window_h - 200.0f);
      debug_bg.setSize(sf::Vector2f(260.0f, 200.0f));
      debug_bg.setFillColor(sf::Color::Black);
      window.draw(debug_bg);
    
      window.draw(debug_text);



      //Flip the screen buffer
      window.display();
    }



    if (load_started) {
      std::string code;
      std::cout << "Enter 6 letter code:" << std::endl;
      std::cin >> code;
      CreateRenderWindow(window);
      ResetPlot();
      StringToParams(code, params);
      GenerateNew(window, t, params);
      load_started = false;
    } else {
      break;
    }
  }

  return 0;
}
