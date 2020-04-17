/**************************************************************************//**
*
* @copyright (C) 2020 Nuvoton Technology Corp. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without modification,
* are permitted provided that the following conditions are met:
*   1. Redistributions of source code must retain the above copyright notice,
*      this list of conditions and the following disclaimer.
*   2. Redistributions in binary form must reproduce the above copyright notice,
*      this list of conditions and the following disclaimer in the documentation
*      and/or other materials provided with the distribution.
*   3. Neither the name of Nuvoton Technology Corp. nor the names of its contributors
*      may be used to endorse or promote products derived from this software
*      without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
* SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
* CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
* OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
* Change Logs:
* Date            Author           Notes
* 2020-2-27       YHKuo            First version
*
******************************************************************************/
#include <rtconfig.h>

#if defined(BSP_USING_SPI)
#include <rthw.h>
#include <rtdevice.h>
#include <rtdef.h>

#include <NuMicro.h>
#include <nu_bitutil.h>

#if defined(BSP_USING_SPI_PDMA)
    #include <drv_pdma.h>
#endif
/* Private define ---------------------------------------------------------------*/
enum
{
    SPI_START = -1,
#if defined(BSP_USING_SPI0)
    SPI0_IDX,
#endif
#if defined(BSP_USING_SPI1)
    SPI1_IDX,
#endif
#if defined(BSP_USING_SPI2)
    SPI2_IDX,
#endif
#if defined(BSP_USING_SPI3)
    SPI3_IDX,
#endif
    SPI_CNT
};

/* Private typedef --------------------------------------------------------------*/
struct nu_spi
{
    struct rt_spi_bus dev;
    char *name;
    SPI_T *spi_base;
    struct rt_spi_configuration configuration;
#if defined(BSP_USING_SPI_PDMA)
    uint32_t dummy;
    int16_t pdma_perp_tx;
    int8_t  pdma_chanid_tx;
    int16_t pdma_perp_rx;
    int8_t  pdma_chanid_rx;
    rt_sem_t m_psSemBus;
#endif
};
typedef struct nu_spi *spi_t;

/* Private functions ------------------------------------------------------------*/
static rt_err_t nu_spi_bus_configure(struct rt_spi_device *device, struct rt_spi_configuration *configuration);
static rt_uint32_t nu_spi_bus_xfer(struct rt_spi_device *device, struct rt_spi_message *message);
static void nu_spi_transmission_with_poll(struct nu_spi *spi_bus,
        uint8_t *send_addr, uint8_t *recv_addr, int length, uint8_t bytes_per_word);
static int nu_spi_register_bus(struct nu_spi *spi_bus, const char *name);
static void nu_spi_drain_rxfifo(SPI_T *spi_base);

#if defined(BSP_USING_SPI_PDMA)
    static void nu_pdma_spi_rx_cb(void *pvUserData, uint32_t u32EventFilter);
    static void nu_pdma_spi_tx_cb(void *pvUserData, uint32_t u32EventFilter);
    static rt_err_t nu_pdma_spi_rx_config(struct nu_spi *spi_bus, uint8_t *pu8Buf, int32_t i32RcvLen, uint8_t bytes_per_word);
    static rt_err_t nu_pdma_spi_tx_config(struct nu_spi *spi_bus, const uint8_t *pu8Buf, int32_t i32SndLen, uint8_t bytes_per_word);
    static rt_size_t nu_spi_pdma_transmit(struct nu_spi *spi_bus, const uint8_t *send_addr, uint8_t *recv_addr, int length, uint8_t bytes_per_word);
    static rt_err_t nu_hw_spi_pdma_allocate(struct nu_spi *spi_bus);
#endif
/* Public functions -------------------------------------------------------------*/


/* Private variables ------------------------------------------------------------*/
static struct rt_spi_ops nu_spi_poll_ops =
{
    .configure = nu_spi_bus_configure,
    .xfer      = nu_spi_bus_xfer,
};

