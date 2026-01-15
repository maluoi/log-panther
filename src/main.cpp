#include <stdio.h>
#include <ctype.h>

#include <GLFW/glfw3.h>
#include <glad/glad.h>

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui.h>
#include <imgui_internal.h>

#include "../vendor/imgui_impl_glfw.h"
#include "../vendor/imgui_impl_opengl3.h"

#include "array.h"
#include "logdata.h"
#include "device_finder.h"
#include "app_finder.h"
#include "platform.h"

#define GLSL_VERSION "#version 330"

///////////////////////////////////////////

logcat_data_t   logcat        = {};
logcat_thread_t logcat_thread = {};
device_finder_t device_finder = {};
app_finder_t    app_finder    = {};
app_launcher_t  app_launcher  = {};
int32_t         app_selected  = -1;
bool            device_autoconnect;

struct details_t {
	array_t<char*>   tag_exclude;
	array_t<char*>   tag_include;
	array_t<char*>   text_exclude;
	array_t<char*>   text_include;
	array_t<uint16_t> pid_exclude;
	array_t<uint16_t> pid_include;
	int32_t selected;       // The anchor/primary selected line (shown in Selected window)
	int32_t selection_end;  // -1 = no range, otherwise the other end of selection range
	float   selected_at;
	int32_t focus_idx;
	float   focus_at;
	int32_t center_idx;
};
details_t details = {};

bool was_at_end    = true;
bool filter_mode   = true;  // true = filter (hide non-matches), false = highlight (show all, highlight matches)
bool highlight_pid = true;  // highlight lines with matching PID/TID when a line is selected
char text_search [512] = {};
char tag_search  [512] = {};
char text_exclude[512] = {};
char tag_exclude [512] = {};
char pid_search  [32]  = {};
char pid_exclude [32]  = {};
size_t text_search_len  = 0;
size_t tag_search_len   = 0;
size_t text_exclude_len = 0;
size_t tag_exclude_len  = 0;
uint16_t pid_search_live  = 0;
uint16_t pid_exclude_live = 0;

bool show_copied_tooltip = false;
bool drag_selecting = false;

///////////////////////////////////////////

const char *_strcasestr(const char *haystack, const char *needle);

void      step();

bool      details_is_valid      (const details_t *details, const logcat_line_t *line);
void      details_get_selection  (const details_t *details, int32_t *out_start, int32_t *out_end);
void      details_copy_selection (const details_t *details, const logcat_data_t *data, bool filter_active);
void      details_promote_tag   (details_t *details, const char *tag);
void      details_demote_tag    (details_t *details, const char *tag);
void      details_promote_text  (details_t *details, const char *tag);
void      details_demote_text   (details_t *details, const char *tag);

void      window_log    ();
void      window_filters();
void      window_details();

void      ui_set_theme();
#ifdef PLATFORM_WINDOWS
GLFWimage load_icon_image(int resource_id);
#endif

///////////////////////////////////////////

int main(int argc, char** argv) {
	platform_set_working_dir_to_exe();

	if (!device_finder_start(&device_finder)) {
		printf("Could not start device finder\n");
		return 1;
	}
	device_autoconnect = true;
	details.selection_end = -1;
	details.selected = -1;

	logcat_create(&logcat);

#ifdef PLATFORM_LINUX
	// On Linux, prefer X11 over Wayland for better window decoration support
	glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
#endif

	glfwInit();
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
	glfwWindowHint(GLFW_OPENGL_PROFILE,        GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);

	GLFWwindow *window = glfwCreateWindow(1280, 720, "log-panther",
										  nullptr, nullptr);
	if (window == nullptr) {
		printf("Could not create GLFW window\n");
		glfwTerminate();
		return -1;
	}
	glfwMakeContextCurrent(window);

#ifdef PLATFORM_WINDOWS
	GLFWimage icon = load_icon_image(101);
	glfwSetWindowIcon(window, 1, &icon);
#endif

	if (!gladLoadGLLoader((GLADloadproc) glfwGetProcAddress)) {
		printf("Could not initialize GLAD");
		return -1;
	}

	ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.IniFilename = "imgui.ini";
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL3_Init(GLSL_VERSION);
	
	ImFontConfig config = ImFontConfig();
	config.OversampleV = 4;
	config.OversampleH = 4;
	ImFont* font = io.Fonts->AddFontFromFileTTF("CascadiaMono.ttf", 16.0f, &config);

	ui_set_theme();

	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();

		glClearColor(0, 0, 0, 0);
		glClear(GL_COLOR_BUFFER_BIT);

		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		// Create a full-screen dockspace
		ImGuiID dock_id = ImGui::DockSpaceOverViewport(ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
		// Set up the default docking layout if it doesn't exist
		if (ImGui::DockBuilderGetNode(dock_id)->ChildNodes[0] == nullptr) {
			ImGuiID dock_main, dock_side, dock_side_top, dock_side_bot;
			ImGui::DockBuilderSplitNode(dock_id,   ImGuiDir_Left, 0.4f, &dock_side,     &dock_main);
			ImGui::DockBuilderSplitNode(dock_side, ImGuiDir_Up,   0.5f, &dock_side_top, &dock_side_bot);
			ImGui::DockBuilderDockWindow("Logcat",   dock_main);
			ImGui::DockBuilderDockWindow("Filters",  dock_side_top);
			ImGui::DockBuilderDockWindow("Selected", dock_side_bot);
			ImGui::DockBuilderFinish(dock_id);
		}

		step();

		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		platform_sleep_ms(1);
		glfwSwapBuffers(window);
	}

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	glfwDestroyWindow(window);
	glfwTerminate();

	logcat_thread_end(&logcat_thread);
	logcat_destroy   (&logcat);
	return 0;
}

