# Spec validation matrix

Generated from `allspecs-cand2` (ST_STATIC, 30 s, one seed, level-60 live gear of the spec) and `profile_audit_raw.json`.
DPS of a healer or support spec on a static dummy is not a measure of its role. "Dead" = every decision rejected the descriptor as not known / passive. "Data findings" come from `profile_audit.py`.

| Class | Spec | Name | Role | Profile | Descriptors seen | Dead descriptors | Casts ok/failed | Dummy DPS | Data findings |
|---|---|---|---|---|---|---|---|---|---|
| 12 | 1 | Headhunting | Dps | yes | 13 | Crush (500915), War Cry (500995), Savage Rage (800954) | 17/0 | 686 | - |
| 12 | 2 | Brutality | Dps | yes | 9 | War Cry (500995), Berserker Rush (560518), Savage Rage (800954), Berserker Axe (804138) | 18/0 | 514 | - |
| 12 | 3 | Ancestry | Support | yes | 16 | Crush (500915), War Cry (500995), Berserker Rush (560518), Savage Rage (800954) | 20/8 | 324 | - |
| 13 | 4 | Shadowhunting | Dps | yes | 15 | Mojo Beam (500950), War Golem (800330), Potion Toss (801661) | 22/0 | 662 | - |
| 13 | 5 | Voodoo | Dps | yes | 16 | Dark Incantation (525377), Severing Threads (572836), Puppet Strings (705910), Spirit Eclipse (801607), Glaive of the Gods (806289) | 25/0 | 1084 | - |
| 13 | 6 | Brewing | Healer | yes | 5 | - | 16/0 | 178 | HEAL_TAG Reclaim Soul (Emergency) (801796); HOT_TAG Healing Ward (500957) |
| 14 | 7 | Infernal | Dps | yes | 12 | Tormentor (800162), Immolation Aura (800207), Felrend (800210), Chaos Nova (802025) | 14/0 | 1147 | - |
| 14 | 8 | Slayer | Dps | yes | 10 | Felrend (800210), Chaos Nova (802025) | 24/0 | 972 | - |
| 14 | 9 | Tyrant | Tank | yes | 9 | Taunt (355), Immolation Aura (800207), Infernal Strike (801016), Chaos Nova (802025) | 23/0 | 302 | - |
| 15 | 10 | Boltslinger | Dps | yes | 15 | Coiling Shot (Builder) (500082), Repeater (Crossbow Barrage) (805903), Saber Slash (Melee Fallback) (982349) | 16/0 | 417 | MISSING Repeater (Crossbow Barrage) (805903); MISSING Repeater (Barrage) (805903) |
| 15 | 11 | Houndmaster | Dps | yes | 16 | Coiling Shot (Builder) (500082), Witchbane (Devastating Volley) (800165), Repeater (Crossbow Barrage) (805903), Saber Slash (Melee Fallback) (982349) | 19/0 | 650 | - |
| 15 | 12 | Inquisition | Dps | yes | 16 | Coiling Shot (Builder) (500082), Witchbane (Devastating Volley) (800165), Repeater (Crossbow Barrage) (805903), Saber Slash (Melee Fallback) (982349) | 23/0 | 295 | MISSING Repeater (805903) |
| 15 | 97 | Black Knight | Tank | yes | 17 | Taunt (355), Coiling Shot (Builder) (500082), Witchbane (Devastating Volley) (800165), Blade Stance (Parry/Threat) (802002), Saber Slash (Melee Fallback) (982349) | 21/0 | 186 | TAUNT_TAG Gaze of the Black Knight (Shield/Taunt) (802138) |
| 16 | 13 | Wind | Support | yes | 11 | Static Field (AoE Field) (500924) | 18/0 | 514 | SHIELD_TAG Gale Guard (Shield) (500923) |
| 16 | 14 | Maelstrom | Dps | yes | 17 | Chain Lightning (421), Static Field (AoE Field) (500924) | 20/0 | 1842 | SHIELD_TAG Gale Guard (500923) |
| 16 | 15 | Lightning | Dps | yes | 15 | Lightning Bolt (403), Chain Lightning (421), Static Field (AoE Field) (500924) | 21/0 | 2001 | SHIELD_TAG Gale Guard (Ward/Shield) (500923) |
| 17 | 16 | Hellfire | Dps | yes | 12 | Chaos Bolt (802057) | 21/0 | 686 | - |
| 17 | 17 | Defiance | Tank | yes | 13 | Taunt (355), Chaos Bolt (802057) | 20/0 | 406 | - |
| 17 | 18 | War | Dps | yes | 11 | Chaos Bolt (802057) | 17/0 | 517 | - |
| 18 | 19 | Gladiator | Dps | yes | 10 | - | 16/0 | 339 | SHIELD_TAG Raise Shield (500168) |
| 18 | 20 | Inspiration | Support | yes | 8 | - | 13/0 | 230 | SHIELD_TAG Raise Shield (500168) |
| 18 | 21 | Vanguard | Tank | yes | 8 | Taunt (355) | 18/0 | 329 | SHIELD_TAG Raise Shield (500168) |
| 19 | 22 | Oathkeeper | Tank | yes | 21 | Crusader's Brand (300513), Martyr (300530), Taunt (355), Divine Charge (527023), Tyr's Guard (560649), Final Prayer (Last Stand) (705268), Tenacious Defender (707385), Courage (Templar Taunt) (707754), Righteous Tempest (AoE Threat Spender) (805409) | 24/0 | 505 | TAUNT_TAG Courage (Templar Taunt) (707754); HEAL_TAG Final Prayer (Last Stand) (705268); SHIELD_TAG Tyr's Guard (560649) |
| 19 | 23 | Zealot | Dps | yes | 19 | Crusader's Brand (300513), Divine Charge (527023), Final Prayer (Last Stand) (705268), Mark of Aggramar (804928) | 23/0 | 525 | HEAL_TAG Final Prayer (705268) |
| 19 | 24 | Crusader | Dps | yes | 17 | Crusader's Brand (300513), Divine Charge (527023), Final Prayer (Last Stand) (705268), Scourgebane (92111) | 22/0 | 297 | HEAL_TAG Final Prayer (705268) |
| 20 | 25 | Fleshweaver | Support | yes | 13 | Waves of Blood (681427), Transfusion (705734), Universal Donor (806428) | 17/0 | 264 | - |
| 20 | 26 | Sanguine | Dps | yes | 21 | Veinburst (504260), Vampyr's Kiss (504275), Crimson Tide (504282), Heartbreak (520314), Hemoburst (572855), Blood Moon (804199) | 22/0 | 2864 | SHIELD_TAG Fleshcraft (801952) |
| 20 | 27 | Accursed | Dps | yes | 19 | Night Hunter's Howl (500124), Rotclaw (804197), Vampiric Fang (804726), Blood Shards (804849), Bloodleaper (806423) | 23/0 | 1081 | PASSIVE Aortic Assault (806212) |
| 20 | 99 | Eternal | Tank | yes | 19 | Taunt (355), Veinburst (504260), Vampyr's Kiss (504275), Crimson Tide (504282), Heartbreak (520314), Bloody Sacrifice (705742), Vampiric Fang (804726), Aortic Aegis (806274) | 22/0 | 618 | SHIELD_TAG Fleshcraft (801952); HEAL_TAG Transfusion (705734); SHIELD_TAG Fleshcraft (801952); HOT_TAG Blood Redistribution (706256) |
| 21 | 28 | Archery | Dps | yes | 19 | Multi-Shot (AoE) (2643), Rapid Fire (Burst) (3045), Concussive Shot (Snare) (5116), Command Aura (524600), Falconstrike (Burst) (806345) | 21/0 | 280 | - |
| 21 | 29 | Farstrider | Support | yes | 21 | Command Aura (524600), Battle Screech (AoE Debuff) (705070), Horn of War (Party Buff) (800086), Falconstrike (Burst) (806345), Horn of Alacrity (Party Buff) (806360) | 25/0 | 534 | - |
| 21 | 30 | Brigand | Dps | yes | 18 | Command Aura (524600), Battle Screech (AoE Debuff) (705070) | 25/1 | 900 | - |
| 22 | 31 | Time | Healer | yes | 1 | - | 17/0 | 309 | HEAL_TAG Do Over (800669); HEAL_TAG Rewind (801294); SHIELD_TAG Infinite Shield (520457); NAME Sandblast (804464); SHIELD_TAG Aeon of Protection (806291); PASSIVE Ripple (806296) |
| 22 | 32 | Infinite | Dps | yes | 12 | Rewind (801294), Sandblast (804464), Infinite Power (92118) | 19/0 | 994 | HEAL_TAG Rewind (801294); SHIELD_TAG Infinite Shield (520457); NAME Sandblast (804464) |
| 22 | 33 | Artificer | Dps | yes | 16 | Chromatic Shard (801292), Sandblast (804464), Melt Reality (806335) | 24/0 | 540 | SHIELD_TAG Infinite Shield (520457); NAME Sandblast (804464) |
| 23 | 34 | Death | Dps | yes | 17 | Ner'zhul's Blessing (704729), Tears of Lordaeron (705752), Putrefy (804558), Death's Due (807796), Crypt Plague (92121) | 21/0 | 638 | HEAL_TAG Sacrifice Undead (805027); NAME Lich Armor (800199); MISSING Putrefy (804558) |
| 23 | 35 | Animation | Dps | yes | 20 | Raise: Skeletal Archer (500332), Flesh to Worms (500338), Unholy Command (802123), Putrefy (804558) | 21/0 | 1069 | HEAL_TAG Sacrifice Undead (805027); NAME Lich Armor (800199); MISSING Raise: Skeletal Archer (500332); NAME Foul Bidding: Slaughter (500982); MISSING Unholy Command (802123); MISSING Putrefy (804558) |
| 23 | 36 | Rime | Dps | yes | 16 | Winds of Northrend (301333), Putrefy (804558), Animate: Frost Wyrm (805428), Tundra Warriors (92122) | 19/2 | 2403 | HEAL_TAG Sacrifice Undead (805027); NAME Lich Armor (800199); MISSING Putrefy (804558) |
| 24 | 37 | Flameweaving | Healer | yes | 2 | - | 14/0 | 17 | NAME Scorching Ray (800790) |
| 24 | 38 | Incineration | Dps | yes | 13 | Inferno Barrier (504380), Overheat (800408), Flamecasting (804300), Molten Storm (805483), Infernus (92124) | 18/0 | 1155 | NAME Slagstone (803950); NAME Scorching Ray (800790) |
| 24 | 39 | Draconic | Dps | yes | 15 | ? (503636), Dragonscales (524818), Ignite (Fire DoT) (800791) | 20/0 | 1340 | PASSIVE Ignis Ultimatus (680369); NAME Slagstone (803950); NAME Scorching Ray (800790) |
| 25 | 40 | Heretic | Healer | yes | 3 | - | 18/0 | 351 | HOT_TAG Dark Infusion (570249); HEAL_TAG Eldritch Shock (808038) |
| 25 | 41 | Corruption | Dps | yes | 13 | Vision of Doom (520388), Eldritch Screams (525049), Herald of C'thun (805119), Herald of Yogg-Saron (805120), Corrupting Whispers (92130) | 15/0 | 1004 | NAME Herald of C'thun (805119) |
| 25 | 42 | Godblade | Dps | yes | 11 | Devourer (300265), Eldritch Strike (801964), Shroud of Pride (805126), Obliteration (92129) | 20/0 | 862 | NAME Eldritch Strike (801964) |
| 25 | 96 | Dreadnought | Tank | yes | 12 | Taunt (355), Dreadnought (680750), Eldritch Strike (801964), Void-Enchanted Armor (804633) | 17/0 | 311 | TAUNT_TAG Grasp of Zek'voz (573028); SHIELD_TAG Dreadnought (680750); SHIELD_TAG Abyssal Ward (804670); HEAL_TAG Satiate (804275); NAME Eldritch Strike (801964) |
| 26 | 43 | Moon Priest | Healer | yes | 3 | - | 19/0 | 96 | HEAL_TAG Vial of Moonwell Water (804652); SHIELD_TAG Astral Aegis (806155); SHIELD_TAG Constellation Barrier (300259) |
| 26 | 44 | Sentinel | Dps | yes | 13 | Constellation Barrier (300259) | 13/0 | 181 | - |
| 26 | 45 | Warden | Dps | yes | 13 | Constellation Barrier (300259), Starsweep (805550) | 22/0 | 667 | SHIELD_TAG Constellation Barrier (300259) |
| 26 | 100 | Moon Guard | Tank | yes | 9 | Constellation Barrier (300259), Taunt (355) | 20/0 | 177 | SHIELD_TAG Constellation Barrier (300259); SHIELD_TAG Astral Aegis (806155) |
| 27 | 46 | Piety | Dps | yes | 9 | Horusath Blast (500154), Consecrated Weapons (704395) | 18/0 | 1226 | NAME Consecrated Weapons (704395); NAME Sunset (804584) |
| 27 | 47 | Valkyrie | Dps | yes | 17 | Horusath Blast (500154), Consecrated Weapons (704395), Dawnbreak (800654), Hammer of Kings (Stun) (804751) | 21/0 | 291 | NAME Consecrated Weapons (704395); NAME Sunset (804584); PASSIVE Sunslam (805629) |
| 27 | 48 | Seraphim | Tank | yes | 13 | Consecrated Weapons (704395) | 17/0 | 195 | TAUNT_TAG Glare (Taunt) (805583); NAME Consecrated Weapons (704395); HOT_TAG Revivify (Self HoT) (801790) |
| 27 | 98 | Blessings | Healer | yes | 1 | - | 7/0 | 104 | HOT_TAG Revivify (Tank HoT) (801790); HOT_TAG Revivify (Party HoT) (801790); NAME Consecrated Weapons (704395) |
| 28 | 49 | Demolition | Dps | yes | 16 | Blackpowder Shot (500234), Deathball (500236), Reload (500237), Rocket Bombardment (706695), Rifle (Gunsling) (801648), Air Strike (801744), Napalm (92138) | 17/0 | 1442 | SHIELD_TAG Kinetic Shield (806224); MISSING Rifle (Gunsling) (801648); NAME Reload (500237) |
| 28 | 50 | Mechanics | Dps | yes | 18 | Deathball (500236), Reload (500237), Scrapshielding (806627) | 17/0 | 267 | SHIELD_TAG Kinetic Shield (806224); SHIELD_TAG Scrapshielding (806627); PASSIVE Gatling Gun (500213); NAME Reload (500237) |
| 28 | 51 | Invention | Healer | yes | 2 | - | 8/0 | 136 | SHIELD_TAG Nanobot Reconstruction (801809); NAME Reload (500237) |
| 29 | 52 | Fortitude | Tank | yes | 16 | Taunt (355) | 21/0 | 276 | - |
| 29 | 53 | Stalking | Dps | yes | 18 | Avatar of Shadra (503856), Toxic Stride (504347), Ambush Predator (800878) | 27/0 | 1801 | NAME Envenom Weapons (803177) |
| 29 | 54 | Rot | Dps | yes | 10 | Master of Venoms (504326), Reinforced Shell (Beetle Defense) (705966), Rot Lich (92142) | 19/0 | 1072 | SHIELD_TAG Reinforced Shell (705966) |
| 29 | 101 | Vizier | Healer | yes | 1 | - | 16/0 | 0 | HEAL_TAG Lifeblood (804963); SHIELD_TAG Reinforced Shell (Beetle Defense) (705966); NAME Antidote (803529) |
| 30 | 55 | Soul | Dps | yes | 15 | Ghastly Screech (AoE Shadow) (806146) | 25/0 | 480 | - |
| 30 | 56 | Harvest | Dps | yes | 15 | Bolstered Form (Plate Armor Stance) (680337), Shudder Scythe (Cleave) (801321), Dirge (Offhand Finisher) (801328) | 21/0 | 739 | MISSING Shudder Scythe (Cleave) (801321) |
| 30 | 57 | Domination | Tank | yes | 12 | Soul Protector (Barrier) (300553), Dirge (Offhand Finisher) (801328), Tormentor (Taunt) (92147) | 11/0 | 191 | SHIELD_TAG Soul Protector (Barrier) (300553); TAUNT_TAG Tormentor (Taunt) (92147); HEAL_TAG Soul Strike (Leech Attack) (500517) |
| 31 | 58 | Grovekeeper | Support | yes | 9 | Earthmother's Roar (Group Heal) (301306), Seismic Tremor (680442) | 11/0 | 329 | SHIELD_TAG Rock Barrier (Tank Shield) (503630); HEAL_TAG Earthmother's Roar (Group Heal) (301306) |
| 31 | 59 | Wildwalker | Dps | yes | 17 | Seismic Tremor (680442), Terrasurge (681119), Magma Fissure (802793), Wildheart (803980), Seismic Wave (805462) | 20/0 | 722 | SHIELD_TAG Rock Barrier (Shield) (503630) |
| 31 | 60 | Mountain King | Tank | yes | 13 | Seismic Smash (300693), Totemic Smash (800178) | 27/1 | 474 | TAUNT_TAG Protective Roar (Taunt) (802782); SHIELD_TAG Rock Barrier (Tank Shield) (503630) |
| 31 | 95 | Geomancy | Dps | yes | 14 | Seismic Wave (805462) | 26/0 | 1450 | SHIELD_TAG Rock Barrier (503630) |
| 32 | 61 | Engravement | Dps | yes | 14 | Frigid Blast (500118), Hoarfrost (801104), Fracture (803018) | 24/0 | 628 | SHIELD_TAG Runeshroud (Shield) (500288) |
| 32 | 62 | Glyphic | Dps | yes | 12 | Fist of the Ancients (712326), Hoarfrost (801104) | 22/2 | 1529 | SHIELD_TAG Runeshroud (Shield) (500288) |
| 32 | 63 | Riftblade | Dps | yes | 10 | Frigid Blast (500118), Runic Brand (712299), Fist of the Ancients (712326), Fracture (803018) | 19/8 | 489 | SHIELD_TAG Runeshroud (500288) |

Specs: 70, run: 70, dead descriptors found: 202.
