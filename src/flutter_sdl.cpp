#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#endif

#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <SDL2/SDL_keyboard.h>
#include <stdio.h>

#include <chrono>
#include <string_view>

#include <cstdint>

#include <embedder.h>
#include "json.hpp"

static_assert(FLUTTER_ENGINE_VERSION == 1, "");

using json = nlohmann::json;

constexpr size_t kInitialWidth = 1024;
constexpr size_t kInitialHeight = 768;

static int captionWidth = 0;
static bool maximized = false;

constexpr float scaleFactor = 1.0f;

#pragma region Window Creation
#ifdef _WIN32

#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>

INT_PTR CALLBACK MyDialogProc( HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam );

static WNDPROC defaultProc = &DefWindowProc;

#ifndef SM_CXPADDEDBORDER
constexpr int SM_CXPADDEDBORDER = 92;
#endif

int captionHeight() {
  return GetSystemMetrics(SM_CYCAPTION);
}

int captionBorders() {
  return GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
}

// Windows specific mess to spawn an SDL window with fancy bits
SDL_Window *makeWindow(HINSTANCE hInstance, int nCmdShow) {
  WNDCLASS wc = {0};

  wc.lpfnWndProc   = MyDialogProc;
  wc.hInstance     = hInstance;
  wc.lpszClassName = L"Thing";
  wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);

  RegisterClass(&wc);

  auto hwnd = CreateWindowEx(
    0,
    L"Thing",
    L"Thing",
    WS_POPUP|WS_CAPTION|DS_CENTER|WS_THICKFRAME|WS_MINIMIZEBOX|WS_MAXIMIZEBOX,
    CW_USEDEFAULT, CW_USEDEFAULT, kInitialWidth, kInitialHeight,
    nullptr, nullptr, hInstance, nullptr
  );

  ShowWindow(hwnd, nCmdShow);

  SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)DefWindowProc);

  auto dummyWindow = SDL_CreateWindow("", 0, 0, 1, 1, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN | SDL_WINDOW_ALLOW_HIGHDPI);

  char sBuf[32];
  sprintf_s<32>(sBuf, "%p", dummyWindow);

  SDL_SetHint(SDL_HINT_VIDEO_WINDOW_SHARE_PIXEL_FORMAT, sBuf);

  auto win = SDL_CreateWindowFrom(hwnd);

  SDL_SetHint(SDL_HINT_VIDEO_WINDOW_SHARE_PIXEL_FORMAT, nullptr);

  defaultProc = (WNDPROC)GetWindowLongPtr(hwnd, GWLP_WNDPROC);

  SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)MyDialogProc);

  return win;
}

INT_PTR CALLBACK MyDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
  LRESULT lr = 0;
  // Ask whether DWM would like to process the incoming message.
  auto dwm_processed = DwmDefWindowProc(hDlg, message, wParam, lParam, &lr);

  switch (message) {
    // case WM_DWMCOMPOSITIONCHANGED:
    case WM_ACTIVATE: {
      // This plays together with WM_NCALCSIZE.

      MARGINS m  = { 0, 0, 2, 0 };
      DwmExtendFrameIntoClientArea( hDlg, &m );

      // Force the system to recalculate NC area (making it send WM_NCCALCSIZE).
      SetWindowPos( hDlg, nullptr, 0, 0, 0, 0, 
        SWP_NOZORDER|SWP_NOOWNERZORDER|SWP_NOMOVE|SWP_NOSIZE|SWP_FRAMECHANGED);

      break;
    }
    case WM_NCCALCSIZE: {
      // Returning 0 from the message when wParam is TRUE removes the standard
      // frame, but keeps the window shadow.
      if (wParam) {
        auto info = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);

        auto border = GetSystemMetrics(SM_CXFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
        info->rgrc[0].left += border;
        info->rgrc[0].right -= border;
        info->rgrc[0].bottom -= border;

        if (IsMaximized(hDlg)) {
          // Prevents the top of the screen from being cut off when maximized
          info->rgrc[0].top += captionBorders();
        }
        return 0;
      }
      return 0;
    }
    case WM_NCHITTEST: {
      // Returning HTCAPTION allows the user to move the window around by clicking 
      // anywhere.
      // Depending on the mouse coordinates passed in LPARAM, you may 
      // return other values to enable resizing.
      if (lr == 0) {
        auto result = DefWindowProc(hDlg, message, wParam, lParam);
        if (result == HTCLIENT) {
          auto xPos = GET_X_LPARAM(lParam); 
          auto yPos = GET_Y_LPARAM(lParam);

          RECT windowRect;
          GetWindowRect(hDlg, &windowRect);
          xPos -= windowRect.left;
          yPos -= windowRect.top;

          if (yPos < captionHeight()) {
            if (yPos < 4) {
              return HTTOP;
            } else {
              if (captionWidth == 0 || xPos < captionWidth) {
                return HTCAPTION;
              }
            }
          }

          // return HTCAPTION;
          return HTCLIENT;
        } else {
          return result;
        }
      }
      break;
    }
  }
  
  if (!dwm_processed) {
    if (message == WM_NCHITTEST || message == WM_NCCALCSIZE) {
      return DefWindowProc(hDlg, message, wParam, lParam);
    } else {
      return defaultProc(hDlg, message, wParam, lParam);
    }
  } else {
    return lr;
  }
}

