# PERFORMANCE BASELINE — 3000 Active CoA Playerbots (Phase 0 Attribution)

## 1. Среда, Репозитории и Контроль Артефактов

### 1.1. Конфигурация хоста (`host.json`)
- **ОС**: Windows 11 Build 26200 / 26300 (`10.0.26200`)
- **CPU**: AMD Ryzen 7 7700X (8 физических ядер / 16 логических потоков, 4500 MHz base)
- **RAM**: 32 GB (`33,397,133,312` bytes)
- **Storage**: NVMe MSI M460 2TB + WDC PC SN730 1TB

### 1.2. Состояние репозиториев и рабочие ветки
- **Модуль (`mod-coa-playerbots`)**:
  - Локальный путь: `C:\games\source\release-bots-20261029`
  - Исходная ветка: `perf/population-with-world-hardening-20261103` (`1efef4f`)
  - Рабочая ветка итерации: `perf/3000-active-bots-20261112`
- **Ядро (`azerothcore-wotlk-coa`)**:
  - Локальный путь тестового стенда: `C:\games\source\candidate-core-20261028` (worktree от `C:\games\coa-core-fork`)
  - Исходный коммит стенда: `66380c781` (`integrate/launcher-module-candidate-20261031`) с наложенным `docs/performance-core.patch` (проверено через `git apply --check --reverse`, код возврата `0`)
  - Рабочая ветка итерации: `perf/3000-active-bots-20261112` (базовое состояние `docs/performance-core.patch` сохранено в бэкап `C:\games\source\bot-performance-artifacts-20261102\pre-audit-backup-20261112` и зафиксировано коммитом `8f5cfc5fd`).
- **Изолированный тестовый стенд и БД**:
  - Путь фикстуры: `C:\games\source\bot-performance-20261102` (отдельный экземпляр MySQL 8.4 на порту `33070`, `worldserver` на порту `18085`, SOAP `17878`).
  - Замороженные бинарники и профили: `C:\games\source\bot-performance-artifacts-20261102` (`runtime-hardening-r8` .. `runtime-hardening-r19-fields`).

---

## 2. Характеристика Популяции 3 000 Ботов (`seed-population.json`)

Все 3 000 seed-ботов — полноценные объекты `Player` с реальными `WorldSession` (без клиентского сокета):
- **Распределение по картам (Континентальная / Distributed модель)**:
  - **Map 1 (Kalimdor)**: `1,421` ботов (`47.37%`)
  - **Map 0 (Eastern Kingdoms)**: `1,301` ботов (`43.37%`)
  - **Map 530 (Outland)**: `278` ботов (`9.27%`)
- **Распределение по классам**: все 21 кастомный класс CoA (ID `12..32`, от `116` до `162` ботов на каждый класс).
- **Уровни**: `1..9` (активный стартовый и ранний открытый мир).
- **Мировые объекты на активных картах (`hardening-r19-fields-off-3000-r1`)**:
  - Загруженных `Creature` в активных гридах: `63,905`
  - Загруженных `GameObject` в активных гридах: `26,171`
- **Динамическое состояние ботов в окне измерения (900 с)**:
  - Онлайн: `3,000` (`100%`)
  - В бою (`combat`): `680 → 728` (`~23.5%`)
  - В движении (`moving`): `1,451 → 1,433` (`~48.0%`)
  - Мертвы / призраки (`dead`): `415 → 450` (`~14.4%`)
  - В режиме дальнего адаптивного тика (`adaptive_far`): `2,208 → 2,259` (`~74.5%`)

---

## 3. Baseline Тайминги и Распределение Времени Тика (3 000 Ботов)

