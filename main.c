/*
 * Mini Escuela - motor base para PSP (pspdev / sceGu)
 * Juego original de ejemplo: recoge 7 cuadernos y evita al perseguidor.
 * Todo el arte son cajas de color (placeholders propios).
 *
 * Controles:
 *   Stick analogico : mover / desplazarse lateral
 *   L / R  o  D-Pad izq/der : girar
 *   X (cruz)        : correr
 *   START           : reiniciar
 */
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspgu.h>
#include <pspgum.h>
#include <math.h>
#include <string.h>

PSP_MODULE_INFO("MiniEscuela", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define BUF_W 512
#define SCR_W 480
#define SCR_H 272
#define MW 16
#define MH 16
#define CELL 4.0f
#define WALL_H 3.0f
#define EYE 1.6f
#define NBOOKS 7
#define PI 3.14159265f
#define COL(r, g, b) (0xff000000u | ((unsigned)(b) << 16) | ((unsigned)(g) << 8) | (unsigned)(r))

/* ---------- Callbacks de salida (boton HOME) ---------- */
static int exit_callback(int a, int b, void *c) { (void)a; (void)b; (void)c; sceKernelExitGame(); return 0; }
static int callback_thread(SceSize args, void *argp) {
    (void)args; (void)argp;
    int id = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    sceKernelRegisterExitCallback(id);
    sceKernelSleepThreadCB();
    return 0;
}
static void setup_callbacks(void) {
    int t = sceKernelCreateThread("update_thread", callback_thread, 0x11, 0xFA0, 0, 0);
    if (t >= 0) sceKernelStartThread(t, 0, 0);
}

/* ---------- VRAM ---------- */
static unsigned int __attribute__((aligned(16))) list[262144];
static unsigned int vram_off = 0;
static void *vram_alloc(unsigned w, unsigned h, unsigned bytes_per_px) {
    void *r = (void *)vram_off;
    vram_off += w * h * bytes_per_px;
    return r;
}

/* ---------- Geometria ---------- */
typedef struct { unsigned int color; float x, y, z; } Vertex;

#define MAX_STATIC 12000
#define MAX_DYN 1024
static Vertex __attribute__((aligned(16))) world_v[MAX_STATIC];
static Vertex __attribute__((aligned(16))) dyn_v[MAX_DYN];
static int world_n = 0;

static unsigned int shade(unsigned int c, float f) {
    unsigned r = (unsigned)((c & 255) * f), g = (unsigned)(((c >> 8) & 255) * f), b = (unsigned)(((c >> 16) & 255) * f);
    return COL(r, g, b);
}

static void quad(Vertex *v, int *n, const float a[3], const float b[3], const float c[3], const float d[3], unsigned int col) {
    const float *p[6] = { a, b, c, a, c, d };
    for (int i = 0; i < 6; i++) {
        v[*n].color = col; v[*n].x = p[i][0]; v[*n].y = p[i][1]; v[*n].z = p[i][2];
        (*n)++;
    }
}

static void box(Vertex *v, int *n, float x0, float y0, float z0, float x1, float y1, float z1, unsigned int col) {
    float P[8][3] = {
        {x0,y0,z0},{x1,y0,z0},{x1,y1,z0},{x0,y1,z0},
        {x0,y0,z1},{x1,y0,z1},{x1,y1,z1},{x0,y1,z1}
    };
    static const int F[6][4] = { {0,1,2,3},{5,4,7,6},{4,0,3,7},{1,5,6,2},{3,2,6,7},{4,5,1,0} };
    static const float S[6] = { 0.8f, 0.8f, 0.65f, 0.65f, 1.0f, 0.5f };
    for (int f = 0; f < 6; f++)
        quad(v, n, P[F[f][0]], P[F[f][1]], P[F[f][2]], P[F[f][3]], shade(col, S[f]));
}

/* ---------- Mapa (# = pared, . = pasillo) ---------- */
static const char *MAP[MH] = {
    "################",
    "#..............#",
    "#.####.##.####.#",
    "#.#..........#.#",
    "#.#.########.#.#",
    "#...#......#...#",
    "###.#......#.###",
    "#...#......#...#",
    "#.#.########.#.#",
    "#.#..........#.#",
    "#.####.##.####.#",
    "#..............#",
    "#.####.##.####.#",
    "#.#..........#.#",
    "#..............#",
    "################"
};
static const int BOOK_X[NBOOKS] = { 1, 14, 1, 14, 7, 7, 3 };
static const int BOOK_Z[NBOOKS] = { 1, 1, 14, 14, 3, 11, 6 };

static int is_wall(int cx, int cz) {
    if (cx < 0 || cz < 0 || cx >= MW || cz >= MH) return 1;
    return MAP[cz][cx] == '#';
}

static void build_world(void) {
    world_n = 0;
    for (int z = 0; z < MH; z++) {
        for (int x = 0; x < MW; x++) {
            float x0 = x * CELL, z0 = z * CELL, x1 = x0 + CELL, z1 = z0 + CELL;
            if (is_wall(x, z)) {
                box(world_v, &world_n, x0, 0, z0, x1, WALL_H, z1, COL(205, 175, 110));
            } else {
                float a[3] = { x0, 0, z0 }, b[3] = { x1, 0, z0 }, c[3] = { x1, 0, z1 }, d[3] = { x0, 0, z1 };
                quad(world_v, &world_n, a, b, c, d, ((x + z) & 1) ? COL(90, 90, 95) : COL(60, 60, 66));
            }
        }
    }
    float a[3] = { 0, WALL_H, 0 }, b[3] = { MW * CELL, WALL_H, 0 };
    float c[3] = { MW * CELL, WALL_H, MH * CELL }, d[3] = { 0, WALL_H, MH * CELL };
    quad(world_v, &world_n, a, b, c, d, COL(150, 150, 150));
    sceKernelDcacheWritebackAll();
}

/* ---------- Estado del juego ---------- */
enum { PLAYING, WON, LOST };
static float px, pz, yaw;
static float cx, cz, ctx, ctz;       /* perseguidor: posicion y objetivo */
static int got[NBOOKS], collected, state;
static unsigned frame = 0;

static void reset_game(void) {
    px = 1.5f * CELL; pz = 11.5f * CELL; yaw = 0.0f;
    cx = ctx = 14.5f * CELL; cz = ctz = 3.5f * CELL;
    memset(got, 0, sizeof(got));
    collected = 0; state = PLAYING;
}

static int solid_at(float x, float z) { return is_wall((int)floorf(x / CELL), (int)floorf(z / CELL)); }
static int blocked(float x, float z) {
    const float r = 0.6f;
    return solid_at(x - r, z - r) || solid_at(x + r, z - r) || solid_at(x - r, z + r) || solid_at(x + r, z + r);
}

/* Busqueda en anchura desde la celda del jugador: dist[z][x] = pasos hasta el jugador */
static int dist[MH][MW];
static void bfs(int sx, int sz) {
    static const int DX[4] = { 1, -1, 0, 0 }, DZ[4] = { 0, 0, 1, -1 };
    int qx[MW * MH], qz[MW * MH], h = 0, t = 0;
    for (int z = 0; z < MH; z++) for (int x = 0; x < MW; x++) dist[z][x] = -1;
    dist[sz][sx] = 0; qx[t] = sx; qz[t++] = sz;
    while (h < t) {
        int x = qx[h], z = qz[h++];
        for (int d = 0; d < 4; d++) {
            int nx = x + DX[d], nz = z + DZ[d];
            if (is_wall(nx, nz) || dist[nz][nx] >= 0) continue;
            dist[nz][nx] = dist[z][x] + 1;
            qx[t] = nx; qz[t++] = nz;
        }
    }
}

static void update_chaser(float dt) {
    static const int DX[4] = { 1, -1, 0, 0 }, DZ[4] = { 0, 0, 1, -1 };
    float speed = 2.5f + 0.5f * collected;      /* mas rapido con cada cuaderno */
    float step = speed * dt;
    float dx = ctx - cx, dz = ctz - cz, d = sqrtf(dx * dx + dz * dz);
    if (d > step) { cx += dx / d * step; cz += dz / d * step; return; }
    cx = ctx; cz = ctz;
    int ccx = (int)(cx / CELL), ccz = (int)(cz / CELL);
    int pcx = (int)(px / CELL), pcz = (int)(pz / CELL);
    if (ccx == pcx && ccz == pcz) { ctx = px; ctz = pz; return; }
    bfs(pcx, pcz);
    int best = dist[ccz][ccx], bx = ccx, bz = ccz;
    for (int k = 0; k < 4; k++) {
        int nx = ccx + DX[k], nz = ccz + DZ[k];
        if (is_wall(nx, nz) || dist[nz][nx] < 0) continue;
        if (dist[nz][nx] < best) { best = dist[nz][nx]; bx = nx; bz = nz; }
    }
    ctx = (bx + 0.5f) * CELL; ctz = (bz + 0.5f) * CELL;
}

static void update(const SceCtrlData *pad, float dt) {
    if (pad->Buttons & PSP_CTRL_START) { reset_game(); return; }
    if (state != PLAYING) return;

    float turn = 0.0f;
    if (pad->Buttons & (PSP_CTRL_LTRIGGER | PSP_CTRL_LEFT)) turn += 1.0f;
    if (pad->Buttons & (PSP_CTRL_RTRIGGER | PSP_CTRL_RIGHT)) turn -= 1.0f;
    yaw += turn * 2.2f * dt;

    float lx = ((int)pad->Lx - 128) / 128.0f, ly = ((int)pad->Ly - 128) / 128.0f;
    if (fabsf(lx) < 0.2f) lx = 0.0f;
    if (fabsf(ly) < 0.2f) ly = 0.0f;
    float fwd = -ly, side = lx;
    float speed = (pad->Buttons & PSP_CTRL_CROSS) ? 7.0f : 4.5f;
    float s = sinf(yaw), c = cosf(yaw);
    float vx = (-s * fwd + c * side) * speed * dt;
    float vz = (-c * fwd - s * side) * speed * dt;
    if (!blocked(px + vx, pz)) px += vx;
    if (!blocked(px, pz + vz)) pz += vz;

    for (int i = 0; i < NBOOKS; i++) {
        if (got[i]) continue;
        float bx = (BOOK_X[i] + 0.5f) * CELL - px, bz = (BOOK_Z[i] + 0.5f) * CELL - pz;
        if (bx * bx + bz * bz < 1.8f * 1.8f) { got[i] = 1; collected++; }
    }
    update_chaser(dt);
    float ex = cx - px, ez = cz - pz;
    if (ex * ex + ez * ez < 1.4f * 1.4f) state = LOST;
    else if (collected == NBOOKS) state = WON;
}

/* ---------- Dibujo ---------- */
static void rect2d(float x, float y, float w, float h, unsigned int c) {
    Vertex *v = (Vertex *)sceGuGetMemory(6 * sizeof(Vertex));
    Vertex t[6] = { {c,x,y,0},{c,x+w,y,0},{c,x+w,y+h,0},{c,x,y,0},{c,x+w,y+h,0},{c,x,y+h,0} };
    memcpy(v, t, sizeof(t));
    sceGuDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 6, 0, v);
}

