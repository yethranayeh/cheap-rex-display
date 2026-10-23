-- Sends program status from one Rex server to the display.
--
--   rex do watcher/cheap-rex-display.lua --all
--
-- --all delivers the events of every session, not only the one the script runs in
-- Arguments: source, the name this server gets on the display
--  (default "mac"), and url, the display's address (default http://crd.local),
--  as in: rex do watcher/cheap-rex-display.lua --all source=laptop
--
-- On start, and whenever the display says it doesn't know this source
--  (after a restart or a Wi-Fi gap), the watcher clears this source on the
--  display and sends the current status of every terminal. Between those it
--  sends each change, and a heartbeat every 15 seconds so the display can
--  tell when this server has gone quiet, e.g. a laptop that went to sleep

local source = rex.args.source or "mac"
local url = rex.args.url or "http://crd.local"
local terminal = "com.superlogical.terminal"

-- Set when a send fails, so the next event sends everything instead
local needResync = true

-- POSTs form fields to the display. Returns the reply body, or nil when the
--  display can't be reached.
local function post(path, fields)
  -- -4: the display answers only IPv4 mDNS queries, and macOS waits 5 s for
  --  the IPv6 answer that will not come
  local argv = { "curl", "-4", "-sS", "-m", "3", "--fail", url .. path }
  fields.source = source

  for k, v in pairs(fields) do
    table.insert(argv, "--data-urlencode")
    table.insert(argv, k .. "=" .. tostring(v))
  end

  local r, err = rex.exec(argv)

  if r == nil or not r.ok then
    rex.log("cheap-rex-display: " .. path .. " failed: " .. tostring(err or r.stderr))
    needResync = true
    return nil
  end

  return r.stdout
end

local function sessionLabel(sessionId)
  -- A script can only view sessions it is attached to, and starts out
  --  attached to the one it runs in
  rex.call("session.attach", { session_id = sessionId })

  local view = rex.session.view{ session_id = sessionId }
  return view and view.label or "?"
end

-- A terminal can report several records at once.
-- The key names the terminal and the record,
--  so the display can clear them one by one or all at once.
local function sendRecord(sessionId, blockId, recordId, record)
  post("/status", {
    key = blockId .. "#" .. (recordId or ""),
    session = sessionLabel(sessionId),
    app = record.effective_app or record.app or "",
    state = record.state,
    kind = record.kind or "",
    msg = record.msg or "",
  })
end

-- Clears this source on the display and sends the status of every terminal
--  in every session
local function resync()
  needResync = false
  if post("/reset", {}) == nil then return end

  local list = rex.call("session.list") or { sessions = {} }

  for _, s in ipairs(list.sessions) do
    rex.call("session.attach", { session_id = s.session_id })
    local view = rex.session.view{ session_id = s.session_id }

    for _, w in ipairs(view and view.windows or {}) do
      for _, layer in ipairs(w.layers or {}) do
        for _, b in ipairs(layer.blocks or {}) do
          if b.creator_name == terminal then
            local st = rex.block.call(terminal, "program_status", {
              session_id = s.session_id, block_id = b.block_id,
            })

            for _, record in ipairs(st and st.records or {}) do
              sendRecord(s.session_id, b.block_id, record.id, record)
            end
          end
        end
      end
    end
  end
end

rex.on("terminal.program_status_changed", function(ctx, ev)
  if needResync then return resync() end
  sendRecord(ctx.session_id, ctx.block_id, ev.id, ev.record)
end)

rex.on("terminal.program_status_removed", function(ctx, ev)
  if needResync then return resync() end

  for _, id in ipairs(ev.ids or {}) do
    post("/status", { key = ctx.block_id .. "#" .. id, state = "clear" })
  end
end)

rex.on("block_closed", function(ctx, ev)
  if needResync then return resync() end
  post("/status", { key = ev.block_id, state = "clear" })
end)

-- A closed session takes its terminals with it; start over rather than
--  track which rows were in it
rex.on("session_destroyed", function(ctx, ev)
  resync()
end)

rex.every(15, function()
  local reply = post("/heartbeat", {})
  if needResync or (reply and reply:match("^unknown")) then resync() end
end)

resync()
