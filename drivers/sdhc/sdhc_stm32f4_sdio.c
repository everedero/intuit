/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * SD host driver for the STM32F4 SDIO (v1) peripheral, written for SDIO
 * cards (CMD52/CMD53). The upstream sdhc_stm32 needs the SDMMC IDMA, which
 * the F4 lacks.
 *
 * Data goes through the FIFO by polling, with interrupts locked: the SDIO
 * hardware flow control has a clock glitch erratum (ES0305), so nothing may
 * stall the FIFO. ponytail: polled FIFO, irq lock up to ~1 ms per CMD53 at
 * 12 MHz; move to DMA2 (stream 3 or 6, channel 4) if that hurts latency.
 */

#define DT_DRV_COMPAT st_stm32f4_sdio

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/irq.h>
#include <zephyr/drivers/sdhc.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/stm32_clock_control.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/sd/sd_spec.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/logging/log.h>
#include <soc.h>

LOG_MODULE_REGISTER(sdhc_stm32f4_sdio, CONFIG_SDHC_LOG_LEVEL);

#define SDIO_KER_HZ MHZ(48)
#define SDIO_F_MIN  KHZ(400)

#define STA_CMD_ERR  (SDIO_STA_CCRCFAIL | SDIO_STA_CTIMEOUT)
#define STA_DATA_ERR (SDIO_STA_DCRCFAIL | SDIO_STA_DTIMEOUT | SDIO_STA_TXUNDERR | SDIO_STA_RXOVERR)
#define ICR_STATIC   0x004007FFU

/* CMD53 argument fields */
#define CMD53_WRITE      BIT(31)
#define CMD53_BLOCK_MODE BIT(27)
#define CMD53_COUNT_MASK 0x1FFU

struct sdio_f4_config {
	SDIO_TypeDef *regs;
	const struct pinctrl_dev_config *pcfg;
	const struct stm32_pclken *pclken;
	size_t pclk_len;
	struct reset_dt_spec reset;
	uint8_t bus_width;
	uint32_t power_delay_ms;
	uint32_t f_max;
	void (*irq_config)(void);
};

struct sdio_f4_data {
	struct k_mutex lock;
	sdhc_interrupt_cb_t cb;
	void *cb_data;
	uint32_t clkcr;
	/* Byte-mode CMD53 counts get rounded up to a power of two */
	uint8_t bounce[512] __aligned(4);
};

/* The registers ignore writes for 3 SDIOCLK + 2 PCLK after a write */
static void reg_settle(void)
{
	k_busy_wait(10);
}

/*
 * Busy loop, also used with interrupts locked (no uptime then). The hardware
 * command/data timeouts end it first; the count is only a backstop.
 */
static int wait_flags(SDIO_TypeDef *r, uint32_t flags)
{
	for (uint32_t n = 0; n < 20000000U; n++) {
		if (r->STA & flags) {
			return 0;
		}
	}
	return -ETIMEDOUT;
}

static int send_cmd(SDIO_TypeDef *r, struct sdhc_command *cmd)
{
	uint32_t type = cmd->response_type & SDHC_NATIVE_RESPONSE_MASK;
	uint32_t c = cmd->opcode | SDIO_CMD_CPSMEN;
	uint32_t sta;
	int err;

	if (type == SD_RSP_TYPE_R2) {
		c |= SDIO_CMD_WAITRESP_0 | SDIO_CMD_WAITRESP_1;
	} else if (type != SD_RSP_TYPE_NONE) {
		c |= SDIO_CMD_WAITRESP_0;
	}

	r->ICR = SDIO_ICR_CCRCFAILC | SDIO_ICR_CTIMEOUTC | SDIO_ICR_CMDRENDC | SDIO_ICR_CMDSENTC;
	r->ARG = cmd->arg;
	r->CMD = c;

	if (type == SD_RSP_TYPE_NONE) {
		return wait_flags(r, SDIO_STA_CMDSENT);
	}

	err = wait_flags(r, SDIO_STA_CMDREND | STA_CMD_ERR);
	sta = r->STA;
	r->ICR = SDIO_ICR_CCRCFAILC | SDIO_ICR_CTIMEOUTC | SDIO_ICR_CMDRENDC;
	if (err || (sta & SDIO_STA_CTIMEOUT)) {
		return -ETIMEDOUT;
	}
	/* R3/R4 carry no CRC: CCRCFAIL is expected (erratum: set anyway) */
	if ((sta & SDIO_STA_CCRCFAIL) && type != SD_RSP_TYPE_R3 && type != SD_RSP_TYPE_R4) {
		return -EIO;
	}

	if (type == SD_RSP_TYPE_R2) {
		cmd->response[0] = r->RESP4;
		cmd->response[1] = r->RESP3;
		cmd->response[2] = r->RESP2;
		cmd->response[3] = r->RESP1;
	} else {
		cmd->response[0] = r->RESP1;
	}
	return 0;
}