В качестве чистого опорного baseline на текущем бинарнике используется контрольный прогон **`hardening-r19-fields-off-3000-r1`** (900 секунд измерения после 600 секунд прогрева на 3 000 ботах, `MapUpdate.Threads = 4`, `MapUpdateInterval = 10`, экспериментальный `SpellFiltering.Enabled = 1`, `FieldReadFastPath.Enabled = 0`), дополненный парным прогоном **`hardening-r19-fields-3000-r1`** и 120-секундным stack-профилем **`hardening-r19-waits-3000-r1`** (`428,688` сэмплов Very Sleepy).

### 3.1. Сводная таблица подсистем (`hardening-r19-fields-off-3000-r1`, 6 525 тиков за 900.33 с)

| Подсистема (`subsystems.csv`) | Calls | Calls/s | Total Wall/Thread Sum (s) | Mean / Call (ms) | p50 (ms) | p95 (ms) | p99 (ms) | Max (ms) | Доля от `world_processing` |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| **`world_processing`** | 6,525 | 7.25 | 900.320 | **137.980** | **133.250** | **185.000** | **207.500** | **277.081** | **100.0%** |
| **`world_tick_period`** | 6,525 | 7.25 | 900.330 | **137.982** | **133.250** | **185.000** | **207.500** | **277.081** | — |
| **`core_maps` (`MapMgr::Update` wall)** | 6,525 | 7.25 | 505.067 | **77.405** | **76.250** | **108.500** | **117.500** | **186.682** | **56.10%** |
| **`core_scripts` (`OnWorldUpdate` wall)** | 6,525 | 7.25 | 394.445 | **60.451** | **57.250** | **86.500** | **108.500** | **171.630** | **43.81%** |
| **`manager` (`BotMgr::Update`)** | 6,525 | 7.25 | 391.030 | **59.928** | **56.750** | **86.250** | **107.750** | **171.227** | **43.43%** |
| **`ai` (`BotAI::Update` sum)** | 7,795,355 | 8,658.35 | 352.842 | **0.045** | **0.008** | **0.192** | **0.511** | **42.681** | **39.19%** |
| **`movement` (`BotMovement::Update`)** | 7,794,955 | 8,657.90 | 51.319 | **0.007** | **0.001** | **0.032** | **0.042** | **4.640** | **5.70%** |
| **`gear_evaluate` (`BotGear::Update`)** | 7,795,355 | 8,658.35 | 28.081 | **0.004** | **0.001** | **0.001** | **0.001** | **54.027** | **3.12%** |
| **`gear_repair` (`BotSpawn::RepairGear`)** | 125 | 0.14 | 3.418 | **27.345** | **26.938** | **31.688** | **50.375** | **53.835** | **0.38% (пики до 54 ms)** |
| **`encounter_director`** | 6,525 | 7.25 | 0.785 | **0.120** | **0.001** | **0.001** | **4.112** | **8.020** | **0.09% (пики до 8 ms)** |
| **`core_sessions` (`UpdateSessions`)** | 6,525 | 7.25 | 0.672 | **0.103** | **0.072** | **0.124** | **0.950** | **8.807** | **0.07%** |
| *`map_total` (сумма по worker-потокам)* | 25,840 | 28.70 | *1,215.380* | *47.035* | *46.375* | *99.250* | *112.500* | *185.790* | *(2.406x параллелизм)* |
| *`map_non_player_objects` (worker sum)* | 25,840 | 28.70 | *502.508* | *19.447* | *23.562* | *37.125* | *42.875* | *77.674* | *41.35% от `map_total`* |
| *`map_players` (worker sum)* | 25,840 | 28.70 | *469.035* | *18.152* | *19.312* | *39.875* | *48.625* | *104.228* | *38.59% от `map_total`* |
| *`map_visibility` (worker sum)* | 25,840 | 28.70 | *200.629* | *7.764* | *6.484* | *24.438* | *31.188* | *80.228* | *16.51% от `map_total`* |
| *`map_cells` (worker sum)* | 25,840 | 28.70 | *43.095* | *1.668* | *1.300* | *5.047* | *6.609* | *22.927* | *3.55% от `map_total`* |

---

