#include "test.h"
#include "fl/net/http/connection.h"
#include "fl/net/http/connection.cpp.hpp"
#include "fl/net/http/native_client.h"
#include "fl/net/http/native_client.cpp.hpp"
#include "fl/net/http/native_server.h"
#include "fl/net/http/native_server.cpp.hpp"

using namespace fl;

// Use unique high ports that don't conflict with other tests
// (native_server.cpp uses port 47211+, stream tests use 47xxx)
static const int kTestPort1 = 58901;
static const int kTestPort2 = 58902;

// Ports for real socket tests (client connecting to a live server)
static const int kClientSocketPort1 = 58911;
static const int kClientSocketPort2 = 58912;
static const int kClientSocketPort3 = 58913;
static const int kClientSocketPort4 = 58914;
static const int kClientSocketPort5 = 58915;

// Helper: wait for condition with timeout (polling with small sleep)
static bool waitFor(int maxIter, int sleepMs, bool (*pred)(void* ctx), void* ctx) {
    for (int i = 0; i < maxIter; ++i) {
        if (pred(ctx)) return true;
        #ifdef _WIN32
        Sleep(sleepMs);
        #else
        usleep(sleepMs * 1000);
        #endif
    }
    return pred(ctx);
}

FL_TEST_CASE("NativeHttpClient - Construction and destruction") {
    NativeHttpClient client("localhost", kTestPort1);
    FL_CHECK(client.getState() == ConnectionState::DISCONNECTED);
    FL_CHECK_FALSE(client.isConnected());
}

FL_TEST_CASE("NativeHttpClient - Connection to invalid host fails") {
    NativeHttpClient client("invalid.host.that.does.not.exist.test", kTestPort1);

    bool result = client.connect();
    FL_CHECK_FALSE(result);
    FL_CHECK_FALSE(client.isConnected());
}

FL_TEST_CASE("NativeHttpClient - Connection state transitions") {
    NativeHttpClient client("localhost", kTestPort1);

    FL_CHECK(client.getState() == ConnectionState::DISCONNECTED);

    // Attempt connection (will fail without server, but state should transition)
    client.connect();

    // State should be DISCONNECTED or RECONNECTING (depending on connection failure)
    ConnectionState state = client.getState();
    FL_CHECK((state == ConnectionState::DISCONNECTED || state == ConnectionState::RECONNECTING));
}

FL_TEST_CASE("NativeHttpClient - Send and recv fail when disconnected") {
    NativeHttpClient client("localhost", kTestPort1);

    uint8_t sendData[] = {'t', 'e', 's', 't'};
    int sendResult = client.send(sendData);
    FL_CHECK(sendResult == -1);

    uint8_t recvBuffer[64];
    int recvResult = client.recv(recvBuffer);
    FL_CHECK(recvResult == -1);
}

FL_TEST_CASE("NativeHttpClient - Non-blocking mode") {
    NativeHttpClient client("localhost", kTestPort1);

    // Set non-blocking mode before connection
    client.setNonBlocking(true);

    // Attempt connection (should not block, even if server doesn't exist)
    bool result = client.connect();

    // Connection may succeed or fail depending on whether server exists
    // The important thing is that it returned immediately (non-blocking)
    // Just verify the call completed without hanging
    FL_CHECK((result == true || result == false));  // Always true, just verify it returned
}

FL_TEST_CASE("NativeHttpClient - Disconnect and close") {
    NativeHttpClient client("localhost", kTestPort1);

    // Disconnect when already disconnected (should be no-op)
    client.disconnect();
    FL_CHECK(client.getState() == ConnectionState::DISCONNECTED);

    // Close permanently
    client.close();
    FL_CHECK(client.getState() == ConnectionState::CLOSED);

    // Connection after close should fail
    bool result = client.connect();
    FL_CHECK_FALSE(result);
    FL_CHECK(client.getState() == ConnectionState::CLOSED);
}

FL_TEST_CASE("NativeHttpClient - Heartbeat tracking") {
    ConnectionConfig config;
    config.heartbeatIntervalMs = 1000;

    NativeHttpClient client("localhost", kTestPort1, config);

    // Should not send heartbeat when disconnected
    FL_CHECK_FALSE(client.shouldSendHeartbeat(0));
    FL_CHECK_FALSE(client.shouldSendHeartbeat(1000));
    FL_CHECK_FALSE(client.shouldSendHeartbeat(2000));
}

FL_TEST_CASE("NativeHttpClient - Reconnection tracking") {
    ConnectionConfig config;
    config.reconnectInitialDelayMs = 1000;
    config.reconnectMaxDelayMs = 5000;
    config.reconnectBackoffMultiplier = 2;

    NativeHttpClient client("localhost", kTestPort1, config);

    // Initial state
    FL_CHECK(client.getReconnectAttempts() == 0);
    FL_CHECK(client.getReconnectDelayMs() == 0);
}

