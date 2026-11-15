#include "microphone.h"

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>

#if DT_NODE_HAS_STATUS(DT_ALIAS(microphone), okay)

#include "audio_level.h"

#include <zephyr/audio/dmic.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/kernel.h>

#define MIC_RATE_HZ 16000U
#define MIC_BLOCK_SIZE ((MIC_RATE_HZ / 10U) * sizeof(int16_t))
#define MIC_BLOCK_COUNT 4U
#define MIC_READ_TIMEOUT_MS 300
#define MIC_STOP_TIMEOUT_MS 100

K_MEM_SLAB_DEFINE_STATIC(microphone_blocks, MIC_BLOCK_SIZE, MIC_BLOCK_COUNT, 4);

static const struct device *const microphone = DEVICE_DT_GET(DT_ALIAS(microphone));
static bool configured;

#if defined(CONFIG_PINCTRL_NON_STATIC)
PINCTRL_DT_DEV_CONFIG_DECLARE(DT_ALIAS(microphone));
#else
PINCTRL_DT_DEFINE(DT_ALIAS(microphone));
#endif

/* nrfx STOP completes in interrupt context. Drain queued buffers and wait for
 * driver-owned DMA buffers before allowing another capture to reuse the slab.
 */
static int stop_capture(void)
{
	int err = dmic_trigger(microphone, DMIC_TRIGGER_STOP);
	if (err != 0) {
		return err;
	}
	int64_t deadline = k_uptime_get() + MIC_STOP_TIMEOUT_MS;
	do {
		for (unsigned int i = 0; i < MIC_BLOCK_COUNT; ++i) {
			void *buffer = NULL;
			size_t size;
			if (dmic_read(microphone, 0, &buffer, &size, 0) != 0) {
				break;
			}
			if (buffer != NULL) {
				k_mem_slab_free(&microphone_blocks, buffer);
			}
		}
		if (k_mem_slab_num_used_get(&microphone_blocks) == 0) {
			return pinctrl_apply_state(PINCTRL_DT_DEV_CONFIG_GET(DT_ALIAS(microphone)),
						   PINCTRL_STATE_SLEEP);
		}
		k_msleep(1);
	} while (k_uptime_get() < deadline);
	return -ETIMEDOUT;
}

int microphone_init(void)
{
	if (!device_is_ready(microphone)) {
		return -ENODEV;
	}
	struct pcm_stream_cfg stream = {
	    .pcm_rate = MIC_RATE_HZ,
	    .pcm_width = 16,
	    .block_size = MIC_BLOCK_SIZE,
	    .mem_slab = &microphone_blocks,
	};
	struct dmic_cfg config = {
	    .io =
		{
		    /* VM3011 normal mode requires >=1.1MHz; 16kHz uses an 80x ratio. */
		    .min_pdm_clk_freq = 1100000,
		    .max_pdm_clk_freq = 3500000,
		    .min_pdm_clk_dc = 40,
		    .max_pdm_clk_dc = 60,
		},
	    .streams = &stream,
	    .channel =
		{
		    .req_num_streams = 1,
		    .req_num_chan = 1,
		    /* Thingy:53 VM3011 L/R SEL is grounded. */
		    .req_chan_map_lo = dmic_build_channel_map(0, 0, PDM_CHAN_LEFT),
		},
	};
	int err = dmic_configure(microphone, &config);
	if (err == 0) {
		err = pinctrl_apply_state(PINCTRL_DT_DEV_CONFIG_GET(DT_ALIAS(microphone)),
					  PINCTRL_STATE_SLEEP);
	}
	configured = err == 0;
	return err;
}

int microphone_read(int32_t *rms)
{
	if (rms == NULL) {
		return -EINVAL;
	}
	if (!configured) {
		return -ENODEV;
	}
	int err = stop_capture();
	if (err != 0) {
		return err;
	}
	err = pinctrl_apply_state(PINCTRL_DT_DEV_CONFIG_GET(DT_ALIAS(microphone)),
				  PINCTRL_STATE_DEFAULT);
	if (err != 0) {
		return err;
	}
	err = dmic_trigger(microphone, DMIC_TRIGGER_START);
	if (err != 0) {
		(void)stop_capture();
		return err;
	}
	int32_t level = 0;
	for (unsigned int block = 0; block < 2U; ++block) {
		void *buffer = NULL;
		size_t size = 0;
		err = dmic_read(microphone, 0, &buffer, &size, MIC_READ_TIMEOUT_MS);
		if (err != 0) {
			break;
		}
		if (buffer == NULL || size != MIC_BLOCK_SIZE) {
			err = -EIO;
		} else if (block == 1U) {
			/* Discard the first block while the microphone and filter settle. */
			err = pcm_rms(buffer, size / sizeof(int16_t), &level);
		}
		if (buffer != NULL) {
			k_mem_slab_free(&microphone_blocks, buffer);
		}
		if (err != 0) {
			break;
		}
	}
	int stop_err = stop_capture();
	if (err != 0 || stop_err != 0) {
		return err != 0 ? err : stop_err;
	}
	*rms = level;
	return 0;
}

#else

int microphone_init(void)
{
	return -ENODEV;
}

int microphone_read(int32_t *rms)
{
	return rms == NULL ? -EINVAL : -ENODEV;
}

#endif
