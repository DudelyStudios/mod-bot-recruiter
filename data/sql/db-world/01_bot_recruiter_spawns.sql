-- Sergeants beside each Portal Master (entry 190000).
--
-- The Mentor rows 9000101-9000109 belong to mod-unbound. That module deletes
-- them and writes the bank coordinates again whenever its spawn file is
-- applied, which a rebuild can do. This file does not move those rows.
-- bot_recruiter.cpp stations every Mentor and every sergeant on the portal
-- master that is actually spawned, each time the world finishes starting.
--
-- Booty Bay, the Gurubashi Arena, and Shattrath have a Portal Master and no
-- Mentor of Unbound's, so a Mentor body is added here for the startup
-- station to use. A neutral hub's sergeants are an Alliance guard on the
-- right and a Horde grunt on the left.
--
-- Spawn guids 9001101-9001116. The old bank guids 9001011-9001020 are removed.
-- Safe to re-run: deletes touch only these guids, and only when they are a
-- recruiter or a Mentor.

DELETE FROM `creature` WHERE `guid` BETWEEN 9001011 AND 9001020 AND `id` BETWEEN 900101 AND 900108;
DELETE FROM `creature` WHERE `guid` BETWEEN 9001101 AND 9001116 AND `id` BETWEEN 900101 AND 900108;
DELETE FROM `creature` WHERE `guid` IN (9000110, 9000111, 9000112) AND `id` = 900001;

INSERT IGNORE INTO `creature`
    (`guid`, `id`, `map`, `zoneId`, `areaId`, `spawnMask`, `phaseMask`, `equipment_id`,
     `position_x`, `position_y`, `position_z`, `orientation`,
     `spawntimesecs`, `wander_distance`, `currentwaypoint`, `curhealth`, `curmana`,
     `MovementType`, `npcflag`, `unit_flags`, `dynamicflags`,
     `ScriptName`, `VerifiedBuild`, `CreateObject`, `Comment`)
VALUES
    (9000110, 900001, 0, 0, 0, 1, 1, 0, -14276.78, 558.35, 8.90, 3.97606, 300, 0, 0, 1, 0, 0, 0, 0, 0, '', NULL, 0, 'The Mentor, Booty Bay, behind Portal Master guid 9001031'),
    (9000111, 900001, 0, 0, 0, 1, 1, 0, -13178.82, 346.68, 43.19, 4.32977, 300, 0, 0, 1, 0, 0, 0, 0, 0, '', NULL, 0, 'The Mentor, Gurubashi Arena, behind Portal Master guid 9001021'),
    (9000112, 900001, 530, 0, 0, 1, 1, 0, -1890.03, 5351.60, -12.43, 1.25883, 300, 0, 0, 1, 0, 0, 0, 0, 0, '', NULL, 0, 'The Mentor, Shattrath, behind Portal Master guid 9001032');

INSERT IGNORE INTO `creature`
    (`guid`, `id`, `map`, `zoneId`, `areaId`, `spawnMask`, `phaseMask`, `equipment_id`,
     `position_x`, `position_y`, `position_z`, `orientation`,
     `spawntimesecs`, `wander_distance`, `currentwaypoint`, `curhealth`, `curmana`,
     `MovementType`, `npcflag`, `unit_flags`, `dynamicflags`,
     `ScriptName`, `VerifiedBuild`, `CreateObject`, `Comment`)
