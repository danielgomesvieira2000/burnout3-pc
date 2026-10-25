/*
 * The car select's EXTRA SERIES, after the HEAVYWEIGHTS: the game's traffic
 * as cars to race - for now the Tuk Tuk. The idea and the way are bryan's
 * (his "Traffic Series" PCSX2 patch: a traffic model with a Heavyweight's
 * parts, other cars' handling, sounds and camera); this is the port's own
 * way in, its files made from the player's disc as it opens - nothing of the
 * game's in the port. Always on, as are the HEAVYWEIGHTS in every mode.
 *
 * What the game has:
 *   - vehicles by a 64-bit ID, a name in base 40 ("COMPCAR18": the files
 *     pveh/COMP/CAR18.*, 0x00251F40); the list of them all, pveh/vlist.bin,
 *     at 0x005186A0: +4 how many (107, room for 128), then per vehicle +0x8
 *     a player's car (1), +0x208 a kind, +0x808 the traffic's, +0xA08 a
 *     weight, +0x408 its ID.
 *   - a vehicle's class by its folder (0x00251CB0): COMP 0, MSCL 1, CUPE 2,
 *     SPRT 3, SUPR 4, HSPC 5, HEVY 6, TRAF 7 - no TRAF on the disc - TSPC 8.
 *     The car select's series are the classes; it lists a series' cars from
 *     the list (0x00251B90, 0x00251C10), any not among the 67 unlocked
 *     (0x0013A660). Its series index is +0x258 of 0x01E8EC30: up to 4, 5
 *     with the specials (+0x265), 6 with the Heavyweights too (+0x264) -
 *     7 here (tools/mods_spec.py).
 *   - the 67 cars' own tables in the executable (0x004CCF80 their IDs, then
 *     their class, names, two floats each): a TRAF car is none of them
 *     (0x003EB060 says -1). A car's settings are its name's in the values
 *     (VehiclePhysics/<name>, VehicleAudio/<name>, ...: 0x0021AB40 the name).
 *
 * Here: the cars' files laid over the disc (pveh/TRAF/CARn.BGV, .HWD, .LWD:
 * the traffic's model with a Heavyweight's two blocks, a player's car's, and
 * that Heavyweight's .HWD and .LWD); the cars added to the list; their names
 * and the series' our strings; their settings another car's - the handling
 * one's, the camera's and the engine's sound each a car of the game's - and
 * the 67's tables theirs too; the rivals in the series all of its cars.
 */

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "ps2host/disc.h"
#include "ps2host/host.h"
#include "ps2host/run.h"

void F_0021AB40(lifted::Context& c);
void F_003EB060(lifted::Context& c);
void F_003EAD60(lifted::Context& c);
void F_003EADD0(lifted::Context& c);
void F_003EAE40(lifted::Context& c);
void F_0042DA70(lifted::Context& c);
void F_0030B6E0(lifted::Context& c);
void F_0042CFA0(lifted::Context& c);

// The cars' pictures (tools/extra_pictures.py, from port/extra_pictures/):
// a null ID ends the list.
struct ExtraPicture { const char* id; const u8* indices; const u8* palettes; };
extern const ExtraPicture kExtraPictures[];

