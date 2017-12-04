#include <zlib.h>
#include "ui.h"
#include "core/assert.h"
#include "core/filesystem.h"
#include "core/sort.h"
#include "core/string.h"
#include "core/thread.h"
#include "core/time.h"
#include "guest/gdrom/disc.h"
#include "guest/pvr/tex.h"
#include "host/host.h"
#include "imgui.h"
#include "options.h"
#include "render/render_backend.h"

struct ui;

#define UI_MAX_HISTORY 32
#define UI_MAX_GAMES 1024
#define UI_MAX_VOLUMES 32
#define UI_MAX_ENTRIES 512
#define UI_MAX_GAMEDIRS 32

enum {
  UI_DLG_NEW,
  UI_DLG_ACTIVE,
  UI_DLG_CANCEL,
  UI_DLG_SUCCESS,
};

enum {
  UI_CATCH_NONE,
  UI_CATCH_DOWN,
  UI_CATCH_UP,
};

typedef void (*build_cb)(struct ui *);

struct page {
  const char *name;
  build_cb build;
};

struct game {
  char filename[PATH_MAX];
  char prodname[256];
  char prodmeta[256];
  texture_handle_t tex;
};

struct file_dlg {
  int state;

  char result[PATH_MAX];

  char volumes[UI_MAX_VOLUMES][PATH_MAX];
  int num_volumes;

  char entries[UI_MAX_ENTRIES][PATH_MAX];
  int num_entries;

  char *sorted[UI_MAX_ENTRIES];
};

struct input_page {
  int catch_state;
  struct button_map *catch_btnmap;
};

struct library_page {
  struct file_dlg adddlg;
  int adddir;
};

struct games_page {
  int state;

  /* game list state */
  int curr_game;
  int next_game;
  int64_t scroll_start;
  int scroll_duration;

  /* loading mask state */
  int64_t loading_start;
};

struct ui {
  struct host *host;
  struct render_backend *r;

  int64_t time;

  /* navigation */
  struct page *history[UI_MAX_HISTORY];
  int history_pos;
  int *dlg;

  /* assets */
  texture_handle_t clouds_tex;
  texture_handle_t disc_tex;

  /* page state */
  struct games_page games_page;
  struct library_page library_page;
  struct input_page input_page;

  /* scan state */
  volatile int scanning;
  char scan_status[PATH_MAX * 2];
  thread_t scan_thread;

  /* mutex to control acess to state used by both threads */
  mutex_t scan_mutex;

  struct game games[UI_MAX_GAMES];
  int num_games;
};

static struct page pages[UI_NUM_PAGES];

#include "assets/clouds.inc"
#include "assets/disc.inc"

/*
 * strings
 */

/* clang-format off */
#define UI_STR_TAB_GAMES     "GAMES"
#define UI_STR_TAB_OPTIONS   "OPTIONS"
#define UI_STR_NO_GAMES      "Your game library is currently empty. Add a directory containing valid .cdi, .chd or .gdi image(s) to get started."
#define UI_STR_GO_TO_LIBRARY "Go to Library"
#define UI_STR_BTN_CANCEL    "Cancel"
#define UI_STR_BTN_ADD       "Add"
#define UI_STR_CARD_LIBRARY  "    " IMICON_HDD "\nLibrary"
#define UI_STR_CARD_SYSTEM   "    " IMICON_MICROCHIP "\nSystem"
#define UI_STR_CARD_VIDEO    "  " IMICON_DESKTOP "\nVideo"
#define UI_STR_CARD_INPUT    "  " IMICON_GAMEPAD "\nInput"
#define UI_STR_LIBRARY_ADD   "Add Directory"
#define UI_STR_TRUE          "true"
#define UI_STR_FALSE         "false"
/* clang-format on */

/*
 * theme
 */

/* clang-format off */
#define VW(w) ((w / 100.0f) * io->DisplaySize.x)
#define VH(h) ((h / 100.0f) * io->DisplaySize.y)

#define UI_TRANS             0x00000000
#define UI_WHITE             0xffffffff
#define UI_LIGHT_BLUE        0xffa9583e
#define UI_DARK_BLUE         0xff201e19
#define UI_DARKER_BLUE       0xff181611
#define UI_LIGHT_RED         0xff3e3ea9

#define UI_WIN_BG            UI_DARK_BLUE
#define UI_WIN_TEXT          0xffd0d0d0
#define UI_CHILD_BG          UI_DARKER_BLUE
#define UI_CHILD_TEXT        UI_WIN_TEXT
#define UI_MODAL_BG          0x80000000
#define UI_NAV_HIGHLIGHT     0xc0ffffff
#define UI_BTN_BG            UI_DARKER_BLUE
#define UI_BTN_ACTIVE_BG     UI_LIGHT_BLUE
#define UI_BTN_HOVER_BG      UI_LIGHT_BLUE
#define UI_BTN_TEXT          UI_WIN_TEXT
#define UI_BTN_NEG_BG        UI_DARKER_BLUE
#define UI_BTN_NEG_ACTIVE_BG UI_LIGHT_RED
#define UI_BTN_NEG_HOVER_BG  UI_LIGHT_RED
#define UI_BTN_NEG_TEXT      UI_WIN_TEXT

#define UI_TAB_BG            UI_TRANS
#define UI_TAB_TEXT          UI_WIN_TEXT
#define UI_TAB_ACTIVE_BG     UI_LIGHT_BLUE
#define UI_TAB_HOVERED_BG    UI_TAB_BG
#define UI_SEL_BG            UI_TRANS
#define UI_SEL_TEXT          UI_WIN_TEXT
#define UI_SEL_ACTIVE_BG     UI_LIGHT_BLUE
#define UI_SEL_HOVERED_BG    UI_SEL_BG

#define UI_PAGE_MAX_WIDTH    VW(70.0f)
#define UI_PAGE_MAX_HEIGHT   VH(50.0f)
#define UI_BTN_PADDING       { VW(1.5f), VH(1.5f) }

#define UI_FONT_HEIGHT       (int)VH(3.5f)
#define UI_PAGE_FONT_HEIGHT  (int)VH(5.0f)
#define UI_GAME_FONT_HEIGHT  (int)VH(5.0f)
#define UI_CARD_FONT_HEIGHT  (int)VH(7.0f)
/* clang-format on */

void igPopStyle_BtnNeg() {
  igPopStyleVar(1);
  igPopStyleColor(4);
}

void igPushStyle_BtnNeg() {
  struct ImGuiIO *io = igGetIO();
  struct ImVec2 btn_padding = UI_BTN_PADDING;
  igPushStyleVarVec(ImGuiStyleVar_FramePadding, btn_padding);
  igPushStyleColorU32(ImGuiCol_Text, UI_BTN_NEG_TEXT);
  igPushStyleColorU32(ImGuiCol_Button, UI_BTN_NEG_BG);
  igPushStyleColorU32(ImGuiCol_ButtonActive, UI_BTN_NEG_ACTIVE_BG);
  igPushStyleColorU32(ImGuiCol_ButtonHovered, UI_BTN_NEG_HOVER_BG);
}