VALUES
    (9001101, 900101, 0, 0, 0, 1, 1, 0, -8847.01, 628.90, 94.30, 0.44062, 300, 0, 0, 1, 0, 0, 0, 0, 0, '', NULL, 0, 'Bot recruiter, Stormwind, right of Portal Master guid 9001024'),
    (9001102, 900102, 0, 0, 0, 1, 1, 0, -4901.84, -967.98, 501.45, 2.25986, 300, 0, 0, 1, 0, 0, 0, 0, 0, '', NULL, 0, 'Bot recruiter, Ironforge, right of Portal Master guid 9001023'),
    (9001103, 900103, 1, 0, 0, 1, 1, 0, 9868.32, 2498.91, 1315.88, 5.94620, 300, 0, 0, 1, 0, 0, 0, 0, 0, '', NULL, 0, 'Bot recruiter, Darnassus, right of Portal Master guid 9001030'),
    (9001104, 900104, 530, 0, 0, 1, 1, 0, -3865.79, -11649.07, -137.63, 2.38273, 300, 0, 0, 1, 0, 0, 0, 0, 0, '', NULL, 0, 'Bot recruiter, Exodar, right of Portal Master guid 9001022'),
    (9001105, 900105, 1, 0, 0, 1, 1, 0, 1603.15, -4373.60, 10.09, 5.23641, 300, 0, 0, 1, 0, 0, 0, 0, 0, '', NULL, 0, 'Bot recruiter, Orgrimmar, right of Portal Master guid 9001025'),
    (9001106, 900106, 0, 0, 0, 1, 1, 0, 1637.16, 235.63, -43.10, 3.13147, 300, 0, 0, 1, 0, 0, 0, 0, 0, '', NULL, 0, 'Bot recruiter, Undercity, right of Portal Master guid 9001027'),
    (9001107, 900107, 1, 0, 0, 1, 1, 0, -1276.20, 77.24, 128.74, 5.95567, 300, 0, 0, 1, 0, 0, 0, 0, 0, '', NULL, 0, 'Bot recruiter, Thunder Bluff, right of Portal Master guid 9001026'),
    (9001108, 900108, 530, 0, 0, 1, 1, 0, 9741.67, -7458.69, 13.56, 3.14231, 300, 0, 0, 1, 0, 0, 0, 0, 0, '', NULL, 0, 'Bot recruiter, Silvermoon, right of Portal Master guid 9001028'),
    (9001109, 900101, 571, 0, 0, 1, 1, 0, 5810.09, 509.57, 657.58, 5.54461, 300, 0, 0, 1, 0, 0, 0, 0, 0, '', NULL, 0, 'Bot recruiter, Dalaran, Alliance, right of Portal Master guid 9001029'),
    (9001110, 900105, 571, 0, 0, 1, 1, 0, 5804.03, 502.92, 657.58, 5.54461, 300, 0, 0, 1, 0, 0, 0, 0, 0, '', NULL, 0, 'Bot recruiter, Dalaran, Horde, left of Portal Master guid 9001029'),
    (9001111, 900101, 0, 0, 0, 1, 1, 0, -14276.47, 551.99, 8.90, 3.97606, 300, 0, 0, 1, 0, 0, 0, 0, 0, '', NULL, 0, 'Bot recruiter, Booty Bay, Alliance, right of Portal Master guid 9001031'),
    (9001112, 900105, 0, 0, 0, 1, 1, 0, -14283.13, 558.04, 8.90, 3.97606, 300, 0, 0, 1, 0, 0, 0, 0, 0, '', NULL, 0, 'Bot recruiter, Booty Bay, Horde, left of Portal Master guid 9001031'),
    (9001113, 900101, 0, 0, 0, 1, 1, 0, -13176.33, 340.82, 43.19, 4.32977, 300, 0, 0, 1, 0, 0, 0, 0, 0, '', NULL, 0, 'Bot recruiter, Gurubashi Arena, Alliance, right of Portal Master guid 9001021'),
    (9001114, 900105, 0, 0, 0, 1, 1, 0, -13184.67, 344.18, 43.19, 4.32977, 300, 0, 0, 1, 0, 0, 0, 0, 0, '', NULL, 0, 'Bot recruiter, Gurubashi Arena, Horde, left of Portal Master guid 9001021'),
    (9001115, 900101, 530, 0, 0, 1, 1, 0, -1892.93, 5357.26, -12.43, 1.25883, 300, 0, 0, 1, 0, 0, 0, 0, 0, '', NULL, 0, 'Bot recruiter, Shattrath, Alliance, right of Portal Master guid 9001032'),
    (9001116, 900105, 530, 0, 0, 1, 1, 0, -1884.37, 5354.50, -12.43, 1.25883, 300, 0, 0, 1, 0, 0, 0, 0, 0, '', NULL, 0, 'Bot recruiter, Shattrath, Horde, left of Portal Master guid 9001032');
