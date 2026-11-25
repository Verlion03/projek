// customskin037 - AML plugin (armeabi-v7a) untuk klien SA-MP 0.3.7 Android.
// Tujuan: klien 0.3.7 tidak crash / tidak menolak skin custom 0.3DL (id 20001-30000),
// dan (tahap 2) bisa memuat DFF/TXD custom dari cache.
//
// STATUS: semua simbol libGTASA di bawah sudah DIVERIFIKASI ada (export dinamis) di libGTASA.so
// dari APK yang kamu upload. Hook sisi libsamp.so BELUM ada (libsamp stripped, butuh offset).
// Kode ini BELUM dikompilasi / diuji di perangkat.

#include <mod/amlmod.h>
#include <mod/logger.h>
#include <mod/config.h>
#include <string>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

MYMODCFG(net.customskin.samp037.compat, SAMP037 CustomSkin Compat, 0.1.0, user)

// ---------------------------------------------------------------- tipe & konstanta
struct RwStream; struct RpClump; struct RwTexDictionary; struct CPed;

static constexpr int VANILLA_SKIN_MAX = 311;
static constexpr int CUSTOM_SKIN_MIN  = 20001;  // rentang newid skin AddCharModel 0.3DL (setahu saya)
static constexpr int CUSTOM_SKIN_MAX  = 30000;
static constexpr unsigned rwID_CLUMP = 0x10, rwID_TEXDICTIONARY = 0x16;
static constexpr int rwSTREAMFILENAME = 2, rwSTREAMREAD = 1;

struct CustomSkin
{
    int newId = 0, baseId = 0;
    std::string dff, txd;
    RpClump* clump = nullptr;           // diisi tahap 2
    RwTexDictionary* txdDict = nullptr; // diisi tahap 2
};

// ---------------------------------------------------------------- state
static void*       hGTASA = nullptr;
static std::string g_cacheDir;
static int         g_fallbackSkin = 0;
static bool        g_loadAssets = false;
static std::unordered_map<int, CustomSkin> g_skins;

// ---------------------------------------------------------------- fungsi RenderWare (diverifikasi ada)
static RwStream*        (*RwStreamOpen)(int type, int access, const void* data);
static RwStream*        (*RwStreamFindChunk)(RwStream*, unsigned type, unsigned* len, unsigned* ver);
static RpClump*         (*RpClumpStreamRead)(RwStream*);
static RwTexDictionary* (*RwTexDictionaryStreamRead)(RwStream*);
static int              (*RwStreamClose)(RwStream*, void*);

// ---------------------------------------------------------------- registry skins.ini
// Format baris:  20001=<baseSkinId>,<file.dff>,<file.txd>
static void LoadRegistry()
{
    g_skins.clear();
    std::ifstream f(g_cacheDir + "/skins.ini");
    if (!f) { logger->Info("skins.ini tidak ada, semua skin custom -> fallback %d", g_fallbackSkin); return; }
    std::string line;
    while (std::getline(f, line))
    {
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        CustomSkin s;
        s.newId = atoi(line.substr(0, eq).c_str());
        if (s.newId < CUSTOM_SKIN_MIN || s.newId > CUSTOM_SKIN_MAX) continue;
        std::stringstream ss(line.substr(eq + 1));
        std::string base;
        std::getline(ss, base, ',');  s.baseId = atoi(base.c_str());
        std::getline(ss, s.dff, ','); std::getline(ss, s.txd, ',');
        if (s.baseId < 0 || s.baseId > VANILLA_SKIN_MAX) s.baseId = g_fallbackSkin;
        g_skins[s.newId] = s;
    }
    logger->Info("registry: %d skin custom", (int)g_skins.size());
}

// Ubah id skin custom -> id vanilla yang aman. Id di luar rentang custom tidak disentuh.
static int ResolveSkin(int id)
{
    if (id < CUSTOM_SKIN_MIN || id > CUSTOM_SKIN_MAX) return id;
    auto it = g_skins.find(id);
    int out = (it != g_skins.end()) ? it->second.baseId : g_fallbackSkin;
    static int logged = 0;  // diagnosa: bukti id custom sampai ke hook GTASA di klien ini
    if (logged < 50) { ++logged; logger->Info("remap skin %d -> %d", id, out); }
    return out;
}

