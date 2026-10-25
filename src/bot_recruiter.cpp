/*
 * City bot recruiter for AzerothCore 3.3.5a.
 *
 * Gossip and .recruiter add call the same recruit path. That path uses the
 * playerbots addclass cache and PlayerbotHolder::AddPlayerBot. It does not
 * change mod-playerbots. The chosen talent row, gear band, and orders are
 * applied after that module has finished logging the bot in and rolling its
 * own talents. A one-off hire is stored in bot_recruiter_hire for this
 * session. Dungeon, Arena, and Battleground teams, and the PvE and PvP
 * builds on those characters, are stored in their own tables.
 */

#include "CharacterCache.h"
#include "Chat.h"
#include "CommandScript.h"
#include "Containers.h"
#include "CreatureScript.h"
#include "DBCStores.h"
#include "DatabaseEnv.h"
#include "PlayerbotsDatabase.h"
#include "Group.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerbotFactory.h"
#include "PlayerbotGuildMgr.h"
#include "PlayerbotMgr.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"
#include "PlayerScript.h"
#include "ScriptedGossip.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "Timer.h"
#include "World.h"
#include "WorldScript.h"
#include "WorldSession.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace Acore::ChatCommands;

namespace
{
constexpr uint32 LOGIN_WAIT_MS = 60000;
constexpr uint32 GROUP_WAIT_MS = 60000;

enum class RecruitActivity : uint8
{
    Dungeon = 0,
    Raid = 1,
    Arena = 2,
    Battleground = 3
};

struct TalentTree
{
    char const* label;
    int pveSpec;
    int pvpSpec;
    char const* tokens;
};

struct ClassChoice
{
    uint8 cls;
    char const* label;
    char const* tokens;
    TalentTree trees[3];
};

struct RaceChoice
{
    uint8 race;
    char const* label;
    char const* tokens;
};

// Premade spec numbers from playerbots.conf.dist. Most classes store three
// PvE rows in 0-2 and the same three PvP rows in 3-5. Death knight and mage
// have an extra PvE row, so their PvP rows start at 4. Druid's second tree is
// the bear build for a dungeon or raid, and the cat build for arena or
// battlegrounds, because the config has no bear PvP row.
ClassChoice const kClasses[] = {
    {CLASS_WARRIOR, "Warrior", "warrior", {
        {"Arms", 0, 3, "arms"},
        {"Fury", 1, 4, "fury"},
        {"Protection", 2, 5, "prot protection"},
    }},
    {CLASS_PALADIN, "Paladin", "paladin", {
        {"Holy", 0, 3, "holy"},
        {"Protection", 1, 4, "prot protection"},
        {"Retribution", 2, 5, "ret retribution"},
    }},
    {CLASS_HUNTER, "Hunter", "hunter", {
        {"Beast Mastery", 0, 3, "bm beastmastery beast"},
        {"Marksmanship", 1, 4, "mm marksmanship"},
        {"Survival", 2, 5, "surv survival"},
    }},
    {CLASS_ROGUE, "Rogue", "rogue", {
        {"Assassination", 0, 3, "as assassination"},
        {"Combat", 1, 4, "combat"},
        {"Subtlety", 2, 5, "sub subtlety"},
    }},
    {CLASS_PRIEST, "Priest", "priest", {
        {"Discipline", 0, 3, "disc discipline"},
        {"Holy", 1, 4, "holy"},
        {"Shadow", 2, 5, "shadow"},
    }},
    {CLASS_DEATH_KNIGHT, "Death Knight", "dk deathknight", {
        {"Blood", 0, 4, "blood"},
        {"Frost", 1, 5, "frost"},
        {"Unholy", 2, 6, "unholy"},
    }},
    {CLASS_SHAMAN, "Shaman", "shaman", {
        {"Elemental", 0, 3, "ele elemental"},
        {"Enhancement", 1, 4, "enh enhancement"},
        {"Restoration", 2, 5, "resto restoration"},
    }},
    {CLASS_MAGE, "Mage", "mage", {
        {"Arcane", 0, 4, "arcane"},
        {"Fire", 1, 5, "fire"},
        {"Frost", 2, 6, "frost"},
    }},
    {CLASS_WARLOCK, "Warlock", "warlock", {
        {"Affliction", 0, 3, "affli affliction"},
        {"Demonology", 1, 4, "demo demonology"},
        {"Destruction", 2, 5, "destro destruction"},
    }},
    {CLASS_DRUID, "Druid", "druid", {
        {"Balance", 0, 4, "balance"},
        {"Feral", 1, 5, "feral bear cat"},
        {"Restoration", 2, 6, "resto restoration"},
    }},
};

RaceChoice const kRaces[] = {
    {RACE_HUMAN, "Human", "human"},
    {RACE_DWARF, "Dwarf", "dwarf"},
    {RACE_NIGHTELF, "Night Elf", "nightelf nelf"},
    {RACE_GNOME, "Gnome", "gnome"},
    {RACE_DRAENEI, "Draenei", "draenei"},
    {RACE_ORC, "Orc", "orc"},
    {RACE_UNDEAD_PLAYER, "Undead", "undead scourge"},
    {RACE_TAUREN, "Tauren", "tauren"},
    {RACE_TROLL, "Troll", "troll"},
    {RACE_BLOODELF, "Blood Elf", "bloodelf belf"},
};

enum class GearBand : uint8
{
    Stored = 0,
    Match = 1,
    Naxx = 2,
    Ulduar = 3,
    Toc = 4,
    Icc10 = 5,
    Icc25 = 6,
    IccHeroic = 7
};

enum class RecruitStance : uint8
{
    Tank = 0,
    Heal = 1,
    Damage = 2,
    Passive = 3
};

struct GearChoice
{
    GearBand band;
    char const* label;
    uint32 itemLevel;
};

// itemLevel 0 means the band is not a fixed raid tier.
GearChoice const kGears[] = {
    {GearBand::Stored, "Leave their gear", 0},
    {GearBand::Match, "Match my gear", 0},
    {GearBand::Naxx, "Naxxramas (200)", 200},
    {GearBand::Ulduar, "Ulduar (225)", 225},
    {GearBand::Toc, "Trial of the Crusader (245)", 245},
    {GearBand::Icc10, "Icecrown 10 (251)", 251},
    {GearBand::Icc25, "Icecrown 25 (264)", 264},
    {GearBand::IccHeroic, "Icecrown heroic (277)", 277},
};

struct StanceChoice
{
    RecruitStance stance;
    char const* label;
};

StanceChoice const kStances[] = {
    {RecruitStance::Tank, "Tank"},
    {RecruitStance::Heal, "Heal"},
    {RecruitStance::Damage, "Damage"},
    {RecruitStance::Passive, "Passive"},
};

struct ActivityChoice
{
    RecruitActivity activity;
    char const* label;
    char const* tokens;
    bool capAtFive;
};

ActivityChoice const kActivities[] = {
    {RecruitActivity::Dungeon, "Dungeon", "dungeon", true},
    {RecruitActivity::Raid, "Raid", "raid", false},
    {RecruitActivity::Arena, "Arena", "arena", true},
    {RecruitActivity::Battleground, "Battlegrounds", "bg battleground battlegrounds", false},
};

enum class SavedTeam : uint8
{
    Dungeon = 0,
    Arena = 1,
    Battleground = 2
};

enum class LoadoutKind : uint8
{
    Pve = 0,
    Pvp = 1
};

struct TeamChoice
{
    SavedTeam team;
    char const* label;
    uint8 cap;
    RecruitActivity activity;
};

TeamChoice const kTeams[] = {
    {SavedTeam::Dungeon, "Dungeon", 5, RecruitActivity::Dungeon},
    {SavedTeam::Arena, "Arena", 5, RecruitActivity::Arena},
    {SavedTeam::Battleground, "Battleground", 10, RecruitActivity::Battleground},
};

struct MenuState
{
    RecruitActivity activity = RecruitActivity::Dungeon;
    uint8 race = 0;
    uint8 cls = 0;
    uint8 tree = 0;
    GearBand gear = GearBand::Stored;
    bool summon = false;
    RecruitStance stance = RecruitStance::Damage;
    SavedTeam team = SavedTeam::Dungeon;
    bool assignTeam = false;
    ObjectGuid setBot;
};

struct PendingRecruit
{
    ObjectGuid masterGuid;
    int specNo = 0;
    GearBand gear = GearBand::Stored;
    bool summon = false;
    RecruitStance stance = RecruitStance::Damage;
    RecruitActivity activity = RecruitActivity::Dungeon;
    uint32 startedMs = 0;
    uint32 appliedMs = 0;
    bool loginFinished = false;
    bool specApplied = false;
    bool teamSummon = false;
    bool assignTeam = false;
    bool applySpec = true;
    SavedTeam team = SavedTeam::Dungeon;
    uint8 levelBefore = 0;
};

struct RecruitRequest
{
    uint8 cls = 0;
    uint8 race = 0;
    RecruitActivity activity = RecruitActivity::Dungeon;
    uint8 tree = 0;
    GearBand gear = GearBand::Stored;
    bool summon = false;
    RecruitStance stance = RecruitStance::Damage;
    uint8 gender = GENDER_NONE;
    ObjectGuid preferred;
    bool createNew = false;
    int specNo = -1;
    bool assignTeam = false;
    bool teamSummon = false;
    bool applySpec = true;
    SavedTeam team = SavedTeam::Dungeon;
    uint8 levelBefore = 0;
};

struct ActiveCall
{
    ObjectGuid master;
    SavedTeam team = SavedTeam::Dungeon;
};

std::unordered_map<ObjectGuid, MenuState> g_menu;
std::unordered_map<ObjectGuid, PendingRecruit> g_pending;
std::unordered_map<ObjectGuid, ActiveCall> g_active;

bool OnTeam(ObjectGuid masterGuid, ObjectGuid botGuid, SavedTeam team);
uint8 TeamCount(ObjectGuid masterGuid, SavedTeam team);
bool GearSetSaved(ObjectGuid botGuid, LoadoutKind kind);
void EquipSavedSet(Player* master, Player* bot, LoadoutKind kind);
void AssignToTeam(Player* master, ChatHandler* handler, ObjectGuid botGuid, SavedTeam team, int specNo);

enum GossipSender
{
    SENDER_ACTIVITY = 1,
    SENDER_RACE = 2,
    SENDER_CLASS = 3,
    SENDER_SPEC = 4,
    SENDER_DISMISS = 5,
    SENDER_GEAR = 7,
    SENDER_ARRIVE = 8,
    SENDER_STANCE = 9,
    SENDER_PERSON = 10,
    SENDER_GENDER = 11,
    SENDER_TEAMS = 12,
    SENDER_TEAM = 13,
    SENDER_TEAM_SUMMON = 14,
    SENDER_TEAM_SAVE = 15,
    SENDER_TEAM_ADD = 16,
    SENDER_TEAM_REMOVE = 17,
    SENDER_TEAM_DROP = 18,
    SENDER_SET_LIST = 19,
    SENDER_SET_BOT = 20,
    SENDER_SET_SAVE = 21,
    SENDER_SET_EQUIP = 22
};

constexpr uint32 PERSON_NEW = 0;
constexpr uint32 SAVED_PARTY_LIMIT = 10;

std::string Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool TokenIn(std::string const& token, char const* list)
{
    std::istringstream in(list);
    std::string word;
    while (in >> word)
        if (word == token)
            return true;
    return false;
}

void Tell(ChatHandler* handler, std::string const& text)
{
    if (handler)
        handler->PSendSysMessage("{}", text);
}

ClassChoice const* FindClass(uint8 cls)
{
    for (ClassChoice const& choice : kClasses)
        if (choice.cls == cls)
            return &choice;
    return nullptr;
}

ClassChoice const* FindClassToken(std::string const& token)
{
    for (ClassChoice const& choice : kClasses)
        if (TokenIn(token, choice.tokens))
            return &choice;
    return nullptr;
}

RaceChoice const* FindRace(uint8 race)
{
    for (RaceChoice const& choice : kRaces)
        if (choice.race == race)
            return &choice;
    return nullptr;
}

RaceChoice const* FindRaceToken(std::string const& token)
{
    for (RaceChoice const& choice : kRaces)
        if (TokenIn(token, choice.tokens))
            return &choice;
    return nullptr;
}

TeamChoice const* FindTeam(SavedTeam team)
{
    for (TeamChoice const& choice : kTeams)
        if (choice.team == team)
            return &choice;
    return nullptr;
}

LoadoutKind KindFor(SavedTeam team)
{
    return team == SavedTeam::Dungeon ? LoadoutKind::Pve : LoadoutKind::Pvp;
}

ActivityChoice const* FindActivity(RecruitActivity activity)
{
    for (ActivityChoice const& choice : kActivities)
        if (choice.activity == activity)
            return &choice;
    return nullptr;
}

ActivityChoice const* FindActivityToken(std::string const& token)
{
    for (ActivityChoice const& choice : kActivities)
        if (TokenIn(token, choice.tokens))
            return &choice;
    return nullptr;
}

GearChoice const* FindGear(GearBand band)
{
    for (GearChoice const& choice : kGears)
        if (choice.band == band)
            return &choice;
    return nullptr;
}

StanceChoice const* FindStance(RecruitStance stance)
{
    for (StanceChoice const& choice : kStances)
        if (choice.stance == stance)
            return &choice;
    return nullptr;
}

bool SameFaction(Player const* master, uint8 race)
{
    return Player::TeamIdForRace(race) == master->GetTeamId(true);
}

bool DeathKnightAllowed(Player const* master, uint8 cls)
{
    if (cls != CLASS_DEATH_KNIGHT)
        return true;
    return master->GetLevel() >= sWorld->getIntConfig(CONFIG_START_HEROIC_PLAYER_LEVEL);
}

bool ClassLegalForRace(uint8 race, uint8 cls)
{
    // playercreateinfo is the list All Races All Classes fills. A row there is a
    // legal pair, including combinations Wrath itself never shipped.
    return sObjectMgr->GetPlayerInfo(race, cls) != nullptr;
}

int SpecNumber(TalentTree const& tree, RecruitActivity activity)
{
    bool pvp = activity == RecruitActivity::Arena || activity == RecruitActivity::Battleground;
    return pvp ? tree.pvpSpec : tree.pveSpec;
}

uint8 OfflineRace(ObjectGuid guid)
{
    QueryResult result = CharacterDatabase.Query("SELECT race FROM characters WHERE guid = {}", guid.GetCounter());
    if (!result)
        return 0;
    return (*result)[0].Get<uint8>();
}

// playerbots_names.gender uses the same order as RandomPlayerbotFactory::NameRaceAndGender.
// That helper is not exported from the playerbots library, so the index is repeated here.
uint8 NameGenderKey(uint8 race, uint8 gender)
{
    uint8 male = 0;
    switch (race)
    {
        case RACE_GNOME:    male = 2; break;
        case RACE_DWARF:    male = 4; break;
        case RACE_NIGHTELF: male = 6; break;
        case RACE_DRAENEI:  male = 8; break;
        case RACE_ORC:      male = 10; break;
        case RACE_TROLL:    male = 12; break;
        case RACE_TAUREN:   male = 14; break;
        case RACE_BLOODELF: male = 16; break;
        case RACE_HUMAN:
        case RACE_UNDEAD_PLAYER:
        default:            male = 0; break;
    }
    return male + (gender >= GENDER_NONE ? uint8(GENDER_MALE) : gender);
}

std::string UnusedBotName(uint8 race, uint8 gender)
{
    uint8 key = NameGenderKey(race, gender);
    for (uint8 attempt = 0; attempt < 5; ++attempt)
    {
        QueryResult result = CharacterDatabase.Query(
            "SELECT n.name FROM playerbots_names n "
            "LEFT OUTER JOIN characters c ON c.name = n.name "
            "WHERE c.guid IS NULL AND n.gender = {} "
            "ORDER BY RAND() LIMIT 1",
            uint8(key));
        if (!result)
            return {};

        std::string name = (*result)[0].Get<std::string>();
        if (ObjectMgr::CheckPlayerName(name, true) == CHAR_NAME_SUCCESS)
            return name;
    }
    return {};
}

// The addclass roster was built from Wrath's original pairs. A legal All Races
// All Classes pair that is not in that roster is created on an addclass account.
// The character insert is prepared only for the asynchronous connection, so the
// save is committed there and onSaved runs after it lands.
bool CreateAddclassCharacter(uint8 race, uint8 cls, uint8 gender, WorldSession* masterSession,
                             std::function<void(ObjectGuid)> onSaved)
{
    QueryResult account = PlayerbotsDatabase.Query(
        "SELECT account_id FROM playerbots_account_type WHERE account_type = 2 LIMIT 1");
    if (!account || !masterSession)
        return false;

    uint32 accountId = (*account)[0].Get<uint32>();
    if (gender >= GENDER_NONE)
        gender = urand(0, 1) ? GENDER_MALE : GENDER_FEMALE;

    std::vector<std::pair<uint8, uint8>> faces;
    std::vector<std::pair<uint8, uint8>> hairs;
    std::vector<uint8> facialHairTypes;
    for (CharSectionsEntry const* section : sCharSectionsStore)
    {
        if (section->RaceID != race || section->SexID != gender)
            continue;

        if (section->BaseSection == SECTION_TYPE_FACE)
            faces.emplace_back(uint8(section->VariationIndex), uint8(section->ColorIndex));
        else if (section->BaseSection == SECTION_TYPE_HAIR)
            hairs.emplace_back(uint8(section->VariationIndex), uint8(section->ColorIndex));
        else if (section->BaseSection == SECTION_TYPE_FACIAL_HAIR)
            facialHairTypes.push_back(uint8(section->VariationIndex));
    }
    if (faces.empty() || hairs.empty())
        return false;

    std::pair<uint8, uint8> face = faces[urand(0, faces.size() - 1)];
    std::pair<uint8, uint8> hair = hairs[urand(0, hairs.size() - 1)];
    bool skipFacialHair = race == RACE_TAUREN || race == RACE_DRAENEI ||
                          (gender == GENDER_FEMALE && race != RACE_NIGHTELF && race != RACE_UNDEAD_PLAYER);
    uint8 facialHair = skipFacialHair || facialHairTypes.empty() ? 0 : facialHairTypes[urand(0, facialHairTypes.size() - 1)];

    std::string name = UnusedBotName(race, gender);
    if (name.empty())
        return false;

    WorldSession* session = new WorldSession(accountId, "", 0, nullptr, SEC_PLAYER, EXPANSION_WRATH_OF_THE_LICH_KING,
                                            time_t(0), LOCALE_enUS, 0, false, false, 0);
    CharacterCreateInfo info(name, race, cls, gender, face.second, face.first, hair.first, hair.second, facialHair);
    Player* bot = new Player(session);
    bot->GetMotionMaster()->Initialize();
    if (!bot->Create(sObjectMgr->GetGenerator<HighGuid::Player>().Generate(), &info))
    {
        bot->CleanupsBeforeDelete();
        delete bot;
        delete session;
        return false;
    }

    bot->setCinematic(2);
    bot->SetAtLoginFlag(AT_LOGIN_NONE);
    if (cls == CLASS_DEATH_KNIGHT)
        bot->learnSpell(50977, false);

    ObjectGuid guid = bot->GetGUID();
    uint8 savedGender = bot->getGender();
    uint8 savedLevel = bot->GetLevel();
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    bot->SaveToDB(trans, true, false);
    bot->CleanupsBeforeDelete();
    delete bot;
    delete session;

    masterSession->AddTransactionCallback(CharacterDatabase.AsyncCommitTransaction(trans)).AfterComplete(
        [guid, accountId, name, savedGender, race, cls, savedLevel, onSaved](bool success)
        {
            if (!success)
                return;

            sCharacterCache->AddCharacterCacheEntry(guid, accountId, name, savedGender, race, cls, savedLevel);
            bool alliance = Player::TeamIdForRace(race) == TEAM_ALLIANCE;
            sRandomPlayerbotMgr.addclassCache[RandomPlayerbotMgr::GetTeamClassIdx(alliance, cls)].insert(guid);
            onSaved(guid);
        });
    return true;
}

bool EligibleBot(ObjectGuid guid, uint8 race)
{
    if (g_pending.find(guid) != g_pending.end())
        return false;
    if (OfflineRace(guid) != race)
        return false;
    if (ObjectAccessor::FindConnectedPlayer(guid))
        return false;
    uint32 guildId = sCharacterCache->GetCharacterGuildIdByGuid(guid);
    if (guildId && PlayerbotGuildMgr::instance().IsRealGuild(guildId))
        return false;
    if (!sRandomPlayerbotMgr.IsAddclassBot(guid.GetCounter()))
        return false;
    return true;
}

bool InMasterGroup(Player const* master, ObjectGuid guid)
{
    Group const* group = master->GetGroup();
    return group && group->IsMember(guid);
}

uint32 ReservedSlots(Player const* master)
{
    uint32 slots = master->GetGroup() ? master->GetGroup()->GetMembersCount() : 1;
    for (auto const& entry : g_pending)
    {
        if (entry.second.masterGuid != master->GetGUID())
            continue;
        if (InMasterGroup(master, entry.first))
            continue;
        ++slots;
    }
    return slots;
}

void RememberRecruit(ObjectGuid botGuid, RecruitRequest const& request, ObjectGuid masterGuid, int specNo)
{
    PendingRecruit pending;
    pending.masterGuid = masterGuid;
    pending.specNo = specNo;
    pending.gear = request.gear;
    pending.summon = request.summon;
    pending.stance = request.stance;
    pending.activity = request.activity;
    pending.startedMs = getMSTime();
    pending.teamSummon = request.teamSummon;
    pending.assignTeam = request.assignTeam;
    pending.applySpec = request.applySpec && specNo >= 0;
    pending.team = request.team;
    pending.levelBefore = request.levelBefore;
    g_pending[botGuid] = pending;
}

void SaveHire(PendingRecruit const& pending, ObjectGuid botGuid)
{
    uint32 masterLow = pending.masterGuid.GetCounter();
    uint32 botLow = botGuid.GetCounter();
    QueryResult existing = CharacterDatabase.Query(
        "SELECT slot FROM bot_recruiter_hire WHERE master_guid = {} AND bot_guid = {}", masterLow, botLow);

    uint8 slot = 0;
    if (existing)
        slot = (*existing)[0].Get<uint8>();
    else
    {
        QueryResult rows = CharacterDatabase.Query(
            "SELECT slot FROM bot_recruiter_hire WHERE master_guid = {} ORDER BY slot", masterLow);
        std::vector<uint8> slots;
        if (rows)
        {
            do
                slots.push_back((*rows)[0].Get<uint8>());
            while (rows->NextRow());
        }

        if (slots.size() >= SAVED_PARTY_LIMIT)
        {
            CharacterDatabase.DirectExecute(
                "DELETE FROM bot_recruiter_hire WHERE master_guid = {} AND slot = {}", masterLow, uint32(slots.front()));
            slot = slots.front();
        }
        else
        {
            for (uint8 used : slots)
            {
                if (used != slot)
                    break;
                ++slot;
            }
        }
    }

    CharacterDatabase.DirectExecute(
        "REPLACE INTO bot_recruiter_hire (master_guid, slot, bot_guid, spec_no, gear_band, summon, stance, activity) "
        "VALUES ({}, {}, {}, {}, {}, {}, {}, {})",
        masterLow, uint32(slot), botLow, pending.specNo, uint32(pending.gear), pending.summon ? 1 : 0,
        uint32(pending.stance), uint32(pending.activity));
}

void ApplyGear(Player* master, Player* bot, GearBand gear)
{
    if (gear == GearBand::Stored)
        return;

    if (gear == GearBand::Match)
    {
        uint32 mixed = PlayerbotAI::GetMixedGearScore(master, true, false, 12);
        if (mixed == 0)
            mixed = 1;
        float multiplier = PlayerbotAI::GetItemScoreMultiplier(ITEM_QUALITY_LEGENDARY);
        uint32 itemLevel = std::max<uint32>(1, uint32(float(mixed) / multiplier));
        PlayerbotFactory::AutoGear(bot, ITEM_QUALITY_LEGENDARY, itemLevel, false);
        return;
    }

    GearChoice const* choice = FindGear(gear);
    if (choice && choice->itemLevel)
        PlayerbotFactory::AutoGear(bot, ITEM_QUALITY_EPIC, choice->itemLevel, false);
}

// A saved teammate logs in on the map stored in the character row. An instance
// that is not loaded has no map object, and the login that follows then faults.
// Write the row onto the master's live map before that login is queried.
void PlaceBesideMaster(Player* master, ObjectGuid botGuid)
{
    if (!master || !master->IsInWorld() || !master->GetMap() || botGuid.IsEmpty())
        return;

    if (Player* already = ObjectAccessor::FindConnectedPlayer(botGuid))
        if (already->IsInWorld())
            return;

    CharacterDatabase.DirectExecute(
        "UPDATE characters SET map = {}, instance_id = {}, zone = {}, position_x = {}, position_y = {}, "
        "position_z = {}, orientation = {}, transguid = 0, taxi_path = '' WHERE guid = {} AND online = 0",
        master->GetMapId(), master->GetInstanceId(), master->GetZoneId(), master->GetPositionX(),
        master->GetPositionY(), master->GetPositionZ(), master->GetOrientation(), botGuid.GetCounter());
}

void ApplyOrders(Player* master, Player* bot, bool summon, RecruitStance stance)
{
    if (summon && master->IsInWorld() && master->GetMap() && master->GetSession() &&
        !master->GetSession()->IsLoggingOut() && bot->IsInWorld() && bot->GetMap())
        bot->TeleportTo(master->GetMapId(), master->GetPositionX(), master->GetPositionY(), master->GetPositionZ(),
                        master->GetOrientation());

    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI)
        return;

