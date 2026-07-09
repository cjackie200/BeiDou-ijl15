#include "stdafx.h"
#include "ElementalWeapon.h"
#include "Client.h"
#include "QuestHook.h"

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace {

// --- Elemental bonus storage (updated by server config packet 0x1006) ---
// Values are in hundredths: 200 = +100%, 50 = elemental mismatch penalty
static short g_fireBonus = 0;       // incRMAF
static short g_poisonBonus = 0;     // incRMAS
static short g_iceBonus = 0;        // incRMAI
static short g_lightningBonus = 0;  // incRMAL
static short g_holyBonus = 0;       // incRMAH
static short g_elemDefault = 0;     // elemDefault
static std::mutex g_bonusMutex;
static std::mutex g_statusMutex;
static std::unordered_set<int> g_poisonFireWeakOids;

constexpr unsigned short kApplyMonsterStatus = 0x00F2;
constexpr unsigned short kCancelMonsterStatus = 0x00F3;
constexpr int kPoisonStatusMask = 0x00000200;
constexpr int kFPWizardPoisonBreath = 2101005;
constexpr int kFPMagePoisonMist = 2111003;
constexpr int kFPMageElementComposition = 2111006;

// --- Skill ID → element character mapping ---
// F = Fire, S = Poison, I = Ice, L = Lightning, H = Holy
// Covers all magic skills with elemental attributes.
static std::unordered_map<int, const char*> BuildSkillElementMap() {
    std::unordered_map<int, const char*> m;

    // === CHINESE WZ SKILL IDs (verified 2026-07-04) ===
    // Fire/Poison Wizard
    m[2101004] = "F"; // 火焰箭
    m[2101005] = "S"; // 毒雾术

    // Fire/Poison Mage
    m[2111002] = "F"; // 末日烈焰
    m[2111003] = "S"; // 致命毒雾
    m[2111006] = "F"; // 火毒合击

    // Fire/Poison ArchMage
    m[2121003] = "F"; // Fire Demon
    m[2121005] = "I"; // Elquines
    m[2121006] = "S"; // Paralyze
    m[2121007] = "F"; // 天降落星

    // Ice/Lightning Wizard
    m[2201004] = "I"; // 冰冻术
    m[2201005] = "L"; // 雷电术

    // Ice/Lightning Mage
    m[2211002] = "I"; // 冰咆哮
    m[2211003] = "L"; // 落雷枪
    m[2211006] = "I"; // 冰雷合击

    // Ice/Lightning ArchMage
    m[2221003] = "I"; // 冰凤球
    m[2221006] = "L"; // 链环闪电
    m[2221007] = "I"; // 落霜冰破

    // Cleric/Priest/Bishop
    m[2301005] = "H"; // 圣箭术
    m[2311004] = "H"; // 圣光
    m[2321007] = "H"; // 光芒飞箭

    // Blaze Wizard (Chinese WZ)
    m[12001004] = "F"; // 炎精灵
    m[12111003] = "F"; // 天降落星(BW)
    m[12111004] = "F"; // 火魔兽(BW)
    m[12111005] = "F"; // 火牢术屏障
    m[12111006] = "F"; // 火风暴

    return m;
}

static const std::unordered_map<int, const char*> kSkillElementMap = BuildSkillElementMap();

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
    case 'H': return g_holyBonus;
    default:  return 0;
    }
}

static short GetBestBonusForElements(const char* elements) {
    if (elements == nullptr) {
        return 0;
    }

    short bestBonus = 0;
    for (const char* elem = elements; *elem != '\0'; ++elem) {
        short bonus = GetBonusForElement(*elem);
        if (bonus > bestBonus) {
            bestBonus = bonus;
        }
    }
    return bestBonus;
}

static bool ElementStringContains(const char* elements, char element) {
    if (elements == nullptr) {
        return false;
    }

    for (const char* it = elements; *it != '\0'; ++it) {
        if (*it == element) {
            return true;
        }
    }
    return false;
}

static bool NeedsElementPacketPatch(int skillId) {
    return skillId == 2111006 || skillId == 2211006;
}

static bool NeedsPoisonMatchPatch(const char* elements, short bonus) {
    return bonus > 0 && ElementStringContains(elements, 'S');
}

static bool IsFirePoisonDotSkill(int skillId) {
    return skillId == kFPWizardPoisonBreath
        || skillId == kFPMagePoisonMist
        || skillId == kFPMageElementComposition;
}

static int CountBits(unsigned int value) {
    int count = 0;
    while (value != 0) {
        count += static_cast<int>(value & 1U);
        value >>= 1;
    }
    return count;
}