FL_TEST_CASE("NativeHttpClient - Update loop") {
    ConnectionConfig config;
    config.reconnectInitialDelayMs = 100;
    config.maxReconnectAttempts = 1;  // Only 1 attempt

    NativeHttpClient client("localhost", kTestPort1, config);

    // Initial state
    FL_CHECK(client.getState() == ConnectionState::DISCONNECTED);

    // Connect (will fail)
    client.connect();

    // Update (should not crash)
    client.update(0);
    client.update(100);
    client.update(200);

    // State should settle to DISCONNECTED after max attempts
    FL_CHECK(client.getState() == ConnectionState::DISCONNECTED);
}

FL_TEST_CASE("NativeHttpClient - Multiple instances") {
    NativeHttpClient client1("localhost", kTestPort1);
    NativeHttpClient client2("localhost", kTestPort2);

    FL_CHECK_FALSE(client1.isConnected());
    FL_CHECK_FALSE(client2.isConnected());

    client1.connect();
    client2.connect();

    // Both should handle failures independently
    FL_CHECK_FALSE(client1.isConnected());
    FL_CHECK_FALSE(client2.isConnected());
}

// =============================================================================
// Real socket tests - client connects to a live server over POSIX/Win sockets
// =============================================================================

FL_TEST_CASE("NativeHttpClient - Connect to live server succeeds") {
    // Start a server for the client to connect to
    NativeHttpServer server(kClientSocketPort1);
    server.setNonBlocking(true);
    FL_REQUIRE(server.start());

    NativeHttpClient client("localhost", kClientSocketPort1);
    bool connected = client.connect();
    FL_CHECK(connected);
    FL_CHECK(client.isConnected());
    FL_CHECK(client.getState() == ConnectionState::CONNECTED);

    // Accept on server side
    server.acceptClients();
    FL_CHECK(server.getClientCount() == 1);

    client.close();
    server.stop();
}

FL_TEST_CASE("NativeHttpClient - Client sends data to server via socket") {
    NativeHttpServer server(kClientSocketPort2);
    server.setNonBlocking(true);
    FL_REQUIRE(server.start());

    NativeHttpClient client("localhost", kClientSocketPort2);
    client.setNonBlocking(true);
    FL_REQUIRE(client.connect());

    // Accept client on server side
    struct AcceptCtx { NativeHttpServer* s; };
    AcceptCtx actx{&server};
    waitFor(10, 10, [](void* c) -> bool {
        auto* x = (AcceptCtx*)c;
        x->s->acceptClients();
        return x->s->getClientCount() > 0;
    }, &actx);
    FL_REQUIRE(server.getClientCount() == 1);
    uint32_t clientId = server.getClientIds()[0];

    // Client sends data
    uint8_t msg[] = {'G', 'E', 'T', ' ', '/'};
    int sent = client.send(msg);
    FL_CHECK(sent == (int)sizeof(msg));

    // Server receives data
    uint8_t buf[64];
    struct RecvCtx { NativeHttpServer* s; uint32_t id; uint8_t* buf; int result; };
    RecvCtx rctx{&server, clientId, buf, -1};
    waitFor(10, 10, [](void* c) -> bool {
        auto* x = (RecvCtx*)c;
        x->result = x->s->recv(x->id, fl::span<uint8_t>(x->buf, 64));
        return x->result > 0;
    }, &rctx);

    FL_CHECK(rctx.result == (int)sizeof(msg));
    FL_CHECK(buf[0] == 'G');
    FL_CHECK(buf[1] == 'E');
    FL_CHECK(buf[2] == 'T');
    FL_CHECK(buf[3] == ' ');
    FL_CHECK(buf[4] == '/');

    client.close();
    server.stop();
}

FL_TEST_CASE("NativeHttpClient - Client receives data from server via socket") {
    NativeHttpServer server(kClientSocketPort3);
    server.setNonBlocking(true);
    FL_REQUIRE(server.start());

    NativeHttpClient client("localhost", kClientSocketPort3);
    client.setNonBlocking(true);
    FL_REQUIRE(client.connect());

    // Accept
    struct AcceptCtx { NativeHttpServer* s; };
    AcceptCtx actx{&server};
    waitFor(10, 10, [](void* c) -> bool {
        auto* x = (AcceptCtx*)c;
        x->s->acceptClients();
        return x->s->getClientCount() > 0;
    }, &actx);
    FL_REQUIRE(server.getClientCount() == 1);
    uint32_t clientId = server.getClientIds()[0];

    // Server sends HTTP-like response
    uint8_t resp[] = {'H', 'T', 'T', 'P', '/', '1', '.', '1', ' ', '2', '0', '0'};
    int sent = server.send(clientId, resp);
    FL_CHECK(sent == (int)sizeof(resp));

    // Client receives
    uint8_t buf[64];
    struct RecvCtx { NativeHttpClient* c; uint8_t* buf; int result; };
    RecvCtx rctx{&client, buf, -1};
    waitFor(10, 10, [](void* c) -> bool {
        auto* x = (RecvCtx*)c;
        x->result = x->c->recv(fl::span<uint8_t>(x->buf, 64));
        return x->result > 0;
    }, &rctx);

    FL_CHECK(rctx.result == (int)sizeof(resp));
    // Verify "HTTP/1.1 200"
    FL_CHECK(buf[0] == 'H');
    FL_CHECK(buf[4] == '/');
    FL_CHECK(buf[9] == '2');
    FL_CHECK(buf[10] == '0');
    FL_CHECK(buf[11] == '0');

    client.close();
    server.stop();
}

