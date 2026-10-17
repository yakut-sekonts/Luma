# Luma — UX / E2E / Stability Autotest Specification

## 0. Назначение

Этот документ — техническое задание для Codex на создание отдельного слоя UX/E2E/Stress автотестов Luma.

Цель слоя — проверять не только то, что функции формально работают, но и то, что приложение ощущается комфортным в реальной длительной работе фотографа:

- интерфейс быстро реагирует на клики, клавиши, drag и scroll;
- скроллы не дёргаются, не прыгают и не теряют позицию;
- клики и double-click не теряются и не выполняются дважды;
- тяжёлые операции не замораживают main UI;
- Cancel действительно отменяет операцию и быстро даёт обратную связь;
- Undo/Redo всегда возвращает ожидаемое состояние;
- асинхронные preview/thumbnail не подменяют новый state устаревшим результатом;
- переключение фотографии, режима и языка не смешивает состояния;
- закрытие приложения во время save/import/export/batch безопасно;
- длительная работа не приводит к утечкам памяти или деградации отзывчивости;
- пользовательские ошибки и ошибки файловой системы не оставляют приложение в полуживом состоянии;
- после добавления Local Adjustments 0.5 маски проверяются теми же стандартами качества.

Репозиторий: `https://github.com/yakut-sekonts/Luma`

На момент подготовки документа публичная `main` содержит релизный контур 0.4.13 и полный функционал фазы 0.4.x. Пользовательская рабочая ветка может быть уже 0.5.0. Codex обязан определять фактическую версию и capabilities из текущего checkout и не хардкодить поведение по номеру версии.

---

# 1. Главный принцип

Codex должен сначала создать воспроизводимый тест, затем исправлять найденную проблему.

Запрещено молча менять production-код во время написания теста, если дефект ещё не воспроизводится тестом.

Для каждого найденного дефекта автотест обязан фиксировать:

1. сценарий;
2. последовательность действий;
3. expected behavior;
4. actual behavior;
5. severity;
6. измеримые метрики;
7. screenshot/log/trace;
8. затронутый subsystem;
9. reproducible seed для случайных сценариев;
10. regression test, который остаётся после исправления.

Если функция ещё отсутствует, но входит в UX зрелого приложения, использовать статус `NOT_IMPLEMENTED`, а не FAIL.

---

# 2. Scope tags

Каждый тест получает scope:

- `CURRENT` — функция уже существует и должна работать сейчас;
- `0.5` — включать, когда в checkout присутствуют Local Adjustments;
- `TARGET` — желаемый UX полноценного зрелого приложения; отсутствие формирует roadmap gap;
- `ALL` — применимо независимо от версии.

Каждый тест получает execution mode:

- `BLOCKING` — release gate;
- `REGRESSION` — обычный CI regression;
- `STRESS` — длительный/нагрузочный;
- `PERF_LOCAL` — измерять на реальном Apple Silicon, не использовать hosted CI как эталон производительности;
- `CHAOS` — случайная последовательность действий с воспроизводимым seed.

---

# 3. Severity

## P0 — Critical

Release запрещён.

Примеры:

- изменён/повреждён оригинальный файл;
- повреждён каталог;
- потеря сохранённых edits;
- crash в основном workflow;
- hard hang > 5 секунд без живого UI/cancel path;
- экспорт заменяет оригинал;
- Undo применяет данные другой фотографии;
- stale async response подменяет preview/metadata текущего asset;
- гонка смешивает state разных asset.

## P1 — High

Release запрещён до исправления или явного исключения функции из scope.

Примеры:

- основной workflow нельзя закончить;
- UI блокируется > 2 секунд там, где операция должна быть фоновой;
- клики/drag регулярно теряются;
- Cancel не останавливает операцию;
- selection/active photo меняются неожиданно;
- Undo/Redo нарушает ожидаемый порядок;
- scroll/resize приводит к пропаданию элементов;
- minimum window size фактически нерабочий.

## P2 — Medium

Идёт в stabilization roadmap.

Примеры:

- jank;
- scroll jump;
- заметный лишний render;
- неправильный focus;
- clipping/overlap;
- transient visual corruption;
- превышение UX performance budgets;
- проблемы accessibility без блокировки core flow.

## P3 — Low / Polish

Идёт в polish backlog.

Примеры:

- spacing;
- слабая анимация;
- неидеальная визуальная обратная связь;
- мелкие affordance-проблемы.

---

# 4. Обязательная тестовая инфраструктура

Рекомендуемая структура:

```text
tests/ux/
    README.md
    UxTestIds.h
    UxDriver.cpp
    UxDriver.h
    UxMetrics.cpp
    UxMetrics.h
    UxWatchdog.cpp
    UxWatchdog.h
    fixtures/
    scenarios/
    stress/
    run_ux_suite.py
```

CTest labels:

```text
ux-smoke
ux-library
ux-develop
ux-input
ux-undo
ux-export
ux-recovery
ux-localization
ux-accessibility
ux-stress
ux-perf
ux-masks
ux-chaos
```

## 4.1. Stable UI test IDs

Все интерактивные QML-элементы должны иметь стабильный `objectName` или эквивалентный test ID.

Не использовать в качестве selector:

- локализованный visible text;
- абсолютные координаты;
- позицию внутри layout;
- текущий index, если объект имеет стабильную сущность.

Примеры:

```text
main.libraryButton
main.developButton
main.exportButton
main.settingsButton
library.search
library.grid
library.importButton
library.flagFilter
library.labelFilter
develop.adjustmentsScroll
develop.exposureSlider
develop.originalButton
develop.filmstrip
crop.frame
transform.autoButton
export.queuePause
```

Добавление test ID не должно менять production behavior.

## 4.2. In-process UI driver

Использовать Qt Test / QTest и настоящий `QQuickWindow`.

Driver должен поддерживать:

- click;
- double click;
- press/release;
- drag;
- wheel;
- pixel-scroll, близкий к trackpad;
- key press/release;
- shortcuts;
- focus traversal;
- resize;
- minimize/restore;
- deactivate/activate application;
- wait-until condition;
- screenshot.

Запрещено строить suite на `sleep(2)`.

Ожидания — condition-based с timeout и диагностикой причины timeout.

## 4.3. Event-loop watchdog

Во время каждого E2E теста должен работать heartbeat main loop.

Фиксировать:

- longest event-loop stall;
- count stall >100 ms;
- count stall >250 ms;
- count stall >1000 ms;
- hard hang.

Пороговая классификация:

```text
> 100 ms  = UX hitch
> 250 ms  = visible stall
> 1000 ms = severe stall
> 2000 ms = P1 UI hang
> 5000 ms = P0 hard hang
```

## 4.4. Frame/input metrics

Для scroll/drag/resize тестов собирать:

- frame interval p50;
- p95;
- p99;
- max;
- dropped/jank frames;
- gesture duration;
- input-to-visual-ack latency;
- input-to-preview latency, где применимо.

GitHub Actions/offscreen использовать для functional regression.

Performance gate — только на реальном Mac с зафиксированным hardware fingerprint.

## 4.5. Артефакты failures

На FAIL/P0/P1 сохранять:

```text
build/ux-artifacts/<run-id>/<test-id>/
    result.json
    before.png
    after.png
    failure.png
    app.log
    metrics.json
    actions.jsonl
    catalog-copy.sqlite
```

Для CHAOS дополнительно:

```text
seed.txt
replay.jsonl
```

Каталог-копию сохранять только для synthetic/public fixtures.

## 4.6. Machine-readable result

Пример:

```json
{
  "id": "UX-LIB-001",
  "status": "PASS",
  "scope": "CURRENT",
  "severity_if_failed": "P1",
  "duration_ms": 832,
  "input_ack_p95_ms": 44,
  "main_loop_stall_max_ms": 73,
  "frame_p95_ms": 18,
  "rss_peak_mb": 612,
  "notes": []
}
```

Statuses:

```text
PASS
FAIL
NOT_IMPLEMENTED
SKIPPED
FLAKY
MEASURED
```

---

# 5. UX performance budgets

Сначала допускается режим `MEASURE_ONLY`, чтобы получить реальный baseline.

## Input

- click acknowledgment p95 <= 100 ms;
- hover state p95 <= 100 ms;
- shortcut acknowledgment p95 <= 100 ms;
- drag control visual follow p95 <= 50 ms;
- Cancel acknowledgment <= 250 ms.

## Navigation

- Library ↔ Develop без нового decode: p95 <= 250 ms;
- open modal/panel: p95 <= 150 ms;
- close modal/panel: p95 <= 150 ms;
- cached photo switch target <= 200 ms.

## Rendering

- interactive slider input никогда не блокирует pointer;
- first preview feedback current-CPU target <= 150 ms;
- post-performance-phase target <= 50 ms;
- initial 20–30 MP RAW preview target <= 1.5 s на reference Mac.

## Scrolling

После прогрева:

- target p95 frame interval <= 33 ms;
- no main-thread stall >250 ms;
- no scroll offset jumps;
- no blank viewport дольше одного кадра.

## Startup

Target:

- warm <= 2 s;
- cold <= 5 s.

---

# 6. Fixture matrix

Создать воспроизводимые public fixtures:

- empty catalog;
- 1 image;
- 10 images;
- 100 images;
- 1 000 images;
- 10 000 metadata-only rows;
- 100 000 metadata fixture;
- mixed RAW + raster formats;
- long filenames;
- Cyrillic filenames;
- emoji filenames;
- deep/long nested path;
- portrait/landscape/square;
- alpha PNG/TIFF;
- generated Bayer DNG;
- corrupted image;
- truncated image;
- unsupported RAW;
- source removed after import;
- read-only source;
- read-only destination;
- simulated low-disk-space condition where practical.

Private Nikon D7500 acceptance использовать только при наличии private fixture path. Он не должен быть обязательным для public CI.

---

# 7. UX test cases

Формат:

`ID [severity][scope][mode] — Scenario → Pass criteria`

---

## A. Startup, lifecycle, window

- `UX-APP-001 [P0][ALL][BLOCKING]` — 50 последовательных запусков и штатных закрытий → 0 crash, каталог каждый раз доступен.
- `UX-APP-002 [P1][CURRENT][BLOCKING]` — запуск с empty catalog → окно появляется, нет зависшего busy state.
- `UX-APP-003 [P1][CURRENT][REGRESSION]` — запуск с последней активной фотографией → active photo восстанавливается без flash чужого preview.
- `UX-APP-004 [P1][CURRENT][REGRESSION]` — последний source отсутствует → приложение открывается и сообщает проблему, не зависает.
- `UX-APP-005 [P1][ALL][STRESS]` — быстро открыть/закрыть приложение 20 раз → нет locked SQLite/zombie process.
- `UX-APP-006 [P1][CURRENT][BLOCKING]` — Cmd+Q в idle → штатное закрытие без лишнего confirmation.
- `UX-APP-007 [P0][CURRENT][BLOCKING]` — Cmd+Q во время autosave → safe wait; edits после restart сохранены.
- `UX-APP-008 [P1][CURRENT][BLOCKING]` — закрытие во время import → UI показывает ожидаемое wait/cancel state, no hard hang.
- `UX-APP-009 [P1][CURRENT][BLOCKING]` — закрытие во время export queue → queue корректно завершается/отменяется.
- `UX-APP-010 [P1][CURRENT][BLOCKING]` — закрытие во время batch → atomic commit/rollback.
- `UX-APP-011 [P1][CURRENT][REGRESSION]` — close одновременно при save + export → no deadlock между контроллерами.
- `UX-APP-012 [P2][ALL][REGRESSION]` — resize min→fullscreen→min 50 раз → layout не разваливается.
- `UX-APP-013 [P1][CURRENT][BLOCKING]` — 1000×640 → все критические действия доступны.
- `UX-APP-014 [P2][CURRENT][REGRESSION]` — ширины 899/900/999/1000/1199/1200/1450 → no layout oscillation.
- `UX-APP-015 [P2][ALL][REGRESSION]` — fullscreen enter/exit 50 раз → валидные scroll positions.
- `UX-APP-016 [P1][ALL][REGRESSION]` — minimize во время background op и restore → операция продолжает жить.
- `UX-APP-017 [P1][ALL][REGRESSION]` — deactivate app mid-drag → gesture завершается или отменяется безопасно.
- `UX-APP-018 [P2][ALL][PERF_LOCAL]` — cold/warm startup p50/p95/p99.
- `UX-APP-019 [P1][ALL][STRESS]` — 2 часа idle с открытым Develop → нет неограниченного RSS growth.
- `UX-APP-020 [P1][ALL][CHAOS]` — 5 000 случайных resize/mode/dialog действий → no crash/hang.

---

## B. Main navigation / top bar

- `UX-NAV-001 [P1][CURRENT][BLOCKING]` — Library→Develop→Library 100 раз → нет blank/stuck states.
- `UX-NAV-002 [P2][CURRENT][PERF_LOCAL]` — p95 Library↔Develop с loaded photo.
- `UX-NAV-003 [P1][CURRENT][REGRESSION]` — Develop без active photo → консистентный empty-state.
- `UX-NAV-004 [P1][CURRENT][REGRESSION]` — Open Photo cancel → 0 side effects.
- `UX-NAV-005 [P1][CURRENT][REGRESSION]` — drag-and-drop supported file → открывается ровно один asset.
- `UX-NAV-006 [P1][CURRENT][REGRESSION]` — drag unsupported file → понятная ошибка, no crash.
- `UX-NAV-007 [P2][CURRENT][REGRESSION]` — filename 255 chars в top bar → elide, controls не вытесняются.
- `UX-NAV-008 [P1][CURRENT][REGRESSION]` — Undo/Redo enabled state соответствует фактическим stacks.
- `UX-NAV-009 [P1][CURRENT][REGRESSION]` — shortcut и toolbar Undo/Redo дают идентичный state.
- `UX-NAV-010 [P2][CURRENT][REGRESSION]` — Export availability корректна при 0/1/N selection.
- `UX-NAV-011 [P1][CURRENT][REGRESSION]` — Settings open/close 50 раз → no popup/focus leak.
- `UX-NAV-012 [P2][CURRENT][REGRESSION]` — UpdateBell open/close while editing → edit state unaffected.
- `UX-NAV-013 [P1][CURRENT][REGRESSION]` — Original shortcut во время busy не меняет скрытый state.
- `UX-NAV-014 [P1][CURRENT][REGRESSION]` — 200 быстрых top-bar clicks → no duplicate modal/action.
- `UX-NAV-015 [P2][ALL][REGRESSION]` — status line updates не меняют высоту layout.

---

## C. Library grid scrolling / virtualization

