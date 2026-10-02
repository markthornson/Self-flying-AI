#pragma once

// Lua scripting: gameplay code that runs without recompiling the engine.
//
// A game's rules live in a Lua file. The engine calls these global functions
// in it, when they exist:
//
//     start()                      once, after the script loads
//     update(dt)                   every fixed step, before physics
//     on_trigger(trigger, other)   when a body walks into a trigger collider
//     hud()                        every frame, to draw text with ui.text()
//
// and the script calls back into the engine through a small set of tables
// (world, input, audio, physics, ui) and two types (vec3 and Entity). The full
// list is in docs/scripting.md. Keeping that surface narrow and documented is
// the point: it is the contract between engine and game, and everything
// behind it can change freely.
//
// Lua 5.4 is the interpreter; sol2 generates the glue that moves values
// between C++ and Lua's stack. If a script errors, the error is logged and
// shown on screen, the script stops running, and the game keeps going so you
// can fix the file and reload.

#include "engine/world/ecs.h"

#include <filesystem>
#include <memory>
#include <string>

namespace eng {

class Physics;
class Input;
class Audio;
class Assets;

// The engine systems a script can reach.
struct ScriptServices {
    World& world;
    Physics& physics;
    Input& input;
    Audio& audio;
    const Assets& assets;
};

class Scripting {
public:
    explicit Scripting(ScriptServices services);
    ~Scripting();
    Scripting(const Scripting&) = delete;
    Scripting& operator=(const Scripting&) = delete;

    // Throws away any previous Lua state, makes a fresh one with the engine
    // API, and runs the file. Other files in the same folder can be loaded
    // from it with require("name").
    bool load(const std::filesystem::path& path);

    void start();
    void update(float dt);
    void on_trigger(Entity trigger, Entity other);
    // Draws the script's HUD and any script error. Call between ImGui frames.
    void hud();

    // Runs a snippet of Lua in the current state, e.g. from tests. An error
    // stops the script, like an error in a callback.
    bool run(const std::string& code);

    // Runs a line typed into the debug console and returns what it printed
    // back: its values for an expression ("world.count()" gives "12"), or
    // the error message. Errors here don't stop the game's script.
    struct Evaluation {
        bool ok = true;
        std::string text;
    };
    Evaluation evaluate(const std::string& line);

    const std::string& error() const;

private:
    struct Impl; // keeps sol2's large headers out of everyone else's build
    std::unique_ptr<Impl> impl_;
};

} // namespace eng
