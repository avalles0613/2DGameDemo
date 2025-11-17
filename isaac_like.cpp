/* READ ME
 * This is a tiny demo of a "Binding of Isaac"-style top-down 1-floor dungeon crawler game.
 * It's a small C++/Win32 2D game demo I built from scratch.
 * Demonstrates basic rendering, procedural rooms, and player/enemy logic.
 *
 * Tested on Windows 11 with C++17 (MSVC). Also includes a MinGW build command below.
 *
 * CONTROLS:
 *   WASD = Move
 *   Arrow Keys = Shoot
 *   R = Restart
 *   ESC = Quit
 *
 * Once all rooms are cleared, the run ends.
 * Press r to start a new run.
 * Press ESC to quit.
 * Build (Visual Studio / MSVC): cl /O2 /std:c++17 isaac_like.cpp user32.lib gdi32.lib
 * Build (MinGW/Clang): g++ isaac_like.cpp -std=c++17 -O2 -lgdi32 -o isaac_like.exe
 */

// isaac_like.cpp
// Tiny "Binding of Isaac"-style 1-floor demo in pure Win32 + software rendering.
// No sprites: everything is rectangles/circles. Random rooms, clear-to-unlock doors,
// simple enemies, bullets, health, a boss room, and run reset.
//
// Build (MinGW/Clang): g++ isaac_like.cpp -std=c++17 -O2 -lgdi32 -o isaac_like.exe
// Build (MSVC): cl /O2 /std:c++17 isaac_like.cpp user32.lib gdi32.lib
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <vector>
#include <array>
#include <cmath>
#include <string>
#include <random>
#include <algorithm> // for std::max, std::min, std::clamp

static const int WIDTH = 960;
static const int HEIGHT = 540;

static BITMAPINFO g_bmpInfo{};
static void* g_pixels = nullptr;
static bool       g_running = true;

inline uint32_t RGBA(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return (uint32_t(r)) | (uint32_t(g) << 8) | (uint32_t(b) << 16) | (uint32_t(a) << 24);
}
static void clear(uint32_t color) {
    uint32_t* px = (uint32_t*)g_pixels;
    std::fill(px, px + WIDTH * HEIGHT, color);
}
static void putpx(int x, int y, uint32_t c) {
    if ((unsigned)x < (unsigned)WIDTH && (unsigned)y < (unsigned)HEIGHT)
        ((uint32_t*)g_pixels)[y * WIDTH + x] = c;
}
static void fillRect(int x, int y, int w, int h, uint32_t c) {
    int x0 = std::max(0, x), y0 = std::max(0, y);
    int x1 = std::min(WIDTH, x + w), y1 = std::min(HEIGHT, y + h);
    for (int j = y0; j < y1; ++j) {
        uint32_t* row = ((uint32_t*)g_pixels) + j * WIDTH;
        for (int i = x0; i < x1; ++i) row[i] = c;
    }
}
static void drawRect(int x, int y, int w, int h, uint32_t c) {
    for (int i = x; i < x + w; ++i) { putpx(i, y, c); putpx(i, y + h - 1, c); }
    for (int j = y; j < y + h; ++j) { putpx(x, j, c); putpx(x + w - 1, j, c); }
}
static void fillCircle(int cx, int cy, int r, uint32_t c) {
    int r2 = r * r;
    int y = r;
    for (int x = 0; x <= r; ++x) {
        while (y * y + x * x > r2) --y;
        for (int i = cx - x; i <= cx + x; ++i) { putpx(i, cy + y, c); putpx(i, cy - y, c); }
    }
}
template<typename T> static T clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

struct RNG {
    std::mt19937_64 eng;
    RNG() : eng(std::random_device{}()) {}
    int  randint(int a, int b) { std::uniform_int_distribution<int> d(a, b); return d(eng); }
    float randf(float a, float b) { std::uniform_real_distribution<float> d(a, b); return d(eng); }
    bool  chance(float p) { std::bernoulli_distribution d(p); return d(eng); }
};

struct Vec {
    float x = 0, y = 0;
    Vec() = default; Vec(float X, float Y) :x(X), y(Y) {}
    Vec operator+(const Vec& o) const { return { x + o.x,y + o.y }; }
    Vec operator-(const Vec& o) const { return { x - o.x,y - o.y }; }
    Vec operator*(float s) const { return { x * s,y * s }; }
    Vec& operator+=(const Vec& o) { x += o.x; y += o.y; return *this; }
};
static float dot(const Vec& a, const Vec& b) { return a.x * b.x + a.y * b.y; }
static float len(const Vec& a) { return std::sqrt(dot(a, a)); }
static Vec norm(const Vec& a) { float L = len(a); return L > 0 ? a * (1.0f / L) : Vec(0, 0); }