namespace {

/* A vehicle's ID: its name's twelve characters in base 40 (0x0021AB40
   reads them back): space, '-', '/', the digits, the letters, '_'. */
constexpr u64 Id(const char* name) {
    u64 id = 0;
    bool ended = false;
    for (int i = 0; i < 12; ++i) {
        const char ch = ended || !name[i] ? ' ' : name[i];
        ended = ended || !name[i];
        const u64 code = ch == ' ' ? 0 : ch == '-' ? 1 : ch == '/' ? 2 : ch == '_' ? 39
                       : ch >= '0' && ch <= '9' ? u64(ch - '0' + 3) : u64(ch - 'A' + 13);
        id = id * 40 + code;
    }
    return id;
}
static_assert(Id("COMPCAR1") == 0x5B55839B95EB4000ull, "the game's own ID for the first compact");

struct ExtraCar {
    const char* id_name;        // its ID's name: pveh/TRAF/CARn
    const char* model;          // the traffic's model on the disc
    const char* parts;          // the Heavyweight whose blocks, .HWD and .LWD it has
    u64 handling, camera, sound;  // the cars whose settings it has
    const char* name;           // as the game writes a car's: mixed case, then capitals
    const char* name_upper;
    u32 kind, weight;           // the list's +0x208, +0xA08
    float wheel;                // its model's scale at the four places below, if 0.05 there (0: none)
};
const ExtraCar kCars[] = {
    // The Tuk Tuk (COMPCAR18): handled as the Compact Type 2, its camera and
    // engine two Heavyweights' - bryan's choices.
    {"TRAFCAR1", "PVEH/COMP/CAR18.BTV", "PVEH/HEVY/CAR1", Id("COMPCAR2"), Id("HEVYCAR5"), Id("HEVYCAR2"),
     "Tuk Tuk", "TUK TUK", 1, 4500, 0.75f},
};
constexpr u32 kCarCount = sizeof kCars / sizeof kCars[0];

const ExtraCar* Find(u64 id) {
    for (const ExtraCar& car : kCars)
        if (Id(car.id_name) == id) return &car;
    return nullptr;
}

// The vehicles' list (pveh/vlist.bin, loaded at start-up).
constexpr u32 kList = 0x005186A0u, kListRoom = 128;
constexpr u32 kPlayerCar = 0x8u, kKind = 0x208u, kTraffic = 0x808u, kWeight = 0xA08u, kIds = 0x408u;
constexpr u32 kExtraClass = 7;

// Our strings: IDs past the language file's 4,006, their UTF-16 below the
// executable (above the custom songs' names, custom_songs.cpp).
constexpr u32 kStringSeries = 0x7E40u, kStringCars = 0x7E41u;      // + 2 per car: mixed, capitals
constexpr u32 kStrings = 0x000FF000u, kStringChars = 32;

// The traffic model's scale the Tuk Tuk has 0.05 (its front wheel's?), at
// the header's +0x1C and three of its records - bryan's model has 0.75.
constexpr u32 kWheelScales[] = {0x1C, 0xB80, 0xB94, 0xBA8};

u32 Le32(const std::vector<u8>& d, std::size_t at) {
    return u32(d[at]) | u32(d[at + 1]) << 8 | u32(d[at + 2]) << 16 | u32(d[at + 3]) << 24;
}
void Put32(std::vector<u8>& d, std::size_t at, u32 v) {
    for (int i = 0; i < 4; ++i) d[at + i] = u8(v >> (8 * i));
}

/* A player's car of a traffic model: the model to its last byte (16-byte
   aligned), then the Heavyweight's two blocks (its header's +0x16DC and
   +0x16E0 point at them; a traffic model has none), the file a whole number
   of 2048-byte sectors (+0x8 says how long). */
std::vector<u8> PlayerModel(std::vector<u8> model, const std::vector<u8>& parts, float wheel) {
    std::size_t end = model.size();
    while (end && !model[end - 1]) --end;
    end = (end + 15) & ~std::size_t(15);
    const u32 first = Le32(parts, 0x16DC), second = Le32(parts, 0x16E0);
    std::size_t last = parts.size();
    while (last > second && !parts[last - 1]) --last;
    model.resize(end);
    model.insert(model.end(), parts.begin() + first, parts.begin() + last);
    model.resize((model.size() + 0x7FF) & ~std::size_t(0x7FF), 0);
    Put32(model, 0x8, u32(model.size()));
    Put32(model, 0x16DC, u32(end));
    Put32(model, 0x16E0, u32(end + (second - first)));
    if (wheel != 0.0f) {
        u32 small, scale;
        const float five_hundredths = 0.05f;
        std::memcpy(&small, &five_hundredths, 4);
        std::memcpy(&scale, &wheel, 4);
        for (const u32 at : kWheelScales)
            if (Le32(model, at) == small) Put32(model, at, scale);
    }
    return model;
}

/* The car select's column of cars shows each car's picture: a record of
   DATA/FRONTEND.TXD named by its ID (0x004277C0 looks it up by name; none:
   a warning sign). The file: +8 how many records, from +0x10 each's number
   and offset (16 bytes; room for more before the first record), then the
   records - a car's 0x4300 bytes: its name at +0xA8, two DMA references to
   its GS images (+0x104 the indices' at +0x200, +0x184 the palettes' at
   +0x2250, as the file is laid out in memory), the 128 x 64 indices as a
   64 x 32 32-bit image at +0x250, the eight palettes at +0x22A0. Ours: a
   car's record copied (COMPCAR2's), with our name, images and references,
   added at the end (tools/extra_pictures.py makes the images). */
constexpr u32 kPictureRecord = 0x4300, kPictureName = 0xA8, kIndicesRef = 0x104, kPalettesRef = 0x184;
constexpr u32 kIndicesAt = 0x200, kPalettesAt = 0x2250, kImageBytes = 0x2000;

void AddPictures(ps2host::Disc& disc) {
    if (!kExtraPictures[0].id) return;
    std::vector<u8> txd;
    if (!disc.ReadFile("DATA/FRONTEND.TXD", &txd) || txd.size() < 0x10) return;
    const u32 count = Le32(txd, 8);
    if (txd.size() < 0x10 + 16ull * count) return;
    u32 first = u32(txd.size()), model = 0;
    for (u32 i = 0; i < count; ++i) {
        const u32 at = Le32(txd, 0x10 + 16 * i + 8);
        first = std::min(first, at);
        if (at + kPictureRecord <= txd.size() && std::memcmp(&txd[at + kPictureName], "COMPCAR2", 9) == 0) model = at;
    }
    if (!model) return;
    u32 added = 0;
    for (const ExtraPicture* p = kExtraPictures; p->id; ++p) {
        if (!Find(Id(p->id)) || 0x10 + 16ull * (count + added + 1) > first) continue;
        const u32 at = u32((txd.size() + 0x3F) & ~std::size_t(0x3F));
        txd.resize(at + kPictureRecord, 0);
        std::memcpy(&txd[at], &txd[model], kPictureRecord);
        std::memset(&txd[at + kPictureName], 0, 16);
        std::memcpy(&txd[at + kPictureName], p->id, std::strlen(p->id));
        std::memcpy(&txd[at + kIndicesAt + 0x50], p->indices, kImageBytes);
        std::memcpy(&txd[at + kPalettesAt + 0x50], p->palettes, kImageBytes);
        Put32(txd, at + kIndicesRef, Le32(txd, model + kIndicesRef) - model + at);
        Put32(txd, at + kPalettesRef, Le32(txd, model + kPalettesRef) - model + at);
        const u32 entry = 0x10 + 16 * (count + added);
        Put32(txd, entry, count + added + 1);
        Put32(txd, entry + 4, 0);
        Put32(txd, entry + 8, at);
        Put32(txd, entry + 12, 0);
        ++added;
    }
    if (!added) return;
    Put32(txd, 8, count + added);
    disc.AddFile("DATA/FRONTEND.TXD", std::move(txd));
}

/* As the disc opens: each car's files, made from the disc's own. */
void AddFiles() {
    ps2host::Disc& disc = ps2host::GuestDisc();
    for (const ExtraCar& car : kCars) {
        std::vector<u8> model, parts, hwd, lwd;
        const std::string from = car.parts;
        if (!disc.ReadFile(car.model, &model) || !disc.ReadFile(from + ".BGV", &parts) ||
            !disc.ReadFile(from + ".HWD", &hwd) || !disc.ReadFile(from + ".LWD", &lwd) || parts.size() < 0x16E4 ||
            Le32(parts, 0x16E0) <= Le32(parts, 0x16DC) || Le32(parts, 0x16E0) > parts.size() || model.size() < 0x16E4) {
            std::fprintf(stderr, "[extra series] %s: its files are not on this disc\n", car.id_name);
            continue;
        }
        // pveh/TRAF/CARn: the ID's name, the folder its first four letters.
        const std::string name = car.id_name;
        const std::string path = "PVEH/" + name.substr(0, 4) + "/" + name.substr(4);
        disc.AddFile(path + ".BGV", PlayerModel(std::move(model), parts, car.wheel));
        disc.AddFile(path + ".HWD", std::move(hwd));
        disc.AddFile(path + ".LWD", std::move(lwd));
    }
    AddPictures(disc);
}

u64 g_last_id = 0;      // the last ID 0x003EB060 was asked for

/* 0x0021AB40(id, out): a vehicle's name - for ours, the name of the car
   whose settings it has: the engine's sound (its sounds' and their AI's
   values, the two at 0x00276B00 and 0x00383A70), the camera's (the bumper
   and look-back cameras' offsets), else the handling's (the physics, the
   garage's stats) - but its own where the garage's column finds a car's
   model by its name (0x00427080, 0x004277C0). */
void Name(lifted::Context& c) {
    if (const ExtraCar* car = Find(c.r[4])) {
        switch (u32(c.r[31])) {
        case 0x004271ECu: case 0x00427818u: break;
        case 0x0028BA5Cu: case 0x0028C7BCu: case 0x00276B64u: case 0x00383B64u: c.r[4] = car->sound; break;
        case 0x002F888Cu: c.r[4] = car->camera; break;
        default: c.r[4] = car->handling; break;
        }
    }
    F_0021AB40(c);
}

/* 0x003EB060(id): which of the 67 a car is (-1: none). Kept for the names
   (0x003EAE40 asked with that -1); an event's set-up (0x0030BFF0,
   0x0030C2C0: the race's class by it) gets the handling car's. */
void Index(lifted::Context& c) {
    const ExtraCar* car = Find(c.r[4]);
    const u32 ra = u32(c.r[31]);
    if (car && (ra == 0x0030C144u || ra == 0x0030C3F8u)) c.r[4] = car->handling;
    g_last_id = c.r[4];
    F_003EB060(c);
}

/* 0x003EAD60, 0x003EADD0(id): two of a car's values from the 67's tables
   (the race's, 0x001B36E0) - the handling car's. */
template <void (*Values)(lifted::Context&)>
void CarValues(lifted::Context& c) {
    if (const ExtraCar* car = Find(c.r[4])) c.r[4] = car->handling;
    Values(c);
}

/* 0x003EAE40(index, capitals): a car's name's string - for none of the 67
   (-1), ours if the car asked about last was. */
void CarName(lifted::Context& c) {
    if (s32(c.r[4]) < 0) {
        for (u32 k = 0; k < kCarCount; ++k)
            if (Id(kCars[k].id_name) == g_last_id) {
                c.r[2] = kStringCars + 2 * k + (u32(c.r[5]) ? 1 : 0);
                return;
            }
    }
    F_003EAE40(c);
}

/* 0x0042DA70(series): its name's string - EXTRA SERIES ours. */
void SeriesName(lifted::Context& c) {
    if (u32(c.r[4]) == kExtraClass) {
        c.r[2] = kStringSeries;
        return;
    }
    F_0042DA70(c);
}

/* 0x0030B6E0(race, class): a rival's car of a class, at random among the
   ones a race may have (no more than three of a car). In the EXTRA SERIES
   any of its cars, as many times as it takes - the game's own random
   numbers (0x00518680, 0x00518684) drawn as it draws them. */
void RivalCar(lifted::Context& c) {
    if (u32(c.r[5]) != kExtraClass) {
        F_0030B6E0(c);
        return;
    }
    constexpr u32 kSeed = 0x00518680u, kStep = 0x00518684u;
    const u32 seed = u32(generated::ReadU32(kSeed)), step = u32(generated::ReadU32(kStep));
    const u32 next = (seed << 16) + u32(s32(seed) >> 16) + step;
    generated::Write32(kSeed, next);
    generated::Write32(kStep, step + next);
    c.r[2] = Id(kCars[next % kCarCount].id_name);
}

/* 0x0042CFA0(select, mode, ..., series, car, heavyweights, specials, ...):
   the car select set up - the HEAVYWEIGHTS always (its t1, the class's
   flag: +0x264), and a SINGLE EVENT's last series kept only where it can
   be (EXTRA with the specials, as the HEAVYWEIGHTS: the game's own check). */
void SelectSetUp(lifted::Context& c) {
    c.r[9] = 1;
    if (u32(c.r[5]) == 9 && !u32(c.r[10]) && u32(c.r[7]) == kExtraClass) {
        c.r[7] = 0;
        c.r[8] = 0;
    }
    F_0042CFA0(c);
}

void WriteString(u32 at, const char* text) {
    u32 i = 0;
    for (; text[i] && i + 1 < kStringChars; ++i) generated::Write16(at + 2 * i, u8(text[i]));
    generated::Write16(at + 2 * i, 0);
}

}  // namespace