void igPopStyle_Btn() {
  igPopStyleVar(1);
  igPopStyleColor(4);
}

void igPushStyle_Btn() {
  struct ImGuiIO *io = igGetIO();
  struct ImVec2 btn_padding = UI_BTN_PADDING;
  igPushStyleVarVec(ImGuiStyleVar_FramePadding, btn_padding);
  igPushStyleColorU32(ImGuiCol_Text, UI_BTN_TEXT);
  igPushStyleColorU32(ImGuiCol_Button, UI_BTN_BG);
  igPushStyleColorU32(ImGuiCol_ButtonActive, UI_BTN_ACTIVE_BG);
  igPushStyleColorU32(ImGuiCol_ButtonHovered, UI_BTN_HOVER_BG);
}

void igPopStyle_Card() {
  igPopStyleColor(4);
  igPopFont();
}

void igPushStyle_Card() {
  struct ImGuiIO *io = igGetIO();
  igPushFontEx(IMFONT_OPENSANS_REGULAR, UI_CARD_FONT_HEIGHT);
  igPushStyleColorU32(ImGuiCol_Text, UI_BTN_TEXT);
  igPushStyleColorU32(ImGuiCol_Button, UI_BTN_BG);
  igPushStyleColorU32(ImGuiCol_ButtonActive, UI_BTN_ACTIVE_BG);
  igPushStyleColorU32(ImGuiCol_ButtonHovered, UI_BTN_HOVER_BG);
}

void igPopStyle_PageTab() {
  igPopStyleColor(4);
  igPopFont();
}

void igPushStyle_PageTab() {
  struct ImGuiIO *io = igGetIO();
  igPushFontEx(IMFONT_OSWALD_MEDIUM, UI_PAGE_FONT_HEIGHT);
  igPushStyleColorU32(ImGuiCol_Text, UI_BTN_TEXT);
  igPushStyleColorU32(ImGuiCol_Button, UI_BTN_BG);
  igPushStyleColorU32(ImGuiCol_ButtonActive, UI_BTN_ACTIVE_BG);
  igPushStyleColorU32(ImGuiCol_ButtonHovered, UI_BTN_HOVER_BG);
}

void igPopStyle_Tab() {
  igPopStyleColor(4);
}

void igPushStyle_Tab() {
  struct ImGuiIO *io = igGetIO();
  igPushStyleColorU32(ImGuiCol_Text, UI_TAB_TEXT);
  igPushStyleColorU32(ImGuiCol_Button, UI_TAB_BG);
  igPushStyleColorU32(ImGuiCol_ButtonActive, UI_TAB_ACTIVE_BG);
  igPushStyleColorU32(ImGuiCol_ButtonHovered, UI_TAB_HOVERED_BG);
}

void igPopStyle_Selectable() {
  igPopStyleColor(4);
}

void igPushStyle_Selectable() {
  struct ImGuiIO *io = igGetIO();
  igPushStyleColorU32(ImGuiCol_Text, UI_SEL_TEXT);
  igPushStyleColorU32(ImGuiCol_Button, UI_SEL_BG);
  igPushStyleColorU32(ImGuiCol_ButtonActive, UI_SEL_ACTIVE_BG);
  igPushStyleColorU32(ImGuiCol_ButtonHovered, UI_SEL_HOVERED_BG);
}

/*
 * private functions
 */
const struct ImVec2 img_uv[2] = {
    {0.0f, 0.0f}, {1.0f, 1.0f},
};

static float ease_in_linear(float t, float b, float c, float d) {
  float f = MIN(MAX((t / d), 0.0f), 1.0f);
  return b + c * f;
}

static float ease_in_quad(float t, float b, float c, float d) {
  float f = MIN(MAX((t / d) * (t / d), 0.0f), 1.0f);
  return b + c * f;
}

static int ui_explode_gamedir(struct ui *ui, char *dirs, int num, size_t size) {
  mutex_lock(ui->scan_mutex);

  char copy[OPTION_MAX_LENGTH];
  strncpy(copy, OPTION_gamedir, sizeof(copy));

  int n = 0;
  char *tok = strtok(copy, ";");

  while (n < num && tok) {
    /* ignore empty entries */
    if (*tok != 0) {
      char *dir = dirs + n * size;
      strncpy(dir, tok, size);
      n++;
    }

    tok = strtok(NULL, ";");
  }

  mutex_unlock(ui->scan_mutex);

  return n;
}

static void ui_implode_gamedir(struct ui *ui, const char *dirs, int num,
                               size_t size) {
  mutex_lock(ui->scan_mutex);

  char *ptr = OPTION_gamedir;
  char *end = ptr + sizeof(OPTION_gamedir);

  *ptr = 0;

  for (int n = 0; n < num && ptr < end; n++) {
    const char *dir = dirs + n * size;
    ptr += snprintf(ptr, end - ptr, "%s;", dir);
  }

  mutex_unlock(ui->scan_mutex);
}

static void ui_close_dlg(struct ui *ui, int state) {
  CHECK(ui->dlg != NULL);
  CHECK(*ui->dlg == UI_DLG_ACTIVE);
  *ui->dlg = state;
  ui->dlg = NULL;
}

static void ui_open_dlg(struct ui *ui, int *dlg) {
  CHECK(ui->dlg == NULL);
  ui->dlg = dlg;
  CHECK(*ui->dlg == UI_DLG_NEW);
  *ui->dlg = UI_DLG_ACTIVE;
}

static int ui_active_tab(struct ui *ui) {
  for (int i = ui->history_pos - 1; i >= 0; i--) {
    struct page *page = ui->history[i];

    if (page->name) {
      int pagenum = (int)(page - pages);
      return pagenum;
    }
  }
  return UI_PAGE_NONE;
}

static void ui_end_page(struct ui *ui) {
  igPopStyleVar(1);

  igEnd();

  igPopStyleColor(2);
  igPopStyleVar(3);
  igPopFont();
}

