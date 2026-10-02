#include "engine/debug/inspector.h"

#include <imgui.h>

namespace eng {

void Inspector::draw(World& world, Entity e) const {
    for (const Section& section : sections_) {
        if (!section.draw(world, e, false)) continue; // the entity doesn't have one
        ImGui::PushID(section.name.c_str());
        if (ImGui::CollapsingHeader(section.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) section.draw(world, e, true);
        ImGui::PopID();
    }
}

} // namespace eng