///////////////////////////////////////////

enum item_select_ {
	item_select_none,
	item_select_pid,
	item_select_label,
	item_select_text,
	item_select_pid_right,
	item_select_label_right,
	item_select_text_right,
};

// Add a pid+label+text combo aligned to other label+value widgets
item_select_ ui_log_item(uint16_t pid, const char* label, const char* text, bool selected, bool highlight_related) {
	ImGuiWindow* window = ImGui::GetCurrentWindow();
	if (window->SkipItems)
		return item_select_none;

	ImGuiContext& g = *GImGui;
	const ImGuiStyle& style = g.Style;

	// PID column (fixed width)
	const float pid_width = 50;
	char pid_str[16];
	snprintf(pid_str, sizeof(pid_str), "%d", pid);

	ImVec2 label_full_size = ImGui::CalcTextSize(label, NULL, true);
	ImVec2 label_size = label_full_size;
	label_size.x = 100;

	const ImRect pid_bb       (window->DC.CursorPos, window->DC.CursorPos + ImVec2(pid_width, label_size.y + style.FramePadding.y * 2));
	const ImRect label_bb     (window->DC.CursorPos + ImVec2(pid_width, 0), window->DC.CursorPos + ImVec2(pid_width + label_size.x, label_size.y + style.FramePadding.y * 2));
	const ImRect label_full_bb(window->DC.CursorPos + ImVec2(pid_width, 0), window->DC.CursorPos + ImVec2(pid_width + fmaxf(label_full_size.x + style.FramePadding.x, label_size.x), label_full_size.y + style.FramePadding.y * 2));
	const ImRect total_bb     (window->DC.CursorPos, window->DC.CursorPos + ImVec2(ImGui::GetContentRegionAvail().x, style.FramePadding.y * 2) + ImVec2(pid_width, 0) + label_size);
	ImGui::ItemSize(total_bb, style.FramePadding.y);
	if (!ImGui::ItemAdd(total_bb, 0))
		return item_select_none;

	bool hovered       = ImGui::IsItemHovered();
	float mouse_x      = ImGui::GetMousePos().x;
	bool pid_hovered   = hovered && (mouse_x < pid_bb.Max.x);
	bool label_hovered = hovered && !pid_hovered && (mouse_x < label_bb.Max.x);

	// Background for PID and label columns
	ImGui::GetWindowDrawList()->AddRectFilled(ImVec2{0, pid_bb.Min.y}, label_bb.Max + ImVec2{0, style.ItemSpacing.y}, IM_COL32(30, 30, 30, 255));
	if (highlight_related && !selected) ImGui::GetWindowDrawList()->AddRectFilled({0,total_bb.Min.y}, total_bb.Max, IM_COL32(60, 100, 80, 100));
	if (hovered || selected) ImGui::GetWindowDrawList()->AddRect({0,total_bb.Min.y}, total_bb.Max, IM_COL32(255, 255, 255, 100));

	// Render the main text
	if (label_size.x > 0.0f) {
		if (hovered && !label_hovered && !pid_hovered)
			ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(label_bb.Max.x, label_bb.Min.y), total_bb.Max, IM_COL32(50, 50, 50, 255));
		ImGui::RenderText(ImVec2(label_bb.Max.x + style.ItemSpacing.x, label_bb.Min.y + style.FramePadding.y), text);
	}

	// Render the PID
	if (pid_hovered) {
		ImGui::GetWindowDrawList()->AddRectFilled(ImVec2{0, pid_bb.Min.y}, pid_bb.Max, IM_COL32(50, 50, 50, 255));
	}
	ImGui::RenderTextClipped(pid_bb.Min, pid_bb.Max, pid_str, nullptr, NULL, ImVec2(0.5f, 0.5f));

	// Render the label/tag
	if (label_hovered) {
		ImGui::GetWindowDrawList()->AddRectFilled(ImVec2{pid_bb.Max.x, label_full_bb.Min.y}, label_full_bb.Max, IM_COL32(50, 50, 50, 255));
		ImGui::RenderTextClipped(label_full_bb.Min, label_full_bb.Max, label, NULL, NULL, ImVec2(0.0f, 0.5f));
	} else {
		ImGui::RenderTextClipped(label_bb.Min, label_bb.Max, label, nullptr, NULL, ImVec2(0.0f, 0.5f));
	}

	bool left_clicked  = ImGui::IsItemClicked(ImGuiMouseButton_Left);
	bool right_clicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);

	if (left_clicked) {
		if (pid_hovered)   return item_select_pid;
		if (label_hovered) return item_select_label;
		return item_select_text;
	}
	if (right_clicked) {
		if (pid_hovered)   return item_select_pid_right;
		if (label_hovered) return item_select_label_right;
		return item_select_text_right;
	}
	return item_select_none;
}

///////////////////////////////////////////

static int32_t ui_select_all_callback(ImGuiInputTextCallbackData* data) {
	if (data->EventFlag == ImGuiInputTextFlags_CallbackAlways)
		data->SelectAll();
	return 0;
}

///////////////////////////////////////////

void step() {
	if (device_autoconnect && device_finder.state != device_finder_state_searching) { 
		device_autoconnect = false;
		if (device_finder.devices.count > 0) {
			logcat_thread_end  (&logcat_thread);
			logcat_thread_start(device_finder.devices[0].id, &logcat_thread, &logcat);
		}
	}
	window_filters();
	window_details();
	window_log();
}

