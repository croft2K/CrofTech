-- A combination lock: three plates stepped on in the right order, within the time limit, open
-- the vault. A wrong plate, or taking too long, starts the combination again and sends a guard.
-- Each plate's trigger sends its input; the events are wired to the vault door and the spawner.

local ORDER = { "left", "right", "middle" }
local TIME_LIMIT = 10 -- seconds from the first plate

local function startAgain(game)
    game.state.step = 0
    game:fire("on_wrong")
end

local function press(game, plate)
    local state = game.state
    if state.solved then
        return
    end
    if ORDER[state.step + 1] ~= plate then
        startAgain(game)
        return
    end
    state.step += 1
    if state.step == 1 then
        state.deadline = game.time + TIME_LIMIT
        game:after(TIME_LIMIT, "check_time")
    end
    if state.step == #ORDER then
        state.solved = true
        game:fire("on_solved")
    else
        game:fire("on_step")
    end
end

return {
    state = { step = 0, solved = false, deadline = 0 },
    events = { "on_step", "on_wrong", "on_solved" },
    inputs = {
        left = function(game) press(game, "left") end,
        middle = function(game) press(game, "middle") end,
        right = function(game) press(game, "right") end,
        -- Sent at the time limit after a first plate. If a later attempt has started since, its
        -- deadline is later, and this one does nothing.
        check_time = function(game)
            local state = game.state
            if not state.solved and state.step > 0 and game.time >= state.deadline then
                print("too slow: start again")
                startAgain(game)
            end
        end,
    },
    start = function(game)
        print("the vault opens to the plates in the right order (left, right, middle)")
    end,
}