static void ui_begin_page(struct ui *ui, struct page *page) {
  struct ImGuiIO *io = igGetIO();
  struct ImGuiStyle *style = igGetStyle();

  struct ImVec2 pos = {0.0f, 0.0f};
  struct ImVec2 size = {VW(100.0f), VH(100.0f)};
  struct ImVec2 padding = {0.0f, 0.0f};
  struct ImVec2 spacing = {VW(1.0f), VH(1.3f)};
  struct ImVec2 original_padding = style->WindowPadding;

  igSetNextWindowSize(size, 0);
  igSetNextWindowPos(pos, 0);

  igPushFontEx(IMFONT_OPENSANS_REGULAR, UI_FONT_HEIGHT);
  igPushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  igPushStyleVarVec(ImGuiStyleVar_ItemSpacing, spacing);
  igPushStyleVarVec(ImGuiStyleVar_WindowPadding, padding);
  igPushStyleColorU32(ImGuiCol_WindowBg, UI_WIN_BG);
  igPushStyleColorU32(ImGuiCol_NavHighlight, UI_NAV_HIGHLIGHT);

  /* give each page a unique window name, as imgui tracks navigation state per
     window. this enables the previously selected item to be restored properly
     when going back to a previous window */
  char title[128];
  int pagenum = (int)(page - pages);
  snprintf(title, sizeof(title), "ui%d", pagenum);
  igBegin(title, NULL,
          ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
              ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
              ImGuiWindowFlags_NoNavFocus |
              ImGuiWindowFlags_NoBringToFrontOnFocus);

  /* push back original padding immediately */
  igPushStyleVarVec(ImGuiStyleVar_WindowPadding, original_padding);

  struct ImDrawList *list = igGetWindowDrawList();

  /* background */
  {
    struct ImVec2 min = {0.0f, 0.0f};
    struct ImVec2 max = {VW(100.0f), VH(100.0f)};
    ImDrawList_AddImage(list, (ImTextureID)(intptr_t)ui->clouds_tex, min, max,
                        img_uv[0], img_uv[1], UI_WHITE);
  }

  /* page tabs */
  {
    /* calculate number of tabs in navbar */
    int num_tabs = 0;
    for (int i = 0; i < UI_NUM_PAGES; i++) {
      struct page *page = &pages[i];

      if (page->name) {
        num_tabs++;
      }
    }

    struct ImVec2 btn_size = {VW(16.8f), VH(6.94f)};
    struct ImVec2 btn_margin = {VW(1.0f), VH(0.0f)};
    float width = (btn_size.x + btn_margin.x) * num_tabs - btn_margin.x;
    struct ImVec2 pos = {(VW(100.0f) - width) / 2.0f, VH(3.47f)};

    igSetCursorPos(pos);
    igPushStyle_PageTab();

    int active_tab = ui_active_tab(ui);

    /* let default focus go to the page content */
    igPushNavDefaultFocus(false);

    for (int i = 0; i < UI_NUM_PAGES; i++) {
      struct page *page = &pages[i];
      int selected = i == active_tab;

      if (!page->name) {
        continue;
      }

      igPushIDPtr(page);
      if (igTab(page->name, btn_size, selected)) {
        ui_set_page(ui, i);
      }
      igPopID();

      igSameLine(0.0f, btn_margin.x);
    }

    igPopNavDefaultFocus();

    igPopStyle_PageTab();
  }
}

/*
 * game scanning
 */
static const char *game_exts[] = {".cdi", ".chd", ".gdi"};

static int ui_has_game_ext(const char *filename, const char **exts,
                           int num_exts) {
  for (int i = 0; i < num_exts; i++) {
    const char *ext = exts[i];

    if (strstr(filename, ext)) {
      return 1;
    }
  }

  return 0;
}

static void ui_insert_game(struct ui *ui, struct game *new_game) {
  int pos = ui->num_games;

  for (int i = 0; i < ui->num_games; i++) {
    struct game *game = &ui->games[i];

    /* avoid inserting duplicates */
    if (!strcmp(game->filename, new_game->filename)) {
      return;
    }

    /* find the sorted position to insert at */
    if (strcmp(game->prodname, new_game->prodname) > 0) {
      pos = i;
      break;
    }
  }

  /* shift and insert */
  CHECK_LT(ui->num_games, UI_MAX_GAMES);
  ui->num_games++;

  for (int i = ui->num_games - 1; i > pos; i--) {
    struct game *game = &ui->games[i];
    struct game *prev = &ui->games[i - 1];
    *game = *prev;
  }

  struct game *game = &ui->games[pos];
  *game = *new_game;
}

static void ui_scan_games_f(struct ui *ui, const char *filename) {
  mutex_lock(ui->scan_mutex);

  /* update status */
  snprintf(ui->scan_status, sizeof(ui->scan_status), "scanning %s", filename);

  if (ui_has_game_ext(filename, game_exts, ARRAY_SIZE(game_exts))) {
    struct disc *disc = disc_create(filename, 0);

    if (disc) {
      struct game game = {0};
      strncpy(game.filename, filename, sizeof(game.filename));
      strncpy(game.prodname, disc->prodnme, sizeof(game.prodname));
      snprintf(game.prodmeta, sizeof(game.prodmeta), "%s / %s", disc->prodver,
               disc->prodnum);
      ui_insert_game(ui, &game);

      disc_destroy(disc);
    }
  }

  mutex_unlock(ui->scan_mutex);
}

static void ui_scan_games_d(struct ui *ui, const char *path) {
  struct games_page *page = &ui->games_page;
  DIR *dir = opendir(path);

  if (!dir) {
    LOG_WARNING("ui_scan_dir failed to open %s", path);
    return;
  }

  struct dirent *ent = NULL;

  while ((ent = readdir(dir)) != NULL) {
    const char *dname = ent->d_name;

    /* ignore special directories */
    if (!strcmp(dname, "..") || !strcmp(dname, ".")) {
      continue;
    }

    char abspath[PATH_MAX];
    snprintf(abspath, sizeof(abspath), "%s" PATH_SEPARATOR "%s", path, dname);

    if (ent->d_type & DT_DIR) {
      ui_scan_games_d(ui, abspath);
    } else if (ent->d_type & DT_REG) {
      ui_scan_games_f(ui, abspath);
    }
  }

  closedir(dir);
}

static void ui_scan_games(struct ui *ui) {
  char dirs[UI_MAX_GAMEDIRS][PATH_MAX];
  int num_dirs = ui_explode_gamedir(ui, dirs[0], UI_MAX_GAMEDIRS, PATH_MAX);

  for (int i = 0; i < num_dirs; i++) {
    ui_scan_games_d(ui, dirs[i]);
  }
}

static void *ui_scan_thread(void *data) {
  struct ui *ui = data;

  ui->scanning = 1;
  ui_scan_games(ui);
  ui->scanning = 0;

  return NULL;
}

static void ui_stop_game_scan(struct ui *ui) {
  if (!ui->scan_thread) {
    return;
  }

  void *result;
  thread_join(ui->scan_thread, &result);
  ui->scan_thread = NULL;

  mutex_destroy(ui->scan_mutex);
  ui->scan_mutex = NULL;
}

static void ui_start_game_scan(struct ui *ui) {
  /* if a scan is already active, early out */
  if (ui->scanning) {
    return;
  }

  /* clean up the scan thread */
  ui_stop_game_scan(ui);

  ui->scan_mutex = mutex_create();
  ui->scan_thread = thread_create(&ui_scan_thread, NULL, ui);
}