## 4. Ответы на Обязательные Вопросы Phase 0 (1–14)

### 4.1. CPU utilization по потокам и картам (Пункты 1, 2, 3, 4, 6)
- **Почему загрузка CPU составляет всего ~1.79 ядра (11.2% от 16 потоков Ryzen 7 7700X)**:
  - В течение 900.33 с измерения сервер выполнил `1,609.82` CPU-секунд полезной работы (`1,215.38 с` в `map_total` + `394.44 с` в `core_scripts`), что равно **1.788 ядра**.
  - Фаза 1 тика (`sMapMgr->Update`, `505.07 с` wall time, `56.1%` времени тика): работают только **3 континентальных карты с ботами** (Kalimdor — `1,421` бот, Eastern Kingdoms — `1,301` бот, Outland — `278` ботов). Даже при `MapUpdate.Threads = 4` эффективный коэффициент параллелизма равен `1215.38 / 505.07 = 2.406x`, так как время `core_maps` полностью определяется самой медленной из двух крупных карт (Kalimdor или Eastern Kingdoms — в среднем `77.4 ms` за тик, p95 `108.5 ms`), пока остальные потоки ждут на `MapUpdater::wait()`.
  - Фаза 2 тика (`sScriptMgr->OnWorldUpdate` → `BotMgr::Update`, `394.44 с` wall time, `43.8%` времени тика): выполняется **строго однопоточно на главном world thread**, пока все `MapUpdate.Threads` простаивают в ожидании следующего тика.
  - **Вывод**: поскольку `world_processing` (`137.98 ms` mean, `185.0 ms` p95) равен сумме последовательных фаз `core_maps` (`77.4 ms` mean, `108.5 ms` p95) + `core_scripts` (`60.45 ms` mean, `86.5 ms` p95), достичь целевого **p95 ≤ 75 ms** невозможно без одновременного кардинального ускорения **обеих** фаз (`Map::Update` и `BotMgr::Update`).
- **Tick Processing vs Tick Period**:
  - Поскольку среднее время тика (`137.98 ms`) превышает `MapUpdateInterval = 10 ms`, сервер работает без пауз между тиками (`world_processing` == `world_tick_period` = `137.98 ms`, частота тиков мира `7.25 Hz`).

### 4.2. Контеншн mutex/locks и ожидание потоков (Пункты 5, 6)
- В 120-секундном профиле `hardening-r19-waits-3000-r1` (`include_wait_frames = true`, общий вес со спящими потоками `939.776`, активный вес после фильтрации ожидания `207.648`):
  - Ожидание worker-потоков карт и фоновых потоков на условных переменных (`NtWaitForAlertByThreadId` / `SleepConditionVariableSRW`): `732.128` (простой потоков из-за сериализации `BotMgr::Update` и дисбаланса 2 больших карт).
  - Реальный lock contention в активном коде сосредоточен в:
    1. `AscensionTinker::State(Player*)` (`std::lock_guard<std::mutex> lock(stateMutex)` вызывается каждые 500 ms для всех 3 000 ботов из параллельных `Map::Update` потоков!).
    2. `BotMgr::IsBotAccountId` (`std::lock_guard<std::mutex> guard(cacheMutex)` вызывается 6 000 раз при каждом проходе `WorldEncounterDirector::EvaluateCandidateGeneration`).
    3. Глобальный аллокатор кучи Windows `ntdll!RtlAllocateHeap` (`6.959` активного веса) / `ntdll!RtlFreeHeap` (`3.568` активного веса) при параллельных аллокациях в `Map::Update` и `SelectKnownSpell`.

### 4.3. Полная декомпозиция `Map::Update`, `Player::Update`, `Creature::Update`, `BotAI::Update` по сэмплам стека (`hardening-r19-waits-3000-r1`, активный вес `207.648`) (Пункты 7, 8, 9, 10, 12, 13)