    // Set the strategies directly, then queue the same whisper a player would send.
    // The follow whisper clears passive, so a passive order is applied after it and
    // is not also queued as "follow".
    if (summon)
    {
        botAI->ChangeStrategy("+follow,-passive,-grind,-move from group", BOT_STATE_NON_COMBAT);
        botAI->ChangeStrategy("-stay,-follow,-passive,-grind,-move from group", BOT_STATE_COMBAT);
    }
    else
    {
        botAI->ChangeStrategy("+stay,-passive,-move from group", BOT_STATE_NON_COMBAT);
        botAI->ChangeStrategy("+stay,-follow,-passive,-move from group", BOT_STATE_COMBAT);
    }

    if (stance == RecruitStance::Passive)
    {
        botAI->ChangeStrategy("+passive", BOT_STATE_NON_COMBAT);
        botAI->ChangeStrategy("+passive", BOT_STATE_COMBAT);
        botAI->HandleCommand(CHAT_MSG_WHISPER, "nc +passive", master);
        botAI->HandleCommand(CHAT_MSG_WHISPER, "co +passive", master);
    }
    else
    {
        if (summon)
            botAI->HandleCommand(CHAT_MSG_WHISPER, "follow", master);
        else
            botAI->HandleCommand(CHAT_MSG_WHISPER, "stay", master);
        botAI->HandleCommand(CHAT_MSG_WHISPER, "co -passive", master);
    }
}

