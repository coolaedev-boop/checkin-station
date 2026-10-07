// Sign On: pick your class, name and project, then Log In.
// Figma frame "Sign On Screen" (1001:3). Bars 32 -> 36 px, chips 21 -> 28 px,
// dropdowns 24 -> 28 px, Log In 28 -> 36 px; everything else at design positions.
#include "Inventory.h"
#include "Screens.h"
#include "Session.h"
#include "Theme.h"
#include <algorithm>
#include <ctype.h>

using namespace Theme;

namespace SignOnScreen {
namespace {
constexpr int32_t BAR_H = 36;
constexpr int32_t CHIP_W = 42, CHIP_H = 28;

lv_obj_t*                g_chips = nullptr;        // scrolls sideways if the roster has many classes
lv_obj_t*                g_name = nullptr;
lv_obj_t*                g_project = nullptr;
lv_obj_t*                g_logIn = nullptr;
std::vector<std::string> g_classes;                // what the chips show, in roster order
std::vector<lv_obj_t*>   g_chipButtons;
std::string              g_class;                  // picked class, empty until one is tapped

// Grey bar, flat against one screen edge and round at the other end.
void bar(lv_obj_t* scr, int32_t x, int32_t y, int32_t w, bool flatLeft) {
  const int32_t r = BAR_H / 2;
  rect(scr, flatLeft ? -r : x, y, w + r, BAR_H, BAR, r);
}

// "2A" is drawn as a big 2 and a small A, as in the design. Other names as they are.
bool gradeAndLetter(const std::string& cls) {
  if (cls.size() < 2 || !isalpha(static_cast<unsigned char>(cls.back()))) return false;
  for (size_t i = 0; i + 1 < cls.size(); ++i)
    if (!isdigit(static_cast<unsigned char>(cls[i]))) return false;
  return true;
}

std::string groupOf(const std::string& cls) {   // 2A and 2B share a pill; "Period 1" stands alone
  return gradeAndLetter(cls) ? cls.substr(0, cls.size() - 1) : cls;
}

bool projectsExist() { return !Inventory::projects().empty(); }

void updateLogIn() {
  const bool ok = !g_class.empty() && lv_dropdown_get_selected(g_name) > 0 &&
                  (!projectsExist() || lv_dropdown_get_selected(g_project) > 0);
  setEnabled(g_logIn, ok);
}

void styleChips() {
  for (size_t i = 0; i < g_chipButtons.size(); ++i) {
    const bool picked = g_classes[i] == g_class;
    lv_obj_set_style_bg_color(g_chipButtons[i], color(picked ? RED : CARD), 0);
    lv_obj_set_style_text_color(g_chipButtons[i], color(picked ? WHITE : BLACK), 0);
  }
}

void fillNames(bool keepPick) {
  const std::vector<std::string> names = g_class.empty() ? std::vector<std::string>{} : Inventory::people(g_class);
  if (keepPick) setOptionsKeepPick(g_name, "Select your name", names);
  else          lv_dropdown_set_options(g_name, optionsText("Select your name", names).c_str());
  setEnabled(g_name, !g_class.empty());
}

void onChip(lv_event_t* e) {
  const size_t i = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
  if (i >= g_classes.size()) return;
  g_class = g_classes[i];
  styleChips();
  fillNames(false);
  updateLogIn();
}

lv_obj_t* chip(lv_obj_t* group, const std::string& cls, size_t index) {
  lv_obj_t* b = lv_button_create(group);
  lv_obj_remove_style_all(b);
  lv_obj_set_size(b, gradeAndLetter(cls) ? CHIP_W : LV_SIZE_CONTENT, CHIP_H);
  lv_obj_set_style_min_width(b, CHIP_W, 0);
  lv_obj_set_style_pad_hor(b, 8, 0);
  lv_obj_set_style_radius(b, 5, 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  lv_obj_add_event_cb(b, onChip, LV_EVENT_CLICKED, reinterpret_cast<void*>(index));
  if (gradeAndLetter(cls)) {
    lv_obj_t* grade = lv_label_create(b);
    lv_label_set_text(grade, cls.substr(0, cls.size() - 1).c_str());
    lv_obj_set_style_text_font(grade, F_BIG, 0);
    lv_obj_t* letter = lv_label_create(b);
    lv_label_set_text(letter, cls.substr(cls.size() - 1).c_str());
    lv_obj_set_style_text_font(letter, F_BODY, 0);
    lv_obj_set_style_translate_y(letter, -3, 0);         // sit on the digit's baseline
  } else {
    lv_obj_t* name = lv_label_create(b);
    lv_label_set_text(name, cls.c_str());
    lv_obj_set_style_text_font(name, F_ITEM, 0);
    lv_obj_set_style_translate_y(name, -4, 0);
  }
  return b;
}

void buildChips() {
  lv_obj_clean(g_chips);
  g_chipButtons.clear();
  g_classes = Inventory::classes();
  lv_obj_t*   group = nullptr;
  std::string groupKey;
  for (size_t i = 0; i < g_classes.size(); ++i) {
    if (!group || groupOf(g_classes[i]) != groupKey) {    // dark pill behind each grade's chips
      groupKey = groupOf(g_classes[i]);
      group    = lv_obj_create(g_chips);
      lv_obj_remove_style_all(group);
      lv_obj_set_size(group, LV_SIZE_CONTENT, CHIP_H);
      lv_obj_set_style_bg_color(group, color(DROPDOWN), 0);
      lv_obj_set_style_bg_opa(group, LV_OPA_COVER, 0);
      lv_obj_set_style_radius(group, 10, 0);
      lv_obj_set_style_clip_corner(group, true, 0);       // outer chip corners follow the pill
      lv_obj_set_style_pad_column(group, 2, 0);
      lv_obj_set_flex_flow(group, LV_FLEX_FLOW_ROW);
      lv_obj_remove_flag(group, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_remove_flag(group, LV_OBJ_FLAG_CLICKABLE);
    }
    g_chipButtons.push_back(chip(group, g_classes[i], i));
  }
  if (g_classes.empty()) text(g_chips, "Add classes to the Roster tab", F_BODY, BLACK, 0, 5);
  styleChips();
}

void onNamePicked(lv_event_t*) { updateLogIn(); }

void onLogIn(lv_event_t*) {
  Session::signOn({g_class, selectedText(g_name), projectsExist() ? selectedText(g_project) : ""});
  Nav::signedOn();
}

void fillProjects(bool keepPick) {
  const std::vector<std::string> projects = Inventory::projects();
  if (projects.empty()) {                                 // no Projects tab yet: not needed to sign on
    lv_dropdown_set_options(g_project, "No projects in the Sheet");
    setEnabled(g_project, false);
    return;
  }
  setEnabled(g_project, true);
  if (keepPick) setOptionsKeepPick(g_project, "Select a project", projects);
  else          lv_dropdown_set_options(g_project, optionsText("Select a project", projects).c_str());
}
}  // namespace

void build(lv_obj_t* scr) {
  title(scr, "Sign On");

  bar(scr, 0, 92, 369, true);
  text(scr, "Class:", F_BIG, BLACK, 19, 97);
  g_chips = plain(scr, 89, 96, 369 - 89 - 4, CHIP_H);
  lv_obj_add_flag(g_chips, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(g_chips, LV_DIR_HOR);
  lv_obj_set_scrollbar_mode(g_chips, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_flex_flow(g_chips, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(g_chips, 8, 0);

  bar(scr, 111, 134, SCREEN_W - 111, false);
  text(scr, "Name:", F_BIG, BLACK, 123, 139);
  g_name = dropdown(scr, 195, 138, 266, 28);
  lv_obj_add_event_cb(g_name, onNamePicked, LV_EVENT_VALUE_CHANGED, nullptr);

  bar(scr, 0, 176, 369, true);
  text(scr, "Project:", F_BIG, BLACK, 10, 181);
  g_project = dropdown(scr, 88, 180, 266, 28);
  lv_obj_add_event_cb(g_project, onNamePicked, LV_EVENT_VALUE_CHANGED, nullptr);

  g_logIn = edgeButton(scr, "Log In", 224, 190, BIG_BUTTON_H, RED, F_BIG, onLogIn);

  buildChips();
  reset();
}

void refresh() {
  const std::vector<std::string> classes = Inventory::classes();
  if (classes != g_classes) {
    if (std::find(classes.begin(), classes.end(), g_class) == classes.end()) g_class.clear();
    buildChips();
  }
  fillNames(true);
  fillProjects(true);
  updateLogIn();
}

void reset() {
  g_class.clear();
  styleChips();
  fillNames(false);
  fillProjects(false);
  updateLogIn();
}
}  // namespace SignOnScreen
