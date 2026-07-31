/**
 * @file servo_test.c
 * @brief Right-arm servo bring-up diagnostic.
 *
 * Answers four questions in order, so the serial log alone identifies which
 * layer is broken:
 *
 *   Stage 1  Can the pin be driven at all?       static high/low with readback
 *   Stage 2  Does the servo respond on that pin? bit-banged 50 Hz frame
 *   Stage 3  Which TUYA_PWM_NUM_* reaches it?    every channel swept in turn
 *   Stage 4  Is the pulse polarity inverted?     one channel, both polarities
 *
 * Stage 3 drives a single channel at a time and never touches the left arm, so
 * neither a shared-supply brownout nor a second channel on the same PWM timer
 * can mask the result.
 *
 * Channel-to-pin expectations below come from ty_to_bk_pwm() in tkl_pwm.c
 * combined with GPIO_PWM_MAP_TABLE: TUYA_PWM_NUM_* is not the hardware PWM
 * index, e.g. NUM_1 lands on hardware PWM4 which is P24.
 */

#include "servo_test.h"
#include "servo_pwm.h"
#include "tal_api.h"
#include "tkl_pwm.h"
#include "tkl_gpio.h"
#include "tkl_system.h"

#if defined(ENABLE_SERVO_TEST) && (ENABLE_SERVO_TEST == 1)

#define ST_FREQ_HZ      50
#define ST_FRAME_MS     20
#define ST_SETTLE_MS    600

typedef struct {
    uint8_t num;  /* TUYA_PWM_NUM_x value written to SERVO_RIGHT_PWM */
    uint8_t gpio; /* pin that channel should reach */
} ST_PWM_PIN_T;

/* NUM_11 is rejected by tkl_pwm.c (TUYA_PWM_ID_MAX is 11), so it is left out. */
static const ST_PWM_PIN_T sg_pwm_pins[] = {
    {0, 18}, {1, 24}, {2, 32}, {3, 34}, {4, 36}, {5, 19},
    {6, 8},  {7, 9},  {8, 25}, {9, 33}, {10, 35},
};

static THREAD_HANDLE sg_test_thrd = NULL;
static uint32_t sg_spin_per_us = 0;

/* ---------------- microsecond busy-wait ---------------- */

/* tkl_system_sleep_us() is an empty stub on T5AI, so Stage 2 needs its own. */
static void __spin(uint32_t loops)
{
    volatile uint32_t i;

    for (i = 0; i < loops; i++) {
        ;
    }
}

/* Takes the fastest of several probes; slower ones were preempted. */
static void __calib_spin(void)
{
    const uint32_t probe = 1000000;
    uint32_t best = 0xFFFFFFFFu;
    int k;

    for (k = 0; k < 5; k++) {
        uint32_t t0 = (uint32_t)tal_system_get_millisecond();
        uint32_t ms;

        __spin(probe);
        ms = (uint32_t)tal_system_get_millisecond() - t0;
        if (ms > 0 && ms < best) {
            best = ms;
        }
    }

    if (best == 0xFFFFFFFFu) {
        best = 1;
    }
    sg_spin_per_us = probe / (best * 1000);
    if (sg_spin_per_us == 0) {
        sg_spin_per_us = 1;
    }
    PR_NOTICE("[servotest] spin calib: %u loops in %u ms -> %u loops/us", probe, best,
              sg_spin_per_us);
}

/* ---------------- stage 1: static pin level ---------------- */

static void __pin_out(TUYA_GPIO_NUM_E pin, TUYA_GPIO_LEVEL_E level)
{
    TUYA_GPIO_BASE_CFG_T cfg = {
        .direct = TUYA_GPIO_OUTPUT,
        .mode = TUYA_GPIO_PUSH_PULL,
        .level = level,
    };

    tkl_gpio_init(pin, &cfg);
    tkl_gpio_write(pin, level);
}

static uint8_t __pin_readback(TUYA_GPIO_NUM_E pin)
{
    TUYA_GPIO_LEVEL_E lv = TUYA_GPIO_LEVEL_LOW;

    tkl_gpio_read(pin, &lv);
    return (lv == TUYA_GPIO_LEVEL_HIGH) ? 1 : 0;
}