void ApplyPendingSpecs()
{
    if (g_pending.empty())
        return;

    std::vector<ObjectGuid> finished;
    for (auto& entry : g_pending)
    {
        ObjectGuid botGuid = entry.first;
        PendingRecruit& pending = entry.second;
        Player* master = ObjectAccessor::FindConnectedPlayer(pending.masterGuid);
        if (!master)
        {
            finished.push_back(botGuid);
            continue;
        }

        Player* bot = ObjectAccessor::FindConnectedPlayer(botGuid);
        PlayerbotMgr* mgr = GET_PLAYERBOT_MGR(master);
        bool loggedIn = bot && bot->IsInWorld() && mgr && mgr->GetPlayerBot(botGuid);
        if (loggedIn && !pending.specApplied)
        {
            // OnBotLogin inserts the bot before it rolls talents. Wait one world
            // tick after the bot appears there, so the roll has returned.
            if (!pending.loginFinished)
                pending.loginFinished = true;
            else
            {
                if (pending.applySpec && pending.specNo < MAX_SPECNO && bot->getClass() < MAX_CLASSES && bot->GetMap())
                    PlayerbotFactory::InitTalentsBySpecNo(bot, pending.specNo, true);
                if (pending.teamSummon)
                {
                    if (GearSetSaved(botGuid, KindFor(pending.team)))
                        EquipSavedSet(master, bot, KindFor(pending.team));
                    if (pending.levelBefore != 0 && bot->GetLevel() != pending.levelBefore)
                    {
                        std::string name;
                        sCharacterCache->GetCharacterNameByGuid(botGuid, name);
                        std::string text = (name.empty() ? "A team member" : name) + " was raised to your level.";
                        if (pending.applySpec || GearSetSaved(botGuid, KindFor(pending.team)))
                            text += " Their saved build was put back.";
                        ChatHandler chat(master->GetSession());
                        Tell(&chat, text);
                    }
                    g_active[botGuid] = ActiveCall{pending.masterGuid, pending.team};
                }
                else
                    ApplyGear(master, bot, pending.gear);
                if (pending.assignTeam)
                    g_active[botGuid] = ActiveCall{pending.masterGuid, pending.team};
                // Login builds combat strategies before these talents exist. The
                // dungeon finder asks playerbots for a role, and that answer is
                // the tank or heal strategy, so rebuild it from the spec now.
                // ApplyOrders runs after, because the rebuild clears follow.
                if (pending.applySpec)
                    if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot))
                        botAI->ResetStrategies();
                ApplyOrders(master, bot, pending.summon, pending.stance);
                bot->SaveToDB(false, false);
                if (!pending.teamSummon)
                    SaveHire(pending, botGuid);
                pending.specApplied = true;
                pending.appliedMs = getMSTime();
            }
        }

        if (pending.specApplied && InMasterGroup(master, botGuid))
            finished.push_back(botGuid);
        else if (!pending.specApplied && GetMSTimeDiffToNow(pending.startedMs) > LOGIN_WAIT_MS)
            finished.push_back(botGuid);
        else if (pending.specApplied && GetMSTimeDiffToNow(pending.appliedMs) > GROUP_WAIT_MS)
            finished.push_back(botGuid);
    }

    for (ObjectGuid guid : finished)
        g_pending.erase(guid);
}

