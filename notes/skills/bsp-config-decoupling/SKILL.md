---
name: bsp-config-decoupling
description: >
  STM32/GD32 BSP peripheral driver decoupling skill based on a centralized
  bsp_config.h pattern. Use when: (1) adding a new BSP peripheral driver
  module, (2) refactoring existing BSP drivers to the centralized config
  pattern, (3) enabling/disabling BSP modules via compile-time switches,
  (4) adding or removing RT-Thread RTOS dependencies in BSP code,
  (5) reviewing BSP include-chain hygiene. Covers the ResistorTactileBoard
  firmware BSP layer (Drivers/BSP) but generalizes to any STM32 HAL +
  RT-Thread Nano project.
---

# BSP Config Decoupling Skill

Centralized `bsp_config.h` pattern for STM32/GD32 + RT-Thread Nano BSP layers.

## Architecture

```
bsp_config.h          <-- single config entry point
  |-- #include <rtthread.h>        (ifdef BSP_USE_RTTHREAD)
  |-- #include "bsp_led.h"         (ifdef BSP_USE_STATUS_LED)
  |-- #include "drv_74hc595.h"     (ifdef BSP_USE_74HC595)
  |-- #include "drv_ads8681.h"     (ifdef BSP_USE_ADS8681)
  \-- ... new modules here ...

bsp_init.h  -->  #include "bsp_config.h"
             + extern handles   (per-module #ifdef guards)

bsp_init.c  -->  per-module init calls  (per-module #ifdef guards)

Driver .h   -->  #include "bsp_config.h"  (never include <rtthread.h> directly)
Driver .c   -->  #include own .h only; RTT code inside #ifdef BSP_USE_RTTHREAD
```

## Macro Convention

Follow RT-Thread `rtconfig.h` style -- **define-or-comment-out**, no numeric values:

```c
/* Enable: */
#define BSP_USE_STATUS_LED

/* Disable: comment out */
/* #define BSP_USE_STATUS_LED */
```

Guard with `#ifdef` / `#ifndef`, never `#if MACRO == 1`:

```c
#ifdef BSP_USE_STATUS_LED
  #include "bsp_led.h"
#endif
```

This keeps a single standard across `rtconfig.h` and `bsp_config.h`.

## Rules

1. **One config file.** All `BSP_USE_*` enable macros and `BSP_USE_RTTHREAD`
   live in `bsp_config.h`. No other file defines these macros.

2. **Define-or-comment-out.** Enable = `#define BSP_USE_XXX` (no value).
   Disable = comment out the line. Guard with `#ifdef` / `#ifndef`.

3. **Conditional includes.** `bsp_config.h` includes each driver header inside
   `#ifdef BSP_USE_<MODULE>`. Disabled modules are fully excluded.

4. **RTT header only in bsp_config.h.** Driver headers that need RT-Thread types
   (e.g. `rt_err_t`) include `"bsp_config.h"` instead of `<rtthread.h>`.
   RTT-dependent APIs in driver headers are wrapped in `#ifdef BSP_USE_RTTHREAD`.

5. **Init guarded by macro.** `bsp_init.c` calls each module's init inside
   `#ifdef BSP_USE_<MODULE>`. Modules requiring a handle define it in
   `bsp_init.c` and expose it via `extern` in `bsp_init.h`.

6. **Drivers stay self-contained.** `drv_*.h` / `drv_*.c` do NOT include
   `bsp_config.h` unless they need RTT types. Pure HAL drivers keep their
   own includes and are unaware of the config layer.

7. **Include guard safety.** Circular includes (bsp_config.h <-> bsp_led.h)
   are safe because bsp_config.h includes `<rtthread.h>` BEFORE including
   driver headers, so RTT types are always available when the driver header
   body is parsed.

## Workflow: Add a New BSP Module

Given a new peripheral `FOO` with driver files `drv_foo.h` / `drv_foo.c`:

### Step 1 -- bsp_config.h

Add enable macro:

```c
/** @brief FOO peripheral module (brief description) */
#define BSP_USE_FOO
```

Add conditional include at the bottom of the include block:

```c
#ifdef BSP_USE_FOO
  #include "drv_foo.h"
#endif
```

If the module has compile-time parameters, define them guarded by `#ifdef`:

```c
#ifdef BSP_USE_FOO
  #define BSP_FOO_DEFAULT_XXX   0x00
#endif
```

### Step 2 -- bsp_init.h

If the module exposes a global handle:

```c
#ifdef BSP_USE_FOO
extern FOO_HandleTypeDef g_bsp_foo;
#endif
```

### Step 3 -- bsp_init.c

Add handle definition + init call:

```c
#ifdef BSP_USE_FOO
  #include "pin_def.h"
  #include "spi.h"
#endif

#ifdef BSP_USE_FOO
FOO_HandleTypeDef g_bsp_foo;
#endif

/* inside bsp_init(): */
#ifdef BSP_USE_FOO
    g_bsp_foo.field = value;
    FOO_Init(&g_bsp_foo);
#endif
```

### Step 4 -- Driver files

Use templates in `templates/` folder:

- `drv_template.h` -- header with platform abstraction + optional RTT guard
- `drv_template.c` -- source with `#ifdef BSP_USE_RTTHREAD` guarded RTT code

If the driver uses RTT types in its public API:

```c
/* in drv_foo.h */
#include "bsp_config.h"   /* instead of <rtthread.h> */

#ifdef BSP_USE_RTTHREAD
rt_err_t FOO_DoSomethingAsync(...);
#endif
```

### Step 5 -- Application layer

Remove private handles / init functions from app code.
Use `g_bsp_foo` from `bsp_init.h` instead.
Remove redundant `#include "drv_foo.h"` if already obtained via
`bsp_config.h` chain (app.h -> basic_service.h -> bsp_init.h -> bsp_config.h).

### Step 6 -- Verify

- [ ] No direct `#include <rtthread.h>` in `Drivers/BSP/` except `bsp_config.h`
- [ ] No stale private handles or init functions in app code
- [ ] Commenting out `#define BSP_USE_FOO` compiles cleanly (module fully excluded)
- [ ] Commenting out `#define BSP_USE_RTTHREAD` compiles cleanly (only GPIO APIs remain)
- [ ] All guards use `#ifdef` / `#ifndef`, no `#if MACRO == 1`

## Workflow: Disable / Remove a Module

1. Comment out `#define BSP_USE_<MODULE>` in `bsp_config.h`.
2. Verify compilation. No other file changes needed.
3. To fully remove: delete the macro, the `#ifdef` include block, the init
   guard in `bsp_init.c`, and the extern in `bsp_init.h`.

## File Naming Conventions

| Type | Pattern | Example |
|------|---------|---------|
| Config | `bsp_config.h` | (unique) |
| Init | `bsp_init.h` / `bsp_init.c` | (unique) |
| Pin map | `pin_def.h` | (unique) |
| Driver header | `drv_<chip>.h` | `drv_74hc595.h` |
| Driver source | `drv_<chip>.c` | `drv_ads8681.c` |
| LED / simple GPIO | `bsp_<function>.h/.c` | `bsp_led.h` |
| Platform port | `drv_<chip>_port_<platform>.h` | `drv_ads8681_port_stm32.h` |

## Templates

See `templates/` folder:

| File | Purpose |
|------|---------|
| `drv_template.h` | New driver header (HAL + optional RTT) |
| `drv_template.c` | New driver source (HAL + optional RTT thread) |
| `bsp_config_snippet.txt` | Lines to add into bsp_config.h for a new module |
| `bsp_init_snippet.txt` | Lines to add into bsp_init.h / bsp_init.c for a new module |