- `UX-LIB-001 [P1][CURRENT][BLOCKING]` — scroll 1 000-photo grid top→bottom→top → no crash/permanent blank cells.
- `UX-LIB-002 [P2][CURRENT][PERF_LOCAL]` — warmed wheel scroll 30 s → frame/stall metrics в budget.
- `UX-LIB-003 [P2][CURRENT][PERF_LOCAL]` — trackpad-like pixel scroll с momentum → no jump назад.
- `UX-LIB-004 [P1][CURRENT][REGRESSION]` — fast scroll пока thumbnails грузятся → late thumbnail попадает только в свою card.
- `UX-LIB-005 [P1][CURRENT][REGRESSION]` — scroll away before thumbnail completion, затем back → stale response не подменяет новый.
- `UX-LIB-006 [P1][CURRENT][REGRESSION]` — thumbnail error → retry bounded, grid responsive.
- `UX-LIB-007 [P2][CURRENT][REGRESSION]` — recycled delegate не наследует retryCount другого asset.
- `UX-LIB-008 [P2][CURRENT][REGRESSION]` — resize 4↔5↔4 columns → viewport остаётся около того же logical asset.
- `UX-LIB-009 [P2][CURRENT][REGRESSION]` — resize вокруг cell breakpoint 50 раз → no relayout loop/flicker.
- `UX-LIB-010 [P1][CURRENT][BLOCKING]` — selected asset после дальнего scroll остаётся selected.
- `UX-LIB-011 [P1][CURRENT][BLOCKING]` — active border отображается только у active asset.
- `UX-LIB-012 [P2][CURRENT][REGRESSION]` — 100k metadata fixture → UI не создаёт 100k QML delegates.
- `UX-LIB-013 [P2][CURRENT][STRESS]` — 10 минут scroll → RSS/object count reaches plateau.
- `UX-LIB-014 [P1][CURRENT][REGRESSION]` — filter change during fast scroll → no out-of-range access.
- `UX-LIB-015 [P1][CURRENT][REGRESSION]` — clear filter after filtered scroll → valid model/scroll state.
- `UX-LIB-016 [P2][CURRENT][REGRESSION]` — empty filtered result → no phantom scrollbar range.
- `UX-LIB-017 [P1][CURRENT][REGRESSION]` — import adds rows while scrolling → scroll не сбрасывается в начало.
- `UX-LIB-018 [P1][CURRENT][REGRESSION]` — edited thumbnail invalidated offscreen → при возвращении новый saved edit.
- `UX-LIB-019 [P2][CURRENT][PERF_LOCAL]` — thumbnail invalidation не вызывает full model reset/jank.
- `UX-LIB-020 [P1][CURRENT][CHAOS]` — 10 000 random scroll/select/filter actions → invariants model/selection.

---

## D. Click, double-click, selection

- `UX-SEL-001 [P1][CURRENT][BLOCKING]` — single click card → select, не open Develop.
- `UX-SEL-002 [P1][CURRENT][BLOCKING]` — double-click card → ровно один activate/open.
- `UX-SEL-003 [P1][CURRENT][REGRESSION]` — double-click не создаёт неправильный intermediate selection.
- `UX-SEL-004 [P1][CURRENT][BLOCKING]` — Cmd-click toggle selection.
- `UX-SEL-005 [P1][CURRENT][BLOCKING]` — Shift-click contiguous range.
- `UX-SEL-006 [P1][CURRENT][REGRESSION]` — Shift-click после filter change использует валидный anchor.
- `UX-SEL-007 [P1][CURRENT][REGRESSION]` — Cmd+A выбирает filtered set, а не hidden rows.
- `UX-SEL-008 [P1][CURRENT][REGRESSION]` — Clear Selection не меняет active photo без причины.
- `UX-SEL-009 [P1][CURRENT][REGRESSION]` — rating 0–5 применяется к selection ровно один раз.
- `UX-SEL-010 [P1][CURRENT][REGRESSION]` — P/X/U работают только вне editable field.
- `UX-SEL-011 [P1][CURRENT][REGRESSION]` — typing P/X/U/1 в search field не меняет metadata.
- `UX-SEL-012 [P1][CURRENT][REGRESSION]` — label update не сбрасывает selection.
- `UX-SEL-013 [P1][CURRENT][REGRESSION]` — rating/flag update не сбрасывает scroll.
- `UX-SEL-014 [P1][CURRENT][REGRESSION]` — Add to collection затрагивает только selected assets.
- `UX-SEL-015 [P1][CURRENT][REGRESSION]` — Remove from collection не удаляет asset/edit.
- `UX-SEL-016 [P2][CURRENT][REGRESSION]` — 100 быстрых кликов по разным cards → final selection соответствует последнему input.
- `UX-SEL-017 [P1][CURRENT][REGRESSION]` — filmstrip single click активирует один раз.
- `UX-SEL-018 [P1][CURRENT][REGRESSION]` — modified filmstrip click не делает нежелательный activate.
- `UX-SEL-019 [P2][CURRENT][REGRESSION]` — selected/active border не остаётся на recycled card.
- `UX-SEL-020 [P1][CURRENT][CHAOS]` — random click/doubleclick/modifier sequences replayable by seed.

---

## E. Search / filters / collections

- `UX-FLT-001 [P1][CURRENT][BLOCKING]` — filename search realtime → correct count, no input lag.
- `UX-FLT-002 [P2][CURRENT][PERF_LOCAL]` — 100k fixture, 20 typed chars → per-keystroke latency.
- `UX-FLT-003 [P1][CURRENT][REGRESSION]` — rapid backspace to empty → full result restored.
- `UX-FLT-004 [P1][CURRENT][REGRESSION]` — Cyrillic search.
- `UX-FLT-005 [P1][CURRENT][REGRESSION]` — Unicode/emoji path search.
- `UX-FLT-006 [P1][CURRENT][REGRESSION]` — 1024-char search input → no hang/overflow.
- `UX-FLT-007 [P1][CURRENT][REGRESSION]` — combined search+rating+flag+label+collection.
- `UX-FLT-008 [P1][CURRENT][REGRESSION]` — 200 rapid filter changes → final model matches UI controls.
- `UX-FLT-009 [P2][CURRENT][REGRESSION]` — filter changes не steal focus из search.
- `UX-FLT-010 [P1][CURRENT][BLOCKING]` — create/rename collection + restart → persists.
- `UX-FLT-011 [P1][CURRENT][REGRESSION]` — duplicate/empty collection names deterministic UX.
- `UX-FLT-012 [P1][CURRENT][REGRESSION]` — Delete Collection Cancel → 0 state changes.
- `UX-FLT-013 [P1][CURRENT][BLOCKING]` — Delete Collection confirmed → assets/edits remain.
- `UX-FLT-014 [P2][CURRENT][REGRESSION]` — 500 collections scroll remains responsive.
- `UX-FLT-015 [P2][CURRENT][REGRESSION]` — long collection names не перекрывают controls.

---

## F. Import UX / filesystem failures

- `UX-IMP-001 [P1][CURRENT][BLOCKING]` — open Import then Cancel → 0 side effects.
- `UX-IMP-002 [P1][CURRENT][BLOCKING]` — Add 100 supported files → progress visible, UI responsive.
- `UX-IMP-003 [P1][CURRENT][BLOCKING]` — Copy 100 files → source hashes unchanged.
- `UX-IMP-004 [P1][CURRENT][BLOCKING]` — cancel at ~10/50/90% → valid checkpoints, no half rows.
- `UX-IMP-005 [P1][CURRENT][REGRESSION]` — repeated Cancel is idempotent.
- `UX-IMP-006 [P1][CURRENT][REGRESSION]` — second import while first active → safely blocked/queued.
- `UX-IMP-007 [P1][CURRENT][REGRESSION]` — duplicates skip → messaging/counts understandable.
- `UX-IMP-008 [P1][CURRENT][REGRESSION]` — duplicates allowed → deterministic no overwrite.
- `UX-IMP-009 [P1][CURRENT][REGRESSION]` — recursive deep folder tree.
- `UX-IMP-010 [P1][CURRENT][REGRESSION]` — inaccessible subfolder → valid files continue; report contains failed path.
- `UX-IMP-011 [P1][CURRENT][REGRESSION]` — source disappears mid-import → no hang.
- `UX-IMP-012 [P1][CURRENT][REGRESSION]` — destination becomes read-only mid-Copy → clean per-file rollback/error.
- `UX-IMP-013 [P1][CURRENT][REGRESSION]` — corrupt image mixed with valid → valid continue.
- `UX-IMP-014 [P1][CURRENT][REGRESSION]` — unsupported file selected through All files → readable failure.
- `UX-IMP-015 [P2][CURRENT][REGRESSION]` — long path/error → dialog remains usable.
- `UX-IMP-016 [P1][CURRENT][REGRESSION]` — Import errors report is scrollable/selectable.
- `UX-IMP-017 [P1][CURRENT][REGRESSION]` — close import dialog while native FileDialog open → no orphan modal.
- `UX-IMP-018 [P2][CURRENT][PERF_LOCAL]` — import 1 000 files → main-loop max stall + cancel latency.
- `UX-IMP-019 [P1][CURRENT][STRESS]` — import/cancel/reimport same fixture 50 cycles → no journal leak.
- `UX-IMP-020 [P0][CURRENT][BLOCKING]` — force terminate during Copy then restart recovery → catalog/source intact.

---

## G. Adjustments ScrollView

- `UX-DSC-001 [P1][CURRENT][BLOCKING]` — scroll top→bottom→top → every panel reachable.
- `UX-DSC-002 [P2][CURRENT][PERF_LOCAL]` — continuous fast scroll 30 s → jank/stalls measured.
- `UX-DSC-003 [P2][CURRENT][REGRESSION]` — scroll while pointer crosses Slider → page scroll не должен случайно менять value.
- `UX-DSC-004 [P1][CURRENT][REGRESSION]` — drag slider then wheel → only intended input changes value.
- `UX-DSC-005 [P2][CURRENT][REGRESSION]` — dynamic panel height change while deep-scrolled → no large unexpected jump.
- `UX-DSC-006 [P1][CURRENT][REGRESSION]` — photo change while deep-scrolled → controls immediately show new photo state.
- `UX-DSC-007 [P2][CURRENT][REGRESSION]` — right panel 250↔400 px while scrolled → no clipped controls.
- `UX-DSC-008 [P2][CURRENT][REGRESSION]` — language switch while deep-scrolled → valid relayout/scroll range.
- `UX-DSC-009 [P1][CURRENT][REGRESSION]` — keyboard-focused control remains reachable/visible.
- `UX-DSC-010 [P1][CURRENT][STRESS]` — 5 000 scroll events + 1 000 panel interactions → no hang/object leak.

---

## H. Develop sliders / input gestures

- `UX-SLD-001 [P1][CURRENT][BLOCKING]` — Exposure min→max→0 drag → thumb follows pointer; final value exact.
- `UX-SLD-002 [P2][CURRENT][PERF_LOCAL]` — pointer→thumb and pointer→preview p95.
- `UX-SLD-003 [P1][CURRENT][BLOCKING]` — continuous drag = one Undo unit.
- `UX-SLD-004 [P1][CURRENT][BLOCKING]` — Undo after drag restores exact prior parameter/render state.
- `UX-SLD-005 [P1][CURRENT][REGRESSION]` — double click resets default once.
- `UX-SLD-006 [P1][CURRENT][REGRESSION]` — double click does not leave dangling beginEdit/endEdit.
- `UX-SLD-007 [P1][CURRENT][REGRESSION]` — disable correction retains parameter but removes effect.
- `UX-SLD-008 [P1][CURRENT][REGRESSION]` — re-enable restores retained effect.
- `UX-SLD-009 [P1][CURRENT][REGRESSION]` — app deactivation mid-drag safely finalizes/cancels gesture.
- `UX-SLD-010 [P1][CURRENT][REGRESSION]` — photo switch mid-drag → no edit leaks to next asset.
- `UX-SLD-011 [P1][CURRENT][REGRESSION]` — Library switch mid-drag → one valid finalization.
- `UX-SLD-012 [P1][CURRENT][REGRESSION]` — drag during autosave → latest value not lost.
- `UX-SLD-013 [P1][CURRENT][STRESS]` — 1 000 rapid alternating Exposure values → persisted final = last input.
- `UX-SLD-014 [P2][CURRENT][STRESS]` — storm across all Basic controls → no multi-second stale render backlog.
- `UX-SLD-015 [P1][CURRENT][REGRESSION]` — WB As Shot/Custom semantics consistent with enabled controls.
- `UX-SLD-016 [P1][CURRENT][REGRESSION]` — Custom WB survives switch away/back and restart.
- `UX-SLD-017 [P2][CURRENT][REGRESSION]` — numeric labels never NaN/Inf/-0.00.
- `UX-SLD-018 [P1][CURRENT][REGRESSION]` — min/max clamp predictable.
- `UX-SLD-019 [P1][CURRENT][REGRESSION]` — Reset All does not touch selection/metadata.
- `UX-SLD-020 [P0][CURRENT][BLOCKING]` — edit 20 controls, immediate quit/restart → internally consistent saved state.

---

## I. Preview / Original / photo switching

- `UX-PRV-001 [P1][CURRENT][BLOCKING]` — Develop↔Original 100 toggles → no stale image.
- `UX-PRV-002 [P2][CURRENT][PERF_LOCAL]` — Original toggle p95.
- `UX-PRV-003 [P1][CURRENT][REGRESSION]` — Original mode не мутирует edits/history.
- `UX-PRV-004 [P1][CURRENT][REGRESSION]` — preset hover → Original → leave → restore exact pre-hover state.
- `UX-PRV-005 [P1][CURRENT][REGRESSION]` — switch photo while old decode active → late old result never replaces new.
- `UX-PRV-006 [P1][CURRENT][BLOCKING]` — Cancel decode → stable previous/empty state.
- `UX-PRV-007 [P1][CURRENT][REGRESSION]` — cancel/load-completion race → no crash.
- `UX-PRV-008 [P1][CURRENT][STRESS]` — rapidly activate 100 filmstrip photos → final preview/metadata = final asset.
- `UX-PRV-009 [P2][CURRENT][PERF_LOCAL]` — cached switch p50/p95.
- `UX-PRV-010 [P2][TARGET][REGRESSION]` — true 1:1 zoom → absent = NOT_IMPLEMENTED roadmap gap.
- `UX-PRV-011 [P2][TARGET][REGRESSION]` — arbitrary zoom/pan with correct overlay coordinates.
- `UX-PRV-012 [P2][TARGET][REGRESSION]` — double-click Fit↔1:1.
- `UX-PRV-013 [P0][ALL][BLOCKING]` — preview asset A + metadata asset B никогда не допускается.
- `UX-PRV-014 [P2][ALL][REGRESSION]` — no white flash during dark-theme rerender.
- `UX-PRV-015 [P1][ALL][STRESS]` — 30 min photo switching+edits → no stale render backlog.