/*
 * file dialog
 */
static int ui_file_dlg_cmp(const void *a, const void *b) {
  const char **entry_a = (const char **)a;
  const char **entry_b = (const char **)b;
  return strcmp(*entry_a, *entry_b) < 0;
}

static void ui_file_dlg_scan(struct file_dlg *dlg, const char *path) {
  dlg->num_entries = 0;

  DIR *dir = opendir(path);

  if (!dir) {
    return;
  }

  struct dirent *ent = NULL;

  while ((ent = readdir(dir)) != NULL) {
    const char *dname = ent->d_name;

    if (!(ent->d_type & DT_DIR)) {
      continue;
    }

    /* ignore fake cwd dir */
    if (!strcmp(dname, ".")) {
      continue;
    }

    if (dlg->num_entries < UI_MAX_ENTRIES) {
      char *entry = dlg->entries[dlg->num_entries];
      char **sorted = &dlg->sorted[dlg->num_entries];
      *sorted = entry;
      snprintf(entry, PATH_MAX, "%s" PATH_SEPARATOR "%s", path, dname);
      dlg->num_entries++;
    }
  }

  closedir(dir);

  msort(dlg->sorted, dlg->num_entries, sizeof(char *), ui_file_dlg_cmp);
}

static void ui_file_dlg_select(struct file_dlg *dlg, const char *path) {
  /* convert to absolute path */
  char tmp[PATH_MAX];
  fs_realpath(path, tmp, sizeof(tmp));

  strncpy(dlg->result, tmp, PATH_MAX);
  ui_file_dlg_scan(dlg, dlg->result);
}

static int ui_file_dlg(struct ui *ui, struct file_dlg *dlg) {
  struct ImGuiIO *io = igGetIO();
  struct ImGuiStyle *style = igGetStyle();

  const struct ImVec2 zero_vec2 = {0.0f, 0.0f};

  /* initialize dialog */
  if (dlg->state == UI_DLG_NEW) {
    dlg->num_volumes = fs_mediadirs(dlg->volumes[0], UI_MAX_VOLUMES, PATH_MAX);

    ui_open_dlg(ui, &dlg->state);
  }

  /* don't render if the dialog has been closed */
  if (dlg->state != UI_DLG_ACTIVE) {
    return 1;
  }

  /* dialog is rendered as a fullscreen window to trap focus */
  struct ImVec2 win_min = {VW(0.0f), VH(0.0f)};
  struct ImVec2 win_size = {VW(100.0f), VH(100.0f)};
  struct ImVec2 dlg_size = {VW(100.0f) * 0.7f, VH(100.0f) * 0.7f};
  struct ImVec2 dlg_min = {(VW(100.0f) - dlg_size.x) / 2.0f,
                           (VH(100.0f) - dlg_size.y) / 2.0f};
  struct ImVec2 dlg_max = {dlg_min.x + dlg_size.x, dlg_min.y + dlg_size.y};

  igSetNextWindowPos(win_min, 0);
  igSetNextWindowSize(win_size, 0);
  igPushStyleColorU32(ImGuiCol_WindowBg, UI_MODAL_BG);
  igBegin("file dialog", NULL,
          ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
              ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);

  igSetCursorPos(dlg_min);
  igPushStyleColorU32(ImGuiCol_ChildWindowBg, UI_WIN_BG);
  igBeginChild(
      "dialog content", dlg_size, false,
      ImGuiWindowFlags_AlwaysUseWindowPadding | ImGuiWindowFlags_NavFlattened);

  struct ImVec2 content_min;
  struct ImVec2 content_max;
  igGetWindowContentRegionMin(&content_min);
  igGetWindowContentRegionMax(&content_max);

  float content_width = content_max.x - content_min.x;
  float content_height = content_max.y - content_min.y;
  float path_height = (float)UI_FONT_HEIGHT;
  float actions_width = VH(24.0f);
  float cwd_width = content_width - actions_width - style->ItemSpacing.x;
  float cwd_height = content_height - style->ItemSpacing.y - path_height;

  /* directory list*/
  {
    struct ImVec2 size = {cwd_width, cwd_height};
    struct ImVec2 btn_size = {-1.0f, 0.0f};
    struct ImVec2 btn_align = {0.0f, 0.5f};
    char label[PATH_MAX * 2];

    igPushStyleColorU32(ImGuiCol_ChildWindowBg, UI_CHILD_BG);
    igBeginChild("entries", size, false,
                 ImGuiWindowFlags_AlwaysUseWindowPadding |
                     ImGuiWindowFlags_NavFlattened);

    igPushStyle_Selectable();
    igPushStyleVarVec(ImGuiStyleVar_ButtonTextAlign, btn_align);

    if (dlg->num_entries) {
      for (int i = 0; i < dlg->num_entries; i++) {
        const char *path = dlg->sorted[i];

        int n = snprintf(label, sizeof(label), IMICON_FOLDER_OPEN " ");
        fs_basename(path, label + n, sizeof(label) - n);

        if (igButton(label, btn_size)) {
          ui_file_dlg_select(dlg, path);
        }
      }
    } else {
      for (int i = 0; i < dlg->num_volumes; i++) {
        const char *path = dlg->volumes[i];

        int n = snprintf(label, sizeof(label), IMICON_HDD " ");
        fs_basename(path, label + n, sizeof(label) - n);

        if (igButton(label, btn_size)) {
          ui_file_dlg_select(dlg, path);
        }
      }
    }

    igPopStyleVar(1);
    igPopStyle_Selectable();

    igEndChild();
    igPopStyleColor(1);
  }

  /* actions */
  {
    struct ImVec2 size = {actions_width, cwd_height};
    struct ImVec2 btn_size = {-1.0f, 0.0f};

    igSameLine(0.0f, style->ItemSpacing.x);

    igBeginChild("actions", size, false, ImGuiWindowFlags_NavFlattened);

    igPushStyle_Btn();

    if (igButton(UI_STR_BTN_ADD, btn_size)) {
      if (dlg->result[0]) {
        ui_close_dlg(ui, UI_DLG_SUCCESS);
      } else {
        ui_close_dlg(ui, UI_DLG_CANCEL);
      }
    }

    if (igButton(UI_STR_BTN_CANCEL, btn_size)) {
      ui_close_dlg(ui, UI_DLG_CANCEL);
    }

    igPopStyle_Btn();

    igEndChild();
  }

  /* current path */
  igText(dlg->result);

  igEndChild();
  igPopStyleColor(1);

  igEnd();
  igPopStyleColor(1);

  return dlg->state != UI_DLG_ACTIVE;
}

/*
 * input page
 */
