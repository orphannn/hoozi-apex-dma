// heirloom_anim_reference.cpp
// -----------------------------------------------------------------------------
// Illustrative, sanitized reference for the Apex Legends heirloom-animation
// external-DMA ceiling described in the accompanying article. It is not a
// working build — it is the mechanism, distilled: cache_A pollution to swap
// the mesh, plus a per-frame FSM that force-writes viewmodel seq index and
// startTime to the two viewmodel (hands / melee-slot) instances.
//
// External DMA API assumed (replace with your own reader):
//
//   namespace dma {
//     uint64_t base;                                      // r5apex.exe base
//     template<class T> T       Read(uint64_t addr, bool cache = false);
//     template<class T> void    Write(uint64_t addr, T v);
//     void Read(uint64_t addr, void* dst, size_t n, bool cache = false);
//
//     using ScatterHandle = void*;
//     ScatterHandle CreateScatterHandle();
//     void          AddScatterReadRequest(ScatterHandle, uint64_t addr,
//                                         void* dst, size_t n);
//     void          ExecuteReadScatter(ScatterHandle);
//     void          CloseScatterHandle(ScatterHandle);
//   }
//
// Everything below assumes that API and standard C++20.
// -----------------------------------------------------------------------------

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace dma {
    extern uint64_t base;
    template <class T> T Read(uint64_t addr, bool cache = false);
    template <class T> void Write(uint64_t addr, T v);
    void Read(uint64_t addr, void* dst, size_t n, bool cache = false);
    using ScatterHandle = void*;
    ScatterHandle CreateScatterHandle();
    void AddScatterReadRequest(ScatterHandle, uint64_t addr, void* dst, size_t n);
    void ExecuteReadScatter(ScatterHandle);
    void CloseScatterHandle(ScatterHandle);
}

// -----------------------------------------------------------------------------
// Offsets — game-version specific. Refresh via your dumper each patch.
// -----------------------------------------------------------------------------
namespace off {
    constexpr uint64_t client_state_ptr                     = 0x0;   // ClientState global
    constexpr uint64_t entity_list                          = 0x0;   // cl_entitylist[]
    constexpr uint64_t local_view_models                    = 0x0;   // player->m_hViewModel[0]
    constexpr uint64_t weapon_name_index                    = 0x0;   // weap->m_iName idx
    constexpr uint64_t studio_hdr                           = 0x1000; // vm+0x1000 CStudioHdr*
    constexpr uint64_t viewmodel_model_index                = 0xD8;  // vm+0xD8 modelIndex
    constexpr uint64_t viewmodel_current_frame_anim_seq     = 0xE48; // vm+0xE48 animSeq
    constexpr uint64_t viewmodel_seq_finished               = 0xE34; // vm+0xE34 m_bSequenceFinished
    constexpr uint64_t viewmodel_anim_start_time            = 0xE4C; // vm+0xE4C animStartTime
    constexpr uint64_t in_speed                             = 0x0;   // kbutton_t in_speed
    constexpr uint64_t kbutton_state                        = 0x8;   // kbutton_t.state
    // modelinfo cache table (cache_A): array of {flags, model_t, ...}
    constexpr uint64_t modelinfo_cache_base                 = 0x0;
    constexpr uint64_t modelinfo_cache_stride               = 0x10;
    constexpr uint64_t modelinfo_cache_model_t_off          = 0x8;
    // studio fields
    constexpr uint64_t studio_cstudiohdr_to_studiohdr       = 0x8;
    constexpr uint64_t studio_cstudiohdr_to_cache           = 0x10;
    constexpr uint64_t studio_cache_numseq                  = 0x2;
    constexpr uint64_t studio_cache_seqdesc_table           = 0x10;
    constexpr uint64_t studio_cache_entry_stride            = 0x10;
    constexpr uint64_t studio_cache_entry_seqdesc           = 0x8;
    constexpr uint64_t studiohdr_numseq                     = 0x0;
    constexpr uint64_t studiohdr_seqindex                   = 0x0;
    constexpr uint64_t mstudioseqdesc_stride                = 0x74;
    constexpr uint64_t mstudioseqdesc_szactivitynameidx     = 0x0;
    constexpr uint64_t mstudioseqdesc_szlabelindex          = 0x0;
}

// -----------------------------------------------------------------------------
// FNV-1a 32 matches the engine's activity-name hash (0x811c9dc5 / 0x01000193).
// -----------------------------------------------------------------------------
constexpr uint32_t fnv1a(const char* s) {
    uint32_t h = 0x811c9dc5u;
    for (; *s; ++s) { h ^= (uint8_t)*s; h *= 0x01000193u; }
    return h;
}

