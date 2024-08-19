RDT Session - A Reliable Data Transfer Session Implementation
----------------------------------------------

# 简介
rdt session是一个用于处理移动网络游戏断线重连问题的快速可靠协议，它通过在应用层和传输层之间增加一个对协议数据的缓存和确认机制，来实现快速重连。该功能是纯算法实现，不负责底层协议（UDP、TCP）的数据收发，需要你自己实现，并以callback的形式提供给rdt session，内部没有任何系统调用。

整个功能只包括mbuf.h、mbuf.c、rdt_session.h、rdt_session.c四个文件，其中mbuf实现可以任意替换为自己的实现。可以方便的集成到用户自己的协议栈中，非常方便。

该实现已经应用到数个移动游戏中，并且在国内外都得到了充分测试，效果非常好，特别是对于一些偏单机的游戏，它可以让玩家离线游戏数十分钟之后重连，而感觉不到自己已经离线。

# 算法思路
----------------------------------------------

## 背景
在移动游戏开发中，由于网络环境的不稳定，经常导致客户端与服务器断开连接，导致客户端卡住。开发者需要处理这里情况，比如进行断线重连来优化玩家体验，而市场上主流游戏都有自己的断线重连策略，来满足策划需求。

假设在客户端与服务器之间有一条连接，客户端向服务器发送了若干数据，而因为网络问题，客户端并不知道服务器是否已经收到这部分数据。在经过一段时间，开始进行重连，客户端新建一条连接，并关闭旧连接，但此时，旧连接的数据已经丢失，而客户端不知道服务器是否收到。在这种情况下，应用层是否重发都会产生问题：1、如果未丢失，重发会造成重复请求，如果该协议非幂等，则会造成其他问题；2、如果丢失，不重发造成该协议丢失。

因此，在应用层和传输层之间，增加了rdt session协议，来对数据进行缓存和确认，当断线重连后，通过检查和重发session中缓存的数据，来保证数据不丢失，不重复，类似kafka消息的Exactly once语义。经过测试，客户端基本可以实现无感重连


# 基本使用
----------------------------------------------

算法使用C语言编写，提供C接口，可以嵌入到自己的协议栈中。API使用不区分服务器与客户端，方法一致。
同时，仓库中包含一个使用lua实现的客户端服务器程序，来展示如何进行握手和重连，以及数据收发。


首先将模块文件链入你的项目即可，API如下：
1、创建rdt session对象，并设置参数:

```cpp
    // 初始化rdt session对象，sid为双端会话ID，user为自定义回调参数指针
    rdt_session_t *rdts = rdts_create(sid, user);
    //max_raw_snd_buf_size为对象中能够缓存的协议数据最大数量，超过该数量则该会话失效，需要重新建立。
    rdts_init(rdts, max_raw_snd_buf_size, auto_ack_size);
```
2、设置日志参数
```cpp

	void writelog(const char *log, rdt_session_t *session, void *user)
    {
        //写日志
    }


    //设置日志相关
    rdts->writelog = writelog;
    rdts->logmask = RDTS_LOG_DEBUG
```

3、握手成功后，发送数据
```cpp
    //将上层协议数据输入, rdt session会将其处理成rdt packet
    int n = rdts_send(rdts, buf, len);

    //将rdt session中的packet获取，由自己负责传输
    uint32_t len = rdts_get_snd_buf_length(rdts);
    const char *data = rdts_pullup_snd_buf(rdts);

    //将已经发送的数据从rdt session中删除
    rdts_drain_snd_buf(rdts, len);

```

4、握手成功后，接收数据
```cpp
    //输入一个下次协议数据包
    int n = rdts_input(rdts, buf, len);

    //将通过rdt session解包后的数据获取，这就是上层协议栈原始数据
    uint32_t len = rdts_get_raw_rcv_buf_length(rdts);
    const char *data = rdts_pullup_raw_rcv_buf(rdts);

    //上次协议栈处理data之后，将其删除
    rdts_drain_raw_rcv_buf(rdts, len);
```

5、rdt session重连，双端操作一致。
```cpp

    //重连回调函数，当rdt session完成双端协议数据握手之后，会回调该函数
    void on_ack(uint64_t offset, void *userdata)
    {
        //重连逻辑可以参考rdt_manager.c中的实现
        rdt_session_t *rdts = (rdt_session_t *)session;
        if (rdts_check_needack(rdts)) {
            rdts_set_needack(rdts, RDTS_NO_ACK);
            //将对端未ack的数据重新发送
            rdts_push_raw(rdts);

            //这里通知lua层重连完成，可以根据自己项目的情况进行修改
            lua_getglobal(gL, "OnSessionReconnected");
            lua_pushinteger(gL, rdts->sid);
            lua_pcall(gL, 1, 0, 0);
        }
    }

    //当应用新建立一条连接并握手成功后，双端进行重连。
    //首先将状态设为enable以及当收到对端ack后需要回调
    rdts_set_enable(rdts, RDTS_ENABLE);
    rdts_set_needack(rdts, RDTS_ACK);
    rdts_set_onack(rdts, on_ack, (void *)rdts);

    //将snd_buf清空，由于raw_snd_buf还缓存原始协议数据，所以并不会丢失
    rdts_drain_snd_buf(rdts, rdts_get_snd_buf_length(rdts));
    //发送ack给对端，告知数据确认offset
    rdts_send_ack(rdts);

```

