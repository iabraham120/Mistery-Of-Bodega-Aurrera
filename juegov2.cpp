
/*
 * "The Mystery Of Jueves De Frescura" - EDICIÓN EXTREMA
 * =======================================================
 * Cambios:
 * 1. Dificultad aumentada (Velocidad, Rango, Sospecha).
 * 2. Icono de ventana implementado.
 * 3. Sistema de guardado con archivos binarios y texto.
 * 
 * Compilar (Windows MinGW):
 *   gcc juegov2.cpp -o juegov2.exe -lallegro -lallegro_primitives -lallegro_image -lallegro_font -lallegro_ttf -lallegro_audio -lallegro_acodec -lm
 *
 * Controles Nuevos:
 *   F5          -> Guardar Partida
 *   F9          -> Cargar Partida
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <string.h>
#include <allegro5/allegro.h>
#include <allegro5/allegro_primitives.h>
#include <allegro5/allegro_image.h>
#include <allegro5/allegro_font.h>
#include <allegro5/allegro_ttf.h>
#include <allegro5/allegro_audio.h>
#include <allegro5/allegro_acodec.h>

/* ================================================================
   CONSTANTES
   ================================================================ */
#define BUFFER_W     480
#define BUFFER_H     270
#define WORLD_W      640
#define WORLD_H      480
#define FPS          60

#define PLAYER_SIZE   16
#define MANAGER_SIZE  20
#define CUSTOMER_R     9
#define DOC_SIZE      14
#define MONEY_R        5
#define ITEM_SIZE     12

#define MAX_DOCS       20
#define MAX_CUSTOMERS  4
#define MAX_MONEY      3
#define MAX_SHELVES   14
#define MAX_DAYS       5
#define MAX_ITEMS      3

#define HIDE_DIST     14.0f

#define SUSPICION_MAX     100.0f
#define SUSPICION_RISE     1.8f   // Antes 1.4 (Sube más rápido)
#define SUSPICION_FALL     0.4f   // Antes 0.6 (Baja más lento)
#define SUSPICION_HIDE_FALL 1.2f  // Antes 1.8

/* ================================================================
   ENUMERACIONES
   ================================================================ */
typedef enum {
    PHASE_TITLE,
    PHASE_AFTERNOON,
    PHASE_NIGHT,
    PHASE_DAY_COMPLETE,
    PHASE_GAMEOVER,
    PHASE_WIN,
    PHASE_DIALOG
} GamePhase;

typedef enum { MINI_QTE, MINI_MASH } MiniType;
typedef enum {
    MANAGER_PATROL,
    MANAGER_CHASE,
    MANAGER_SEARCH,
    MANAGER_DISTRACTED
} ManagerState;

typedef enum {
    ITEM_COFFEE,
    ITEM_KEYS,
    ITEM_WALKIE
} ItemType;

/* ================================================================
   ESTRUCTURAS
   ================================================================ */
typedef struct { float x, y, w, h; } Shelf;

typedef struct {
    float x, y, speed;
    int   night_lives;
    bool  speed_debuff;
    bool  hiding;
    bool  can_hide;
    bool  has_coffee;
    bool  has_keys;
    bool  has_walkie;
} Player;

typedef struct {
    float        x, y, speed;
    ManagerState state;
    float        patrol_dir;
    float        distract_timer;
    float        search_timer;
    float        target_x, target_y;
    float        suspicion;
    int          ping_timer;
} Manager;

typedef struct { float x, y; bool collected; } Document;
typedef struct { float x, y; bool active; int timer; } Money;

typedef struct {
    float x, y;
    bool  active, mini_active, mini_done, mini_success;
    int   result_timer;
    MiniType mini_type;
    float qte_cursor, qte_window, qte_win_size;
    int   mash_count, mash_needed;
    float mash_timer;
} Customer;

typedef struct {
    float    x, y;
    ItemType type;
    bool     active;
} Item;

typedef struct {
    const char* nombre;
    const char* texto;
} DialogLine;

/* ================================================================
   GLOBALS
   ================================================================ */
ALLEGRO_DISPLAY* disp;
ALLEGRO_BITMAP*  buffer;
ALLEGRO_FONT*    font;
ALLEGRO_SAMPLE* sfx_dialog;

GamePhase game_phase;
GamePhase next_phase_after_dialog;
int       current_day;
int       high_score_day; // Nuevo: Récord

Player   player;
Manager  boss;
Document docs[MAX_DOCS];
int      docs_collected, docs_needed;

Customer customers[MAX_CUSTOMERS];
int      customers_served, customers_needed_count;

Money    moneys[MAX_MONEY];
int      money_count;

Item     items[MAX_ITEMS];

Shelf    shelves[MAX_SHELVES];
int      num_shelves;

float    cam_x, cam_y;
int      caught_flash;
int      transition_timer;

// Mensaje de estado para guardado/carga
char status_msg[64] = "";
int status_msg_timer = 0;

DialogLine* current_dialog = NULL;
int dialog_index = 0;
int dialog_total = 0;

DialogLine dialog_intro[] = {
    {"Jugador", "Otro día más en el Aurrera..."},
    {"Jugador", "Lo unico que me motiva a quedarme aqui es el aumento que me prometieron hace una semana que misteriosamente se cancelo"},
    {"Cliente", "Oye bro, deja de hablar solo y apurate con mi pedido..."},
    {"Jugador", "Ya voy we, calmate tantito."},
    {"Cliente", "Mas te vale o me quejo con el gerente."},
    {"Jugador", "Chale... A ver si no me anulan el aumento por esto"}
};

DialogLine dialog_night[] = {
    {"Jugador", "Lo que sea por el GamePass..."},
    {"Gerente", "Esos 1000 varos que me ahorrare SON MIOS"}
};

/* ================================================================
   CÁMARA
   ================================================================ */
void update_camera()
{
    cam_x = player.x + PLAYER_SIZE / 2.0f - BUFFER_W / 2.0f;
    cam_y = player.y + PLAYER_SIZE / 2.0f - BUFFER_H / 2.0f;
    if (cam_x < 0)                   cam_x = 0;
    if (cam_y < 0)                   cam_y = 0;
    if (cam_x > WORLD_W - BUFFER_W)  cam_x = WORLD_W - BUFFER_W;
    if (cam_y > WORLD_H - BUFFER_H)  cam_y = WORLD_H - BUFFER_H;
}
#define WX(x)  ((x) - cam_x)
#define WY(y)  ((y) - cam_y)

/* ================================================================
   ALLEGRO
   ================================================================ */
void must_init(bool test, const char* desc)
{
    if (test) return;
    printf("No se pudo iniciar: %s\n", desc);
    exit(1);
}
void disp_init()
{
    al_set_new_display_flags(ALLEGRO_FULLSCREEN_WINDOW);
    disp = al_create_display(0, 0);
    must_init(disp, "display");
    buffer = al_create_bitmap(BUFFER_W, BUFFER_H);
    must_init(buffer, "bitmap buffer");
}
void disp_deinit()   { al_destroy_bitmap(buffer); al_destroy_display(disp); }
void disp_pre_draw() { al_set_target_bitmap(buffer); }
void disp_post_draw()
{
    int dw = al_get_display_width(disp);
    int dh = al_get_display_height(disp);
    float scale = fminf((float)dw / BUFFER_W, (float)dh / BUFFER_H);
    float sw = BUFFER_W * scale;
    float sh = BUFFER_H * scale;
    float ox = (dw - sw) * 0.5f;
    float oy = (dh - sh) * 0.5f;

    al_set_target_backbuffer(disp);
    al_clear_to_color(al_map_rgb(0, 0, 0));
    al_draw_scaled_bitmap(buffer, 0, 0, BUFFER_W, BUFFER_H, ox, oy, sw, sh, 0);
    al_flip_display();
}