// All "u16 packed relative offset" fields in studio data share this decode:
//   real = (w & 0xFFFE) << (4 * (w & 1))
// When the low bit is 0 the naive `w` happens to work — this is why some
// code bases get away with using it raw. Low-bit-1 models silently read
// bogus addresses. Always use this helper.
static inline uint64_t unpack_studio_rel(uint16_t w) {
    return (uint64_t)(w & 0xFFFE) << (4 * (w & 1));
}

// -----------------------------------------------------------------------------
// modelprecache stringtable → precache idx resolver. First lookup triggers a
// full scan (~5000 rows, ~3 s). Later lookups are O(1). We keep a highwater
// mark so mid-match additions trigger an incremental rescan.
// -----------------------------------------------------------------------------
class PathResolver {
public:
    uint32_t lookup(const char* path) {
        if (!path) return 0;
        if (!scanned_) scan();
        if (auto it = cache_.find(path); it != cache_.end()) return it->second;
        if (scanned_) {
            const uint16_t cur = precache_used();
            if (cur > last_scanned_used_) {
                scan();
                if (auto it = cache_.find(path); it != cache_.end()) return it->second;
            }
        }
        return 0;
    }
    void invalidate() { scanned_ = false; last_scanned_used_ = 0; cache_.clear(); }

private:
    static constexpr uint64_t kModelprecacheOff = 0x29330;
    static constexpr uint64_t kItemSize         = 0x48;

    bool     scanned_          = false;
    uint16_t last_scanned_used_ = 0;
    std::unordered_map<std::string, uint32_t> cache_;

    uint16_t precache_used() {
        const uint64_t table = dma::Read<uint64_t>(dma::base + off::client_state_ptr + kModelprecacheOff, true);
        if (!table) return 0;
        const uint64_t items = dma::Read<uint64_t>(table + 0x48, true);
        if (!items) return 0;
        return dma::Read<uint16_t>(items + 0x32, true);
    }

    void scan() {
        const uint16_t used = precache_used();
        if (!used) return;
        const uint64_t table    = dma::Read<uint64_t>(dma::base + off::client_state_ptr + kModelprecacheOff, true);
        const uint64_t items    = dma::Read<uint64_t>(table + 0x48, true);
        const uint64_t elements = dma::Read<uint64_t>(items + 0x18, true);
        if (!elements) return;
        cache_.clear();
        cache_.reserve(used);
        for (uint16_t i = 0; i < used; ++i) {
            const uint64_t str_ptr = dma::Read<uint64_t>(elements + (uint64_t)i * kItemSize + 0x10, true);
            if (!str_ptr) continue;
            char nm[160] = {};
            dma::Read(str_ptr, nm, sizeof(nm) - 1, true);
            if (nm[0]) cache_.emplace(nm, (uint32_t)i);
        }
        if (!cache_.empty()) { scanned_ = true; last_scanned_used_ = used; }
    }
};

// -----------------------------------------------------------------------------
// StudioInfo: two resolution paths for seqdesc[i].
//   cache_table path — CStudioHdr+0x10 → cache_data → seqdesc pointer table
//   fallback path    — bare studiohdr_t + packed seqindex (contiguous array)
// -----------------------------------------------------------------------------
struct StudioInfo {
    uint64_t studiohdr     = 0;
    uint64_t cache_table   = 0;
    uint64_t fallback_base = 0;
    uint32_t seqcount      = 0;
};

static StudioInfo read_studio_info(uint64_t cstudiohdr) {
    StudioInfo out{};
    if (!cstudiohdr) return out;
    out.studiohdr = dma::Read<uint64_t>(cstudiohdr + off::studio_cstudiohdr_to_studiohdr, true);
    const uint64_t cache_data = dma::Read<uint64_t>(cstudiohdr + off::studio_cstudiohdr_to_cache, true);
    if (cache_data) {
        const uint16_t n = dma::Read<uint16_t>(cache_data + off::studio_cache_numseq, true);
        const uint64_t t = dma::Read<uint64_t>(cache_data + off::studio_cache_seqdesc_table, true);
        if (n > 0 && n <= 512 && t) { out.cache_table = t; out.seqcount = n; return out; }
    }
    if (out.studiohdr) {
        const uint16_t n = dma::Read<uint16_t>(out.studiohdr + off::studiohdr_numseq, true);
        const uint16_t p = dma::Read<uint16_t>(out.studiohdr + off::studiohdr_seqindex, true);
        if (n > 0 && n <= 512 && p) { out.fallback_base = out.studiohdr + unpack_studio_rel(p); out.seqcount = n; }
    }
    return out;
}

