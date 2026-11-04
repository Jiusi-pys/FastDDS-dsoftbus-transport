/* Loader metadata only. Never invoke any resolved Runtime, Factory or SDK API. */
#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int StockCanonicalFile(const char* input, char output[PATH_MAX])
{
    struct stat info;
    if (!input || input[0] != '/' || !realpath(input, output) ||
            stat(output, &info) != 0 || !S_ISREG(info.st_mode)) {
        fprintf(stderr, "invalid absolute file: %s errno=%d\n", input ? input : "(null)", errno);
        return 0;
    }
    return 1;
}

static int StockResolveAddress(void* module, const char* name, const char* expected)
{
    Dl_info info;
    char actual[PATH_MAX];
    (void)dlerror();
    void* address = dlsym(module, name);
    const char* error = dlerror();
    if (error || !address) {
        fprintf(stderr, "dlsym %s: %s\n", name, error ? error : "null address");
        return 0;
    }
    memset(&info, 0, sizeof(info));
    if (!dladdr(address, &info) || !info.dli_fname || !StockCanonicalFile(info.dli_fname, actual)) {
        fprintf(stderr, "provider unavailable for %s\n", name);
        return 0;
    }
    printf("RESOLVED symbol=%s provider=%s address=%p invoked=0\n", name, actual, address);
    fflush(stdout);
    if (strcmp(actual, expected) != 0) {
        fprintf(stderr, "unexpected provider for %s: expected=%s actual=%s\n", name, expected, actual);
        return 0;
    }
    return 1;
}

int main(int argc, char** argv)
{
    char product[PATH_MAX], client[PATH_MAX];
    static const char* socket_symbols[] = {"Socket", "Listen", "Bind", "SendBytes", "Shutdown"};
    if (argc < 5) {
        fprintf(stderr, "usage: %s /absolute/product.so /absolute/libsoftbus_client.z.so current_Runtime_symbol current_Bootstrap_symbol [product_symbols...]\n", argv[0]);
        return 2;
    }
    if (!StockCanonicalFile(argv[1], product) || !StockCanonicalFile(argv[2], client)) return 2;
    void* module = dlopen(product, RTLD_NOW | RTLD_LOCAL);
    if (!module) {
        const char* error = dlerror();
        fprintf(stderr, "dlopen: %s\n", error ? error : "unknown loader error");
        return 1;
    }
    printf("LOADED product=%s mode=RTLD_NOW_LOCAL runtime_constructed=0 explicit_sdk_calls=0\n", product);
    fflush(stdout);
    int ok = 1;
    for (size_t i = 0; i < sizeof(socket_symbols) / sizeof(socket_symbols[0]); ++i)
        if (!StockResolveAddress(module, socket_symbols[i], client)) ok = 0;
    for (int i = 3; i < argc; ++i)
        if (!StockResolveAddress(module, argv[i], product)) ok = 0;
    const int close_result = dlclose(module);
    printf("DLCLOSE result=%d metadata_only=1 SDK_COMMUNICATION=NOT_TESTED\n", close_result);
    fflush(stdout);
    if (close_result != 0) {
        const char* error = dlerror();
        fprintf(stderr, "dlclose: %s\n", error ? error : "unknown loader error");
        ok = 0;
    }
    return ok ? 0 : 1;
}