static void __stage1_level(TUYA_GPIO_NUM_E pin)
{
    uint8_t rb_hi, rb_lo;

    PR_NOTICE("[servotest] --- STAGE 1: static level on P%d (probe P%d against GND) ---",
              (int)pin, (int)pin);

    __pin_out(pin, TUYA_GPIO_LEVEL_HIGH);
    rb_hi = __pin_readback(pin);
    PR_NOTICE("[servotest] S1 P%d HIGH for 3s, readback=%u (expect 1, ~3.3V)", (int)pin, rb_hi);
    tal_system_sleep(3000);

    __pin_out(pin, TUYA_GPIO_LEVEL_LOW);
    rb_lo = __pin_readback(pin);
    PR_NOTICE("[servotest] S1 P%d LOW for 3s, readback=%u (expect 0, ~0V)", (int)pin, rb_lo);
    tal_system_sleep(3000);

    /* The T5AI GPIO table sets GPIO_IO_DISABLE on these pins, so the input latch
     * is off and a readback while driving is not trustworthy. A stuck readback
     * therefore proves nothing on its own; only the multimeter does. */
    if (rb_hi == 1 && rb_lo == 0) {
        PR_NOTICE("[servotest] S1 readback tracks the drive (hi=%u lo=%u), pin is definitely live",
                  rb_hi, rb_lo);
    } else {
        PR_WARN("[servotest] S1 readback did not track (hi=%u lo=%u). Inconclusive: input latch is "
                "disabled on this pin, so trust the multimeter and stage 2, not this number.",
                rb_hi, rb_lo);
    }
}

/* ---------------- stage 2: bit-banged servo frame ---------------- */

/* Interrupts are masked for the pulse only; the 18-19 ms gap stays schedulable. */
static void __bitbang(TUYA_GPIO_NUM_E pin, uint32_t pulse_us, uint32_t total_ms)
{
    uint32_t frames = total_ms / ST_FRAME_MS;
    uint32_t gap_ms = ST_FRAME_MS - (pulse_us + 999) / 1000;
    uint32_t i;

    for (i = 0; i < frames; i++) {
        uint32_t irq = tkl_system_enter_critical();

        tkl_gpio_write(pin, TUYA_GPIO_LEVEL_HIGH);
        __spin(pulse_us * sg_spin_per_us);
        tkl_gpio_write(pin, TUYA_GPIO_LEVEL_LOW);
        tkl_system_exit_critical(irq);

        tal_system_sleep(gap_ms);
    }
}

static void __stage2_bitbang(TUYA_GPIO_NUM_E pin)
{
    PR_NOTICE("[servotest] --- STAGE 2: bit-banged 50Hz frame on P%d (watch the arm) ---",
              (int)pin);

    __pin_out(pin, TUYA_GPIO_LEVEL_LOW);
    __calib_spin();

    PR_NOTICE("[servotest] S2 1500us / 90deg centre");
    __bitbang(pin, 1500, 1500);
    PR_NOTICE("[servotest] S2 1000us / 0deg");
    __bitbang(pin, 1000, 1500);
    PR_NOTICE("[servotest] S2 2000us / 180deg");
    __bitbang(pin, 2000, 1500);
    PR_NOTICE("[servotest] S2 1500us / 90deg centre");
    __bitbang(pin, 1500, 1500);

    PR_NOTICE("[servotest] S2 done. If the arm MOVED here the wiring, supply and pin are fine "
              "and the fault is in the PWM channel mapping. If it stayed dead, suspect the "
              "servo, its 5V supply or the ground return.");
}

/* ---------------- stage 3/4: PWM channels ---------------- */

/* Same 0.5-2.5 ms mapping servo_pwm.c uses, in 1/10000 of the period. */
static uint32_t __angle_duty(uint8_t angle)
{
    return (uint32_t)((0.5 + (double)angle / 180.0 * 2.0) * 10000.0 / 20.0);
}