/* A string of ours by ID (custom_songs.cpp's lookup asks first): its guest
   address, or 0. */
u32 ExtraSeriesString(u32 id) {
    if (id < kStringSeries || id >= kStringCars + 2 * kCarCount) return 0;
    const u32 k = id - kStringSeries, at = kStrings + k * kStringChars * 2;
    WriteString(at, k == 0 ? "EXTRA SERIES" : (k - 1) % 2 ? kCars[(k - 1) / 2].name_upper : kCars[(k - 1) / 2].name);
    return at;
}

/* Each frame step (mods.cpp): the cars in the vehicles' list, once it is
   loaded - again if the game loads it again. */
void ExtraSeriesFrame() {
    if (generated::ReadU32(kList) != 6u) return;
    u32 count = u32(generated::ReadU32(kList + 4u));
    if (count < 107u || count + kCarCount > kListRoom) return;
    for (const ExtraCar& car : kCars) {
        const u64 id = Id(car.id_name);
        bool there = false;
        for (u32 i = 0; i < count && !there; ++i) there = generated::ReadU64(kList + kIds + 8u * i) == id;
        if (there) continue;
        generated::Write32(kList + kPlayerCar + 4u * count, 1);
        generated::Write32(kList + kKind + 4u * count, car.kind);
        generated::Write32(kList + kTraffic + 4u * count, 0);
        generated::Write32(kList + kWeight + 4u * count, car.weight);
        generated::Write64(kList + kIds + 8u * count, id);
        ++count;
    }
    generated::Write32(kList + 4u, count);
}

void RegisterExtraSeries() {
    ps2host::Disc::OnOpen(AddFiles);
    ps2host::Override(0x0021AB40u, Name, "a vehicle's name: the EXTRA SERIES' cars' settings another car's");
    ps2host::Override(0x003EB060u, Index, "which of the 67 a car is: the EXTRA SERIES' names, an event's class");
    ps2host::Override(0x003EAD60u, CarValues<F_003EAD60>, "a car's value: the EXTRA SERIES' the handling car's");
    ps2host::Override(0x003EADD0u, CarValues<F_003EADD0>, "a car's value: the EXTRA SERIES' the handling car's");
    ps2host::Override(0x003EAE40u, CarName, "a car's name: the EXTRA SERIES' cars'");
    ps2host::Override(0x0042DA70u, SeriesName, "a series' name: EXTRA SERIES");
    ps2host::Override(0x0030B6E0u, RivalCar, "a rival's car: the EXTRA SERIES' cars");
    ps2host::Override(0x0042CFA0u, SelectSetUp, "the car select set up: HEAVYWEIGHTS always, EXTRA SERIES kept");
}