static void ui_controllers_build(struct ui *ui) {
  struct ImGuiIO *io = igGetIO();

  struct ImVec2 size = {UI_PAGE_MAX_WIDTH, UI_PAGE_MAX_HEIGHT};
  struct ImVec2 pos = {(VW(100.0f) - size.x) / 2.0f,
                       (VH(100.0f) - size.y) / 2.0f};
  struct ImVec2 btn_size = {-1.0f, VH(8.0f)};

  igSetCursorPos(pos);
  igBeginChild("controllers", size, false, ImGuiWindowFlags_NavFlattened);

  igPushStyle_Btn();

  int max_controllers = input_max_controllers(ui->host);
  for (int i = 0; i < max_controllers; i++) {
    const char *controller_name = input_controller_name(ui->host, i);
    if (!controller_name) {
      controller_name = "No controller detected.";
    }
    char port[32];
    snprintf(port, sizeof(port), "Port %d", i);
    igOptionString(port, controller_name, btn_size);
  }

  igPopStyle_Btn();

  igEndChild();
}

static void ui_keyboard_build(struct ui *ui) {
  struct input_page *page = &ui->input_page;
  struct ImGuiIO *io = igGetIO();

  struct ImVec2 size = {UI_PAGE_MAX_WIDTH, UI_PAGE_MAX_HEIGHT};
  struct ImVec2 pos = {(VW(100.0f) - size.x) / 2.0f,
                       (VH(100.0f) - size.y) / 2.0f};
  struct ImVec2 btn_size = {-1.0f, VH(8.0f)};

  igSetCursorPos(pos);
  igBeginChild("keyboard", size, false, ImGuiWindowFlags_NavFlattened);

  igPushStyle_Btn();

  for (int i = 0; i < NUM_BUTTONS; i++) {
    struct button_map *btnmap = &BUTTONS[i];

    if (!btnmap->desc) {
      continue;
    }

    const char *value = get_name_by_key(*btnmap->key);

    if (page->catch_state == UI_CATCH_DOWN && page->catch_btnmap == btnmap) {
      value = "Waiting...";
    }

    if (igOptionString(btnmap->desc, value, btn_size)) {
      page->catch_state = UI_CATCH_DOWN;
      page->catch_btnmap = btnmap;
    }
  }

  igPopStyle_Btn();

  igEndChild();
}

static void ui_input_build(struct ui *ui) {
  struct ImGuiIO *io = igGetIO();

  struct ImVec2 size = {UI_PAGE_MAX_WIDTH, UI_PAGE_MAX_HEIGHT};
  struct ImVec2 pos = {(VW(100.0f) - size.x) / 2.0f,
                       (VH(100.0f) - size.y) / 2.0f};
  struct ImVec2 btn_size = {-1.0f, VH(8.0f)};

  igSetCursorPos(pos);
  igBeginChild("input", size, false, ImGuiWindowFlags_NavFlattened);

  igPushStyle_Btn();

  if (igButton("Controller info", btn_size)) {
    ui_set_page(ui, UI_PAGE_CONTROLLERS);
  }

  if (igButton("Keyboard binds", btn_size)) {
    ui_set_page(ui, UI_PAGE_KEYBOARD);
  }

  igPopStyle_Btn();

  igEndChild();
}

/*
 * video page
 */
static void ui_video_build(struct ui *ui) {
  struct ImGuiIO *io = igGetIO();

  struct ImVec2 size = {UI_PAGE_MAX_WIDTH, UI_PAGE_MAX_HEIGHT};
  struct ImVec2 pos = {(VW(100.0f) - size.x) / 2.0f,
                       (VH(100.0f) - size.y) / 2.0f};
  struct ImVec2 btn_size = {-1.0f, VH(8.0f)};

  igSetCursorPos(pos);
  igBeginChild("video", size, false, ImGuiWindowFlags_NavFlattened);

  igPushStyle_Btn();

  {
    const char *value_str = OPTION_fullscreen ? UI_STR_TRUE : UI_STR_FALSE;

    if (igOptionString("Fullscreen", value_str, btn_size)) {
      OPTION_fullscreen = !OPTION_fullscreen;
      OPTION_fullscreen_dirty = 1;
    }
  }

  {
    if (igOptionString("Aspect ratio", OPTION_aspect, btn_size)) {
      int next = 0;
      for (int i = 0; i < NUM_ASPECT_RATIOS; i++) {
        if (!strcmp(ASPECT_RATIOS[i], OPTION_aspect)) {
          next = (i + 1) % NUM_ASPECT_RATIOS;
          break;
        }
      }
      strncpy(OPTION_aspect, ASPECT_RATIOS[next], sizeof(OPTION_aspect));
      OPTION_aspect_dirty = 1;
    }
  }

  igPopStyle_Btn();

  igEndChild();
}

/*
 * system page
 */
static void ui_system_build(struct ui *ui) {
  struct ImGuiIO *io = igGetIO();

  struct ImVec2 size = {UI_PAGE_MAX_WIDTH, UI_PAGE_MAX_HEIGHT};
  struct ImVec2 pos = {(VW(100.0f) - size.x) / 2.0f,
                       (VH(100.0f) - size.y) / 2.0f};
  struct ImVec2 btn_size = {-1.0f, VH(8.0f)};

  igSetCursorPos(pos);
  igBeginChild("system", size, false, ImGuiWindowFlags_NavFlattened);

  igPushStyle_Btn();

  {
    if (igOptionString("Time sync", OPTION_sync, btn_size)) {
      int next = 0;
      for (int i = 0; i < NUM_TIMESYNCS; i++) {
        if (!strcmp(TIMESYNCS[i], OPTION_sync)) {
          next = (i + 1) % NUM_TIMESYNCS;
          break;
        }
      }
      strncpy(OPTION_sync, TIMESYNCS[next], sizeof(OPTION_sync));
      OPTION_sync_dirty = 1;
    }
  }

  {
    if (igOptionString("Region", OPTION_region, btn_size)) {
      int next = 0;
      for (int i = 0; i < NUM_REGIONS; i++) {
        if (!strcmp(REGIONS[i], OPTION_region)) {
          next = (i + 1) % NUM_REGIONS;
          break;
        }
      }
      strncpy(OPTION_region, REGIONS[next], sizeof(OPTION_region));
      OPTION_region_dirty = 1;
    }
  }

  {
    if (igOptionString("Language", OPTION_language, btn_size)) {
      int next = 0;
      for (int i = 0; i < NUM_LANGUAGES; i++) {
        if (!strcmp(LANGUAGES[i], OPTION_language)) {
          next = (i + 1) % NUM_LANGUAGES;
          break;
        }
      }
      strncpy(OPTION_language, LANGUAGES[next], sizeof(OPTION_language));
      OPTION_language_dirty = 1;
    }
  }

  {
    if (igOptionString("Broadcast", OPTION_broadcast, btn_size)) {
      int next = 0;
      for (int i = 0; i < NUM_BROADCASTS; i++) {
        if (!strcmp(BROADCASTS[i], OPTION_broadcast)) {
          next = (i + 1) % NUM_BROADCASTS;
          break;
        }
      }
      strncpy(OPTION_broadcast, BROADCASTS[next], sizeof(OPTION_broadcast));
      OPTION_broadcast_dirty = 1;
    }
  }

  igPopStyle_Btn();

  igEndChild();
}

