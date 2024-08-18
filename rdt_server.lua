--[[
由服务器发起握手流程如下:
server (HANDSHAKE_1)---> client
server <------(HANDSHAKE_2) client
server <--(handshake succ)---> client

]]

-- package.cpath = package.cpath .. ";./?.so"

local SOCKET = require "lsocket"
local SERVER = require "lsocket.server"

local port = assert(tonumber(...))
local so = assert(SOCKET.bind(port))

local SESSION_START = 10000
--rdt session 是否握手成功
local enable = false

local readsocket = { so }
local client = {}
local fds = {}
local enable = {}
local current_id
local fd2rdt = {}
local session_2_delete = {}

function string.split( line, sep, maxsplit ) 
	if not line or string.len(line) == 0 then
		return {}
	end
	sep = sep or ' '
	maxsplit = maxsplit or 0
	local retval = {}
	local idx = 0
	local pos = 1
	local step = 0
	local item, from, to
	while true do
		from, to = string.find(line, sep, pos, true)
		step = step + 1
		if (maxsplit ~= 0 and step > maxsplit) or from == nil then
			item = string.sub(line, pos)
			idx = idx + 1
			retval[idx] = item
			break
		else
			item = string.sub(line, pos, from-1)
			idx = idx + 1
			retval[idx] = item
			pos = to + 1
		end
	end
	return retval
end

local function sendmsgbyso(so, msg)
    msg = SOCKET.pack_msg(msg)
    local len = 0
    while true do
        len = so:send(msg:sub(len + 1))
        if len then
            if len == #msg then
                break
            end
        else
            error("write failed")
        end
    end
end

local function sendmsgbyrdt(session_id, msg)
    -- msg = SOCKET.pack_msg(msg)
    SERVER.rdt_send(session_id, msg)
end

local function accept(c)
	table.insert(readsocket, c)
	local fd = c:info().fd
	client[c] = fd
	fds[fd] = c

   
end

local function close(s)
    local fd = client[s]
    local session_id = fd2rdt[fd]
	fds[client[s]] = nil
	client[s] = nil
    fd2rdt[fd] = nil
	local tmp = readsocket
	readsocket = {}
	for _,v in pairs(tmp) do
		if v ~= s then
			table.insert(readsocket, v)
		end
	end

    --不删除引擎里的rdt对象，因为后面要演示如何重连
    if session_id then
        print("disable rdt session: ", session_id)
        enable[session_id] = false
        session_2_delete[session_id] = os.time()
    end
end

local function on_recv_raw(s, str)
    local args = string.split(str, " ")
    local cmd = args[1]
    if cmd == "HANDSHAKE_2" then
	    local fd = client[s]
        local session_id = fd2rdt[fd]
        SERVER.rdt_create(session_id)
        enable[session_id] = true
        print("rdt handshake succ: ", fd, session_id)
        sendmsgbyrdt(session_id, "HANDSHAKE_3")
    elseif cmd == "LOGIN" then
        --客户端登录时，进行rdt handshake
	    local fd = client[s]
        SESSION_START = SESSION_START + 1
        local session_id = SESSION_START
        fd2rdt[fd] = session_id
        enable[session_id] = false
        local msg = "HANDSHAKE_1 " .. session_id
        sendmsgbyso(s, msg)
    elseif cmd == "RECONN_HANDSHAKE_1" then
	    local fd = client[s]
        local session_id = tonumber(args[2])
        fd2rdt[fd] = session_id
        enable[session_id] = false
        print("RECONN_HANDSHAKE_1: ", fd, session_id)
        SERVER.rdt_disable(session_id)
        sendmsgbyso(s, "RECONN_HANDSHAKE_2")
        SERVER.rdt_reconnect(session_id)
        -- poll()
        local t, sid, msg = SERVER.rdt_poll(session_id)
        assert(t == 2, t)
        sendmsgbyso(s, msg)

        enable[session_id] = true
    else
        print("server: ", str)
    end
end

local function recv(s)
	local fd = client[s]
    local session_id = fd2rdt[fd]
	local str, err = s:recv_packet()
	if str then
        if session_id and enable[session_id] then
            SERVER.rdt_recv(session_id, str)
        else
            on_recv_raw(s, str)
        end

	elseif str == nil then
		print("disconnect", fd)
--		report closed
--		pool:recv(fd, "")
		close(s)
		current_id = nil
	end
end

local function poll()
    local msg_in = {}

    for fd, session_id in pairs(fd2rdt) do
        if enable[session_id] then
            while true do
                local t, sid, msg = SERVER.rdt_poll(session_id)
                if t == nil then
                    break
                elseif t == 1 then
                    --message in
                    msg_in[fd] = session_id
                    print("<==========:", sid, msg)
                else
                    local so = assert(fds[fd])
                    sendmsgbyso(so, msg)
                end
            end
        end
    end

    return msg_in
end

--当重连成功后，引擎会回调该函数
function _G.OnSessionReconnected(session_id)
    -- enable[session_id] = true
    print("server reconnect succ: ", session_id)
end


print("start server: ", port)
while true do
	local r = SOCKET.select(readsocket)
	local t = 0
	for _, s in ipairs(r) do
		if s == so then
			local c, ip, port = so:accept()
			if c then
				print("accept :", ip, port)
				accept(c)
			end
		else
			recv(s)
			local msg_in = poll()
            for fd, session_id in pairs(msg_in) do
                local msg = tostring(t) .. "\n"
                sendmsgbyrdt(session_id, msg)
            end
			t=t+1
			poll()
		end
	end
end