| Ранг | Узел стека / Функция | Активный вес (inclusive) | % от активного CPU (`207.65`) | Подсистема и корневая причина |
|---:|---|---:|---:|---|
| — | **`Map::Update`** | **150.793** | **72.62%** | Суммарное обновление карт в worker-потоках |
| — | **`BotMgr::Update`** | **52.905** | **25.48%** | Однопоточное обновление ботов на главном потоке (`BotAI::Update` = `47.828`) |
| **1** | **`Map::UpdateNonPlayerObjects`** (`Creature::Update` = `53.136`) | **60.330** | **29.05%** | Обновление `63,905` существ в активных гридах:<ul><li>`Cell::VisitObjects<AIRelocationNotifier>` (`17.358`, `8.36%` всего сервера!) — `MoveInLineOfSight` + `CanSeeOrDetect` (`6.630`) при случайном блуждании мирных NPC</li><li>`Unit::UpdateSplinePosition` (`7.516`) — перемещение сплайнов + VMap `GetAreaAndLiquidData`</li><li>`SmartAI::UpdateAI` (`4.074`)</li><li>`ExecuteScript<AllCreatureScript>` (`3.811`), из них `destiny_weaver_view_refresh_script::OnAllCreatureUpdate` = `2.624` (лишние обращения к `CustomData` на каждом тике каждого существа)</li><li>`RandomMovementGenerator::Update` (`2.666`)</li><li>`InterleavedUpdateClock::Due` (`2.108`) + `Object::GetGuidValue` (`1.795`) — поиск в `unordered_map` на 64 000 элементов на каждом объекте</li></ul> |
| **2** | **`Player::Update`** | **58.291** | **28.07%** | Обновление `3,000` игроков-ботов:<ul><li>`ScriptMgr::OnPlayerUpdate` + `OnUnitUpdate` (**`~28.0` веса, 48% всего `Player::Update`!**):<ul><li>`guardian_talents::OnPlayerUpdate` (`3.486`) — полный обход `GetSpellMap()` + `std::set`/`std::map` каждые 100 ms</li><li>`tinker_player::OnPlayerUpdate` (`3.005`) — **отсутствует проверка класса!** Выполняется для всех 3 000 ботов всех классов каждые 500 ms с захватом глобального `stateMutex`</li><li>`SkinpeelerTrackScript::OnPlayerUpdate` (`2.999`) — поиск NPC в радиусе 200 ярдов каждые 1000 ms у всех 3 000 ботов ради отправки клиентского пакета `SMSG_UPDATE_QUEST_ICON` в пустой сокет</li><li>`xoroth_player::OnPlayerUpdate` (`2.962`) — 4–8 полных сканов книги заклинаний каждые 250 ms (`16–32` скана в секунду на бота) без проверки ревизии</li><li>`CoAChallengesPlayer::OnPlayerUpdate` (`2.356`) — 7 проверок режимов испытаний на каждом тике даже при `CachedGameModeMask == 0`</li><li>`cultist_player` (`2.213`), `venomancer_player` (`1.745`), `pyromancer_player` (`1.599`), `sun_cleric_player` (`1.379`, включая 100-ярдовый grid search `Nearby` каждые 500 ms), `witch_doctor_update` (`1.306`) — регулярные полные сканы `GetSpellMap()` без проверки изменения аур и `SpellBookRevision`</li><li>`AscensionRemovedSpellActionBars::Flush` (`1.021`) — поиск в `CustomData` до проверки класса `Applies(player)`</li></ul></li><li>`Unit::Update` (ауры, периодические тики, сплайны игрока): `20.531`</li><li>`Player::UpdateLocalChannels`: `3.841`</li></ul> |
| **3** | **`Map::HandleDelayedVisibility`** (`ExecuteDelayedUnitRelocationEvent`) | **27.342** | **13.17%** | Обновление видимости при движении игроков и существ:<ul><li>`Cell::VisitObjects<PlayerRelocationNotifier>` (`21.663`) → `Player::UpdateVisibilityOf<Creature>` (`14.439`) + `<Player>` (`2.899`) + `<GameObject>` (`1.616`)</li><li>Для ботов без сокета (`IsBot()`) `UpdateVisibilityOf` сериализует бинарные `UpdateData` блоки (`BuildCreateUpdateBlockForPlayer` / `BuildOutOfRangeUpdateBlock` / `BuildValuesUpdateBlockForPlayer`) и сжимает/собирает `WorldPacket` в `SendToSelf()`, который тут же отбрасывается в `SendDirectMessage()`, так как у бота нет `WorldSocket`!</li><li>`Map::AddObjectToPendingUpdateList`: `2.548`</li><li>`Cell::VisitObjects<CreatureRelocationNotifier>`: `1.976`</li></ul> |
| **4** | **`BotAI::SelectKnownSpell` + `SpellResolver::ResolveSpell`** | **12.097** (`SelectKnownSpell`) / **10.616** (`ResolveSpell`) | **5.83%** (25.3% от `BotAI::Update`) | **Критический дефект кэша (Cache Thrashing)**:<ul><li>В `SpellResolver.cpp` кэш `LearnedRanks::roots` ограничен 256 записями и при переполнении выполняет `cache->roots.clear()`</li><li>У кастомных классов CoA в `GetSpellMap()` > 300 способностей/пассивок/навыков</li><li>`SelectKnownSpell` вызывает `ResolveSpell` для **каждого** заклинания в `GetSpellMap()` **до** проверки `predicate(spellInfo)`</li><li>В результате каждый вызов `SelectKnownSpell` переполняет лимит 256, сбрасывает кэш и заново аллоцирует тысячи узлов `std::unordered_map`, порождая пиковую нагрузку на `RtlAllocateHeap` (`6.959`) и `RtlFreeHeap` (`3.568`)!</li></ul> |
| **5** | **`BotMovement::Update` → `ResolveGroundZ`** | **5.202** (`3.032` в `ResolveGroundZ`) | **2.51%** | На каждом тике `BotAI::Update` вызывается `ResolveGroundZ(bot, bx, by, bz)` → `UpdateAllowedPositionZ` → VMap/Grid raycast `GetHeight` (`3.032` веса, 58% всей стоимости `BotMovement::Update`), даже когда координаты `(bx, by, bz)` бота не изменились или не наступил 500 ms интервал проверки безопасной позиции! Плюс 8–10 повторных поисков `shard.locomotion[botGuid]` / `mounts[botGuid]` за один вызов. |
| **6** | **`BotGear::Update` / `BotSpawn::RepairGear`** | Пики **27–54 ms** (до **641 ms**) | — | `RepairGear` вызывает `GearPool` > 70 раз на бота; каждый вызов копирует и приводит к нижнему регистру `item->Name1`, ищет 8 подстрок и пересчитывает `ItemPower` через `sSpellMgr->GetSpellInfo` на каждом сравнении `std::sort`. |
| **7** | **`BotMgr` линейные сканы и циклы по 3 000 сессиям** | **~5.0 ms / тик** (`38.2 с` за 900 с) | **~3.6% тика** | Разница между `manager` (`391.03 с`) и `ai` (`352.84 с`) составляет **38.19 с** (`5.85 ms` на каждый тик мира): 6 отдельных полных проходов по всем 3 000 сессиям в `BotMgr::Update` + `O(N)` линейный поиск в `BotMgr::FindBotSession`. |
| **8** | **`WorldEncounterDirector` (`EvaluateCandidateGeneration` + `O(N^2)` подбор пар)** | Пики **4.1–8.0 ms** | — | Каждые 5 с обходит всех 3 000 игроков с 6 000 захватами мьютекса `cacheMutex` в `IsBotAccountId` и строит `CandidateBotSnapshot` внутри вложенных циклов `O(N^2)` в `HunterAndPrey`, `Caravan`, `RivalAdventuringParty`. |

