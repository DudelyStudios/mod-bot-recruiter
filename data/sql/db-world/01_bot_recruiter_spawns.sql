-- Sergeants beside each Mentor spawn.
-- The Mentor stands at the eight capital banks and in Dalaran
-- (mod-unbound, 15_unbound_mentor_spawns.sql, guids 9000101-9000109).
-- Each capital recruiter is 3 yards to the Mentor's right, on the same floor,
-- facing the same way. Dalaran has one Alliance sergeant on the right and one
-- Horde sergeant the same distance to the left.
--
-- Spawn guids 9001011-9001020. The creature table's first column is `id`.
-- Safe to re-run: the DELETE removes only these guids when they are a recruiter.

DELETE FROM `creature` WHERE `guid` BETWEEN 9001011 AND 9001020 AND `id` BETWEEN 900101 AND 900108;

INSERT IGNORE INTO `creature`
    (`guid`, `id`, `map`, `zoneId`, `areaId`, `spawnMask`, `phaseMask`, `equipment_id`,
     `position_x`, `position_y`, `position_z`, `orientation`,
     `spawntimesecs`, `wander_distance`, `currentwaypoint`, `curhealth`, `curmana`,
     `MovementType`, `npcflag`, `unit_flags`, `dynamicflags`,
     `ScriptName`, `VerifiedBuild`, `CreateObject`, `Comment`)
VALUES
    (9001011, 900101, 0, 0, 0, 1, 1, 0, -8932.55, 617.81, 99.61, 0.454, 300, 0, 0, 1, 0, 0, 0, 0, 0, '', NULL, 0, 'Bot recruiter, Stormwind Trade District bank, human guard, beside Mentor guid 9000101'),
    (9001012, 900102, 0, 0, 0, 1, 1, 0, -4891.68, -995.89, 504.02, 2.234, 300, 0, 0, 1, 0, 0, 0, 0, 0, '', NULL, 0, 'Bot recruiter, Ironforge Commons bank, dwarf guard, beside Mentor guid 9000102'),
    (9001013, 900103, 1, 0, 0, 1, 1, 0, 9943.12, 2513.95, 1317.66, 4.328, 300, 0, 0, 1, 0, 0, 0, 0, 0, '', NULL, 0, 'Bot recruiter, Darnassus Tradesmen''s Terrace bank, night elf sentinel, beside Mentor guid 9000103'),
    (9001014, 900104, 530, 0, 0, 1, 1, 0, -3916.53, -11549.54, -150.04, 4.588, 300, 0, 0, 1, 0, 0, 0, 0, 0, '', NULL, 0, 'Bot recruiter, Exodar bank, draenei peacekeeper, beside Mentor guid 9000104'),
    (9001015, 900105, 1, 0, 0, 1, 1, 0, 1623.90, -4380.25, 12.06, 3.438, 300, 0, 0, 1, 0, 0, 0, 0, 0, '', NULL, 0, 'Bot recruiter, Orgrimmar Valley of Strength bank, orc grunt, beside Mentor guid 9000105'),
    (9001016, 900106, 0, 0, 0, 1, 1, 0, 1604.18, 244.03, -52.06, 0.087, 300, 0, 0, 1, 0, 0, 0, 0, 0, '', NULL, 0, 'Bot recruiter, Undercity Trade Quarter bank, undead guardian, beside Mentor guid 9000106'),
    (9001017, 900107, 1, 0, 0, 1, 1, 0, -1260.32, 26.00, 128.27, 1.571, 300, 0, 0, 1, 0, 0, 0, 0, 0, '', NULL, 0, 'Bot recruiter, Thunder Bluff bank, tauren bluffwatcher, beside Mentor guid 9000107'),
    (9001018, 900108, 530, 0, 0, 1, 1, 0, 9804.09, -7491.25, 13.64, 3.159, 300, 0, 0, 1, 0, 0, 0, 0, 0, '', NULL, 0, 'Bot recruiter, Silvermoon Bazaar bank, blood elf guardian, beside Mentor guid 9000108'),
    (9001019, 900101, 571, 0, 0, 1, 1, 0, 5629.18, 696.51, 652.68, 5.934, 300, 0, 0, 1, 0, 0, 0, 0, 0, '', NULL, 0, 'Bot recruiter, Dalaran Runeweaver Square bank, Alliance side, beside Mentor guid 9000109'),
    (9001020, 900105, 571, 0, 0, 1, 1, 0, 5627.12, 690.87, 652.68, 5.934, 300, 0, 0, 1, 0, 0, 0, 0, 0, '', NULL, 0, 'Bot recruiter, Dalaran Runeweaver Square bank, Horde side, beside Mentor guid 9000109');