---

## J. Curves / HSL / targeted tools

- `UX-ADV-001 [P1][CURRENT][BLOCKING]` — add/move/delete curve point → pointer capture valid; one gesture=one Undo.
- `UX-ADV-002 [P1][CURRENT][REGRESSION]` — curve point dragged beyond bounds → safe clamp.
- `UX-ADV-003 [P1][CURRENT][REGRESSION]` — rapid RGB/R/G/B switch during drag → no stale binding/crash.
- `UX-ADV-004 [P1][CURRENT][REGRESSION]` — parametric curve drag/Undo/Redo.
- `UX-ADV-005 [P1][CURRENT][REGRESSION]` — HSL band switch during rapid slider use.
- `UX-ADV-006 [P1][CURRENT][BLOCKING]` — targeted tone click+vertical drag → correct group changes.
- `UX-ADV-007 [P1][CURRENT][BLOCKING]` — targeted HSL click+drag.
- `UX-ADV-008 [P1][CURRENT][REGRESSION]` — targeted click outside painted image → no edit.
- `UX-ADV-009 [P1][CURRENT][REGRESSION]` — photo switch cancels targeted gesture.
- `UX-ADV-010 [P1][CURRENT][REGRESSION]` — targeted tool never competes with Crop/Guided pointer ownership.
- `UX-ADV-011 [P1][CURRENT][REGRESSION]` — Original disables targeted mutation.
- `UX-ADV-012 [P2][CURRENT][PERF_LOCAL]` — targeted drag latency.
- `UX-ADV-013 [P1][CURRENT][STRESS]` — 500 Curve/HSL edits → Undo all → Redo all; state hash identical.
- `UX-ADV-014 [P1][CURRENT][REGRESSION]` — photo switch clears transient selected curve point state.
- `UX-ADV-015 [P1][CURRENT][REGRESSION]` — export immediately after targeted edit uses final saved state.

---

## K. Crop

- `UX-CRP-001 [P1][CURRENT][BLOCKING]` — drag each crop grip.
- `UX-CRP-002 [P1][CURRENT][BLOCKING]` — move crop frame.
- `UX-CRP-003 [P1][CURRENT][REGRESSION]` — no zero/negative-size crop.
- `UX-CRP-004 [P1][CURRENT][REGRESSION]` — Free↔Original↔1:1↔4:3↔3:2↔16:9.
- `UX-CRP-005 [P1][CURRENT][REGRESSION]` — custom ratios 1:100 and 100:1 remain valid/manipulable.
- `UX-CRP-006 [P1][CURRENT][REGRESSION]` — swap ratio orientation.
- `UX-CRP-007 [P1][CURRENT][REGRESSION]` — quarter-turn while Crop active.
- `UX-CRP-008 [P1][CURRENT][REGRESSION]` — horizontal/vertical flip with crop.
- `UX-CRP-009 [P1][CURRENT][REGRESSION]` — Straighten drag = one Undo.
- `UX-CRP-010 [P1][CURRENT][REGRESSION]` — Done/Cancel leaves no transient overlay residue.
- `UX-CRP-011 [P1][CURRENT][REGRESSION]` — Original while Crop active has defined non-destructive behavior.
- `UX-CRP-012 [P1][CURRENT][REGRESSION]` — photo switch exits transient crop interaction.
- `UX-CRP-013 [P1][CURRENT][REGRESSION]` — preset hover while Crop active does not corrupt crop mode.
- `UX-CRP-014 [P1][CURRENT][REGRESSION]` — guide overlay changes display only, no history/autosave.
- `UX-CRP-015 [P2][CURRENT][PERF_LOCAL]` — crop drag frame pacing.
- `UX-CRP-016 [P1][CURRENT][STRESS]` — 1 000 random valid crop drags with geometry invariants.
- `UX-CRP-017 [P1][CURRENT][REGRESSION]` — export immediately after Done uses final geometry.
- `UX-CRP-018 [P0][CURRENT][BLOCKING]` — restart reproduces crop geometry exactly.

---

## L. Transform / Perspective

- `UX-TRN-001 [P1][CURRENT][BLOCKING]` — each manual Transform control drag/Undo.
- `UX-TRN-002 [P1][CURRENT][REGRESSION]` — extreme valid values remain finite.
- `UX-TRN-003 [P1][CURRENT][BLOCKING]` — Auto runs async; UI responsive; Cancel available.
- `UX-TRN-004 [P1][CURRENT][REGRESSION]` — Auto cancellation race near completion.
- `UX-TRN-005 [P1][CURRENT][REGRESSION]` — uncertain Auto refusal → no edit/history entry.
- `UX-TRN-006 [P1][CURRENT][BLOCKING]` — Guided vertical pair interaction.
- `UX-TRN-007 [P1][CURRENT][BLOCKING]` — Guided horizontal pair interaction.
- `UX-TRN-008 [P1][CURRENT][REGRESSION]` — invalid/degenerate guides → safe refusal.
- `UX-TRN-009 [P1][CURRENT][REGRESSION]` — leave Guided mid-drag → transient endpoints cleared.
- `UX-TRN-010 [P1][CURRENT][REGRESSION]` — Crop and Guided cannot own pointer simultaneously.
- `UX-TRN-011 [P1][CURRENT][REGRESSION]` — Constrain Crop toggle no stale geometry.
- `UX-TRN-012 [P1][CURRENT][REGRESSION]` — photo switch cancels Auto/Guided transient state.
- `UX-TRN-013 [P2][CURRENT][PERF_LOCAL]` — Auto causes no main-loop stall >250 ms.
- `UX-TRN-014 [P1][CURRENT][STRESS]` — Auto/cancel 100 cycles.
- `UX-TRN-015 [P0][CURRENT][BLOCKING]` — restart reproduces Transform state.

---

## M. Lens corrections

- `UX-LNS-001 [P1][CURRENT][BLOCKING]` — known lens auto profile displayed/applied safely.
- `UX-LNS-002 [P1][CURRENT][REGRESSION]` — unknown lens never silently maps to arbitrary profile.
- `UX-LNS-003 [P1][CURRENT][REGRESSION]` — manual profile search typing responsive.
- `UX-LNS-004 [P2][CURRENT][PERF_LOCAL]` — full profile DB search latency.
- `UX-LNS-005 [P1][CURRENT][REGRESSION]` — rapid profile switching → final choice wins.
- `UX-LNS-006 [P1][CURRENT][REGRESSION]` — distortion/vignetting gesture = one Undo.
- `UX-LNS-007 [P1][CURRENT][REGRESSION]` — CA toggle no stale preview.
- `UX-LNS-008 [P1][CURRENT][REGRESSION]` — disable Optics retains values but removes effect.
- `UX-LNS-009 [P1][CURRENT][REGRESSION]` — optics subset in Copy/Paste/Preset independent.
- `UX-LNS-010 [P1][CURRENT][BLOCKING]` — profile selection persists restart.

---

## N. Detail / diagnostic views

- `UX-DTL-001 [P1][CURRENT][BLOCKING]` — sharpening sliders normal drag.
- `UX-DTL-002 [P1][CURRENT][REGRESSION]` — Option-drag shows expected diagnostic.
- `UX-DTL-003 [P1][CURRENT][BLOCKING]` — release Option restores normal preview.
- `UX-DTL-004 [P1][CURRENT][REGRESSION]` — app deactivation while Option held restores normal state.
- `UX-DTL-005 [P1][CURRENT][REGRESSION]` — photo switch clears diagnostic state.
- `UX-DTL-006 [P1][CURRENT][REGRESSION]` — export while diagnostic visible exports normal render, not diagnostic.
- `UX-DTL-007 [P1][CURRENT][REGRESSION]` — preset hover while diagnostic active does not persist diagnostic.
- `UX-DTL-008 [P1][CURRENT][REGRESSION]` — luminance NR enable/disable retains values.
- `UX-DTL-009 [P1][CURRENT][REGRESSION]` — color NR enable/disable retains values.
- `UX-DTL-010 [P2][CURRENT][PERF_LOCAL]` — expensive NR slider storm remains controllable.
- `UX-DTL-011 [P1][CURRENT][STRESS]` — 200 NR/sharpen edits then immediate export.
- `UX-DTL-012 [P0][CURRENT][BLOCKING]` — restart reproduces Detail state.

---

## O. Presets / ready-made filters

- `UX-PRS-001 [P1][CURRENT][BLOCKING]` — create preset with subset.
- `UX-PRS-002 [P1][CURRENT][REGRESSION]` — rename/edit/delete user preset.
- `UX-PRS-003 [P1][CURRENT][REGRESSION]` — built-in preset cannot be renamed/deleted.
- `UX-PRS-004 [P1][CURRENT][BLOCKING]` — hover preview does not save edits/history.
- `UX-PRS-005 [P1][CURRENT][BLOCKING]` — pointer leave restores exact pre-hover render.
- `UX-PRS-006 [P1][CURRENT][REGRESSION]` — hover A→B→C rapidly → stale A/B never replaces C.
- `UX-PRS-007 [P1][CURRENT][REGRESSION]` — click preset after hover = one Undo.
- `UX-PRS-008 [P1][CURRENT][REGRESSION]` — Undo preset restores complete prior subset.
- `UX-PRS-009 [P1][CURRENT][REGRESSION]` — JSON export/import roundtrip.
- `UX-PRS-010 [P1][CURRENT][REGRESSION]` — invalid JSON → actionable error, existing presets intact.
- `UX-PRS-011 [P1][CURRENT][REGRESSION]` — corrupt preset store on startup not silently overwritten.
- `UX-PRS-012 [P2][CURRENT][REGRESSION]` — 512 presets panel remains scrollable/responsive.
- `UX-PRS-013 [P2][CURRENT][PERF_LOCAL]` — rapid hover 100 presets → latency/stalls measured.
- `UX-PRS-014 [P1][CURRENT][REGRESSION]` — built-in filter repeated application idempotent.
- `UX-PRS-015 [P1][CURRENT][REGRESSION]` — Natural affects only declared groups.
- `UX-PRS-016 [P1][CURRENT][REGRESSION]` — single-photo preset affects only expected asset.
- `UX-PRS-017 [P1][CURRENT][REGRESSION]` — Copy settings A → switch B → Paste correct subset.
- `UX-PRS-018 [P1][CURRENT][REGRESSION]` — restart Copy snapshot semantics match documented behavior.
- `UX-PRS-019 [P1][CURRENT][STRESS]` — 1 000 create/delete/rename operations → storage integrity.
- `UX-PRS-020 [P1][CURRENT][CHAOS]` — hover/click/Undo/photo-switch random sequence.

---

## P. Batch editing

- `UX-BAT-001 [P1][CURRENT][BLOCKING]` — batch preset on N selection → all N atomic.
- `UX-BAT-002 [P0][CURRENT][BLOCKING]` — injected failure mid-batch → rollback all.
- `UX-BAT-003 [P1][CURRENT][BLOCKING]` — Cancel batch → atomic rollback/defined cancel state.
- `UX-BAT-004 [P1][CURRENT][REGRESSION]` — repeated Cancel idempotent.
- `UX-BAT-005 [P1][CURRENT][BLOCKING]` — batch Undo restores all assets.
- `UX-BAT-006 [P1][CURRENT][BLOCKING]` — batch Redo reapplies all assets.
- `UX-BAT-007 [P1][CURRENT][REGRESSION]` — editor Undo vs batch Undo arbitration predictable.
- `UX-BAT-008 [P1][CURRENT][REGRESSION]` — conflicting later edit causes safe refusal, not corruption.
- `UX-BAT-009 [P1][CURRENT][REGRESSION]` — currently open affected asset refreshes correctly.
- `UX-BAT-010 [P1][CURRENT][REGRESSION]` — selection/scroll position preserved across batch commit.
- `UX-BAT-011 [P2][CURRENT][PERF_LOCAL]` — responsiveness for 1/100/1k assets.
- `UX-BAT-012 [P1][CURRENT][REGRESSION]` — close app mid-batch safe.
- `UX-BAT-013 [P1][CURRENT][REGRESSION]` — updater restart mid-batch waits safely.
- `UX-BAT-014 [P1][CURRENT][STRESS]` — 100 apply/undo cycles → no catalog growth anomaly.
- `UX-BAT-015 [P1][CURRENT][REGRESSION]` — no-op batch creates no misleading history operation.

---

## Q. Export panel / queue

- `UX-EXP-001 [P1][CURRENT][BLOCKING]` — open/close Export panel 50 times → no state leak.
- `UX-EXP-002 [P1][CURRENT][REGRESSION]` — invalid destination blocks Start with clear reason.
- `UX-EXP-003 [P1][CURRENT][REGRESSION]` — long filename template does not break dialog.
- `UX-EXP-004 [P1][CURRENT][BLOCKING]` — single export progress visible; UI responsive.
- `UX-EXP-005 [P1][CURRENT][BLOCKING]` — queue N photos → correct completed/total/current file.
- `UX-EXP-006 [P1][CURRENT][REGRESSION]` — Pause means pause after current file and text matches behavior.
- `UX-EXP-007 [P1][CURRENT][BLOCKING]` — Resume continues next job.
- `UX-EXP-008 [P1][CURRENT][BLOCKING]` — Cancel safely stops queue.
- `UX-EXP-009 [P1][CURRENT][REGRESSION]` — repeated Cancel idempotent.
- `UX-EXP-010 [P1][CURRENT][REGRESSION]` — one failed file does not stop later jobs.
- `UX-EXP-011 [P1][CURRENT][REGRESSION]` — failures list scrollable/readable.
- `UX-EXP-012 [P1][CURRENT][REGRESSION]` — Skip collision policy.
- `UX-EXP-013 [P1][CURRENT][REGRESSION]` — Unique collision policy.
- `UX-EXP-014 [P1][CURRENT][REGRESSION]` — Replace policy never resolves to original/catalog path.
- `UX-EXP-015 [P0][CURRENT][BLOCKING]` — export cannot overwrite original through symlink/alias/path normalization.
- `UX-EXP-016 [P1][CURRENT][REGRESSION]` — edit changes after enqueue do not alter immutable queued snapshot.
- `UX-EXP-017 [P1][CURRENT][REGRESSION]` — watermark changes after enqueue follow defined immutable semantics.
- `UX-EXP-018 [P1][CURRENT][REGRESSION]` — close app during queue → explicit wait/cancel flow.
- `UX-EXP-019 [P1][CURRENT][REGRESSION]` — autosave failure before export → Retry save path; no stale export.
- `UX-EXP-020 [P2][CURRENT][PERF_LOCAL]` — export under load → main-loop stall/input latency measured.
- `UX-EXP-021 [P1][CURRENT][REGRESSION]` — export + Library scroll simultaneously.
- `UX-EXP-022 [P1][CURRENT][REGRESSION]` — export + live Develop changes → queued snapshot unchanged.
- `UX-EXP-023 [P1][CURRENT][STRESS]` — queue 500 fixtures with random pause/resume.
- `UX-EXP-024 [P1][CURRENT][STRESS]` — 100 start/cancel cycles → no orphan temp files.
- `UX-EXP-025 [P1][CURRENT][REGRESSION]` — reusable settings persist according to design.
- `UX-EXP-026 [P2][CURRENT][REGRESSION]` — conflicting resize inputs show deterministic derived size.
- `UX-EXP-027 [P1][CURRENT][REGRESSION]` — metadata privacy toggles match resulting file.
- `UX-EXP-028 [P1][CURRENT][REGRESSION]` — watermark file missing at execution → per-file failure, queue survives.
- `UX-EXP-029 [P1][CURRENT][REGRESSION]` — Unicode destination/name/watermark.
- `UX-EXP-030 [P0][CURRENT][BLOCKING]` — original SHA unchanged for every export format.