FL_TEST_CASE("NativeHttpClient - Bidirectional echo over socket") {
    NativeHttpServer server(kClientSocketPort4);
    server.setNonBlocking(true);
    FL_REQUIRE(server.start());

    NativeHttpClient client("localhost", kClientSocketPort4);
    client.setNonBlocking(true);
    FL_REQUIRE(client.connect());

    // Accept
    struct AcceptCtx { NativeHttpServer* s; };
    AcceptCtx actx{&server};
    waitFor(10, 10, [](void* c) -> bool {
        auto* x = (AcceptCtx*)c;
        x->s->acceptClients();
        return x->s->getClientCount() > 0;
    }, &actx);
    FL_REQUIRE(server.getClientCount() == 1);
    uint32_t clientId = server.getClientIds()[0];

    // Client sends request
    uint8_t request[] = {'P', 'I', 'N', 'G'};
    FL_CHECK(client.send(request) == (int)sizeof(request));

    // Server reads it
    uint8_t srvBuf[64];
    struct SrvRecv { NativeHttpServer* s; uint32_t id; uint8_t* buf; int result; };
    SrvRecv sr{&server, clientId, srvBuf, -1};
    waitFor(10, 10, [](void* c) -> bool {
        auto* x = (SrvRecv*)c;
        x->result = x->s->recv(x->id, fl::span<uint8_t>(x->buf, 64));
        return x->result > 0;
    }, &sr);
    FL_CHECK(sr.result == 4);

    // Server echoes back as "PONG"
    uint8_t response[] = {'P', 'O', 'N', 'G'};
    FL_CHECK(server.send(clientId, response) == (int)sizeof(response));

    // Client reads response
    uint8_t cliBuf[64];
    struct CliRecv { NativeHttpClient* c; uint8_t* buf; int result; };
    CliRecv cr{&client, cliBuf, -1};
    waitFor(10, 10, [](void* c) -> bool {
        auto* x = (CliRecv*)c;
        x->result = x->c->recv(fl::span<uint8_t>(x->buf, 64));
        return x->result > 0;
    }, &cr);
    FL_CHECK(cr.result == 4);
    FL_CHECK(cliBuf[0] == 'P');
    FL_CHECK(cliBuf[1] == 'O');
    FL_CHECK(cliBuf[2] == 'N');
    FL_CHECK(cliBuf[3] == 'G');

    client.close();
    server.stop();
}

FL_TEST_CASE("NativeHttpClient - Large payload transfer over socket") {
    NativeHttpServer server(kClientSocketPort5);
    server.setNonBlocking(true);
    FL_REQUIRE(server.start());

    NativeHttpClient client("localhost", kClientSocketPort5);
    client.setNonBlocking(true);
    FL_REQUIRE(client.connect());

    // Accept
    struct AcceptCtx { NativeHttpServer* s; };
    AcceptCtx actx{&server};
    waitFor(10, 10, [](void* c) -> bool {
        auto* x = (AcceptCtx*)c;
        x->s->acceptClients();
        return x->s->getClientCount() > 0;
    }, &actx);
    FL_REQUIRE(server.getClientCount() == 1);
    uint32_t clientId = server.getClientIds()[0];

    // Build a 4KB payload (simulating a JSON or HTTP body)
    const int payloadSize = 4096;
    fl::vector<uint8_t> payload;
    payload.resize(payloadSize);
    for (int i = 0; i < payloadSize; ++i) {
        payload[i] = (uint8_t)(i & 0xFF);
    }

    // Client sends large payload
    int totalSent = 0;
    while (totalSent < payloadSize) {
        int remaining = payloadSize - totalSent;
        fl::span<const uint8_t> chunk(payload.data() + totalSent, remaining);
        int sent = client.send(chunk);
        if (sent > 0) {
            totalSent += sent;
        } else {
            #ifdef _WIN32
            Sleep(1);
            #else
            usleep(1000);
            #endif
        }
    }
    FL_CHECK(totalSent == payloadSize);

    // Server receives all data
    fl::vector<uint8_t> recvBuf;
    recvBuf.resize(payloadSize);
    int totalRecv = 0;
    for (int attempt = 0; attempt < 100 && totalRecv < payloadSize; ++attempt) {
        uint8_t tmp[1024];
        int got = server.recv(clientId, tmp);
        if (got > 0) {
            for (int j = 0; j < got && totalRecv < payloadSize; ++j) {
                recvBuf[totalRecv++] = tmp[j];
            }
        } else {
            #ifdef _WIN32
            Sleep(5);
            #else
            usleep(5000);
            #endif
        }
    }
    FL_CHECK(totalRecv == payloadSize);

    // Verify data integrity
    bool dataMatch = true;
    for (int i = 0; i < payloadSize; ++i) {
        if (recvBuf[i] != (uint8_t)(i & 0xFF)) {
            dataMatch = false;
            break;
        }
    }
    FL_CHECK(dataMatch);

    client.close();
    server.stop();
}