bool Recruit(Player* master, ChatHandler* handler, RecruitRequest const& request)
{
    uint8 cls = request.cls;
    uint8 race = request.race;
    RecruitActivity activity = request.activity;
    uint8 tree = request.tree;
    if (!sPlayerbotAIConfig.Enabled)
    {
        Tell(handler, "Playerbots is disabled.");
        return false;
    }

    if (!master || !master->GetSession())
    {
        Tell(handler, "You may only recruit a bot from an active session.");
        return false;
    }

    PlayerbotMgr* mgr = GET_PLAYERBOT_MGR(master);
    if (!mgr)
    {
        Tell(handler, "You cannot control bots yet.");
        return false;
    }

    if (sPlayerbotAIConfig.AddClassCommand == 0 && !master->CanBeGameMaster())
    {
        Tell(handler, "You do not have permission to recruit a bot.");
        return false;
    }

    ClassChoice const* classChoice = FindClass(cls);
    RaceChoice const* raceChoice = FindRace(race);
    ActivityChoice const* activityChoice = FindActivity(activity);
    if (!classChoice || !raceChoice || !activityChoice || tree > 2)
    {
        Tell(handler, "Usage: .recruiter add <class> <race> <activity> <spec>");
        return false;
    }

    if (!SameFaction(master, race))
    {
        Tell(handler, "That race is the other faction.");
        return false;
    }

    if (!ClassLegalForRace(race, cls))
    {
        Tell(handler, "That race cannot be that class.");
        return false;
    }

    if (!DeathKnightAllowed(master, cls))
    {
        Tell(handler, "Your level is too low to summon a death knight.");
        return false;
    }

    if (activityChoice->capAtFive && ReservedSlots(master) >= 5)
    {
        Tell(handler, std::string(activityChoice->label) + " recruits stop at 5 members.");
        return false;
    }

    if (mgr->GetPlayerbotsCount() >= uint32(sPlayerbotAIConfig.MaxAddedBots))
    {
        Tell(handler, "You have added too many bots.");
        return false;
    }

    if (request.assignTeam)
    {
        TeamChoice const* teamChoice = FindTeam(request.team);
        bool already = !request.preferred.IsEmpty() && OnTeam(master->GetGUID(), request.preferred, request.team);
        if (teamChoice && !already && TeamCount(master->GetGUID(), request.team) >= teamChoice->cap)
        {
            Tell(handler, std::string(teamChoice->label) + " already has " + std::to_string(teamChoice->cap) + " members.");
            return false;
        }
    }

    bool alliance = master->GetTeamId(true) == TEAM_ALLIANCE;
    std::unordered_set<ObjectGuid> const& guidCache =
        sRandomPlayerbotMgr.addclassCache[RandomPlayerbotMgr::GetTeamClassIdx(alliance, cls)];

    auto const eligible = [&](ObjectGuid const& guid) -> bool { return EligibleBot(guid, race); };

    int specNo = request.teamSummon ? request.specNo
                                   : (request.specNo >= 0 ? request.specNo : SpecNumber(classChoice->trees[tree], activity));
    GearChoice const* gearChoice = FindGear(request.gear);
    StanceChoice const* stanceChoice = FindStance(request.stance);
    std::string notice = std::string("Recruiting a ") + raceChoice->label + " " + classChoice->label + " for " +
                         activityChoice->label + ": " + classChoice->trees[tree].label + ". Gear: " +
                         (gearChoice ? gearChoice->label : "Leave their gear") + ". Orders: " +
                         (request.summon ? "meet you, " : "stay put, ") +
                         (stanceChoice ? stanceChoice->label : "Damage") + ".";
    if (request.teamSummon)
        notice.clear();

    ObjectGuid picked = request.preferred;
    if (request.createNew)
        picked = ObjectGuid::Empty;
    else if (picked.IsEmpty())
    {
        if (sPlayerbotAIConfig.AddClassRandomCharacter)
        {
            std::vector<ObjectGuid> candidates(guidCache.begin(), guidCache.end());
            Acore::Containers::RandomShuffle(candidates);
            for (ObjectGuid const& guid : candidates)
            {
                if (eligible(guid))
                {
                    picked = guid;
                    break;
                }
            }
        }
        else
        {
            for (ObjectGuid const& guid : guidCache)
            {
                if (eligible(guid))
                {
                    picked = guid;
                    break;
                }
            }
        }
    }
    else if (!eligible(picked))
    {
        Tell(handler, "That recruit is not available.");
        return false;
    }

    if (picked.IsEmpty())
    {
        ObjectGuid masterGuid = master->GetGUID();
        uint32 masterAccountId = master->GetSession()->GetAccountId();
        RecruitRequest saved = request;
        if (!CreateAddclassCharacter(race, cls, request.gender, master->GetSession(),
                                     [masterGuid, masterAccountId, specNo, notice, saved](ObjectGuid guid)
                                     {
                                         Player* masterNow = ObjectAccessor::FindConnectedPlayer(masterGuid);
                                         if (!masterNow || !masterNow->GetSession())
                                             return;

                                         RememberRecruit(guid, saved, masterGuid, specNo);
                                         if (saved.assignTeam)
                                             AssignToTeam(masterNow, nullptr, guid, saved.team, specNo);
                                         if (PlayerbotMgr* botMgr = GET_PLAYERBOT_MGR(masterNow))
                                             botMgr->AddPlayerBot(guid, masterAccountId);
                                         ChatHandler chat(masterNow->GetSession());
                                         if (!notice.empty())
                                             chat.PSendSysMessage("{}", notice);
                                     }))
        {
            Tell(handler, "No available character of that race and class.");
            return false;
        }

        Tell(handler, "Creating that recruit. They will join in a moment.");
        return true;
    }

    RememberRecruit(picked, request, master->GetGUID(), specNo);
    if (request.assignTeam)
        AssignToTeam(master, handler, picked, request.team, specNo);
    PlaceBesideMaster(master, picked);
    mgr->AddPlayerBot(picked, master->GetSession()->GetAccountId());
    if (!notice.empty())
        Tell(handler, notice);
    return true;
}