---

## R. Undo / Redo / reset / persistence

- `UX-UND-001 [P1][CURRENT][BLOCKING]` — 50 heterogeneous edits → Undo all exact reverse order.
- `UX-UND-002 [P1][CURRENT][BLOCKING]` — Redo all recreates final document hash.
- `UX-UND-003 [P1][CURRENT][REGRESSION]` — new edit after Undo invalidates correct Redo branch.
- `UX-UND-004 [P1][CURRENT][REGRESSION]` — no-op does not pollute history.
- `UX-UND-005 [P1][CURRENT][REGRESSION]` — correction enable toggle Undo.
- `UX-UND-006 [P1][CURRENT][REGRESSION]` — Crop gesture one Undo.
- `UX-UND-007 [P1][CURRENT][REGRESSION]` — Guided/Auto Transform one Undo each.
- `UX-UND-008 [P1][CURRENT][REGRESSION]` — preset application one Undo.
- `UX-UND-009 [P1][CURRENT][REGRESSION]` — targeted adjustment one logical Undo.
- `UX-UND-010 [P1][CURRENT][REGRESSION]` — Reset All + Undo restores complete prior document.
- `UX-UND-011 [P0][CURRENT][BLOCKING]` — history never crosses asset boundary.
- `UX-UND-012 [P1][CURRENT][REGRESSION]` — A→B→A switch respects per-session history semantics.
- `UX-UND-013 [P1][CURRENT][REGRESSION]` — session-local history resets after restart as documented.
- `UX-UND-014 [P0][CURRENT][BLOCKING]` — force quit after completed autosave → saved edits survive.
- `UX-UND-015 [P1][CURRENT][REGRESSION]` — kill during pending debounce → last committed state intact, DB valid.
- `UX-UND-016 [P1][CURRENT][STRESS]` — history-cap boundary behavior deterministic.
- `UX-UND-017 [P1][CURRENT][CHAOS]` — random edits/undo/redo with document-hash oracle.
- `UX-UND-018 [P0][CURRENT][BLOCKING]` — original SHA invariant throughout suite.

---

## S. Autosave / errors / retry / recovery

- `UX-REC-001 [P1][CURRENT][BLOCKING]` — idle→saving→saved visual state transitions.
- `UX-REC-002 [P1][CURRENT][REGRESSION]` — injected SQLite busy → UI responsive, retry/backoff.
- `UX-REC-003 [P1][CURRENT][REGRESSION]` — injected save failure → visible error + Retry.
- `UX-REC-004 [P1][CURRENT][BLOCKING]` — Retry succeeds without duplicate history.
- `UX-REC-005 [P1][CURRENT][REGRESSION]` — rapid repeated Retry → single safe flow.
- `UX-REC-006 [P0][CURRENT][BLOCKING]` — disk-full simulation during save → catalog remains readable.
- `UX-REC-007 [P0][CURRENT][BLOCKING]` — kill during catalog write → SQLite recovery/no corruption.
- `UX-REC-008 [P1][CURRENT][REGRESSION]` — source removed while Develop open → committed edits preserved.
- `UX-REC-009 [P1][CURRENT][REGRESSION]` — corrupt thumbnail cache → regenerate, app remains usable.
- `UX-REC-010 [P1][CURRENT][STRESS]` — fault injection across save/import/export 100 cycles.
- `UX-REC-011 [P1][CURRENT][REGRESSION]` — long localized error banner remains actionable.
- `UX-REC-012 [P2][CURRENT][REGRESSION]` — stale error disappears after successful retry.

---

## T. Settings / localization / updates

- `UX-LOC-001 [P1][CURRENT][BLOCKING]` — runtime switch all available languages without restart.
- `UX-LOC-002 [P1][CURRENT][REGRESSION]` — language switch не меняет edits/selection/filter values.
- `UX-LOC-003 [P2][CURRENT][REGRESSION]` — каждая локаль на 1000×640: критические controls доступны.
- `UX-LOC-004 [P2][CURRENT][REGRESSION]` — longest translations in Import/Export remain actionable.
- `UX-LOC-005 [P1][CURRENT][REGRESSION]` — filename `%`, braces, Unicode treated as data.
- `UX-LOC-006 [P1][CURRENT][REGRESSION]` — language persists restart.
- `UX-LOC-007 [P1][CURRENT][REGRESSION]` — corrupt preferences → safe English fallback.
- `UX-LOC-008 [P1][CURRENT][REGRESSION]` — preference write failure safe.
- `UX-UPD-001 [P1][CURRENT][REGRESSION]` — update popup in idle does not block app.
- `UX-UPD-002 [P1][CURRENT][BLOCKING]` — updater restart waits for autosave.
- `UX-UPD-003 [P1][CURRENT][BLOCKING]` — updater restart waits for import/export/batch.
- `UX-UPD-004 [P1][CURRENT][REGRESSION]` — user may cancel cancellable op while restart pending.
- `UX-UPD-005 [P1][CURRENT][REGRESSION]` — update check failure does not block editor.
- `UX-UPD-006 [P2][CURRENT][REGRESSION]` — update UI does not steal focus mid-slider drag.

---

## U. Keyboard / focus / accessibility

- `UX-KEY-001 [P1][CURRENT][BLOCKING]` — Cmd+O.
- `UX-KEY-002 [P1][CURRENT][BLOCKING]` — Cmd+Z / Cmd+Shift+Z.
- `UX-KEY-003 [P1][CURRENT][BLOCKING]` — Cmd+A in Library.
- `UX-KEY-004 [P1][CURRENT][REGRESSION]` — P/X/U and 0–5 shortcuts.
- `UX-KEY-005 [P1][CURRENT][REGRESSION]` — metadata shortcuts suppressed inside TextField/TextArea.
- `UX-KEY-006 [P1][CURRENT][REGRESSION]` — backslash Original toggle.
- `UX-KEY-007 [P1][CURRENT][REGRESSION]` — fullscreen shortcut.
- `UX-KEY-008 [P2][ALL][REGRESSION]` — deterministic Tab order through primary controls.
- `UX-KEY-009 [P2][ALL][REGRESSION]` — focused interactive control has visible focus indication.
- `UX-KEY-010 [P1][ALL][REGRESSION]` — Enter/Space activates focused button once.
- `UX-KEY-011 [P1][ALL][REGRESSION]` — Esc closes cancellable dialog but not mandatory safety wait.
- `UX-KEY-012 [P2][ALL][REGRESSION]` — all icon-only buttons have Accessible.name.
- `UX-KEY-013 [P2][ALL][REGRESSION]` — sliders expose accessible name/value/range.
- `UX-KEY-014 [P2][ALL][REGRESSION]` — AX tree has no duplicate unlabeled primary controls.
- `UX-KEY-015 [P2][ALL][REGRESSION]` — keyboard-only import→select→Develop→edit→export.
- `UX-KEY-016 [P2][ALL][REGRESSION]` — focus not trapped in ScrollView.
- `UX-KEY-017 [P2][ALL][REGRESSION]` — closing popup returns focus logically.
- `UX-KEY-018 [P1][ALL][CHAOS]` — random modifier up/down around gestures → no stuck modifier state.

---

## V. macOS-specific UX

- `UX-MAC-001 [P2][ALL][REGRESSION]` — Retina 2× pointer/render mapping.
- `UX-MAC-002 [P2][ALL][REGRESSION]` — display scale/external monitor change → no crash/layout corruption.
- `UX-MAC-003 [P1][CURRENT][REGRESSION]` — move window between displays → display profile update without stale asset.
- `UX-MAC-004 [P2][ALL][PERF_LOCAL]` — display profile transition stall metrics.
- `UX-MAC-005 [P1][ALL][REGRESSION]` — sleep/wake with open photo.
- `UX-MAC-006 [P1][ALL][REGRESSION]` — sleep/wake during import/export.
- `UX-MAC-007 [P1][ALL][REGRESSION]` — removable source disappears after Add import.
- `UX-MAC-008 [P1][ALL][REGRESSION]` — removable destination disappears during Copy/export.
- `UX-MAC-009 [P2][ALL][REGRESSION]` — Magic Mouse/trackpad momentum scroll does not modify slider accidentally.
- `UX-MAC-010 [P2][ALL][REGRESSION]` — nested ScrollViews receive trackpad scrolling predictably.
- `UX-MAC-011 [P2][ALL][REGRESSION]` — macOS appearance change never makes controls unreadable.
- `UX-MAC-012 [P1][ALL][REGRESSION]` — Finder open-with file → one action/window, no duplicate open.

---

## W. Performance / memory / soak

- `UX-PERF-001 [P2][ALL][PERF_LOCAL]` — cold startup 20 runs.
- `UX-PERF-002 [P2][ALL][PERF_LOCAL]` — warm startup 20 runs.
- `UX-PERF-003 [P2][CURRENT][PERF_LOCAL]` — initial 24 MP generated RAW preview 20 runs.
- `UX-PERF-004 [P2][CURRENT][PERF_LOCAL]` — cached image switch 100 samples.
- `UX-PERF-005 [P2][CURRENT][PERF_LOCAL]` — grid scroll 1k assets.
- `UX-PERF-006 [P2][CURRENT][PERF_LOCAL]` — search 100k metadata fixture.
- `UX-PERF-007 [P2][CURRENT][PERF_LOCAL]` — Basic slider preview latency.
- `UX-PERF-008 [P2][CURRENT][PERF_LOCAL]` — Noise Reduction slider latency.
- `UX-PERF-009 [P2][CURRENT][PERF_LOCAL]` — Crop drag frame pacing.
- `UX-PERF-010 [P2][CURRENT][PERF_LOCAL]` — Guided Transform drag frame pacing.
- `UX-PERF-011 [P2][CURRENT][PERF_LOCAL]` — import 1k UI responsiveness.
- `UX-PERF-012 [P2][CURRENT][PERF_LOCAL]` — export while editing responsiveness.
- `UX-PERF-013 [P1][ALL][STRESS]` — 60 min realistic editing → RSS plateau.
- `UX-PERF-014 [P1][ALL][STRESS]` — 60 min Library scroll/search/select → bounded delegate/object count.
- `UX-PERF-015 [P1][ALL][STRESS]` — 500 photo switches → no linear memory growth.
- `UX-PERF-016 [P1][ALL][STRESS]` — 500 preset hovers → no linear memory growth.
- `UX-PERF-017 [P1][ALL][STRESS]` — Export panel open/close 200 cycles.
- `UX-PERF-018 [P1][ALL][STRESS]` — Import dialog open/cancel 200 cycles.
- `UX-PERF-019 [P2][ALL][PERF_LOCAL]` — peak RSS recorded on 8 GB reference config.
- `UX-PERF-020 [P1][ALL][STRESS]` — throttled/high-load CPU scenario → UI remains cancellable.

---

## X. Concurrency / race conditions

- `UX-RACE-001 [P0][CURRENT][BLOCKING]` — photo switch while old preview callback pending.
- `UX-RACE-002 [P1][CURRENT][REGRESSION]` — thumbnail invalidation while request in flight.
- `UX-RACE-003 [P1][CURRENT][REGRESSION]` — autosave completion after photo switch.
- `UX-RACE-004 [P1][CURRENT][REGRESSION]` — preset hover completion after pointer leave.
- `UX-RACE-005 [P1][CURRENT][REGRESSION]` — Auto Transform completion after cancel/switch.
- `UX-RACE-006 [P1][CURRENT][REGRESSION]` — export snapshot while autosave debounce pending.
- `UX-RACE-007 [P1][CURRENT][REGRESSION]` — batch commit while thumbnail workers render old revisions.
- `UX-RACE-008 [P1][CURRENT][REGRESSION]` — filter change while Library page fetch pending.
- `UX-RACE-009 [P1][CURRENT][REGRESSION]` — import row insert while selection operation runs.
- `UX-RACE-010 [P1][CURRENT][REGRESSION]` — close request while multiple controllers emit close-ready.
- `UX-RACE-011 [P1][CURRENT][STRESS]` — randomized async completion ordering with injected delays.
- `UX-RACE-012 [P0][ALL][CHAOS]` — 10k random legal actions + random worker delays; replayable.

---

## Y. Long text / geometry / visual resilience

- `UX-VIS-001 [P2][ALL][REGRESSION]` — filename 255 chars in top bar/card/source label.
- `UX-VIS-002 [P2][ALL][REGRESSION]` — path 1 000 chars.
- `UX-VIS-003 [P2][CURRENT][REGRESSION]` — exporter current filename 255 chars.
- `UX-VIS-004 [P2][CURRENT][REGRESSION]` — 100-line export failures list.
- `UX-VIS-005 [P2][CURRENT][REGRESSION]` — 100-line import report.
- `UX-VIS-006 [P2][CURRENT][REGRESSION]` — status text changes no top-level geometry jump.
- `UX-VIS-007 [P2][ALL][REGRESSION]` — all translations at minimum window.
- `UX-VIS-008 [P2][ALL][REGRESSION]` — no text under another interactive control.
- `UX-VIS-009 [P2][ALL][REGRESSION]` — no invisible hitbox after popup/panel close.
- `UX-VIS-010 [P1][ALL][REGRESSION]` — disabled button cannot activate through hidden shortcut/event.
- `UX-VIS-011 [P2][ALL][REGRESSION]` — tooltip never blocks subsequent click.
- `UX-VIS-012 [P2][ALL][REGRESSION]` — scrollbar range/thumb updates after dynamic relayout.