static uint64_t resolve_seqdesc(const StudioInfo& s, uint32_t idx) {
    if (idx >= s.seqcount) return 0;
    if (s.cache_table)
        return dma::Read<uint64_t>(s.cache_table + (uint64_t)idx * off::studio_cache_entry_stride + off::studio_cache_entry_seqdesc, true);
    if (s.fallback_base)
        return s.fallback_base + (uint64_t)idx * off::mstudioseqdesc_stride;
    return 0;
}

static uint32_t read_activity_hash(const StudioInfo& s, uint32_t idx) {
    const uint64_t desc = resolve_seqdesc(s, idx);
    if (!desc) return 0;
    const uint16_t name_off = dma::Read<uint16_t>(desc + off::mstudioseqdesc_szactivitynameidx, true);
    if (!name_off) return 0;
    char buf[64] = {};
    dma::Read(desc + unpack_studio_rel(name_off), buf, sizeof(buf) - 1, true);
    return buf[0] ? fnv1a(buf) : 0;
}

// Real duration in seconds. Chain (per the engine's SequenceDuration path):
//   seqdesc+0x2A = packed anim-index table offset → animdesc
//   animdesc+0x00 = float fps, +0x08 = int numframes
//   duration = (numframes - 1) / fps
static float read_seq_duration(const StudioInfo& s, uint32_t idx) {
    const uint64_t desc = resolve_seqdesc(s, idx);
    if (!desc) return 0.0f;
    const uint16_t aii = dma::Read<uint16_t>(desc + 0x2A, true);
    if (!aii) return 0.0f;
    const uint16_t off0 = dma::Read<uint16_t>(desc + unpack_studio_rel(aii), true);
    if (!off0) return 0.0f;
    const uint64_t anim = desc + unpack_studio_rel(off0);
    const float   fps    = dma::Read<float>(anim + 0x00, true);
    const int32_t frames = dma::Read<int32_t>(anim + 0x08, true);
    if (!(fps > 1.0f) || fps > 1000.0f || frames <= 1 || frames > 100000) return 0.0f;
    return (float)(frames - 1) / fps;
}

static constexpr uint32_t kInvalidSeq = 0xFFFFFFFFu;

// -----------------------------------------------------------------------------
// Label suffix + activity double-match. Shared studios (numseq ≈ 127) include
// seq rows whose activity tag is empty; picking by label alone can land on a
// one-shot instead of the looping idle. Match label AND activity when possible.
// -----------------------------------------------------------------------------
static bool label_ends_with(const StudioInfo& s, uint32_t idx, const char* suffix) {
    const uint64_t desc = resolve_seqdesc(s, idx);
    if (!desc) return false;
    const uint16_t off_ = dma::Read<uint16_t>(desc + off::mstudioseqdesc_szlabelindex, true);
    if (!off_) return false;
    char buf[200] = {};
    dma::Read(desc + unpack_studio_rel(off_), buf, sizeof(buf) - 1, true);
    size_t len = 0; while (len < sizeof(buf) && buf[len]) ++len;
    size_t sl = 0;  while (suffix[sl]) ++sl;
    if (sl > len) return false;
    for (size_t i = 0; i < sl; ++i) if (buf[len - sl + i] != suffix[i]) return false;
    return true;
}

static uint32_t find_seq_by_label_and_activity(const StudioInfo& s,
                                               const char* const* lbls, size_t nlbl,
                                               const uint32_t* hashes, size_t nh) {
    for (uint32_t i = 0; i < s.seqcount; ++i) {
        bool lbl_ok = false;
        for (size_t k = 0; k < nlbl; ++k) if (label_ends_with(s, i, lbls[k])) { lbl_ok = true; break; }
        if (!lbl_ok) continue;
        const uint32_t h = read_activity_hash(s, i);
        if (!h) continue;
        for (size_t k = 0; k < nh; ++k) if (h == hashes[k]) return i;
    }
    return kInvalidSeq;
}