///////////////////////////////////////////

array_t<int32_t> log_visible = {};
void window_log() {
	int32_t     filter_idx     = -1;
	const char *filter_text    = nullptr;
	uint16_t    filter_pid     = 0;
	bool        filter_promote = false;
	bool        filter_tag     = false;

	log_visible.clear();
	details.selected_at = -1;

	ImGui::Begin("Logcat");
	{
		//if (device_finder.state == device_finder_state_finished || device_finder.state == device_finder_state_searching) {
			const char *show_name = device_finder.state == device_finder_state_error ? "Error" : "Connect device...";
			char show_name_buffer[128];
			if (logcat_thread.run) {
				for (int n = 0; n < device_finder.devices.count; n++) {
					if (strcmp(logcat.src_id, device_finder.devices[n].id) == 0) {
						snprintf(show_name_buffer, sizeof(show_name_buffer), "%s (%s)", device_finder.devices[n].model, device_finder.devices[n].id);
						show_name = show_name_buffer;
						break;
					}
				}
			}
			
			ImGui::SetNextItemWidth(200);
			static bool was_open = false;
			if (ImGui::BeginCombo("##Connect to device", show_name)) {
				if (was_open == false && device_finder.state == device_finder_state_finished) {
					device_finder_start(&device_finder);
				}
				was_open = true;

				// Loop through the items array and select the current item
				for (int32_t n = 0; n < device_finder.devices.count; n++) {
					snprintf(show_name_buffer, sizeof(show_name_buffer), "%s (%s)", device_finder.devices[n].model, device_finder.devices[n].id);
					bool active = logcat_thread.run && strcmp(logcat.src_id, device_finder.devices[n].id) == 0;
					if (ImGui::Selectable(show_name_buffer, active)) {
						logcat_thread_end  (&logcat_thread);
						logcat_thread_start(device_finder.devices[n].id, &logcat_thread, &logcat);
					}
				}
				ImGui::EndCombo();
			} else {
				was_open = false;
			}
		//}// else if ()
		ImGui::SameLine();

		ImGui::Checkbox("Pause", &logcat_thread.pause);

		// App launcher UI
		ImGui::SameLine();
		ImGui::SetNextItemWidth(200);
		static bool app_combo_was_open = false;
		const char *app_show_name = app_finder.state == app_finder_state_searching ? "Loading..." :
		                            (app_selected >= 0 && app_selected < app_finder.apps.count) ? app_finder.apps[app_selected].package :
		                            "Select app...";
		if (ImGui::BeginCombo("##App", app_show_name)) {
			// Start fetching apps when dropdown opens
			if (!app_combo_was_open && logcat_thread.run) {
				app_finder_start(&app_finder, logcat.src_id);
			}
			app_combo_was_open = true;

			for (int32_t n = 0; n < app_finder.apps.count; n++) {
				bool selected = (n == app_selected);
				if (ImGui::Selectable(app_finder.apps[n].package, selected)) {
					app_selected = n;
				}
			}
			ImGui::EndCombo();
		} else {
			app_combo_was_open = false;
		}

		ImGui::SameLine();
		bool can_run = app_selected >= 0 && app_selected < app_finder.apps.count &&
		               logcat_thread.run &&
		               app_launcher.state != app_launcher_state_launching &&
		               app_launcher.state != app_launcher_state_polling_pid;
		ImGui::BeginDisabled(!can_run);
		if (ImGui::Button("Run")) {
			// Clear log first
			logcat_clear(&logcat);
			// Start the app
			app_launcher_start(&app_launcher, logcat.src_id, app_finder.apps[app_selected].package);
		}
		ImGui::EndDisabled();

		// Check if launcher finished and got a PID
		if (app_launcher.state == app_launcher_state_finished && app_launcher.pid != 0) {
			// Put PID in the search buffer (not committed)
			snprintf(pid_search, sizeof(pid_search), "%d", app_launcher.pid);
			app_launcher.pid = 0; // Consume the PID so we don't keep setting it
		}

		ImGui::SameLine();
		ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
		ImGui::SameLine();

		if (ImGui::Button("Save")) {
			char filename[512] = {};
			if (platform_file_dialog_save(filename, sizeof(filename), "Save Logcat")) {
				logcat_to_file(&logcat, filename);
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Trim ^")) {
			platform_mutex_lock(logcat.lines_mutex);
			for (int32_t i = 0; i < details.selected; i++) {
				free(logcat.lines[0].line);
				logcat.lines.remove(0);
			}
			details.selected  = 0;
			details.focus_idx = 0;
			details.focus_at  = 0.5f;
			platform_mutex_unlock(logcat.lines_mutex);
		}
		ImGui::SameLine();
		if (ImGui::Button("Trim v")) {
			platform_mutex_lock(logcat.lines_mutex);
			int32_t count = logcat.lines.count;
			for (int32_t i = details.selected+1; i < count; i++) {
				free(logcat.lines[details.selected+1].line);
				logcat.lines.remove(details.selected+1);
			}
			details.focus_idx = details.selected;
			details.focus_at  = 0.5f;
			platform_mutex_unlock(logcat.lines_mutex);
		}
		ImGui::SameLine();
		if (ImGui::Button("Clear")) {
			logcat_clear(&logcat);
		}

		ImGui::BeginChild("ScrollingRegion", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar | ImGuiWindowFlags_HorizontalScrollbar);
		// Get the bounds of the visible area
		float start      = ImGui::GetItemRectMin().y;
		float scroll_max = ImGui::GetWindowContentRegionMax().y - ImGui::GetWindowContentRegionMin().y;
		platform_mutex_lock(logcat.lines_mutex);

		// Cache selected line's PID/TID for highlighting
		uint16_t selected_pid = 0;
		uint16_t selected_tid = 0;
		bool     has_selection = details.selected >= 0 && details.selected < (int32_t)logcat.lines.count;
		if (has_selection) {
			selected_pid = logcat.lines[details.selected].pid;
			selected_tid = logcat.lines[details.selected].tid;
		}

		// Compute selection range once for the loop
		int32_t sel_start, sel_end;
		details_get_selection(&details, &sel_start, &sel_end);

		// Track hovered line for drag selection
		int32_t drag_hover_line = -1;

		for (size_t i = 0; i < logcat.lines.count; i++)
		{
			logcat_line_t line  = logcat.lines[i];
			bool          valid = details_is_valid(&details, &line);

			// If this one matches focus, make sure we scroll to it.
			if (details.focus_idx == i) {
				ImGui::SetScrollHereY(details.focus_at);
				was_at_end = false;
			}

			// In filter mode, skip items that don't match
			if (filter_mode && !valid) continue;

			// Calculate a color for the line
			ImVec4 color = {};
			switch (line.severity) {
				case 'W': color = ImVec4(1, 1, 0.5f, 1); break;
				case 'E': color = ImVec4(1, 0.5f, 0.5f, 1); break;
				case 'I': color = ImVec4(0.8f, 1, 0.8f, 1); break;
				case 'D': color = ImVec4(0.8f, 0.8f, 1, 1); break;
				case 'V': color = ImVec4(1, 1, 1, 1); break;
				default:  color = ImVec4(1, 1, 1, 1); break;
			}
			// Dim non-matching items (in filter mode when selected, or in highlight mode)
			if (!valid) color = ImVec4(color.x * 0.5f, color.y * 0.5f, color.z * 0.5f, color.w);

			// Draw the line
			bool highlight_related = highlight_pid && has_selection && (line.pid == selected_pid || line.tid == selected_tid);
			// Only visible (valid) items can be part of selection in filter mode
			bool in_selection = ((int32_t)i >= sel_start && (int32_t)i <= sel_end) && (!filter_mode || valid);
			ImGui::PushStyleColor(ImGuiCol_Text, color);
			item_select_ select = ui_log_item(line.pid, logcat.tags[line.tag], line.line, in_selection, highlight_related);
			ImGui::PopStyleColor();

			// Track hovered line for drag selection
			if (ImGui::IsItemHovered() && drag_selecting) {
				drag_hover_line = i;
			}

			// Figure out if this line is visible
			float item_y = ImGui::GetItemRectMin().y - start;
			if (item_y <= scroll_max &&
			    item_y >= 0) {
				if (i == details.selected) {
					details.selected_at = item_y / (float)scroll_max;
				}
				log_visible.add(i);
			}

			// Schedule any line interaction for later, so it doesn't interfere
			// with any focus logic for this frame.
			if (select != item_select_none) {
				bool is_left  = (select == item_select_pid || select == item_select_label || select == item_select_text);
				bool is_right = (select == item_select_pid_right || select == item_select_label_right || select == item_select_text_right);

				// Ctrl+Left Click = add to Match Any (include)
				if      (ImGui::GetIO().KeyCtrl && select == item_select_pid  ) {filter_idx = i; filter_promote = true;  filter_pid = line.pid; }
				else if (ImGui::GetIO().KeyCtrl && select == item_select_label) {filter_idx = i; filter_promote = true;  filter_tag = true;  filter_text = logcat.tags[line.tag]; }
				else if (ImGui::GetIO().KeyCtrl && select == item_select_text ) {filter_idx = i; filter_promote = true;  filter_tag = false; filter_text = line.line;             }
				// Ctrl+Right Click = add to Exclude Any
				else if (ImGui::GetIO().KeyCtrl && select == item_select_pid_right  ) {filter_idx = i; filter_promote = false; filter_pid = line.pid; }
				else if (ImGui::GetIO().KeyCtrl && select == item_select_label_right) {filter_idx = i; filter_promote = false; filter_tag = true;  filter_text = logcat.tags[line.tag]; }
				else if (ImGui::GetIO().KeyCtrl && select == item_select_text_right ) {filter_idx = i; filter_promote = false; filter_tag = false; filter_text = line.line;             }
				// Shift+Left Click = extend selection range
				else if (ImGui::GetIO().KeyShift && is_left) {
					if (details.selected < 0) {
						// No anchor yet, this becomes the anchor
						details.selected = i;
						details.selection_end = -1;
					} else {
						// Extend from anchor to clicked line
						details.selection_end = i;
					}
				}
				// Left click without modifiers = start drag selection
				else if (is_left) {
					details.selected = i;
					details.selection_end = -1;
					drag_selecting = true;
				}
				// Right click without Ctrl = copy selection to clipboard
				else if (is_right) {
					// If no selection or clicked line is outside selection, select just this line
					bool in_selection = ((int32_t)i >= sel_start && (int32_t)i <= sel_end);
					if (details.selected < 0 || !in_selection) {
						details.selected = i;
						details.selection_end = -1;
					}
					details_copy_selection(&details, &logcat, filter_mode);
					show_copied_tooltip = true;
				}
			}
		}
		platform_mutex_unlock(logcat.lines_mutex);

		// Handle drag selection
		if (drag_selecting) {
			if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
				// Update selection end while dragging
				if (drag_hover_line >= 0 && drag_hover_line != details.selected) {
					details.selection_end = drag_hover_line;
				}
			} else {
				// Mouse released - end drag
				drag_selecting = false;
			}
		}

		// Show "Copied!" tooltip while right mouse button is held after copy
		if (show_copied_tooltip) {
			if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
				ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
				ImGui::SetTooltip("Copied!");
			} else {
				show_copied_tooltip = false;
			}
		}

		// Lock to the bottom, if the user scrolls to the bottom
		float scroll_y     = ImGui::GetScrollY();
		float scroll_y_max = ImGui::GetScrollMaxY();
		bool is_near_end   = scroll_y_max <= 0 || scroll_y >= scroll_y_max - 20.0f;

		// Only auto-scroll if we were at end AND still near end (user hasn't scrolled away)
		if (was_at_end && is_near_end && details.focus_idx == -1) {
			ImGui::SetScrollHereY(1.0f);
		} else {
			// Update lock state: engage if near end, break if scrolled away
			was_at_end = is_near_end;
		}

		ImGui::EndChild();
	}
	ImGui::End();

	details.focus_idx  = -1;
	details.center_idx = log_visible.count > 0 ? log_visible[log_visible.count / 2] : -1;

	if (filter_text != nullptr || filter_pid != 0) {
		if (details.selected_at >= 0 && details.selected_at <= 1) {
			details.focus_idx = details.selected;
			details.focus_at  = details.selected_at;
		} else {
			details.focus_idx = details.center_idx;
			details.focus_at  = 0.5f;
		}
		if (filter_pid != 0) {
			if (filter_promote) details.pid_include.insert(0, filter_pid);
			else                details.pid_exclude.insert(0, filter_pid);
		} else if (filter_promote == true  && filter_tag == true ) details_promote_tag (&details, filter_text);
		else if   (filter_promote == true  && filter_tag == false) details_promote_text(&details, filter_text);
		else if   (filter_promote == false && filter_tag == true ) details_demote_tag  (&details, filter_text);
		else if   (filter_promote == false && filter_tag == false) details_demote_text (&details, filter_text);
	}
}