static struct nu_spi nu_spi_arr [] =
{
#if defined(BSP_USING_SPI0)
    {
        .name = "spi0",
        .spi_base = SPI0,

#if defined(BSP_USING_SPI_PDMA)
#if defined(BSP_USING_SPI0_PDMA)
        .pdma_perp_tx = PDMA_SPI0_TX,
        .pdma_perp_rx = PDMA_SPI0_RX,
#else
        .pdma_perp_tx = NU_PDMA_UNUSED,
        .pdma_perp_rx = NU_PDMA_UNUSED,
#endif
#endif
    },
#endif
#if defined(BSP_USING_SPI1)
    {
        .name = "spi1",
        .spi_base = SPI1,

#if defined(BSP_USING_SPI_PDMA)
#if defined(BSP_USING_SPI1_PDMA)
        .pdma_perp_tx = PDMA_SPI1_TX,
        .pdma_perp_rx = PDMA_SPI1_RX,
#else
        .pdma_perp_tx = NU_PDMA_UNUSED,
        .pdma_perp_rx = NU_PDMA_UNUSED,
#endif
#endif

    },
#endif
#if defined(BSP_USING_SPI2)
    {
        .name = "spi2",
        .spi_base = SPI2,

#if defined(BSP_USING_SPI_PDMA)
#if defined(BSP_USING_SPI2_PDMA)
        .pdma_perp_tx = PDMA_SPI2_TX,
        .pdma_perp_rx = PDMA_SPI2_RX,
#else
        .pdma_perp_tx = NU_PDMA_UNUSED,
        .pdma_perp_rx = NU_PDMA_UNUSED,
#endif
#endif

    },
#endif
#if defined(BSP_USING_SPI3)
    {
        .name = "spi3",
        .spi_base = SPI3,

#if defined(BSP_USING_SPI_PDMA)
#if defined(BSP_USING_SPI3_PDMA)
        .pdma_perp_tx = PDMA_SPI3_TX,
        .pdma_perp_rx = PDMA_SPI3_RX,
#else
        .pdma_perp_tx = NU_PDMA_UNUSED,
        .pdma_perp_rx = NU_PDMA_UNUSED,
#endif
#endif

    },
#endif
    {0}
}; /* spi nu_spi */

static rt_err_t nu_spi_bus_configure(struct rt_spi_device *device,
                                     struct rt_spi_configuration *configuration)
{
    struct nu_spi *spi_bus;
    uint32_t u32SPIMode;
    uint32_t u32BusClock;
    rt_err_t ret = RT_EOK;

    RT_ASSERT(device != RT_NULL);
    RT_ASSERT(configuration != RT_NULL);

    spi_bus = (struct nu_spi *) device->bus;

    /* Check mode */
    switch (configuration->mode & RT_SPI_MODE_3)
    {
    case RT_SPI_MODE_0:
        u32SPIMode = SPI_MODE_0;
        break;
    case RT_SPI_MODE_1:
        u32SPIMode = SPI_MODE_1;
        break;
    case RT_SPI_MODE_2:
        u32SPIMode = SPI_MODE_2;
        break;
    case RT_SPI_MODE_3:
        u32SPIMode = SPI_MODE_3;
        break;
    default:
        ret = RT_EIO;
        goto exit_nu_spi_bus_configure;
    }

    /* Check data width */
    if (!(configuration->data_width == 8  ||
            configuration->data_width == 16 ||
            configuration->data_width == 24 ||
            configuration->data_width == 32))
    {
        ret = RT_EINVAL;
        goto exit_nu_spi_bus_configure;
    }

    /* Try to set clock and get actual spi bus clock */
    u32BusClock = SPI_SetBusClock(spi_bus->spi_base, configuration->max_hz);
    if (configuration->max_hz > u32BusClock)
    {
        rt_kprintf("%s clock max frequency is %dHz (!= %dHz)\n", spi_bus->name, u32BusClock, configuration->max_hz);
        configuration->max_hz = u32BusClock;
    }

