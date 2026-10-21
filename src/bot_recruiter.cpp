/*
 * City bot recruiter for AzerothCore 3.3.5a.
 *
 * Gossip and .recruiter add call the same recruit path. That path uses the
 * playerbots addclass cache and PlayerbotHolder::AddPlayerBot. It does not
 * change mod-playerbots. The chosen talent row, gear band, and orders are
 * applied after that module has finished logging the bot in and rolling its
 * own talents. The last hire is stored in bot_recruiter_hire so the same
 * party can be brought back after a logout.
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

struct MenuState
{
    RecruitActivity activity = RecruitActivity::Dungeon;
    uint8 race = 0;
    uint8 cls = 0;
    uint8 tree = 0;
    GearBand gear = GearBand::Stored;
    bool summon = false;
    RecruitStance stance = RecruitStance::Damage;
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
};

std::unordered_map<ObjectGuid, MenuState> g_menu;
std::unordered_map<ObjectGuid, PendingRecruit> g_pending;

enum GossipSender
{
    SENDER_ACTIVITY = 1,
    SENDER_RACE = 2,
    SENDER_CLASS = 3,
    SENDER_SPEC = 4,
    SENDER_DISMISS = 5,
    SENDER_RECALL = 6,
    SENDER_GEAR = 7,
    SENDER_ARRIVE = 8,
    SENDER_STANCE = 9,
    SENDER_PERSON = 10,
    SENDER_GENDER = 11
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
    g_pending[botGuid] = pending;
}

bool HasSavedParty(ObjectGuid masterGuid)
{
    QueryResult result = CharacterDatabase.Query(
        "SELECT 1 FROM bot_recruiter_hire WHERE master_guid = {} LIMIT 1", masterGuid.GetCounter());
    return bool(result);
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

void ApplyOrders(Player* master, Player* bot, bool summon, RecruitStance stance)
{
    if (summon)
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
                PlayerbotFactory::InitTalentsBySpecNo(bot, pending.specNo, true);
                ApplyGear(master, bot, pending.gear);
                ApplyOrders(master, bot, pending.summon, pending.stance);
                bot->SaveToDB(false, false);
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

    bool alliance = master->GetTeamId(true) == TEAM_ALLIANCE;
    std::unordered_set<ObjectGuid> const& guidCache =
        sRandomPlayerbotMgr.addclassCache[RandomPlayerbotMgr::GetTeamClassIdx(alliance, cls)];

    auto const eligible = [&](ObjectGuid const& guid) -> bool { return EligibleBot(guid, race); };

    int specNo = request.specNo >= 0 ? request.specNo : SpecNumber(classChoice->trees[tree], activity);
    GearChoice const* gearChoice = FindGear(request.gear);
    StanceChoice const* stanceChoice = FindStance(request.stance);
    std::string notice = std::string("Recruiting a ") + raceChoice->label + " " + classChoice->label + " for " +
                         activityChoice->label + ": " + classChoice->trees[tree].label + ". Gear: " +
                         (gearChoice ? gearChoice->label : "Leave their gear") + ". Orders: " +
                         (request.summon ? "meet you, " : "stay put, ") +
                         (stanceChoice ? stanceChoice->label : "Damage") + ".";
    if (request.specNo >= 0)
        notice = "Bringing a saved recruit back.";

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
                                         if (PlayerbotMgr* botMgr = GET_PLAYERBOT_MGR(masterNow))
                                             botMgr->AddPlayerBot(guid, masterAccountId);
                                         ChatHandler chat(masterNow->GetSession());
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
    mgr->AddPlayerBot(picked, master->GetSession()->GetAccountId());
    Tell(handler, notice);
    return true;
}

void ShowActivities(Player* player, Creature* creature)
{
    ClearGossipMenuFor(player);
    g_menu[player->GetGUID()] = MenuState();
    if (HasSavedParty(player->GetGUID()))
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Bring them back", SENDER_RECALL, 0);
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
    return request;
}

bool RecallParty(Player* master, ChatHandler* handler)
{
    QueryResult result = CharacterDatabase.Query(
        "SELECT bot_guid, spec_no, gear_band, summon, stance, activity FROM bot_recruiter_hire "
        "WHERE master_guid = {} ORDER BY slot",
        master->GetGUID().GetCounter());
    if (!result)
    {
        Tell(handler, "There is no party to bring back.");
        return false;
    }

    uint32 brought = 0;
    do
    {
        Field* fields = result->Fetch();
        ObjectGuid guid = ObjectGuid::Create<HighGuid::Player>(fields[0].Get<uint32>());
        CharacterCacheEntry const* entry = sCharacterCache->GetCharacterCacheByGuid(guid);
        if (!entry || !EligibleBot(guid, entry->Race))
            continue;

        RecruitRequest request;
        request.cls = entry->Class;
        request.race = entry->Race;
        request.activity = RecruitActivity(fields[5].Get<uint8>());
        request.tree = 0;
        request.gear = GearBand(fields[2].Get<uint8>());
        request.summon = fields[3].Get<uint8>() != 0;
        request.stance = RecruitStance(fields[4].Get<uint8>());
        request.preferred = guid;
        request.specNo = fields[1].Get<int32>();
        if (Recruit(master, handler, request))
            ++brought;
    } while (result->NextRow());

    if (!brought)
        Tell(handler, "None of that party is free to join.");
    return brought != 0;
}

}  // namespace

class bot_recruiter_worldscript : public WorldScript
{
public:
    bot_recruiter_worldscript() : WorldScript("bot_recruiter_worldscript", {uint16(WORLDHOOK_ON_UPDATE)}) {}

    void OnUpdate(uint32 /*diff*/) override { ApplyPendingSpecs(); }
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
            char args[] = "remove *";
            ChatHandler handler(player->GetSession());
            PlayerbotMgr::HandlePlayerbotMgrCommand(&handler, args);
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

        if (sender == SENDER_RECALL)
        {
            ChatHandler handler(player->GetSession());
            RecallParty(player, &handler);
            CloseGossipMenuFor(player);
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
}
