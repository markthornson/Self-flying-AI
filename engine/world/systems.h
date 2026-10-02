#pragma once

// Systems that every game runs, whatever it is about.

#include "engine/world/ecs.h"

namespace eng {

class Assets;
class Renderer;

// Copies each Transform into its PreviousTransform. Call at the start of every
// fixed step, before anything moves.
void save_previous_transforms(World& world);

// Snaps an entity's PreviousTransform to its current Transform, so a teleport
// (a respawn, say) jumps instead of visibly sliding across the level.
void skip_interpolation(World& world, Entity e);

// Submits every entity with a Transform and a MeshRenderer to the renderer,
// placed `alpha` of the way from its previous to its current transform.
// `assets` turns each MeshRenderer's model handle into the mesh to draw.
void draw_meshes(World& world, Renderer& renderer, const Assets& assets, float alpha);

} // namespace eng
