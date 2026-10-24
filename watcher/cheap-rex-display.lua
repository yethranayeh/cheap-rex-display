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
--  display and sends the current status of every pane. Between those it
--  sends each change, and a heartbeat every 15 seconds so the display can
--  tell when this server has gone quiet, e.g. a laptop that went to sleep
--
-- It also keeps a request open to the display's port 81, which sends a line
--  for each tap on one of this server's rows. A tap brings that pane to the
--  front. The request is opened again on each heartbeat if it dropped.

local source = rex.args.source or "mac"
local url = rex.args.url or "http://crd.local"
-- The Rex app that runs the terminal in each pane
local terminalApp = "com.superlogical.terminal"

-- Set when a send fails, so the next event sends everything instead
local needResync = true
-- The session of each pane sent, so a tap can focus it
local paneSessions = {}
-- The open request for taps, from rex.exec.start
local taps = nil

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

-- A pane can report several records at once.
-- The key names the pane and the record,
--  so the display can clear them one by one or all at once.
local function sendRecord(sessionId, blockId, recordId, record)
  paneSessions[blockId] = sessionId
  post("/status", {
    key = blockId .. "#" .. (recordId or ""),
    session = sessionLabel(sessionId),
    app = record.effective_app or record.app or "",
    state = record.state,
    kind = record.kind or "",
    msg = record.msg or "",
  })
end

-- Sends the status of every pane in one session
local function sendSession(sessionId)
  rex.call("session.attach", { session_id = sessionId })
  local list = rex.session.list_blocks{ session_id = sessionId }

  for _, block in ipairs(list and list.blocks or {}) do
    if block.creator_name == terminalApp then
      local status = rex.block.call(terminalApp, "program_status", {
        session_id = sessionId,
        block_id = block.block_id,
      })

      for _, record in ipairs(status and status.records or {}) do
        sendRecord(sessionId, block.block_id, record.id, record)
      end
    end
  end
end

-- Clears this source on the display and sends the status of every pane
--  in every session
local function resync()
  needResync = false

  if post("/reset", {}) == nil then
    return
  end

  local list = rex.call("session.list")

  for _, session in ipairs(list and list.sessions or {}) do
    sendSession(session.session_id)
  end
end

-- The tab number of the window that holds a pane, counting from 1
local function tabIndex(sessionId, blockId)
  local view = rex.session.view{ session_id = sessionId }

  for i, window in ipairs(view and view.windows or {}) do
    if rex.json.encode(window):find(blockId, 1, true) then
      return i
    end
  end

  return nil
end

-- Brings a tapped pane to the front. The key is the one this watcher sent,
--  pane#record. focus_block only changes the session on the server: each
--  Rex app keeps its own tab in view, so the app is asked to show the
--  session and the pane's tab as well.
local function focusPane(key)
  local blockId = key:match("^[^#]*")
  local sessionId = paneSessions[blockId]

  if sessionId == nil then
    rex.log("cheap-rex-display: tap on unknown pane " .. blockId)
    return
  end

  local _, err = rex.session.focus_block{ session_id = sessionId, block_id = blockId }
  if err then
    rex.log("cheap-rex-display: focus failed: " .. tostring(err))
    return
  end

  local index = tabIndex(sessionId, blockId)
  local clients = rex.call("client.list")

  for _, client in ipairs(clients and clients.clients or {}) do
    if client.info and client.info.kind == "app" then
      local function perform(action, args)
        return rex.call("client.perform", {
          client_id = client.client_id,
          session_id = sessionId,
          action = action,
          args = args,
        })
      end

      perform("session.select", { session_id = sessionId })
      if index then
        perform("client.tab.goto", { index = index })
      end
    end
  end
end

-- Opens the request for taps unless it is still open. curl -N passes each
--  line on as it comes; a piece of output may end mid-line.
local function connectTaps()
  if taps and taps:running() then
    return
  end

  local buffer = ""
  -- detach: the request must outlive the heartbeat that opens it
  taps = rex.exec.start({ "curl", "-4", "-sS", "-N", "--fail", url .. ":81/taps?source=" .. source }, {
    detach = true,
    stdout = function(data)
      buffer = buffer .. data
      for line in buffer:gmatch("([^\n]*)\n") do
        local key = line:match("^tap (.+)$")
        if key then
          focusPane(key)
        end
      end
      buffer = buffer:match("[^\n]*$")
    end,
  })
end

rex.on("terminal.program_status_changed", function(ctx, ev)
  if needResync then
    resync()
    return
  end

  sendRecord(ctx.session_id, ctx.block_id, ev.id, ev.record)
end)

rex.on("terminal.program_status_removed", function(ctx, ev)
  if needResync then
    resync()
    return
  end

  for _, id in ipairs(ev.ids or {}) do
    post("/status", { key = ctx.block_id .. "#" .. id, state = "clear" })
  end
end)

rex.on("block_closed", function(ctx, ev)
  if needResync then
    resync()
    return
  end

  post("/status", { key = ev.block_id, state = "clear" })
end)

-- Rows show the session's name, so send the renamed session's rows again
rex.on("session_label_changed", function(ctx, ev)
  if needResync then
    resync()
    return
  end

  sendSession(ctx.session_id)
end)

-- A closed session takes its panes with it; start over rather than
--  track which rows were in it
rex.on("session_destroyed", function(ctx, ev)
  resync()
end)

rex.every(15, function()
  local reply = post("/heartbeat", {})
  local forgotten = reply ~= nil and reply:match("^unknown") ~= nil

  if needResync or forgotten then
    resync()
  end

  connectTaps()
end)

resync()
connectTaps()