/* ================================================================
   UTILIDADES
   ================================================================ */
bool collide(float ax,float ay,float aw,float ah,
             float bx,float by,float bw,float bh){ 
    return !(ax>bx+bw||ax+aw<bx||ay>by+bh||ay+ah<by); 
}

void clamp_world(float* x,float* y,float w,float h)
{
    if (*x<2)             *x=2;
    if (*y<2)             *y=2;
    if (*x+w>WORLD_W-2)   *x=WORLD_W-2-w;
    if (*y+h>WORLD_H-2)   *y=WORLD_H-2-h;
}

void resolve_shelf_collision(float* x,float* y,float w,float h)
{
    for(int i=0;i<num_shelves;i++){
        Shelf* s=&shelves[i];
        if(!collide(*x,*y,w,h,s->x,s->y,s->w,s->h)) 
            continue;
        float ox=(*x+w/2.0f)-(s->x+s->w/2.0f);
        float oy=(*y+h/2.0f)-(s->y+s->h/2.0f);
        float hw=(w+s->w)/2.0f, hh=(h+s->h)/2.0f;
        float px=hw-fabsf(ox), py=hh-fabsf(oy);
        if(px<py) 
            *x+=(ox<0)?-px:px;
        else      
            *y+=(oy<0)?-py:py;
    }
}

bool near_shelf()
{
    float expand = HIDE_DIST;
    for(int i=0;i<num_shelves;i++){
        Shelf* s=&shelves[i];
        if(collide(player.x-expand,player.y-expand,
                   PLAYER_SIZE+expand*2,PLAYER_SIZE+expand*2,
                   s->x,s->y,s->w,s->h))
            return true;
    }
    return false;
}

bool manager_can_see_player(float detect_range)
{
    if(player.hiding) 
        return false;
    float dx=player.x-boss.x, dy=player.y-boss.y;
    return sqrtf(dx*dx+dy*dy) < detect_range;
}

/* ================================================================
   MAPA
   ================================================================ */
void build_shelves()
{       
    Shelf layout[]={
        {  60, 80,195,20},
        {325, 80,195,20},
        {  60,160,100,20},
        {220,160,160,20},
        {450,160,130,20},
        {  60,240,195,20},
        {340,240,195,20},
        {  60,320,100,20},
        {220,320,165,20},
        {450,320,130,20},
        {  60,400,195,20},
        {325,400,195,20},
        { 310, 80, 20,110},
        {310,260, 20,110},
    };
    num_shelves=(int)(sizeof(layout)/sizeof(layout[0]));
    for(int i=0;i<num_shelves;i++) shelves[i]=layout[i];
}

/* ================================================================
   DIFICULTAD POR DÍA (MODIFICADO PARA SER MÁS DIFÍCIL)
   ================================================================ */
int   day_docs_needed()      { 
    return 4 + current_day * 2; 
}
int   day_customers_needed() { 
    return 2 + current_day; 
}
float day_boss_speed()       { 
    return 2.2f + current_day * 0.55f; 
}
float day_detect_range()     { 
    return 110.0f + current_day * 25.0f;
}
int   day_money_count()      { 
    int m=4-current_day; 
    return m>0?m:1; 
}

/* ================================================================
   SPAWN
   ================================================================ */
void spawn_customer(int i)
{
    for(int t=0;t<60;t++){
        customers[i].x=40.0f+rand()%(WORLD_W-80);
        customers[i].y=40.0f+rand()%(WORLD_H-80);
        bool ok=true;
        for(int s=0;s<num_shelves&&ok;s++)
            if(collide(customers[i].x-CUSTOMER_R,customers[i].y-CUSTOMER_R,CUSTOMER_R*2,CUSTOMER_R*2,shelves[s].x,shelves[s].y,shelves[s].w,shelves[s].h))
                ok=false;
        if(ok) 
            break;
    }
    customers[i].active=true;
    customers[i].mini_active=customers[i].mini_done=customers[i].mini_success=false;
    customers[i].result_timer=0;
    customers[i].mini_type=(rand()%2)?MINI_QTE:MINI_MASH;
    customers[i].qte_cursor=0;
    customers[i].qte_window=0.25f+(rand()%50)/100.0f;
    customers[i].qte_win_size=0.08f+(rand()%10)/100.0f;
    customers[i].mash_count=0;
    customers[i].mash_needed=8+rand()%(3+current_day*2);
    customers[i].mash_timer=0;
}

void spawn_item(int i, ItemType type)
{
    for(int t=0;t<80;t++){
        items[i].x=50.0f+rand()%(WORLD_W-100);
        items[i].y=50.0f+rand()%(WORLD_H-100);
        bool ok=true;
        for(int s=0;s<num_shelves&&ok;s++)
            if(collide(items[i].x,items[i].y,ITEM_SIZE,ITEM_SIZE,shelves[s].x,shelves[s].y,shelves[s].w,shelves[s].h))
                ok=false;
        float dx=items[i].x-player.x, dy=items[i].y-player.y;
        if(sqrtf(dx*dx+dy*dy)<50.0f) 
            ok=false;
        if(ok) 
            break;
    }
    items[i].type=type;
    items[i].active=true;
}

/* ================================================================
   INICIALIZAR FASES
   ================================================================ */
void init_afternoon()
{
    game_phase=PHASE_AFTERNOON;
    player.x=WORLD_W/2.0f;
    player.y=WORLD_H/2.0f;
    player.speed=3.0f;
    player.hiding=player.can_hide=false;

    customers_served=0;
    customers_needed_count=day_customers_needed();
    for(int i=0;i<MAX_CUSTOMERS;i++) 
        customers[i].active=false;
    int n=customers_needed_count<MAX_CUSTOMERS?customers_needed_count:MAX_CUSTOMERS;
    for(int i=0;i<n;i++) 
        spawn_customer(i);

    spawn_item(0, ITEM_COFFEE);
    spawn_item(1, ITEM_KEYS);
    spawn_item(2, ITEM_WALKIE);

    update_camera();
}

void init_night()
{
    game_phase=PHASE_NIGHT;
    player.speed_debuff=(player.night_lives==1);

    float base_speed = player.speed_debuff ? 2.0f : 3.5f;
    player.speed = base_speed + (player.has_coffee ? 0.6f : 0.0f);

    player.hiding=player.can_hide=false;
    player.x=20;
    player.y=WORLD_H/2.0f;

    boss.x=WORLD_W-50.0f;
    boss.y=WORLD_H/2.0f;
    boss.speed=day_boss_speed();
    boss.state=MANAGER_PATROL;
    boss.patrol_dir=-1.0f;
    boss.distract_timer=boss.search_timer=0;
    boss.target_x=boss.x; boss.target_y=boss.y;
    boss.suspicion=0;
    boss.ping_timer=0;

    docs_needed=day_docs_needed();
    docs_collected=0;
    for(int i=0;i<MAX_DOCS;i++){
        docs[i].collected=false;
        for(int t=0;t<60;t++){
            docs[i].x=30.0f+rand()%(WORLD_W-60);
            docs[i].y=30.0f+rand()%(WORLD_H-60);
            bool ok=true;
            for(int s=0;s<num_shelves&&ok;s++)
                if(collide(docs[i].x,docs[i].y,DOC_SIZE,DOC_SIZE,
                           shelves[s].x,shelves[s].y,shelves[s].w,shelves[s].h))
                    ok=false;
            if(ok) 
                break;
        }
    }

    money_count = day_money_count() + (player.has_keys ? 1 : 0);
    for(int i=0;i<MAX_MONEY;i++) moneys[i].active=false;

    caught_flash=0;
    update_camera();
}