### 4.4. Анализ аномального stall `8302.618 ms` (`hardening-r19-fields-3000-r1`)
- В прогоне `hardening-r19-fields-3000-r1` зафиксирован единичный всплеск:
  - `world_processing.max_us = 8,302,618` (`8,302.618 ms`)
  - `core_maps.max_us = 8,167,843` (`8,167.843 ms`)
  - `map_total.max_us = 8,167,183` (`8,167.183 ms`)
  - `map_visibility.max_us = 8,037,467` (`8,037.467 ms`)
  - В том же прогоне `gear_repair.max_us = 641,816` (`641.816 ms`), `gear_evaluate.max_us = 641,897` (`641.897 ms`), `ai.max_us = 661,401` (`661.401 ms`).
- **Установленные факты**:
  - 98.4% длительности этого stall (`8,037.47 ms` из `8,167.18 ms` в `Map::Update`) пришлось на фазу `Map::HandleDelayedVisibility`.
  - В `ExecuteDelayedUnitRelocationEvent` при массовом одновременном перемещении/телепортации/спавне ботов выполняется синхронная подгрузка гридов (`GetMap()->LoadGridsInRange(*player, MAX_VISIBILITY_DISTANCE)`) + квадратичный обход `PlayerRelocationNotifier` с генерацией пакетов `UpdateData` для всех объектов в радиусе видимости.
  - В контрольном прогоне `hardening-r19-fields-off-3000-r1` максимальное время `map_visibility` составило `80.228 ms`, а `world_processing.max` — `277.081 ms`.
  - В рамках Phase 2 мы добавляем:
    1. Устранение бесполезной сериализации бинарных `UpdateData` / `WorldPacket` в `Player::UpdateVisibilityOf` и `VisibleNotifier::SendToSelf` для ботов без клиентского сокета (`IsBot()`), при полном сохранении серверных графов видимости (`ObjectVisibilityContainer`, `AddObjectToPendingUpdateList`, `MoveInLineOfSight`).
    2. Детальные диагностические счётчики для `HandleDelayedVisibility` (количество обработанных unit/player relocation событий, число вызовов `LoadGridsInRange` и детектор stall > 250 ms с логированием карты, числа релокаций и подгрузок гридов).

