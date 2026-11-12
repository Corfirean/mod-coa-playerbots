# PERFORMANCE OPTIMIZATION PLAN — 3000 Active CoA Playerbots

## 1. Стратегическая Цель и Математический Бюджет Тика

В опорном baseline (`hardening-r19-fields-off-3000-r1`, 3 000 активных ботов) время тика мира (`world_processing`) складывается из двух последовательных фаз:
1. **`core_maps` (`sMapMgr->Update`)**: `77.40 ms` mean | `108.50 ms` p95 | `117.50 ms` p99 (выполняется параллельно на `MapUpdate.Threads` по 3 активным континентальным картам).
2. **`core_scripts` (`sScriptMgr->OnWorldUpdate` → `BotMgr::Update`)**: `60.45 ms` mean | `86.50 ms` p95 | `108.50 ms` p99 (выполняется однопоточно на главном потоке после завершения `sMapMgr->Update`).
- **Суммарный `world_processing`**: `137.98 ms` mean | **`185.00 ms` p95** | **`207.50 ms` p99**.

Чтобы достичь целевых критериев приёмки (**`world_processing` p95 ≤ 75 ms, p99 ≤ 150 ms** без потери игровой функциональности ботов), необходимо сократить:
- **`core_maps` (`Map::Update`)** с `108.5 ms` p95 до **≤ 42 ms** p95 (`-61%`).
- **`core_scripts` (`BotMgr::Update`)** с `86.5 ms` p95 до **≤ 33 ms** p95 (`-62%`).

Все оптимизации проектируются как **строго сохраняющие игровую семантику** (сокращается только стоимость повторных сканов, аллокаций, лишней сериализации в пустой сокет и холостых проверок), с поддержкой фича-флагов конфигурации (`.conf.dist` по умолчанию остаётся в безопасном консервативном режиме до прохождения квалификации).

---

## 2. Поэтапный План Оптимизаций по Фазам (Phases 1–8)

### Stage A — Phase 6: Spell Resolution, Rank Lookup & Per-Bot Capability Index
**Измеренный вклад в baseline**:
- `BotAI::SelectKnownSpell` = `12.097` веса (`25.3%` всего `BotAI::Update`), `SpellResolver::ResolveSpell` = `10.616` веса, `ntdll!RtlAllocateHeap` + `RtlFreeHeap` = `10.527` веса.

**Корневые проблемы**:
1. В `module/src/engine/SpellResolver.cpp` (`ResolveCached`) кэш `LearnedRanks::roots` ограничен 256 элементами: `if (cache->roots.size() >= 256 && !cache->roots.count(root)) cache->roots.clear();`. У каждого CoA-бота в `GetSpellMap()` > 300 записей.
2. В `module/src/engine/SpellPredicates.cpp` (`SelectKnownSpell`) для **каждого** заклинания в `GetSpellMap()` сначала вызывается `sSpellMgr->GetFirstSpellInChain(spellId)` и `SpellResolver::ResolveSpell`, и только **после** этого проверяется `predicate(spellInfo)`! Это переполняет 256-элементный кэш на каждом вызове `SelectKnownSpell`, вызывая бесконечный цикл `clear()` + реаллокацию хэш-таблицы.
3. На каждый вызов `ResolveCached` выполняются 2 отдельных поиска по строковому ключу в `DataMap` с `dynamic_cast` (`bot->CustomData.GetDefault<LearnedRanks>("bot_rank_cache")` и `SpellBookRevision::Get(bot->CustomData)`).

**Реализация**:
1. **Устранение thrashing в `SpellResolver`**:
   - Убрать деструктивный сброс `roots.clear()` при 256 элементах (книга заклинаний одного бота конечна — ~300–600 корней, и кэш уже инвалидируется при изменении `SpellBookRevision` или `SpellRankLookup::MetadataEpoch()`).
   - Хранить ссылку/указатель на счётчик ревизии непосредственно внутри `LearnedRanks`, сокращая `DataMap` lookup + `dynamic_cast` с 2 до 1 и используя плоский открытый массив/таблицу быстрого доступа.