static bool __pwm_sweep(uint8_t num, TUYA_PWM_POLARITY_E pol)
{
    static const uint8_t angles[] = {90, 0, 90, 180, 90};
    TUYA_PWM_BASE_CFG_T cfg = {
        .duty = __angle_duty(90),
        .frequency = ST_FREQ_HZ,
        .polarity = pol,
    };
    OPERATE_RET rt;
    int i;

    rt = tkl_pwm_init((TUYA_PWM_NUM_E)num, &cfg);
    if (rt != OPRT_OK) {
        PR_ERR("[servotest] NUM_%u init failed rt=%d", num, rt);
        return false;
    }

    rt = tkl_pwm_start((TUYA_PWM_NUM_E)num);
    if (rt != OPRT_OK) {
        PR_ERR("[servotest] NUM_%u start failed rt=%d", num, rt);
        tkl_pwm_deinit((TUYA_PWM_NUM_E)num);
        return false;
    }

    for (i = 0; i < (int)(sizeof(angles) / sizeof(angles[0])); i++) {
        tkl_pwm_duty_set((TUYA_PWM_NUM_E)num, __angle_duty(angles[i]));
        tal_system_sleep(ST_SETTLE_MS);
    }

    tkl_pwm_stop((TUYA_PWM_NUM_E)num);
    tkl_pwm_deinit((TUYA_PWM_NUM_E)num);
    return true;
}

static void __stage3_scan(TUYA_GPIO_NUM_E pin)
{
    int i;

    PR_NOTICE("[servotest] --- STAGE 3: sweeping every PWM channel, one at a time ---");
    PR_NOTICE("[servotest] S3 the arm on P%d should move on exactly one channel; that "
              "channel number is the correct SERVO_RIGHT_PWM value",
              (int)pin);

    for (i = 0; i < (int)(sizeof(sg_pwm_pins) / sizeof(sg_pwm_pins[0])); i++) {
        PR_NOTICE("[servotest] S3 >>> now driving TUYA_PWM_NUM_%u, expected pin P%d%s",
                  sg_pwm_pins[i].num, (int)sg_pwm_pins[i].gpio,
                  (sg_pwm_pins[i].gpio == (uint8_t)pin) ? "  <== THIS ONE should move the arm"
                                                        : "");
        __pwm_sweep(sg_pwm_pins[i].num, TUYA_PWM_POSITIVE);
        tal_system_sleep(400);
    }
    PR_NOTICE("[servotest] S3 done");
}

static void __stage4_polarity(void)
{
    uint8_t num = (uint8_t)SERVO_RIGHT_PWM;

    PR_NOTICE("[servotest] --- STAGE 4: polarity check on the configured channel NUM_%u ---",
              num);

    PR_NOTICE("[servotest] S4 POSITIVE polarity (pulse is the high time)");
    __pwm_sweep(num, TUYA_PWM_POSITIVE);
    tal_system_sleep(500);

    PR_NOTICE("[servotest] S4 NEGATIVE polarity (current firmware setting)");
    __pwm_sweep(num, TUYA_PWM_NEGATIVE);

    PR_NOTICE("[servotest] S4 done. If only one polarity moved the arm, servo_pwm_init() must "
              "use that one.");
}

static void __test_task(void *arg)
{
    TUYA_GPIO_NUM_E pin = (TUYA_GPIO_NUM_E)SERVO_TEST_PIN;
    THREAD_HANDLE self;

    (void)arg;

    tal_system_sleep(4000); /* let boot logs drain first */
    self = sg_test_thrd;

    PR_NOTICE("[servotest] ===== RIGHT ARM SERVO DIAGNOSTIC =====");
    PR_NOTICE("[servotest] wiring: right servo signal -> P%d, servo V+ -> 5V, servo GND -> "
              "board GND (shared ground is mandatory)",
              (int)pin);
    PR_NOTICE("[servotest] left arm stays idle for the whole run; build config says "
              "SERVO_LEFT_PWM=%d SERVO_RIGHT_PWM=%d",
              (int)SERVO_LEFT_PWM, (int)SERVO_RIGHT_PWM);

    __stage1_level(pin);
    __stage2_bitbang(pin);
    __stage3_scan(pin);
    __stage4_polarity();

    PR_NOTICE("[servotest] ===== DIAGNOSTIC COMPLETE, restoring normal servo driver =====");
    servo_pwm_init();

    sg_test_thrd = NULL;
    tal_thread_delete(self); /* marks STOP; the tal wrapper reclaims us on return */
}

#endif /* ENABLE_SERVO_TEST */

OPERATE_RET servo_test_start(void)
{
#if defined(ENABLE_SERVO_TEST) && (ENABLE_SERVO_TEST == 1)
    THREAD_CFG_T cfg = {
        .stackDepth = 4 * 1024,
        .priority = THREAD_PRIO_2,
        .thrdname = "servo_test",
    };

    return tal_thread_create_and_start(&sg_test_thrd, NULL, NULL, __test_task, NULL, &cfg);
#else
    return OPRT_OK;
#endif
}