---

# 8. 0.5 Local Adjustments / Masks

Включать автоматически, если checkout содержит Local Adjustments capability.

## Mask lifecycle

- `UX-MSK-001 [P1][0.5][BLOCKING]` — create first mask → visible in list/preview.
- `UX-MSK-002 [P1][0.5][BLOCKING]` — multiple masks → correct active selection.
- `UX-MSK-003 [P1][0.5][REGRESSION]` — rename mask.
- `UX-MSK-004 [P1][0.5][REGRESSION]` — delete mask + Undo restores it.
- `UX-MSK-005 [P1][0.5][REGRESSION]` — duplicate mask if supported.
- `UX-MSK-006 [P1][0.5][REGRESSION]` — Invert = one Undo.
- `UX-MSK-007 [P1][0.5][REGRESSION]` — Add/Subtract/Intersect hierarchy consistent.
- `UX-MSK-008 [P0][0.5][BLOCKING]` — restart preserves masks/effect exactly.
- `UX-MSK-009 [P0][0.5][BLOCKING]` — photo switch never shows previous photo mask overlay.
- `UX-MSK-010 [P1][0.5][REGRESSION]` — Original hides local effect without mutating masks.

## Brush

- `UX-MSK-011 [P1][0.5][BLOCKING]` — brush stroke follows pointer.
- `UX-MSK-012 [P2][0.5][PERF_LOCAL]` — pointer→overlay p95 target <=50 ms.
- `UX-MSK-013 [P1][0.5][BLOCKING]` — continuous stroke = one Undo.
- `UX-MSK-014 [P1][0.5][REGRESSION]` — release outside image safely finalizes.
- `UX-MSK-015 [P1][0.5][REGRESSION]` — app deactivation mid-stroke safe.
- `UX-MSK-016 [P1][0.5][REGRESSION]` — size/feather/flow changes behave immediately.
- `UX-MSK-017 [P1][0.5][REGRESSION]` — fast zig-zag stroke has no giant missing segments.
- `UX-MSK-018 [P1][0.5][STRESS]` — 5 000 points / 500 strokes → bounded memory.
- `UX-MSK-019 [P1][0.5][REGRESSION]` — outside-image brush makes no edit.
- `UX-MSK-020 [P0][0.5][BLOCKING]` — brush coordinates remain correct with Crop/Transform.

## Gradients

- `UX-MSK-021 [P1][0.5][BLOCKING]` — Linear Gradient create/move/rotate/resize.
- `UX-MSK-022 [P1][0.5][BLOCKING]` — Radial Gradient create/move/resize/rotate.
- `UX-MSK-023 [P1][0.5][REGRESSION]` — handles never become permanently inaccessible.
- `UX-MSK-024 [P1][0.5][REGRESSION]` — one gradient drag = one Undo.
- `UX-MSK-025 [P2][0.5][PERF_LOCAL]` — gradient drag frame pacing.

## Range masks

- `UX-MSK-026 [P1][0.5][BLOCKING]` — Luminance Range eyedropper/selection.
- `UX-MSK-027 [P1][0.5][BLOCKING]` — Color Range eyedropper/selection.
- `UX-MSK-028 [P1][0.5][REGRESSION]` — click outside photo ignored.
- `UX-MSK-029 [P1][0.5][REGRESSION]` — threshold drag = one Undo.
- `UX-MSK-030 [P2][0.5][PERF_LOCAL]` — range overlay update latency.

## Overlay / local adjustments

- `UX-MSK-031 [P1][0.5][BLOCKING]` — overlay toggle affects display only.
- `UX-MSK-032 [P1][0.5][REGRESSION]` — overlay color/opacity never affects export.
- `UX-MSK-033 [P1][0.5][BLOCKING]` — local Exposure affects only masked area.
- `UX-MSK-034 [P1][0.5][REGRESSION]` — local slider gesture = one Undo.
- `UX-MSK-035 [P1][0.5][REGRESSION]` — switch active mask → controls update immediately, no stale values.
- `UX-MSK-036 [P1][0.5][REGRESSION]` — disable mask retains data but removes effect.
- `UX-MSK-037 [P0][0.5][BLOCKING]` — export never contains editor overlay.
- `UX-MSK-038 [P1][0.5][REGRESSION]` — preset/Original/Export transitions clear transient overlay state.
- `UX-MSK-039 [P1][0.5][STRESS]` — 100 masks × 10 local parameters remains usable.
- `UX-MSK-040 [P0][0.5][BLOCKING]` — mask edit + immediate quit/restart → consistent document.

---

# 9. TARGET UX gaps — probe для roadmap

Эти проверки не должны ломать build, пока функция не объявлена реализованной.

Статус: `NOT_IMPLEMENTED` + запись в roadmap candidates.

Проверять:

- true 1:1 zoom;
- arbitrary zoom;
- pan;
- Fit/Fill;
- before/after split;
- histogram interaction;
- clipping indicators;
- persistent History;
- Versions/Snapshots;
- missing-file relink;
- catalog backup workflow;
- smart collections;
- XMP workflow;
- customizable shortcuts;
- multi-monitor secondary view;
- richer accessibility navigation;
- Metal rendering;
- progressive quality rendering;
- AI features;
- HDR;
- Panorama.

Для каждого gap фиксировать:

```text
feature
current_status
user_impact
recommended_priority
dependencies
suggested_milestone
```

---

# 10. UX Chaos Monkey

Реализовать state-machine random tester.

## Action pool

```text
switch Library/Develop
select asset
Cmd-select
Shift-select
scroll grid
scroll filmstrip
scroll adjustments
rate
flag
label
change filters
type search
open/cancel import
open/cancel export
open/close settings
drag random enabled slider
toggle correction
toggle Original
apply preset
hover preset
Undo
Redo
enter/leave Crop
Transform Auto/cancel
resize window
fullscreen
deactivate/reactivate app
```

После 0.5 добавить:

```text
create mask
select mask
brush stroke
gradient drag
invert
add/subtract/intersect
local slider
delete mask
Undo/Redo
```

## Invariants после каждого шага

- process alive;
- main-loop heartbeat alive;
- active asset ID valid;
- selectedCount соответствует модели;
- UI active photo = editor active asset;
- displayed numeric values finite;
- no gesture remains active beyond timeout;
- no modal stack inconsistency;
- no hidden busy state without operation;
- original hashes unchanged;
- catalog integrity valid at checkpoints.

## Reproducibility

Каждый fail печатает seed.

Replay:

```bash
python3 tests/ux/run_ux_suite.py --replay build/ux-artifacts/.../replay.jsonl
```

---

# 11. Performance baseline profiles

Минимум:

```text
Apple Silicon 8 GB
Apple Silicon 16 GB
```

Если доступен один Mac — обязательно сохранить:

```text
chip
RAM
macOS version
display scale
build type
Qt version
commit SHA
catalog size
fixture
```

Нельзя использовать performance цифры hosted GitHub Actions как release benchmark.

---

# 12. Release UX gates

## MVP / 0.5 candidate

Обязательно:

- 0 P0;
- 0 P1 в CURRENT/0.5 blocking suite;
- catalog integrity passes;
- original hash invariant passes;
- no hard hang;
- каждый failure имеет artifacts;
- blocking test не flaky после 3 последовательных повторов.

P2 допускаются, но автоматически идут в stabilization roadmap.

## Stabilization candidate

Дополнительно:

- scroll/input performance budgets достигнуты или имеют explicit exception;
- soak tests pass;
- RSS reaches plateau;
- 100k metadata UX usable;
- совокупно минимум 100k chaos actions без P0/P1.

## 1.0 candidate

Дополнительно:

- TARGET gaps, объявленные scope 1.0, больше не NOT_IMPLEMENTED;
- keyboard-only critical workflow проходит;
- signed/notarized install/update UX проверяется black-box;
- update/recovery workflow проходит black-box.

---

# 13. Итоговый отчёт для roadmap

После полного прогона создавать:

```text
build/ux-report/UX_REPORT.md
build/ux-report/UX_REPORT.json
build/ux-report/ROADMAP_CANDIDATES.md
```

## UX_REPORT.md

Обязательно:

- commit/version;
- hardware;
- executed/pass/fail/skipped/not-implemented;
- P0/P1/P2/P3 summary;
- slowest interactions;
- worst event-loop stalls;
- worst scroll scenarios;
- memory growth;
- flaky tests;
- top failure screenshots.

## ROADMAP_CANDIDATES.md

Группировать проблемы не по test ID, а по продуктовым темам:

```text
Stability
Responsiveness
Library scale
Develop interaction
Async rendering races
Undo/recovery
Import/export resilience
Accessibility
Localization/layout
Local Adjustments
Missing full-product UX capabilities
```

Каждый roadmap candidate:

```text
Problem
Evidence / failing test IDs
Severity
Frequency
User impact
Likely subsystem
Recommended fix
Dependencies
Suggested milestone
```

---

# 14. Что НЕ считать хорошим UX автотестом

Не принимать тест, который:

- вызывает только C++ controller method, если цель — проверить настоящий click/drag/scroll;
- проверяет только «не упало» без state assertion;
- использует fixed sleep вместо condition;
- кликает по абсолютным координатам без необходимости;
- проходит, даже если пользователь визуально не получил ожидаемый результат;
- увеличивает timeout вместо расследования hang;
- скрывает flaky через retry без артефакта первого fail;
- чинит production code до появления reproducing regression test.

---

# 15. Порядок реализации Codex

## Stage 1 — Instrumentation

Сделать:

- stable test IDs;
- UI driver;
- screenshot on failure;
- event-loop watchdog;
- frame/input/RSS metrics;
- result JSON.

## Stage 2 — Blocking smoke UX

Первый обязательный набор:

```text
UX-APP-002
UX-APP-007
UX-APP-008
UX-APP-009
UX-NAV-001
UX-LIB-001
UX-SEL-001..005
UX-IMP-002..004
UX-DSC-001
UX-SLD-001..005
UX-PRV-001
UX-CRP-001
UX-TRN-003
UX-PRS-004..008
UX-BAT-001..006
UX-EXP-004..009
UX-UND-001..015
UX-REC-001..007
```

## Stage 3 — Scroll / race / fault injection

Добавить:

- Library recycling;
- async preview races;
- thumbnails;
- close coordination;
- disk/database failures.

## Stage 4 — Performance baseline

Сначала baseline, потом оптимизация.

## Stage 5 — Stress / chaos

Добавить soak, memory, random monkey + replay.

## Stage 6 — 0.5 Local Adjustments

Автоматически включить `UX-MSK-*`, если capability присутствует.

## Stage 7 — Roadmap generation

Сформировать первый `ROADMAP_CANDIDATES.md` по фактическим результатам.

---

# 16. Definition of Done

Задача по UX автотестам завершена, когда:

1. suite запускается одной командой;
2. blocking suite доступен через CTest/CI;
3. perf suite запускается локально на Mac;
4. каждый fail сохраняет screenshot/log/metrics;
5. hangs определяются watchdog, а не только global timeout;
6. random failure воспроизводим seed/replay;
7. public fixtures не содержат личных фото;
8. private Nikon acceptance optional;
9. original files never mutate;
10. после suite выполняется catalog integrity check;
11. формируется UX_REPORT;
12. формируется ROADMAP_CANDIDATES;
13. mask tests автоматически включаются по capability;
14. test selectors не зависят от языка;
15. selectors не зависят от абсолютных screen coordinates без необходимости.

---

# 17. Команды верхнего уровня

Полный прогон:

```bash
python3 tests/ux/run_ux_suite.py \
  --app build/Luma.app \
  --suite all \
  --artifacts build/ux-artifacts \
  --report build/ux-report
```

Release gate:

```bash
python3 tests/ux/run_ux_suite.py --suite blocking
```

Performance:

```bash
python3 tests/ux/run_ux_suite.py --suite perf --runs 20
```

Chaos:

```bash
python3 tests/ux/run_ux_suite.py --suite chaos --actions 10000 --seed 123456
```

---

# 18. Конечная продуктовая цель

Критерий успеха тестового контура:

> Пользователь может долго и агрессивно работать с Luma — быстро скроллить, переключать фотографии, тянуть ползунки, отменять действия, применять presets, запускать import/batch/export, менять язык, закрывать приложение в неудобный момент — и приложение остаётся предсказуемым, отзывчивым, безопасным для данных и понятным по своему состоянию.

Тестовая система должна превращать субъективное:

```text
«неудобно»
«подтормаживает»
«иногда подвисает»
«иногда не срабатывает»
```

в измеримое:

```text
scenario
reproduction
metric
severity
artifact
regression test
roadmap item
```

---

# ДОПОЛНЕНИЕ: обязательное покрытие roadmap 0.6 → 1.0.0

Это дополнение расширяет базовую UX/E2E/Stability-спецификацию на **все опубликованные фазы Luma вплоть до 1.0.0**.

Главный принцип: после завершения новой фазы все blocking/regression-тесты предыдущих фаз остаются активными. Новая функциональность не может считаться принятой, если она ломает уже существующие Library / Develop / Local Adjustments / Export / Recovery workflow.

Рекомендуемые scope-теги дополнительно:

```text
0.6   Performance
0.7   Advanced
0.8   Merge
0.9   Stabilization
1.0   Stable daily-use release
GPU_PERF
PRIVACY
MIGRATION
SOAK
```

Capability detection должен определяться по рабочему checkout, а не по захардкоженному номеру версии. Если функция ещё не реализована до своей фазы — `NOT_IMPLEMENTED`; после объявления соответствующей фазы завершённой — отсутствие обязательной функции становится release-blocking failure.

---

# Фаза 0.6 — Performance

Roadmap scope:

```text
Metal processing backend
CPU/GPU tolerance tests
bounded LRU
progressive quality
true 1:1 rendering
memory pressure handling
latency benchmarks on 8/16 GB Apple Silicon
```

## Metal / GPU backend

