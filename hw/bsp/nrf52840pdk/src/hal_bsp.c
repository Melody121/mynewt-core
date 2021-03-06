/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <nrf52840.h>
#include "os/os_cputime.h"
#include "syscfg/syscfg.h"
#include "sysflash/sysflash.h"
#include "flash_map/flash_map.h"
#include "hal/hal_bsp.h"
#include "hal/hal_system.h"
#include "hal/hal_flash.h"
#include "hal/hal_spi.h"
#include "hal/hal_watchdog.h"
#include "hal/hal_i2c.h"
#include "mcu/nrf52_hal.h"
#include "uart/uart.h"
#include "uart_hal/uart_hal.h"
#include "os/os_dev.h"
#include "bsp.h"
#if MYNEWT_VAL(ADC_0)
#include <adc_nrf52/adc_nrf52.h>
#include <nrfx_saadc.h>
#endif
#if MYNEWT_VAL(PWM_0) || \
    MYNEWT_VAL(PWM_1) || \
    MYNEWT_VAL(PWM_2) || \
    MYNEWT_VAL(PWM_3)
#include <pwm_nrf52/pwm_nrf52.h>
#endif
#if MYNEWT_VAL(SOFT_PWM)
#include <soft_pwm/soft_pwm.h>
#endif

#if MYNEWT_VAL(UART_0)
static struct uart_dev os_bsp_uart0;
static const struct nrf52_uart_cfg os_bsp_uart0_cfg = {
    .suc_pin_tx = MYNEWT_VAL(UART_0_PIN_TX),
    .suc_pin_rx = MYNEWT_VAL(UART_0_PIN_RX),
    .suc_pin_rts = MYNEWT_VAL(UART_0_PIN_RTS),
    .suc_pin_cts = MYNEWT_VAL(UART_0_PIN_CTS),
};
#endif

#if MYNEWT_VAL(UART_1)
static struct uart_dev os_bsp_bitbang_uart1;
static const struct uart_bitbang_conf os_bsp_uart1_cfg = {
    .ubc_txpin = MYNEWT_VAL(UART_1_PIN_TX),
    .ubc_rxpin = MYNEWT_VAL(UART_1_PIN_RX),
    .ubc_cputimer_freq = MYNEWT_VAL(OS_CPUTIME_FREQ),
};
#endif

#if MYNEWT_VAL(SPI_0_MASTER)
/*
 * NOTE: Our HAL expects that the SS pin, if used, is treated as a gpio line
 * and is handled outside the SPI routines.
 */
static const struct nrf52_hal_spi_cfg os_bsp_spi0m_cfg = {
    .sck_pin      = MYNEWT_VAL(SPI_0_MASTER_PIN_SCK),
    .mosi_pin     = MYNEWT_VAL(SPI_0_MASTER_PIN_MOSI),
    .miso_pin     = MYNEWT_VAL(SPI_0_MASTER_PIN_MISO),
};
#endif

#if MYNEWT_VAL(SPI_0_SLAVE)
static const struct nrf52_hal_spi_cfg os_bsp_spi0s_cfg = {
    .sck_pin      = MYNEWT_VAL(SPI_0_SLAVE_PIN_SCK),
    .mosi_pin     = MYNEWT_VAL(SPI_0_SLAVE_PIN_MOSI),
    .miso_pin     = MYNEWT_VAL(SPI_0_SLAVE_PIN_MISO),
    .ss_pin       = MYNEWT_VAL(SPI_0_SLAVE_PIN_SS),
};
#endif

#if MYNEWT_VAL(ADC_0)
static struct adc_dev os_bsp_adc0;
static nrfx_saadc_config_t os_bsp_adc0_config = {
    .resolution         = MYNEWT_VAL(ADC_0_RESOLUTION),
    .oversample         = MYNEWT_VAL(ADC_0_OVERSAMPLE),
    .interrupt_priority = MYNEWT_VAL(ADC_0_INTERRUPT_PRIORITY),
};
#endif

#if MYNEWT_VAL(PWM_0)
static struct pwm_dev os_bsp_pwm0;
int pwm0_idx;
#endif
#if MYNEWT_VAL(PWM_1)
static struct pwm_dev os_bsp_pwm1;
int pwm1_idx;
#endif
#if MYNEWT_VAL(PWM_2)
static struct pwm_dev os_bsp_pwm2;
int pwm2_idx;
#endif
#if MYNEWT_VAL(PWM_3)
static struct pwm_dev os_bsp_pwm2;
int pwm3_idx;
#endif
#if MYNEWT_VAL(SOFT_PWM)
static struct pwm_dev os_bsp_spwm;
#endif

