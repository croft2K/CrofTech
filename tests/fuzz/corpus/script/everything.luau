local function count(game, by)
    game.state.n += by
    if game.state.n > 3 then
        game.state.done = true
        game:fire("on_done")
    end
end

return {
    state = { n = 0, done = false },
    events = { "on_done" },
    inputs = {
        one = function(game) count(game, 1) end,
        later = function(game) game:after(0.5, "one") end,
        send = function(game) game:send("door", "move_to_end", game:random()) end,
        text = function(game)
            local parts = {}
            for word in string.gmatch("a b c", "%a") do table.insert(parts, word) end
            print(table.concat(parts, ","), tostring({}), string.format("%s %d", parts, 3))
        end,
    },
    start = function(game) game.state.n = game:random(1, 2) end,
}