    /* Need to initialize new configuration? */
    if (rt_memcmp(configuration, &spi_bus->configuration, sizeof(*configuration)) != 0)
    {
        rt_memcpy(&spi_bus->configuration, configuration, sizeof(*configuration));
        
        SPI_Open(spi_bus->spi_base, SPI_MASTER, u32SPIMode, configuration->data_width, u32BusClock);

        if (configuration->mode & RT_SPI_CS_HIGH)
        {
            /* Set CS pin to LOW */
            SPI_SET_SS_LOW(spi_bus->spi_base);
        }
        else
        {
            /* Set CS pin to HIGH */
            SPI_SET_SS_HIGH(spi_bus->spi_base);
        }

        if (configuration->mode & RT_SPI_MSB)
        {
            /* Set sequence to MSB first */
            SPI_SET_MSB_FIRST(spi_bus->spi_base);
        }
        else
        {
            /* Set sequence to LSB first */
            SPI_SET_LSB_FIRST(spi_bus->spi_base);
        }
    }

    /* Clear SPI RX FIFO */
    nu_spi_drain_rxfifo(spi_bus->spi_base);

exit_nu_spi_bus_configure:

    return -(ret);
}

#if defined(BSP_USING_SPI_PDMA)
static void nu_pdma_spi_rx_cb(void *pvUserData, uint32_t u32EventFilter)
{
    struct nu_spi *spi_bus;
    spi_bus = (struct nu_spi *)pvUserData;

    RT_ASSERT(spi_bus != RT_NULL);

    /* Get base address of spi register */
    SPI_T *spi_base = spi_bus->spi_base;

    if (u32EventFilter & NU_PDMA_EVENT_TRANSFER_DONE)
    {
        SPI_DISABLE_RX_PDMA(spi_base);  // Stop DMA TX transfer
    }
}
static rt_err_t nu_pdma_spi_rx_config(struct nu_spi *spi_bus, uint8_t *pu8Buf, int32_t i32RcvLen, uint8_t bytes_per_word)
{
    rt_err_t result = RT_EOK;
    rt_uint8_t *dst_addr = NULL;
    nu_pdma_memctrl_t memctrl = eMemCtl_Undefined;

    /* Get base address of spi register */
    SPI_T *spi_base = spi_bus->spi_base;

    rt_uint8_t spi_pdma_rx_chid = spi_bus->pdma_chanid_rx;

    result = nu_pdma_callback_register(spi_pdma_rx_chid,
                                       nu_pdma_spi_rx_cb,
                                       (void *)spi_bus,
                                       NU_PDMA_EVENT_TRANSFER_DONE);

    if (pu8Buf == RT_NULL)
    {
        memctrl  = eMemCtl_SrcFix_DstFix;
        dst_addr = (rt_uint8_t *) &spi_bus->dummy;
    }
    else
    {
        memctrl  = eMemCtl_SrcFix_DstInc;
        dst_addr = pu8Buf;
    }

    result = nu_pdma_channel_memctrl_set(spi_pdma_rx_chid, memctrl);
    result = nu_pdma_transfer(spi_pdma_rx_chid,
                              bytes_per_word * 8,
                              (uint32_t)&spi_base->RX,
                              (uint32_t)dst_addr,
                              i32RcvLen / bytes_per_word,
                              0);
    return result;
}

static void nu_pdma_spi_tx_cb(void *pvUserData, uint32_t u32EventFilter)
{
    struct nu_spi *spi_bus;
    spi_bus = (struct nu_spi *)pvUserData;

    RT_ASSERT(spi_bus != RT_NULL);

    /* Get base address of spi register */
    SPI_T *spi_base = spi_bus->spi_base;

    if (u32EventFilter & NU_PDMA_EVENT_TRANSFER_DONE)
    {
        SPI_DISABLE_TX_PDMA(spi_base);  // Stop DMA TX transfer
    }
    rt_sem_release(spi_bus->m_psSemBus);

}