static int xfer_fifo(SDIO_TypeDef *r, uint8_t *buf, size_t len, bool write)
{
	uint32_t sta;

	do {
		sta = r->STA;
		if (write) {
			int n = (sta & SDIO_STA_TXFIFOHE) ? 8 : ((sta & SDIO_STA_TXFIFOF) ? 0 : 1);

			while (n-- && len) {
				uint32_t w = 0;
				size_t k = MIN(len, 4);

				memcpy(&w, buf, k);
				r->FIFO = w;
				buf += k;
				len -= k;
			}
		} else {
			int n = (sta & SDIO_STA_RXFIFOHF) ? 8 : ((sta & SDIO_STA_RXDAVL) ? 1 : 0);

			while (n-- && len) {
				uint32_t w = r->FIFO;
				size_t k = MIN(len, 4);

				memcpy(buf, &w, k);
				buf += k;
				len -= k;
			}
		}
	} while (!(sta & (SDIO_STA_DATAEND | STA_DATA_ERR)));

	/* Words still in the RX FIFO after DATAEND */
	while (!write && len && (r->STA & SDIO_STA_RXDAVL)) {
		uint32_t w = r->FIFO;
		size_t k = MIN(len, 4);

		memcpy(buf, &w, k);
		buf += k;
		len -= k;
	}

	if (sta & SDIO_STA_DTIMEOUT) {
		return -ETIMEDOUT;
	}
	if (sta & STA_DATA_ERR) {
		LOG_DBG("data error, STA 0x%08x", sta);
		return -EIO;
	}
	return 0;
}

static int sdio_f4_request(const struct device *dev, struct sdhc_command *cmd,
			   struct sdhc_data *data)
{
	const struct sdio_f4_config *cfg = dev->config;
	struct sdio_f4_data *d = dev->data;
	SDIO_TypeDef *r = cfg->regs;
	uint8_t *buf = NULL;
	size_t len = 0, user_len = 0;
	bool write = false, bounce = false;
	uint32_t dctrl = 0;
	unsigned int key;
	int err;

	if (data) {
		uint32_t bs = data->block_size;

		write = cmd->opcode == SDIO_RW_EXTENDED ? (cmd->arg & CMD53_WRITE)
							: (cmd->opcode == SD_WRITE_SINGLE_BLOCK ||
							   cmd->opcode == SD_WRITE_MULTIPLE_BLOCK);
		buf = data->data;
		user_len = bs * data->blocks;

		if (cmd->opcode == SDIO_RW_EXTENDED && !(cmd->arg & CMD53_BLOCK_MODE) &&
		    !IS_POWER_OF_TWO(bs)) {
			/* The DPSM only does 2^n block sizes: transfer more, keep what was asked */
			bs = 1U << LOG2CEIL(bs);
			cmd->arg = (cmd->arg & ~CMD53_COUNT_MASK) | (bs & CMD53_COUNT_MASK);
			bounce = true;
		}
		if (!IS_POWER_OF_TWO(bs) || bs > 16384) {
			LOG_ERR("block size %u not supported", data->block_size);
			return -ENOTSUP;
		}
		len = bs * data->blocks;
		dctrl = SDIO_DCTRL_DTEN | (LOG2(bs) << SDIO_DCTRL_DBLOCKSIZE_Pos) |
			(write ? 0 : SDIO_DCTRL_DTDIR) |
			(cmd->opcode == SDIO_RW_EXTENDED ? SDIO_DCTRL_SDIOEN : 0);
		if (bounce) {
			if (write) {
				memcpy(d->bounce, buf, user_len);
				memset(d->bounce + user_len, 0, len - user_len);
			}
			buf = d->bounce;
		}
	}

	k_mutex_lock(&d->lock, K_FOREVER);
	r->ICR = ICR_STATIC & ~SDIO_ICR_SDIOITC;

	if (data) {
		uint32_t hz = SDIO_KER_HZ / ((d->clkcr & SDIO_CLKCR_CLKDIV) + 2);

		r->DTIMER = (uint32_t)((uint64_t)hz * MAX(data->timeout_ms, 100) / 1000);
		r->DLEN = len;
	}

