#pragma once

// The component inspector: shows an entity's components in the debug UI and
// lets you edit them while the game runs.
//
// Engines like Unity and Godot can list any object's fields because their
// languages have *reflection*: code can ask a type what fields it has. C++
// can't (yet), so we do what most C++ engines do and register, once per
// component type, a small function that draws that type's fields:
//
//     inspector.add<Body>("Body", [](Body& b) {
//         ImGui::DragFloat3("Velocity", &b.velocity.x, 0.1f);
//     });
//
// draw() then walks the registered types, and for each one the entity has,
// shows a collapsible section with that function's widgets. The engine
// registers its own components (DebugTools does it); a game adds its own the
// same way.

#include "engine/world/ecs.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace eng {

class Inspector {
public:
    template <typename T>
    void add(std::string name, std::function<void(T&)> draw) {
        // Type erasure: wrap the typed function in one that takes a World and
        // an Entity, so components of every type fit in one list.
        sections_.push_back({std::move(name), [draw = std::move(draw)](World& world, Entity e, bool open) {
                                 T* component = world.get<T>(e);
                                 if (component && open) draw(*component);
                                 return component != nullptr;
                             }});
    }

    // Draws every registered component the entity has. Call inside an ImGui window.
    void draw(World& world, Entity e) const;

private:
    struct Section {
        std::string name;
        // Returns whether the entity has the component, and when `open` is
        // true also draws its fields.
        std::function<bool(World&, Entity, bool open)> draw;
    };
    std::vector<Section> sections_;
};

} // namespace eng