static rt_err_t nu_pdma_spi_tx_config(struct nu_spi *spi_bus, const uint8_t *pu8Buf, int32_t i32SndLen, uint8_t bytes_per_word)
{
    rt_err_t result = RT_EOK;
    rt_uint8_t *src_addr = NULL;
    nu_pdma_memctrl_t memctrl = eMemCtl_Undefined;

    /* Get base address of spi register */
    SPI_T *spi_base = spi_bus->spi_base;

    rt_uint8_t spi_pdma_tx_chid = spi_bus->pdma_chanid_tx;

    result = nu_pdma_callback_register(spi_pdma_tx_chid,
                                       nu_pdma_spi_tx_cb,
                                       (void *)spi_bus,
                                       NU_PDMA_EVENT_TRANSFER_DONE);

    if (pu8Buf == RT_NULL)
    {
        spi_bus->dummy = 0;
        memctrl = eMemCtl_SrcFix_DstFix;
        src_addr = (rt_uint8_t *)&spi_bus->dummy;
    }
    else
    {
        memctrl = eMemCtl_SrcInc_DstFix;
        src_addr = (rt_uint8_t *)pu8Buf;
    }

    result = nu_pdma_channel_memctrl_set(spi_pdma_tx_chid, memctrl);
    result = nu_pdma_transfer(spi_pdma_tx_chid,
                              bytes_per_word * 8,
                              (uint32_t)src_addr,
                              (uint32_t)&spi_base->TX,
                              i32SndLen / bytes_per_word,
                              0);

    return result;
}


/**
 * SPI PDMA transfer
 */
static rt_size_t nu_spi_pdma_transmit(struct nu_spi *spi_bus, const uint8_t *send_addr, uint8_t *recv_addr, int length, uint8_t bytes_per_word)
{
    rt_err_t result = RT_EOK;

    /* Get base address of spi register */
    SPI_T *spi_base = spi_bus->spi_base;

    result = nu_pdma_spi_rx_config(spi_bus, recv_addr, length, bytes_per_word);
    RT_ASSERT(result == RT_EOK);
    result = nu_pdma_spi_tx_config(spi_bus, send_addr, length, bytes_per_word);
    RT_ASSERT(result == RT_EOK);

    /* Trigger TX/RX at the same time. */
    SPI_TRIGGER_TX_PDMA(spi_base);
    SPI_TRIGGER_RX_PDMA(spi_base);

    /* Wait PDMA transfer done */
    rt_sem_take(spi_bus->m_psSemBus, RT_WAITING_FOREVER);

    while (SPI_IS_BUSY(spi_base));

    return result;
}

static rt_err_t nu_hw_spi_pdma_allocate(struct nu_spi *spi_bus)
{
    /* Allocate SPI_TX nu_dma channel */
    if ((spi_bus->pdma_chanid_tx = nu_pdma_channel_allocate(spi_bus->pdma_perp_tx)) < 0)
    {
        goto exit_nu_hw_spi_pdma_allocate;
    }
    /* Allocate SPI_RX nu_dma channel */
    else if ((spi_bus->pdma_chanid_rx = nu_pdma_channel_allocate(spi_bus->pdma_perp_rx)) < 0)
    {
        nu_pdma_channel_free(spi_bus->pdma_chanid_tx);
        goto exit_nu_hw_spi_pdma_allocate;
    }

    spi_bus->m_psSemBus = rt_sem_create("spibus_sem", 0, RT_IPC_FLAG_FIFO);

    return RT_EOK;

exit_nu_hw_spi_pdma_allocate:

    return -(RT_ERROR);
}

#endif

static void nu_spi_drain_rxfifo(SPI_T *spi_base)
{
    while (SPI_IS_BUSY(spi_base));

    // Drain SPI RX FIFO
    if (!SPI_GET_RX_FIFO_EMPTY_FLAG(spi_base))
    {
        SPI_ClearRxFIFO(spi_base);
        while (!SPI_GET_RX_FIFO_EMPTY_FLAG(spi_base));
    }
}

