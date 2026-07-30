#pragma once

#ifdef __cplusplus
extern "C" {
#endif

union PluginVersion {
    struct Components {
        unsigned short build;
        unsigned short patch;
        unsigned short minor;
        unsigned short major;
    } components;
    unsigned long long raw;
};
static_assert(sizeof(PluginVersion) == 8, "PluginVersion has an unexpected size");

#define PLUG_VER(a, b, c, d) \
    {                        \
        { d, c, b, a }       \
    }

struct PluginInfo {
    enum { MaxInfoVer = 1 };
    unsigned long infoVersion;
    const char* name;
    PluginVersion version;
};
static_assert(sizeof(PluginInfo) == 24, "PluginInfo has an unexpected size");

#ifdef __cplusplus
}
#endif
