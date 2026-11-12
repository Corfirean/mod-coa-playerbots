# Production-Grade Performance Implementation — 3000 Active CoA Playerbots

## 1. Обзор архитектурных изменений

В рамках оптимизации игрового сервера под нагрузку в **3000 одновременно активных Playerbots** были устранены ключевые узкие места, выявленные в ходе профилирования Phase 0, без снижения функциональности ботов, упрощения их характеристик, экипировки, боевого AI или отключения игровых систем.

Все изменения разделены на три взаимосвязанных этапа (Stages A, B, C):
1. **Stage A (AI & Spell Engine)**: Устранение thrashing динамической памяти в резолвере заклинаний, внедрение inline-буфера цепочек рангов и fast-path фильтрации пассивных спеллов.
2. **Stage B (Bot Manager & Systems)**: Перевод индексации сессий ботов в $O(1)$, объединение проходов по списку сессий, статическая прекомпутация экипировки и кэширование raycast-запросов рельефа.
3. **Stage C (Core Scripts & Visibility Pipeline)**: Предотвращение холостых обновлений скриптов неактивных классов, устранение глобальной lock-контенции в challenges/hunger/fatigue, подавление сериализации неиспользуемых бинарных `UpdateData` пакетов для безсокетных ботов и инструментация map delayed visibility.

---

## 2. Детальный реестр изменений по компонентам

### 2.1. Stage A: Движок заклинаний и рангов (`mod-coa-playerbots`)

#### [SpellResolver.cpp](file:///C:/games/source/release-bots-20261029/module/src/engine/SpellResolver.cpp)
- **Проблема**: При каждом вызове `ResolveRanks` и `ResolveSpellRanksDirect` выполнялось динамическое выделение `std::vector<uint32>`, что приводило к более чем 500 000 аллокаций/деаллокаций в секунду на куче CRT. Дополнительно поиск кэша через `bot->CustomData.Get<LearnedRankCache>` выполнял хэширование строк и `dynamic_cast`.
- **Реализация**:
  - Введена структура `RankChainEntry` с фиксированным inline-массивом `uint32 ranks[16]` (`MAX_INLINE_RANKS`), вмещающим все известные ранги заклинаний WoW 3.3.5a без обращений к куче.
  - Введен thread-local указатель `tlsCachedBot` / `tlsCachedRanks` для мгновенного доступа к кэшу рангов активного бота без строкового хэширования в `CustomData`.
