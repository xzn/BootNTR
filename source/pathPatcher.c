#include "main.h"
#include "config.h"

extern ntrConfig_t      *ntrConfig;
extern bootNtrConfig_t  *bnConfig;
extern char             *g_primary_error;
extern char             *g_secondary_error;
extern char             *g_third_error;

#define RELOC_COUNT     9
#define BASE            0x100100

enum
{
    BINARY = 0,
    PLUGIN,
    DEBUG,
    KERNEL,
    FS,
    PM,
    SM,
    HOMEMENU,
    ARM
};

static const char  *originalPath[RELOC_COUNT] =
{
    "/ntr.bin",
    "/plugin/%s",
    "/debug.flag",
    "/axiwram.dmp",
    "/pid0.dmp",
    "/pid2.dmp",
    "/pid3.dmp",
    "/pidf.dmp",
    "/arm11.bin"
};

static const char *fixedName[RELOC_COUNT] =
{
    "",
    "%s",
    "",
    "Kernel.dmp",
    "FS.dmp",
    "PM.dmp",
    "SM.dmp",
    "HomeMenu.dmp",
    "arm11.dmp"
};

static char fixedPath[RELOC_COUNT][0x100] = { 0 };

static const char *ntrVersionStrings[5] =
{
    // "ntr_3_2.bin",
    // "ntr_3_3.bin",
    "",
    "",

    // "ntr.o3ds.bin",
    "ntr.n3ds.bin",
    "ntr.n3ds.hr.bin"
};

const char *outNtrVersionStrings[4] =
{
    // "ntr_3_2.bin",
    // "ntr_3_3.bin",
    "",
    "",

    "ntr_3_6.bin",
    "ntr_3_6_hr.bin"
};

// There was a bug in libctu where DMAState was read as a u32 instead of a u8.
// In order to fix the NTR bins, we have to patch those bugs by changing a LDR to LDRB
static void fixDMAStateBug(u32* start, size_t size) {
    const u8 ldrDMAStatePat1[] = {0x14, 0x30, 0x9D, 0xE5, 0x00, 0x00, 0x52, 0xE3}; // NTR 3.2, 3.3, O3DS 3.6
    const u8 ldrDMAStatePat2[] = {0x14, 0x20, 0x9D, 0xE5, 0x5C, 0x30, 0x93, 0xE5}; // NTR N3DS 3.6
    u32* end = start + size/4 - sizeof(ldrDMAStatePat1)/4;
    while(start != end) {
        if (memcmp(start, ldrDMAStatePat1, sizeof(ldrDMAStatePat1)) == 0 || memcmp(start, ldrDMAStatePat2, sizeof(ldrDMAStatePat2)) == 0) {
            ((u8*)start)[2] = 0xDD; // Convert LDR to LDRB
            break;
        }
        start++;
    }
}

static void patchBinary(u8 *mem, int size)
{
    int     i;
    int     expand;
    char    *str;
    u32     offset;
    u32     *patchMe;
    u32     patchOffset;
    u32     *patchRel;
    u32     strlength;

    expand = 0;
    for (i = 0; i < RELOC_COUNT; i++)
    {
        // Skip auto enable debugger on O3DS
        if (i == 2 && !bnConfig->isNew3DS)
            continue;

        str = (char *)originalPath[i];
        strlength = strlen(str);
        offset = memfind(mem, size, str, strlength);
        if (offset == 0)
        {
            if (bnConfig->isDebug)
            {
                newAppTop(DEFAULT_COLOR, TINY, "Not found \"%s\".", str);
                updateUI();
            }
            continue;
        }

        // Clear string data
        memset(&mem[offset], 0, strlength);

        // Rebase pointer relative to NTRs base.
        offset += BASE;
        patchMe = (u32 *)memfind(mem, size, (u8 *)&offset, 4); // Find xref
        if (patchMe == 0)
        {
            if (bnConfig->isDebug)
            {
                newAppTop(DEFAULT_COLOR, TINY, "Pointer for \"%s\"", str);
                newAppTop(DEFAULT_COLOR, TINY, "is missing!Aborting.\n");
                updateUI();
            }
            break;
        }

        // New offset is end + patch count * buffer
        patchOffset = size + (0x100 * expand);
        strcpy((void *)&mem[patchOffset], fixedPath[i]);

        expand += 1;

        patchRel = (u32 *)(&mem[(u32)patchMe]);
        // Rebase new pointer
        *patchRel = patchOffset + BASE;
    }
}