void start_game()
{
    current_day=1;
    player.night_lives=2;
    player.speed_debuff=false;
    player.has_coffee=player.has_keys=player.has_walkie=false;
    build_shelves();
    init_afternoon();
}

void start_dialog(DialogLine* lines, int total, GamePhase next_phase)
{
    current_dialog = lines;
    dialog_total = total;
    dialog_index = 0;
    next_phase_after_dialog = next_phase;
    game_phase = PHASE_DIALOG;
}

/* ================================================================
   ARCHIVOS (BINARIO Y TEXTO) - MOVIDO AQUÍ PARA VER LAS FUNCIONES INIT
   ================================================================ */
void load_highscore_text() {
    FILE* f = fopen("assets/highscore.txt", "r");
    if (f) {
        fscanf(f, "%d", &high_score_day);
        fclose(f);
    } else {
        high_score_day = 0;
    }
}

void save_highscore_text() {
    // Solo guarda si superó el récord
    if (current_day > high_score_day) {
        high_score_day = current_day;
        FILE* f = fopen("assets/highscore.txt", "w");
        if (f) {
            fprintf(f, "%d", high_score_day);
            fclose(f);
        }
    }
}

void save_game_binary() {
    FILE* f = fopen("assets/savegame.bin", "wb");
    if (f) {
        // Guardamos datos importantes
        fwrite(&current_day, sizeof(int), 1, f);
        fwrite(&player.night_lives, sizeof(int), 1, f);
        fwrite(&player.has_coffee, sizeof(bool), 1, f);
        fwrite(&player.has_keys, sizeof(bool), 1, f);
        fwrite(&player.has_walkie, sizeof(bool), 1, f);
        
        // Guardamos fase actual para saber donde cargar
        int phase_int = (int)game_phase;
        fwrite(&phase_int, sizeof(int), 1, f);
        
        fclose(f);
        strcpy(status_msg, "JUEGO GUARDADO!");
        status_msg_timer = FPS * 2;
    }
}

void load_game_binary() {
    FILE* f = fopen("assets/savegame.bin", "rb");
    if (f) {
        int saved_day, saved_lives, saved_phase;
        bool c, k, w;
        
        fread(&saved_day, sizeof(int), 1, f);
        fread(&saved_lives, sizeof(int), 1, f);
        fread(&c, sizeof(bool), 1, f);
        fread(&k, sizeof(bool), 1, f);
        fread(&w, sizeof(bool), 1, f);
        fread(&saved_phase, sizeof(int), 1, f);
        fclose(f);

        // Aplicar estado cargado
        current_day = saved_day;
        player.night_lives = saved_lives;
        player.has_coffee = c;
        player.has_keys = k;
        player.has_walkie = w;
        
        // Reiniciar fase según lo guardado
        if (saved_phase == PHASE_NIGHT) init_night();
        else init_afternoon();

        strcpy(status_msg, "JUEGO CARGADO!");
        status_msg_timer = FPS * 2;
    } else {
        strcpy(status_msg, "NO HAY PARTIDA GUARDADA");
        status_msg_timer = FPS * 2;
    }
}

/* ================================================================
   UPDATE ─ TARDE
   ================================================================ */
void update_afternoon(bool keys[], bool keys_pressed[])
{
    if(keys[ALLEGRO_KEY_W]) 
        player.y-=player.speed;
    if(keys[ALLEGRO_KEY_S]) 
        player.y+=player.speed;
    if(keys[ALLEGRO_KEY_A]) 
        player.x-=player.speed;
    if(keys[ALLEGRO_KEY_D]) 
        player.x+=player.speed;
    resolve_shelf_collision(&player.x,&player.y,PLAYER_SIZE,PLAYER_SIZE);
    clamp_world(&player.x,&player.y,PLAYER_SIZE,PLAYER_SIZE);
    update_camera();

    for(int i=0;i<MAX_ITEMS;i++){
        if(!items[i].active) 
            continue;
        if(!collide(player.x,player.y,PLAYER_SIZE,PLAYER_SIZE,items[i].x,items[i].y,ITEM_SIZE,ITEM_SIZE)) 
            continue;
        items[i].active=false;
        switch(items[i].type){
            case ITEM_COFFEE: 
                player.has_coffee=true; 
                break;
            case ITEM_KEYS:   
                player.has_keys=true;   
                break;
            case ITEM_WALKIE: 
                player.has_walkie=true;  
                break;
        }
    }

    for(int i=0;i<MAX_CUSTOMERS;i++){
        if(!customers[i].active) continue;

        if(customers[i].result_timer>0){
            if(--customers[i].result_timer==0){
                customers[i].active=false;
                if(customers[i].mini_success){
                    customers_served++;
                    if(customers_served>=customers_needed_count){
                        player.night_lives=2;
                        start_dialog(dialog_night, sizeof(dialog_night) / sizeof(dialog_night[0]), PHASE_NIGHT);
                        return;
                    }
                }
                spawn_customer(i);
            }
            continue;
        }

        float dx=player.x+PLAYER_SIZE/2.0f-customers[i].x;
        float dy=player.y+PLAYER_SIZE/2.0f-customers[i].y;
        if(!customers[i].mini_active&&!customers[i].mini_done&&
           sqrtf(dx*dx+dy*dy)<22.0f){
            customers[i].mini_active=true;
            customers[i].qte_cursor=customers[i].mash_count=0;
            customers[i].mash_timer=0;
        }
        if(!customers[i].mini_active) continue;

        bool done=false,success=false;
        if(customers[i].mini_type==MINI_QTE){
            customers[i].qte_cursor+=1.0f/(FPS*2.5f);
            if(customers[i].qte_cursor>=1.0f){done=true;success=false;}
            if(keys_pressed[ALLEGRO_KEY_SPACE]){
                float c=customers[i].qte_cursor;
                done=true;
                success=(c>=customers[i].qte_window&&
                         c<=customers[i].qte_window+customers[i].qte_win_size);
            }
        } else {
            customers[i].mash_timer+=1.0f/(FPS*4.0f);
            if(customers[i].mash_timer>=1.0f){
                done=true; success=(customers[i].mash_count>=customers[i].mash_needed);
            }
            if(keys_pressed[ALLEGRO_KEY_SPACE]){
                customers[i].mash_count++;
                if(customers[i].mash_count>=customers[i].mash_needed) done=success=true;
            }
        }
        if(done){
            customers[i].mini_active=false;
            customers[i].mini_done=true;
            customers[i].mini_success=success;
            customers[i].result_timer=FPS;
        }
    }
}

/* ================================================================
   UPDATE ─ NOCHE
   ================================================================ */