static int nu_spi_read(SPI_T *spi_base, uint8_t *recv_addr, uint8_t bytes_per_word)
{
    int size = 0;
    uint32_t val;

    // Read RX data
    if (!SPI_GET_RX_FIFO_EMPTY_FLAG(spi_base))
    {
        // Input data to SPI TX
        switch (bytes_per_word)
        {
        case 4:
            val = SPI_READ_RX(spi_base);
            nu_set32_le(recv_addr, val);
            break;
        case 3:
            val = SPI_READ_RX(spi_base);
            nu_set24_le(recv_addr, val);
            break;
        case 2:
            val = SPI_READ_RX(spi_base);
            nu_set16_le(recv_addr, val);
            break;
        case 1:
            *recv_addr = SPI_READ_RX(spi_base);
            break;
        }
        size = bytes_per_word;
    }
    return size;
}

static int nu_spi_write(SPI_T *spi_base, const uint8_t *send_addr, uint8_t bytes_per_word)
{
    // Wait SPI TX send data
    while (SPI_GET_TX_FIFO_FULL_FLAG(spi_base));

    // Input data to SPI TX
    switch (bytes_per_word)
    {
    case 4:
        SPI_WRITE_TX(spi_base, nu_get32_le(send_addr));
        break;
    case 3:
        SPI_WRITE_TX(spi_base, nu_get24_le(send_addr));
        break;
    case 2:
        SPI_WRITE_TX(spi_base, nu_get16_le(send_addr));
        break;
    case 1:
        SPI_WRITE_TX(spi_base, *((uint8_t *)send_addr));
        break;
    }

    return bytes_per_word;
}

/**
 * @brief SPI bus polling
 * @param dev : The pointer of the specified SPI module.
 * @param send_addr : Source address
 * @param recv_addr : Destination address
 * @param length    : Data length
 */
static void nu_spi_transmission_with_poll(struct nu_spi *spi_bus,
        uint8_t *send_addr, uint8_t *recv_addr, int length, uint8_t bytes_per_word)
{
    SPI_T *spi_base = spi_bus->spi_base;

    // Write-only
    if ((send_addr != RT_NULL) && (recv_addr == RT_NULL))
    {
        while (length > 0)
        {
            send_addr += nu_spi_write(spi_base, send_addr, bytes_per_word);
            length -= bytes_per_word;
        }
    } // if (send_addr != RT_NULL && recv_addr == RT_NULL)
    // Read-only
    else if ((send_addr == RT_NULL) && (recv_addr != RT_NULL))
    {
        spi_bus->dummy = 0;
        while (length > 0)
        {
            /* Input data to SPI TX FIFO */
            length -= nu_spi_write(spi_base, (const uint8_t *)&spi_bus->dummy, bytes_per_word);

            /* Read data from RX FIFO */
            recv_addr += nu_spi_read(spi_base, recv_addr, bytes_per_word);
        }
    } // else if (send_addr == RT_NULL && recv_addr != RT_NULL)
    // Read&Write
    else
    {
        while (length > 0)
        {
            /* Input data to SPI TX FIFO */
            send_addr += nu_spi_write(spi_base, send_addr, bytes_per_word);
            length -= bytes_per_word;

            /* Read data from RX FIFO */
            recv_addr += nu_spi_read(spi_base, recv_addr, bytes_per_word);
        }
    } // else

    /* Wait RX or drian RX-FIFO */
    if (recv_addr)
    {
        // Wait SPI transmission done
        while (SPI_IS_BUSY(spi_base))
        {
            while (!SPI_GET_RX_FIFO_EMPTY_FLAG(spi_base))
            {
                recv_addr += nu_spi_read(spi_base, recv_addr, bytes_per_word);
            }
        }

        while (!SPI_GET_RX_FIFO_EMPTY_FLAG(spi_base))
        {
            recv_addr += nu_spi_read(spi_base, recv_addr, bytes_per_word);
        }
    }
    else
    {
        /* Clear SPI RX FIFO */
        nu_spi_drain_rxfifo(spi_base);
    }
}