void ShowActivities(Player* player, Creature* creature)
{
    ClearGossipMenuFor(player);
    g_menu[player->GetGUID()] = MenuState();
    AddGossipItemFor(player, GOSSIP_ICON_CHAT, "My teams", SENDER_TEAMS, 0);
    for (ActivityChoice const& choice : kActivities)
        AddGossipItemFor(player, GOSSIP_ICON_BATTLE, choice.label, SENDER_ACTIVITY, uint32(choice.activity));
    AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Send them away", SENDER_DISMISS, 0);
    SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature);
}

void ShowRaces(Player* player, Creature* creature)
{
    ClearGossipMenuFor(player);
    for (RaceChoice const& choice : kRaces)
    {
        if (!SameFaction(player, choice.race))
            continue;

        bool anyClass = false;
        for (ClassChoice const& classChoice : kClasses)
        {
            if (ClassLegalForRace(choice.race, classChoice.cls) && DeathKnightAllowed(player, classChoice.cls))
            {
                anyClass = true;
                break;
            }
        }
        if (!anyClass)
            continue;

        AddGossipItemFor(player, GOSSIP_ICON_CHAT, choice.label, SENDER_RACE, choice.race);
    }
    SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature);
}

void ShowClasses(Player* player, Creature* creature, uint8 race)
{
    ClearGossipMenuFor(player);
    for (ClassChoice const& choice : kClasses)
    {
        if (!ClassLegalForRace(race, choice.cls) || !DeathKnightAllowed(player, choice.cls))
            continue;
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, choice.label, SENDER_CLASS, choice.cls);
    }
    SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature);
}

void ShowSpecs(Player* player, Creature* creature, uint8 cls)
{
    ClearGossipMenuFor(player);
    ClassChoice const* choice = FindClass(cls);
    if (!choice)
        return;

    for (uint8 tree = 0; tree < 3; ++tree)
        AddGossipItemFor(player, GOSSIP_ICON_TRAINER, choice->trees[tree].label, SENDER_SPEC, tree);
    SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature);
}

void ShowGears(Player* player, Creature* creature)
{
    ClearGossipMenuFor(player);
    for (GearChoice const& choice : kGears)
        AddGossipItemFor(player, GOSSIP_ICON_VENDOR, choice.label, SENDER_GEAR, uint32(choice.band));
    SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature);
}

void ShowArrival(Player* player, Creature* creature)
{
    ClearGossipMenuFor(player);
    AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Meet me here", SENDER_ARRIVE, 1);
    AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Leave them where they are", SENDER_ARRIVE, 0);
    SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature);
}

void ShowStances(Player* player, Creature* creature)
{
    ClearGossipMenuFor(player);
    for (StanceChoice const& choice : kStances)
        AddGossipItemFor(player, GOSSIP_ICON_BATTLE, choice.label, SENDER_STANCE, uint32(choice.stance));
    SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature);
}

void ShowPeople(Player* player, Creature* creature)
{
    ClearGossipMenuFor(player);
    MenuState const& state = g_menu[player->GetGUID()];
    bool alliance = player->GetTeamId(true) == TEAM_ALLIANCE;
    std::unordered_set<ObjectGuid> const& guidCache =
        sRandomPlayerbotMgr.addclassCache[RandomPlayerbotMgr::GetTeamClassIdx(alliance, state.cls)];

    std::vector<std::pair<std::string, ObjectGuid>> named;
    for (ObjectGuid const& guid : guidCache)
    {
        if (!EligibleBot(guid, state.race))
            continue;
        std::string name;
        if (!sCharacterCache->GetCharacterNameByGuid(guid, name))
            continue;
        named.emplace_back(name, guid);
    }
    std::sort(named.begin(), named.end(),
              [](std::pair<std::string, ObjectGuid> const& left, std::pair<std::string, ObjectGuid> const& right)
              { return left.first < right.first; });

    uint32 shown = 0;
    for (auto const& entry : named)
    {
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, entry.first, SENDER_PERSON, entry.second.GetCounter());
        if (++shown >= 8)
            break;
    }
    AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Someone new", SENDER_PERSON, PERSON_NEW);
    SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature);
}

void ShowGenders(Player* player, Creature* creature)
{
    ClearGossipMenuFor(player);
    AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Male", SENDER_GENDER, GENDER_MALE);
    AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Female", SENDER_GENDER, GENDER_FEMALE);
    SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature);
}

RecruitRequest RequestFromMenu(MenuState const& state)
{
    RecruitRequest request;
    request.cls = state.cls;
    request.race = state.race;
    request.activity = state.activity;
    request.tree = state.tree;
    request.gear = state.gear;
    request.summon = state.summon;
    request.stance = state.stance;
    request.assignTeam = state.assignTeam;
    request.team = state.team;
    return request;
}

bool OnTeam(ObjectGuid masterGuid, ObjectGuid botGuid, SavedTeam team)
{
    if (botGuid.IsEmpty())
        return false;

    QueryResult result = CharacterDatabase.Query(
        "SELECT 1 FROM bot_recruiter_team WHERE master_guid = {} AND team = {} AND bot_guid = {} LIMIT 1",
        masterGuid.GetCounter(), uint32(team), botGuid.GetCounter());
    return bool(result);
}

uint8 TeamCount(ObjectGuid masterGuid, SavedTeam team)
{
    QueryResult result = CharacterDatabase.Query(
        "SELECT slot FROM bot_recruiter_team WHERE master_guid = {} AND team = {}", masterGuid.GetCounter(),
        uint32(team));
    if (!result)
        return 0;

    uint8 count = 0;
    do
    {
        ++count;
    } while (result->NextRow());
    return count;
}