enum class Dir { Up, Right, Down, Left };
static std::array<Vec, 4> DIRV{ Vec(0,-1), Vec(1,0), Vec(0,1), Vec(-1,0) };

struct Bullet {
    Vec p, v;
    float r = 4.f, ttl = 1.1f;
    bool dead = false;
};
struct Enemy {
    Vec p;
    float r = 12.f;
    float hp = 2.f;       // Boss will get more
    float speed = 55.f;
    int kind = 0;         // 0=chaser, 1=patroller
    Vec  patrolDir{ 1,0 };
    bool dead = false;
};
struct Room {
    bool exists = false;
    bool cleared = false;
    bool boss = false;
    bool doors[4] = { false,false,false,false }; // U R D L
    std::vector<Enemy> enemies;
};

struct Player {
    Vec p;
    float r = 12.f;
    float speed = 125.f;
    int hp = 6; // 3 hearts
    std::vector<Bullet> shots;
    float shotCooldown = 0.f;
} g_player;

static const int GRID_W = 5, GRID_H = 5;
static const int ROOM_W = 720, ROOM_H = 400;
static const int ROOM_X = (WIDTH - ROOM_W) / 2;
static const int ROOM_Y = (HEIGHT - ROOM_H) / 2;
static const int DOOR_W = 80, DOOR_H = 18;
static Room g_dungeon[GRID_H][GRID_W];
static int g_rx = GRID_W / 2, g_ry = GRID_H / 2; // current room
static int g_startx, g_starty;
static bool g_runOver = false;
static bool g_allCleared = false;
static RNG  g_rng;