void update_night(bool keys[], bool keys_pressed[])
{
    player.can_hide = near_shelf();
    if(keys_pressed[ALLEGRO_KEY_LSHIFT] || keys_pressed[ALLEGRO_KEY_RSHIFT]){
        if(player.can_hide)
            player.hiding = !player.hiding;
    }
    if(player.hiding && !player.can_hide)
        player.hiding=false;

    float move_speed = player.hiding ? player.speed * 0.35f : player.speed;
    if(keys[ALLEGRO_KEY_W]) player.y-=move_speed;
    if(keys[ALLEGRO_KEY_S]) player.y+=move_speed;
    if(keys[ALLEGRO_KEY_A]) player.x-=move_speed;
    if(keys[ALLEGRO_KEY_D]) player.x+=move_speed;
    resolve_shelf_collision(&player.x,&player.y,PLAYER_SIZE,PLAYER_SIZE);
    clamp_world(&player.x,&player.y,PLAYER_SIZE,PLAYER_SIZE);
    update_camera();

    if(keys_pressed[ALLEGRO_KEY_TAB] && player.has_walkie && boss.ping_timer<=0)
        boss.ping_timer = FPS * 3;
    if(boss.ping_timer>0) boss.ping_timer--;

    if(keys_pressed[ALLEGRO_KEY_SPACE] && money_count>0){
        for(int i=0;i<MAX_MONEY;i++){
            if(moneys[i].active) continue;
            moneys[i].x=player.x+35.0f+(float)(rand()%20)-10.0f;
            moneys[i].y=player.y+(float)(rand()%10)-5.0f;
            moneys[i].active=true;
            moneys[i].timer=FPS*6;
            money_count--;
            break;
        }
    }
    for(int i=0;i<MAX_MONEY;i++)
        if(moneys[i].active&&--moneys[i].timer<=0)
            moneys[i].active=false;

    for(int i=0;i<docs_needed;i++){
        if(!docs[i].collected&&
           collide(player.x,player.y,PLAYER_SIZE,PLAYER_SIZE,
                   docs[i].x,docs[i].y,DOC_SIZE,DOC_SIZE)){
            docs[i].collected=true;
            docs_collected++;
        }
    }
    if(docs_collected>=docs_needed){
        transition_timer=FPS*2;
        game_phase=PHASE_DAY_COMPLETE;
        save_highscore_text(); // Guardar récord al pasar de día
        return;
    }

    float detect=day_detect_range();
    bool can_see=manager_can_see_player(detect);

    if(can_see){
        float rate = (boss.state==MANAGER_CHASE) ? SUSPICION_RISE*1.5f : SUSPICION_RISE;
        boss.suspicion+=rate;
        if(boss.suspicion>SUSPICION_MAX) boss.suspicion=SUSPICION_MAX;
    } else {
        float fall = player.hiding ? SUSPICION_HIDE_FALL : SUSPICION_FALL;
        boss.suspicion-=fall;
        if(boss.suspicion<0) boss.suspicion=0;
    }

    if(boss.suspicion>=SUSPICION_MAX && boss.state!=MANAGER_CHASE)
        boss.state=MANAGER_CHASE;

    float dx=player.x-boss.x, dy=player.y-boss.y;
    float dist=sqrtf(dx*dx+dy*dy);

    bool attracted=false;
    for(int i=0;i<MAX_MONEY&&!attracted;i++){
        if(!moneys[i].active) continue;
        float mdx=moneys[i].x-boss.x, mdy=moneys[i].y-boss.y;
        float md=sqrtf(mdx*mdx+mdy*mdy);
        if(md<110.0f){
            attracted=true;
            boss.state=MANAGER_DISTRACTED;
            boss.distract_timer=FPS*4;
            boss.suspicion=0;
            if(md>3.0f){ boss.x+=boss.speed*mdx/md; boss.y+=boss.speed*mdy/md; }
        }
    }

    if(!attracted){
        if(boss.distract_timer>0&&--boss.distract_timer==0)
            boss.state=MANAGER_SEARCH;

        switch(boss.state){
            case MANAGER_PATROL:
                boss.x+=boss.patrol_dir*1.5f;
                boss.y=(WORLD_H/2.0f)+sinf(boss.x*0.018f)*100.0f;
                if(boss.x>=WORLD_W-MANAGER_SIZE-5||boss.x<=5)
                    boss.patrol_dir*=-1.0f;
                break;

            case MANAGER_CHASE:
                if(dist>0.5f){ boss.x+=boss.speed*dx/dist; boss.y+=boss.speed*dy/dist; }
                if(boss.suspicion<=0&&dist>80.0f){
                    boss.state=MANAGER_SEARCH;
                    boss.target_x=player.x; boss.target_y=player.y;
                    boss.search_timer=FPS*4;
                }
                break;

            case MANAGER_SEARCH:{
                boss.search_timer--;
                float sdx=boss.target_x-boss.x, sdy=boss.target_y-boss.y;
                float sd=sqrtf(sdx*sdx+sdy*sdy);
                if(sd>6.0f){ boss.x+=boss.speed*0.75f*sdx/sd; boss.y+=boss.speed*0.75f*sdy/sd; }
                else{ boss.x+=(float)(rand()%3)-1.0f; boss.y+=(float)(rand()%3)-1.0f; }
                if(boss.suspicion>=SUSPICION_MAX) boss.state=MANAGER_CHASE;
                if(boss.search_timer<=0)         boss.state=MANAGER_PATROL;
                break;
            }
            case MANAGER_DISTRACTED: break;
        }
    }
    clamp_world(&boss.x,&boss.y,MANAGER_SIZE,MANAGER_SIZE);

    if(collide(player.x,player.y,PLAYER_SIZE,PLAYER_SIZE,
               boss.x,  boss.y,  MANAGER_SIZE,MANAGER_SIZE)){
        caught_flash=FPS/2;
        if(--player.night_lives<=0){
            game_phase=PHASE_GAMEOVER;
        } else {
            init_night();
        }
    }
}

/* ================================================================
   DRAW HELPERS
   ================================================================ */
void draw_map_afternoon()
{
    al_clear_to_color(al_map_rgb(210,220,200));
    for(int gx=0;gx<WORLD_W;gx+=40){
        float sx=WX(gx);
        if(sx>=-1&&sx<=BUFFER_W+1)
            al_draw_line(sx,0,sx,BUFFER_H,al_map_rgba(175,170,150,80),1);
    }
    for(int gy=0;gy<WORLD_H;gy+=40){
        float sy=WY(gy);
        if(sy>=-1&&sy<=BUFFER_H+1)
            al_draw_line(0,sy,BUFFER_W,sy,al_map_rgba(175,170,150,80),1);
    }
    al_draw_rectangle(WX(0),WY(0),WX(WORLD_W),WY(WORLD_H),al_map_rgb(120,100,60),2);
    for(int i=0;i<num_shelves;i++){
        Shelf* s=&shelves[i];
        al_draw_filled_rectangle(WX(s->x),WY(s->y),WX(s->x+s->w),WY(s->y+s->h),al_map_rgb(155,125,85));
        al_draw_filled_rectangle(WX(s->x),WY(s->y),WX(s->x+s->w),WY(s->y+4),al_map_rgb(200,170,120));
        al_draw_rectangle(WX(s->x),WY(s->y),WX(s->x+s->w),WY(s->y+s->h),al_map_rgb(100,80,50),1);
    }
}