static bool IsPoisonFireWeakOid(int oid) {
    std::lock_guard<std::mutex> lock(g_statusMutex);
    return g_poisonFireWeakOids.find(oid) != g_poisonFireWeakOids.end();
}

static void TrackApplyMonsterStatus(const unsigned char* payload, unsigned long payloadSize) {
    if (payload == nullptr || payloadSize < 20) {
        return;
    }

    const int oid = ReadI32(payload);
    if (oid <= 0) {
        return;
    }

    const int firstMask = ReadI32(payload + 12);
    const int secondMask = ReadI32(payload + 16);
    if ((secondMask & kPoisonStatusMask) == 0) {
        return;
    }

    const int statusCount = CountBits(static_cast<unsigned int>(firstMask))
        + CountBits(static_cast<unsigned int>(secondMask));
    if (statusCount <= 0) {
        return;
    }

    const unsigned long entriesOffset = 20;
    const unsigned long entriesBytes = static_cast<unsigned long>(statusCount) * 8UL;
    if (payloadSize < entriesOffset + entriesBytes) {
        return;
    }

    int firePoisonSkillId = 0;
    unsigned long cursor = entriesOffset;
    for (int i = 0; i < statusCount; ++i) {
        const int sourceSkillId = ReadI32(payload + cursor + 2);
        if (IsFirePoisonDotSkill(sourceSkillId)) {
            firePoisonSkillId = sourceSkillId;
            break;
        }
        cursor += 8;
    }

    if (firePoisonSkillId == 0) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_statusMutex);
        g_poisonFireWeakOids.insert(oid);
    }

    if (Client::debug) {
        QuestHookTrace("ElementalWeapon poison fire-weak apply oid=%d skill=%d statusCount=%d",
            oid, firePoisonSkillId, statusCount);
    }
}

static void TrackCancelMonsterStatus(const unsigned char* payload, unsigned long payloadSize) {
    if (payload == nullptr || payloadSize < 20) {
        return;
    }

    const int oid = ReadI32(payload);
    if (oid <= 0) {
        return;
    }

    const int secondMask = ReadI32(payload + 16);
    if ((secondMask & kPoisonStatusMask) == 0) {
        return;
    }

    bool erased = false;
    {
        std::lock_guard<std::mutex> lock(g_statusMutex);
        erased = g_poisonFireWeakOids.erase(oid) > 0;
    }

    if (erased && Client::debug) {
        QuestHookTrace("ElementalWeapon poison fire-weak cancel oid=%d", oid);
    }
}

static bool ApplyRatesToMagicAttackPacket(
    COutPacket* packet,
    int numAttacked,
    int numDamage,
    int baseRate,
    bool fireSkill,
    int* firstRawDmg,
    int* firstNewDmg,
    int* fireWeakTargets) {
    if (packet == nullptr || packet->Data == nullptr || baseRate <= 0) {
        return false;
    }

    const unsigned long bytesPerTarget = 4 + 14 + 4 * static_cast<unsigned long>(numDamage) + 4;
    const unsigned long totalTargetBytes = static_cast<unsigned long>(numAttacked) * bytesPerTarget;
    if (packet->Size < totalTargetBytes + 29) {
        return false;
    }

    unsigned long offset = packet->Size - totalTargetBytes;
    bool modified = false;

    for (int target = 0; target < numAttacked; target++) {
        if (offset + 18 > packet->Size) {
            return modified;
        }

        const int oid = ReadI32(packet->Data + offset);
        int targetRate = baseRate;
        if (fireSkill && IsPoisonFireWeakOid(oid)) {
            targetRate = (targetRate * 150) / 100;
            if (fireWeakTargets != nullptr) {
                ++(*fireWeakTargets);
            }
        }

        offset += 18;
        for (int line = 0; line < numDamage; line++) {
            if (offset + 4 > packet->Size) {
                return modified;
            }

            int damage = ReadI32(packet->Data + offset);
            if (damage > 0 && targetRate != 100) {
                long long scaledDamage = (static_cast<long long>(damage) * targetRate) / 100;
                if (scaledDamage > 2147483647LL) {
                    scaledDamage = 2147483647LL;
                }
                int newDamage = static_cast<int>(scaledDamage);
                if (newDamage != damage) {
                    WriteI32(packet->Data + offset, newDamage);
                    modified = true;
                    if (firstRawDmg != nullptr && *firstRawDmg == 0) {
                        *firstRawDmg = damage;
                    }
                    if (firstNewDmg != nullptr && *firstNewDmg == 0) {
                        *firstNewDmg = newDamage;
                    }
                }
            }
            offset += 4;
        }
        offset += 4;
    }

    return modified;
}

