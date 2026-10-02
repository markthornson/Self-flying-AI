-- The level layout. Kept apart from the rules in game.lua so the two can
-- change independently; in phase 3 this becomes a scene file, and later a
-- level built in Blender.
--
-- Units are metres. +Y is up and the camera looks down -Z, so -Z is "away
-- from you" on screen. Platform and crate models are 1 m cubes centred on
-- their position, so a box at y = 0.5 with scale 1 sits on the ground.

local level = {}

level.SPAWN = vec3(0, 1, 6)

-- Solid scenery: a grass-topped block of size (w, h, d) whose top is at `top`.
local function platform(x, top, z, w, h, d)
    world.spawn{ name = "ground", model = "platform", collider = "box",
                 position = vec3(x, top - h / 2, z), scale = vec3(w, h, d) }
end

local function crate(x, y, z, size)
    size = size or 1
    world.spawn{ name = "crate", model = "crate", collider = "box",
                 position = vec3(x, y + size / 2, z), scale = size }
end

local function coin(list, x, y, z)
    -- Coins float a little above where they sit and are triggers: nothing
    -- bumps into them, but touching one reports an on_trigger.
    list[#list + 1] = world.spawn{ name = "coin", model = "coin", position = vec3(x, y + 0.7, z),
                                   collider = "sphere", radius = 0.45, trigger = true }
end

local function enemy(list, from, to, speed, phase)
    local e = world.spawn{ name = "enemy", model = "enemy", position = from,
                           collider = "sphere", radius = 0.45, trigger = true }
    list[#list + 1] = { entity = e, from = from, to = to, speed = speed, phase = phase or 0 }
end

function level.build()
    local coins, enemies = {}, {}

    -- The main island, and a smaller one across a gap to the right.
    platform(0, 0, 0, 20, 2, 18)
    platform(17, 0, -4, 6, 2, 8)

    -- A staircase of crates up to a high ledge at the back left.
    crate(-3, 0, -3)
    crate(-4.2, 0, -4.2)
    crate(-4.2, 1, -4.2)
    platform(-7, 2.5, -6.5, 5, 1, 4)

    -- A stepping stone over the gap. Miss it and you fall.
    platform(12, 1, -2, 1.5, 1, 1.5)

    -- A crate tower in the middle with a coin on top.
    crate(3, 0, -5, 1.2)
    crate(3, 1.2, -5, 1.2)
    crate(4.2, 0, -5, 1.2)

    -- Coins: easy ones near the start, harder ones up high and far away.
    coin(coins, -3, 0, 3)
    coin(coins, 3, 0, 3)
    coin(coins, 0, 0, -1)
    coin(coins, -6, 0, 1)
    coin(coins, 6, 0, -1)
    coin(coins, -4.2, 2, -4.2)
    coin(coins, -8, 2.5, -7.5)
    coin(coins, -6, 2.5, -6)
    coin(coins, 3, 2.4, -5)
    coin(coins, 12, 1, -2)
    coin(coins, 16, 0, -2)
    coin(coins, 18.5, 0, -6.5)

    -- Diamonds patrolling the open ground.
    enemy(enemies, vec3(-6, 0.6, -2), vec3(6, 0.6, -2), 0.9)
    enemy(enemies, vec3(-1.5, 0.6, 5), vec3(-1.5, 0.6, -6), 1.3, 1.0)
    enemy(enemies, vec3(15, 0.6, -6.5), vec3(19, 0.6, -6.5), 1.6)

    local player = world.spawn{ name = "player", model = "player", position = level.SPAWN,
                                collider = "sphere", radius = 0.5, body = true }

    return { player = player, coins = coins, enemies = enemies }
end

return level
