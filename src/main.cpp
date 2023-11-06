#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>

#include <stdio.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
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

#define GLSL_VERSION "#version 330"

///////////////////////////////////////////

logcat_data_t   logcat        = {};
logcat_thread_t logcat_thread = {};
device_finder_t device_finder = {};
bool            device_autoconnect;

struct details_t {
	array_t<char*> tag_exclude;
	array_t<char*> tag_include;
	array_t<char*> text_exclude;
	array_t<char*> text_include;
	int32_t selected;
	float   selected_at;
	int32_t focus_idx;
	float   focus_at;
	int32_t center_idx;
};
details_t details = {};

bool was_at_end = true;
char text_search [512] = {};
char tag_search  [512] = {};
char text_exclude[512] = {};
char tag_exclude [512] = {};
size_t text_search_len  = 0;
size_t tag_search_len   = 0;
size_t text_exclude_len = 0;
size_t tag_exclude_len  = 0;

///////////////////////////////////////////

const char *_strcasestr(const char *haystack, const char *needle);

void      step();

bool      details_is_valid    (const details_t *details, const logcat_line_t *line);
void      details_promote_tag (details_t *details, const char *tag);
void      details_demote_tag  (details_t *details, const char *tag);
void      details_promote_text(details_t *details, const char *tag);
void      details_demote_text (details_t *details, const char *tag);

void      window_log    ();
void      window_filters();
void      window_details();

void      ui_set_theme();
GLFWimage load_icon_image(int resource_id) ;

///////////////////////////////////////////

//int main() {
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR pCmdLine, int nCmdShow) {
	if (!device_finder_start(&device_finder)) {
		printf("Could not start device finder\n");
		return 1;
	}
	device_autoconnect = true;
	
	logcat_create(&logcat);

	glfwInit();
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
	glfwWindowHint(GLFW_OPENGL_PROFILE,        GLFW_OPENGL_CORE_PROFILE);

	GLFWwindow *window = glfwCreateWindow(1280, 720, "log-panther",
										  nullptr, nullptr);
	if (window == nullptr) {
		printf("Could not create GLFW window\n");
		glfwTerminate();
		return -1;
	}
	glfwMakeContextCurrent(window);
	
	GLFWimage icon = load_icon_image(101);
	glfwSetWindowIcon(window, 1, &icon);

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
		Sleep(1);
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
	item_select_label,
	item_select_text,
};