- `UX-P06-GPU-001 [P0][0.6][BLOCKING]` — включение Metal backend не меняет catalog/edit state.
- `UX-P06-GPU-002 [P0][0.6][BLOCKING]` — CPU и GPU render neutral image совпадают в утверждённой tolerance.
- `UX-P06-GPU-003 [P1][0.6][REGRESSION]` — CPU/GPU tolerance: WB/Exposure/Tone.
- `UX-P06-GPU-004 [P1][0.6][REGRESSION]` — CPU/GPU tolerance: Curves/HSL.
- `UX-P06-GPU-005 [P1][0.6][REGRESSION]` — CPU/GPU tolerance: Sharpening/Noise Reduction.
- `UX-P06-GPU-006 [P1][0.6][REGRESSION]` — CPU/GPU tolerance: Crop/Transform/Lens.
- `UX-P06-GPU-007 [P1][0.6][REGRESSION]` — CPU/GPU tolerance: Local Adjustments / masks.
- `UX-P06-GPU-008 [P1][0.6][REGRESSION]` — alpha PNG/TIFF одинаково трактуются CPU/GPU.
- `UX-P06-GPU-009 [P1][0.6][REGRESSION]` — 16-bit/high-dynamic-range pipeline остаётся finite.
- `UX-P06-GPU-010 [P1][0.6][REGRESSION]` — Metal init failure → корректный fallback без blank preview.
- `UX-P06-GPU-011 [P1][0.6][REGRESSION]` — GPU command failure → UI остаётся живым, пользователь видит ошибку/fallback.
- `UX-P06-GPU-012 [P1][0.6][REGRESSION]` — sleep/wake пересоздаёт GPU resources без crash.
- `UX-P06-GPU-013 [P1][0.6][REGRESSION]` — disconnect/reconnect display не оставляет preview black.
- `UX-P06-GPU-014 [P2][0.6][GPU_PERF]` — basic slider pointer→preview latency p50/p95/p99.
- `UX-P06-GPU-015 [P2][0.6][GPU_PERF]` — Crop/Transform drag frame pacing.
- `UX-P06-GPU-016 [P2][0.6][GPU_PERF]` — CPU vs Metal throughput report на одинаковых fixtures.
- `UX-P06-GPU-017 [P1][0.6][STRESS]` — 10 000 GPU renders без resource leak.
- `UX-P06-GPU-018 [P1][0.6][CHAOS]` — random edits/photo switching при искусственно переставленном порядке GPU completions.

## Progressive quality

- `UX-P06-PRG-001 [P1][0.6][BLOCKING]` — тяжёлая правка быстро даёт interaction preview, затем final-quality render.
- `UX-P06-PRG-002 [P0][0.6][BLOCKING]` — low-quality preview никогда не экспортируется как final output.
- `UX-P06-PRG-003 [P1][0.6][REGRESSION]` — новая revision отменяет obsolete final render предыдущей revision.
- `UX-P06-PRG-004 [P0][0.6][REGRESSION]` — late render старой revision не заменяет новую картинку.
- `UX-P06-PRG-005 [P2][0.6][GPU_PERF]` — time-to-first-feedback.
- `UX-P06-PRG-006 [P2][0.6][GPU_PERF]` — time-to-final-quality.
- `UX-P06-PRG-007 [P1][0.6][REGRESSION]` — Original toggle корректно взаимодействует с progressive queue.
- `UX-P06-PRG-008 [P1][0.6][REGRESSION]` — preset hover progressive preview не сохраняется.
- `UX-P06-PRG-009 [P1][0.6][REGRESSION]` — photo switch немедленно инвалидирует старые progressive stages.
- `UX-P06-PRG-010 [P1][0.6][STRESS]` — 1 000 быстрых slider changes не создают хвост устаревших render jobs.

## True 1:1 / zoom / pan

- `UX-P06-ZOM-001 [P1][0.6][BLOCKING]` — true 1:1 показывает заявленное pixel mapping.
- `UX-P06-ZOM-002 [P1][0.6][REGRESSION]` — Fit → 1:1 → Fit сохраняет логический центр изображения.
- `UX-P06-ZOM-003 [P1][0.6][REGRESSION]` — zoom around pointer сохраняет anchor.
- `UX-P06-ZOM-004 [P1][0.6][REGRESSION]` — pan boundaries не позволяют потерять изображение вне canvas.
- `UX-P06-ZOM-005 [P1][0.6][REGRESSION]` — photo switch имеет определённую политику reset/persist zoom.
- `UX-P06-ZOM-006 [P1][0.6][BLOCKING]` — Crop coordinate mapping корректен на 1:1 и произвольном zoom.
- `UX-P06-ZOM-007 [P1][0.6][BLOCKING]` — Guided Transform mapping корректен при zoom/pan.
- `UX-P06-ZOM-008 [P1][0.6][BLOCKING]` — Brush/masks mapping корректен при zoom/pan.
- `UX-P06-ZOM-009 [P1][0.6][BLOCKING]` — targeted tone/HSL mapping корректен при zoom/pan.
- `UX-P06-ZOM-010 [P2][0.6][GPU_PERF]` — pan frame pacing на 1:1 в течение 30 секунд.
- `UX-P06-ZOM-011 [P1][0.6][STRESS]` — 5 000 zoom/pan actions без drift/NaN.
- `UX-P06-ZOM-012 [P2][0.6][REGRESSION]` — trackpad pinch/scroll semantics предсказуемы.

## Bounded LRU / cache pressure

- `UX-P06-LRU-001 [P1][0.6][BLOCKING]` — preview cache соблюдает byte/item budget.
- `UX-P06-LRU-002 [P1][0.6][BLOCKING]` — thumbnail cache bounded.
- `UX-P06-LRU-003 [P1][0.6][REGRESSION]` — recently used item переживает нормальный pressure.
- `UX-P06-LRU-004 [P1][0.6][REGRESSION]` — evicted item прозрачно регенерируется.
- `UX-P06-LRU-005 [P1][0.6][REGRESSION]` — corrupted cache entry регенерируется без app-level error.
- `UX-P06-LRU-006 [P1][0.6][STRESS]` — 10k photo navigation не даёт linear memory growth.
- `UX-P06-LRU-007 [P1][0.6][STRESS]` — cache churn + edits достигает memory plateau.
- `UX-P06-LRU-008 [P2][0.6][GPU_PERF]` — hit/miss latency baseline.

## Memory pressure: 8 GB / 16 GB Apple Silicon

- `UX-P06-MEM-001 [P0][0.6][BLOCKING]` — memory warning не повреждает catalog/edit state.
- `UX-P06-MEM-002 [P1][0.6][BLOCKING]` — reclaimable caches освобождаются под pressure.
- `UX-P06-MEM-003 [P1][0.6][REGRESSION]` — качество preview может деградировать, но UI остаётся интерактивным.
- `UX-P06-MEM-004 [P1][0.6][REGRESSION]` — export под memory pressure либо завершается, либо чисто падает с контролируемой ошибкой.
- `UX-P06-MEM-005 [P1][0.6][REGRESSION]` — import под memory pressure остаётся cancellable.
- `UX-P06-MEM-006 [P1][0.6][REGRESSION]` — большой mask document не приводит к crash.
- `UX-P06-MEM-007 [P1][0.6][SOAK]` — 2 часа realistic-use на 8 GB profile.
- `UX-P06-MEM-008 [P1][0.6][SOAK]` — 2 часа realistic-use на 16 GB profile.
- `UX-P06-MEM-009 [P2][0.6][GPU_PERF]` — peak RSS / compressed memory / swap / GPU memory report.
- `UX-P06-MEM-010 [P1][0.6][STRESS]` — 100 больших изображений + edits + export при constrained memory.

### Release gate 0.6

0.6 не закрывается при stale GPU render, CPU/GPU mismatch выше tolerance, linear memory growth, неправильном 1:1 mapping, export preview-quality вместо final-quality или потере cancelability.

---

# Фаза 0.7 — Advanced

Roadmap scope:

```text
optional local AI denoise
AI masks
content-aware removal
semantic search
provider interfaces
privacy consent before any cloud option
```

## Provider interfaces / privacy

- `UX-P07-PRV-001 [P0][0.7][PRIVACY]` — local-only mode делает zero network calls во всех AI workflow.
- `UX-P07-PRV-002 [P0][0.7][PRIVACY]` — cloud provider не может использоваться до explicit consent.
- `UX-P07-PRV-003 [P0][0.7][PRIVACY]` — Cancel в consent оставляет cloud provider disabled.
- `UX-P07-PRV-004 [P1][0.7][PRIVACY]` — consent UI явно отражает тип передаваемых данных.
- `UX-P07-PRV-005 [P0][0.7][PRIVACY]` — выбор cloud provider не инициирует скрытую индексацию/загрузку существующего каталога.
- `UX-P07-PRV-006 [P1][0.7][REGRESSION]` — provider unavailable не ломает обычный Develop/Library.
- `UX-P07-PRV-007 [P1][0.7][REGRESSION]` — timeout/provider error остаётся cancellable.
- `UX-P07-PRV-008 [P1][0.7][REGRESSION]` — auth failure изолирован и понятен пользователю.
- `UX-P07-PRV-009 [P0][0.7][REGRESSION]` — AI result привязан к exact asset ID + edit revision.
- `UX-P07-PRV-010 [P0][0.7][REGRESSION]` — late AI result photo A никогда не меняет photo B.
- `UX-P07-PRV-011 [P1][0.7][PRIVACY]` — logs не содержат image bytes / credentials / provider secret.
- `UX-P07-PRV-012 [P1][0.7][CHAOS]` — randomized provider delay/errors + photo switching.

## Local AI denoise

- `UX-P07-DNS-001 [P1][0.7][BLOCKING]` — AI denoise показывает явное processing state.
- `UX-P07-DNS-002 [P1][0.7][BLOCKING]` — operation cancellable.
- `UX-P07-DNS-003 [P1][0.7][REGRESSION]` — cancel оставляет предыдущий committed state.
- `UX-P07-DNS-004 [P1][0.7][REGRESSION]` — photo switch отсоединяет старый result.
- `UX-P07-DNS-005 [P1][0.7][REGRESSION]` — normal Develop после denoise следует pipeline contract.
- `UX-P07-DNS-006 [P1][0.7][REGRESSION]` — Original bypasses result визуально, но не удаляет его.
- `UX-P07-DNS-007 [P1][0.7][REGRESSION]` — export использует только committed denoise state.
- `UX-P07-DNS-008 [P1][0.7][BLOCKING]` — committed denoise survives restart/reload.
- `UX-P07-DNS-009 [P2][0.7][GPU_PERF]` — inference latency by megapixels.
- `UX-P07-DNS-010 [P1][0.7][STRESS]` — 100 start/cancel cycles без memory leak.
- `UX-P07-DNS-011 [P1][0.7][REGRESSION]` — insufficient memory → controlled refusal.
- `UX-P07-DNS-012 [P1][0.7][REGRESSION]` — unsupported source clearly disables feature.

## AI masks

- `UX-P07-MSK-001 [P1][0.7][BLOCKING]` — generate subject mask.
- `UX-P07-MSK-002 [P1][0.7][REGRESSION]` — generate supported object/background/person masks.
- `UX-P07-MSK-003 [P1][0.7][BLOCKING]` — AI mask входит в общий Local Adjustments mask lifecycle.
- `UX-P07-MSK-004 [P1][0.7][REGRESSION]` — manual refinement of AI mask.
- `UX-P07-MSK-005 [P1][0.7][REGRESSION]` — invert/add/subtract/intersect работает с AI mask.
- `UX-P07-MSK-006 [P1][0.7][REGRESSION]` — rerun AI не уничтожает user-refined mask молча.
- `UX-P07-MSK-007 [P1][0.7][REGRESSION]` — model/version change имеет явные semantics.
- `UX-P07-MSK-008 [P0][0.7][REGRESSION]` — async revision guard предотвращает stale mask.
- `UX-P07-MSK-009 [P2][0.7][GPU_PERF]` — time-to-first-overlay.
- `UX-P07-MSK-010 [P1][0.7][STRESS]` — 200 generate/delete/refine cycles.

## Content-aware removal

- `UX-P07-REM-001 [P1][0.7][BLOCKING]` — draw removal region → generate result.
- `UX-P07-REM-002 [P1][0.7][BLOCKING]` — generation cancellable.
- `UX-P07-REM-003 [P1][0.7][BLOCKING]` — one committed removal = predictable Undo unit.
- `UX-P07-REM-004 [P1][0.7][REGRESSION]` — multiple regions maintain ordering/state.
- `UX-P07-REM-005 [P0][0.7][REGRESSION]` — late result cannot attach to another photo/revision.
- `UX-P07-REM-006 [P1][0.7][REGRESSION]` — Crop/Transform change handles removal geometry safely.
- `UX-P07-REM-007 [P1][0.7][REGRESSION]` — Original shows source without destructive deletion.
- `UX-P07-REM-008 [P0][0.7][REGRESSION]` — editor overlay never appears in export.
- `UX-P07-REM-009 [P2][0.7][GPU_PERF]` — generation/interaction latency.
- `UX-P07-REM-010 [P1][0.7][STRESS]` — 100 remove/Undo/Redo cycles.

## Semantic search

- `UX-P07-SEM-001 [P1][0.7][BLOCKING]` — deterministic fixture query returns expected semantic neighborhood.
- `UX-P07-SEM-002 [P1][0.7][REGRESSION]` — semantic search combines with rating/flag/label/collection filters.
- `UX-P07-SEM-003 [P1][0.7][REGRESSION]` — empty query has defined state.
- `UX-P07-SEM-004 [P1][0.7][REGRESSION]` — typing query rapidly cancels/debounces old searches.
- `UX-P07-SEM-005 [P0][0.7][REGRESSION]` — late query A result cannot replace query B.
- `UX-P07-SEM-006 [P2][0.7][PERF_LOCAL]` — search latency at 10k/100k assets.
- `UX-P07-SEM-007 [P1][0.7][REGRESSION]` — indexing progress не блокирует Library.
- `UX-P07-SEM-008 [P1][0.7][REGRESSION]` — cancel/restart indexing leaves recoverable index.
- `UX-P07-SEM-009 [P0][0.7][PRIVACY]` — local semantic search делает zero network calls.
- `UX-P07-SEM-010 [P1][0.7][REGRESSION]` — corrupt/missing index rebuild path.
- `UX-P07-SEM-011 [P1][0.7][STRESS]` — reindex after 10k metadata changes без catalog corruption.
- `UX-P07-SEM-012 [P2][0.7][REGRESSION]` — scoring refresh не сбрасывает selection/scroll неожиданно.

### Release gate 0.7

0.7 блокируется любым privacy breach, stale AI result, non-cancellable AI operation, попаданием editor overlay в export, потерей Undo semantics либо деградацией обычного не-AI workflow при недоступном provider.

---

# Фаза 0.8 — Merge

Roadmap scope:

```text
HDR
panorama
alignment
deghosting
editable float outputs
```

## Common merge lifecycle

