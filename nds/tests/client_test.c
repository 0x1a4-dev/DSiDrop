#include "client.h"
#include <assert.h>
#include <string.h>

static bool parseClient(const char *text, ClientReply *reply) {
    char buffer[512]; snprintf(buffer, sizeof(buffer), "%s", text); return clientParse(buffer, reply);
}

int runClientTests(void) {
    ClientReply reply;
    assert(parseClient("RFCLIENT1\t3000\t-\t-\t-\t-\t0\n", &reply));
    assert(!*reply.task.task && reply.interval == 3000);
    const char *valid = "RFCLIENT1\t3000\t0123456789abcdef0123456789abcdef\tnds\taaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\tHomebrew Adventure\t16384\n";
    assert(parseClient(valid, &reply));
    assert(reply.task.game.system == SYSTEM_NDS && reply.task.game.size == 16384);
    assert(!strcmp(reply.task.game.title, "Homebrew Adventure"));
    assert(parseClient("RFCLIENT1\t3000\t0123456789abcdef0123456789abcdef\tothers\taaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\tMy upload\t16384\n", &reply));
    assert(reply.task.game.system == SYSTEM_OTHERS);
    assert(parseClient("RFCLIENT1\t3000\t0123456789abcdef0123456789abcdef\tgba\t1234abcd\tGBA Adventure\t16384\n", &reply));
    assert(reply.task.game.system == SYSTEM_GBA);
    assert(!parseClient("RFCLIENT1\t0\t-\t-\t-\t-\t0\n", &reply));
    assert(!parseClient("RFCLIENT1\t3000\t-\tnds\t-\t-\t0\n", &reply));
    assert(!parseClient("RFCLIENT1\t3000\tbad\tgba\t1234abcd\tGame\t16384\n", &reply));
    assert(!parseClient("RFCLIENT1\t3000\t0123456789abcdef0123456789abcdef\tnds\t1234abcd\tGame\t16384\n", &reply));
    assert(!parseClient("RFCLIENT1\t3000\t0123456789abcdef0123456789abcdef\tnes\t1234abcd\tGame\t16384\n", &reply));
    assert(!parseClient("RFCLIENT1\t3000\t-\t-\t-\t-\t0\nextra", &reply));
    assert(parseClient(valid, &reply));
    Settings options = { .host = "192.168.1.10", .port = 3000 };
    static char memory[1024];
    for (unsigned status = CLIENT_DOWNLOADING; status <= CLIENT_CANCELED; status++) {
        memset(memory, 0, sizeof(memory));
        FILE *file = fmemopen(memory, sizeof(memory), "w+"); assert(file);
        reply.task.status = status;
        assert(clientTaskWrite(file, &reply.task, &options, "001122334455"));
        long size = ftell(file); fclose(file);
        file = fmemopen(memory, size, "r"); assert(file);
        ClientTask restored;
        assert(clientTaskRead(file, &restored, &options, "001122334455"));
        assert(restored.status == status && !strcmp(restored.task, reply.task.task)); fclose(file);
        file = fmemopen(memory, size, "r"); assert(file);
        assert(!clientTaskRead(file, &restored, &options, "001122334466")); fclose(file);
        Settings other = options; other.port++;
        file = fmemopen(memory, size, "r"); assert(file);
        assert(!clientTaskRead(file, &restored, &other, "001122334455")); fclose(file);
        memory[size - 3] ^= 1;
        file = fmemopen(memory, size, "r"); assert(file);
        assert(!clientTaskRead(file, &restored, &options, "001122334455")); fclose(file);
    }
    puts("Client protocol/state: three systems, bounded IDs, MAC/server binding, durable completion/cancel/error and CRC validation passed");
    return 0;
}