void draw_map_night()
{
    al_clear_to_color(al_map_rgb(10,10,24));
    for(int gx=0;gx<WORLD_W;gx+=40){
        float sx=WX(gx);
        if(sx>=-1&&sx<=BUFFER_W+1)
            al_draw_line(sx,0,sx,BUFFER_H,al_map_rgba(22,22,48,90),1);
    }
    for(int gy=0;gy<WORLD_H;gy+=40){
        float sy=WY(gy);
        if(sy>=-1&&sy<=BUFFER_H+1)
            al_draw_line(0,sy,BUFFER_W,sy,al_map_rgba(22,22,48,90),1);
    }
    al_draw_rectangle(WX(0),WY(0),WX(WORLD_W),WY(WORLD_H),al_map_rgb(50,50,90),2);
    for(int i=0;i<num_shelves;i++){
        Shelf* s=&shelves[i];
        al_draw_filled_rectangle(WX(s->x),WY(s->y),WX(s->x+s->w),WY(s->y+s->h),al_map_rgb(45,32,16));
        al_draw_filled_rectangle(WX(s->x),WY(s->y),WX(s->x+s->w),WY(s->y+3),al_map_rgb(65,48,22));
        al_draw_rectangle(WX(s->x),WY(s->y),WX(s->x+s->w),WY(s->y+s->h),al_map_rgb(75,52,28),1);
    }
}

void draw_minimap()
{
    const float SC=0.095f;
    const float MM_W=WORLD_W*SC, MM_H=WORLD_H*SC;
    const float MM_X=BUFFER_W-MM_W-4, MM_Y=26;

    al_draw_filled_rectangle(MM_X-1,MM_Y-1,MM_X+MM_W+1,MM_Y+MM_H+1,al_map_rgba(0,0,0,180));
    al_draw_rectangle(MM_X-1,MM_Y-1,MM_X+MM_W+1,MM_Y+MM_H+1,al_map_rgb(90,90,120),1);
    for(int i=0;i<num_shelves;i++){
        Shelf* s=&shelves[i];
        al_draw_filled_rectangle(MM_X+s->x*SC,MM_Y+s->y*SC,
                                 MM_X+(s->x+s->w)*SC,MM_Y+(s->y+s->h)*SC,
                                 al_map_rgb(70,55,35));
    }
    for(int i=0;i<docs_needed;i++)
        if(!docs[i].collected)
            al_draw_filled_rectangle(MM_X+docs[i].x*SC-1,MM_Y+docs[i].y*SC-1,
                                     MM_X+docs[i].x*SC+2,MM_Y+docs[i].y*SC+2,
                                     al_map_rgb(240,220,80));
    {
        float px=MM_X+player.x*SC, py=MM_Y+player.y*SC;
        ALLEGRO_COLOR pc=player.speed_debuff?al_map_rgb(120,120,255):al_map_rgb(255,210,60);
        al_draw_filled_circle(px,py,2,pc);
    }
    {
        float bx=MM_X+boss.x*SC, by=MM_Y+boss.y*SC;
        bool show = boss.ping_timer>0 ? (boss.ping_timer/6)%2==0 : true;
        if(show) al_draw_filled_circle(bx,by,2,al_map_rgb(255,40,40));
    }
    al_draw_text(font,al_map_rgb(100,100,140),
                 (int)(MM_X+MM_W/2),(int)(MM_Y+MM_H+1),ALLEGRO_ALIGN_CENTRE,"MAPA");
}

void draw_suspicion_bar()
{
    float ratio = boss.suspicion / SUSPICION_MAX;
    float bx=36, by=15, bw=BUFFER_W-40.0f, bh=8.0f;

    al_draw_filled_rectangle(0,(int)by-1,BUFFER_W,(int)(by+bh+1),
                             al_map_rgba(0,0,0,160));
    al_draw_filled_rectangle(bx,by,bx+bw,by+bh,al_map_rgba(35,35,35,210));

    if(ratio>0){
        int r=(int)(255*ratio), g=(int)(255*(1.0f-ratio));
        if(r>255)r=255; if(g>255)g=255;
        al_draw_filled_rectangle(bx,by,bx+ratio*bw,by+bh,al_map_rgb(r,g,0));
    }
    al_draw_rectangle(bx,by,bx+bw,by+bh,al_map_rgba(130,130,130,180),1);

    al_draw_text(font,al_map_rgb(190,160,160),2,(int)(by-1),
                 ALLEGRO_ALIGN_LEFT,"SOS:");

    const char* estados[]={"PATRULLA","PERSIGUE","BUSCA","DISTRAIDO"};
    ALLEGRO_COLOR ec;
    switch(boss.state){
        case MANAGER_CHASE:      ec=al_map_rgb(255,80,80);  break;
        case MANAGER_SEARCH:     ec=al_map_rgb(255,180,0);  break;
        case MANAGER_DISTRACTED: ec=al_map_rgb(80,160,255); break;
        default:                 ec=al_map_rgb(150,150,150);break;
    }
    al_draw_text(font,ec,BUFFER_W-2,(int)(by-1),
                 ALLEGRO_ALIGN_RIGHT,estados[boss.state]);

    if(boss.suspicion>=SUSPICION_MAX)
        al_draw_text(font,al_map_rgb(255,60,60),
                     BUFFER_W/2,(int)(by-1),ALLEGRO_ALIGN_CENTRE,"!TE VIO!");
}

void draw_item(Item* it)
{
    float ix=WX(it->x), iy=WY(it->y);
    if(ix<-16||ix>BUFFER_W+16||iy<-16||iy>BUFFER_H+16) return;

    ALLEGRO_COLOR bg, border;
    const char* label;
    switch(it->type){
        case ITEM_COFFEE: bg=al_map_rgb(120,70,30);  border=al_map_rgb(200,130,60);  label="C"; break;
        case ITEM_KEYS:   bg=al_map_rgb(160,140,0);  border=al_map_rgb(255,220,0);   label="K"; break;
        default:          bg=al_map_rgb(0,80,160);   border=al_map_rgb(0,160,255);   label="W"; break;
    }

    al_draw_filled_rounded_rectangle(ix,iy,ix+ITEM_SIZE,iy+ITEM_SIZE,3,3,bg);
    al_draw_rounded_rectangle(ix,iy,ix+ITEM_SIZE,iy+ITEM_SIZE,3,3,border,1);
    al_draw_text(font,al_map_rgb(255,255,255),(int)(ix+3),(int)(iy+2),0,label);
}