2. **Оптимизация `SelectKnownSpell`**:
   - Проверять базовую пригодность заклинания (не удалено, `SpellInfo` существует, `predicate(spellInfo)` истинен, и в `spellMap` нет активного `nextRank` из цепочки) **до** вызова `ResolveSpell` / `IsSpellReady`.
3. **Per-Bot `CapabilityIndex` (Phase 6)**:
   - Добавить персистентный в рамках `CustomData` индекс `BotCapabilityIndex`, инвалидируемый только по `(SpellBookRevision, SpellRankLookup::MetadataEpoch(), activeShapeshiftForm)`:
     - Предварительно отфильтрованные списки изученных максимальных рангов по категориям: `CombatSpells`, `HealingSpells`, `DispelSpells`, `InterruptSpells`, `BuffAndUtilitySpells`, `AllActiveRankSpells`.
   - Использовать этот компактный вектор (~15–40 активных классовых заклинаний вместо 350+ сырых записей `PlayerSpellMap`) при поиске заклинаний в `SelectKnownSpell` и ротациях `BotAI`.

---

### Stage B — Phases 4, 5, 7, 8: `BotMgr`, `BotGear`, `WorldEncounterDirector`, `BotScheduling`, `BotMovement`

#### 2.1. Phase 4 — `BotMgr` O(1) индексация и консолидация тика (`~5.85 ms/tick` → `< 0.8 ms/tick`)
- **Проблема**:
  - `BotMgr::FindBotSession(ObjectGuid::LowType charLowGuid)` выполняет линейный поиск `O(N)` по `_botSessions` (3 000 элементов).
  - `BotMgr::Update` выполняет 6 отдельных циклов по всем 3 000 сессиям на каждом 10 ms тике мира (телепорты, инвайты в группы/гильдии, межкартовый фоллоу лидера, возврат призрака на карту трупа, групповой лут, трейд).
- **Реализация**:
  - Добавить `std::unordered_map<ObjectGuid::LowType, WorldSession*> _botSessionByLowGuid` в `BotMgr`, синхронно обновляемый при `Add`/`Erase`/`Clear` сессий, сделав `FindBotSession` и `FindBotPlayer` строго `O(1)`.
  - Объединить разрозненные per-tick проходы по `_botSessions` в `BotMgr::Update` и разнести некритичные к миллисекундам проверки (межкартовый фоллоу, проверка трупа на другой карте, проверка приглашений/лута при отсутствии активной группы) по детерминированным бакетам (staggered slices по тикам) без потери отзывчивости.

#### 2.2. Phase 4/6 — Предвычисление `CandidatePool` в `BotGear` / `BotSpawnRandom` (устранение пиков `27–54 ms` / `641 ms` в `gear_repair`)
- **Проблема**:
  - `BotSpawn::RepairGear` вызывает `GearPool` 70+ раз на бота. Внутри `GearPool` для каждого кандидата вызывается `BotGear::Allowed(bot, item)` (аллокация `std::string name = item->Name1`, `std::transform(::tolower)`, 8 поисков `name.find(...)`) и 4 раза вызывается `ItemPower` (`sSpellMgr->GetSpellInfo`), а затем `std::sort` пересчитывает `PowerScore` на каждом сравнении.
- **Реализация**:
  - При однократном построении `BuildCandidatePool()` отфильтровать статически запрещённые предметы (`AllowedTemplate(item)`) и сразу сохранить предвычисленные `pvePower` и `pvpPower` в структуре `LevelItemCandidate`.
  - В `GearPool` использовать готовые `candidate.pvePower` / `candidate.pvpPower` и быструю проверку `AllowedForBot(bot, item)` без строковых аллокаций и повторных вызовов `sSpellMgr->GetSpellInfo`.

#### 2.3. Phase 5 — `WorldEncounterDirector` O(N) индексация и снапшоты (`encounter_director` пики `8.0 ms` → `< 0.5 ms`)
- **Проблема**:
  - `EvaluateCandidateGeneration` вызывает `IsRealPlayer(p)` и `IsBotPlayer(p)` для 3 000 игроков, захватывая `std::mutex cacheMutex` в `BotMgr::IsBotAccountId` 6 000 раз.
  - `TryBuildWorldEncounter` в `HunterAndPrey`, `Caravan`, `RivalAdventuringParty` выполняет вложенные циклы `O(N^2)` по `freeBots`, конструируя `BuildCandidateBotSnapshot` внутри внутреннего цикла (`N*(N-1)` раз!).