static void render(void) {
    sceGuStart(GU_DIRECT, list);
    sceGuClearColor(COL(25, 20, 30));
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
    sceGuEnable(GU_DEPTH_TEST);

    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadIdentity();
    sceGumPerspective(70.0f, 16.0f / 9.0f, 0.1f, 120.0f);
    sceGumMatrixMode(GU_VIEW);
    sceGumLoadIdentity();
    ScePspFVector3 eye = { -px, -EYE, -pz };
    sceGumRotateY(-yaw);
    sceGumTranslate(&eye);
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();

    sceGumDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, world_n, 0, world_v);

    /* Objetos dinamicos: cuadernos y perseguidor (cajas placeholder) */
    int n = 0;
    float bob = 0.15f * sinf(frame * 0.08f);
    for (int i = 0; i < NBOOKS; i++) {
        if (got[i]) continue;
        float x = (BOOK_X[i] + 0.5f) * CELL, z = (BOOK_Z[i] + 0.5f) * CELL;
        box(dyn_v, &n, x - 0.4f, 0.9f + bob, z - 0.3f, x + 0.4f, 1.5f + bob, z + 0.3f, COL(60, 120, 230));
    }
    box(dyn_v, &n, cx - 0.5f, 0.0f, cz - 0.5f, cx + 0.5f, 2.4f, cz + 0.5f, COL(150, 60, 170));
    sceKernelDcacheWritebackAll();
    sceGumDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, n, 0, dyn_v);

    /* HUD: un cuadro por cuaderno */
    sceGuDisable(GU_DEPTH_TEST);
    for (int i = 0; i < NBOOKS; i++) {
        rect2d(8.0f + i * 20.0f, 8.0f, 16.0f, 16.0f, COL(0, 0, 0));
        rect2d(10.0f + i * 20.0f, 10.0f, 12.0f, 12.0f, i < collected ? COL(255, 220, 40) : COL(70, 70, 70));
    }
    if (state == WON)  rect2d(140.0f, 116.0f, 200.0f, 40.0f, COL(40, 200, 70));
    if (state == LOST) rect2d(140.0f, 116.0f, 200.0f, 40.0f, COL(210, 40, 40));

    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
}