// Kept as a no-op while native client WZ elemental calculation is authoritative.
extern "C" int __cdecl ApplyCalcDamageElementalBonus(int damage) {
    return damage;
}

} // anonymous namespace

// --- Public API ---

namespace ElementalWeapon {

void HandleConfigPacket(const unsigned char* payload, unsigned long payloadSize) {
    if (payloadSize < 10) {
        return; // Minimum legacy packet: 5 shorts = 10 bytes
    }

    short fireBonus = static_cast<short>(ReadU16(payload));
    short poisonBonus = static_cast<short>(ReadU16(payload + 2));
    short iceBonus = static_cast<short>(ReadU16(payload + 4));
    short lightningBonus = static_cast<short>(ReadU16(payload + 6));
    short holyBonus = 0;
    short elemDefault = 0;
    if (payloadSize >= 12) {
        holyBonus = static_cast<short>(ReadU16(payload + 8));
        elemDefault = static_cast<short>(ReadU16(payload + 10));
    } else {
        elemDefault = static_cast<short>(ReadU16(payload + 8));
    }

    {
        std::lock_guard<std::mutex> lock(g_bonusMutex);
        g_fireBonus = fireBonus;
        g_poisonBonus = poisonBonus;
        g_iceBonus = iceBonus;
        g_lightningBonus = lightningBonus;
        g_holyBonus = holyBonus;
        g_elemDefault = elemDefault;
    }

    if (Client::debug) {
        QuestHookTrace("ElementalWeapon config: F=%d S=%d I=%d L=%d H=%d elemDefault=%d",
            fireBonus, poisonBonus, iceBonus, lightningBonus, holyBonus, elemDefault);
    }
}

void ClearRuntimeState() {
    std::lock_guard<std::mutex> lock(g_statusMutex);
    g_poisonFireWeakOids.clear();
}

void TrackMonsterStatusPacket(unsigned short opcode, const unsigned char* payload, unsigned long payloadSize) {
    if (opcode == kApplyMonsterStatus) {
        TrackApplyMonsterStatus(payload, payloadSize);
        return;
    }
    if (opcode == kCancelMonsterStatus) {
        TrackCancelMonsterStatus(payload, payloadSize);
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
        return false;
    }

    const char* elemChars = it->second;

    const bool fullPacketPatch = NeedsElementPacketPatch(skillId);
    short bonus;
    short elemDefault;
    {
        std::lock_guard<std::mutex> lock(g_bonusMutex);
        bonus = GetBestBonusForElements(elemChars);
        elemDefault = g_elemDefault;
    }

    int baseRate = 100;
    if (fullPacketPatch || NeedsPoisonMatchPatch(elemChars, bonus)) {
        baseRate = bonus > 0 ? bonus : elemDefault;
        if (baseRate <= 0) {
            baseRate = 100;
        }
    } else if (bonus <= 0 && elemDefault > 0 && elemDefault != 100) {
        baseRate = elemDefault;
    }

    // Fixed damage offset: header(25) + oid(4) + skip14 = 43
    const int DMG_OFFSET = 43;
    int firstRawDmg = 0;
    if (packet->Size >= DMG_OFFSET + 4) {
        firstRawDmg = ReadI32(packet->Data + DMG_OFFSET);
    }

    int firstPatchedRawDmg = 0;
    int firstPatchedNewDmg = 0;
    int fireWeakTargets = 0;
    const bool fireSkill = ElementStringContains(elemChars, 'F');
    bool modified = ApplyRatesToMagicAttackPacket(packet, numAttacked, numDamage, baseRate, fireSkill,
        &firstPatchedRawDmg, &firstPatchedNewDmg, &fireWeakTargets);

    if (Client::debug) {
        if (modified) {
            QuestHookTrace("[DMG] Patch skill=%d e=%s dmg=%d->%d baseRate=%d weakTargets=%d F=%d S=%d I=%d L=%d H=%d elemDefault=%d",
                skillId, elemChars, firstPatchedRawDmg, firstPatchedNewDmg, baseRate, fireWeakTargets,
                g_fireBonus, g_poisonBonus, g_iceBonus, g_lightningBonus, g_holyBonus, g_elemDefault);
        } else {
            QuestHookTrace("[DMG] Native skill=%d e=%s dmg=%d baseRate=%d weakTargets=%d F=%d S=%d I=%d L=%d H=%d elemDefault=%d",
                skillId, elemChars, firstRawDmg, baseRate, fireWeakTargets,
                g_fireBonus, g_poisonBonus, g_iceBonus, g_lightningBonus, g_holyBonus, g_elemDefault);
        }
    }

    return modified;
}

} // namespace ElementalWeapon