- **Реализация**:
  - Использовать быстрый безмьютексный предикат `session->IsBot()` в `IsBotPlayer` / `IsRealPlayer`.
  - Вычислять `CandidateBotSnapshot` **один раз** для каждого бота в `freeBots` (`O(N)`) перед подбором пар и индексировать кандидатов по карте/команде/уровневым корзинам.

#### 2.4. Phase 7 — Priority Work Scheduler & Tail-Latency Control (`BotScheduling` / `BotWorkBudget`)
- **Реализация**:
  - Разделить планирование и бюджетирование работ на классы приоритета (`LatencyCritical`: активный бой, прерывание каста, экстренное исцеление, выход из опасных зон / телепорт; `Background`: переоценка экипировки, несрочный выбор нового квеста, экономика/крафт, дальнее перепланирование маршрута).
  - Добавить защиту от голодания (`starvation prevention` по возрасту ожидания в очереди) и метрики возраста очередей.

#### 2.5. Phase 8 — Оптимизация `BotMovement::Update` и `ResolveGroundZ` (`5.202` веса → `< 1.2` веса)
- **Проблема**:
  - На каждом вызове `BotMovement::Update` безусловно вызывается `ResolveGroundZ(bot, bx, by, bz)` (`3.032` веса — 58.3% всей стоимости `BotMovement::Update`!), хотя полученный `groundZ` используется только раз в 500 ms в блоке `if (now - lastSafe >= 500)` или при проверке `airborne`.
  - В `BotMovement::Update` выполняется 8–10 отдельных поисков по `unordered_map<ObjectGuid, ...>` в `shard` (`locomotion`, `mounts`, `lastSafeCheck`, `airborneMs`, `waterEscapeTarget`).
- **Реализация**:
  - Вызывать `ResolveGroundZ(bot, bx, by, bz)` лениво — только когда реально выполняется 500 ms проверка безопасности (`now - lastSafe >= 500`) или бот уже находится в состоянии падения/прыжка (`IsFalling()` / `airborneMs > 0`), плюс кэшировать `(lastGroundX, lastGroundY, lastGroundZ)` при неподвижном боте.
  - Консолидировать обращения к `shard.locomotion[botGuid]` и `shard.mounts[botGuid]` через локальные указатели/ссылки в начале `BotMovement::Update`.

---

### Stage C — Phases 2 & 3: Ядро и Скрипты (`Player::Update`, `Creature::Update`, `Map::UpdateNonPlayerObjects`, `HandleDelayedVisibility`)

#### 2.6. Phase 2/6 — Оптимизация `PlayerScript::OnPlayerUpdate` и `UnitScript::OnUnitUpdate` в ядре и модулях (`~28.0` веса → `< 3.0` веса)
1. **`AscensionTinker.cpp` (`tinker_player::OnPlayerUpdate` = `3.005` веса)**:
   - Добавить отсутствующий ранний выход `if (Owner(player) != player) return;` в начале `OnPlayerUpdate` (сейчас код Tinker выполняется каждые 500 ms для **всех 3 000 игроков всех 21 класса** с захватом глобального `stateMutex`!).
   - Кэшировать проверку `ReconcileModules(player)` по `(SpellBookRevision, talentAuraMask)`.
2. **`SkinpeelerQuest.cpp` (`SkinpeelerTrackScript::OnPlayerUpdate` = `2.999` веса)**:
   - В блоке отправки клиентского UI-пакета иконки квеста (`SMSG_UPDATE_QUEST_ICON`, каждые 1000 ms ищет существ в радиусе 200 ярдов) пропускать поиск `FindNearestCreature`, если `player->GetSession()->IsBot()` (так как `SendPacket` в пустой сокет бота — чистый no-op).