bool GearSetSaved(ObjectGuid botGuid, LoadoutKind kind)
{
    QueryResult result = CharacterDatabase.Query(
        "SELECT gear_saved FROM bot_recruiter_loadout WHERE bot_guid = {} AND kind = {} LIMIT 1", botGuid.GetCounter(),
        uint32(kind));
    return result && (*result)[0].Get<uint8>() != 0;
}

void StoreWornSet(Player* bot, LoadoutKind kind)
{
    uint32 botLow = bot->GetGUID().GetCounter();
    int specNo = -1;
    QueryResult row = CharacterDatabase.Query(
        "SELECT spec_no FROM bot_recruiter_loadout WHERE bot_guid = {} AND kind = {}", botLow, uint32(kind));
    if (row)
        specNo = (*row)[0].Get<int32>();

    CharacterDatabase.DirectExecute("DELETE FROM bot_recruiter_loadout_item WHERE bot_guid = {} AND kind = {}", botLow,
                                    uint32(kind));
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!item)
            continue;

        CharacterDatabase.DirectExecute(
            "REPLACE INTO bot_recruiter_loadout_item (bot_guid, kind, equip_slot, item_guid, item_entry) "
            "VALUES ({}, {}, {}, {}, {})",
            botLow, uint32(kind), uint32(slot), item->GetGUID().GetCounter(), item->GetEntry());
    }

    CharacterDatabase.DirectExecute(
        "REPLACE INTO bot_recruiter_loadout (bot_guid, kind, spec_no, gear_saved) VALUES ({}, {}, {}, 1)", botLow,
        uint32(kind), specNo);
}

void MaybeUpdateSet(Player* bot, LoadoutKind kind)
{
    if (!bot)
        return;

    if (!GearSetSaved(bot->GetGUID(), kind))
    {
        StoreWornSet(bot, kind);
        return;
    }

    QueryResult rows = CharacterDatabase.Query(
        "SELECT equip_slot, item_guid FROM bot_recruiter_loadout_item WHERE bot_guid = {} AND kind = {}",
        bot->GetGUID().GetCounter(), uint32(kind));
    if (rows)
    {
        do
        {
            Field* fields = rows->Fetch();
            Item* worn = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, fields[0].Get<uint8>());
            if (!worn || worn->GetGUID().GetCounter() != fields[1].Get<uint32>())
                return;
        } while (rows->NextRow());
    }

    StoreWornSet(bot, kind);
}

void SnapshotActiveSets(Player* master)
{
    if (!master)
        return;

    std::vector<ObjectGuid> mine;
    for (auto const& entry : g_active)
        if (entry.second.master == master->GetGUID())
            mine.push_back(entry.first);

    for (ObjectGuid guid : mine)
    {
        if (Player* bot = ObjectAccessor::FindConnectedPlayer(guid))
            if (bot->IsInWorld())
                MaybeUpdateSet(bot, KindFor(g_active[guid].team));
        g_active.erase(guid);
    }
}

void EquipSavedSet(Player* master, Player* bot, LoadoutKind kind)
{
    if (!master->GetSession() || !bot->IsInWorld())
        return;

    ChatHandler handler(master->GetSession());
    if (!GearSetSaved(bot->GetGUID(), kind))
    {
        Tell(&handler, "That character has no saved set of that kind yet.");
        return;
    }

    QueryResult rows = CharacterDatabase.Query(
        "SELECT equip_slot, item_guid FROM bot_recruiter_loadout_item WHERE bot_guid = {} AND kind = {}",
        bot->GetGUID().GetCounter(), uint32(kind));
    if (!rows)
        return;

    uint32 missing = 0;
    do
    {
        Field* fields = rows->Fetch();
        uint8 slot = fields[0].Get<uint8>();
        Item* item = bot->GetItemByGuid(ObjectGuid::Create<HighGuid::Item>(fields[1].Get<uint32>()));
        if (!item)
        {
            ++missing;
            continue;
        }

        Item* worn = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (worn && worn->GetGUID() == item->GetGUID())
            continue;

        uint16 dest = 0;
        if (bot->CanEquipItem(slot, dest, item, true) != EQUIP_ERR_OK)
        {
            ++missing;
            continue;
        }
        bot->EquipItem(dest, item, true);
    } while (rows->NextRow());

    if (missing)
        Tell(&handler, "Some pieces of the saved set are gone, so those slots were left alone.");
}

void AssignToTeam(Player* master, ChatHandler* handler, ObjectGuid botGuid, SavedTeam team, int specNo)
{
    TeamChoice const* choice = FindTeam(team);
    if (!master || !choice || botGuid.IsEmpty())
        return;

    uint32 masterLow = master->GetGUID().GetCounter();
    uint32 botLow = botGuid.GetCounter();
    LoadoutKind kind = KindFor(team);
    int storedSpec = specNo;
    uint8 gearSaved = 0;
    QueryResult loadout = CharacterDatabase.Query(
        "SELECT spec_no, gear_saved FROM bot_recruiter_loadout WHERE bot_guid = {} AND kind = {}", botLow, uint32(kind));
    if (loadout)
    {
        if (specNo < 0)
            storedSpec = (*loadout)[0].Get<int32>();
        gearSaved = (*loadout)[1].Get<uint8>();
    }

    CharacterDatabase.DirectExecute(
        "REPLACE INTO bot_recruiter_loadout (bot_guid, kind, spec_no, gear_saved) VALUES ({}, {}, {}, {})", botLow,
        uint32(kind), storedSpec, uint32(gearSaved));

    if (OnTeam(master->GetGUID(), botGuid, team))
        return;

    if (TeamCount(master->GetGUID(), team) >= choice->cap)
    {
        Tell(handler, std::string(choice->label) + " already has " + std::to_string(choice->cap) + " members.");
        return;
    }

    uint8 slot = 0;
    QueryResult slots = CharacterDatabase.Query(
        "SELECT slot FROM bot_recruiter_team WHERE master_guid = {} AND team = {} ORDER BY slot", masterLow, uint32(team));
    if (slots)
    {
        do
        {
            if ((*slots)[0].Get<uint8>() != slot)
                break;
            ++slot;
        } while (slots->NextRow());
    }

    CharacterDatabase.DirectExecute(
        "REPLACE INTO bot_recruiter_team (master_guid, team, slot, bot_guid) VALUES ({}, {}, {}, {})", masterLow,
        uint32(team), uint32(slot), botLow);
}

bool SummonTeam(Player* master, ChatHandler* handler, SavedTeam team)
{
    TeamChoice const* choice = FindTeam(team);
    if (!master || !choice)
        return false;

    QueryResult result = CharacterDatabase.Query(
        "SELECT bot_guid FROM bot_recruiter_team WHERE master_guid = {} AND team = {} ORDER BY slot",
        master->GetGUID().GetCounter(), uint32(team));
    if (!result)
    {
        Tell(handler, std::string("Your ") + choice->label + " team has no members.");
        return false;
    }

    PlayerbotMgr* mgr = GET_PLAYERBOT_MGR(master);
    ActivityChoice const* activityChoice = FindActivity(choice->activity);
    uint32 called = 0;
    uint32 skipped = 0;
    uint32 waiting = 0;
    do
    {
        ObjectGuid guid = ObjectGuid::Create<HighGuid::Player>((*result)[0].Get<uint32>());
        if ((mgr && mgr->GetPlayerBot(guid)) || g_pending.find(guid) != g_pending.end())
        {
            ++skipped;
            continue;
        }

        if (activityChoice && activityChoice->capAtFive && ReservedSlots(master) >= 5)
        {
            ++waiting;
            continue;
        }

        CharacterCacheEntry const* entry = sCharacterCache->GetCharacterCacheByGuid(guid);
        if (!entry || !EligibleBot(guid, entry->Race))
            continue;

        int specNo = -1;
        QueryResult spec = CharacterDatabase.Query(
            "SELECT spec_no FROM bot_recruiter_loadout WHERE bot_guid = {} AND kind = {}", guid.GetCounter(),
            uint32(KindFor(team)));
        if (spec)
            specNo = (*spec)[0].Get<int32>();

        RecruitRequest request;
        request.cls = entry->Class;
        request.race = entry->Race;
        request.activity = choice->activity;
        request.gear = GearBand::Stored;
        request.summon = true;
        request.preferred = guid;
        request.specNo = specNo;
        request.teamSummon = true;
        request.applySpec = specNo >= 0;
        request.team = team;
        request.levelBefore = sCharacterCache->GetCharacterLevelByGuid(guid);
        if (Recruit(master, handler, request))
            ++called;
    } while (result->NextRow());

    if (called)
        Tell(handler, std::string("Calling your ") + choice->label + " team.");
    else if (!skipped && !waiting)
        Tell(handler, "None of that team is free to join.");
    if (skipped)
        Tell(handler, std::to_string(skipped) + " already with you were left as they are.");
    if (waiting)
        Tell(handler, std::to_string(waiting) + " stayed behind because the party is full.");
    return called != 0;
}