#if MYNEWT_VAL(I2C_0)
static const struct nrf52_hal_i2c_cfg hal_i2c0_cfg = {
    .scl_pin = MYNEWT_VAL(I2C_0_PIN_SCL),
    .sda_pin = MYNEWT_VAL(I2C_0_PIN_SDA),
    .i2c_frequency = MYNEWT_VAL(I2C_0_FREQ_KHZ),
};
#endif

/*
 * What memory to include in coredump.
 */
static const struct hal_bsp_mem_dump dump_cfg[] = {
    [0] = {
        .hbmd_start = &_ram_start,
        .hbmd_size = RAM_SIZE
    }
};

const struct hal_flash *
hal_bsp_flash_dev(uint8_t id)
{
    /*
     * Internal flash mapped to id 0.
     */
    if (id != 0) {
        return NULL;
    }
    return &nrf52k_flash_dev;
}

const struct hal_bsp_mem_dump *
hal_bsp_core_dump(int *area_cnt)
{
    *area_cnt = sizeof(dump_cfg) / sizeof(dump_cfg[0]);
    return dump_cfg;
}

int
hal_bsp_power_state(int state)
{
    return (0);
}

/**
 * Returns the configured priority for the given interrupt. If no priority
 * configured, return the priority passed in
 *
 * @param irq_num
 * @param pri
 *
 * @return uint32_t
 */
uint32_t
hal_bsp_get_nvic_priority(int irq_num, uint32_t pri)
{
    uint32_t cfg_pri;

    switch (irq_num) {
    /* Radio gets highest priority */
    case RADIO_IRQn:
        cfg_pri = 0;
        break;
    default:
        cfg_pri = pri;
    }
    return cfg_pri;
}