3. **`AscensionGuardianTalents.cpp` (`3.486` веса), `AscensionXoroth.cpp` (`2.962` веса), `AscensionCultist.cpp` (`2.213` веса), `AscensionVenomancer.cpp` (`1.745` веса), `AscensionPyromancer.cpp` (`1.599` веса), `AscensionSunCleric.cpp` (`1.379` веса), `AscensionWitchDoctorAuras.cpp` (`1.306` веса)**:
   - В каждом из этих классовых скриптов синхронизация заклинаний (`Refresh` / `SyncReplacements` / `Replace`) сканирует всю `player->GetSpellMap()` по таймеру (каждые 100–500 ms), даже когда ни `SpellBookRevision::Get(player->CustomData)`, ни уровень игрока, ни ключевые таланты/ауры не изменились.
   - Кэшировать сигнатуру состояния `(spellBookRevision, level, controllingAuraMask)` на состоянии игрока и выполнять полный пересчёт книги заклинаний **только при реальном изменении** ревизии книги заклинаний, уровня или контролирующих аур!
   - В `AscensionSunCleric::Refresh`: не вызывать 100-ярдовый grid search `Nearby(player, 100)` каждые 500 ms вне боя при отсутствии цели `blessed`, если рядом никто не нуждается в переназначении.
4. **`AscensionRemovedSpellActionBars.h` (`1.021` веса)**:
   - Проверять `if (!Applies(player)) return;` **до** поиска в `player->CustomData.Get<Pending>("coa.removed_spell_action_bars")`.
5. **`CoA.Challenges.Scripts.cpp` (`CoAChallengesPlayer::OnPlayerUpdate` = `2.356` веса)**:
   - Добавить быстрый путь: если у игрока нет активных режимов испытаний (`CachedGameModeMask == 0`) и нет отложенных `SpellbindPending` или косметических состояний, пропускать 7 ежетиковых вызовов `HungerUpdate`, `FatigueUpdate`, `SpellbindUpdate`, `EnforceHighRisk`, `UpdateCosmeticElite`, `UpdateAdventureHealthPreview`.

#### 2.7. Phase 2 — Оптимизация `HandleDelayedVisibility` / `Player::UpdateVisibilityOf` (`27.342` веса → `< 10.0` веса) и диагностика Stall
- **Проблема**:
  - При перемещении 3 000 ботов `PlayerRelocationNotifier` вызывает `Player::UpdateVisibilityOf<T>(target, i_data, i_visibleNow)`.
  - Внутри `UpdateVisibilityOf` для ботов без клиентского сокета (`IsBot()`) вызываются `target->BuildCreateUpdateBlockForPlayer(&data, this)` и `target->BuildOutOfRangeUpdateBlock(&data)`, которые сериализуют полные бинарные пакеты `UpdateData` (аллокации `ByteBuffer`, побитовые маски полей `UpdateMask`), а затем `VisibleNotifier::SendToSelf()` собирает из `i_data` `WorldPacket` и вызывает `SendDirectMessage(&packet)`, который **немедленно выбрасывает пакет**, так как у бота нет `WorldSocket`!
  - То же самое происходит в `Map::SendObjectUpdates()` (`BuildValuesUpdateBlockForPlayer` для получателей-ботов).
- **Реализация**:
  - При `player->IsBot()` (под управлением конфигурационного флага `CoA.Playerbots.Perf.SkipBotClientUpdatePackets`, включённого для ботов без сокета):
    - Полностью сохранять серверную логику видимости: `GetObjectVisibilityContainer().LinkWorldObjectVisibility(target)`, `UnlinkWorldObjectVisibility(target)`, `UpdateObjectVisibility(false)`, `visibleNow` и `MoveInLineOfSight` / `AIRelocationNotifier`.
    - Пропускать только построение бинарных сетевых буферов `BuildCreateUpdateBlockForPlayer`, `BuildOutOfRangeUpdateBlock` и `i_data.BuildPacket` для игроков без клиентского сокета (`IsBot()`), а также не добавлять ботов без сокета в сетевой список сериализации `AddObjectToPendingUpdateList` (который используется исключительно в `Map::SendObjectUpdates` для отправки `SMSG_UPDATE_OBJECT` клиентам).
  - Добавить диагностику и телеметрию в `Map::HandleDelayedVisibility` для отслеживания числа релокаций и мгновенного логирования любого stall > 250 ms.