///////////////////////////////////////////

bool ui_string_list(const char *label, array_t<char*> *list, char *buffer, size_t buffer_size, size_t *ref_prev_len) {
	bool result = false;
	ImGui::PushID(ImGui::GetID(label));

	ImGui::PushItemFlag(ImGuiItemFlags_Disabled, buffer[0] == '\0');
	if (ImGui::Button("+")) {
		list->insert(0, strdup(buffer));
		list->remove(list->count - 1);
		buffer[0] = '\0';
	}
	ImGui::PopItemFlag();

	ImGui::SameLine();
	float clearButtonWidth = ImGui::CalcTextSize("x").x + ImGui::GetStyle().FramePadding.x * 3.0f;
	ImGui::PushItemWidth(-clearButtonWidth);
	if (ImGui::InputTextWithHint("##input", label, buffer, buffer_size, ImGuiInputTextFlags_EscapeClearsAll | ImGuiInputTextFlags_EnterReturnsTrue)) {
		list->insert(0, strdup(buffer));
		list->remove(list->count - 1);
		buffer[0] = '\0';
	}
	size_t len = strlen(buffer);
	if (len != *ref_prev_len) {
		*ref_prev_len = len;
		if (list->count == 0 || list->last() != buffer)
			list->add(buffer);
		if (list->last() == buffer && buffer[0] == '\0')
			list->remove(list->count - 1);
		result = true;
	}
	ImGui::PopItemWidth();
	ImGui::SameLine();
	if (ImGui::Button("x")) {
		buffer[0] = '\0';
	}

	ImGui::Indent(10);
	for (int32_t i = 0; i < list->count; i++) {
		if (list->get(i) == buffer) continue;
		ImGui::PushID(i);
		if (ImGui::Button("-")) {
			list->remove(i);
			i--;
			result = true;
			ImGui::PopID();
			continue;
		}
		ImGui::SameLine();
		ImGui::Text("%s", list->get(i));
		ImGui::PopID();
	}
	ImGui::Indent(-10);

	ImGui::PopID();
	return result;
}