// Add a label+text combo aligned to other label+value widgets
item_select_ ui_log_item(const char* label, const char* text, bool selected) {
	ImGuiWindow* window = ImGui::GetCurrentWindow();
	if (window->SkipItems)
		return item_select_none;

	ImGuiContext& g = *GImGui;
	const ImGuiStyle& style = g.Style;
	const float w = ImGui::CalcItemWidth();

	ImVec2 label_full_size = ImGui::CalcTextSize(label, NULL, true);
	ImVec2 label_size = label_full_size;
	label_size.x = 100;
	const ImRect label_bb     (window->DC.CursorPos, window->DC.CursorPos + ImVec2(label_size.x, label_size.y + style.FramePadding.y * 2));
	const ImRect label_full_bb(window->DC.CursorPos, window->DC.CursorPos + ImVec2(fmaxf(label_full_size.x + style.FramePadding.x, label_size.x), label_full_size.y + style.FramePadding.y * 2));
	const ImRect total_bb     (window->DC.CursorPos, window->DC.CursorPos + ImVec2(ImGui::GetContentRegionAvail().x, style.FramePadding.y * 2) + label_size);
	ImGui::ItemSize(total_bb, style.FramePadding.y);
	if (!ImGui::ItemAdd(total_bb, 0))
		return item_select_none;

	bool hovered       = ImGui::IsItemHovered();
	bool label_hovered = hovered && (ImGui::GetMousePos().x < label_bb.Max.x);
	ImVec2 bmin = ImVec2(0, total_bb.Min.y);
	ImVec2 bmax = ImVec2(ImGui::GetWindowSize().x, total_bb.Max.y);
	
	ImGui::GetWindowDrawList()->AddRectFilled(ImVec2{0, label_bb.Min.y}, label_bb.Max + ImVec2{0, style.ItemSpacing.y}, IM_COL32(30, 30, 30, 255));
	if (hovered || selected) ImGui::GetWindowDrawList()->AddRect({0,total_bb.Min.y}, total_bb.Max, IM_COL32(255, 255, 255, 100));

	// Render the main text
	if (label_size.x > 0.0f) {
		if (hovered && !label_hovered)
			ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(label_bb.Max.x , label_bb.Min.y), total_bb.Max, IM_COL32(50, 50, 50, 255));
		ImGui::RenderText(ImVec2(label_bb.Max.x + style.ItemSpacing.x, label_bb.Min.y + style.FramePadding.y), text);
	}
	// Render the label
	if (label_hovered) {
		ImGui::GetWindowDrawList()->AddRectFilled(ImVec2{0, label_full_bb.Min.y}, label_full_bb.Max + ImVec2{0, 0}, IM_COL32(50, 50, 50, 255));
		ImGui::RenderTextClipped(label_full_bb.Min, label_full_bb.Max, label, NULL, NULL, ImVec2(0.0f, 0.5f));
	} else {
		ImGui::RenderTextClipped(label_bb.Min, label_bb.Max, label, nullptr, NULL, ImVec2(0.0f, 0.5f));
	}

	return ImGui::IsItemClicked()
		? (label_hovered ? item_select_label : item_select_text)
		: item_select_none;
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
	bool        filter_promote = item_select_none;
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

		if (ImGui::Button("Clear")) {
			logcat_clear(&logcat);
		}
		ImGui::SameLine();
		ImGui::Checkbox("Pause", &logcat_thread.pause);
		ImGui::SameLine();
		if (ImGui::Button("Save")) {
			// open a dialog and ask for a filename
			OPENFILENAME ofn = {};
			char filename[512] = {};
			ofn.lStructSize = sizeof(ofn);
			ofn.hwndOwner   = glfwGetWin32Window(glfwGetCurrentContext());
			ofn.lpstrFilter = "All Files\0*.*\0";
			ofn.lpstrFile   = filename;
			ofn.nMaxFile    = sizeof(filename);
			ofn.lpstrTitle  = "Save Logcat";
			ofn.Flags = OFN_DONTADDTORECENT | OFN_OVERWRITEPROMPT;
			if (GetSaveFileName(&ofn)) {
				logcat_to_file(&logcat, filename);
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Trim ^")) {
			EnterCriticalSection(&logcat.lines_section);
			for (int32_t i = 0; i < details.selected; i++) {
				free(logcat.lines[0].line);
				logcat.lines.remove(0);
			}
			details.selected  = 0;
			details.focus_idx = 0;
			details.focus_at  = 0.5f;
			LeaveCriticalSection(&logcat.lines_section);
		}
		ImGui::SameLine();
		if (ImGui::Button("Trim v")) {
			EnterCriticalSection(&logcat.lines_section);
			int32_t count = logcat.lines.count;
			for (int32_t i = details.selected+1; i < count; i++) {
				free(logcat.lines[details.selected+1].line);
				logcat.lines.remove(details.selected+1);
			}
			details.focus_idx = details.selected;
			details.focus_at  = 0.5f;
			LeaveCriticalSection(&logcat.lines_section);
		}

		ImGui::BeginChild("ScrollingRegion", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar | ImGuiWindowFlags_HorizontalScrollbar);
		// Get the bounds of the visible area
		float start      = ImGui::GetItemRectMin().y;
		float scroll_max = ImGui::GetWindowContentRegionMax().y - ImGui::GetWindowContentRegionMin().y;
		EnterCriticalSection(&logcat.lines_section);
		for (size_t i = 0; i < logcat.lines.count; i++)
		{
			logcat_line_t line  = logcat.lines[i];
			bool          valid = details_is_valid(&details, &line);

			// If this one matches focus, make sure we scroll to it.
			if (details.focus_idx == i) {
				ImGui::SetScrollHereY(details.focus_at);
				was_at_end = false;
			}

			// If it has been filtered out, we don't want to display it at all.
			if (i != details.selected && !valid) continue;

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
			if (!valid) color = ImVec4(color.x * 0.5f, color.y * 0.5f, color.z * 0.5f, color.w);

			// Draw the line
			ImGui::PushStyleColor(ImGuiCol_Text, color);
			item_select_ select = ui_log_item(logcat.tags[line.tag], line.line, i == details.selected);
			ImGui::PopStyleColor();

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
				if      (ImGui::GetIO().KeyShift && select == item_select_label) {filter_idx = i; filter_promote = true;  filter_tag = true;  filter_text = logcat.tags[line.tag]; } //details_promote_tag (&details, logcat.tags[line.tag]);
				else if (ImGui::GetIO().KeyShift && select == item_select_text ) {filter_idx = i; filter_promote = true;  filter_tag = false; filter_text = line.line;             } //details_promote_text(&details, line.line);
				else if (ImGui::GetIO().KeyCtrl  && select == item_select_label) {filter_idx = i; filter_promote = false; filter_tag = true;  filter_text = logcat.tags[line.tag]; } //details_demote_tag  (&details, logcat.tags[line.tag]);
				else if (ImGui::GetIO().KeyCtrl  && select == item_select_text ) {filter_idx = i; filter_promote = false; filter_tag = false; filter_text = line.line;             } //details_demote_text (&details, line.line);
				else details.selected = i != details.selected ? i : -1;
			}
		}
		LeaveCriticalSection(&logcat.lines_section);

		// Lock to the bottom, if the user scrolls to the bottom
		bool is_at_end = ImGui::GetScrollY() == ImGui::GetScrollMaxY();
		if (was_at_end && (!ImGui::IsItemHovered() || ImGui::GetIO().MouseWheel == 0) && details.focus_idx == -1)
			ImGui::SetScrollHereY(1.0f);
		was_at_end = is_at_end;

		ImGui::EndChild();
	}
	ImGui::End();

	details.focus_idx  = -1;
	details.center_idx = log_visible.count > 0 ? log_visible[log_visible.count / 2] : -1;

	if (filter_text != nullptr) {
		if (details.selected_at >= 0 && details.selected_at <= 1) {
			details.focus_idx = details.selected;
			details.focus_at  = details.selected_at;
		} else {
			details.focus_idx = details.center_idx;
			details.focus_at  = 0.5f;
		}
		if      (filter_promote == true  && filter_tag == true ) details_promote_tag (&details, filter_text);
		else if (filter_promote == true  && filter_tag == false) details_promote_text(&details, filter_text);
		else if (filter_promote == false && filter_tag == true ) details_demote_tag  (&details, filter_text);
		else if (filter_promote == false && filter_tag == false) details_demote_text (&details, filter_text);
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

void window_filters() {
	ImGui::Begin("Filters");

	ImGui::SeparatorText("Match Any");

	bool focus = false;
	focus = ui_string_list("Text Match", &details.text_include, text_search, sizeof(text_search), &text_search_len) || focus;
	focus = ui_string_list("Tag Match",  &details.tag_include,  tag_search,  sizeof(tag_search ), &tag_search_len ) || focus;
	
	ImGui::SeparatorText("Exclude Any");

	focus = ui_string_list("Text Exclude", &details.text_exclude, text_exclude, sizeof(text_exclude), &text_exclude_len) || focus;
	focus = ui_string_list("Tag Exclude",  &details.tag_exclude,  tag_exclude,  sizeof(tag_exclude ), &tag_exclude_len ) || focus;

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
		ImGui::LabelText("Time", "%d-%d %d:%d:%d.%d", line.month, line.day, line.hour, line.minute, line.second, line.time);
		ImGui::InputText("Tag", logcat.tags[line.tag], strlen(logcat.tags[line.tag]) + 1, ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_CallbackAlways, ui_select_all_callback);
		ImGui::InputText("Line", line.line, strlen(line.line) + 1, ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_CallbackAlways, ui_select_all_callback);
		ImGui::TextWrapped("%s", line.line);

		ImGui::Separator();

		if (ImGui::Button("Focus"))
			details.focus_idx = details.selected;
			details.focus_at  = 0.5f;
		ImGui::SameLine();
		if (ImGui::Button("Deselect"))
			details.selected = -1;
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

	// Return true if any of the includes match
	for (int32_t i = 0; i < details->tag_include.count; i++)
		if (strstr(logcat.tags[line->tag], details->tag_include[i]) != nullptr)
			return true;
	for (int32_t i = 0; i < details->text_include.count; i++)
		if (strstr(line->line, details->text_include[i]) != nullptr)
			return true;

	// If there are no includes at all, then all lines that got this far pass
	return details->tag_include.count == 0 && details->text_include.count == 0;
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