----------------------------------------

## rdt session握手示例

协议握手在应用层，在连接建立之后，服务器可以决定是否使用rdt session，如果选择使用，则由服务器发起握手。因此，协议握手阶段，数据是不通过rdt session传输的，它使用原始的TCP、UDP协议进行传输。待握手成功后，协议数据可以选择是通过rdt session还是原始协议进行传输，有较大的灵活性。

下面是一个lua应用的握手实例可供参考，具体的实现可以根据项目进行调整，不必拘泥于此。
握手协议参考tcp三次握手过程，从on_client_login()开始

+ client
```lua
    --客户端收到服务器命令并执行不同的操作
    local client_sid = nil
    local handshake_succ = false
    function process_cmd(cmd, msg, session_id)
        if cmd == "HANDSHAKE_1" then
            --收到服务器握手包，记录session_id，创建rdt对象
            client_sid = session_id
            send_server_with_tcp("HANDSHAKE_2")
            CLIENT.rdt_create(session_id)
            enable = true --客户端的rdt已经建立，握手成功
        else if cmd == "HANDSHAKE_3" then
            --收到服务器的最后一个握手包，确认握手成功,客户端可以通过rdt发送数据了
            handshake_succ = true
            send_server_with_rdt("protocol msg")
        end

    end

```

+ server
```lua
    --握手发起处
    --当客户端登录成功后，服务器可以选择是否发起握手
    local handshake_succ = false
    function on_client_login()
        --handshake step1:创建一个session_id并发送给客户端
        local session_id = 10000
        send_client_with_tcp("HANDSHAKE_1 " .. session_id)
    end

    --服务器收到客户端命令，进行处理
    function process_cmd(cmd, msg)
        if cmd == "HANDSHAKE_2" then
            --收到客户端的握手包，确认客户端握手成功，后续服务器可以通过rdt发送数据
            SERVER.rdt_create(session_id)
            handshake_succ = true
            send_client_with_rdt("HANDSHAKE_3")
        end
    end
```


## 重连过程
重连由客户端发起，同样需要进行握手

+ client
```lua
    --当客户端需要重连时，新建一个连接，并通过此连接进行协议握手
    local session_id = 10000
    local handshake_succ = false
    function start_reconn()
        --将当前rdt session状态设为disable
        --新建一条TCP连接，用来通信
        enable = false
        handshake_succ = false
        CLIENT.rdt_disable(session_id)
        send_server_with_tcp("RECONN_HANDSHAKE_1 " .. session_id)
    end

    function process_cmd(cmd, msg)
        if cmd == "RECONN_HANDSHAKE_2" then
            --客户端收到命令，请求rdt session重连
            --当客户端与服务器的rdt session重连完成后，会回调回lua 函数,从而整个重连过程完成。
            CLIENT.rdt_reconnect(session_id)
        end
    end


    --当重连成功后，引擎会回调该函数
    function _G.OnSessionReconnected(session_id)
        --重连成功，后续可以正常收发数据
        print("client reconnect succ: ", session_id)
        handshake_succ = true
    end

```

+server


```lua
    --服务器收到客户端命令，进行处理
    function process_cmd(cmd, msg)
        if cmd == "RECONN_HANDSHAKE_1" then
            --客户端请求重连，找到session_id
            --将session状态设为disable
            SERVER.rdt_disable(session_id)
            send_client_with_tcp("RECONN_HANDSHAKE_2")
            --请求rdt session进行重连
            SERVER.rdt_reconnect(session_id)
            --当客户端与服务器的rdt session重连完成后，会回调回lua 函数。
        end
    end

    --当重连成功后，引擎会回调该函数
    function _G.OnSessionReconnected(session_id)
        --重连成功，后续可以正常收发数据
        print("server reconnect succ: ", session_id)
        handshake_succ = true
    end

```

-------------------------------------


# 关于文档
------------------------------------

后续会持续更新文档以及补充更多示例。

# 作者
我专注于游戏服务器开发，参与过回合制端游MMORPG、多款手游项目的开发。在开发手游项目时，每个项目都需要处理断线重连问题，但当时公司基本没有一个成熟的解决方案，在参考公开的方案后，实现了该算法，并且在实际应用中，效果也非常不错。因此，后续会继续深入该问题，不断的优化实现。

欢迎贡献并持续优化。有问题请联系：zhuyankong@163.com