	/* Reads: arm the DPSM before the command, the card may answer at once */
	key = irq_lock();
	if (data && !write) {
		r->DCTRL = dctrl;
	}
	err = send_cmd(r, cmd);
	if (!err && data) {
		if (write) {
			r->DCTRL = dctrl;
		}
		err = xfer_fifo(r, buf, len, write);
	}
	irq_unlock(key);

	r->DCTRL = SDIO_DCTRL_SDIOEN;
	r->ICR = ICR_STATIC & ~SDIO_ICR_SDIOITC;
	k_mutex_unlock(&d->lock);

	if (err) {
		LOG_DBG("CMD%u arg 0x%08x: %d", cmd->opcode, cmd->arg, err);
		return err;
	}
	if (data) {
		if (bounce && !write) {
			memcpy(data->data, d->bounce, user_len);
		}
		data->bytes_xfered = user_len;
	}
	return 0;
}

static int sdio_f4_set_io(const struct device *dev, struct sdhc_io *ios)
{
	const struct sdio_f4_config *cfg = dev->config;
	struct sdio_f4_data *d = dev->data;
	SDIO_TypeDef *r = cfg->regs;
	uint32_t clkcr = 0;

	if (ios->bus_width == SDHC_BUS_WIDTH8BIT ||
	    (ios->bus_width == SDHC_BUS_WIDTH4BIT && cfg->bus_width != 4)) {
		return -ENOTSUP;
	}
	if (ios->signal_voltage && ios->signal_voltage != SD_VOL_3_3_V) {
		return -ENOTSUP;
	}

	if (ios->clock) {
		uint32_t div = DIV_ROUND_UP(SDIO_KER_HZ, MIN(ios->clock, cfg->f_max));

		clkcr = SDIO_CLKCR_CLKEN | (CLAMP(div, 2, 257) - 2);
	}
	if (ios->bus_width == SDHC_BUS_WIDTH4BIT) {
		clkcr |= SDIO_CLKCR_WIDBUS_0;
	}

	k_mutex_lock(&d->lock, K_FOREVER);
	if (ios->power_mode == SDHC_POWER_OFF) {
		r->POWER = 0;
	} else if (r->POWER != SDIO_POWER_PWRCTRL) {
		r->POWER = SDIO_POWER_PWRCTRL;
		k_msleep(2);
	}
	reg_settle();
	r->CLKCR = clkcr;
	reg_settle();
	d->clkcr = clkcr;
	k_mutex_unlock(&d->lock);

	LOG_DBG("clock %u Hz, %u-bit, CLKCR 0x%04x", ios->clock, ios->bus_width, clkcr);
	return 0;
}

static int sdio_f4_get_card_present(const struct device *dev)
{
	/* Soldered SDIO module */
	return 1;
}

static int sdio_f4_card_busy(const struct device *dev)
{
	/* ponytail: no D0 readback on this IP; SDIO cards don't hold busy here */
	return 0;
}

static int sdio_f4_get_host_props(const struct device *dev, struct sdhc_host_props *props)
{
	const struct sdio_f4_config *cfg = dev->config;

	memset(props, 0, sizeof(*props));
	props->f_min = SDIO_F_MIN;
	props->f_max = cfg->f_max;
	props->power_delay = cfg->power_delay_ms;
	props->host_caps.vol_330_support = true;
	props->host_caps.max_blk_len = 0; /* 512 */
	props->host_caps.sdio_async_interrupt_support = true;
	props->bus_4_bit_support = cfg->bus_width == 4;
	return 0;
}

static int sdio_f4_reset(const struct device *dev)
{
	const struct sdio_f4_config *cfg = dev->config;
	struct sdio_f4_data *d = dev->data;
	SDIO_TypeDef *r = cfg->regs;

	r->DCTRL = 0;
	r->ICR = ICR_STATIC;
	reg_settle();
	r->CLKCR = d->clkcr;
	r->DCTRL = SDIO_DCTRL_SDIOEN;
	return 0;
}

static int sdio_f4_enable_interrupt(const struct device *dev, sdhc_interrupt_cb_t callback,
				    int sources, void *user_data)
{
	const struct sdio_f4_config *cfg = dev->config;
	struct sdio_f4_data *d = dev->data;

	if (sources != SDHC_INT_SDIO) {
		return -ENOTSUP;
	}
	d->cb = callback;
	d->cb_data = user_data;
	cfg->regs->ICR = SDIO_ICR_SDIOITC;
	cfg->regs->MASK |= SDIO_MASK_SDIOITIE;
	return 0;
}