void SaveGroupAsTeam(Player* master, ChatHandler* handler, SavedTeam team)
{
    TeamChoice const* choice = FindTeam(team);
    Group* group = master ? master->GetGroup() : nullptr;
    if (!choice)
        return;
    if (!group)
    {
        Tell(handler, "You are not in a group.");
        return;
    }

    uint32 masterLow = master->GetGUID().GetCounter();
    CharacterDatabase.DirectExecute("DELETE FROM bot_recruiter_team WHERE master_guid = {} AND team = {}", masterLow,
                                    uint32(team));

    uint8 slot = 0;
    for (GroupReference* ref = group->GetFirstMember(); ref && slot < choice->cap; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || member == master || !sRandomPlayerbotMgr.IsAddclassBot(member->GetGUID().GetCounter()))
            continue;

        uint32 botLow = member->GetGUID().GetCounter();
        CharacterDatabase.DirectExecute(
            "REPLACE INTO bot_recruiter_team (master_guid, team, slot, bot_guid) VALUES ({}, {}, {}, {})", masterLow,
            uint32(team), uint32(slot), botLow);

        LoadoutKind kind = KindFor(team);
        QueryResult existing = CharacterDatabase.Query(
            "SELECT 1 FROM bot_recruiter_loadout WHERE bot_guid = {} AND kind = {}", botLow, uint32(kind));
        if (!existing)
            CharacterDatabase.DirectExecute(
                "INSERT INTO bot_recruiter_loadout (bot_guid, kind, spec_no, gear_saved) VALUES ({}, {}, -1, 0)", botLow,
                uint32(kind));

        ++slot;
    }

    Tell(handler, "Saved " + std::to_string(slot) + " members to your " + choice->label + " team.");
}

void ShowTeams(Player* player, Creature* creature)
{
    ClearGossipMenuFor(player);
    for (TeamChoice const& choice : kTeams)
        AddGossipItemFor(player, GOSSIP_ICON_BATTLE, choice.label, SENDER_TEAM, uint32(choice.team));
    SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature);
}

void ShowTeam(Player* player, Creature* creature, SavedTeam team)
{
    if (!FindTeam(team))
        return;

    MenuState& state = g_menu[player->GetGUID()];
    state.team = team;
    state.assignTeam = false;
    ClearGossipMenuFor(player);
    AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "Summon team", SENDER_TEAM_SUMMON, uint32(team));
    AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Save my group", SENDER_TEAM_SAVE, uint32(team));
    AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Add a member", SENDER_TEAM_ADD, uint32(team));
    AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Remove a member", SENDER_TEAM_REMOVE, uint32(team));
    AddGossipItemFor(player, GOSSIP_ICON_VENDOR, "Gear sets", SENDER_SET_LIST, uint32(team));
    SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature);
}

void ShowTeamMembers(Player* player, Creature* creature, SavedTeam team)
{
    ClearGossipMenuFor(player);
    QueryResult result = CharacterDatabase.Query(
        "SELECT bot_guid FROM bot_recruiter_team WHERE master_guid = {} AND team = {} ORDER BY slot",
        player->GetGUID().GetCounter(), uint32(team));
    if (result)
    {
        do
        {
            ObjectGuid guid = ObjectGuid::Create<HighGuid::Player>((*result)[0].Get<uint32>());
            std::string name;
            if (!sCharacterCache->GetCharacterNameByGuid(guid, name))
                name = "Unknown";
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, name, SENDER_TEAM_DROP, guid.GetCounter());
        } while (result->NextRow());
    }
    else
    {
        ChatHandler handler(player->GetSession());
        Tell(&handler, "That team has no members.");
        CloseGossipMenuFor(player);
        return;
    }
    SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature);
}

void ShowSetBots(Player* player, Creature* creature)
{
    ClearGossipMenuFor(player);
    PlayerbotMgr* mgr = GET_PLAYERBOT_MGR(player);
    bool any = false;
    if (mgr)
    {
        for (PlayerBotMap::const_iterator itr = mgr->GetPlayerBotsBegin(); itr != mgr->GetPlayerBotsEnd(); ++itr)
        {
            std::string name;
            if (!sCharacterCache->GetCharacterNameByGuid(itr->first, name) && itr->second)
                name = itr->second->GetName();
            if (name.empty())
                continue;
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, name, SENDER_SET_BOT, itr->first.GetCounter());
            any = true;
        }
    }
    if (!any)
    {
        ChatHandler handler(player->GetSession());
        Tell(&handler, "None of your recruits are here to change sets.");
        CloseGossipMenuFor(player);
        return;
    }
    SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature);
}

void ShowSetActions(Player* player, Creature* creature, ObjectGuid botGuid)
{
    g_menu[player->GetGUID()].setBot = botGuid;
    ClearGossipMenuFor(player);
    AddGossipItemFor(player, GOSSIP_ICON_VENDOR, "Save what they are wearing as the PvE set", SENDER_SET_SAVE,
                     uint32(LoadoutKind::Pve));
    AddGossipItemFor(player, GOSSIP_ICON_VENDOR, "Save what they are wearing as the PvP set", SENDER_SET_SAVE,
                     uint32(LoadoutKind::Pvp));
    AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "Equip the PvE set", SENDER_SET_EQUIP, uint32(LoadoutKind::Pve));
    AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "Equip the PvP set", SENDER_SET_EQUIP, uint32(LoadoutKind::Pvp));
    SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature);
}

}  // namespace

class bot_recruiter_worldscript : public WorldScript
{
public:
    bot_recruiter_worldscript() : WorldScript("bot_recruiter_worldscript", {uint16(WORLDHOOK_ON_UPDATE)}) {}

    void OnUpdate(uint32 /*diff*/) override { ApplyPendingSpecs(); }
};

class bot_recruiter_playerscript : public PlayerScript
{
public:
    bot_recruiter_playerscript()
        : PlayerScript("bot_recruiter_playerscript", {uint16(PLAYERHOOK_ON_BEFORE_LOGOUT)})
    {
    }

    void OnPlayerBeforeLogout(Player* player) override { SnapshotActiveSets(player); }
};

class bot_recruiter_commandscript : public CommandScript
{
public:
    bot_recruiter_commandscript() : CommandScript("bot_recruiter_commandscript") {}

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable recruiterCommandTable = {
            {"add", HandleRecruiterAdd, SEC_PLAYER, Console::No},
        };
        static ChatCommandTable commandTable = {
            {"recruiter", recruiterCommandTable},
        };
        return commandTable;
    }

    static bool HandleRecruiterAdd(ChatHandler* handler, char const* args)
    {
        Player* master = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
        if (!args || !*args)
        {
            Tell(handler, "Usage: .recruiter add <class> <race> <activity> <spec>");
            return false;
        }

        std::istringstream in(args);
        std::string classToken;
        std::string raceToken;
        std::string activityToken;
        std::string specToken;
        in >> classToken >> raceToken >> activityToken >> specToken;
        if (classToken.empty() || raceToken.empty() || activityToken.empty() || specToken.empty())
        {
            Tell(handler, "Usage: .recruiter add <class> <race> <activity> <spec>");
            return false;
        }

        classToken = Lower(classToken);
        raceToken = Lower(raceToken);
        activityToken = Lower(activityToken);
        specToken = Lower(specToken);

        ClassChoice const* classChoice = FindClassToken(classToken);
        if (!classChoice)
        {
            Tell(handler, "Unknown class. Use warrior, paladin, hunter, rogue, priest, shaman, mage, warlock, druid, or dk.");
            return false;
        }

        RaceChoice const* raceChoice = FindRaceToken(raceToken);
        if (!raceChoice)
        {
            Tell(handler, "Unknown race. Use human, dwarf, nightelf, gnome, draenei, orc, undead, tauren, troll, or bloodelf.");
            return false;
        }

        ActivityChoice const* activityChoice = FindActivityToken(activityToken);
        if (!activityChoice)
        {
            Tell(handler, "Unknown activity. Use dungeon, raid, arena, or battleground.");
            return false;
        }

        uint8 tree = 3;
        if (specToken == "0" || specToken == "1" || specToken == "2")
            tree = static_cast<uint8>(specToken[0] - '0');
        else
        {
            for (uint8 index = 0; index < 3; ++index)
            {
                if (TokenIn(specToken, classChoice->trees[index].tokens))
                {
                    tree = index;
                    break;
                }
            }
        }
        if (tree > 2)
        {
            Tell(handler, "Unknown spec. Use 0, 1, 2, or that class's talent tree name.");
            return false;
        }

        RecruitRequest request;
        request.cls = classChoice->cls;
        request.race = raceChoice->race;
        request.activity = activityChoice->activity;
        request.tree = tree;
        Recruit(master, handler, request);
        return true;
    }
};