void draw_afternoon()
{
    draw_map_afternoon();

    for(int i=0;i<MAX_ITEMS;i++)
        if(items[i].active) draw_item(&items[i]);

    for(int i=0;i<MAX_CUSTOMERS;i++){
        if(!customers[i].active) continue;
        float cx=WX(customers[i].x), cy=WY(customers[i].y);
        if(cx<-20||cx>BUFFER_W+20||cy<-20||cy>BUFFER_H+20) continue;
        al_draw_filled_circle(cx,cy,CUSTOMER_R,al_map_rgb(70,150,255));
        al_draw_circle(cx,cy,CUSTOMER_R,al_map_rgb(30,90,190),1);
        if(!customers[i].mini_active&&!customers[i].mini_done)
            al_draw_text(font,al_map_rgb(255,220,30),(int)cx-3,(int)cy-22,0,"?");
        if(customers[i].mini_done&&customers[i].result_timer>0){
            const char* msg=customers[i].mini_success?"Bien hecho!":"Fallaste...";
            ALLEGRO_COLOR mc=customers[i].mini_success?al_map_rgb(0,255,0):al_map_rgb(255,80,80);
            al_draw_text(font,mc,BUFFER_W/2,BUFFER_H/2-8,ALLEGRO_ALIGN_CENTRE,msg);
        }
    }

    for(int i=0;i<MAX_CUSTOMERS;i++){
        if(!customers[i].mini_active) continue;
        float bx=30,by=8,bw=BUFFER_W-60.0f,bh=14.0f;
        if(customers[i].mini_type==MINI_QTE){
            al_draw_filled_rectangle(bx,by,bx+bw,by+bh,al_map_rgb(40,40,40));
            float wx=bx+customers[i].qte_window*bw;
            float ww=customers[i].qte_win_size*bw;
            al_draw_filled_rectangle(wx,by,wx+ww,by+bh,al_map_rgb(0,200,0));
            float cur=bx+customers[i].qte_cursor*bw;
            al_draw_filled_rectangle(cur-2,by-2,cur+2,by+bh+2,al_map_rgb(255,240,0));
            al_draw_rectangle(bx,by,bx+bw,by+bh,al_map_rgb(180,180,180),1);
            al_draw_text(font,al_map_rgb(255,255,255),BUFFER_W/2,(int)(by+bh+3),
                         ALLEGRO_ALIGN_CENTRE,"Presiona ESPACIO en la zona verde!");
        } else {
            al_draw_filled_rectangle(bx,by,bx+bw,by+bh,al_map_rgb(40,40,40));
            float prog=(float)customers[i].mash_count/customers[i].mash_needed;
            if(prog>1)prog=1;
            al_draw_filled_rectangle(bx,by,bx+prog*bw,by+bh,al_map_rgb(255,140,0));
            float tr=1.0f-customers[i].mash_timer;
            al_draw_filled_rectangle(bx,by+bh+2,bx+tr*bw,by+bh+5,al_map_rgb(200,30,30));
            al_draw_rectangle(bx,by,bx+bw,by+bh,al_map_rgb(180,180,180),1);
            al_draw_text(font,al_map_rgb(255,255,255),BUFFER_W/2,(int)(by+bh+7),
                         ALLEGRO_ALIGN_CENTRE,"Presiona ESPACIO rapido!");
        }
    }

    al_draw_filled_rectangle(WX(player.x),WY(player.y),
                             WX(player.x+PLAYER_SIZE),WY(player.y+PLAYER_SIZE),
                             al_map_rgb(255,200,50));
    al_draw_rectangle(WX(player.x),WY(player.y),
                      WX(player.x+PLAYER_SIZE),WY(player.y+PLAYER_SIZE),
                      al_map_rgb(160,110,0),1);

    al_draw_filled_rectangle(0,0,BUFFER_W,14,al_map_rgba(0,0,0,160));
    char hud[80];
    snprintf(hud,sizeof(hud),"Dia %d/%d   Clientes: %d / %d",
             current_day,MAX_DAYS,customers_served,customers_needed_count);
    al_draw_text(font,al_map_rgb(255,255,180),4,2,0,hud);

    {
        int item_x = BUFFER_W - 2;
        if(player.has_walkie){ item_x-=24; al_draw_text(font,al_map_rgb(0,160,255), item_x,2,0,"[W]"); }
        if(player.has_keys)  { item_x-=24; al_draw_text(font,al_map_rgb(255,220,0), item_x,2,0,"[K]"); }
        if(player.has_coffee){ item_x-=24; al_draw_text(font,al_map_rgb(200,130,60),item_x,2,0,"[C]"); }
    }

    al_draw_filled_rectangle(0,BUFFER_H-12,BUFFER_W,BUFFER_H,al_map_rgba(0,0,0,130));
    al_draw_text(font,al_map_rgb(140,140,160),BUFFER_W/2,BUFFER_H-10,
                 ALLEGRO_ALIGN_CENTRE,"F5=Guardar F9=Cargar");
}

void draw_night()
{
    draw_map_night();

    for(int i=0;i<docs_needed;i++){
        if(docs[i].collected) continue;
        float dx=WX(docs[i].x), dy=WY(docs[i].y);
        if(dx<-20||dx>BUFFER_W+20||dy<-20||dy>BUFFER_H+20) continue;
        al_draw_filled_circle(dx+DOC_SIZE/2,dy+DOC_SIZE/2,11,al_map_rgba(240,220,80,35));
        al_draw_filled_rectangle(dx,dy,dx+DOC_SIZE,dy+DOC_SIZE,al_map_rgb(240,228,100));
        al_draw_rectangle(dx,dy,dx+DOC_SIZE,dy+DOC_SIZE,al_map_rgb(150,130,0),1);
        al_draw_line(dx+3,dy+4, dx+11,dy+4, al_map_rgb(100,80,0),1);
        al_draw_line(dx+3,dy+7, dx+11,dy+7, al_map_rgb(100,80,0),1);
        al_draw_line(dx+3,dy+10,dx+9, dy+10,al_map_rgb(100,80,0),1);
    }

    for(int i=0;i<MAX_MONEY;i++){
        if(!moneys[i].active) continue;
        float mx=WX(moneys[i].x), my=WY(moneys[i].y);
        if(mx<-10||mx>BUFFER_W+10||my<-10||my>BUFFER_H+10) continue;
        al_draw_filled_circle(mx,my,MONEY_R,al_map_rgb(0,175,50));
        al_draw_circle(mx,my,MONEY_R,al_map_rgb(0,110,25),1);
        al_draw_text(font,al_map_rgb(0,255,80),(int)mx-3,(int)my-13,0,"$");
    }

    if(boss.ping_timer>0){
        float pingx=WX(boss.x+MANAGER_SIZE/2), pingy=WY(boss.y+MANAGER_SIZE/2);
        float pulse=8.0f+4.0f*sinf(boss.ping_timer*0.25f);
        al_draw_circle(pingx,pingy,pulse,al_map_rgba(0,200,255,180),2);
    }

    ALLEGRO_COLOR bc;
    switch(boss.state){
        case MANAGER_CHASE:      bc=al_map_rgb(255,30,30);  break;
        case MANAGER_SEARCH:     bc=al_map_rgb(255,155,0);  break;
        case MANAGER_DISTRACTED: bc=al_map_rgb(60,60,220);  break;
        default:                 bc=al_map_rgb(175,35,35); break;
    }
    float bsx=WX(boss.x), bsy=WY(boss.y);
    if(bsx>-MANAGER_SIZE&&bsx<BUFFER_W+MANAGER_SIZE&&
       bsy>-MANAGER_SIZE&&bsy<BUFFER_H+MANAGER_SIZE){
        al_draw_filled_rectangle(bsx,bsy,bsx+MANAGER_SIZE,bsy+MANAGER_SIZE,bc);
        al_draw_text(font,al_map_rgb(255,255,255),(int)(bsx+4),(int)(bsy+4),0,"G");
    }

    ALLEGRO_COLOR pc;
    float ps=PLAYER_SIZE, po=0;
    if(player.hiding){
        pc=al_map_rgb(80,70,20);
        ps=PLAYER_SIZE-4; po=2;
    } else if(player.speed_debuff){
        pc=al_map_rgb(120,120,255);
    } else {
        pc=al_map_rgb(255,200,50);
    }
    al_draw_filled_rectangle(WX(player.x+po),WY(player.y+po),
                             WX(player.x+po+ps),WY(player.y+po+ps),pc);
    al_draw_rectangle(WX(player.x+po),WY(player.y+po),
                      WX(player.x+po+ps),WY(player.y+po+ps),
                      al_map_rgb(100,80,0),1);

    if(player.can_hide&&!player.hiding)
        al_draw_text(font,al_map_rgb(0,220,120),
                     (int)WX(player.x+PLAYER_SIZE/2)-14,
                     (int)WY(player.y-12),0,"[SHIFT]");
    if(player.hiding)
        al_draw_text(font,al_map_rgb(0,255,140),
                     (int)WX(player.x+PLAYER_SIZE/2)-18,
                     (int)WY(player.y-12),0,"ESCONDIDO");

    if(caught_flash>0){
        caught_flash--;
        al_draw_filled_rectangle(0,0,BUFFER_W,BUFFER_H,al_map_rgba(200,0,0,85));
    }

    draw_minimap();

    al_draw_filled_rectangle(0,0,BUFFER_W,14,al_map_rgba(0,0,0,180));
    char hud[100];
    snprintf(hud,sizeof(hud),"Dia %d/%d   Docs: %d/%d   $: %d   Vidas: %d",
             current_day,MAX_DAYS,docs_collected,docs_needed,money_count,player.night_lives);
    al_draw_text(font,al_map_rgb(255,255,255),4,2,0,hud);

    {
        int item_x = BUFFER_W - 2;
        if(player.has_walkie){ item_x-=24; al_draw_text(font,al_map_rgb(0,160,255), item_x,2,0,"[W]"); }
        if(player.has_keys)  { item_x-=24; al_draw_text(font,al_map_rgb(255,220,0), item_x,2,0,"[K]"); }
        if(player.has_coffee){ item_x-=24; al_draw_text(font,al_map_rgb(200,130,60),item_x,2,0,"[C]"); }
    }

    draw_suspicion_bar();

    if(player.speed_debuff){
        al_draw_filled_rectangle(0,BUFFER_H-12,BUFFER_W,BUFFER_H,al_map_rgba(30,0,60,180));
        al_draw_text(font,al_map_rgb(160,140,255),BUFFER_W/2,BUFFER_H-10,
                     ALLEGRO_ALIGN_CENTRE,"ULTIMA OPORTUNIDAD - F5=Guardar F9=Cargar");
    } else {
        al_draw_filled_rectangle(0,BUFFER_H-12,BUFFER_W,BUFFER_H,al_map_rgba(0,0,0,130));
        al_draw_text(font,al_map_rgb(130,130,150),BUFFER_W/2,BUFFER_H-10,
                     ALLEGRO_ALIGN_CENTRE,"F5=Guardar F9=Cargar");
    }
}