#else

SDL_Window *makeWindow() {
  return SDL_CreateWindow(
    "hello_sdl2",
    SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
    kInitialWidth, kInitialHeight,
    SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI
  );
}

#endif
#pragma endregion

void updateSize(FlutterEngine engine, size_t width, size_t height, float pixelRatio, bool maximized) {
  printf("%dx%d@%f\n", width, height, pixelRatio);
  FlutterWindowMetricsEvent event = {0};
  event.struct_size = sizeof(event);
  event.width = width * scaleFactor;
  event.height = height * scaleFactor;
  event.pixel_ratio = pixelRatio * scaleFactor;
#ifdef _WIN32
  // Windows gives us DPI-aware system metrics, but FlutterWindowMetrics wants physical size
  event.padding_top = (captionHeight() + (maximized ? 0 : captionBorders())); // pixelRatio;
#endif
  // event.padding_top = kWindowTitleBarSize * event.pixel_ratio;
  // event.padding_bottom = event.padding_right = event.padding_left = kWindowBorderSize * event.pixel_ratio;
  FlutterEngineSendWindowMetricsEvent(engine, &event);
}

void updatePointer(FlutterEngine engine, FlutterPointerPhase phase, double x, double y, size_t timestamp) {
  FlutterPointerEvent event = {};
  event.struct_size = sizeof(event);
  event.phase = phase;
  event.x = x * scaleFactor;
  event.y = y * scaleFactor;
  event.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::high_resolution_clock::now().time_since_epoch())
          .count();
  FlutterEngineSendPointerEvent(reinterpret_cast<FlutterEngine>(engine), &event, 1);
}

void sendPlatformMessage(FlutterEngine engine, const char *channel, const json &message) {
  FlutterPlatformMessage platformMessage = {0};

  platformMessage.struct_size = sizeof(FlutterPlatformMessage);
  platformMessage.channel = channel;
  std::string msg = message.dump();
  platformMessage.message = reinterpret_cast<const uint8_t*>(msg.c_str());
  platformMessage.message_size = msg.length();

  FlutterEngineSendPlatformMessage(
    engine,
    &platformMessage
  );
}

void sendTextEditing(FlutterEngine engine, const SDL_TextEditingEvent &event) {
  sendPlatformMessage(engine, "flit/sdl/textinput", json {
    {"method", "textEditing"},
    {"args", {
      {"text", event.text},
      {"start", event.start},
      {"length", event.length}
    }}
  });
}

void sendTextInput(FlutterEngine engine, const SDL_TextInputEvent &event) {
  sendPlatformMessage(engine, "flit/sdl/textinput", json {
    {"method", "textInput"},
    {"args", event.text}
  });
}

void sendKeyInput(FlutterEngine engine, const SDL_KeyboardEvent &event) {
  sendPlatformMessage(engine, "flit/sdl/textinput", json {
    {"method", "keyInput"},
    {"args", {
      event.keysym.sym,
      event.keysym.mod,
      event.type == SDL_KEYDOWN ? (event.repeat ? 1 : 0) : 2,
    }}
  });
}

void handleTextInputMethod(SDL_Window *window, const std::string_view &contents) {
  auto j = json::parse(contents.begin(), contents.end());
  const std::string &method = j["method"];
  if (method == "start") {
    SDL_StartTextInput();
  } else if (method == "stop") {
    SDL_StopTextInput();
  }
}