- `UX-P08-MRG-001 [P1][0.8][BLOCKING]` — create merge job from multi-selection.
- `UX-P08-MRG-002 [P1][0.8][REGRESSION]` — invalid selection/type/size rejected before destructive work.
- `UX-P08-MRG-003 [P1][0.8][BLOCKING]` — current phase/progress visible.
- `UX-P08-MRG-004 [P1][0.8][BLOCKING]` — cancel merge.
- `UX-P08-MRG-005 [P1][0.8][REGRESSION]` — cancel cleans partial derived output/temp files.
- `UX-P08-MRG-006 [P0][0.8][BLOCKING]` — source originals unchanged.
- `UX-P08-MRG-007 [P1][0.8][REGRESSION]` — app close during merge follows safe wait/cancel path.
- `UX-P08-MRG-008 [P1][0.8][REGRESSION]` — restart after crash/cancel cleans/recover unfinished job.
- `UX-P08-MRG-009 [P1][0.8][BLOCKING]` — successful result enters Library exactly once.
- `UX-P08-MRG-010 [P1][0.8][BLOCKING]` — derived result independently editable/non-destructive.
- `UX-P08-MRG-011 [P1][0.8][STRESS]` — repeated start/cancel no temp leak.
- `UX-P08-MRG-012 [P2][0.8][PERF_LOCAL]` — UI responsiveness during background merge.

## HDR

- `UX-P08-HDR-001 [P1][0.8][BLOCKING]` — merge valid 3-frame bracket.
- `UX-P08-HDR-002 [P1][0.8][REGRESSION]` — 5/7-frame bracket.
- `UX-P08-HDR-003 [P1][0.8][REGRESSION]` — unordered inputs normalized/presented predictably.
- `UX-P08-HDR-004 [P1][0.8][REGRESSION]` — incompatible dimensions handled clearly.
- `UX-P08-HDR-005 [P1][0.8][REGRESSION]` — metadata mismatch warning without data loss.
- `UX-P08-HDR-006 [P1][0.8][BLOCKING]` — alignment option affects derived result only.
- `UX-P08-HDR-007 [P1][0.8][BLOCKING]` — deghost option exercised on moving-object fixture.
- `UX-P08-HDR-008 [P1][0.8][REGRESSION]` — clipped/high-range inputs never produce NaN/Inf.
- `UX-P08-HDR-009 [P1][0.8][BLOCKING]` — result has correct editable dynamic-range representation.
- `UX-P08-HDR-010 [P1][0.8][REGRESSION]` — export HDR result to supported SDR/16-bit output path.
- `UX-P08-HDR-011 [P2][0.8][PERF_LOCAL]` — timings for 3/5/7 frames.
- `UX-P08-HDR-012 [P1][0.8][STRESS]` — 50 HDR jobs in session.

## Panorama

- `UX-P08-PAN-001 [P1][0.8][BLOCKING]` — horizontal panorama.
- `UX-P08-PAN-002 [P1][0.8][BLOCKING]` — vertical panorama.
- `UX-P08-PAN-003 [P1][0.8][REGRESSION]` — shuffled input ordering deterministic or explicitly user-controlled.
- `UX-P08-PAN-004 [P1][0.8][REGRESSION]` — insufficient overlap refused cleanly.
- `UX-P08-PAN-005 [P1][0.8][REGRESSION]` — incompatible focal/orientation warning.
- `UX-P08-PAN-006 [P1][0.8][REGRESSION]` — moving-object/deghost fixture.
- `UX-P08-PAN-007 [P1][0.8][REGRESSION]` — extreme output dimensions do not break UI/layout.
- `UX-P08-PAN-008 [P1][0.8][REGRESSION]` — transparent uncovered boundary preview is correct.
- `UX-P08-PAN-009 [P2][0.8][PERF_LOCAL]` — 5/10/25-frame timings.
- `UX-P08-PAN-010 [P1][0.8][STRESS]` — 20 panorama jobs one session.
- `UX-P08-PAN-011 [P1][0.8][REGRESSION]` — cancel at alignment/stitch/deghost phases.
- `UX-P08-PAN-012 [P1][0.8][BLOCKING]` — panorama remains editable after restart.

## Alignment / deghost primitives

- `UX-P08-ALG-001 [P1][0.8][BLOCKING]` — alignment transform finite/bounded.
- `UX-P08-ALG-002 [P1][0.8][REGRESSION]` — alignment failure creates no partial catalog result.
- `UX-P08-ALG-003 [P1][0.8][REGRESSION]` — source IDs/revisions retained for derived result provenance.
- `UX-P08-DGH-001 [P1][0.8][BLOCKING]` — deterministic deghost on/off fixture difference.
- `UX-P08-DGH-002 [P1][0.8][REGRESSION]` — deghost controls have predictable history semantics.
- `UX-P08-DGH-003 [P0][0.8][REGRESSION]` — deghost preview/selection overlay never written into output.

## Editable float outputs

- `UX-P08-FLT-001 [P0][0.8][BLOCKING]` — float output survives save/restart without precision-destructive conversion.
- `UX-P08-FLT-002 [P1][0.8][BLOCKING]` — Develop sliders remain finite on float source.
- `UX-P08-FLT-003 [P1][0.8][REGRESSION]` — Crop/Transform/Lens operate on float result.
- `UX-P08-FLT-004 [P1][0.8][REGRESSION]` — Local Adjustments operate on float result.
- `UX-P08-FLT-005 [P1][0.8][REGRESSION]` — thumbnails/previews have predictable display transform.
- `UX-P08-FLT-006 [P1][0.8][REGRESSION]` — JPEG/PNG/TIFF/DNG export follows declared encoding/tone semantics.
- `UX-P08-FLT-007 [P0][0.8][REGRESSION]` — extreme edits do not propagate NaN/Inf.
- `UX-P08-FLT-008 [P1][0.8][STRESS]` — 100 edit/undo cycles on large float merge.

### Release gate 0.8

0.8 блокируется изменением originals, orphan/partial derived files, precision loss, неотменяемым merge, stale merge result и main-thread hang во время alignment/stitch/deghost.

---

# Фаза 0.9 — Stabilization

Roadmap scope:

```text
10k / 100k / 500k catalogs
complete metadata / XMP workflow
backups
missing-file relinking
recovery
fault injection
UI/performance regression tests
```

## 10k / 100k / 500k catalogs

- `UX-P09-CAT-001 [P1][0.9][BLOCKING]` — open 10k catalog.
- `UX-P09-CAT-002 [P2][0.9][PERF_LOCAL]` — 10k startup/open metrics.
- `UX-P09-CAT-003 [P2][0.9][PERF_LOCAL]` — 10k grid scroll.
- `UX-P09-CAT-004 [P2][0.9][PERF_LOCAL]` — 10k search/filter latency.
- `UX-P09-CAT-005 [P1][0.9][STRESS]` — 10k one-hour Library workflow.
- `UX-P09-CAT-006 [P1][0.9][BLOCKING]` — open 100k catalog.
- `UX-P09-CAT-007 [P2][0.9][PERF_LOCAL]` — 100k startup/open metrics.
- `UX-P09-CAT-008 [P2][0.9][PERF_LOCAL]` — 100k scroll/page fetch.
- `UX-P09-CAT-009 [P2][0.9][PERF_LOCAL]` — 100k combined search/filter.
- `UX-P09-CAT-010 [P1][0.9][STRESS]` — 100k one-hour realistic workload.
- `UX-P09-CAT-011 [P1][0.9][BLOCKING]` — open 500k catalog without materializing all QML delegates.
- `UX-P09-CAT-012 [P2][0.9][PERF_LOCAL]` — 500k startup/open.
- `UX-P09-CAT-013 [P2][0.9][PERF_LOCAL]` — 500k search latency.
- `UX-P09-CAT-014 [P2][0.9][PERF_LOCAL]` — 500k filter latency.
- `UX-P09-CAT-015 [P2][0.9][PERF_LOCAL]` — random navigation to distant rows.
- `UX-P09-CAT-016 [P1][0.9][STRESS]` — 500k two-hour Library workflow.
- `UX-P09-CAT-017 [P1][0.9][STRESS]` — thumbnail worker backlog bounded.
- `UX-P09-CAT-018 [P1][0.9][STRESS]` — RSS/QObject/delegate counts reach plateau.
- `UX-P09-CAT-019 [P1][0.9][REGRESSION]` — collection operations atomic at scale.
- `UX-P09-CAT-020 [P1][0.9][REGRESSION]` — batch metadata edit on large selection cancellable/progressed.

## Complete metadata workflow

- `UX-P09-META-001 [P1][0.9][BLOCKING]` — display complete supported metadata set without blocking Library.
- `UX-P09-META-002 [P1][0.9][REGRESSION]` — Unicode metadata.
- `UX-P09-META-003 [P1][0.9][REGRESSION]` — very long captions/fields.
- `UX-P09-META-004 [P1][0.9][REGRESSION]` — mixed/common multi-selection states if supported.
- `UX-P09-META-005 [P1][0.9][REGRESSION]` — metadata edit history follows product contract.
- `UX-P09-META-006 [P1][0.9][BLOCKING]` — batch metadata write atomic.
- `UX-P09-META-007 [P0][0.9][REGRESSION]` — catalog-only metadata mode never silently rewrites original.
- `UX-P09-META-008 [P1][0.9][REGRESSION]` — export metadata policy uses current supported metadata state correctly.

## XMP workflow

- `UX-P09-XMP-001 [P0][0.9][BLOCKING]` — sidecar write atomic.
- `UX-P09-XMP-002 [P0][0.9][BLOCKING]` — original hash unchanged in sidecar workflow.
- `UX-P09-XMP-003 [P1][0.9][BLOCKING]` — read existing XMP.
- `UX-P09-XMP-004 [P1][0.9][REGRESSION]` — external XMP modification → explicit conflict UX.
- `UX-P09-XMP-005 [P1][0.9][REGRESSION]` — malformed XMP does not corrupt catalog.
- `UX-P09-XMP-006 [P1][0.9][REGRESSION]` — unsupported fields handled/preserved according to contract.
- `UX-P09-XMP-007 [P1][0.9][REGRESSION]` — filesystem case/path rules correct.
- `UX-P09-XMP-008 [P1][0.9][REGRESSION]` — read-only sidecar error actionable.
- `UX-P09-XMP-009 [P1][0.9][REGRESSION]` — batch XMP sync progress/cancel.
- `UX-P09-XMP-010 [P1][0.9][MIGRATION]` — supported catalog edits ↔ XMP roundtrip.
- `UX-P09-XMP-011 [P1][0.9][STRESS]` — 10k sidecar read/write synthetic run.
- `UX-P09-XMP-012 [P0][0.9][BLOCKING]` — crash during sidecar atomic replace leaves valid old or new file, never torn partial.

## Backup / restore

- `UX-P09-BKP-001 [P0][0.9][BLOCKING]` — manual backup creates readable snapshot.
- `UX-P09-BKP-002 [P0][0.9][BLOCKING]` — restore reproduces catalog/edit state.
- `UX-P09-BKP-003 [P1][0.9][REGRESSION]` — cancel backup leaves no false-success state.
- `UX-P09-BKP-004 [P1][0.9][REGRESSION]` — backup does not freeze editor.
- `UX-P09-BKP-005 [P0][0.9][REGRESSION]` — disk-full preserves previous good backup.
- `UX-P09-BKP-006 [P1][0.9][REGRESSION]` — corrupt backup rejected before restore.
- `UX-P09-BKP-007 [P1][0.9][REGRESSION]` — restore over active catalog requires explicit safe UX.
- `UX-P09-BKP-008 [P0][0.9][MIGRATION]` — older compatible backup migrates safely.
- `UX-P09-BKP-009 [P1][0.9][REGRESSION]` — retention/rotation never deletes only/newest valid backup.
- `UX-P09-BKP-010 [P1][0.9][SOAK]` — repeated/scheduled backup workflow if implemented.

## Missing-file relinking

- `UX-P09-RLK-001 [P1][0.9][BLOCKING]` — missing original visibly marked, edits remain intact.
- `UX-P09-RLK-002 [P1][0.9][BLOCKING]` — relink single file by identity.
- `UX-P09-RLK-003 [P1][0.9][BLOCKING]` — relink moved folder/tree.
- `UX-P09-RLK-004 [P0][0.9][REGRESSION]` — wrong same-name/different-hash file not silently relinked.
- `UX-P09-RLK-005 [P1][0.9][REGRESSION]` — explicitly modified legitimate source has controlled resolution flow.
- `UX-P09-RLK-006 [P1][0.9][BLOCKING]` — relink keeps edits/rating/collections.
- `UX-P09-RLK-007 [P1][0.9][REGRESSION]` — relink invalidates/regenerates preview safely.
- `UX-P09-RLK-008 [P1][0.9][REGRESSION]` — bulk 1k relink progress/cancel.
- `UX-P09-RLK-009 [P1][0.9][REGRESSION]` — cancel bulk relink keeps completed mappings consistent.
- `UX-P09-RLK-010 [P1][0.9][STRESS]` — repeated disconnect/reconnect external volume.

## Recovery / fault injection

- `UX-P09-FLT-001 [P0][0.9][BLOCKING]` — process kill at catalog transaction checkpoints.
- `UX-P09-FLT-002 [P0][0.9][BLOCKING]` — kill during XMP replace.
- `UX-P09-FLT-003 [P0][0.9][BLOCKING]` — kill during backup.
- `UX-P09-FLT-004 [P0][0.9][BLOCKING]` — kill during relink commit.
- `UX-P09-FLT-005 [P0][0.9][BLOCKING]` — kill during batch commit.
- `UX-P09-FLT-006 [P0][0.9][BLOCKING]` — kill during merge result registration.
- `UX-P09-FLT-007 [P1][0.9][REGRESSION]` — randomized SQLite busy/locked.
- `UX-P09-FLT-008 [P1][0.9][REGRESSION]` — ENOSPC during save/import/export/backup.
- `UX-P09-FLT-009 [P1][0.9][REGRESSION]` — permissions revoked mid-operation.
- `UX-P09-FLT-010 [P1][0.9][REGRESSION]` — source disappears mid-read.
- `UX-P09-FLT-011 [P1][0.9][REGRESSION]` — destination disappears mid-write.
- `UX-P09-FLT-012 [P1][0.9][REGRESSION]` — worker delay/out-of-order completions.
- `UX-P09-FLT-013 [P1][0.9][CHAOS]` — 100k random actions + fault injection with deterministic replay.
- `UX-P09-FLT-014 [P0][0.9][SOAK]` — 8-hour realistic session, catalog integrity preserved.

## Performance regression infrastructure