/*
 * library page
 */
static void ui_library_build(struct ui *ui) {
  struct library_page *page = &ui->library_page;
  struct ImGuiIO *io = igGetIO();

  char dirs[UI_MAX_GAMEDIRS][PATH_MAX];
  int num_dirs = ui_explode_gamedir(ui, dirs[0], UI_MAX_GAMEDIRS, PATH_MAX);
  int modified = 0;

  struct ImVec2 size = {UI_PAGE_MAX_WIDTH, UI_PAGE_MAX_HEIGHT};
  struct ImVec2 pos = {(VW(100.0f) - size.x) / 2.0f,
                       (VH(100.0f) - size.y) / 2.0f};
  struct ImVec2 btn_size = {-1.0f, VH(8.0f)};

  igSetCursorPos(pos);
  igBeginChild("library", size, false, ImGuiWindowFlags_NavFlattened);

  /* list of directories */
  {
    igPushStyle_BtnNeg();

    int remove = -1;
    for (int i = 0; i < num_dirs; i++) {
      igPushIDPtr(dirs[i]);
      if (igOptionString(dirs[i], IMICON_TIMES, btn_size)) {
        remove = i;
      }
      igPopID();
    }
    if (remove != -1) {
      for (int i = remove; i < num_dirs - 1; i++) {
        strncpy(dirs[i], dirs[i + 1], PATH_MAX);
      }
      modified = 1;
      num_dirs--;
    }

    igPopStyle_BtnNeg();
  }

  /* add directory */
  {
    igPushStyle_Btn();

    if (igButton(UI_STR_LIBRARY_ADD, btn_size)) {
      page->adddir = 1;
      memset(&page->adddlg, 0, sizeof(page->adddlg));
    }

    igPopStyle_Btn();

    if (page->adddir) {
      if (ui_file_dlg(ui, &page->adddlg)) {
        if (page->adddlg.state == UI_DLG_SUCCESS) {
          strncpy(dirs[num_dirs], page->adddlg.result, PATH_MAX);
          modified = 1;
          num_dirs++;
        }

        page->adddir = 0;
      }
    }
  }

  igEndChild();

  if (modified) {
    ui_implode_gamedir(ui, dirs[0], num_dirs, PATH_MAX);
    ui_start_game_scan(ui);
  }
}

/*
 * options page
 */
static void ui_options_build(struct ui *ui) {
  struct ImGuiIO *io = igGetIO();

  struct ImVec2 btn_padding = {VW(1.5f), VW(1.5f)};
  struct ImVec2 btn_size = {VW(30.0f), VH(30.0f)};
  struct ImVec2 btn_align = {0.5f, 0.5f};

  struct ImVec2 min = {(VW(100.0f) - btn_size.x * 2.0f - btn_padding.x) / 2.0f,
                       (VH(100.0f) - btn_size.y * 2.0f - btn_padding.y) / 2.0f};

  igPushStyle_Card();
  igPushStyleVarVec(ImGuiStyleVar_ButtonTextAlign, btn_align);

  igSetCursorPosX(min.x);
  igSetCursorPosY(min.y);
  if (igButton(UI_STR_CARD_LIBRARY, btn_size)) {
    ui_set_page(ui, UI_PAGE_LIBRARY);
  }

  igSetCursorPosX(min.x + btn_size.x + btn_padding.x);
  igSetCursorPosY(min.y);
  if (igButton(UI_STR_CARD_SYSTEM, btn_size)) {
    ui_set_page(ui, UI_PAGE_SYSTEM);
  }

  igSetCursorPosX(min.x);
  igSetCursorPosY(min.y + btn_size.y + btn_padding.y);
  if (igButton(UI_STR_CARD_VIDEO, btn_size)) {
    ui_set_page(ui, UI_PAGE_VIDEO);
  }

  igSetCursorPosX(min.x + btn_size.x + btn_padding.x);
  igSetCursorPosY(min.y + btn_size.y + btn_padding.y);
  if (igButton(UI_STR_CARD_INPUT, btn_size)) {
    ui_set_page(ui, UI_PAGE_INPUT);
  }

  igPopStyleVar(1);
  igPopStyle_Card();
}

/*
 * games page
 */
enum {
  GAMES_ST_READY,
  GAMES_ST_LOADING,
  GAMES_ST_DIALOG,
  GAMES_ST_NUM,
};

enum {
  GAMES_EV_SELECTED,
  GAMES_EV_LOADED,
  GAMES_EV_CLOSED,
  GAMES_EV_NUM,
};

static texture_handle_t ui_load_disc_texture(struct ui *ui, struct game *game) {
  struct disc *disc = disc_create(game->filename, 0);
  if (!disc) {
    return ui->disc_tex;
  }

  int fad, len;
  int res = disc_find_file(disc, "0GDTEX.PVR", &fad, &len);
  if (!res) {
    return ui->disc_tex;
  }

  uint8_t *converted = malloc(1024 * 1024 * 4);
  uint8_t *pvrt = malloc(len);
  int read = disc_read_bytes(disc, fad, len, pvrt, len);
  CHECK_EQ(read, len);

  const struct pvr_tex_header *header = pvr_tex_header(pvrt);
  const uint8_t *data = pvr_tex_data(pvrt);
  pvr_tex_decode(data, header->width, header->height, header->width,
                 header->texture_fmt, header->pixel_fmt, NULL, 0, converted,
                 sizeof(converted));

  texture_handle_t tex = r_create_texture(
      ui->r, PXL_RGBA, FILTER_BILINEAR, WRAP_CLAMP_TO_EDGE, WRAP_CLAMP_TO_EDGE,
      0, header->width, header->height, converted);

  free(pvrt);
  free(converted);

  disc_destroy(disc);

  return tex;
}

static void ui_games_event(struct ui *ui, int event) {
  struct games_page *page = &ui->games_page;

  switch (page->state) {
    case GAMES_ST_READY: {
      if (event == GAMES_EV_SELECTED) {
        page->loading_start = ui->time;
        page->state = GAMES_ST_LOADING;
      } else {
        LOG_FATAL("ui_games_event unexpected event");
      }
    } break;

    case GAMES_ST_LOADING: {
      if (event == GAMES_EV_LOADED) {
        if (1) {
          page->state = GAMES_ST_READY;
          ui_set_page(ui, UI_PAGE_NONE);
        } else {
          page->state = GAMES_ST_DIALOG;
        }
      } else {
        LOG_FATAL("ui_games_event unexpected event");
      }
    } break;

    case GAMES_ST_DIALOG: {
      if (event == GAMES_EV_CLOSED) {
        page->state = GAMES_ST_READY;
      } else {
        LOG_FATAL("ui_games_event unexpected event");
      }
    } break;
  }
}

