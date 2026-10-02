# Lua scripting API

This is everything a game script can see. It is deliberately small: the
narrower the contract between engine and game, the more freely each side can
change. The code behind it is `engine/script/scripting.cpp`.

Scripts run Lua 5.4 with the `base`, `math`, `string`, `table` and `package`
libraries. `io` and `os` are left out on purpose: game rules have no reason to
touch files or run programs. `require("name")` loads `name.lua` from the same
folder as the main script.

## Callbacks the engine calls

Define any of these as global functions; missing ones are skipped.

| Function | When |
|---|---|
| `start()` | Once, after the script loads (and again whenever it reloads: on save, or with F5). |
| `update(dt)` | Every fixed step, 60 times a second, before physics. `dt` is the step length in seconds. |
| `on_trigger(trigger, other)` | A body (`other`) started touching a trigger collider (`trigger`). Once per contact. |
| `hud()` | Every frame. Draw text with `ui.text` and `ui.title`. |

If any of them raises an error, the message is logged and shown on screen and
the engine stops calling the script until it is reloaded.

## vec3

A 3D vector, copied by value like a number.

```lua
local v = vec3(1, 2, 3)      -- vec3() is zero
v.y = 5
local w = v + vec3(0, 1, 0) * 2
print(w, w:length(), w:normalized(), v:dot(w), v:cross(w), v:lerp(w, 0.5))
```

Supports `+`, `-`, unary `-`, `*` and `/` by a number, `==`, and `tostring`.

## Entity

An id for something in the world. Holding one is always safe: once the
entity is destroyed, `alive()` returns false and the other methods do nothing
(getters return zero values).

| Method | |
|---|---|
| `e:alive()` | Still exists? |
| `e:name()` | The name it was spawned with. |
| `e:position()`, `e:set_position(v)` | Where it is. Movement between steps is smoothed when drawn. |
| `e:teleport(v)` | Move instantly, with no smoothing, and stop its velocity. |
| `e:scale()`, `e:set_scale(s)` | `s` is a number or a vec3. |
| `e:yaw()`, `e:set_yaw(radians)` | Facing, as an angle about the up axis. 0 faces +Z. |
| `e:velocity()`, `e:set_velocity(v)` | Metres per second. Bodies only. |
| `e:on_ground()` | Standing on something floor-like this step? Bodies only. |
| `e:set_tint(r, g, b)` | Multiplies the model's colours. |
| `e:destroy()` | Removed at the end of the current step. |

## world

| Function | |
|---|---|
| `world.spawn{...}` | Creates an entity; returns it. Keys below. |
| `world.find(name)` | The first entity with this name, or `nil`. |
| `world.find_all(name)` | A list of every entity with this name. |
| `world.count()` | How many entities exist. |
| `world.clear()` | Destroys everything. |

`world.spawn` keys, all optional:

| Key | Default | |
|---|---|---|
| `name` | none | A label for `find`, `name()` and trigger handling. |
| `model` | none | A model loaded by the game, e.g. `"coin"`. |
| `position` | `vec3(0, 0, 0)` | |
| `scale` | `1` | A number or a vec3. Also scales the collider. |
| `yaw` | `0` | Radians. |
| `tint` | `vec3(1, 1, 1)` | |
| `collider` | none | `"box"` (a 1 m cube before scaling) or `"sphere"`. |
| `radius` | `0.5` | Sphere colliders. |
| `trigger` | `false` | Report touches through `on_trigger` instead of blocking. |
| `body` | `false` | Moves with its velocity and gravity, and is pushed out of solid colliders. |
| `gravity` | `1` | Multiplies gravity for this body; 0 floats. |

Colliders follow the entity's position and scale but not its rotation: boxes
stay axis aligned.

## input

Actions are named and bound by the game in C++ (`move_x`, `jump`...).

| Function | |
|---|---|
| `input.axis(name)` | -1 to 1. |
| `input.held(name)` | Button down right now. |
| `input.pressed(name)` | Pressed since the last step; true for exactly one step per press. |

## audio

Sounds are loaded by the game and played by name.

| Function | |
|---|---|
| `audio.play(name, volume, pitch)` | On the effects bus. `volume` and `pitch` default to 1. |
| `audio.play_at(name, position, volume)` | Effects bus, quieter with distance and panned left or right from the camera. |
| `audio.play_ui(name, volume)` | On the UI bus. |
| `audio.music(name, volume)` | Loop on the music bus, replacing any music playing. |
| `audio.stop_music()` | |

## physics

| Function | |
|---|---|
| `physics.raycast(origin, direction, max_distance, ignore)` | The nearest solid hit as `{ entity, distance, point, normal }`, or `nil`. `ignore` is an optional entity to skip. |
| `physics.gravity()`, `physics.set_gravity(v)` | Default `vec3(0, -20, 0)`. |

## ui

Only works inside `hud()`.

| Function | |
|---|---|
| `ui.text(string)` | A line in the HUD box at the top left. |
| `ui.title(string)` | Big text in the middle of the screen. |

## Logging

`print(...)` and `log(...)` both write to the engine log, prefixed `[lua]`.
The log shows in the debug console (F1) as well as the terminal.

## The debug console

Press F1 and type into the console at the bottom. Each line runs as Lua in
the game's own script state, so everything above works there, along with the
script's own globals:

```lua
world.find("player"):position()             -- shows vec3(0, 0.5, 6)
world.find("player"):teleport(vec3(0, 5, 0))
physics.set_gravity(vec3(0, -5, 0))         -- moon jumps
#world.find_all("coin")                     -- coins left
```

An expression shows its value. A mistake shows the error in red and, unlike
an error in `update()`, doesn't stop the game's script.