// ---------------------------------------------------------------- tahap 2: muat DFF/TXD ke memori
// Ini baru MEMUAT. Menempelkan clump ke satu ped (bukan ke model info bersama) belum
// diimplementasikan, karena butuh offset layout CEntity yang harus diuji di perangkat.
static bool LoadAssets(CustomSkin& s)
{
    if (s.clump && s.txdDict) return true;
    auto readOne = [&](const std::string& name, unsigned chunk) -> void*
    {
        std::string path = g_cacheDir + "/" + name;
        RwStream* st = RwStreamOpen(rwSTREAMFILENAME, rwSTREAMREAD, path.c_str());
        if (!st) return nullptr;
        void* res = nullptr;
        if (RwStreamFindChunk(st, chunk, nullptr, nullptr))
            res = (chunk == rwID_CLUMP) ? (void*)RpClumpStreamRead(st) : (void*)RwTexDictionaryStreamRead(st);
        RwStreamClose(st, nullptr);
        return res;
    };
    if (!s.txdDict) s.txdDict = (RwTexDictionary*)readOne(s.txd, rwID_TEXDICTIONARY);
    if (!s.clump)   s.clump   = (RpClump*)readOne(s.dff, rwID_CLUMP);
    return s.clump && s.txdDict;
}

// ---------------------------------------------------------------- hook libGTASA
// Aman: hanya mengganti id yang berada di rentang custom (tidak valid di engine) dengan id vanilla.
SDECL_HOOKv(CStreaming_RequestModel, int id, int flags)
{
    CStreaming_RequestModel(ResolveSkin(id), flags);
}

SDECL_HOOKv(CPed_SetModelIndex, CPed* self, unsigned idx)
{
    int resolved = ResolveSkin((int)idx);
    if (g_loadAssets && resolved != (int)idx)
    {
        auto it = g_skins.find((int)idx);
        if (it != g_skins.end()) LoadAssets(it->second);
    }
    CPed_SetModelIndex(self, (unsigned)resolved);
}

// ---------------------------------------------------------------- entry
ON_MOD_LOAD()
{
    logger->SetTag("CustomSkin037");

    hGTASA = aml->GetLibHandle("libGTASA.so");
    if (!hGTASA) { logger->Error("libGTASA.so tidak ditemukan"); return; }

    std::string defDir = std::string(aml->GetAndroidDataPath()) + "/customskin";
    g_cacheDir     = cfg->Bind("CacheDir", defDir.c_str(), "General")->GetString();
    g_fallbackSkin = cfg->Bind("FallbackSkin", 0, "General")->GetInt();
    g_loadAssets   = cfg->Bind("LoadAssets", false, "General")->GetBool(); // tahap 2, eksperimental
    cfg->Save();
    mkdir(g_cacheDir.c_str(), 0777);

    SETSYM_TO(RwStreamOpen,               hGTASA, "_Z12RwStreamOpen12RwStreamType18RwStreamAccessTypePKv");
    SETSYM_TO(RwStreamFindChunk,          hGTASA, "_Z17RwStreamFindChunkP8RwStreamjPjS1_");
    SETSYM_TO(RpClumpStreamRead,          hGTASA, "_Z17RpClumpStreamReadP8RwStream");
    SETSYM_TO(RwTexDictionaryStreamRead,  hGTASA, "_Z25RwTexDictionaryStreamReadP8RwStream");
    SETSYM_TO(RwStreamClose,              hGTASA, "_Z13RwStreamCloseP8RwStreamPv");

    LoadRegistry();

    HOOKSYM(CStreaming_RequestModel, hGTASA, "_ZN10CStreaming12RequestModelEii");
    HOOKSYM(CPed_SetModelIndex,      hGTASA, "_ZN4CPed13SetModelIndexEj");

    logger->Info("siap. cache=%s fallback=%d loadAssets=%d", g_cacheDir.c_str(), g_fallbackSkin, (int)g_loadAssets);
}