static void ui_games_build(struct ui *ui) {
  struct games_page *page = &ui->games_page;
  struct ImGuiIO *io = igGetIO();
  struct ImGuiStyle *style = igGetStyle();
  struct ImDrawList *list = igGetWindowDrawList();

  float disc_small = VH(44.4f);
  float disc_large = VH(52.1f);
  float disc_margin = VH(7.4f);
  float disc_mid = (VW(100.0f) - disc_large) / 2.0f;

  mutex_lock(ui->scan_mutex);

  /* background */
  {
    float bg_height = VH(34.7f);
    struct ImVec2 bg_min = {VW(0.0f), (VH(100.0f) - bg_height) / 2.0f};
    struct ImVec2 bg_max = {VW(100.0f), bg_min.y + bg_height};
    ImDrawList_AddRectFilled(list, bg_min, bg_max, UI_WIN_BG, 0.0f, 0);
  }

  /* scan status */
  if (ui->scanning) {
    struct ImVec2 text_size;
    igCalcTextSize(&text_size, ui->scan_status, NULL, false, 0.0f);

    struct ImVec2 padding = style->WindowPadding;
    struct ImVec2 min = {VW(0.0f), VH(100.0f) - text_size.y - padding.y * 2.0f};
    struct ImVec2 max = {text_size.x + padding.x * 2.0f, VH(100.0f)};
    struct ImVec2 text_pos = {min.x + padding.x, min.y + padding.y};

    ImDrawList_AddText(list, text_pos, UI_WIN_TEXT, ui->scan_status, NULL);
  }

  /* games */
  if (ui->num_games) {
    float list_padding = (VW(100.0f) - disc_small) / 2.0f;
    struct ImVec2 pos = {VW(0.0f), (VH(100.0f) - disc_large) / 2.0f};
    struct ImVec2 size = {VW(100.0f), disc_large};
    struct ImVec2 content_size = {ui->num_games * (disc_small + disc_margin) -
                                      disc_margin + list_padding * 2.0f,
                                  size.y};

    igSetCursorPos(pos);
    igSetNextWindowSize(size, 0);
    igSetNextWindowContentSize(content_size);

    igBeginChild("games list", size, false,
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNavScroll |
                     ImGuiWindowFlags_NavFlattened);

    struct ImVec2 disc_pos = {list_padding, (size.y - disc_small) / 2.0f};
    igSetCursorPos(disc_pos);

    for (int i = 0; i < ui->num_games; i++) {
      struct game *game = &ui->games[i];

      /* interpolate the disc size based on how far it is from the middle */
      struct ImVec2 cursor_pos;
      igGetCursorScreenPos(&cursor_pos);
      float delta_mid = disc_mid - ABS(cursor_pos.x - disc_mid);
      float delta_frac = MAX(0.0f, MIN(1.0f, delta_mid / (float)disc_large));
      float disc_size = disc_small + (disc_large - disc_small) * delta_frac;

      /* ensure disc texture is loaded */
      if (!game->tex) {
        game->tex = ui_load_disc_texture(ui, game);
      }

      igPushIDPtr(game);
      ImTextureID disc_tex = (ImTextureID)(intptr_t)game->tex;
      if (igDiscButton(disc_tex, disc_small, disc_size, img_uv[0], img_uv[1])) {
        ui_games_event(ui, GAMES_EV_SELECTED);
      }
      igPopID();

      /* scroll on focus */
      if (igIsItemFocused()) {
        /* start animation if not currently scrolling */
        if (page->curr_game == page->next_game) {
          page->scroll_start = ui->time;
        }

        page->next_game = i;

        /* update animation duration if another game is focused mid-scroll */
        page->scroll_duration =
            (int)log2(1.0f + ABS(page->next_game - page->curr_game)) * 200;
      }

      igSameLine(0.0f, disc_margin);
    }

    /* apply scroll animation */
    {
      float target = page->next_game * (disc_small + disc_margin);
      float base = page->curr_game * (disc_small + disc_margin);
      float change = target - base;
      float time = (float)(ui->time - page->scroll_start);
      float duration = (float)page->scroll_duration;
      float scroll = ease_in_linear(time, base, change, duration);

      if ((target > base && scroll >= target) ||
          (target < base && scroll <= target)) {
        page->curr_game = page->next_game;
      }

      igSetScrollX(scroll);
    }

    igEndChild();

    /* current game info */
    if (page->curr_game < ui->num_games) {
      struct game *game = &ui->games[page->curr_game];
      struct ImVec2 text_size;
      struct ImVec2 text_pos;

      igPushFontEx(IMFONT_OSWALD_MEDIUM, UI_GAME_FONT_HEIGHT);
      igCalcTextSize(&text_size, game->prodname, NULL, false, 0.0f);
      text_pos.x = (VW(100.0f) - text_size.x) / 2.0f;
      text_pos.y = (VH(100.0f) + disc_large) / 2.0f + VH(6.0f);
      ImDrawList_AddText(list, text_pos, UI_WIN_TEXT, game->prodname, NULL);
      igPopFont();

      text_pos.y += text_size.y;

      igCalcTextSize(&text_size, game->prodmeta, NULL, false, 0.0f);
      text_pos.x = (VW(100.0f) - text_size.x) / 2.0f;
      ImDrawList_AddText(list, text_pos, UI_WIN_TEXT, game->prodmeta, NULL);
    }
  }
  /* no games found */
  else {
    struct ImVec2 size = {VW(50.0f), VH(20.0f)};
    struct ImVec2 pos = {(VW(100.0f) - size.x) / 2.0f,
                         (VH(100.0f) - size.y) / 2.0f};

    igSetCursorPos(pos);
    igPushTextWrapPos(pos.x + size.x);
    igText(UI_STR_NO_GAMES);

    struct ImVec2 btn_padding = {0.0f, VH(2.0f)};
    struct ImVec2 btn_size = {0.0f, 0.0f};
    struct ImVec2 btn_pos = {pos.x + btn_padding.x,
                             igGetCursorPosY() + btn_padding.y};

    igSetCursorPos(btn_pos);
    igPushStyle_Btn();
    if (igButton(UI_STR_GO_TO_LIBRARY, btn_size)) {
      ui_set_page(ui, UI_PAGE_LIBRARY);
    }
    igPopStyle_Btn();
  }

  /* loading mask */
  if (page->state == GAMES_ST_LOADING) {
    /* use a separate child window for the loading mask due to imgui
       rendering child windows after the parent */
    struct ImVec2 pos = {0.0f, 0.0f};
    struct ImVec2 size = {VW(100.0f), VH(100.0f)};

    igSetCursorPos(pos);
    igBeginChild("loading mask", size, false, 0);

    struct ImDrawList *child_list = igGetWindowDrawList();
    float time = (float)(ui->time - page->loading_start);
    float duration = 400.0f;
    float alpha = ease_in_linear(time, 0.0f, 1.0f, duration);

    if (alpha >= 1.0f) {
      struct game *game = &ui->games[page->curr_game];
      ui_load_game(ui->host, game->filename);
      ui_games_event(ui, GAMES_EV_LOADED);
    }

    struct ImVec2 min = {0.0f, 0.0f};
    struct ImVec2 max = {VW(100.0f), VH(100.0f)};
    int col = MIN(MAX((int)(alpha * 255), 0), 255) << 24;
    ImDrawList_AddRectFilled(child_list, min, max, col, 0.0f, 0);

    igEndChild();
  }

  mutex_unlock(ui->scan_mutex);
}

