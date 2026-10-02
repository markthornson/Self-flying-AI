-- Coin Hunt: the game's rules, in Lua.
--
-- The engine calls start() once, update(dt) every fixed step (60 a second),
-- on_trigger(trigger, other) when a body walks into a trigger, and hud()
-- every frame. Everything the script can do is listed in docs/scripting.md.
--
-- Edit this file while the game runs and press F5 to reload it.

local level = require("level")

-- Tuning. Try changing these and pressing F5.
local MOVE_SPEED = 6.0     -- metres per second
local ACCELERATION = 45.0  -- how quickly we reach MOVE_SPEED; lower feels icier
local JUMP_SPEED = 8.5     -- upward speed when a jump starts
local COYOTE_TIME = 0.1    -- seconds after leaving a ledge when jumping still works
local TIME_LIMIT = 90.0    -- seconds to find every coin
local PENALTY = 5.0        -- seconds lost when hit or falling
local FALL_HEIGHT = -10.0  -- below this, you've fallen off the world

local player
local coins = {}           -- coin entities still in play
local enemies = {}         -- { entity, from, to, speed, phase }
local total_coins = 0
local collected = 0
local time_left = 0.0
local state = "playing"    -- "playing", "won" or "lost"
local ground_timer = 0.0   -- counts down from COYOTE_TIME once off the ground
local elapsed = 0.0
local flash = 0.0          -- seconds left to show the penalty message

function start()
    world.clear()
    local built = level.build()
    player = built.player
    coins = built.coins
    enemies = built.enemies
    total_coins = #coins
    collected = 0
    time_left = TIME_LIMIT
    state = "playing"
    elapsed = 0.0
    flash = 0.0
    audio.music("music", 0.45)
end

-- --- Each step ----------------------------------------------------------------------

local function move_player(dt)
    -- Which way the stick or keys point. The camera looks down -Z, so
    -- "forward" on the stick is -Z in the world.
    local wish = vec3(input.axis("move_x"), 0, -input.axis("move_forward"))
    if wish:length() > 1 then wish = wish:normalized() end -- diagonals aren't faster

    -- Steer the horizontal velocity towards where we want to go, but only by
    -- ACCELERATION * dt per step. Instant changes feel robotic.
    local v = player:velocity()
    local change = vec3(wish.x * MOVE_SPEED - v.x, 0, wish.z * MOVE_SPEED - v.z)
    local max_change = ACCELERATION * dt
    if change:length() > max_change then change = change:normalized() * max_change end
    v = v + change

    -- Coyote time: allow a jump for a moment after running off a ledge.
    -- Players press jump a little late all the time, and this forgives it.
    if player:on_ground() then ground_timer = COYOTE_TIME else ground_timer = ground_timer - dt end
    if input.pressed("jump") and ground_timer > 0 then
        v.y = JUMP_SPEED
        ground_timer = 0
        audio.play("jump", 0.6)
    end
    player:set_velocity(v)

    -- Face the way we're going. atan(x, z) is the angle from +Z, which is the
    -- way the model faces at yaw 0.
    if wish:length() > 0.1 then player:set_yaw(math.atan(wish.x, wish.z)) end
end

local function move_enemies()
    for _, e in ipairs(enemies) do
        -- Back and forth along a line, easing at each end. (1 - cos) / 2 runs
        -- smoothly from 0 to 1 and back as the angle goes round.
        local t = (1 - math.cos(elapsed * e.speed + e.phase)) / 2
        e.entity:set_position(e.from:lerp(e.to, t))
        e.entity:set_yaw(elapsed * 3)
    end
end

local function spin_coins()
    for _, coin in ipairs(coins) do
        if coin:alive() then coin:set_yaw(elapsed * 2.5) end
    end
end

local function penalty()
    audio.play("hurt", 0.8)
    time_left = math.max(0, time_left - PENALTY)
    flash = 1.5
    player:teleport(level.SPAWN)
end

function update(dt)
    if input.pressed("restart") then
        start()
        return
    end

    elapsed = elapsed + dt
    flash = math.max(0, flash - dt)
    spin_coins()
    move_enemies()

    if state ~= "playing" then
        -- Game over: stop the player but let the world keep moving.
        player:set_velocity(vec3(0, player:velocity().y, 0))
        return
    end

    move_player(dt)

    time_left = time_left - dt
    if time_left <= 0 then
        time_left = 0
        state = "lost"
        audio.stop_music()
        audio.play_ui("lose")
    end

    if player:position().y < FALL_HEIGHT then penalty() end
end

-- --- Events ---------------------------------------------------------------------------

function on_trigger(trigger, other)
    if other ~= player or state ~= "playing" then return end

    local kind = trigger:name()
    if kind == "coin" then
        audio.play_at("coin", trigger:position())
        trigger:destroy() -- gone at the end of this step
        collected = collected + 1
        if collected == total_coins then
            state = "won"
            audio.stop_music()
            audio.play_ui("win")
        end
    elseif kind == "enemy" then
        penalty()
    end
end

-- --- HUD -------------------------------------------------------------------------------

function hud()
    ui.text(string.format("Coins  %d / %d", collected, total_coins))
    ui.text(string.format("Time   %d", math.ceil(time_left)))
    if flash > 0 then ui.text(string.format("-%d seconds!", PENALTY)) end

    if state == "won" then
        ui.title(string.format("You win!  %d seconds to spare", math.floor(time_left)))
        ui.text("Press R to play again")
    elseif state == "lost" then
        ui.title("Out of time!")
        ui.text("Press R to try again")
    elseif elapsed < 6 then
        ui.text("WASD to move, Space to jump")
    end
end