void draw_title()
{
    al_clear_to_color(al_map_rgb(10,10,40));
    al_draw_text(font,al_map_rgb(255,220,0), BUFFER_W/2,28,ALLEGRO_ALIGN_CENTRE,"THE MYSTERY OF");
    al_draw_text(font,al_map_rgb(255,255,80),BUFFER_W/2,42,ALLEGRO_ALIGN_CENTRE,"JUEVES DE FRESCURA");

    al_draw_text(font,al_map_rgb(220,200,120),BUFFER_W/2,62,ALLEGRO_ALIGN_CENTRE,"--- TARDE ---");
    al_draw_text(font,al_map_rgb(200,200,160),BUFFER_W/2,73,ALLEGRO_ALIGN_CENTRE,"WASD = mover   ESPACIO = minijuego");
    al_draw_text(font,al_map_rgb(200,200,160),BUFFER_W/2,83,ALLEGRO_ALIGN_CENTRE,"Recoge [C]afe, [K]llaves y [W]alkie antes de la noche");

    al_draw_text(font,al_map_rgb(180,140,140),BUFFER_W/2,97,ALLEGRO_ALIGN_CENTRE,"--- NOCHE ---");
    al_draw_text(font,al_map_rgb(200,200,160),BUFFER_W/2,108,ALLEGRO_ALIGN_CENTRE,"ESPACIO = tirar dinero para distraer al gerente");
    al_draw_text(font,al_map_rgb(200,200,160),BUFFER_W/2,118,ALLEGRO_ALIGN_CENTRE,"SHIFT = esconderse junto a un estante");
    al_draw_text(font,al_map_rgb(200,200,160),BUFFER_W/2,128,ALLEGRO_ALIGN_CENTRE,"TAB = ping walkie (si lo recogiste)");
    al_draw_text(font,al_map_rgb(160,140,200),BUFFER_W/2,140,ALLEGRO_ALIGN_CENTRE,"Barra de SOSPECHA: llena = te persigue");

    // Mostrar High Score
    char hs[64];
    snprintf(hs, sizeof(hs), "Record: Dia %d", high_score_day);
    al_draw_text(font,al_map_rgb(0, 255, 150), BUFFER_W/2, 155, ALLEGRO_ALIGN_CENTRE, hs);

    al_draw_text(font,al_map_rgb(255,255,255),BUFFER_W/2,BUFFER_H-18,ALLEGRO_ALIGN_CENTRE,"Presiona ENTER para comenzar");
}

void draw_day_complete()
{
    al_clear_to_color(al_map_rgb(0,8,0));
    if(current_day>=MAX_DAYS){
        al_draw_text(font,al_map_rgb(100,255,100),BUFFER_W/2,BUFFER_H/2-30,ALLEGRO_ALIGN_CENTRE,"CONSEGUISTE EL AUMENTO!");
        al_draw_text(font,al_map_rgb(255,255,80), BUFFER_W/2,BUFFER_H/2-16,ALLEGRO_ALIGN_CENTRE,"El gerente no pudo contigo.");
        al_draw_text(font,al_map_rgb(200,200,200),BUFFER_W/2,BUFFER_H/2,   ALLEGRO_ALIGN_CENTRE,"Eres el empleado del mes.");
        al_draw_text(font,al_map_rgb(160,160,160),BUFFER_W/2,BUFFER_H/2+14,ALLEGRO_ALIGN_CENTRE,"Gracias por jugar!");
    } else {
        char msg[64];
        snprintf(msg,sizeof(msg),"Dia %d completado!",current_day);
        al_draw_text(font,al_map_rgb(100,255,100),BUFFER_W/2,BUFFER_H/2-30,ALLEGRO_ALIGN_CENTRE,msg);
        snprintf(msg,sizeof(msg),"Manana es el Dia %d...",current_day+1);
        al_draw_text(font,al_map_rgb(220,200,80), BUFFER_W/2,BUFFER_H/2-16,ALLEGRO_ALIGN_CENTRE,msg);
        al_draw_text(font,al_map_rgb(180,140,140),BUFFER_W/2,BUFFER_H/2-2, ALLEGRO_ALIGN_CENTRE,"El gerente estara mas alerta.");
        char inv[80]="Inventario: ";
        if(player.has_coffee) strcat(inv,"[Cafe] ");
        if(player.has_keys)   strcat(inv,"[Llaves] ");
        if(player.has_walkie) strcat(inv,"[Walkie] ");
        if(!player.has_coffee&&!player.has_keys&&!player.has_walkie)
            strcat(inv,"(vacio)");
        al_draw_text(font,al_map_rgb(200,200,120),BUFFER_W/2,BUFFER_H/2+14,ALLEGRO_ALIGN_CENTRE,inv);
    }
    al_draw_text(font,al_map_rgb(255,255,255),BUFFER_W/2,BUFFER_H/2+30,ALLEGRO_ALIGN_CENTRE,"Presiona ENTER");
}