class npc_bot_recruiter : public CreatureScript
{
public:
    npc_bot_recruiter() : CreatureScript("npc_bot_recruiter") {}

    bool OnGossipHello(Player* player, Creature* creature) override
    {
        ShowActivities(player, creature);
        return true;
    }

    bool OnGossipSelect(Player* player, Creature* creature, uint32 sender, uint32 action) override
    {
        if (sender == SENDER_DISMISS)
        {
            SnapshotActiveSets(player);
            char args[] = "remove *";
            ChatHandler handler(player->GetSession());
            PlayerbotMgr::HandlePlayerbotMgrCommand(&handler, args);
            CloseGossipMenuFor(player);
            return true;
        }

        if (sender == SENDER_TEAMS)
        {
            ShowTeams(player, creature);
            return true;
        }

        if (sender == SENDER_TEAM)
        {
            if (!FindTeam(SavedTeam(action)))
                return true;
            ShowTeam(player, creature, SavedTeam(action));
            return true;
        }

        if (sender == SENDER_TEAM_SUMMON)
        {
            if (!FindTeam(SavedTeam(action)))
                return true;
            ChatHandler handler(player->GetSession());
            SummonTeam(player, &handler, SavedTeam(action));
            CloseGossipMenuFor(player);
            return true;
        }

        if (sender == SENDER_TEAM_SAVE)
        {
            if (!FindTeam(SavedTeam(action)))
                return true;
            ChatHandler handler(player->GetSession());
            SaveGroupAsTeam(player, &handler, SavedTeam(action));
            CloseGossipMenuFor(player);
            return true;
        }

        if (sender == SENDER_TEAM_ADD)
        {
            TeamChoice const* teamChoice = FindTeam(SavedTeam(action));
            if (!teamChoice)
                return true;
            MenuState& adding = g_menu[player->GetGUID()];
            adding.team = teamChoice->team;
            adding.activity = teamChoice->activity;
            adding.assignTeam = true;
            ShowRaces(player, creature);
            return true;
        }

        if (sender == SENDER_TEAM_REMOVE)
        {
            if (!FindTeam(SavedTeam(action)))
                return true;
            g_menu[player->GetGUID()].team = SavedTeam(action);
            ShowTeamMembers(player, creature, SavedTeam(action));
            return true;
        }

        if (sender == SENDER_TEAM_DROP)
        {
            ObjectGuid botGuid = ObjectGuid::Create<HighGuid::Player>(action);
            ChatHandler handler(player->GetSession());
            CharacterDatabase.DirectExecute(
                "DELETE FROM bot_recruiter_team WHERE master_guid = {} AND team = {} AND bot_guid = {}",
                player->GetGUID().GetCounter(), uint32(g_menu[player->GetGUID()].team), botGuid.GetCounter());
            std::string name;
            sCharacterCache->GetCharacterNameByGuid(botGuid, name);
            Tell(&handler, (name.empty() ? std::string("That character") : name) + " was removed from the team.");
            CloseGossipMenuFor(player);
            return true;
        }

        if (sender == SENDER_SET_LIST)
        {
            ShowSetBots(player, creature);
            return true;
        }

        if (sender == SENDER_SET_BOT)
        {
            ObjectGuid botGuid = ObjectGuid::Create<HighGuid::Player>(action);
            PlayerbotMgr* mgr = GET_PLAYERBOT_MGR(player);
            if (!mgr || !mgr->GetPlayerBot(botGuid))
            {
                ChatHandler handler(player->GetSession());
                Tell(&handler, "That recruit is not here.");
                CloseGossipMenuFor(player);
                return true;
            }
            ShowSetActions(player, creature, botGuid);
            return true;
        }

        if (sender == SENDER_SET_SAVE || sender == SENDER_SET_EQUIP)
        {
            if (action > uint32(LoadoutKind::Pvp))
                return true;
            MenuState const& menu = g_menu[player->GetGUID()];
            PlayerbotMgr* mgr = GET_PLAYERBOT_MGR(player);
            Player* bot = mgr ? mgr->GetPlayerBot(menu.setBot) : nullptr;
            ChatHandler handler(player->GetSession());
            if (!bot)
            {
                Tell(&handler, "That recruit is not here.");
                CloseGossipMenuFor(player);
                return true;
            }
            LoadoutKind kind = LoadoutKind(action);
            if (sender == SENDER_SET_SAVE)
            {
                StoreWornSet(bot, kind);
                Tell(&handler, kind == LoadoutKind::Pve ? "Saved the PvE set." : "Saved the PvP set.");
            }
            else
                EquipSavedSet(player, bot, kind);
            CloseGossipMenuFor(player);
            return true;
        }

        MenuState& state = g_menu[player->GetGUID()];
        if (sender == SENDER_ACTIVITY)
        {
            if (action > uint32(RecruitActivity::Battleground))
                return true;
            state.activity = RecruitActivity(action);
            ShowRaces(player, creature);
            return true;
        }

        if (sender == SENDER_RACE)
        {
            if (!FindRace(uint8(action)) || !SameFaction(player, uint8(action)))
                return true;
            state.race = uint8(action);
            ShowClasses(player, creature, state.race);
            return true;
        }

        if (sender == SENDER_CLASS)
        {
            if (!FindClass(uint8(action)) || !ClassLegalForRace(state.race, uint8(action)) ||
                !DeathKnightAllowed(player, uint8(action)))
                return true;
            state.cls = uint8(action);
            ShowSpecs(player, creature, state.cls);
            return true;
        }

        if (sender == SENDER_SPEC)
        {
            if (action > 2 || !FindClass(state.cls) || !FindRace(state.race))
                return true;
            state.tree = uint8(action);
            ShowGears(player, creature);
            return true;
        }

        if (sender == SENDER_GEAR)
        {
            if (!FindClass(state.cls) || !FindRace(state.race) || !FindGear(GearBand(action)))
                return true;
            state.gear = GearBand(action);
            ShowArrival(player, creature);
            return true;
        }

        if (sender == SENDER_ARRIVE)
        {
            if (!FindClass(state.cls) || !FindRace(state.race) || action > 1)
                return true;
            state.summon = action == 1;
            ShowStances(player, creature);
            return true;
        }

        if (sender == SENDER_STANCE)
        {
            if (!FindClass(state.cls) || !FindRace(state.race) || !FindStance(RecruitStance(action)))
                return true;
            state.stance = RecruitStance(action);
            ShowPeople(player, creature);
            return true;
        }

        if (sender == SENDER_PERSON)
        {
            if (action == PERSON_NEW)
            {
                ShowGenders(player, creature);
                return true;
            }

            ObjectGuid guid = ObjectGuid::Create<HighGuid::Player>(action);
            if (!EligibleBot(guid, state.race))
            {
                ChatHandler handler(player->GetSession());
                Tell(&handler, "That recruit is not available.");
                CloseGossipMenuFor(player);
                return true;
            }

            RecruitRequest request = RequestFromMenu(state);
            request.preferred = guid;
            ChatHandler handler(player->GetSession());
            Recruit(player, &handler, request);
            CloseGossipMenuFor(player);
            return true;
        }

        if (sender == SENDER_GENDER)
        {
            if (!FindClass(state.cls) || !FindRace(state.race) ||
                (action != GENDER_MALE && action != GENDER_FEMALE))
                return true;

            RecruitRequest request = RequestFromMenu(state);
            request.gender = uint8(action);
            request.createNew = true;
            ChatHandler handler(player->GetSession());
            Recruit(player, &handler, request);
            CloseGossipMenuFor(player);
            return true;
        }

        return true;
    }
};

void Addmod_bot_recruiterScripts()
{
    new bot_recruiter_commandscript();
    new npc_bot_recruiter();
    new bot_recruiter_worldscript();
    new bot_recruiter_playerscript();
}
