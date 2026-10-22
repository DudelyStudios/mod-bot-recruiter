-- The last bots a player hired, so the sergeant can bring that party back
-- after a logout. One row per hired character, up to ten per player.
-- Safe to re-run.

CREATE TABLE IF NOT EXISTS `bot_recruiter_hire` (
  `master_guid` int unsigned NOT NULL,
  `slot` tinyint unsigned NOT NULL,
  `bot_guid` int unsigned NOT NULL,
  `spec_no` int NOT NULL DEFAULT 0,
  `gear_band` tinyint unsigned NOT NULL DEFAULT 0,
  `summon` tinyint unsigned NOT NULL DEFAULT 0,
  `stance` tinyint unsigned NOT NULL DEFAULT 2,
  `activity` tinyint unsigned NOT NULL DEFAULT 0,
  PRIMARY KEY (`master_guid`, `slot`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