static int sdio_f4_disable_interrupt(const struct device *dev, int sources)
{
	const struct sdio_f4_config *cfg = dev->config;
	struct sdio_f4_data *d = dev->data;

	if (sources != SDHC_INT_SDIO) {
		return -ENOTSUP;
	}
	cfg->regs->MASK &= ~SDIO_MASK_SDIOITIE;
	d->cb = NULL;
	return 0;
}

static void sdio_f4_isr(const struct device *dev)
{
	const struct sdio_f4_config *cfg = dev->config;
	struct sdio_f4_data *d = dev->data;

	/* Card interrupt is a level: mask it, the client re-enables it once serviced */
	if (cfg->regs->STA & SDIO_STA_SDIOIT) {
		cfg->regs->MASK &= ~SDIO_MASK_SDIOITIE;
		cfg->regs->ICR = SDIO_ICR_SDIOITC;
		if (d->cb) {
			d->cb(dev, SDHC_INT_SDIO, d->cb_data);
		}
	}
}

static int sdio_f4_init(const struct device *dev)
{
	const struct sdio_f4_config *cfg = dev->config;
	struct sdio_f4_data *d = dev->data;
	const struct device *clk = DEVICE_DT_GET(STM32_CLOCK_CONTROL_NODE);
	int err;

	k_mutex_init(&d->lock);

	err = clock_control_on(clk, (clock_control_subsys_t)&cfg->pclken[0]);
	if (!err && cfg->pclk_len > 1) {
		err = clock_control_configure(clk, (clock_control_subsys_t)&cfg->pclken[1], NULL);
	}
	if (err) {
		LOG_ERR("clock setup failed (%d)", err);
		return err;
	}
	if (cfg->reset.dev) {
		reset_line_toggle_dt(&cfg->reset);
	}

	err = pinctrl_apply_state(cfg->pcfg, PINCTRL_STATE_DEFAULT);
	if (err) {
		return err;
	}

	cfg->regs->MASK = 0;
	cfg->regs->ICR = ICR_STATIC;
	cfg->irq_config();
	return 0;
}

static DEVICE_API(sdhc, sdio_f4_api) = {
	.reset = sdio_f4_reset,
	.request = sdio_f4_request,
	.set_io = sdio_f4_set_io,
	.get_card_present = sdio_f4_get_card_present,
	.card_busy = sdio_f4_card_busy,
	.get_host_props = sdio_f4_get_host_props,
	.enable_interrupt = sdio_f4_enable_interrupt,
	.disable_interrupt = sdio_f4_disable_interrupt,
};

#define SDIO_F4_INIT(n)                                                                            \
	PINCTRL_DT_INST_DEFINE(n);                                                                 \
	static const struct stm32_pclken sdio_f4_pclken_##n[] = STM32_DT_INST_CLOCKS(n);           \
	static void sdio_f4_irq_config_##n(void)                                                   \
	{                                                                                          \
		IRQ_CONNECT(DT_INST_IRQN(n), DT_INST_IRQ(n, priority), sdio_f4_isr,                \
			    DEVICE_DT_INST_GET(n), 0);                                             \
		irq_enable(DT_INST_IRQN(n));                                                       \
	}                                                                                          \
	static const struct sdio_f4_config sdio_f4_config_##n = {                                  \
		.regs = (SDIO_TypeDef *)DT_INST_REG_ADDR(n),                                       \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(n),                                         \
		.pclken = sdio_f4_pclken_##n,                                                      \
		.pclk_len = DT_INST_NUM_CLOCKS(n),                                                 \
		.reset = RESET_DT_SPEC_INST_GET_OR(n, {0}),                                        \
		.bus_width = DT_INST_PROP(n, bus_width),                                           \
		.power_delay_ms = DT_INST_PROP(n, power_delay_ms),                                 \
		.f_max = DT_INST_PROP(n, max_bus_freq),                                            \
		.irq_config = sdio_f4_irq_config_##n,                                              \
	};                                                                                         \
	static struct sdio_f4_data sdio_f4_data_##n;                                               \
	DEVICE_DT_INST_DEFINE(n, sdio_f4_init, NULL, &sdio_f4_data_##n, &sdio_f4_config_##n,       \
			      POST_KERNEL, CONFIG_SDHC_INIT_PRIORITY, &sdio_f4_api);

DT_INST_FOREACH_STATUS_OKAY(SDIO_F4_INIT)
