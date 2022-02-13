-- mod-version:2
local core = require "core"
local config = require "core.config"
local Object = require "core.object"
local process = require "process"

local json = require "plugins.litepresence.json"

local PASSTHROUGH_PATH = USERDIR .. "/plugins/litepresence/rpc.com"
local OPCODES = {
	HANDSHAKE = 0,
	FRAME = 1,
	CLOSE = 2,
	PING = 3,
	PONG = 4
}


local RPC = Object:extend()

function RPC:new(client_id, connect_timeout)
	self.client = client_id
	self.timeout = connect_timeout

	self.buf = { size = 0 }
	self.run = true

	core.add_thread(function()
		while self.run do
			local ok, proc = pcall(process.start, { PASSTHROUGH_PATH }, {
				stdout = process.REDIRECT_PIPE,
				stdin = process.REDIRECT_PIPE,
				stderr = process.REDIRECT_PIPE
			})

			if not ok then
				self:on_error(proc, 0)
			else
				self.proc = proc
				self:send(OPCODES.HANDSHAKE, { v = 1, client_id = client_id })

				while self.proc:running() do
					local ok, output = pcall(self.proc.read_stdout, self.proc)
					if not ok then break end

					if output and #output > 0 then
						self:recv(output)
					end

					coroutine.yield(1 / config.fps)
				end

				pcall(self.proc.terminate, self.proc)

				-- try to read stderr
				if self.run then
					local ok, err = pcall(self.proc.read_stderr, self.proc)
					err = err or "unknown error"
					self:on_error(err:gsub("%s$", ""), self.proc:returncode() or 0)
				end

				self.proc = nil
			end

			coroutine.yield(1)
		end
	end)
end

function RPC:start()
	core.add_thread(function()
		while not self.stop do
			::TOP::
			coroutine.yield(self.timeout)

			local ok, proc = pcall(process.start, { PASSTHROUGH_PATH })
			if not ok then
				self:on_error(proc, 0)
				goto TOP
			end

			self.proc = proc

			while self.proc:running() do
				local ok, output = pcall(proc.read_stdout, proc)
				if not ok or self.stop then break end

				if output and #output > 0 then
					self:recv(output)
				end
			end

			proc:terminate()
			if not self.stop then
				-- try reading stderr for error
				local _, err = pcall(proc.read_stderr, proc)
				err = err:gsub("%s*$", ""):gsub("^%s*", "")
				self:on_error(err, proc:returncode() or 0)
			end
		end
	end)
end

function RPC:stop()
	self.stop = true
	if self.proc then
		self.proc:terminate()
	end
end

local function write32le(i)
	return string.char((i << 0) & 0xFF, (i << 8) & 0xFF, (i << 16) & 0xFF, (i << 24) & 0xFF)
end

function RPC:send(op, msg)
	if self.proc then
		msg = json.encode(msg)
		self.proc:write(write32le(op) .. write32le(#msg) .. msg)
	end
end

local function read32le(str, idx)
	local a, b, c, d = string.byte(str, idx, idx + 4)
	return ((a << 0) | (b << 8) | (c << 16) | (d << 24))
end

function RPC:recv(str)
	self.buf[#self.buf+1] = str
	self.buf.size = self.buf.size + #str
	if not self.header and self.buf.size >= 8 then
		local s = table.concat(self.buf)
		local op = read32le(s, 1)
		local size = read32le(s, 5)
		self.header = {
			op = op,
			size = size
		}
		local rest = s:sub(9)
		self.buf = { rest, size = #rest }
	end

	if self.buf.size >= self.header.size then
		local s = table.concat(self.buf)
		local msg = s:sub(1, self.header.size)
		local rest = s:sub(self.header.size + 1)
		self:on_message(self.header.op, json.decode(msg))
		self.header = nil
		self.buf = { rest, size = #rest }
	end
end

function RPC:set_activity(activity)
	if self.proc then
		self:send(OPCODES.FRAME, {
			cmd = "SET_ACTIVITY",
			args = {
				pid = system.get_process_id and system.get_process_id(),
				activity = activity
			},
			nonce = string.format("%08d-%08d", math.floor(system.get_time() * 1000), math.random(10000))
		})
	else
		self.presence = activity
	end
end

function RPC:on_ready()
	if self.presence then
		self:set_activity(self.presence)
		self.presence = nil
	end
end

function RPC:on_message(op, msg)
	if op == OPCODES.PING then
		self:send(OPCODES.PONG, msg)
	elseif op == OPCODES.FRAME then
		if msg.evt == "READY" then
			self:on_ready()
		end
	end
end

function RPC:on_error(msg, code)
	print(msg, code)
end


return RPC
