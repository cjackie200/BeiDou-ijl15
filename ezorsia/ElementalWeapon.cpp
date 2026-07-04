#include "stdafx.h"
#include "ElementalWeapon.h"
#include "Client.h"
#include "QuestHook.h"

#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace {

// --- Elemental bonus storage (updated by server config packet 0x1006) ---
// Values are in hundredths: 125 = +25%, 110 = +10%, 0 = no bonus
static short g_fireBonus = 0;       // incRMAF
static short g_poisonBonus = 0;     // incRMAS
static short g_iceBonus = 0;        // incRMAI
static short g_lightningBonus = 0;  // incRMAL
static short g_elemDefault = 0;     // elemDefault (reserved for Phase 2)
static std::mutex g_bonusMutex;

// Set by TryApplyElementalBonus to tell CalcDamage hook which element to boost
static char g_lastSkillElem = 0;
// Set by CalcDamage hook to prevent TryApplyElementalBonus from double-applying
static bool g_calcDamageBoosted = false;

// --- Skill ID → element character mapping ---
// F = Fire, S = Poison, I = Ice, L = Lightning
// Covers all magic skills with elemental attributes.
static std::unordered_map<int, char> BuildSkillElementMap() {
    std::unordered_map<int, char> m;

    // === Fire/Poison Wizard ===
    m[2101004] = 'F'; // Fire Arrow
    m[2101005] = 'S'; // Poison Breath

    // === Fire/Poison Mage ===
    m[2111002] = 'F'; // Explosion
    m[2111003] = 'S'; // Poison Mist
    m[2111005] = 'F'; // Element Composition (FP)

    // === Fire/Poison ArchMage ===
    m[2121003] = 'F'; // Fire Demon
    m[2121004] = 'F'; // Meteor Shower
    m[2121005] = 'S'; // Paralyze

    // === Ice/Lightning Wizard ===
    m[2201001] = 'I'; // Cold Beam
    m[2201004] = 'L'; // Thunder Bolt

    // === Ice/Lightning Mage ===
    m[2211002] = 'I'; // Ice Strike
    m[2211004] = 'L'; // Thunder Spear
    m[2211006] = 'I'; // Element Composition (IL)

    // === Ice/Lightning ArchMage ===
    m[2221003] = 'I'; // Ice Demon
    m[2221004] = 'I'; // Blizzard
    m[2221006] = 'L'; // Chain Lightning

    // === Cleric/Priest/Bishop ===
    m[2301005] = 'H'; // Holy Arrow
    m[2311004] = 'H'; // Shining Ray
    m[2321007] = 'H'; // Angel Ray

    // === Blaze Wizard ===
    m[12001004] = 'F'; // Fire Arrow (BW)
    m[12101004] = 'F'; // Fire Pillar
    m[12111005] = 'F'; // Element Composition (BW)
    m[12111006] = 'F'; // Flame Gear

    // === Evan ===
    m[22121000] = 'F'; // Fire Circle
    m[22141002] = 'I'; // Ice Breath (Evan)
    m[22141004] = 'L'; // Thunder Circle
    m[22151002] = 'F'; // Fire Breath (Evan)
    m[22171051] = 'F'; // Blaze
    m[22181001] = 'S'; // Poison Circle

    return m;
}

static const std::unordered_map<int, char> kSkillElementMap = BuildSkillElementMap();

// --- Utility ---

static unsigned short ReadU16(const unsigned char* ptr) {
    return static_cast<unsigned short>(ptr[0] | (ptr[1] << 8));
}

static int ReadI32(const unsigned char* ptr) {
    return static_cast<int>(
        static_cast<unsigned int>(ptr[0])
        | (static_cast<unsigned int>(ptr[1]) << 8)
        | (static_cast<unsigned int>(ptr[2]) << 16)
        | (static_cast<unsigned int>(ptr[3]) << 24));
}

static void WriteI32(unsigned char* ptr, int value) {
    ptr[0] = static_cast<unsigned char>(value & 0xFF);
    ptr[1] = static_cast<unsigned char>((value >> 8) & 0xFF);
    ptr[2] = static_cast<unsigned char>((value >> 16) & 0xFF);
    ptr[3] = static_cast<unsigned char>((value >> 24) & 0xFF);
}

static short GetBonusForElement(char elem) {
    switch (elem) {
    case 'F': return g_fireBonus;
    case 'S': return g_poisonBonus;
    case 'I': return g_iceBonus;
    case 'L': return g_lightningBonus;
    case 'H': return 0; // Holy — no weapon with incRMAH exists
    default:  return 0;
    }
}

// Called by CalcDamage::MDamage codecave.
// Must use C linkage for easy calling from naked asm.
extern "C" int __cdecl ApplyCalcDamageElementalBonus(int damage) {
    if (g_lastSkillElem == 0) return damage;

    short bonus = GetBonusForElement(g_lastSkillElem);
    if (bonus <= 0) return damage;

    int boosted = (damage * (int)bonus) / 100;
    g_calcDamageBoosted = true;
    return boosted;
}

} // anonymous namespace