### 4.5. Вызовы `PathGenerator`, БД и Игровая Активность (Пункты 8, 11, 14)
- **Навигация (`PathGenerator`)**:
  - В `hardening-r19-waits-3000-r1` прямой вес `Detour` / `PathGenerator::CalculatePath` составляет `< 0.5%` активного профиля благодаря уже действующим `BotMovement` waypoint/corridor кэшам и бюджетам, тогда как главной стоимостью внутри `BotMovement::Update` (`5.202` веса) является ежетиковый VMap raycast `ResolveGroundZ` (`3.032` веса, `58.3%` от `BotMovement::Update`).
- **База данных (MySQL 8.4) и память**:
  - Загрузка CPU процессом `mysqld`: `0.008%` (`~0` очередей задержки).
  - Память `worldserver.exe` (`RSS`): `2,812.6 MB` в начале окна измерения → `2,848.1 MB` в конце 900 с (`+1.26%`), `mysqld.exe`: `516.7 MB`.
- **Полезная игровая активность за 900 с (0.25 часа × 3 000 ботов = 750 бот-часов)**:
  - Частота вызовов `BotAI::Update`: `8,658.35 calls/s` (`2.89 Hz` в среднем на одного бота).
  - Сражения: `~704` бота одновременно в бою (`23.5%` популяции), `~1,442` бота в активном перемещении (`48.1%`).
  - Ремонты/переодевания при повышении уровня и износе: `125` полных циклов `RepairGear` (`166.7` на 1000 бот-часов).
  - Динамические мировые энкаунтеры: `180` циклов оценки `WorldEncounterDirector` (каждые 5 с).