void handleTitleBarMethod(SDL_Window *window, const std::string_view &contents) {
  auto j = json::parse(contents.begin(), contents.end());
  const std::string &method = j["method"];
  if (method == "updateMetrics") {
    captionWidth = static_cast<float>(j["args"][0]);
  } else if (method == "close") {
    SDL_Event event = {0};
    event.type = SDL_QUIT;
    SDL_PushEvent(&event);
  } else if (method == "restore") {
    if (maximized) {
      SDL_RestoreWindow(window);
    } else {
      SDL_MaximizeWindow(window);
    }
  } else if (method == "minimize") {
    SDL_MinimizeWindow(window);
  }
}

void updateMaximizeState(FlutterEngine engine) {
  sendPlatformMessage(engine, "flit/titlebar", json {
    {"method", "updateMaximizeState"},
    {"args", maximized}
  });
}

void messageCallback(const FlutterPlatformMessage *message, void *userData) {
	auto channel = std::string_view(message->channel);
	auto messageContents = std::string_view(reinterpret_cast<const char*>(message->message), message->message_size);
	auto window = reinterpret_cast<SDL_Window*>(userData);
	
  fwrite(channel.data(), 1, channel.size(), stdout);
  fputs(": ", stdout);
  fwrite(messageContents.data(), 1, messageContents.size(), stdout);
  fputc('\n', stdout);
  fflush(stdout);

  if (channel == "flit/titlebar") {
    // Update metrics (just caption width for now)
    handleTitleBarMethod(window, messageContents);
  } else if (channel == "flit/sdl/textinput") {
    handleTextInputMethod(window, messageContents);
  }
}

FlutterEngine RunFlutter(SDL_Window *window, SDL_GLContext context) {
  SDL_SetWindowData(window, "GL", context);

  FlutterRendererConfig config = {};
  config.type = kOpenGL;
  config.open_gl.struct_size = sizeof(config.open_gl);
  config.open_gl.make_current = [](void *userdata) -> bool {
    auto window = (SDL_Window*)userdata;
    SDL_GL_MakeCurrent(window, SDL_GetWindowData(window, "GL"));
    return true;
  };
  config.open_gl.clear_current = [](void *) -> bool {
    SDL_GL_MakeCurrent(nullptr, nullptr);
    return true;
  };
  config.open_gl.present = [](void *userdata) -> bool {
    auto window = (SDL_Window*)userdata;
    SDL_GL_SwapWindow(window);
    SDL_Event event = {0};
    event.type = SDL_USEREVENT;
    SDL_PushEvent(&event);
    return true;
  };
  config.open_gl.fbo_callback = [](void *) -> uint32_t {
    return 0; // FBO0
  };

#ifndef MY_PROJECT
#define MY_PROJECT "C:\\Users\\ds841\\Documents\\flutter\\examples\\flutter_gallery\\"
#endif

  char fullPathBuffer[256] = {};
  GetFullPathNameA(MY_PROJECT, 256, fullPathBuffer, nullptr);
  printf("%s\n", fullPathBuffer);
  auto str = std::string(fullPathBuffer) + "lib/main.dart";

  FlutterProjectArgs args = {0};
  args.struct_size = sizeof(FlutterProjectArgs);
  args.assets_path = MY_PROJECT "build\\flutter_assets";
  args.main_path = str.c_str();
  args.packages_path = MY_PROJECT ".packages";
  args.platform_message_callback = &messageCallback;
  FlutterEngine engine = nullptr;
  auto result = FlutterEngineRun(FLUTTER_ENGINE_VERSION, &config, // renderer
                 &args, window, &engine);
  // assert(result == kSuccess && engine != nullptr);

  // glfwSetWindowUserPointer(window, engine);

  // GLFWwindowSizeCallback(window, kInitialWindowWidth, kInitialWindowHeight);
  int w, h;
  SDL_GetWindowSize(window, &w, &h);
  float ddpi = 1.0f;
  SDL_GetDisplayDPI(SDL_GetWindowDisplayIndex(window), &ddpi, nullptr, nullptr);
  updateSize(engine, w, h, ddpi / 96.0f, SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED);

  return engine;
}

