#include "engine/debug/debug_tools.h"

#include "engine/app.h"
#include "engine/assets/assets.h"
#include "engine/core/math/math.h"
#include "engine/physics/physics.h"
#include "engine/world/components.h"

#include <imgui.h>
#include <imgui_stdlib.h> // InputText for std::string

#include <algorithm>
#include <cstdio>
#include <string>

namespace eng {

namespace {

constexpr float kRadToDeg = 57.2957795f;

// "player #3" for a named entity, "#3" otherwise. The number is the entity's
// slot, which is enough to tell apart entities with the same name.
std::string label_of(World& world, Entity e) {
    const Name* name = world.get<Name>(e);
    std::string label = name ? name->value + " " : std::string();
    return label + "#" + std::to_string(e.index);
}

// Where to put a window on first use, relative to the area below the menu bar.
void place_window(float x, float y, float width, float height) {
    const ImGuiViewport* view = ImGui::GetMainViewport();
    float left = x >= 0.0f ? view->WorkPos.x + x : view->WorkPos.x + view->WorkSize.x + x - width;
    float top = y >= 0.0f ? view->WorkPos.y + y : view->WorkPos.y + view->WorkSize.y + y - height;
    ImGui::SetNextWindowPos({left, top}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({width, height}, ImGuiCond_FirstUseEver);
}

ImU32 color_of(LogLevel level) {
    switch (level) {
    case LogLevel::Warn: return IM_COL32(255, 200, 90, 255);
    case LogLevel::Error: return IM_COL32(255, 110, 110, 255);
    case LogLevel::Command: return IM_COL32(140, 200, 255, 255);
    case LogLevel::Result: return IM_COL32(170, 255, 170, 255);
    case LogLevel::Info: break;
    }
    return IM_COL32(220, 220, 220, 255);
}

} // namespace

// --- The engine's own components ------------------------------------------------------

DebugTools::DebugTools() {
    inspector_.add<Name>("Name", [](Name& n) { ImGui::InputText("##name", &n.value); });

    inspector_.add<Transform>("Transform", [](Transform& t) {
        ImGui::DragFloat3("Position", &t.position.x, 0.05f);
        // Rotations are quaternions (see quat.h), which nobody can read, so
        // show and edit them as angles in degrees and convert back.
        Vec3 degrees = euler_from_quat(t.rotation) * kRadToDeg;
        if (ImGui::DragFloat3("Rotation", &degrees.x, 0.5f)) t.rotation = quat_from_euler(degrees / kRadToDeg);
        ImGui::DragFloat3("Scale", &t.scale.x, 0.02f);
    });

    inspector_.add<MeshRenderer>("Mesh renderer", [this](MeshRenderer& m) {
        ImGui::ColorEdit4("Tint", &m.tint.x);
        // Which model to draw: pick from every loaded one. This only swaps
        // the handle; the assets themselves are untouched.
        if (!app_assets_) return;
        const auto* current = app_assets_->models().get(m.model);
        if (ImGui::BeginCombo("Model", current ? current->name.c_str() : "(none)")) {
            app_assets_->models().each([&](Handle<Model> h, const AssetTable<Model>::Entry& e) {
                if (ImGui::Selectable(e.name.c_str(), h == m.model)) m.model = h;
            });
            ImGui::EndCombo();
        }
    });

    inspector_.add<Collider>("Collider", [](Collider& c) {
        int shape = c.shape == Collider::Shape::Box ? 0 : 1;
        if (ImGui::Combo("Shape", &shape, "Box\0Sphere\0")) c.shape = shape == 0 ? Collider::Shape::Box : Collider::Shape::Sphere;
        if (c.shape == Collider::Shape::Box)
            ImGui::DragFloat3("Half extents", &c.half_extents.x, 0.01f, 0.0f, 100.0f);
        else
            ImGui::DragFloat("Radius", &c.radius, 0.01f, 0.0f, 100.0f);
        ImGui::DragFloat3("Offset", &c.offset.x, 0.01f);
        ImGui::Checkbox("Trigger", &c.is_trigger);
    });

    inspector_.add<Body>("Body", [](Body& b) {
        ImGui::DragFloat3("Velocity", &b.velocity.x, 0.1f);
        ImGui::DragFloat("Gravity scale", &b.gravity_scale, 0.05f);
        ImGui::BeginDisabled(); // set by physics each step; shown, not edited
        ImGui::Checkbox("On ground", &b.on_ground);
        ImGui::EndDisabled();
    });
}

// --- Windows ------------------------------------------------------------------------------

void DebugTools::draw(App& app) {
    app_assets_ = &app.assets();
    draw_menu(app);
    if (show_frame_) draw_frame_window(app);
    if (show_entities_) draw_entities_window();
    if (show_assets_) draw_assets_window(app);
    if (show_console_) draw_console_window(app);
}

void DebugTools::draw_menu(App& app) {
    if (!ImGui::BeginMainMenuBar()) return;
    if (ImGui::BeginMenu("Windows")) {
        ImGui::MenuItem("Frame", nullptr, &show_frame_);
        ImGui::MenuItem("Entities", nullptr, &show_entities_);
        ImGui::MenuItem("Assets", nullptr, &show_assets_);
        ImGui::MenuItem("Console", nullptr, &show_console_);
        ImGui::EndMenu();
    }
    bool paused = app.paused();
    if (ImGui::MenuItem(paused ? "Resume" : "Pause")) app.set_paused(!paused);
    if (paused && ImGui::MenuItem("Step")) app.step_once();

    const FrameStats& s = app.stats();
    ImGui::Separator();
    ImGui::TextDisabled("%.0f fps  %.2f ms%s   F1 hides these tools", s.fps, s.frame_ms, paused ? "  (paused)" : "");
    ImGui::EndMainMenuBar();
}

void DebugTools::draw_frame_window(App& app) {
    place_window(-10.0f, 10.0f, 340.0f, 330.0f);
    if (!ImGui::Begin("Frame", &show_frame_)) return ImGui::End();

    const FrameStats& s = app.stats();
    ImGui::Text("%.1f fps (%.2f ms)  %s", s.fps, s.frame_ms, app.renderer().backend_name());

    // The frame-time graph. Smooth play needs every frame inside the budget
    // (16.7 ms for 60 fps); spikes above the line are the stutters you feel.
    const float budget_ms = 1000.0f / 60.0f;
    float top = budget_ms * 2.0f;
    for (const FrameTimings& t : s.history) top = std::max(top, t.total);
    char overlay[48];
    std::snprintf(overlay, sizeof overlay, "frame time, last %d frames", FrameStats::kHistory);
    ImGui::PlotLines("##frames", &s.history[0].total, FrameStats::kHistory, s.next, overlay, 0.0f, top,
                     {-1.0f, 80.0f}, sizeof(FrameTimings));
    // Draw the budget as a line across the graph.
    ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
    float y = max.y - (max.y - min.y) * (budget_ms / top);
    ImGui::GetWindowDrawList()->AddLine({min.x, y}, {max.x, y}, IM_COL32(255, 200, 90, 160));
    ImGui::TextDisabled("Orange line: the %.1f ms budget for 60 fps", budget_ms);

    // Where the time went, averaged over the graph's frames.
    if (ImGui::BeginTable("phases", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Part of the frame");
        ImGui::TableSetupColumn("Average ms");
        ImGui::TableSetupColumn("Worst ms");
        ImGui::TableHeadersRow();
        auto row = [&](const char* name, float FrameTimings::*field) {
            float sum = 0.0f, worst = 0.0f;
            for (const FrameTimings& t : s.history) {
                sum += t.*field;
                worst = std::max(worst, t.*field);
            }
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(name);
            ImGui::TableNextColumn();
            ImGui::Text("%.2f", sum / FrameStats::kHistory);
            ImGui::TableNextColumn();
            ImGui::Text("%.2f", worst);
        };
        row("Events", &FrameTimings::events);
        row("Hot reload", &FrameTimings::assets);
        row("Simulation", &FrameTimings::simulation);
        row("Debug UI", &FrameTimings::ui);
        row("Render + vsync", &FrameTimings::render);
        row("Whole frame", &FrameTimings::total);
        ImGui::EndTable();
    }

    ImGui::Text("Steps this frame: %d   total: %llu", s.steps_last_frame,
                static_cast<unsigned long long>(s.total_steps));
    ImGui::Text("Draw calls: %zu   meshes on GPU: %zu", app.renderer().draws_last_frame(),
                app.renderer().mesh_count());
    if (world_) ImGui::Text("Entities: %zu", world_->entity_count());
    ImGui::End();
}

void DebugTools::draw_entities_window() {
    place_window(10.0f, 150.0f, 330.0f, 320.0f);
    if (!ImGui::Begin("Entities", &show_entities_)) return ImGui::End();
    if (!world_) {
        ImGui::TextDisabled("This game has no world to show.");
        return ImGui::End();
    }
    World& world = *world_;

    // The list, filtered by name.
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##filter", "Filter by name", &entity_filter_);
    ImGui::BeginChild("list", {0.0f, ImGui::GetTextLineHeightWithSpacing() * 6.0f}, ImGuiChildFlags_Borders);
    world.each_entity([&](Entity e) {
        std::string label = label_of(world, e);
        if (!entity_filter_.empty() && label.find(entity_filter_) == std::string::npos) return;
        ImGui::PushID(static_cast<int>(e.index));
        if (ImGui::Selectable(label.c_str(), e == selected_)) selected_ = e;
        ImGui::PopID();
    });
    ImGui::EndChild();

    // The selected entity's components. It may have been destroyed since it
    // was picked: the generation in its id tells us.
    if (!world.alive(selected_)) {
        ImGui::TextDisabled("Pick an entity to inspect it.");
        return ImGui::End();
    }
    ImGui::SeparatorText(label_of(world, selected_).c_str());
    inspector_.draw(world, selected_);
    ImGui::Spacing();
    if (ImGui::Button("Destroy")) world.destroy(selected_);
    ImGui::End();
}

void DebugTools::draw_assets_window(App& app) {
    place_window(-10.0f, -10.0f, 600.0f, 260.0f);
    if (!ImGui::Begin("Assets", &show_assets_)) return ImGui::End();
    Assets& assets = app.assets();

    bool hot = assets.hot_reload();
    if (ImGui::Checkbox("Hot reload", &hot)) assets.set_hot_reload(hot);
    ImGui::SameLine();
    ImGui::TextDisabled("watching %zu files; save one to reload it", assets.watched_files());

    // One table per kind of asset. `extra` adds that kind's own column.
    auto table = [&](const char* id, auto& entries, const char* extra_name, auto extra) {
        const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit;
        if (!ImGui::BeginTable(id, 6, flags)) return;
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("File", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Refs");
        ImGui::TableSetupColumn(extra_name);
        ImGui::TableSetupColumn("Reloads");
        ImGui::TableSetupColumn("");
        ImGui::TableHeadersRow();
        entries.each([&](auto handle, auto& e) {
            ImGui::PushID(static_cast<int>(handle.index));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(e.name.c_str());
            ImGui::TableNextColumn();
            if (e.path.empty()) {
                ImGui::TextDisabled("(built in code)");
            } else {
                ImGui::TextUnformatted(e.path.filename().string().c_str());
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", e.path.string().c_str());
            }
            if (!e.error.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 110, 110, 255));
                ImGui::TextWrapped("%s", e.error.c_str());
                ImGui::PopStyleColor();
            }
            ImGui::TableNextColumn();
            ImGui::Text("%u", e.refs);
            ImGui::TableNextColumn();
            extra(e);
            ImGui::TableNextColumn();
            ImGui::Text("%d", e.reloads);
            ImGui::TableNextColumn();
            if (!e.path.empty() && ImGui::SmallButton("Reload")) assets.reload(handle);
            ImGui::PopID();
        });
        ImGui::EndTable();
    };

    ImGui::SeparatorText("Models");
    table("models", assets.models(), "Triangles",
          [](const AssetTable<Model>::Entry& e) { ImGui::Text("%zu", e.value.triangles); });
    ImGui::SeparatorText("Sounds");
    table("sounds", assets.sounds(), "Loaded",
          [](const AssetTable<Sound>::Entry& e) { ImGui::TextUnformatted(e.value.sound.valid() ? "yes" : "no"); });
    ImGui::End();
}

void DebugTools::draw_console_window(App& app) {
    place_window(10.0f, -10.0f, 640.0f, 210.0f);
    if (!ImGui::Begin("Console", &show_console_)) return ImGui::End();

    LogHistory& log = app.log();
    if (ImGui::SmallButton("Clear")) log.clear();
    ImGui::SameLine();
    ImGui::TextDisabled(run_console_ ? "Type Lua below: world.count(), physics.set_gravity(vec3(0, -5, 0)) ..."
                                     : "Engine log");

    // The log, scrolled to the bottom whenever a new line arrives.
    const float input_height = ImGui::GetFrameHeightWithSpacing();
    ImGui::BeginChild("log", {0.0f, run_console_ ? -input_height : 0.0f}, ImGuiChildFlags_Borders);
    log.each([](const LogLine& line) {
        ImGui::PushStyleColor(ImGuiCol_Text, color_of(line.level));
        ImGui::TextUnformatted(line.text.c_str());
        ImGui::PopStyleColor();
    });
    std::uint64_t lines = log.count();
    if (lines != log_seen_) {
        ImGui::SetScrollHereY(1.0f);
        log_seen_ = lines;
    }
    ImGui::EndChild();

    if (!run_console_) return ImGui::End();

    // The input line. Up and down walk back through earlier lines, like a
    // terminal. ImGui tells us about those keys through a callback.
    auto history_callback = [](ImGuiInputTextCallbackData* data) -> int {
        auto* self = static_cast<DebugTools*>(data->UserData);
        auto& history = self->console_history_;
        if (history.empty()) return 0;
        int pos = self->history_pos_;
        if (data->EventKey == ImGuiKey_UpArrow) pos = pos < 0 ? static_cast<int>(history.size()) - 1 : std::max(0, pos - 1);
        else if (data->EventKey == ImGuiKey_DownArrow && pos >= 0) pos = pos + 1 < static_cast<int>(history.size()) ? pos + 1 : -1;
        if (pos == self->history_pos_) return 0;
        self->history_pos_ = pos;
        data->DeleteChars(0, data->BufTextLen);
        if (pos >= 0) data->InsertChars(0, history[static_cast<std::size_t>(pos)].c_str());
        return 0;
    };
    if (focus_console_) {
        ImGui::SetKeyboardFocusHere();
        focus_console_ = false;
    }
    ImGui::SetNextItemWidth(-1.0f);
    const ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory;
    if (ImGui::InputTextWithHint("##input", "Lua", &console_input_, flags, history_callback, this)) {
        if (!console_input_.empty()) run_console_line(app, console_input_);
        console_input_.clear();
        focus_console_ = true; // keep typing without clicking back in
    }
    ImGui::End();
}

void DebugTools::run_console_line(App& app, const std::string& line) {
    LogHistory& log = app.log();
    log.add(LogLevel::Command, "> " + line);
    console_history_.push_back(line);
    history_pos_ = -1;
    ConsoleReply reply = run_console_(line);
    if (!reply.text.empty()) log.add(reply.ok ? LogLevel::Result : LogLevel::Error, reply.text);
}

} // namespace eng