/*
 * public interface
 */
void ui_set_page(struct ui *ui, int page_index) {
  struct page *next_page = NULL;
  struct page *top_page = NULL;

  if (page_index != UI_PAGE_NONE) {
    next_page = &pages[page_index];
  }

  if (ui->history_pos) {
    top_page = ui->history[ui->history_pos - 1];
  }

  /* don't push the same page */
  if (next_page == top_page) {
    return;
  }

  if (next_page) {
    ui->history[ui->history_pos] = next_page;
    ui->history_pos = (ui->history_pos + 1) % UI_MAX_HISTORY;
  } else {
    ui->history_pos = 0;
  }

  /* trigger global callbacks for when the ui is open / closed */
  if (!top_page && next_page) {
    ui_opened(ui->host);
  } else if (!next_page) {
    ui_closed(ui->host);
  }
}

void ui_build_menus(struct ui *ui) {
  struct page *top_page =
      ui->history_pos ? ui->history[ui->history_pos - 1] : NULL;

  if (!top_page) {
    return;
  }

  int64_t now = (int64_t)(time_nanoseconds() / (float)NS_PER_MS);
  ui->time = now;

  ui_begin_page(ui, top_page);
  top_page->build(ui);
  ui_end_page(ui);
}

int ui_keydown(struct ui *ui, int key, int16_t value) {
  struct input_page *page = &ui->input_page;
  struct button_map *btnmap = page->catch_btnmap;

  if (page->catch_state == UI_CATCH_DOWN) {
    if (value) {
      *btnmap->key = key;
      *btnmap->dirty = 1;

      /* swallow the corresponding up event as well */
      page->catch_state = UI_CATCH_UP;
      return 1;
    }
  } else if (page->catch_state == UI_CATCH_DOWN) {
    if (*btnmap->key == (int)key && !value) {
      page->catch_state = UI_CATCH_NONE;
      return 1;
    }
  }

  /* handle back button navigation */
  if (key == K_CONT_B && value) {
    /* prioritize canceling any open dialog */
    if (ui->dlg) {
      ui_close_dlg(ui, UI_DLG_CANCEL);
    } else if (ui->history_pos > 1) {
      ui->history_pos = ui->history_pos - 1;
    }
  }

  return 0;
}

void ui_mousemove(struct ui *ui, int x, int y) {}

void ui_vid_destroyed(struct ui *ui) {
  {
    mutex_lock(ui->scan_mutex);

    for (int i = 0; i < ui->num_games; i++) {
      struct game *game = &ui->games[i];

      if (game->tex != ui->disc_tex) {
        r_destroy_texture(ui->r, game->tex);
        game->tex = 0;
      }
    }

    mutex_unlock(ui->scan_mutex);
  }

  r_destroy_texture(ui->r, ui->clouds_tex);
  ui->clouds_tex = 0;

  r_destroy_texture(ui->r, ui->disc_tex);
  ui->disc_tex = 0;

  ui->r = NULL;
}

void ui_vid_created(struct ui *ui, struct render_backend *r) {
  ui->r = r;

  /* load background image */
  unsigned long clouds_len = clouds_width * clouds_height * 3;
  uint8_t *clouds_data = malloc(clouds_len);
  int res = uncompress(clouds_data, &clouds_len, clouds_gz, clouds_gz_len);
  CHECK_EQ(res, Z_OK);
  ui->clouds_tex = r_create_texture(ui->r, PXL_RGB, FILTER_BILINEAR,
                                    WRAP_CLAMP_TO_EDGE, WRAP_CLAMP_TO_EDGE, 0,
                                    clouds_width, clouds_height, clouds_data);
  free(clouds_data);

  /* load default disc image */
  unsigned long disc_len = disc_width * disc_height * 4;
  uint8_t *disc_data = malloc(disc_len);
  res = uncompress(disc_data, &disc_len, disc_gz, disc_gz_len);
  CHECK_EQ(res, Z_OK);
  ui->disc_tex = r_create_texture(ui->r, PXL_RGBA, FILTER_BILINEAR,
                                  WRAP_CLAMP_TO_EDGE, WRAP_CLAMP_TO_EDGE, 0,
                                  disc_width, disc_height, disc_data);
  free(disc_data);
}

void ui_destroy(struct ui *ui) {
  ui_stop_game_scan(ui);

  free(ui);
}

struct ui *ui_create(struct host *host) {
  struct ui *ui = calloc(1, sizeof(struct ui));

  ui->host = host;

  /* init pages */
  pages[UI_PAGE_GAMES].name = UI_STR_TAB_GAMES;
  pages[UI_PAGE_GAMES].build = ui_games_build;

  pages[UI_PAGE_OPTIONS].name = UI_STR_TAB_OPTIONS;
  pages[UI_PAGE_OPTIONS].build = ui_options_build;

  pages[UI_PAGE_LIBRARY].name = NULL;
  pages[UI_PAGE_LIBRARY].build = ui_library_build;

  pages[UI_PAGE_SYSTEM].name = NULL;
  pages[UI_PAGE_SYSTEM].build = ui_system_build;

  pages[UI_PAGE_VIDEO].name = NULL;
  pages[UI_PAGE_VIDEO].build = ui_video_build;

  pages[UI_PAGE_INPUT].name = NULL;
  pages[UI_PAGE_INPUT].build = ui_input_build;

  pages[UI_PAGE_CONTROLLERS].name = NULL;
  pages[UI_PAGE_CONTROLLERS].build = ui_controllers_build;

  pages[UI_PAGE_KEYBOARD].name = NULL;
  pages[UI_PAGE_KEYBOARD].build = ui_keyboard_build;

  ui_start_game_scan(ui);

  return ui;
}