static bool keyDown(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

static RECT doorRect(Dir d) {
    switch (d) {
    case Dir::Up:    return RECT{ ROOM_X + (ROOM_W - DOOR_W) / 2, ROOM_Y - 2, ROOM_X + (ROOM_W + DOOR_W) / 2, ROOM_Y + DOOR_H };
    case Dir::Down:  return RECT{ ROOM_X + (ROOM_W - DOOR_W) / 2, ROOM_Y + ROOM_H - DOOR_H, ROOM_X + (ROOM_W + DOOR_W) / 2, ROOM_Y + ROOM_H + 2 };
    case Dir::Left:  return RECT{ ROOM_X - 2, ROOM_Y + (ROOM_H - DOOR_W) / 2, ROOM_X + DOOR_H, ROOM_Y + (ROOM_H + DOOR_W) / 2 };
    case Dir::Right: return RECT{ ROOM_X + ROOM_W - DOOR_H, ROOM_Y + (ROOM_H - DOOR_W) / 2, ROOM_X + ROOM_W + 2, ROOM_Y + (ROOM_H + DOOR_W) / 2 };
    }
    return RECT{ 0,0,0,0 };
}
static bool circleRectOverlap(const Vec& c, float r, const RECT& rc) {
    float nx = clamp(c.x, float(rc.left), float(rc.right));
    float ny = clamp(c.y, float(rc.top), float(rc.bottom));
    float dx = c.x - nx, dy = c.y - ny;
    return dx * dx + dy * dy <= r * r;
}

static void spawnEnemies(Room& R) {
    R.enemies.clear();
    int count = g_rng.randint(2, 5);
    if (R.boss) { count = 6; }
    for (int i = 0; i < count; ++i) {
        Enemy e;
        e.p = Vec(g_rng.randf(ROOM_X + 40, ROOM_X + ROOM_W - 40),
            g_rng.randf(ROOM_Y + 40, ROOM_Y + ROOM_H - 40));
        e.kind = R.boss ? (i % 2) : g_rng.randint(0, 1);
        if (R.boss) {
            e.hp = 4.f;
            e.r = 14.f;
            e.speed = 70.f;
        }
        R.enemies.push_back(e);
    }
    R.cleared = R.enemies.empty();
}

static void carveDungeon() {
    // Reset
    for (int y = 0; y < GRID_H; ++y) for (int x = 0; x < GRID_W; ++x) g_dungeon[y][x] = Room{};
    // Random DFS from center to create ~6-9 rooms
    int targetRooms = g_rng.randint(6, 9);
    int cx = GRID_W / 2, cy = GRID_H / 2;
    g_startx = cx; g_starty = cy;
    struct Node { int x, y; };
    std::vector<Node> stack;
    std::vector<std::pair<int, int>> order;
    g_dungeon[cy][cx].exists = true; order.push_back({ cx,cy });
    stack.push_back({ cx,cy });
    int made = 1;

    auto inb = [&](int X, int Y) { return X >= 0 && Y >= 0 && X < GRID_W && Y < GRID_H; };

    while (made < targetRooms && !stack.empty()) {
        Node cur = stack.back();
        std::array<Dir, 4> dirs{ Dir::Up,Dir::Right,Dir::Down,Dir::Left };
        std::shuffle(dirs.begin(), dirs.end(), g_rng.eng);
        bool extended = false;
        for (Dir d : dirs) {
            int nx = cur.x + (d == Dir::Right ? 1 : (d == Dir::Left ? -1 : 0));
            int ny = cur.y + (d == Dir::Down ? 1 : (d == Dir::Up ? -1 : 0));
            if (!inb(nx, ny) || g_dungeon[ny][nx].exists) continue;
            // carve
            g_dungeon[cur.y][cur.x].exists = true;
            g_dungeon[ny][nx].exists = true;
            g_dungeon[cur.y][cur.x].doors[(int)d] = true;
            g_dungeon[ny][nx].doors[(int)((int(d) + 2) % 4)] = true;
            stack.push_back({ nx,ny });
            order.push_back({ nx,ny });
            ++made; extended = true;
            break;
        }
        if (!extended) stack.pop_back();
    }

    // Boss room = farthest from start among existing
    auto dist = [&](int x, int y) { int dx = x - g_startx, dy = y - g_starty; return dx * dx + dy * dy; };
    int bx = g_startx, by = g_starty, best = -1;
    for (int y = 0; y < GRID_H; ++y)for (int x = 0; x < GRID_W; ++x) {
        if (!g_dungeon[y][x].exists) continue;
        int d = dist(x, y);
        if (d > best) { best = d; bx = x; by = y; }
    }
    g_dungeon[by][bx].boss = true;

    // Populate enemies
    for (int y = 0; y < GRID_H; ++y)for (int x = 0; x < GRID_W; ++x) {
        if (g_dungeon[y][x].exists) {
            if (x == g_startx && y == g_starty) { g_dungeon[y][x].cleared = true; } // spawn room safe
            else spawnEnemies(g_dungeon[y][x]);
        }
    }
}

static void resetRun() {
    carveDungeon();
    g_rx = g_startx; g_ry = g_starty;
    g_player = Player{};
    g_player.p = Vec(ROOM_X + ROOM_W / 2.f, ROOM_Y + ROOM_H / 2.f);
    g_runOver = false;
    g_allCleared = false;
}

static void drawHUD() {
    // hearts
    int x = ROOM_X, y = ROOM_Y - 28;
    for (int i = 0; i < g_player.hp; ++i) {
        fillRect(x + i * 16, y, 14, 14, RGBA(220, 40, 40));
    }
    // room marker & tips
    std::string txt = "WASD move | Arrows shoot | R restart | ESC quit";
    // primitive text: draw tiny bars for legibility
    // (Keep simple: just draw a thin top bar as a "HUD line")
    drawRect(ROOM_X, ROOM_Y - 34, ROOM_W, 1, RGBA(255, 255, 255));
    // mini-map dots
    int mx = ROOM_X + ROOM_W - 120, my = ROOM_Y - 26;
    for (int y = 0; y < GRID_H; ++y) {
        for (int x = 0; x < GRID_W; ++x) {
            if (!g_dungeon[y][x].exists) continue;
            uint32_t c = RGBA(120, 120, 120);
            if (g_dungeon[y][x].boss) c = RGBA(200, 90, 200);
            if (x == g_rx && y == g_ry) c = RGBA(255, 255, 255);
            fillRect(mx + x * 8, my + y * 8, 6, 6, c);
        }
    }
    // win/lose banner
    if (g_runOver) {
        // in future render on screen text properly; for now just a box with fake text bars
        std::string s = (g_player.hp <= 0) ? "You Died - Press R to Retry" : "Floor Cleared! Press R for New Run";
        int bw = 450, bh = 50;
        fillRect((WIDTH - bw) / 2, (HEIGHT - bh) / 2, bw, bh, RGBA(0, 0, 0, 220));
        drawRect((WIDTH - bw) / 2, (HEIGHT - bh) / 2, bw, bh, RGBA(255, 255, 255));
        // fake "text": centered bars
        // title bar
        fillRect((WIDTH - bw) / 2 + 14, (HEIGHT - bh) / 2 + 14, bw - 28, 4, RGBA(255, 255, 255));
        // subtitle-ish
        fillRect((WIDTH - bw) / 2 + 80, (HEIGHT - bh) / 2 + 30, bw - 160, 3, RGBA(200, 200, 200));
    }
}

static void drawRoom(const Room& R) {
    // walls
    fillRect(ROOM_X, ROOM_Y, ROOM_W, ROOM_H, RGBA(20, 20, 25));
    drawRect(ROOM_X, ROOM_Y, ROOM_W, ROOM_H, RGBA(200, 200, 200));
    // doors (closed if uncleared)
    for (int i = 0; i < 4; ++i) {
        if (!R.doors[i]) continue;
        RECT rc = doorRect((Dir)i);
        bool locked = !R.cleared;
        uint32_t col = locked ? RGBA(180, 60, 60) : RGBA(100, 220, 120);
        fillRect(rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, col);
    }
    // boss tint
    if (R.boss) {
        // subtle border glow
        drawRect(ROOM_X + 3, ROOM_Y + 3, ROOM_W - 6, ROOM_H - 6, RGBA(200, 80, 200));
    }
}

static void updateEnemies(Room& R, float dt) {
    for (auto& e : R.enemies) {
        if (e.dead) continue;
        Vec toP = g_player.p - e.p;
        float d = len(toP);
        if (e.kind == 0) { // chaser
            Vec dir = norm(toP);
            e.p += dir * e.speed * dt;
        }
        else { // patrol
            // bounce patrol within inner bounds
            e.p += e.patrolDir * (e.speed * 0.75f) * dt;
            if (e.p.x < ROOM_X + 30 || e.p.x > ROOM_X + ROOM_W - 30) e.patrolDir.x *= -1;
            if (e.p.y < ROOM_Y + 30 || e.p.y > ROOM_Y + ROOM_H - 30) e.patrolDir.y *= -1;
            // occasionally nudge toward player
            if (d < 180.f) e.p += norm(toP) * (e.speed * 0.4f) * dt;
        }
        // collide with walls
        e.p.x = clamp(e.p.x, float(ROOM_X + 20 + int(e.r)), float(ROOM_X + ROOM_W - 20 - int(e.r)));
        e.p.y = clamp(e.p.y, float(ROOM_Y + 20 + int(e.r)), float(ROOM_Y + ROOM_H - 20 - int(e.r)));
    }
    // cull dead
    R.enemies.erase(std::remove_if(R.enemies.begin(), R.enemies.end(), [](const Enemy& e) {return e.dead; }), R.enemies.end());
    if (!R.enemies.empty()) return;
    R.cleared = true;
}

static void drawEnemies(const Room& R) {
    for (auto& e : R.enemies) {
        uint32_t c = e.kind == 0 ? RGBA(240, 180, 60) : RGBA(120, 200, 255);
        if (e.hp <= 1.f) c = RGBA(255, 120, 120);
        fillCircle(int(e.p.x), int(e.p.y), int(e.r), c);
    }
}

static void updateBullets(Room& R, float dt) {
    for (auto& b : g_player.shots) {
        if (b.dead) continue;
        b.p += b.v * dt;
        b.ttl -= dt;
        if (b.ttl <= 0) b.dead = true;
        // wall bounce (simple clamp as kill)
        if (b.p.x<ROOM_X + 20 || b.p.x>ROOM_X + ROOM_W - 20 || b.p.y<ROOM_Y + 20 || b.p.y>ROOM_Y + ROOM_H - 20)
            b.dead = true;
        // hit enemies
        for (auto& e : R.enemies) {
            if (e.dead) continue;
            float dx = b.p.x - e.p.x, dy = b.p.y - e.p.y;
            float rr = (b.r + e.r); rr *= rr;
            if (dx * dx + dy * dy <= rr) {
                e.hp -= 1.f;
                b.dead = true;
                if (e.hp <= 0) e.dead = true;
                break;
            }
        }
    }
    g_player.shots.erase(std::remove_if(g_player.shots.begin(), g_player.shots.end(), [](const Bullet& b) {return b.dead; }), g_player.shots.end());
}
static void drawBullets() {
    for (auto& b : g_player.shots) fillCircle(int(b.p.x), int(b.p.y), int(b.r), RGBA(255, 255, 255));
}

static void drawPlayer() {
    fillCircle(int(g_player.p.x), int(g_player.p.y), int(g_player.r), RGBA(180, 220, 255));
    // tiny "eye" to suggest facing based on last shot or movement could be added
}

static void playerShoot(const Vec& dir) {
    if (g_player.shotCooldown > 0.f) return;
    Bullet b;
    b.p = g_player.p + dir * (g_player.r + 6.f);
    b.v = dir * 360.f;
    b.r = 5.f;
    b.ttl = 0.9f;
    g_player.shots.push_back(b);
    g_player.shotCooldown = 0.12f; // fire rate
}

static void playerUpdateMove(float dt) {
    Vec mv(0, 0);
    if (keyDown('W')) mv.y -= 1;
    if (keyDown('S')) mv.y += 1;
    if (keyDown('A')) mv.x -= 1;
    if (keyDown('D')) mv.x += 1;
    if (mv.x != 0 || mv.y != 0) mv = norm(mv);
    g_player.p += mv * g_player.speed * dt;

    // clamp to inner room (leave holes where doors are? keep simple; doors are overlays)
    g_player.p.x = clamp(g_player.p.x, float(ROOM_X + 20 + int(g_player.r)), float(ROOM_X + ROOM_W - 20 - int(g_player.r)));
    g_player.p.y = clamp(g_player.p.y, float(ROOM_Y + 20 + int(g_player.r)), float(ROOM_Y + ROOM_H - 20 - int(g_player.r)));
}

static void playerShootInput() {
    Vec d(0, 0);
    if (keyDown(VK_UP))    d.y -= 1;
    if (keyDown(VK_DOWN))  d.y += 1;
    if (keyDown(VK_LEFT))  d.x -= 1;
    if (keyDown(VK_RIGHT)) d.x += 1;
    if (d.x != 0 || d.y != 0) playerShoot(norm(d));
}

static void playerHitCheck(Room& R, float dt) {
    // touch damage if overlapping enemies
    for (auto& e : R.enemies) {
        if (e.dead) continue;
        float dx = g_player.p.x - e.p.x, dy = g_player.p.y - e.p.y;
        float rr = (g_player.r + e.r); rr *= rr;
        if (dx * dx + dy * dy <= rr) {
            // blink damage: simple cooldown by moving player a bit and subtract hp once per overlap window
            static float hurtCD = 0.f;
            if (hurtCD <= 0.f) {
                g_player.hp -= 1;
                hurtCD = 0.9f;
                // knockback
                Vec kb = norm(g_player.p - e.p);
                g_player.p += kb * 20.f;
                if (g_player.hp <= 0) { g_runOver = true; }
            }
            // tick cooldown
            hurtCD = std::max(0.f, hurtCD - dt);
        }
    }
}

static void handleDoorsAndTransitions(Room& R) {
    if (!R.cleared) return;

    auto tryGo = [&](Dir d, int nx, int ny, Vec newPos) {
        if (!R.doors[(int)d]) return false;
        RECT rc = doorRect(d);

        // expand door hitbox to make overlap easier
        InflateRect(&rc, 10, 10);

        if (circleRectOverlap(g_player.p, g_player.r, rc)) {
            g_rx = nx; g_ry = ny;
            g_player.p = newPos;
            return true;
        }
        return false;
        };

    if (R.doors[(int)Dir::Up] && g_ry > 0 && g_dungeon[g_ry - 1][g_rx].exists)
        if (tryGo(Dir::Up, g_rx, g_ry - 1, Vec(ROOM_X + ROOM_W / 2.f, ROOM_Y + ROOM_H - 60))) return;

    if (R.doors[(int)Dir::Down] && g_ry < GRID_H - 1 && g_dungeon[g_ry + 1][g_rx].exists)
        if (tryGo(Dir::Down, g_rx, g_ry + 1, Vec(ROOM_X + ROOM_W / 2.f, ROOM_Y + 60))) return;

    if (R.doors[(int)Dir::Left] && g_rx > 0 && g_dungeon[g_ry][g_rx - 1].exists)
        if (tryGo(Dir::Left, g_rx - 1, g_ry, Vec(ROOM_X + ROOM_W - 60, ROOM_Y + ROOM_H / 2.f))) return;

    if (R.doors[(int)Dir::Right] && g_rx < GRID_W - 1 && g_dungeon[g_ry][g_rx + 1].exists)
        if (tryGo(Dir::Right, g_rx + 1, g_ry, Vec(ROOM_X + 60, ROOM_Y + ROOM_H / 2.f))) return;
}


static void checkAllCleared() {
    bool any = false, allclear = true;
    for (int y = 0; y < GRID_H; ++y) for (int x = 0; x < GRID_W; ++x) {
        if (!g_dungeon[y][x].exists) continue;
        any = true;
        if (!g_dungeon[y][x].cleared) allclear = false;
    }
    g_allCleared = any && allclear;
    if (g_allCleared && !g_runOver) {
        g_runOver = true; // floor done
    }
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_DESTROY) { g_running = false; PostQuitMessage(0); return 0; }
    return DefWindowProc(h, m, w, l);
}