static void init_gu(void) {
    void *fbp0 = vram_alloc(BUF_W, SCR_H, 4);
    void *fbp1 = vram_alloc(BUF_W, SCR_H, 4);
    void *zbp  = vram_alloc(BUF_W, SCR_H, 2);
    sceGuInit();
    sceGuStart(GU_DIRECT, list);
    sceGuDrawBuffer(GU_PSM_8888, fbp0, BUF_W);
    sceGuDispBuffer(SCR_W, SCR_H, fbp1, BUF_W);
    sceGuDepthBuffer(zbp, BUF_W);
    sceGuOffset(2048 - (SCR_W / 2), 2048 - (SCR_H / 2));
    sceGuViewport(2048, 2048, SCR_W, SCR_H);
    sceGuDepthRange(65535, 0);
    sceGuScissor(0, 0, SCR_W, SCR_H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuFrontFace(GU_CW);
    sceGuShadeModel(GU_SMOOTH);
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
}

int main(void) {
    setup_callbacks();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    init_gu();
    build_world();
    reset_game();

    SceCtrlData pad;
    memset(&pad, 0, sizeof(pad));
    for (;;) {
        sceCtrlPeekBufferPositive(&pad, 1);
        update(&pad, 1.0f / 60.0f);
        render();
        frame++;
    }
    return 0;
}