void draw_gameover()
{
    al_clear_to_color(al_map_rgb(8,0,0));
    char msg[64];
    snprintf(msg,sizeof(msg),"El gerente te atrapo. (Dia %d)",current_day);
    al_draw_text(font,al_map_rgb(255,50,50),  BUFFER_W/2,BUFFER_H/2-26,ALLEGRO_ALIGN_CENTRE,msg);
    al_draw_text(font,al_map_rgb(200,100,100),BUFFER_W/2,BUFFER_H/2-10,ALLEGRO_ALIGN_CENTRE,"Tendras que repetir la tarde de hoy.");
    al_draw_text(font,al_map_rgb(160,160,200),BUFFER_W/2,BUFFER_H/2+8, ALLEGRO_ALIGN_CENTRE,"(Conservas los objetos recogidos)");
    al_draw_text(font,al_map_rgb(255,255,255),BUFFER_W/2,BUFFER_H/2+24,ALLEGRO_ALIGN_CENTRE,"Presiona ENTER");
}

void draw_dialog()
{
    al_clear_to_color(al_map_rgb(20,20,40));
    float box_y = BUFFER_H - 80;
    al_draw_filled_rectangle(0, box_y, BUFFER_W, BUFFER_H,al_map_rgba(0,0,0,200));
    al_draw_rectangle(0, box_y, BUFFER_W, BUFFER_H,al_map_rgb(255,255,255), 2);
    al_draw_text(font, al_map_rgb(255,200,50),10, box_y + 5, 0,current_dialog[dialog_index].nombre);
    al_draw_multiline_text(font,al_map_rgb(255,255,255),10,box_y + 25,BUFFER_W - 20,14,0,current_dialog[dialog_index].texto);
    al_draw_text(font, al_map_rgb(180,180,180),BUFFER_W - 10, BUFFER_H - 20,ALLEGRO_ALIGN_RIGHT,"ENTER...");
}

/* ================================================================
   MAIN
   ================================================================ */
int main(void)
{
    srand((unsigned int)time(NULL));
    must_init(al_init(),                  "allegro");
    al_install_audio();
    al_init_acodec_addon();
    al_reserve_samples(16);
    must_init(al_install_keyboard(),      "keyboard");
    must_init(al_init_primitives_addon(), "primitives");
    must_init(al_init_image_addon(),      "image addon");
    al_init_font_addon();
    al_init_ttf_addon();

    disp_init();
    
    // Cargar Icono
    ALLEGRO_BITMAP *icon = al_load_bitmap("assets/icon.jpeg");
    if (!icon) {
        //cuadrado rojo para que no falle
        icon = al_create_bitmap(32, 32);
        al_set_target_bitmap(icon);
        al_clear_to_color(al_map_rgb(255, 0, 0));
        al_set_target_backbuffer(disp);
    }
    al_set_display_icon(disp, icon);

    font=al_create_builtin_font();
    load_highscore_text();
    
    ALLEGRO_EVENT_QUEUE* queue=al_create_event_queue();
    must_init(queue,"event queue");
    ALLEGRO_TIMER* timer=al_create_timer(1.0/FPS);
    must_init(timer,"timer");

    al_register_event_source(queue,al_get_display_event_source(disp));
    al_register_event_source(queue,al_get_keyboard_event_source());
    al_register_event_source(queue,al_get_timer_event_source(timer));

    bool keys[ALLEGRO_KEY_MAX]         = {0};
    bool keys_pressed[ALLEGRO_KEY_MAX] = {0};

    game_phase=PHASE_TITLE;
    bool running=true;
    al_start_timer(timer);

    while(running){
        ALLEGRO_EVENT event;
        al_wait_for_event(queue,&event);

        if(event.type==ALLEGRO_EVENT_DISPLAY_CLOSE) running=false;
        if(event.type==ALLEGRO_EVENT_KEY_DOWN){
            keys[event.keyboard.keycode]=true;
            keys_pressed[event.keyboard.keycode]=true;
        }
        if(event.type==ALLEGRO_EVENT_KEY_UP)
            keys[event.keyboard.keycode]=false;

        if(event.type!=ALLEGRO_EVENT_TIMER) 
            continue;

        // Logica de guardar/cargar global
        if(game_phase != PHASE_TITLE && game_phase != PHASE_DIALOG) {
            if(keys_pressed[ALLEGRO_KEY_F5]) {
                save_game_binary();
            }
            if(keys_pressed[ALLEGRO_KEY_F9]) {
                load_game_binary();
            }
        }

        // Actualizar temporizador de mensaje de estado
        if(status_msg_timer > 0) status_msg_timer--;

        switch(game_phase){
            case PHASE_TITLE:
                if(keys_pressed[ALLEGRO_KEY_ENTER]){
                    start_game();
                    start_dialog(dialog_intro, sizeof(dialog_intro) / sizeof(dialog_intro[0]), PHASE_AFTERNOON);
                }
                break;
            case PHASE_AFTERNOON:
                update_afternoon(keys,keys_pressed);
                break;
            case PHASE_DIALOG:
                if(keys_pressed[ALLEGRO_KEY_ENTER]){
                    dialog_index++;
                    if(dialog_index >= dialog_total){
                        dialog_index = 0;
                        if(next_phase_after_dialog == PHASE_AFTERNOON)
                            init_afternoon();
                        else if(next_phase_after_dialog == PHASE_NIGHT)
                            init_night();
                    }
                }
            break;    
            case PHASE_NIGHT:
                update_night(keys,keys_pressed);
                break;
            case PHASE_DAY_COMPLETE:
                if(transition_timer>0){
                    transition_timer--;
                    break;
                }
                if(keys_pressed[ALLEGRO_KEY_ENTER]){
                    if(current_day>=MAX_DAYS){
                        game_phase=PHASE_WIN;
                    } else {
                        current_day++;
                        player.night_lives=2;
                        init_afternoon();
                    }
                }
                break;
            case PHASE_GAMEOVER:
                if(keys_pressed[ALLEGRO_KEY_ENTER]){
                    player.night_lives=2;
                    init_afternoon(); 
                }
                break;
            case PHASE_WIN:
                if(keys_pressed[ALLEGRO_KEY_ENTER]) start_game();
                break;
        }

        memset(keys_pressed,0,sizeof(keys_pressed));

        disp_pre_draw();
        switch(game_phase){
            case PHASE_TITLE:        
                draw_title();        
                break;
            case PHASE_AFTERNOON:    
                draw_afternoon();    
                break;
            case PHASE_DIALOG:
                draw_dialog();
                break;    
            case PHASE_NIGHT:        
                draw_night();        
                break;
            case PHASE_DAY_COMPLETE: 
                draw_day_complete(); 
                break;
            case PHASE_GAMEOVER:     
                draw_gameover();     
                break;
            case PHASE_WIN:          
                draw_day_complete(); 
                break;
        }
        
        // Dibujar mensaje de estado
        if(status_msg_timer > 0) {
             al_draw_text(font, al_map_rgb(255, 255, 0), BUFFER_W/2, BUFFER_H/2 - 50, ALLEGRO_ALIGN_CENTRE, status_msg);
        }

        disp_post_draw();
    }

    al_destroy_font(font);
    al_destroy_timer(timer);
    al_destroy_event_queue(queue);
    disp_deinit();
    if(icon) 
        al_destroy_bitmap(icon);
    return 0;
}