// -----------------------------------------------------------------------------
// Activity / label constant tables (subset — the full production set carries
// several sprint / crouch / jump variants per state).
// -----------------------------------------------------------------------------
static constexpr uint32_t kCandIdle[]  = { fnv1a("ACT_VM_IDLE"), fnv1a("ACT_VM_ONEHANDED_IDLE") };
static constexpr uint32_t kCandSprint[]= { fnv1a("ACT_VM_SPRINT"), fnv1a("ACT_VM_ONEHANDED_SPRINT") };
static constexpr uint32_t kCandMelee[] = { fnv1a("ACT_VM_MELEE_ATTACK2"), fnv1a("ACT_VM_MELEE_ATTACK1") };
static constexpr const char* kLblIdle[]   = { "/idle.rseq"   };
static constexpr const char* kLblSprint[] = { "/sprint.rseq" };
static constexpr const char* kLblMelee[]  = { "/melee_idle_swipe.rseq" };
static constexpr const char* kLblRaise[]  = { "/meleeraise.rseq" };

// Activities we must NOT overwrite while playing (attack / raise). Overwriting
// them mid-play cancels them; wait until seq_finished and pick up naturally.
static constexpr uint32_t kAttackHard[] = {
    fnv1a("ACT_VM_MELEE_ATTACK1"),  fnv1a("ACT_VM_MELEE_ATTACK2"),
    fnv1a("ACT_VM_MELEE_ATTACK3"),  fnv1a("ACT_VM_MELEE_HEAVY"),
};
static bool is_attack_hard(uint32_t h) { for (auto p : kAttackHard) if (p == h) return true; return false; }

// -----------------------------------------------------------------------------
// Cross-tick state
// -----------------------------------------------------------------------------
struct PollutedSlot { uint16_t idx = 0; uint64_t orig = 0; bool valid = false; };

struct VmCacheEntry {
    uint64_t vm_ptr             = 0;
    uint64_t cstudiohdr         = 0; // captured; used to detect stale/dangling vm
    uint64_t anim_block_ptr     = 0; // vm+0x110  (prev)
    uint64_t anim_block_next    = 0; // vm+0x108  (next)
    uint64_t anim_block_third   = 0; // vm+0x118  (alias of prev in practice)
    uint64_t sub_A_ptr          = 0;
    uint64_t sub_B_ptr          = 0;
    uint64_t sub_C_ptr          = 0;
};

struct SeqCache {
    uint64_t cstudiohdr = 0;
    uint32_t numseq     = 0;
    uint32_t idle       = kInvalidSeq;
    uint32_t sprint     = kInvalidSeq;
    uint32_t melee      = kInvalidSeq;
    uint32_t raise      = kInvalidSeq;
};

enum class AttackPhase : uint8_t { None, Swipe, Recovery };

struct HeirloomModel { const char* model_path; };

// -----------------------------------------------------------------------------
// The driver
// -----------------------------------------------------------------------------
class HeirloomAnim {
public:
    HeirloomAnim() = default;
    ~HeirloomAnim() { restore_cache_A(); }

    struct Input {
        uint64_t local_entity_ptr  = 0;
        uint64_t weapon_entity_ptr = 0;
        bool     is_in_game        = false;
        bool     is_dead           = false;
        uint32_t player_flags      = 0;      // FL_ONGROUND=1, FL_DUCKING=2
        int      wanted_heirloom   = -1;     // index into heirlooms_
        float    curtime           = 0.0f;
        bool     attack_down       = false;
        bool     moving            = false;
    };

    void set_heirlooms(std::vector<HeirloomModel> h) { heirlooms_ = std::move(h); }

    void tick(const Input& in);

private:
    void restore_cache_A();

    PathResolver resolver_;
    std::vector<HeirloomModel> heirlooms_;
    bool was_in_game_ = false;

    // pollution — up to two viewmodel-model-index slots (hands + melee-item)
    std::array<PollutedSlot, 2> slots_{};
    int  polluted_heir_id_ = -1;
    uint16_t last_mdlidx_ = 0;
    int  mdlidx_stable_   = 0;

    // vm cache, keyed by weapon-name-idx so hands<->melee swaps keep both hot
    std::unordered_map<uint16_t, VmCacheEntry> vm_cache_;

    // per-cstudiohdr seq resolution cache
    std::unordered_map<uint64_t, SeqCache> seq_map_;
    SeqCache seq_{};

    // attack FSM
    AttackPhase phase_ = AttackPhase::None;
    uint32_t    phase_seq_ = kInvalidSeq;
    float       phase_dur_ = 0.0f;
    std::chrono::steady_clock::time_point phase_start_{};
    bool        last_attack_ = false;
};