///////////////////////////////////////////

bool ui_pid_list(const char *label, array_t<uint16_t> *list, char *buffer, size_t buffer_size, uint16_t *ref_live_pid) {
	bool result = false;
	ImGui::PushID(ImGui::GetID(label));

	// Parse the current buffer value
	int parsed_pid = 0;
	bool valid_input = buffer[0] != '\0' && sscanf(buffer, "%d", &parsed_pid) == 1 && parsed_pid > 0 && parsed_pid <= 65535;
	uint16_t current_pid = valid_input ? (uint16_t)parsed_pid : 0;

	// Live filtering: update the temporary PID when buffer changes
	if (current_pid != *ref_live_pid) {
		// Remove old live PID from list if it was there
		if (*ref_live_pid != 0) {
			for (int32_t i = 0; i < list->count; i++) {
				if (list->get(i) == *ref_live_pid) {
					list->remove(i);
					break;
				}
			}
		}
		// Add new live PID to list
		if (current_pid != 0) {
			list->add(current_pid);
		}
		*ref_live_pid = current_pid;
		result = true;
	}

	ImGui::PushItemFlag(ImGuiItemFlags_Disabled, !valid_input);
	if (ImGui::Button("+") && valid_input) {
		// Remove the live entry first (it's at the end)
		if (*ref_live_pid != 0) {
			for (int32_t i = 0; i < list->count; i++) {
				if (list->get(i) == *ref_live_pid) {
					list->remove(i);
					break;
				}
			}
			*ref_live_pid = 0;
		}
		// Insert as committed entry at the front
		list->insert(0, (uint16_t)parsed_pid);
		buffer[0] = '\0';
		result = true;
	}
	ImGui::PopItemFlag();

	ImGui::SameLine();
	float clearButtonWidth = ImGui::CalcTextSize("x").x + ImGui::GetStyle().FramePadding.x * 3.0f;
	ImGui::PushItemWidth(-clearButtonWidth);
	if (ImGui::InputTextWithHint("##input", label, buffer, buffer_size, ImGuiInputTextFlags_EscapeClearsAll | ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsDecimal)) {
		if (valid_input) {
			// Remove the live entry first
			if (*ref_live_pid != 0) {
				for (int32_t i = 0; i < list->count; i++) {
					if (list->get(i) == *ref_live_pid) {
						list->remove(i);
						break;
					}
				}
				*ref_live_pid = 0;
			}
			// Insert as committed entry at the front
			list->insert(0, (uint16_t)parsed_pid);
			buffer[0] = '\0';
			result = true;
		}
	}
	ImGui::PopItemWidth();
	ImGui::SameLine();
	if (ImGui::Button("x")) {
		buffer[0] = '\0';
	}

	ImGui::Indent(10);
	for (int32_t i = 0; i < list->count; i++) {
		// Skip the live entry in the UI (it's shown in the input field)
		if (list->get(i) == *ref_live_pid) continue;
		ImGui::PushID(i);
		if (ImGui::Button("-")) {
			list->remove(i);
			i--;
			result = true;
			ImGui::PopID();
			continue;
		}
		ImGui::SameLine();
		ImGui::Text("%d", list->get(i));
		ImGui::PopID();
	}
	ImGui::Indent(-10);

	ImGui::PopID();
	return result;
}