#### 2.8. Phase 3 — Оптимизация `Map::UpdateNonPlayerObjects` и `Creature::Update` (`60.330` веса → `< 22.0` веса)
1. **Инлайн-состояние `InterleavedUpdateClock` внутри `UpdatableMapObject` (`3.903` веса → `~0`)**:
   - Сейчас `InterleavedUpdateClock::Due` на каждом не-игровом объекте (64 000 раз за обход) вызывает `obj->GetGUID()` (`1.795` веса) и ищет указатель в огромной хэш-таблице `std::unordered_map<uintptr_t, Entry> _entries` (`2.108` веса).
   - Перенести поля `_interleavedLastUpdateMs`, `_interleavedAccumulatedDiff`, `_interleavedSlice` непосредственно в базовый класс `UpdatableMapObject` (`Object.h`), превратив `InterleavedUpdateClock::Due` в `O(1)` чтение полей самого объекта без единого обращения к `unordered_map`!
2. **Оптимизация `destiny_weaver_view_refresh_script::OnAllCreatureUpdate` (`2.624` веса → `~0.1` веса)**:
   - В `modules/mod-destiny-weaver/src/destiny_weaver_scaling.cpp` на каждом тике каждого из 63 905 существ вызываются `creature->CustomData.Erase(DAMAGE_REMAINDER_KEY)` и `creature->CustomData.GetDefault<SeenFaction>(SEEN_FACTION_KEY)` (аллоцируя `SeenFaction` в `CustomData` для каждого живого существа в мире!).
   - Проверять наличие данных без аллокации (`Get<SeenFaction>`) и выполнять проверку смены фракции только для существ с активным масштабированием / раз в интервал `VIEW_REFRESH_INTERVAL_MS`.
3. **Устранение лишних `AIRelocationNotifier` (`17.358` веса) и `UpdatePositionData` для мирных ненаблюдаемых NPC (NPC Simulation LOD — Phase 3)**:
   - Из 63 905 загруженных существ большинство — мирные существа, блуждающие через `RandomMovementGenerator` в гридах, где поблизости (в радиусе агро/боя) нет враждебных целей или реальных игроков.
   - При каждом шаге сплайна `Creature::UpdateSplinePosition` вызывает `Map::CreatureRelocation` → `SetDelayedUnitAINotifyEvent`, что на каждом тике запускает `Cell::VisitObjects<Acore::AIRelocationNotifier>` (`17.358` веса — `42.4%` всей стоимости `Unit::Update` существ!).
   - Реализовать безопасную систему **NPC Simulation LOD** (управляемую через `MapUpdate.NpcSimulationLod.*`):
     - Классифицировать существ на: `ActiveCombat` (в бою, с угрозой, призывные/петы/транспорты/боссы/ивентовые), `Observed` (вблизи реальных игроков или взаимодействующих ботов), и `PeacefulFar` (вне боя, без угрозы, далеко от игроков).
     - Для мирных существ вне боя коалесцировать частоту `AIRelocationNotifier` (не чаще раза в `NpcLodAiNotifyIntervalMs`, например 250–400 ms вместо каждых 10–50 ms сплайна, с немедленной проверкой при входе в бой или приближении игрока) и пропускать избыточные VMap raycasts `GetAreaAndLiquidData` при горизонтальном перемещении в пределах одной и той же ячейки на одном уровне Z.

---

### Stage D — Phase 1 & Phase 9: Масштабирование `MapUpdate.Threads` и Квалификационные Бенчмарки
1. Проверить масштабирование `MapUpdate.Threads` (`1, 2, 4, 6, 8`) на континентальном (Distributed) и концентрированном (Concentrated) профилях размещения.
2. Дополнительно проверить возможность параллельного подготовительного обновления per-map ботов или снижения сериализации на главном потоке при полном соблюдении потокобезопасности (`BotAI::Update` никогда не вызывается из не-авторизованных фоновых потоков одновременно с мутацией мира).
3. Провести серию контролируемых бенчмарков на 3 000 ботов с проверкой персистентности (`--verify-persistence`), игровой пропускной способности и стабильности памяти.