// --- Public API ---

namespace ElementalWeapon {

void HandleConfigPacket(const unsigned char* payload, unsigned long payloadSize) {
    if (payloadSize < 10) {
        return; // Minimum: 5 shorts = 10 bytes
    }

    short fireBonus = static_cast<short>(ReadU16(payload));
    short poisonBonus = static_cast<short>(ReadU16(payload + 2));
    short iceBonus = static_cast<short>(ReadU16(payload + 4));
    short lightningBonus = static_cast<short>(ReadU16(payload + 6));
    short elemDefault = static_cast<short>(ReadU16(payload + 8));

    {
        std::lock_guard<std::mutex> lock(g_bonusMutex);
        g_fireBonus = fireBonus;
        g_poisonBonus = poisonBonus;
        g_iceBonus = iceBonus;
        g_lightningBonus = lightningBonus;
        g_elemDefault = elemDefault;
    }

    if (Client::debug) {
        QuestHookTrace("ElementalWeapon config: F=%d S=%d I=%d L=%d elemDefault=%d",
            fireBonus, poisonBonus, iceBonus, lightningBonus, elemDefault);
    }
}

bool TryApplyElementalBonus(COutPacket* packet) {
    if (packet == nullptr || packet->Data == nullptr || packet->Size < 29) {
        return false;
    }

    const unsigned char* data = packet->Data;
    const unsigned short opcode = ReadU16(data);
    if (opcode != 0x002E) {
        return false; // Not a magic attack
    }

    const unsigned char attackedAndDamage = data[3];
    const int numAttacked = (attackedAndDamage >> 4) & 0x0F;
    const int numDamage = attackedAndDamage & 0x0F;

    if (numAttacked == 0 || numDamage == 0) {
        return false;
    }

    const int skillId = ReadI32(data + 4);

    // Look up skill element
    auto it = kSkillElementMap.find(skillId);
    if (it == kSkillElementMap.end()) {
        g_lastSkillElem = 0; // Not an elemental skill
        return false;
    }

    const char elemChar = it->second;
    g_lastSkillElem = elemChar; // Remember for next CalcDamage::MDamage call

    // --- Get bonus and extract first packet damage for logging ---
    short bonus;
    {
        std::lock_guard<std::mutex> lock(g_bonusMutex);
        bonus = GetBonusForElement(elemChar);
    }

    // Fixed damage offset: header(25) + oid(4) + skip14 = 43
    const int DMG_OFFSET = 43;
    int firstRawDmg = 0;
    if (packet->Size >= DMG_OFFSET + 4) {
        firstRawDmg = ReadI32(packet->Data + DMG_OFFSET);
    }

    // If CalcDamage already boosted, skip modification but LOG the real damage
    if (g_calcDamageBoosted) {
        g_calcDamageBoosted = false;
        if (Client::debug) {
            QuestHookTrace("[DMG] Calc skill=%d e=%c dmg=%d +%d%% F=%d S=%d I=%d L=%d",
                skillId, elemChar, firstRawDmg, bonus - 100,
                g_fireBonus, g_poisonBonus, g_iceBonus, g_lightningBonus);
        }
        return false;
    }

    if (bonus <= 0) {
        if (Client::debug) {
            QuestHookTrace("[DMG] None skill=%d e=%c dmg=%d F=%d S=%d I=%d L=%d",
                skillId, elemChar, firstRawDmg,
                g_fireBonus, g_poisonBonus, g_iceBonus, g_lightningBonus);
        }
        return false;
    }

    // --- Fallback: apply bonus to first damage value in packet ---
    bool modified = false;
    int firstNewDmg = 0;
    int dmgOff = DMG_OFFSET;
    for (int t = 0; t < numAttacked; t++) {
        for (int d = 0; d < numDamage; d++) {
            if (dmgOff + 4 > packet->Size) break;
            int dmg = ReadI32(packet->Data + dmgOff);
            if (dmg > 0) {
                const int nd = (dmg * (int)bonus) / 100;
                if (nd != dmg) {
                    WriteI32(packet->Data + dmgOff, nd);
                    modified = true;
                    if (!firstNewDmg) { firstRawDmg = dmg; firstNewDmg = nd; }
                }
            }
            dmgOff += 4;
        }
        dmgOff += 4; // trailer between targets
    }

    if (Client::debug) {
        QuestHookTrace("[DMG] Pkt  skill=%d e=%c dmg=%d->%d +%d%% F=%d S=%d I=%d L=%d",
            skillId, elemChar, firstRawDmg, firstNewDmg, bonus - 100,
            g_fireBonus, g_poisonBonus, g_iceBonus, g_lightningBonus);
    }

    return modified;
}

} // namespace ElementalWeapon