///////////////////////////////////////////

void window_filters() {
	ImGui::Begin("Filters");

	ImGui::SeparatorText("Match Any");

	bool focus = false;
	focus = ui_string_list("Text Match", &details.text_include, text_search, sizeof(text_search), &text_search_len) || focus;
	focus = ui_string_list("Tag Match",  &details.tag_include,  tag_search,  sizeof(tag_search ), &tag_search_len ) || focus;
	focus = ui_pid_list   ("PID Match",  &details.pid_include,  pid_search,  sizeof(pid_search ), &pid_search_live ) || focus;

	ImGui::SeparatorText("Exclude Any");

	focus = ui_string_list("Text Exclude", &details.text_exclude, text_exclude, sizeof(text_exclude), &text_exclude_len) || focus;
	focus = ui_string_list("Tag Exclude",  &details.tag_exclude,  tag_exclude,  sizeof(tag_exclude ), &tag_exclude_len ) || focus;
	focus = ui_pid_list   ("PID Exclude",  &details.pid_exclude,  pid_exclude,  sizeof(pid_exclude ), &pid_exclude_live) || focus;

	ImGui::SeparatorText("Mode");

	static bool prev_filter_mode = filter_mode;
	if (ImGui::RadioButton("Filter", filter_mode)) filter_mode = true;
	ImGui::SameLine();
	if (ImGui::RadioButton("Highlight", !filter_mode)) filter_mode = false;
	if (prev_filter_mode != filter_mode) {
		prev_filter_mode = filter_mode;
		focus = true;
	}

	ImGui::Separator();

	if (ImGui::Button("Reset All")) {
		for (int32_t i = 0; i < details.tag_exclude.count; i++)
			if (details.tag_exclude[i] != tag_exclude)
				free(details.tag_exclude[i]);
		for (int32_t i = 0; i < details.tag_include.count; i++)
			if (details.tag_include[i] != tag_search)
				free(details.tag_include[i]);
		for (int32_t i = 0; i < details.text_exclude.count; i++)
			if (details.text_exclude[i] != text_exclude)
				free(details.text_exclude[i]);
		for (int32_t i = 0; i < details.text_include.count; i++)
			if (details.text_include[i] != text_search)
				free(details.text_include[i]);
		details.text_include.clear();
		details.tag_include .clear();
		details.text_exclude.clear();
		details.tag_exclude .clear();
		details.pid_include .clear();
		details.pid_exclude .clear();
		pid_search_live  = 0;
		pid_exclude_live = 0;
		focus = true;
	}

	ImGui::End();

	if (focus) {
		if (details.selected_at >= 0 && details.selected_at <= 1) {
			details.focus_idx = details.selected;
			details.focus_at  = details.selected_at;
		} else {
			details.focus_idx = details.center_idx;
			details.focus_at  = 0.5f; 
		}
	}
}

///////////////////////////////////////////

void window_details() {
	ImGui::Begin("Selected");

	if (details.selected >= 0 && details.selected < logcat.lines.count ) {
		logcat_line_t line = logcat.lines[details.selected];

		const char *severity = "";
		switch (line.severity)
		{
			case 'W': severity = "WARNING"; break;
			case 'E': severity = "ERROR";   break;
			case 'I': severity = "Info";    break;
			case 'D': severity = "Debug";   break;
			case 'V': severity = "Verbose"; break;
			case 'S': severity = "Silent";  break;
			case 'F': severity = "Fatal";   break;
			default:  severity = ""; break;
		}
		
		ImGui::LabelText("Process ID", "%d", line.pid);
		ImGui::LabelText("Thread ID", "%d", line.tid);
		ImGui::LabelText("Severity", "%s", severity);
		ImGui::LabelText("Time", "%d-%d %d:%d:%d.%d", line.month, line.day, line.hour, line.minute, line.second, line.millisecond);
		ImGui::InputText("Tag", logcat.tags[line.tag], strlen(logcat.tags[line.tag]) + 1, ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_CallbackAlways, ui_select_all_callback);
		ImGui::TextWrapped("%s", line.line);

		ImGui::Separator();

		if (ImGui::Button("Focus")) {
			details.focus_idx = details.selected;
			details.focus_at  = 0.5f;
		}
		ImGui::SameLine();
		if (ImGui::Button("Deselect"))
			details.selected = -1;
		ImGui::SameLine();
		ImGui::Checkbox("Highlight PID", &highlight_pid);
	} else {
		ImGui::Text("No line selected");
	}

	ImGui::End();
}