void
hal_bsp_init(void)
{
    int rc;

    /* Make sure system clocks have started */
    hal_system_clock_start();

#if MYNEWT_VAL(TIMER_0)
    rc = hal_timer_init(0, NULL);
    assert(rc == 0);
#endif
#if MYNEWT_VAL(TIMER_1)
    rc = hal_timer_init(1, NULL);
    assert(rc == 0);
#endif
#if MYNEWT_VAL(TIMER_2)
    rc = hal_timer_init(2, NULL);
    assert(rc == 0);
#endif
#if MYNEWT_VAL(TIMER_3)
    rc = hal_timer_init(3, NULL);
    assert(rc == 0);
#endif
#if MYNEWT_VAL(TIMER_4)
    rc = hal_timer_init(4, NULL);
    assert(rc == 0);
#endif
#if MYNEWT_VAL(TIMER_5)
    rc = hal_timer_init(5, NULL);
    assert(rc == 0);
#endif

#if MYNEWT_VAL(ADC_0)
rc = os_dev_create((struct os_dev *) &os_bsp_adc0,
                   "adc0",
                   OS_DEV_INIT_KERNEL,
                   OS_DEV_INIT_PRIO_DEFAULT,
                   nrf52_adc_dev_init,
                   &os_bsp_adc0_config);
assert(rc == 0);
#endif

#if MYNEWT_VAL(PWM_0)
    pwm0_idx = 0;
    rc = os_dev_create((struct os_dev *) &os_bsp_pwm0,
                       "pwm0",
                       OS_DEV_INIT_KERNEL,
                       OS_DEV_INIT_PRIO_DEFAULT,
                       nrf52_pwm_dev_init,
                       &pwm0_idx);
    assert(rc == 0);
#endif
#if MYNEWT_VAL(PWM_1)
    pwm1_idx = 1;
    rc = os_dev_create((struct os_dev *) &os_bsp_pwm1,
                       "pwm1",
                       OS_DEV_INIT_KERNEL,
                       OS_DEV_INIT_PRIO_DEFAULT,
                       nrf52_pwm_dev_init,
                       &pwm1_idx);
    assert(rc == 0);
#endif
#if MYNEWT_VAL(PWM_2)
    pwm2_idx = 2;
    rc = os_dev_create((struct os_dev *) &os_bsp_pwm2,
                       "pwm2",
                       OS_DEV_INIT_KERNEL,
                       OS_DEV_INIT_PRIO_DEFAULT,
                       nrf52_pwm_dev_init,
                       &pwm2_idx);
    assert(rc == 0);
#endif
#if MYNEWT_VAL(PWM_3)
    pwm3_idx = 3;
    rc = os_dev_create((struct os_dev *) &os_bsp_pwm3,
                       "pwm2",
                       OS_DEV_INIT_KERNEL,
                       OS_DEV_INIT_PRIO_DEFAULT,
                       nrf52_pwm_dev_init,
                       &pwm3_idx);
    assert(rc == 0);
#endif
#if MYNEWT_VAL(SOFT_PWM)
    rc = os_dev_create((struct os_dev *) &os_bsp_spwm,
                       "spwm",
                       OS_DEV_INIT_KERNEL,
                       OS_DEV_INIT_PRIO_DEFAULT,
                       soft_pwm_dev_init,
                       NULL);
    assert(rc == 0);
#endif

#if (MYNEWT_VAL(OS_CPUTIME_TIMER_NUM) >= 0)
    rc = os_cputime_init(MYNEWT_VAL(OS_CPUTIME_FREQ));
    assert(rc == 0);
#endif

#if MYNEWT_VAL(I2C_0)
    rc = hal_i2c_init(0, (void *)&hal_i2c0_cfg);
    assert(rc == 0);
#endif

#if MYNEWT_VAL(SPI_0_MASTER)
    rc = hal_spi_init(0, (void *)&os_bsp_spi0m_cfg, HAL_SPI_TYPE_MASTER);
    assert(rc == 0);
#endif

#if MYNEWT_VAL(SPI_0_SLAVE)
    rc = hal_spi_init(0, (void *)&os_bsp_spi0s_cfg, HAL_SPI_TYPE_SLAVE);
    assert(rc == 0);
#endif

#if MYNEWT_VAL(UART_0)
    rc = os_dev_create((struct os_dev *) &os_bsp_uart0, "uart0",
      OS_DEV_INIT_PRIMARY, 0, uart_hal_init, (void *)&os_bsp_uart0_cfg);
    assert(rc == 0);
#endif

#if MYNEWT_VAL(UART_1)
    rc = os_dev_create((struct os_dev *) &os_bsp_bitbang_uart1, "uart1",
      OS_DEV_INIT_PRIMARY, 0, uart_bitbang_init, (void *)&os_bsp_uart1_cfg);
    assert(rc == 0);
#endif

}

#if MYNEWT_VAL(BSP_USE_HAL_SPI)
void
bsp_spi_read_buf(uint8_t addr, uint8_t *buf, uint8_t size)
{
    int i;
    uint8_t rxval;
    NRF_SPI_Type *spi;
    spi = NRF_SPI0;

    if (size == 0) {
        return;
    }

    i = -1;
    spi->EVENTS_READY = 0;
    spi->TXD = (uint8_t)addr;
    while (size != 0) {
        spi->TXD = 0;
        while (!spi->EVENTS_READY) {}
        spi->EVENTS_READY = 0;
        rxval = (uint8_t)(spi->RXD);
        if (i >= 0) {
            buf[i] = rxval;
        }
        size -= 1;
        ++i;
        if (size == 0) {
            while (!spi->EVENTS_READY) {}
            spi->EVENTS_READY = 0;
            buf[i] = (uint8_t)(spi->RXD);
        }
    }
}

void
bsp_spi_write_buf(uint8_t addr, uint8_t *buf, uint8_t size)
{
    uint8_t i;
    uint8_t rxval;
    NRF_SPI_Type *spi;

    if (size == 0) {
        return;
    }

    spi = NRF_SPI0;

    spi->EVENTS_READY = 0;

    spi->TXD = (uint8_t)addr;
    for (i = 0; i < size; ++i) {
        spi->TXD = buf[i];
        while (!spi->EVENTS_READY) {}
        rxval = (uint8_t)(spi->RXD);
        spi->EVENTS_READY = 0;
    }

    while (!spi->EVENTS_READY) {}
    rxval = (uint8_t)(spi->RXD);
    spi->EVENTS_READY = 0;
    (void)rxval;
}
#endif
