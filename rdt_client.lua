--[[
由服务器发起握手流程如下:
server (HANDSHAKE_1)---> client
server <------(HANDSHAKE_2) client
server <--(handshake succ)---> client

]]

package.cpath = package.cpath .. ";./?.so"
local SOCKET = require "lsocket"
local CLIENT = require "lsocket.client"

local ip, port = ...
port = assert(tonumber(port))

local session_id = 10000
--rdt session 是否握手成功
local enable = false
local handshake_succ = false
local MSG_ID = 1000

--是否出发重连标记
local need_reconn = false

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

local function sendmsgbyrdt(so, msg)
    -- msg = SOCKET.pack_msg(msg)
    CLIENT.rdt_send(session_id, msg)
end

local function send_rdt_testmsg(so)
    local msg = string.format("client msg. sid=%d,msgid=%d", session_id, MSG_ID)
    MSG_ID = MSG_ID + 1
    sendmsgbyrdt(so, msg)
end


local so = assert(SOCKET.connect(ip, port))




local function on_recv_rdt_msg(so, session_id, msg)
    local args = string.split(msg, " ")
    local cmd = args[1]
    if cmd == "HANDSHAKE_3" then
        handshake_succ = true
        print("client recv HANDSHAKE_3: ", session_id)
        --之后，我们可以利用rdt来进行数据传输了。
        -- send_rdt_testmsg(so)
        -- send_rdt_testmsg(so)
        -- send_rdt_testmsg(so)
        need_reconn = 5
    else
		print("<=====", session_id, msg)
    end
end

local function poll(so)
    if not session_id then
        return
    end

    while true do
		local t, msg = CLIENT.rdt_poll(session_id)
		if t == nil then
			break
		end
		if t == 1 then
			-- message in
            on_recv_rdt_msg(so, session_id, msg)
		else
			-- message out
			sendmsgbyso(so, msg)
		end
    end
end


--处理没有使用rdt的数据包，主要用来握手
local function on_recv_raw(so, msg)
    assert(msg, msg)
    local args = string.split(msg, " ")
    local cmd = args[1]
    if cmd == "HANDSHAKE_1" then
        local sid = tonumber(args[2])
        assert(sid, cmd)
        session_id = sid
        print("client: HANDSHAKE_1")
        sendmsgbyso(so, "HANDSHAKE_2")
        CLIENT.rdt_create(session_id)
        enable = true
    elseif cmd == "RECONN_HANDSHAKE_2" then
        print("client RECONN_HANDSHAKE_2")
        CLIENT.rdt_reconnect(session_id)
        poll(so)
        enable = true
    else
        print("client: ", msg)
    end
end

local so = assert(SOCKET.connect(ip, port))
--通知服务器登录，服务器如果决定使用rdt连接，会发起握手
sendmsgbyso(so, "LOGIN")
local readsocket = { so }

----------测试重连---------------------
local function test_reconn()
    print("reconn enable: ", enable, handshake_succ)
    send_rdt_testmsg(so)
    send_rdt_testmsg(so)
    poll(so)
    so:close()
    so = assert(SOCKET.connect(ip, port))

    --先将rdt session设置为disable
    enable = false
    handshake_succ = false
    CLIENT.rdt_disable(session_id)
    sendmsgbyso(so, "RECONN_HANDSHAKE_1 " .. session_id)
    readsocket = {so}
end

--当重连成功后，引擎会回调该函数
function _G.OnSessionReconnected(session_id)
    print("client reconnect succ: ", session_id)
    handshake_succ = true
    -- need_reconn = 5
end

----------测试重连---------------------
--//


while true do
    local r = SOCKET.select(readsocket, {}, 1)
    if type(r) == "table" then
        assert(r[1] == so)
        local msg = assert(so:recv_packet())
        if enable then
            CLIENT.rdt_recv(session_id, msg)
        else
            on_recv_raw(so, msg)
        end
        poll(so)
    else
        need_reconn = need_reconn - 1
        if need_reconn == 0 then
            test_reconn()
        elseif handshake_succ then
            send_rdt_testmsg(so)
            send_rdt_testmsg(so)
            poll(so)
        end
    end


end