///////////////////////////////////////////

bool details_is_valid(const details_t *details, const logcat_line_t *line) {
	// Return false if any of the excludes match
	for (int32_t i = 0; i < details->tag_exclude.count; i++)
		if (strstr(logcat.tags[line->tag], details->tag_exclude[i]) != nullptr)
			return false;
	for (int32_t i = 0; i < details->text_exclude.count; i++)
		if (strstr(line->line, details->text_exclude[i]) != nullptr)
			return false;
	for (int32_t i = 0; i < details->pid_exclude.count; i++)
		if (line->pid == details->pid_exclude[i])
			return false;

	// Return true if any of the includes match
	for (int32_t i = 0; i < details->tag_include.count; i++)
		if (strstr(logcat.tags[line->tag], details->tag_include[i]) != nullptr)
			return true;
	for (int32_t i = 0; i < details->text_include.count; i++)
		if (strstr(line->line, details->text_include[i]) != nullptr)
			return true;
	for (int32_t i = 0; i < details->pid_include.count; i++)
		if (line->pid == details->pid_include[i])
			return true;

	// If there are no includes at all, then all lines that got this far pass
	return details->tag_include.count == 0 && details->text_include.count == 0 && details->pid_include.count == 0;
}

///////////////////////////////////////////

void details_get_selection(const details_t *details, int32_t *out_start, int32_t *out_end) {
	if (details->selection_end < 0 || details->selected < 0) {
		// No range selection, just the current line
		*out_start = details->selected;
		*out_end = details->selected;
	} else {
		*out_start = details->selection_end < details->selected ? details->selection_end : details->selected;
		*out_end = details->selection_end > details->selected ? details->selection_end : details->selected;
	}
}

void details_copy_selection(const details_t *details, const logcat_data_t *data, bool filter_active) {
	int32_t start, end;
	details_get_selection(details, &start, &end);

	if (start < 0 || end < 0 || start >= data->lines.count) return;
	if (end >= data->lines.count) end = data->lines.count - 1;

	// Calculate required buffer size (only for valid lines when filtering)
	size_t total_size = 0;
	for (int32_t i = start; i <= end; i++) {
		const logcat_line_t &line = data->lines[i];
		// Skip filtered items
		if (filter_active && !details_is_valid(details, &line)) continue;
		// Format: "PID  TID S TAG: TEXT\n"
		// Approximate max: 30 + tag_len + line_len
		total_size += 32 + strlen(data->tags[line.tag]) + strlen(line.line);
	}
	if (total_size == 0) return; // Nothing to copy
	total_size += 1; // null terminator

	char *buffer = (char*)malloc(total_size);
	if (!buffer) return;

	char *ptr = buffer;
	for (int32_t i = start; i <= end; i++) {
		const logcat_line_t &line = data->lines[i];
		// Skip filtered items
		if (filter_active && !details_is_valid(details, &line)) continue;
		int written;
		if (line.severity == 0) {
			written = sprintf(ptr, "%s\n", line.line);
		} else {
			written = sprintf(ptr, "%d %d %c %s: %s\n",
				line.pid, line.tid, line.severity, data->tags[line.tag], line.line);
		}
		ptr += written;
	}

	ImGui::SetClipboardText(buffer);
	free(buffer);
}

///////////////////////////////////////////

void details_promote_generic(array_t<char*> *lower, array_t<char*> *higher, const char *tag, char *avoid_buffer) {
	for (int32_t i = 0; i < lower->count; i++) {
		if (strcmp(lower->get(i), tag) == 0) {
			if (lower->get(i) != avoid_buffer)
				free(lower->get(i));
			lower->remove(i);
			return;
		}
	}
	higher->insert(0, strdup(tag));
}

///////////////////////////////////////////

void details_promote_tag(details_t *details, const char *tag) {
	details_promote_generic(&details->tag_exclude, &details->tag_include, tag, tag_exclude);
}

///////////////////////////////////////////

void details_demote_tag(details_t *details, const char *tag) {
	details_promote_generic(&details->tag_include, &details->tag_exclude, tag, tag_search);
}

///////////////////////////////////////////

void details_promote_text(details_t *details, const char *text) {
	details_promote_generic(&details->text_exclude, &details->text_include, text, text_exclude);
}

///////////////////////////////////////////

void details_demote_text(details_t *details, const char *text) {
	details_promote_generic(&details->text_include, &details->text_exclude, text, text_search);
}

///////////////////////////////////////////

