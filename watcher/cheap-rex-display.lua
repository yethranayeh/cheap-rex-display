-- Sends program status from one Rex server to the display.
--
--   rex do watcher/cheap-rex-display.lua --all
--
-- --all delivers the events of every session, not only the one the script runs in
-- Arguments: source, the name this server gets on the display
--  (default "mac"), and url, the display's address (default http://crd.local),
--  as in: rex do watcher/cheap-rex-display.lua --all source=laptop
--
-- The watcher only sends changes, so the display
--  starts out empty and shows what changes from then on

local source = rex.args.source or "mac"
local url = rex.args.url or "http://crd.local"

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
rex.on("terminal.program_status_changed", function(ctx, ev)
  local record = ev.record

  post("/status", {
    key = ctx.block_id .. "#" .. (ev.id or ""),
    session = sessionLabel(ctx.session_id),
    app = record.effective_app or record.app or "",
    state = record.state,
    kind = record.kind or "",
    msg = record.msg or "",
  })
end)

rex.on("terminal.program_status_removed", function(ctx, ev)
  for _, id in ipairs(ev.ids or {}) do
    post("/status", { key = ctx.block_id .. "#" .. id, state = "clear" })
  end
end)

rex.on("block_closed", function(ctx, ev)
  post("/status", { key = ev.block_id, state = "clear" })
end)
