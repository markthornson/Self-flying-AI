#include "engine/world/systems.h"

#include "engine/assets/assets.h"
#include "engine/core/profile.h"
#include "engine/render/renderer.h"
#include "engine/world/components.h"

namespace eng {

void save_previous_transforms(World& world) {
    PROFILE_SCOPE("save_previous_transforms");
    world.each<PreviousTransform, Transform>([](Entity, PreviousTransform& previous, Transform& current) {
        previous.value = current;
    });
}

void skip_interpolation(World& world, Entity e) {
    PreviousTransform* previous = world.get<PreviousTransform>(e);
    Transform* current = world.get<Transform>(e);
    if (previous && current) previous->value = *current;
}

void draw_meshes(World& world, Renderer& renderer, const Assets& assets, float alpha) {
    PROFILE_SCOPE("draw_meshes");
    world.each<MeshRenderer, Transform>([&](Entity e, MeshRenderer& look, Transform& current) {
        // Entities without a PreviousTransform (scenery that never moves) are
        // drawn where they are.
        const PreviousTransform* previous = world.get<PreviousTransform>(e);
        Transform shown = previous ? interpolate(previous->value, current, alpha) : current;
        renderer.draw(assets.mesh(look.model), shown.to_matrix(), look.tint);
    });
}

} // namespace eng
