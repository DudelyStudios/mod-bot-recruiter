-- City bot recruiters. Entries 900101-900108 share the script npc_bot_recruiter.
-- Safe to re-run: these statements touch only those entries.
--
-- Gossip only. No vendor or trainer flag. Each capital's sergeant uses that
-- city's own guard model, so an Alliance bank gets an Alliance race and a
-- Horde bank gets a Horde race. Faction 35 stays friendly to both.
--
-- 900101 Stormwind City Guard (human, display 3167), also the Alliance sergeant in Dalaran
-- 900102 Ironforge Guard (dwarf, 3524)
-- 900103 Darnassus Sentinel (night elf, 2306)
-- 900104 Exodar Peacekeeper (draenei, 17372)
-- 900105 Orgrimmar Grunt (orc, 4259), also the Horde sergeant in Dalaran
-- 900106 Undercity Guardian (undead, 10699)
-- 900107 Bluffwatcher (tauren, 2141)
-- 900108 Silvermoon City Guardian (blood elf, 15511)

DELETE FROM `creature_template_model` WHERE `CreatureID` BETWEEN 900101 AND 900108;
DELETE FROM `creature_template` WHERE `entry` BETWEEN 900101 AND 900108;

INSERT INTO `creature_template`
    (`entry`, `name`, `subname`, `gossip_menu_id`,
     `minlevel`, `maxlevel`, `exp`, `faction`, `npcflag`,
     `speed_walk`, `speed_run`, `speed_swim`, `speed_flight`,
     `detection_range`, `rank`, `dmgschool`,
     `DamageModifier`, `BaseAttackTime`, `RangeAttackTime`,
     `BaseVariance`, `RangeVariance`,
     `unit_class`, `unit_flags`, `unit_flags2`, `dynamicflags`,
     `family`, `type`, `type_flags`,
     `lootid`, `pickpocketloot`, `skinloot`,
     `PetSpellDataId`, `VehicleId`, `mingold`, `maxgold`,
     `AIName`, `MovementType`, `HoverHeight`,
     `HealthModifier`, `ManaModifier`, `ArmorModifier`, `ExperienceModifier`,
     `RacialLeader`, `movementId`, `RegenHealth`,
     `flags_extra`, `ScriptName`, `VerifiedBuild`)
VALUES
    (900101, 'Sergeant', 'Bot Recruiter', 0, 80, 80, 0, 35, 1, 1.0, 1.14286, 1.0, 1.0, 18, 0, 0, 1.0, 1500, 2000, 1.0, 1.0, 1, 768, 2048, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, '', 0, 1.0, 1.0, 1.0, 1.0, 1.0, 0, 0, 1, 2, 'npc_bot_recruiter', 12340),
    (900102, 'Sergeant', 'Bot Recruiter', 0, 80, 80, 0, 35, 1, 1.0, 1.14286, 1.0, 1.0, 18, 0, 0, 1.0, 1500, 2000, 1.0, 1.0, 1, 768, 2048, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, '', 0, 1.0, 1.0, 1.0, 1.0, 1.0, 0, 0, 1, 2, 'npc_bot_recruiter', 12340),
    (900103, 'Sergeant', 'Bot Recruiter', 0, 80, 80, 0, 35, 1, 1.0, 1.14286, 1.0, 1.0, 18, 0, 0, 1.0, 1500, 2000, 1.0, 1.0, 1, 768, 2048, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, '', 0, 1.0, 1.0, 1.0, 1.0, 1.0, 0, 0, 1, 2, 'npc_bot_recruiter', 12340),
    (900104, 'Sergeant', 'Bot Recruiter', 0, 80, 80, 0, 35, 1, 1.0, 1.14286, 1.0, 1.0, 18, 0, 0, 1.0, 1500, 2000, 1.0, 1.0, 1, 768, 2048, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, '', 0, 1.0, 1.0, 1.0, 1.0, 1.0, 0, 0, 1, 2, 'npc_bot_recruiter', 12340),
    (900105, 'Sergeant', 'Bot Recruiter', 0, 80, 80, 0, 35, 1, 1.0, 1.14286, 1.0, 1.0, 18, 0, 0, 1.0, 1500, 2000, 1.0, 1.0, 1, 768, 2048, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, '', 0, 1.0, 1.0, 1.0, 1.0, 1.0, 0, 0, 1, 2, 'npc_bot_recruiter', 12340),
    (900106, 'Sergeant', 'Bot Recruiter', 0, 80, 80, 0, 35, 1, 1.0, 1.14286, 1.0, 1.0, 18, 0, 0, 1.0, 1500, 2000, 1.0, 1.0, 1, 768, 2048, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, '', 0, 1.0, 1.0, 1.0, 1.0, 1.0, 0, 0, 1, 2, 'npc_bot_recruiter', 12340),
    (900107, 'Sergeant', 'Bot Recruiter', 0, 80, 80, 0, 35, 1, 1.0, 1.14286, 1.0, 1.0, 18, 0, 0, 1.0, 1500, 2000, 1.0, 1.0, 1, 768, 2048, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, '', 0, 1.0, 1.0, 1.0, 1.0, 1.0, 0, 0, 1, 2, 'npc_bot_recruiter', 12340),
    (900108, 'Sergeant', 'Bot Recruiter', 0, 80, 80, 0, 35, 1, 1.0, 1.14286, 1.0, 1.0, 18, 0, 0, 1.0, 1500, 2000, 1.0, 1.0, 1, 768, 2048, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, '', 0, 1.0, 1.0, 1.0, 1.0, 1.0, 0, 0, 1, 2, 'npc_bot_recruiter', 12340);

INSERT INTO `creature_template_model`
    (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`, `VerifiedBuild`)
VALUES
    (900101, 0, 3167, 2.0, 1.0, 12340),
    (900102, 0, 3524, 2.0, 1.0, 12340),
    (900103, 0, 2306, 2.0, 1.0, 12340),
    (900104, 0, 17372, 2.0, 1.0, 12340),
    (900105, 0, 4259, 2.0, 1.0, 12340),
    (900106, 0, 10699, 2.0, 1.0, 12340),
    (900107, 0, 2141, 2.0, 1.0, 12340),
    (900108, 0, 15511, 2.0, 1.0, 12340);

-- Same presence as the Portal Master (entry 190000): double size, and spell
-- 30540 (Summon Visual), the purple circle under that NPC. Visibility Large
-- keeps the name readable from across the square. Every recruiter entry shares it.
DELETE FROM `creature_template_addon` WHERE `entry` BETWEEN 900101 AND 900108;
INSERT INTO `creature_template_addon`
    (`entry`, `path_id`, `mount`, `bytes1`, `bytes2`, `emote`, `visibilityDistanceType`, `auras`)
VALUES
    (900101, 0, 0, 0, 0, 0, 3, '30540'),
    (900102, 0, 0, 0, 0, 0, 3, '30540'),
    (900103, 0, 0, 0, 0, 0, 3, '30540'),
    (900104, 0, 0, 0, 0, 0, 3, '30540'),
    (900105, 0, 0, 0, 0, 0, 3, '30540'),
    (900106, 0, 0, 0, 0, 0, 3, '30540'),
    (900107, 0, 0, 0, 0, 0, 3, '30540'),
    (900108, 0, 0, 0, 0, 0, 3, '30540');