int APIENTRY WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    // Window
    WNDCLASS wc{}; wc.lpszClassName = TEXT("IsaacLikeWin"); wc.hInstance = hInst; wc.lpfnWndProc = WndProc; wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClass(&wc);
    DWORD style = WS_OVERLAPPEDWINDOW & ~(WS_MAXIMIZEBOX | WS_THICKFRAME);
    HWND hwnd = CreateWindow(wc.lpszClassName, TEXT("Mini Isaac-like (No Sprites)"),
        style, CW_USEDEFAULT, CW_USEDEFAULT, WIDTH + 16, HEIGHT + 39, nullptr, nullptr, hInst, nullptr);
    ShowWindow(hwnd, SW_SHOW);

    // Backbuffer
    g_bmpInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    g_bmpInfo.bmiHeader.biWidth = WIDTH;
    g_bmpInfo.bmiHeader.biHeight = -HEIGHT; // top-down
    g_bmpInfo.bmiHeader.biPlanes = 1;
    g_bmpInfo.bmiHeader.biBitCount = 32;
    g_bmpInfo.bmiHeader.biCompression = BI_RGB;

    HDC hdc = GetDC(hwnd);
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP dib = CreateDIBSection(hdc, &g_bmpInfo, DIB_RGB_COLORS, &g_pixels, NULL, 0);
    SelectObject(memDC, dib);

    // Game init
    resetRun();

    LARGE_INTEGER freq, t0; QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&t0);
    double acc = 0.0, dt = 1.0 / 120.0; // update at 120 Hz
    MSG msg{};
    while (g_running) {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) g_running = false;
            TranslateMessage(&msg); DispatchMessage(&msg);
        }
        if (!g_running) break;

        if (keyDown(VK_ESCAPE)) { g_running = false; break; }
        if (g_runOver && keyDown('R')) { resetRun(); }

        LARGE_INTEGER t1; QueryPerformanceCounter(&t1);
        double elapsed = double(t1.QuadPart - t0.QuadPart) / double(freq.QuadPart);
        t0 = t1; acc += elapsed;

        // fixed update loop
        while (acc >= dt) {
            acc -= dt;
            if (!g_runOver) {
                Room& R = g_dungeon[g_ry][g_rx];
                // input
                playerUpdateMove((float)dt);
                playerShootInput();
                if (g_player.shotCooldown > 0.f) g_player.shotCooldown -= (float)dt;

                // systems
                updateEnemies(R, (float)dt);
                updateBullets(R, (float)dt);
                playerHitCheck(R, (float)dt);
                handleDoorsAndTransitions(R);
                checkAllCleared();
                if (g_player.hp <= 0) g_runOver = true;
            }
        }

        // render
        clear(RGBA(15, 15, 18));
        Room& RR = g_dungeon[g_ry][g_rx];
        drawRoom(RR);
        drawEnemies(RR);
        drawBullets();
        drawPlayer();
        drawHUD();

        BitBlt(hdc, 0, 0, WIDTH, HEIGHT, memDC, 0, 0, SRCCOPY);
        Sleep(1);
    }

    // cleanup
    DeleteObject(dib);
    DeleteDC(memDC);
    ReleaseDC(hwnd, hdc);
    DestroyWindow(hwnd);
    return 0;
}