static void nu_spi_transfer(struct nu_spi *spi_bus, uint8_t *tx, uint8_t *rx, int length, uint8_t bytes_per_word)
{
#if defined(BSP_USING_SPI_PDMA)
    /* DMA transfer constrains */
    if ((spi_bus->pdma_chanid_rx >= 0) &&
            (!(uint32_t)tx % bytes_per_word) &&
            (!(uint32_t)rx % bytes_per_word) &&
            (bytes_per_word != 3))
        nu_spi_pdma_transmit(spi_bus, tx, rx, length, bytes_per_word);
    else
        nu_spi_transmission_with_poll(spi_bus, tx, rx, length, bytes_per_word);
#else
    nu_spi_transmission_with_poll(spi_bus, tx, rx, length, bytes_per_word);
#endif
}

static rt_uint32_t nu_spi_bus_xfer(struct rt_spi_device *device, struct rt_spi_message *message)
{
    struct nu_spi *spi_bus;
    struct rt_spi_configuration *configuration;
    uint8_t bytes_per_word;

    RT_ASSERT(device != RT_NULL);
    RT_ASSERT(device->bus != RT_NULL);
    RT_ASSERT(message != RT_NULL);

    spi_bus = (struct nu_spi *) device->bus;
    configuration = &spi_bus->configuration;
    bytes_per_word = configuration->data_width / 8;

    if ((message->length % bytes_per_word) != 0)
    {
        /* Say bye. */
        rt_kprintf("%s: error payload length(%d%%%d != 0).\n", spi_bus->name, message->length, bytes_per_word);
        return 0;
    }

    if (message->length > 0)
    {
        if (message->cs_take && !(configuration->mode & RT_SPI_NO_CS))
        {
            if (configuration->mode & RT_SPI_CS_HIGH)
            {
                SPI_SET_SS_HIGH(spi_bus->spi_base);
            }
            else
            {
                SPI_SET_SS_LOW(spi_bus->spi_base);
            }
        }

        nu_spi_transfer(spi_bus, (uint8_t *)message->send_buf, (uint8_t *)message->recv_buf, message->length, bytes_per_word);

        if (message->cs_release && !(configuration->mode & RT_SPI_NO_CS))
        {
            if (configuration->mode & RT_SPI_CS_HIGH)
            {
                SPI_SET_SS_LOW(spi_bus->spi_base);
            }
            else
            {
                SPI_SET_SS_HIGH(spi_bus->spi_base);
            }
        }

    }

    return message->length;
}

static int nu_spi_register_bus(struct nu_spi *spi_bus, const char *name)
{
    return rt_spi_bus_register(&spi_bus->dev, name, &nu_spi_poll_ops);
}

/**
 * Hardware SPI Initial
 */
static int rt_hw_spi_init(void)
{
    int i;

    for (i = (SPI_START + 1); i < SPI_CNT; i++)
    {
        nu_spi_register_bus(&nu_spi_arr[i], nu_spi_arr[i].name);
#if defined(BSP_USING_SPI_PDMA)
        nu_spi_arr[i].pdma_chanid_tx = -1;
        nu_spi_arr[i].pdma_chanid_rx = -1;
        if ((nu_spi_arr[i].pdma_perp_tx != NU_PDMA_UNUSED) && (nu_spi_arr[i].pdma_perp_rx != NU_PDMA_UNUSED))
        {
            if (nu_hw_spi_pdma_allocate(&nu_spi_arr[i]) != RT_EOK)
            {
                rt_kprintf("Failed to allocate DMA channels for %s. We will use poll-mode for this bus.\n", nu_spi_arr[i].name);
            }
        }
#endif
    }

    return 0;
}

INIT_DEVICE_EXPORT(rt_hw_spi_init);

#endif //#if defined(BSP_USING_SPI)