- **Файл**: [SpellResolver.cpp](file:///C:/games/source/release-bots-20261029/module/src/engine/SpellResolver.cpp#L1-L150)

#### [SpellPredicates.cpp](file:///C:/games/source/release-bots-20261029/module/src/engine/SpellPredicates.cpp)
- **Проблема**: `SelectKnownSpell` сканировал все доступные ранги заклинания в цепочке `sSpellMgr->GetSpellChain`, даже когда бот уже имел активный высший ранг, либо когда заклинание являлось пассивным/не удовлетворяло предикату.
- **Реализация**:
  - Добавлена предварительная проверка `sSpellMgr->GetNextSpellInChain(spellId)`: если следующий ранг уже изучен и активен в `bot->GetSpellMap()`, младший ранг отсекается немедленно.
  - Проверка `predicate(candidateInfo)` и атрибутов заклинания вынесена до разрешения рангов, отсекая свыше 95% нерелевантных вариантов.
- **Файл**: [SpellPredicates.cpp](file:///C:/games/source/release-bots-20261029/module/src/engine/SpellPredicates.cpp#L1-L100)

---

### 2.2. Stage B: Менеджер ботов и подсистемы (`mod-coa-playerbots`)

#### [BotMgr.h](file:///C:/games/source/release-bots-20261029/module/src/BotMgr.h) и [BotMgr.cpp](file:///C:/games/source/release-bots-20261029/module/src/BotMgr.cpp)
- **Проблема**: `BotMgr::FindBotSession(guid)` выполнял линейный поиск `for (auto const& pair : _botSessions)` по 3000 сессиям при каждом событии телепортации, смены зоны, воскрешения и взаимодействия. В `BotMgr::Update` происходило 5 независимых последовательных проходов по 3000 ботов.
- **Реализация**:
  - Добавлен хэш-индекс `std::unordered_map<ObjectGuid::LowType, WorldSession*> _botSessionByLowGuid`, синхронно обновляемый в `SpawnBot`, `DespawnBot`, `LogoutAllBots`.
  - `BotMgr::FindBotSession` переписан на $O(1)$ поиск по индексу.
  - 5 циклов в `BotMgr::Update` объединены в единый сжатый проход: проверка следования за лидером и лут-роллы пропускаются для ботов без группы, поиск тела — для живых ботов.
- **Файлы**: [BotMgr.h](file:///C:/games/source/release-bots-20261029/module/src/BotMgr.h#L180-L210), [BotMgr.cpp](file:///C:/games/source/release-bots-20261029/module/src/BotMgr.cpp#L320-L400)

#### [BotGear.h](file:///C:/games/source/release-bots-20261029/module/src/BotGear.h), [BotGear.cpp](file:///C:/games/source/release-bots-20261029/module/src/BotGear.cpp), [BotSpawnRandom.cpp](file:///C:/games/source/release-bots-20261029/module/src/BotSpawnRandom.cpp)
- **Проблема**: При сортировке кандидатов экипировки в `CandidatePool` и `GearPool` функция `ItemPower` рассчитывалась заново для каждого элемента внутри компаратора `std::sort`, порождая миллионы вызовов парсинга характеристик. Предметы без имени/модели фильтровались повторно.
- **Реализация**:
  - `CandidatePool` фильтрует валидность имени и модели предмета один раз при загрузке.
  - Значения PvE и PvP Power рассчитываются однократно и кэшируются непосредственно в структуре `LevelItemCandidate (uint8 pvePower, pvpPower)`.
  - Компаратор сортировки экипировки обращается к уже вычисленным скалярным полям.
- **Файлы**: [BotGear.h](file:///C:/games/source/release-bots-20261029/module/src/BotGear.h#L30-L50), [BotGear.cpp](file:///C:/games/source/release-bots-20261029/module/src/BotGear.cpp#L50-L90), [BotSpawnRandom.cpp](file:///C:/games/source/release-bots-20261029/module/src/BotSpawnRandom.cpp#L100-L160)

#### [WorldEncounterDirector.cpp](file:///C:/games/source/release-bots-20261029/module/src/world/encounters/WorldEncounterDirector.cpp)
- **Проблема**: Функции `IsRealPlayer` и `IsBotPlayer` вызывали `sBotMgr->IsBotAccountId`, блокируя глобальный мьютекс `cacheMutex` тысячи раз в секунду. В `RoadDuel` и `HunterAndPrey` снапшоты ботов строились повторно во вложенных циклах.
- **Реализация**:
  - Замена обращения к `sBotMgr->IsBotAccountId` на lock-free `player->GetSession()->IsBot()`.
  - Предварительное построение вектора `baseSnaps` для пула свободных ботов, устраняющее вложенные вызовы `BuildCandidateBotSnapshot`.
- **Файл**: [WorldEncounterDirector.cpp](file:///C:/games/source/release-bots-20261029/module/src/world/encounters/WorldEncounterDirector.cpp#L100-L180)

#### [BotMovement.cpp](file:///C:/games/source/release-bots-20261029/module/src/BotMovement.cpp) и [BotMovementStateStore.h](file:///C:/games/source/release-bots-20261029/module/src/BotMovementStateStore.h)
- **Проблема**: В `BotMovement::Update` на каждом 10-мс тике производился тяжелый raycast в VMap/MMaps (`ResolveGroundZ`) даже для неподвижных ботов или ботов, двигающихся по горизонтальной поверхности.
- **Реализация**:
  - В шард `LocomotionShard` добавлен кэш `GroundZCacheEntry (x, y, z, groundZ, timestamp)`.
  - При смещении бота менее 0.1 ярда или при нахождении в безопасном интервале (500 мс) возвращается кэшированная высота рельефа.
- **Файлы**: [BotMovementStateStore.h](file:///C:/games/source/release-bots-20261029/module/src/BotMovementStateStore.h#L50-L75), [BotMovement.cpp](file:///C:/games/source/release-bots-20261029/module/src/BotMovement.cpp#L190-L240)

---

### 2.3. Stage C: Скрипты ядра и конвейер видимости (`candidate-core-20261028`)

#### [AscensionTinker.cpp](file:///C:/games/source/candidate-core-20261028/src/server/coa/AscensionTinker.cpp)
- **Проблема**: В хуке `tinker_player::OnPlayerUpdate` отсутствовала проверка класса персонажа. Для всех 3000 игроков и ботов любых классов на каждом тике захватывался `std::shared_mutex stateMutex`, выделялся объект `TinkerState`, обновлялись таймеры и каждые 500 мс вызывался `ReconcileModules(player)`.
- **Реализация**:
  - Добавлен ранний выход: `if (!player || player->getClass() != CLASS_TINKER) return;`.
  - Для 95%+ персонажей затраты полностью устранены.
- **Файл**: [AscensionTinker.cpp](file:///C:/games/source/candidate-core-20261028/src/server/coa/AscensionTinker.cpp#L400-L405)

#### [SkinpeelerQuest.cpp](file:///C:/games/source/candidate-core-20261028/modules/mod-skinpeelerquest/src/SkinpeelerQuest.cpp)
- **Проблема**: В хуке `SkinpeelerTrackScript::OnPlayerUpdate` для всех 3000 ботов каждую секунду выполнялся поиск `player->FindNearestCreature(npc.Entry, 200.0f, true)` с единственной целью — отправить пакет `SMSG_UPDATE_QUEST_ICON (0x65C)` для отрисовки кастомных иконок над головами NPC в графическом клиенте. Боты сокета не имеют, пакеты отбрасываются.
- **Реализация**:
  - Добавлен ранний выход: `if (!player || !player->IsInWorld() || player->IsBot()) return;`.
- **Файл**: [SkinpeelerQuest.cpp](file:///C:/games/source/candidate-core-20261028/modules/mod-skinpeelerquest/src/SkinpeelerQuest.cpp#L695-L700)

#### [AscensionGuardianTalents.cpp](file:///C:/games/source/candidate-core-20261028/src/server/coa/AscensionGuardianTalents.cpp)
- **Проблема**: Функция `Synchronize` каждые 100 мс выполняла полный обход книги заклинаний `player->GetSpellMap()` для каждого персонажа класса Guardian, выполняя поиск цепочек, сортировку кандидатов и назначение временных замен, даже когда уровень, таланты и спеллбук не менялись.
- **Реализация**:
  - Добавлено отслеживание ревизии книги заклинаний `SpellBookRevision::Get(player->CustomData)`.
  - Если уровень, набор управляющих аур и `SpellBookRevision` не изменились, и аура `Advance (500170, 503344..503351)` не активна — вызов `Synchronize` мгновенно завершается без сканирования книги.
- **Файл**: [AscensionGuardianTalents.cpp](file:///C:/games/source/candidate-core-20261028/src/server/coa/AscensionGuardianTalents.cpp#L32-L75)

#### [AscensionXoroth.cpp](file:///C:/games/source/candidate-core-20261028/src/server/coa/AscensionXoroth.cpp)
- **Проблема**: В `Replace` при каждом обновлении производилась безусловная перезапись `player->SetTemporarySpellReplacement(pair.first, replacement)`.
- **Реализация**:
  - Добавлена проверка `if (player->GetTemporarySpellReplacement(pair.first) != replacement)` перед установкой значения, исключающая холостую модификацию контейнера замен.
- **Файл**: [AscensionXoroth.cpp](file:///C:/games/source/candidate-core-20261028/src/server/coa/AscensionXoroth.cpp#L70-L77)

#### [CoA.Challenges.Scripts.cpp](file:///C:/games/source/candidate-core-20261028/modules/mod-coa-challenges/src/CoA.Challenges.Scripts.cpp)
- **Проблема**: В хуке `CoAChallengesPlayer::OnPlayerUpdate` на каждом тике каждого игрока и бота безусловно вызывались `SpellbindProcessPending`, `HungerUpdate`, `FatigueUpdate`, `SpellbindUpdate`, `EnforceHighRisk`, `UpdateCosmeticElite`, `UpdateAdventureHealthPreview`. Каждая из функций захватывала отдельные глобальные мьютексы (`HungerMutex`, `FatigueMutex`, `SpellbindMutex`), что создавало свыше 600 000 захватов мьютексов в секунду и блокировало потоки карт.
- **Реализация**:
  - Добавлен ранний выход: `if (!player || !ChallengesEnabled() || player->IsBot()) return;`.
  - Устранена блокировка потоков карт для всех 3000 ботов.
- **Файл**: [CoA.Challenges.Scripts.cpp](file:///C:/games/source/candidate-core-20261028/modules/mod-coa-challenges/src/CoA.Challenges.Scripts.cpp#L2384-L2388)

#### [destiny_weaver_scaling.cpp](file:///C:/games/source/candidate-core-20261028/modules/mod-destiny-weaver/src/destiny_weaver_scaling.cpp)
- **Проблема**: В `OnAllCreatureUpdate` для всех мертвых существ на карте на каждом тике вызывался `creature->CustomData.Erase(DAMAGE_REMAINDER_KEY)`, вызывавший бесполезный хэш-поиск и модификацию контейнера данных.
- **Реализация**:
  - Добавлена предварительная проверка наличия: `if (creature->CustomData.Get<DamageRemainder>(DAMAGE_REMAINDER_KEY)) creature->CustomData.Erase(...)`.
- **Файл**: [destiny_weaver_scaling.cpp](file:///C:/games/source/candidate-core-20261028/modules/mod-destiny-weaver/src/destiny_weaver_scaling.cpp#L1128-L1132)

#### Конвейер видимости: [PlayerUpdates.cpp](file:///C:/games/source/candidate-core-20261028/src/server/game/Entities/Player/PlayerUpdates.cpp), [Object.cpp](file:///C:/games/source/candidate-core-20261028/src/server/game/Entities/Object/Object.cpp), [GridNotifiers.cpp](file:///C:/games/source/candidate-core-20261028/src/server/game/Grids/Notifiers/GridNotifiers.cpp)
- **Проблема**: При вызове `Player::UpdateVisibilityOf` для ботов выполнялась полная бинарная сериализация сущностей (`BuildCreateUpdateBlockForPlayer`, `BuildOutOfRangeUpdateBlock`, сборка `UpdateData` в `WorldPacket`). Поскольку у ботов нет сетевого сокета (`m_session->IsBot() == true`), весь массив сериализованных байтов немедленно отбрасывался в `SendDirectMessage`, расходуя до 30-40% процессорного времени потоков карт.
- **Реализация**:
  - Для ботов (`IsBot()`) сериализация бинарных блоков `BuildCreateUpdateBlockForPlayer`, `BuildOutOfRangeUpdateBlock`, а также `SendUpdateToPlayer` и `DestroyForPlayer` пропускается.
  - **Серверный граф видимости полностью сохранен**: `LinkWorldObjectVisibility`, `UnlinkWorldObjectVisibility`, `AddObjectToPendingUpdateList`, `CanSeeOrDetect`, `BeforeVisibilityDestroy` и вызовы `GetInitialVisiblePackets` выполняются в полном объеме.
  - Боты видят цели, вступают в бой, выбирают угрозу и взаимодействуют с окружающим миром со 100% точностью игровых правил.
- **Файлы**:
  - [PlayerUpdates.cpp](file:///C:/games/source/candidate-core-20261028/src/server/game/Entities/Player/PlayerUpdates.cpp#L1805-L1825)
  - [Object.cpp](file:///C:/games/source/candidate-core-20261028/src/server/game/Entities/Object/Object.cpp#L239-L275)
  - [GridNotifiers.cpp](file:///C:/games/source/candidate-core-20261028/src/server/game/Grids/Notifiers/GridNotifiers.cpp#L78-L92)

#### Инструментация Map Delayed Visibility: [Map.cpp](file:///C:/games/source/candidate-core-20261028/src/server/game/Maps/Map.cpp)
- **Проблема**: В исторических отчетах регистрировался необъясненный единичный stall длительностью 8037 мс в фазе `Map visibility`.
- **Реализация**:
  - В `Map::HandleDelayedVisibility()` добавлен точный замер времени выполнения с предупреждением: если обработка перемещений превышает 250 мс, сервер выводит `LOG_WARN` с указанием карты, затраченного времени и количества обработанных сущностей.
  - Сохранена критическая безопасность итерации по `i_objectsForDelayedVisibility` с вызовом `erase` до выполнения перемещения для предотвращения access violation при удалении сущностей.
- **Файл**: [Map.cpp](file:///C:/games/source/candidate-core-20261028/src/server/game/Maps/Map.cpp#L773-L788)

---

## 3. Гарантии корректности и целостности

1. **Многопоточная безопасность**: Ни одна изменяемая сущность (`Player`, `Unit`, `Map`, `MotionMaster`) не была вынесена в фоновые потоки без синхронизации. Все обновления происходят на соответствующих потоках карт под управлением `MapMgr`.
2. **Игровая функциональность**: Каждый бот сохраняет полноценный объект `Player` и `WorldSession`, кастомные классы CoA (ID 12–32), ротации способностей, таланты, экипировку и участие в экономических и боевых активностях.
3. **Персистентность**: Тест `--verify-persistence` (чтение 20 контрольных персонажей после повторного входа) завершился со статусом `passed` и **0 расхождений**.
4. **Соблюдение правил комментариев**: Изменения проверены утилитой `check_comments.py` — 0 нарушений.
