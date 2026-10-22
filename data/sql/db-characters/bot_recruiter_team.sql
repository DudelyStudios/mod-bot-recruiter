-- Standing Dungeon, Arena, and Battleground teams, plus the PvE and PvP
-- builds those characters keep. Safe to re-run.

CREATE TABLE IF NOT EXISTS `bot_recruiter_team` (
  `master_guid` int unsigned NOT NULL,
  `team` tinyint unsigned NOT NULL,
  `slot` tinyint unsigned NOT NULL,
  `bot_guid` int unsigned NOT NULL,
  PRIMARY KEY (`master_guid`, `team`, `slot`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `bot_recruiter_loadout` (
  `bot_guid` int unsigned NOT NULL,
  `kind` tinyint unsigned NOT NULL,
  `spec_no` int NOT NULL DEFAULT -1,
  `gear_saved` tinyint unsigned NOT NULL DEFAULT 0,
  PRIMARY KEY (`bot_guid`, `kind`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `bot_recruiter_loadout_item` (
  `bot_guid` int unsigned NOT NULL,
  `kind` tinyint unsigned NOT NULL,
  `equip_slot` tinyint unsigned NOT NULL,
  `item_guid` int unsigned NOT NULL,
  `item_entry` int unsigned NOT NULL,
  PRIMARY KEY (`bot_guid`, `kind`, `equip_slot`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