Result  loadAndPatch(version_t version)
{
    FILE    *ntr;
    int     size;
    int     newSize;
    char    *binPath;
    char    *plgPath;
    char    inPath[0x100];
    char    outPath[0x100];
    u8      *mem;
    bool    isNew3DS = bnConfig->isNew3DS;

    if (!isNew3DS) {
        clearTop(1);
        newAppTop(COLOR_SALMON, SKINNY, "Support New 3DS only (Old 3DS detected).");
        goto error;
    }

    binPath = bnConfig->config->binariesPath;
    plgPath = bnConfig->config->pluginPath;

    strJoin(inPath, "romfs:/", ntrVersionStrings[version/* + (version >= SELECT_V36 && isNew3DS)*/]);

    if (!strncmp("sdmc:", binPath, 5)) binPath += 5;
    if (!strncmp("sdmc:", plgPath, 5)) plgPath += 5;

    // if (version != V32)
    // {
        strJoin(fixedPath[PLUGIN], plgPath, fixedName[PLUGIN]);
        strJoin(outPath, binPath, outNtrVersionStrings[version]);
        strJoin(fixedPath[BINARY], binPath, outNtrVersionStrings[version]);
        strJoin(fixedPath[DEBUG], binPath, outNtrVersionStrings[version]);
        strJoin(fixedPath[KERNEL], binPath, fixedName[KERNEL]);
        strJoin(fixedPath[FS], binPath, fixedName[FS]);
        strJoin(fixedPath[PM], binPath, fixedName[PM]);
        strJoin(fixedPath[SM], binPath, fixedName[SM]);
        strJoin(fixedPath[HOMEMENU], binPath, fixedName[HOMEMENU]);
        strJoin(fixedPath[ARM], binPath, fixedName[ARM]);
    // }
    // // 3.2
    // else
    // {
    //     strcpy(outPath, originalPath[BINARY]);
    // }
    ntr = fopen(inPath, "rb");
    if (!ntr) {
        newAppTop(COLOR_SALMON, SKINNY, "loadAndPatch fopen inPath \"%s\" error.", inPath);
        goto error;
    }
    fseek(ntr, 0, SEEK_END);
    size = ftell(ntr);
    rewind(ntr);
    // newSize = (version == V32) ? size : size + (RELOC_COUNT * 0x100);
    newSize = size + (RELOC_COUNT * 0x100);
    mem = (u8 *)malloc(newSize);
    if (!mem) {
        newAppTop(COLOR_SALMON, SKINNY, "loadAndPatch malloc error.");
        goto error;
    }
    memset(mem, 0, newSize);
    fread(mem, size, 1, ntr);
    fclose(ntr);
    svcFlushProcessDataCache(CURRENT_PROCESS_HANDLE, (u32)mem, newSize);
    fixDMAStateBug((u32*)mem, size);
    // if (version != V32)
        patchBinary(mem, size);
    ntr = fopen(outPath, "wb");
    if (!ntr) {
        newAppTop(COLOR_SALMON, SKINNY, "loadAndPatch fopen outPath \"%s\" error.", outPath);
        goto error;
    }
    fwrite(mem, newSize, 1, ntr);
    fclose(ntr);
    free(mem);

    return(0);
error:
    return(RESULT_ERROR);
}