- `UX-P09-PERF-001 [P1][0.9][BLOCKING]` — checked-in machine-readable baseline schema.
- `UX-P09-PERF-002 [P1][0.9][PERF_LOCAL]` — startup regression tracking.
- `UX-P09-PERF-003 [P1][0.9][PERF_LOCAL]` — Library scroll regression tracking.
- `UX-P09-PERF-004 [P1][0.9][PERF_LOCAL]` — search/filter regression tracking.
- `UX-P09-PERF-005 [P1][0.9][PERF_LOCAL]` — photo switch regression tracking.
- `UX-P09-PERF-006 [P1][0.9][PERF_LOCAL]` — Develop slider regression tracking.
- `UX-P09-PERF-007 [P1][0.9][PERF_LOCAL]` — mask brush regression tracking.
- `UX-P09-PERF-008 [P1][0.9][PERF_LOCAL]` — export throughput regression tracking.
- `UX-P09-PERF-009 [P1][0.9][PERF_LOCAL]` — peak RSS regression tracking.
- `UX-P09-PERF-010 [P1][0.9][PERF_LOCAL]` — main-loop stall regression tracking.

### Release gate 0.9

0.9 блокируется невозможностью стабильно работать с заявленным максимальным catalog size, повреждением XMP/catalog/originals, backup без доказанного restore, небезопасным relink, linear memory growth и отсутствием воспроизводимого fault-injection/performance baseline.

---

# Версия 1.0.0 — Stable daily-use release

Roadmap scope:

```text
all supported workflows pass on supported hardware
local masks
presets
batch sync
export
restart
signing
notarization
release checksums
known issues
```

1.0 тестируется как конечный пользовательский продукт, а не как отдельная фича.

## Golden daily-use workflows

- `UX-P10-DAY-001 [P0][1.0][BLOCKING]` — fresh install → import RAW → Develop → Local Mask → preset → export → quit → reopen → exact saved state.
- `UX-P10-DAY-002 [P0][1.0][BLOCKING]` — fresh install → common image → edit → export → restart.
- `UX-P10-DAY-003 [P1][1.0][BLOCKING]` — 100-photo culling: import/rate/flag/filter/collection/develop/batch/export.
- `UX-P10-DAY-004 [P1][1.0][SOAK]` — realistic 500-photo, 2-hour photographer session.
- `UX-P10-DAY-005 [P1][1.0][SOAK]` — 8-hour mixed Library/Develop/export session.
- `UX-P10-DAY-006 [P1][1.0][SOAK]` — 24-hour idle/background/wake/reuse survival.
- `UX-P10-DAY-007 [P0][1.0][BLOCKING]` — unexpected kill after committed edits → safe restart.
- `UX-P10-DAY-008 [P0][1.0][BLOCKING]` — machine restart between sessions → no lost committed data.
- `UX-P10-DAY-009 [P1][1.0][BLOCKING]` — external volume missing → relink → continue editing.
- `UX-P10-DAY-010 [P0][1.0][BLOCKING]` — restore catalog backup → continue work.
- `UX-P10-DAY-011 [P1][1.0][BLOCKING]` — HDR/Panorama derived result participates in normal Library/Edit/Export.
- `UX-P10-DAY-012 [P1][1.0][BLOCKING]` — AI provider offline does not block non-AI daily use.
- `UX-P10-DAY-013 [P1][1.0][BLOCKING]` — language switch mid-project + restart.
- `UX-P10-DAY-014 [P1][1.0][BLOCKING]` — full core workflow keyboard-only.
- `UX-P10-DAY-015 [P0][1.0][BLOCKING]` — all original hashes unchanged after complete golden suite.

## Installation / signing / notarization

- `UX-P10-INS-001 [P0][1.0][BLOCKING]` — clean supported macOS installs from DMG.
- `UX-P10-INS-002 [P1][1.0][BLOCKING]` — drag-to-Applications workflow correct.
- `UX-P10-INS-003 [P0][1.0][BLOCKING]` — app bundle signed with expected release identity.
- `UX-P10-INS-004 [P0][1.0][BLOCKING]` — Gatekeeper assessment passes.
- `UX-P10-INS-005 [P0][1.0][BLOCKING]` — notarization/stapling valid when required by release design.
- `UX-P10-INS-006 [P1][1.0][REGRESSION]` — release runs without Qt SDK/Homebrew/dev toolchain.
- `UX-P10-INS-007 [P1][1.0][REGRESSION]` — version consistent in UI/Finder/bundle metadata.
- `UX-P10-INS-008 [P1][1.0][REGRESSION]` — app icon/DMG branding intact.
- `UX-P10-INS-009 [P1][1.0][REGRESSION]` — minimum supported macOS enforced gracefully.
- `UX-P10-INS-010 [P1][1.0][REGRESSION]` — unsupported architecture behavior explicit.

## Upgrade / schema migration

- `UX-P10-UPG-001 [P0][1.0][MIGRATION]` — upgrade latest supported previous release → 1.0.
- `UX-P10-UPG-002 [P0][1.0][MIGRATION]` — catalog migration preserves asset count/identity.
- `UX-P10-UPG-003 [P0][1.0][MIGRATION]` — edit migration preserves rendered output in tolerance.
- `UX-P10-UPG-004 [P1][1.0][MIGRATION]` — presets survive.
- `UX-P10-UPG-005 [P1][1.0][MIGRATION]` — local masks survive.
- `UX-P10-UPG-006 [P1][1.0][MIGRATION]` — collections/ratings/flags/labels survive.
- `UX-P10-UPG-007 [P1][1.0][MIGRATION]` — compatible export settings survive.
- `UX-P10-UPG-008 [P1][1.0][MIGRATION]` — XMP/metadata workflow remains consistent.
- `UX-P10-UPG-009 [P0][1.0][MIGRATION]` — migration failure rolls back or keeps pre-migration backup.
- `UX-P10-UPG-010 [P1][1.0][MIGRATION]` — unsupported newer schema refused safely by old reader.
- `UX-P10-UPG-011 [P0][1.0][BLOCKING]` — Sparkle update signature validation.
- `UX-P10-UPG-012 [P1][1.0][BLOCKING]` — update waits for save/import/export/batch/merge.
- `UX-P10-UPG-013 [P1][1.0][REGRESSION]` — canceled update does not damage installation.
- `UX-P10-UPG-014 [P0][1.0][REGRESSION]` — interrupted update recovery leaves valid old or new app.
- `UX-P10-UPG-015 [P1][1.0][REGRESSION]` — update restart restores session/catalog state.

## Release artifacts / provenance

- `UX-P10-REL-001 [P0][1.0][BLOCKING]` — SHA256SUMS matches every shipped artifact.
- `UX-P10-REL-002 [P0][1.0][BLOCKING]` — BUILD_INFO commit equals release tag/head.
- `UX-P10-REL-003 [P0][1.0][BLOCKING]` — appcast references exact published archive.
- `UX-P10-REL-004 [P0][1.0][BLOCKING]` — appcast/archive signatures validate.
- `UX-P10-REL-005 [P1][1.0][REGRESSION]` — DMG and ZIP contain same version/build.
- `UX-P10-REL-006 [P1][1.0][REGRESSION]` — release notes match actual capabilities.
- `UX-P10-REL-007 [P1][1.0][REGRESSION]` — known issues file present when there are accepted P2/P3 defects.
- `UX-P10-REL-008 [P1][1.0][REGRESSION]` — third-party licenses packaged.
- `UX-P10-REL-009 [P0][1.0][REGRESSION]` — no private test assets, secrets or signing material in release.
- `UX-P10-REL-010 [P0][1.0][BLOCKING]` — published artifacts byte-identical to CI-verified artifacts by hash.

## 1.0 data-safety invariants

После каждого 1.0 qualification run проверять:

```text
original SHA unchanged
PRAGMA integrity_check == ok
foreign_keys valid
no orphan edit children
no orphan masks
no orphan derived merge assets
no orphan temp export/import files
no abandoned import journal after recovery
backup opens/restores
relink identity valid
all persisted numeric values finite
no active background job after clean shutdown
```

- `UX-P10-DAT-001 [P0][1.0][BLOCKING]` — invariants after golden daily-use suite.
- `UX-P10-DAT-002 [P0][1.0][BLOCKING]` — invariants after chaos suite.
- `UX-P10-DAT-003 [P0][1.0][BLOCKING]` — invariants after fault-injection suite.
- `UX-P10-DAT-004 [P0][1.0][BLOCKING]` — invariants after migration.
- `UX-P10-DAT-005 [P0][1.0][BLOCKING]` — invariants after backup restore.

## Accessibility / minimum usability gate

- `UX-P10-A11Y-001 [P1][1.0][BLOCKING]` — keyboard-only golden workflow.
- `UX-P10-A11Y-002 [P1][1.0][BLOCKING]` — primary controls have meaningful AX names/roles.
- `UX-P10-A11Y-003 [P2][1.0][REGRESSION]` — visible focus on primary keyboard-reachable controls.
- `UX-P10-A11Y-004 [P2][1.0][REGRESSION]` — all supported localizations keep core controls reachable.
- `UX-P10-A11Y-005 [P2][1.0][REGRESSION]` — minimum window can complete core workflow.
- `UX-P10-A11Y-006 [P1][1.0][REGRESSION]` — modal/focus stack never traps user.

## 1.0 performance regression gate

Обязательные tracked metrics:

```text
startup
Library open 10k / 100k / 500k
grid scroll
search/filter
cached photo switch
initial RAW preview
slider first-feedback
slider final-quality
mask brush latency
crop/transform latency
AI inference where enabled
HDR/Panorama merge
export throughput
peak RSS
main-loop max stall
```

- `UX-P10-PERF-001 [P1][1.0][PERF_LOCAL]` — critical p95 interaction regression <= approved threshold (default 25%).
- `UX-P10-PERF-002 [P1][1.0][PERF_LOCAL]` — no new main-thread stall >1 s in golden workflow.
- `UX-P10-PERF-003 [P1][1.0][PERF_LOCAL]` — scroll jank within approved budget.
- `UX-P10-PERF-004 [P1][1.0][PERF_LOCAL]` — peak RSS budget on 8 GB reference Mac.
- `UX-P10-PERF-005 [P1][1.0][PERF_LOCAL]` — peak RSS budget on 16 GB reference Mac.
- `UX-P10-PERF-006 [P1][1.0][SOAK]` — no linear RSS growth over 8-hour session.

## 1.0 chaos / soak qualification

Минимальная cumulative qualification цель:

```text
1,000,000 random legal UI actions
100,000 actions with async completion randomization
10,000 actions with filesystem fault injection
8-hour realistic editing soak
8-hour large-Library soak
100 cold launches
100 update/restart cycles
```

- `UX-P10-CHA-001 [P0][1.0][CHAOS]` — no P0 in 1M cumulative actions.
- `UX-P10-CHA-002 [P1][1.0][CHAOS]` — every P1 has seed/replay and is reproducible.
- `UX-P10-CHA-003 [P0][1.0][CHAOS]` — data safety invariants after chaos.
- `UX-P10-SOK-001 [P1][1.0][SOAK]` — 8-hour editing session.
- `UX-P10-SOK-002 [P1][1.0][SOAK]` — 8-hour Library session.
- `UX-P10-SOK-003 [P1][1.0][SOAK]` — overnight idle/sleep/wake/reuse.
- `UX-P10-SOK-004 [P1][1.0][SOAK]` — repeated import/edit/export cycles.

## Final 1.0 release acceptance

Версия 1.0.0 может считаться Stable только при:

```text
P0 = 0
P1 blocking = 0
catalog integrity failures = 0
original modifications = 0
unreproducible crashes = 0
hard hangs = 0
migration data-loss = 0
backup restore failures = 0
privacy violations = 0
signature/notarization/checksum failures = 0
```

P2 допускаются только если проблема документирована в known issues/post-1.0 backlog, не рискует данными, не блокирует core workflow и имеет воспроизводящий regression test.

---

# Сквозная матрица regression по фазам

```text
0.5 candidate = 0.1–0.5 + Local Adjustments
0.6 candidate = всё выше + Performance
0.7 candidate = всё выше + Advanced
0.8 candidate = всё выше + Merge
0.9 candidate = всё выше + Stabilization
1.0 candidate = FULL SUITE + release qualification
```

Нельзя закрывать новую фазу ценой регрессии предыдущей.

Пример:

```text
Metal ускорил preview на 60%,
но Crop pointer mapping перестал совпадать после zoom.
Результат: 0.6 FAIL.
```

---

# Phase-aware запуск Codex

Codex должен поддержать команды вида:

```bash
python3 tests/ux/run_ux_suite.py --through 0.5
python3 tests/ux/run_ux_suite.py --through 0.6
python3 tests/ux/run_ux_suite.py --through 0.7
python3 tests/ux/run_ux_suite.py --through 0.8
python3 tests/ux/run_ux_suite.py --through 0.9
python3 tests/ux/run_ux_suite.py --through 1.0
```

Только новая фаза:

```bash
python3 tests/ux/run_ux_suite.py --phase 0.8
```

Финальная квалификация 1.0:

```bash
python3 tests/ux/run_ux_suite.py \
  --through 1.0 \
  --release-qualification \
  --artifacts build/ux-artifacts \
  --report build/ux-report
```

---

# Дополнение к ROADMAP_CANDIDATES

Функции, уже закреплённые в roadmap, больше не считать абстрактными TARGET gaps:

```text
Metal / progressive rendering / true 1:1        → 0.6
AI denoise / AI masks / removal / semantic      → 0.7
HDR / Panorama / alignment / deghost / float    → 0.8
500k / XMP / backup / relink / recovery         → 0.9
signing / notarization / stable daily use        → 1.0
```

В generic post-roadmap gaps оставлять только возможности, которые не закреплены в текущем roadmap: например persistent History, Versions/Snapshots, smart collections, customizable shortcuts, histogram interaction, before/after split, multi-monitor secondary view и прочие будущие продуктовые направления.

---

# Дополнение к Definition of Done тестового контура

К исходному DoD добавить:

1. 0.6–1.0 suites автоматически включаются по capability/phase manifest.
2. CPU/GPU comparison имеет явную numerical tolerance и reference fixtures.
3. privacy suite может доказать отсутствие network calls в local-only mode.
4. AI/merge asynchronous results имеют asset+revision guards.
5. 10k/100k/500k catalog fixtures воспроизводимы.
6. Backup считается протестированным только после успешного restore и integrity-check.
7. Relink проверяет identity, а не только filename/path.
8. Migration suite использует сохранённые fixtures реальных предыдущих schema versions.
9. 1.0 release artifact проходит signing/notarization/checksum/provenance validation.
10. Все предыдущие phase-blocking tests остаются обязательными для следующих релизов.