// -----------------------------------------------------------------------------
// Restore cache_A on shutdown / disable. Failing to do this leaves the game
// with heirloom mesh on regular weapons until process restart.
// -----------------------------------------------------------------------------
void HeirloomAnim::restore_cache_A() {
    for (auto& s : slots_) {
        if (s.valid && s.orig) {
            const uint64_t a = dma::base + off::modelinfo_cache_base
                             + (uint64_t)s.idx * off::modelinfo_cache_stride
                             + off::modelinfo_cache_model_t_off;
            dma::Write<uint64_t>(a, s.orig);
        }
        s = PollutedSlot{};
    }
    polluted_heir_id_ = -1;
    last_mdlidx_ = 0;
    mdlidx_stable_ = 0;
}

void HeirloomAnim::tick(const Input& in) {
    using namespace std::chrono;
    constexpr auto kPhaseMin = milliseconds(80);
    constexpr auto kPhaseCap = milliseconds(1500);
    constexpr auto kRecoveryHold = milliseconds(420);

    const int  heir_id = in.wanted_heirloom;
    const bool enabled = heir_id >= 0 && heir_id < (int)heirlooms_.size();

    bool any_polluted = polluted_heir_id_ != -1
                     || std::any_of(slots_.begin(), slots_.end(), [](auto& s){ return s.valid; });

    if (!enabled && !any_polluted) { was_in_game_ = false; return; }

    if (in.is_in_game && !was_in_game_) resolver_.invalidate();
    was_in_game_ = in.is_in_game;

    if (!enabled) restore_cache_A();
    if (!in.is_in_game && any_polluted) {
        for (auto& s : slots_) s = PollutedSlot{};
        polluted_heir_id_ = -1;
        seq_ = SeqCache{};
        seq_map_.clear();
        vm_cache_.clear();
    }

    if (!(in.is_in_game && enabled)) return;
    if (!in.local_entity_ptr || in.is_dead || !in.weapon_entity_ptr) return;

    // ── Stage 1: resolve viewmodel entity ────────────────────────────────
    uint32_t vm_handle = 0xFFFFFFFFu;
    {
        auto h = dma::CreateScatterHandle();
        dma::AddScatterReadRequest(h, in.local_entity_ptr + off::local_view_models,
                                   &vm_handle, sizeof(vm_handle));
        dma::ExecuteReadScatter(h);
        dma::CloseScatterHandle(h);
    }
    if (vm_handle == 0xFFFFFFFFu || vm_handle == 0u) return;
    const uint64_t vm_ptr = dma::Read<uint64_t>(dma::base + off::entity_list
                                                + ((uint64_t)(vm_handle & 0xFFFF) << 5));
    if (!vm_ptr) return;

    const uint32_t heir_idx = resolver_.lookup(heirlooms_[heir_id].model_path);
    if (!heir_idx) return;

    const uint64_t heir_cache_flags = dma::base + off::modelinfo_cache_base
                                    + (uint64_t)heir_idx * off::modelinfo_cache_stride;
    const uint64_t heir_cache_model = heir_cache_flags + off::modelinfo_cache_model_t_off;

    // ── Stage 3: bulk-read the fields the FSM needs ──────────────────────
    uint16_t weap_name_idx     = 0;
    uint32_t cache_flags       = 0;
    uint64_t cache_model_t     = 0;
    uint64_t cur_cstudiohdr    = 0;
    uint32_t vm_model_index    = 0;
    uint32_t cur_seq_e48       = 0;
    uint8_t  seq_finished      = 0;
    uint64_t anim_block_ptr    = 0;
    uint64_t anim_block_next   = 0;
    uint64_t anim_block_third  = 0;
    uint32_t in_speed_state    = 0;

    auto h3 = dma::CreateScatterHandle();
    dma::AddScatterReadRequest(h3, in.weapon_entity_ptr + off::weapon_name_index,
                               &weap_name_idx, sizeof(weap_name_idx));
    dma::AddScatterReadRequest(h3, heir_cache_flags, &cache_flags,   sizeof(cache_flags));
    dma::AddScatterReadRequest(h3, heir_cache_model, &cache_model_t, sizeof(cache_model_t));
    dma::AddScatterReadRequest(h3, vm_ptr + off::studio_hdr,
                               &cur_cstudiohdr, sizeof(cur_cstudiohdr));
    dma::AddScatterReadRequest(h3, vm_ptr + off::viewmodel_model_index,
                               &vm_model_index, sizeof(vm_model_index));
    dma::AddScatterReadRequest(h3, vm_ptr + off::viewmodel_current_frame_anim_seq,
                               &cur_seq_e48, sizeof(cur_seq_e48));
    dma::AddScatterReadRequest(h3, vm_ptr + off::viewmodel_seq_finished,
                               &seq_finished, sizeof(seq_finished));
    dma::AddScatterReadRequest(h3, vm_ptr + 0x110, &anim_block_ptr,   sizeof(anim_block_ptr));
    dma::AddScatterReadRequest(h3, vm_ptr + 0x108, &anim_block_next,  sizeof(anim_block_next));
    dma::AddScatterReadRequest(h3, vm_ptr + 0x118, &anim_block_third, sizeof(anim_block_third));
    dma::AddScatterReadRequest(h3, dma::base + off::in_speed + off::kbutton_state,
                               &in_speed_state, sizeof(in_speed_state));
    dma::ExecuteReadScatter(h3);
    dma::CloseScatterHandle(h3);

    // Caller decides whether the wielded weapon is one of the melee slots we
    // want to hijack. Weapon-name index changes between patches; do this by
    // the game's own name table (fists / melee_pilot / mp_weapon_melee_*).
    const bool is_hands_or_melee = true; // <replace with your check>
    const bool cache_loaded_ok   = ((cache_flags & 7) == 1) && cache_model_t;

    if (!is_hands_or_melee && any_polluted) { restore_cache_A(); any_polluted = false; }

    // ── cache_A pollution: swap the mesh via modelinfo cache ─────────────
    // Two slots so both viewmodel_model_index values seen during swap keep the
    // heirloom mesh. The (mdlidx, weap_name_idx) pair must be stable for a few
    // frames — during a swap the two fields update at different times and a
    // one-shot capture will pollute the wrong slot.
    if (is_hands_or_melee && cache_loaded_ok) {
        const uint16_t mdlidx = (uint16_t)vm_model_index;
        if (mdlidx != last_mdlidx_) { last_mdlidx_ = mdlidx; mdlidx_stable_ = 1; }
        else                        { mdlidx_stable_++; }
        if (mdlidx_stable_ >= 5 && mdlidx && mdlidx != 0xFFFF) {
            bool tracked = false;
            for (auto& s : slots_) if (s.valid && s.idx == mdlidx) { tracked = true; break; }
            if (!tracked)
                for (auto& s : slots_) if (!s.valid) { s.idx = mdlidx; break; }
        }

        if (polluted_heir_id_ != -1 && polluted_heir_id_ != heir_id) {
            for (auto& s : slots_) {
                if (!s.valid || !s.orig) continue;
                const uint64_t a = dma::base + off::modelinfo_cache_base
                                 + (uint64_t)s.idx * off::modelinfo_cache_stride
                                 + off::modelinfo_cache_model_t_off;
                dma::Write<uint64_t>(a, s.orig);
                s = PollutedSlot{};
            }
        }

        for (auto& s : slots_) {
            if (!s.valid && !s.idx) continue;
            if (s.valid && polluted_heir_id_ == heir_id) continue;
            const uint64_t a = dma::base + off::modelinfo_cache_base
                             + (uint64_t)s.idx * off::modelinfo_cache_stride
                             + off::modelinfo_cache_model_t_off;
            const uint64_t orig = dma::Read<uint64_t>(a);
            if (orig && orig != cache_model_t) {
                dma::Write<uint64_t>(a, cache_model_t);
                s.orig = orig; s.valid = true;
                polluted_heir_id_ = heir_id;
            } else if (orig == cache_model_t) {
                s.orig = 0; s.valid = true; polluted_heir_id_ = heir_id;
            }
        }
    }

    // ── Studio + seq cache refill on cstudiohdr change ───────────────────
    StudioInfo s_cur{};
    uint32_t heir_seqcount = 0;
    if (is_hands_or_melee && cur_cstudiohdr) {
        s_cur = read_studio_info(cur_cstudiohdr);
        heir_seqcount = s_cur.seqcount;
    }

    if (cur_cstudiohdr && is_hands_or_melee) {
        auto it = seq_map_.find(cur_cstudiohdr);
        if (it != seq_map_.end()) seq_ = it->second;
        const bool need_refill = (it == seq_map_.end())
                              || (seq_.numseq && seq_.numseq != heir_seqcount);
        if (need_refill && s_cur.seqcount >= 64) {
            SeqCache nc;
            nc.cstudiohdr = cur_cstudiohdr;
            nc.numseq     = s_cur.seqcount;
            nc.idle   = find_seq_by_label_and_activity(s_cur, kLblIdle,   1, kCandIdle,   2);
            nc.sprint = find_seq_by_label_and_activity(s_cur, kLblSprint, 1, kCandSprint, 2);
            nc.melee  = find_seq_by_label_and_activity(s_cur, kLblMelee,  1, kCandMelee,  2);
            nc.raise  = find_seq_by_label_and_activity(s_cur, kLblRaise,  1, kCandMelee,  2);
            seq_map_[cur_cstudiohdr] = nc;
            seq_ = nc;
        }
    }

    // Read sub-struct seqs for protect logic. Every anim_block_ptr → +0x80 →
    // sub-struct; the seq lives at sub+0x1C, cycle at sub+0x08.
    uint64_t sub_A = anim_block_ptr   ? dma::Read<uint64_t>(anim_block_ptr   + 0x80) : 0;
    uint64_t sub_B = anim_block_next  ? dma::Read<uint64_t>(anim_block_next  + 0x80) : 0;
    uint64_t sub_C = anim_block_third ? dma::Read<uint64_t>(anim_block_third + 0x80) : 0;
    uint32_t act_A = 0, act_B = 0, act_e48 = 0;
    if (s_cur.seqcount > 0) {
        if (cur_seq_e48 < s_cur.seqcount) act_e48 = read_activity_hash(s_cur, cur_seq_e48);
        if (sub_A) { uint16_t s = dma::Read<uint16_t>(sub_A + 0x1C); if (s < s_cur.seqcount) act_A = read_activity_hash(s_cur, s); }
        if (sub_B) { uint16_t s = dma::Read<uint16_t>(sub_B + 0x1C); if (s < s_cur.seqcount) act_B = read_activity_hash(s_cur, s); }
    }

    if (is_hands_or_melee && cur_cstudiohdr && sub_A && sub_B) {
        auto& ce = vm_cache_[weap_name_idx];
        ce.vm_ptr = vm_ptr; ce.cstudiohdr = cur_cstudiohdr;
        ce.anim_block_ptr = anim_block_ptr;
        ce.anim_block_next = anim_block_next;
        ce.anim_block_third = anim_block_third;
        ce.sub_A_ptr = sub_A; ce.sub_B_ptr = sub_B; ce.sub_C_ptr = sub_C;
    }

    // ── FSM: attack driver ───────────────────────────────────────────────
    uint32_t target_seq = kInvalidSeq;
    bool should_write   = false;
    bool phase_enter    = false;

    if (is_hands_or_melee && cur_cstudiohdr && seq_.cstudiohdr == cur_cstudiohdr
        && seq_.idle != kInvalidSeq && heir_seqcount >= 64) {
        const bool ducking     = (in.player_flags & 0x2) != 0;
        const bool on_ground   = (in.player_flags & 0x1) != 0;
        const bool sprinting   = (in_speed_state & 1) != 0;
        const bool raw_attack  = in.attack_down;
        const auto now         = std::chrono::steady_clock::now();

        if (raw_attack && !last_attack_ && seq_.melee != kInvalidSeq) {
            phase_       = AttackPhase::Swipe;
            phase_seq_   = seq_.melee;
            phase_dur_   = read_seq_duration(s_cur, phase_seq_);
            phase_start_ = now;
            phase_enter  = true;
        }
        last_attack_ = raw_attack;

        auto phase_done = [&]{
            const auto elapsed = now - phase_start_;
            if (phase_dur_ > 0.05f)
                return elapsed >= milliseconds((int)(phase_dur_ * 1000.0f));
            return elapsed >= kPhaseMin
                && (uint16_t)cur_seq_e48 == (uint16_t)phase_seq_
                && seq_finished != 0;
        };

        if (phase_ == AttackPhase::Swipe && phase_done()) {
            if (seq_.raise != kInvalidSeq) {
                phase_       = AttackPhase::Recovery;
                phase_seq_   = seq_.raise;
                phase_dur_   = read_seq_duration(s_cur, phase_seq_);
                phase_start_ = now;
                phase_enter  = true;
            } else {
                phase_ = AttackPhase::None; phase_seq_ = kInvalidSeq;
            }
        }
        if (phase_ == AttackPhase::Recovery) {
            const bool done = phase_dur_ > 0.05f
                            ? phase_done()
                            : (now - phase_start_ >= kRecoveryHold);
            if (done) { phase_ = AttackPhase::None; phase_seq_ = kInvalidSeq; }
        }
        if (phase_ != AttackPhase::None && now - phase_start_ > kPhaseCap) {
            phase_ = AttackPhase::None; phase_seq_ = kInvalidSeq;
        }
        if (phase_ == AttackPhase::Recovery && in.moving)
            { phase_ = AttackPhase::None; phase_seq_ = kInvalidSeq; }

        // Protect: don't overwrite hard-attack activities mid-play unless we're
        // the one driving them.
        auto guarded = [](uint32_t a){ return a && is_attack_hard(a); };
        bool protect = guarded(act_e48) || guarded(act_A) || guarded(act_B);
        if (phase_ != AttackPhase::None) protect = false;

        if      (phase_ != AttackPhase::None) target_seq = phase_seq_;
        else if (sprinting && in.moving && seq_.sprint != kInvalidSeq && on_ground)
            target_seq = seq_.sprint;
        else target_seq = seq_.idle;

        should_write = !protect && target_seq != kInvalidSeq;
    }

    // Combo re-attack won't replay from cycle 0 if seq is unchanged
    // (SetSequence returns early on prevSeq==newSeq). Write a different seq
    // for one tick to force the change-edge, then the next tick writes the
    // real target and the engine replays it as new.
    uint32_t write_seq = target_seq;
    if (phase_enter && (uint16_t)cur_seq_e48 == (uint16_t)target_seq) {
        const uint32_t bounce = (seq_.raise != kInvalidSeq && seq_.raise != target_seq)
                                 ? seq_.raise : seq_.idle;
        if (bounce != kInvalidSeq && bounce != target_seq) write_seq = bounce;
    }

    // ── Reassert loop: write seq + startTime on every reachable viewmodel ─
    if (should_write && write_seq < heir_seqcount) {
        const uint16_t tgt16 = (uint16_t)write_seq;
        for (auto& [name, ce] : vm_cache_) {
            if (!ce.vm_ptr) continue;
            const bool is_current = (ce.vm_ptr == vm_ptr);

            // Guard against dangling / stale cached pointers on the non-current
            // vm. Its pointers only refresh when it becomes current; a respawn
            // or studio reload leaves them pointing at freed memory. This read
            // must NOT go through page cache — cached reads happily return
            // stale bytes and defeat the guard.
            if (!is_current) {
                const uint64_t cs_now = dma::Read<uint64_t>(ce.vm_ptr + off::studio_hdr);
                if (!cs_now || cs_now != ce.cstudiohdr) { ce = VmCacheEntry{}; continue; }
                const StudioInfo s_other = read_studio_info(cs_now);
                if (s_other.seqcount != heir_seqcount) continue;
                ce.anim_block_ptr   = dma::Read<uint64_t>(ce.vm_ptr + 0x110, true);
                ce.anim_block_next  = dma::Read<uint64_t>(ce.vm_ptr + 0x108, true);
                ce.anim_block_third = dma::Read<uint64_t>(ce.vm_ptr + 0x118, true);
                ce.sub_A_ptr = ce.anim_block_ptr   ? dma::Read<uint64_t>(ce.anim_block_ptr   + 0x80, true) : 0;
                ce.sub_B_ptr = ce.anim_block_next  ? dma::Read<uint64_t>(ce.anim_block_next  + 0x80, true) : 0;
                ce.sub_C_ptr = ce.anim_block_third ? dma::Read<uint64_t>(ce.anim_block_third + 0x80, true) : 0;
            }
            if (!ce.sub_A_ptr || !ce.sub_B_ptr) continue;

            // 1) Sink override: the seq the engine reads.
            if (!is_current || (uint16_t)cur_seq_e48 != tgt16)
                dma::Write<uint32_t>(ce.vm_ptr + off::viewmodel_current_frame_anim_seq,
                                     (uint32_t)tgt16);

            // 2) On phase enter: clear seq_finished and pin animStartTime so
            //    the engine computes cycle from zero. Without this, Recovery
            //    inherits the swipe's stale track cycle and finishes instantly.
            if (phase_enter) {
                dma::Write<uint8_t>(ce.vm_ptr + off::viewmodel_seq_finished, 0);
                if (in.curtime > 0.0f)
                    dma::Write<float>(ce.vm_ptr + off::viewmodel_anim_start_time, in.curtime);
                dma::Write<float>(ce.sub_A_ptr + 0x08, 0.0f);
                dma::Write<float>(ce.sub_B_ptr + 0x08, 0.0f);
                if (ce.sub_C_ptr && ce.sub_C_ptr != ce.sub_A_ptr)
                    dma::Write<float>(ce.sub_C_ptr + 0x08, 0.0f);
            }
        }
    }
}