#ifdef _WIN32
int WinMain(_In_ HINSTANCE hInstance,
      _In_opt_ HINSTANCE hPrevInstance,
      _In_ LPSTR    lpCmdLine,
      _In_ int       nCmdShow) {
#else
int main() {
#endif

#ifdef _WIN32
  typedef BOOL (WINAPI *SetProcessDPIAware_t)(void);
  HMODULE hMod = LoadLibrary(L"user32.dll");
  if ( hMod ) {
    SetProcessDPIAware_t pSetProcessDPIAware = (SetProcessDPIAware_t)GetProcAddress(hMod, "SetProcessDPIAware");
    if ( pSetProcessDPIAware ) {
      pSetProcessDPIAware();
    }
    FreeLibrary( hMod );
  }
#endif

  SDL_Window* window = NULL;
  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    fprintf(stderr, "could not initialize sdl2: %s\n", SDL_GetError());
    return 1;
  }

  /* Request opengl 3.2 context.
   * SDL doesn't have the ability to choose which profile at this time of writing,
   * but it should default to the core profile */
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);

  /* Turn on double buffering with a 24bit Z buffer.
   * You may need to change this to 16 or 32 for your system */
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

#ifdef _WIN32
  window = makeWindow(hInstance, nCmdShow);
#else
  window = makeWindow();
#endif

  if (window == NULL) {
    fprintf(stderr, "could not create window: %s\n", SDL_GetError());
    return 1;
  }
  
  auto context = SDL_GL_CreateContext(window);

  SDL_GL_SetSwapInterval(1);

  auto engine = RunFlutter(window, context);

  bool mouseDown = false;
  int mouseId = 0;
  int lastMouseX, lastMouseY;

  SDL_Event e;
  bool quit = false;
  while (!quit) {
    __FlutterEngineFlushPendingTasksNow();
    while (SDL_WaitEvent(&e) != 0) {
      __FlutterEngineFlushPendingTasksNow();
      if (e.type == SDL_QUIT) {
        quit = true;
        printf("Quit\n");
        fflush(stdout);
        break;
      } else if (e.type == SDL_WINDOWEVENT) {
        if (e.window.event == SDL_WINDOWEVENT_CLOSE) {
          quit = true;
          printf("Quit (window close)\n");
          fflush(stdout);
          break;
        } else if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
          int w, h;
          SDL_GetWindowSize(window, &w, &h);

          if (!(SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)) {
            float ddpi = 1.0f;
            SDL_GetDisplayDPI(SDL_GetWindowDisplayIndex(window), &ddpi, nullptr, nullptr);
            updateSize(engine, w, h, ddpi / 96.0f, SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED);
          }
        } else if (e.window.event == SDL_WINDOWEVENT_LEAVE) {
          if (mouseDown) {
            mouseDown = false;
            updatePointer(engine, FlutterPointerPhase::kUp, lastMouseX, lastMouseY, 0);
          }
        } else if (e.window.event == SDL_WINDOWEVENT_SHOWN) {
          maximized = (SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED);
          updateMaximizeState(engine);
        } else if (e.window.event == SDL_WINDOWEVENT_MAXIMIZED) {
          maximized = true;
          updateMaximizeState(engine);
        } else if (e.window.event == SDL_WINDOWEVENT_RESTORED) {
          maximized = false;
          updateMaximizeState(engine);
        }
      } else if (e.type == SDL_MOUSEBUTTONDOWN) {
        if (!mouseDown) {
          mouseDown = true;
          mouseId = e.button.which;
          lastMouseX = e.button.x;
          lastMouseY = e.button.y;
          updatePointer(engine, FlutterPointerPhase::kDown, e.button.x, e.button.y, e.button.timestamp);
        }
      } else if (e.type == SDL_MOUSEBUTTONUP) {
        if (mouseDown && mouseId == e.button.which) {
          mouseDown = false;
          lastMouseX = e.button.x;
          lastMouseY = e.button.y;
          updatePointer(engine, FlutterPointerPhase::kUp, e.button.x, e.button.y, e.button.timestamp);
        }
      } else if (e.type == SDL_MOUSEMOTION) {
        if (mouseDown && mouseId == e.motion.which) {
          lastMouseX = e.motion.x;
          lastMouseY = e.motion.y;
          updatePointer(engine, FlutterPointerPhase::kMove, e.motion.x, e.motion.y, e.motion.timestamp);
        }
      } else if (e.type == SDL_TEXTEDITING) {
        printf("Text editing: '%s' %d %d\n", e.edit.text, e.edit.start, e.edit.length);
        sendTextEditing(engine, e.edit);
      } else if (e.type == SDL_TEXTINPUT) {
        printf("Text input: '%s'\n", e.text.text);
        sendTextInput(engine, e.text);
      } else if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
        sendKeyInput(engine, e.key);
      }
      __FlutterEngineFlushPendingTasksNow();
    }
  }

  SDL_GL_DeleteContext(context);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}