const char *_strcasestr(const char *haystack, const char *needle) {
	// If needle is an empty string, return the original string
	if (!*needle) return (char *)haystack;

	// Iterate over each character in the haystack
	for (; *haystack; haystack++) {
		// If the first character (case-insensitive) matches,
		// then check the rest of the substring
		if (tolower((unsigned char)*haystack) == tolower((unsigned char)*needle)) {
			const char *h, *n;

			// Compare the remaining characters
			for (h = haystack, n = needle; *h && *n; h++, n++) {
				if (tolower((unsigned char)*h) != tolower((unsigned char)*n)) {
					break;
				}
			}

			// If we reached the end of the needle string,
			// then we've found a match
			if (!*n) {
				return (char *)haystack;
			}
		}
	}

	return NULL;
}

///////////////////////////////////////////

void ui_set_theme() {
	ImGui::GetStyle().FrameRounding = 8;
	ImGui::GetStyle().FramePadding  = {8, 4};
	ImGui::GetStyle().ItemSpacing   = {8, 6};
	ImGui::GetStyle().DockingSeparatorSize    = 4;
	ImGui::GetStyle().SeparatorTextBorderSize = 4;
	ImVec4* colors = ImGui::GetStyle().Colors;
	colors[ImGuiCol_FrameBg]                = ImVec4(0.05f, 0.19f, 0.11f, 0.54f);
	colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.24f, 0.86f, 0.52f, 0.40f);
	colors[ImGuiCol_FrameBgActive]          = ImVec4(0.24f, 0.86f, 0.52f, 0.67f);
	colors[ImGuiCol_TitleBg]                = ImVec4(0.02f, 0.07f, 0.04f, 1.00f);
	colors[ImGuiCol_TitleBgActive]          = ImVec4(0.05f, 0.19f, 0.11f, 1.00f);
	colors[ImGuiCol_CheckMark]              = ImVec4(0.24f, 0.86f, 0.52f, 1.00f);
	colors[ImGuiCol_SliderGrab]             = ImVec4(0.24f, 0.86f, 0.52f, 1.00f);
	colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.24f, 0.86f, 0.52f, 1.00f);
	colors[ImGuiCol_Button]                 = ImVec4(0.24f, 0.86f, 0.52f, 0.40f);
	colors[ImGuiCol_ButtonHovered]          = ImVec4(0.24f, 0.86f, 0.52f, 1.00f);
	colors[ImGuiCol_ButtonActive]           = ImVec4(0.24f, 0.86f, 0.52f, 1.00f);
	colors[ImGuiCol_Header]                 = ImVec4(0.24f, 0.86f, 0.52f, 0.31f);
	colors[ImGuiCol_HeaderHovered]          = ImVec4(0.24f, 0.86f, 0.52f, 0.80f);
	colors[ImGuiCol_HeaderActive]           = ImVec4(0.24f, 0.86f, 0.52f, 1.00f);
	colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.15f, 0.54f, 0.32f, 0.78f);
	colors[ImGuiCol_SeparatorActive]        = ImVec4(0.15f, 0.54f, 0.32f, 1.00f);
	colors[ImGuiCol_ResizeGrip]             = ImVec4(0.24f, 0.86f, 0.52f, 0.20f);
	colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.24f, 0.86f, 0.52f, 0.67f);
	colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.24f, 0.86f, 0.52f, 0.95f);
	colors[ImGuiCol_Tab]                    = ImVec4(0.15f, 0.54f, 0.32f, 0.86f);
	colors[ImGuiCol_TabHovered]             = ImVec4(0.24f, 0.86f, 0.52f, 0.80f);
	colors[ImGuiCol_TabActive]              = ImVec4(0.15f, 0.54f, 0.32f, 1.00f);
	colors[ImGuiCol_TabUnfocused]           = ImVec4(0.05f, 0.19f, 0.12f, 0.97f);
	colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.05f, 0.19f, 0.11f, 1.00f);
	colors[ImGuiCol_DockingPreview]         = ImVec4(0.24f, 0.86f, 0.52f, 0.70f);
	colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.24f, 0.86f, 0.52f, 0.35f);
	colors[ImGuiCol_NavHighlight]           = ImVec4(0.24f, 0.86f, 0.52f, 1.00f);
}

///////////////////////////////////////////

#ifdef PLATFORM_WINDOWS

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

GLFWimage load_icon_image(int resource_id) {
	HICON hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(resource_id));
	if (!hIcon) return {};

	ICONINFO iconInfo;
	GetIconInfo(hIcon, &iconInfo);
	BITMAP bm;
	GetObject(iconInfo.hbmColor, sizeof(BITMAP), &bm);

	BITMAPINFO bmi = {0};
	bmi.bmiHeader.biSize     = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth    = bm.bmWidth;
	bmi.bmiHeader.biHeight   = -bm.bmHeight; // top-down
	bmi.bmiHeader.biPlanes   = 1;
	bmi.bmiHeader.biBitCount = 32;

	GLFWimage image;
	image.width  = bm.bmWidth;
	image.height = bm.bmHeight;
	image.pixels = (unsigned char *)malloc(bm.bmWidth * bm.bmHeight * 4);

	HDC hdc = GetDC(NULL);
	GetDIBits(hdc, iconInfo.hbmColor, 0, bm.bmHeight, image.pixels, &bmi, DIB_RGB_COLORS);
	ReleaseDC(NULL, hdc);

	DeleteObject(iconInfo.hbmMask);
	DeleteObject(iconInfo.hbmColor);

	for (size_t i = 0; i < bm.bmWidth * bm.bmHeight; i++) {
		uint8_t t = image.pixels[i * 4 + 0];
		image.pixels[i * 4 + 0] = image.pixels[i * 4 + 2];
		image.pixels[i * 4 + 2] = t;
	}

	return image;
}

#endif // PLATFORM_WINDOWS