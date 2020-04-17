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
* 2020-2-7        Wayne            First version
*
******************************************************************************/

#include <rtconfig.h>

#if defined(BSP_USING_SDH)

#include <rtdevice.h>
#include <NuMicro.h>
#include <drv_pdma.h>
#include <string.h>

#if defined(RT_USING_DFS)
    #include <dfs_fs.h>
    #include <dfs_posix.h>
#endif

/* Private define ---------------------------------------------------------------*/
// RT_DEV_NAME_PREFIX sdh

#ifndef NU_SDH_MOUNTPOINT_ROOT
    #define NU_SDH_MOUNTPOINT_ROOT  "/mnt"
#endif

#ifndef NU_SDH_MOUNTPOINT_SDH0
    #define NU_SDH_MOUNTPOINT_SDH0  NU_SDH_MOUNTPOINT_ROOT"/sd0"
#endif

#ifndef NU_SDH_MOUNTPOINT_SDH1
    #define NU_SDH_MOUNTPOINT_SDH1  NU_SDH_MOUNTPOINT_ROOT"/sd1"
#endif

#if defined(NU_SDH_USING_PDMA)
    #define NU_SDH_MEMCPY  nu_pdma_memcpy
#else
    #define NU_SDH_MEMCPY  memcpy
#endif

enum
{
    SDH_START = -1,
#if defined(BSP_USING_SDH0)
    SDH0_IDX,
#endif
#if defined(BSP_USING_SDH1)
    SDH1_IDX,
#endif
    SDH_CNT
};

#define SDH_BLOCK_SIZE   512ul

#if defined(NU_SDH_HOTPLUG)
    #define NU_SDH_TID_STACK_SIZE  1024
#endif

#if defined(NU_SDH_HOTPLUG)
enum
{
    NU_SDH_CARD_INSERTED_SD0 = (1 << 0),
    NU_SDH_CARD_REMOVED_SD0 = (1 << 1),
    NU_SDH_CARD_INSERTED_SD1 = (1 << 2),
    NU_SDH_CARD_REMOVED_SD1 = (1 << 3),
    NU_SDH_CARD_EVENT_ALL = (NU_SDH_CARD_INSERTED_SD0 | NU_SDH_CARD_REMOVED_SD0 | NU_SDH_CARD_INSERTED_SD1 | NU_SDH_CARD_REMOVED_SD1)
};
#endif

/* Private typedef --------------------------------------------------------------*/
struct nu_sdh
{
    struct rt_device      dev;
    char                 *name;
#if defined(NU_SDH_HOTPLUG)
    char                 *mounted_point;
#endif
    SDH_T                *base;
    uint32_t              is_card_inserted;
    SDH_INFO_T           *info;
    struct rt_semaphore   lock;
    uint8_t              *pbuf;
};
typedef struct nu_sdh *nu_sdh_t;

#if defined(NU_SDH_HOTPLUG)
    static struct rt_thread sdh_tid;
    static rt_uint8_t sdh_stack[NU_SDH_TID_STACK_SIZE];
#endif

/* Private functions ------------------------------------------------------------*/
static void nu_sdh_isr(nu_sdh_t sdh);
static rt_err_t nu_sdh_init(rt_device_t dev);
static rt_err_t nu_sdh_open(rt_device_t dev, rt_uint16_t oflag);
static rt_err_t nu_sdh_close(rt_device_t dev);
static rt_size_t nu_sdh_read(rt_device_t dev, rt_off_t pos, void *buffer, rt_size_t blk_nb);
static rt_err_t nu_sdh_control(rt_device_t dev, int cmd, void *args);
static int rt_hw_sdh_init(void);

#if defined(NU_SDH_HOTPLUG)
    static rt_bool_t nu_sdh_hotplug_is_mounted(const char *mounting_path);
    static void sdh_hotplugger(void *param);
    static rt_err_t nu_sdh_hotplug_mount(nu_sdh_t sdh);
    static rt_err_t nu_sdh_hotplug_unmount(nu_sdh_t sdh);
#endif

/* Public functions -------------------------------------------------------------*/


/* Private variables ------------------------------------------------------------*/
static struct nu_sdh nu_sdh_arr [] =
{
#if defined(BSP_USING_SDH0)
    {
        .name = "sdh0",
#if defined(NU_SDH_HOTPLUG)
        .mounted_point = NU_SDH_MOUNTPOINT_SDH0,
#endif
        .base = SDH0,
        .info = &SD0,
    },
#endif
#if defined(BSP_USING_SDH1)
    {
        .name = "sdh1",
#if defined(NU_SDH_HOTPLUG)
        .mounted_point = NU_SDH_MOUNTPOINT_SDH1,
#endif
        .base = SDH1,
        .info = &SD1,
    },
#endif
    {0}
}; /* struct nu_sdh nu_sdh_arr [] */
static struct rt_event sdh_event;

static void nu_sdh_isr(nu_sdh_t sdh)
{
    SDH_T *sdh_base = sdh->base;
    unsigned int volatile isr;
    unsigned int volatile ier;
    SDH_INFO_T *pSD = sdh->info;

    // FMI data abort interrupt
    if (sdh_base->GINTSTS & SDH_GINTSTS_DTAIF_Msk)
    {
        /* ResetAllEngine() */
        sdh_base->GCTL |= SDH_GCTL_GCTLRST_Msk;
    }

    //----- SD interrupt status
    isr = sdh_base->INTSTS;
    if (isr & SDH_INTSTS_BLKDIF_Msk)
    {
        // block down
        pSD->DataReadyFlag = TRUE;
        SDH_CLR_INT_FLAG(sdh_base, SDH_INTSTS_BLKDIF_Msk);
    }

    if (isr & SDH_INTSTS_CDIF_Msk)   // card detect
    {
        /* SD interrupt status */
        // it is work to delay 50 times for SD_CLK = 200KHz
        {
            int volatile i;         // delay 30 fail, 50 OK
            for (i = 0; i < 0x500; i++); // delay to make sure got updated value from REG_SDISR.
            isr = sdh_base->INTSTS;
        }

        if (isr & SDH_INTSTS_CDSTS_Msk)
        {
            /* Card removed */
#if defined(NU_SDH_HOTPLUG)
            if (sdh->base == SDH0)
                rt_event_send(&sdh_event, NU_SDH_CARD_REMOVED_SD0);
            else if (sdh->base == SDH1)
                rt_event_send(&sdh_event, NU_SDH_CARD_REMOVED_SD1);
#endif
            sdh->info->IsCardInsert = FALSE;   // SDISR_CD_Card = 1 means card remove for GPIO mode
            rt_memset((void *)sdh->info, 0, sizeof(SDH_INFO_T));
        }
        else
        {
            SDH_Open(sdh_base, CardDetect_From_GPIO);
            if (!SDH_Probe(sdh_base))
            {
                /* Card inserted */
#if defined(NU_SDH_HOTPLUG)
                if (sdh->base == SDH0)
                    rt_event_send(&sdh_event, NU_SDH_CARD_INSERTED_SD0);
                else if (sdh->base == SDH1)
                    rt_event_send(&sdh_event, NU_SDH_CARD_INSERTED_SD1);
#endif
            }
        }
        /* Clear CDIF interrupt flag */
        SDH_CLR_INT_FLAG(sdh_base, SDH_INTSTS_CDIF_Msk);
    }

    // CRC error interrupt
    if (isr & SDH_INTSTS_CRCIF_Msk)
    {
        if (!(isr & SDH_INTSTS_CRC16_Msk))
        {
            /* CRC_16 error */
            // handle CRC 16 error
        }
        else if (!(isr & SDH_INTSTS_CRC7_Msk))
        {
            if (!pSD->R3Flag)
            {
                /* CRC_7 error */
                // handle CRC 7 error
            }
        }
        /* Clear CRCIF interrupt flag */
        SDH_CLR_INT_FLAG(sdh_base, SDH_INTSTS_CRCIF_Msk);
    }

    /* Data-in timeout */
    if (isr & SDH_INTSTS_DITOIF_Msk)
    {
        sdh_base->INTSTS |= SDH_INTSTS_DITOIF_Msk;
    }

    /* Response-in timeout interrupt */
    if (isr & SDH_INTSTS_RTOIF_Msk)
    {
        sdh_base->INTSTS |= SDH_INTSTS_RTOIF_Msk;
    }
}

#if defined(BSP_USING_SDH0)
void SDH0_IRQHandler(void)
{
    /* enter interrupt */
    rt_interrupt_enter();

    nu_sdh_isr(&nu_sdh_arr[SDH0_IDX]);

    /* leave interrupt */
    rt_interrupt_leave();
}
#endif

#if defined(BSP_USING_SDH1)
void SDH1_IRQHandler(void)
{
    /* enter interrupt */
    rt_interrupt_enter();

    nu_sdh_isr(&nu_sdh_arr[SDH1_IDX]);

    /* leave interrupt */
    rt_interrupt_leave();
}
#endif

/* RT-Thread Device Driver Interface */
static rt_err_t nu_sdh_init(rt_device_t dev)
{
    return RT_EOK;
}

static rt_err_t nu_sdh_open(rt_device_t dev, rt_uint16_t oflag)
{
    nu_sdh_t sdh = (nu_sdh_t)dev;

    RT_ASSERT(dev != RT_NULL);

    return (SDH_Probe(sdh->base) == 0) ? RT_EOK :  -(RT_ERROR);
}

static rt_err_t nu_sdh_close(rt_device_t dev)
{
    return RT_EOK;
}

static rt_size_t nu_sdh_read(rt_device_t dev, rt_off_t pos, void *buffer, rt_size_t blk_nb)
{
    rt_uint32_t ret = 0;
    nu_sdh_t sdh = (nu_sdh_t)dev;

    RT_ASSERT(dev != RT_NULL);
    RT_ASSERT(buffer != RT_NULL);

    rt_sem_take(&sdh->lock, RT_WAITING_FOREVER);

    /* Check alignment. */
    if (((uint32_t)buffer & 0x03) != 0)
    {
        /* Non-aligned. */
        uint32_t i;
        uint8_t *copy_buffer = (uint8_t *)buffer;

        sdh->pbuf = rt_malloc(SDH_BLOCK_SIZE);
        if (sdh->pbuf == RT_NULL)
            goto exit_nu_sdh_read;

        for (i = 0; i < blk_nb; i++)
        {
            /* Read to temp buffer from specified sector. */
            ret = SDH_Read(sdh->base, &sdh->pbuf[0], pos, 1);
            if (ret != Successful)
                goto exit_nu_sdh_read;

            /* Move to user's buffer */
            NU_SDH_MEMCPY((void *)copy_buffer, (void *)&sdh->pbuf[0], SDH_BLOCK_SIZE);

            pos ++;
            copy_buffer += SDH_BLOCK_SIZE;
        }
    }
    else
    {
        /* Read to user's buffer from specified sector. */
        ret = SDH_Read(sdh->base, (uint8_t *)buffer, pos, blk_nb);
    }

exit_nu_sdh_read:

    if (sdh->pbuf)
    {
        rt_free(sdh->pbuf);
        sdh->pbuf = RT_NULL;
    }

    rt_sem_release(&sdh->lock);

    if (ret == Successful)
        return blk_nb;

    rt_kprintf("Read failed: %d, buffer 0x%08x\n", ret, buffer);
    rt_set_errno(-RT_ENOSYS);
    return 0;
}

static rt_size_t nu_sdh_write(rt_device_t dev, rt_off_t pos, const void *buffer, rt_size_t blk_nb)
{
    rt_uint32_t ret = 0;
    nu_sdh_t sdh = (nu_sdh_t)dev;

    RT_ASSERT(dev != RT_NULL);
    RT_ASSERT(buffer != RT_NULL);

    rt_sem_take(&sdh->lock, RT_WAITING_FOREVER);

    /* Check alignment. */
    if (((uint32_t)buffer & 0x03) != 0)
    {
        /* Non-aligned. */
        uint32_t i;
        uint8_t *copy_buffer = (uint8_t *)buffer;

        sdh->pbuf = rt_malloc(SDH_BLOCK_SIZE);
        if (sdh->pbuf == RT_NULL)
            goto exit_nu_sdh_write;

        for (i = 0; i < blk_nb; i++)
        {
            NU_SDH_MEMCPY((void *)&sdh->pbuf[0], copy_buffer, SDH_BLOCK_SIZE);

            ret = SDH_Write(sdh->base, (uint8_t *)&sdh->pbuf[0], pos, 1);
            if (ret != Successful)
                goto exit_nu_sdh_write;

            pos++;
            copy_buffer += SDH_BLOCK_SIZE;
        }
    }
    else
    {
        /* Write to device directly. */
        ret = SDH_Write(sdh->base, (uint8_t *)buffer, pos, blk_nb);
    }

exit_nu_sdh_write:

    if (sdh->pbuf)
    {
        rt_free(sdh->pbuf);
        sdh->pbuf = RT_NULL;
    }

    rt_sem_release(&sdh->lock);

    if (ret == Successful) return blk_nb;

    rt_kprintf("write failed: %d, buffer 0x%08x\n", ret, buffer);
    rt_set_errno(-RT_ENOSYS);
    return 0;
}

static rt_err_t nu_sdh_control(rt_device_t dev, int cmd, void *args)
{
    nu_sdh_t sdh = (nu_sdh_t)dev;

    RT_ASSERT(dev != RT_NULL);

    if (cmd == RT_DEVICE_CTRL_BLK_GETGEOME)
    {
        SDH_INFO_T *sdh_info = sdh->info;

        struct rt_device_blk_geometry *geometry;

        geometry = (struct rt_device_blk_geometry *)args;
        if (geometry == RT_NULL) return -RT_ERROR;

        geometry->bytes_per_sector = sdh_info->sectorSize;
        geometry->block_size = sdh_info->sectorSize;
        geometry->sector_count = sdh_info->totalSectorN;
    }

    return RT_EOK;
}


static int rt_hw_sdh_init(void)
{
    int i;
    rt_err_t ret = RT_EOK;
    rt_uint32_t flags = RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_REMOVABLE | RT_DEVICE_FLAG_STANDALONE;

    rt_event_init(&sdh_event, "sdh_event", RT_IPC_FLAG_FIFO);

    for (i = (SDH_START + 1); i < SDH_CNT; i++)
    {
        /* Register sdcard device */
        nu_sdh_arr[i].dev.type  = RT_Device_Class_Block;
        nu_sdh_arr[i].dev.init  = nu_sdh_init;
        nu_sdh_arr[i].dev.open  = nu_sdh_open;
        nu_sdh_arr[i].dev.close = nu_sdh_close;
        nu_sdh_arr[i].dev.read  = nu_sdh_read;
        nu_sdh_arr[i].dev.write = nu_sdh_write;
        nu_sdh_arr[i].dev.control = nu_sdh_control;

        /* Private */
        nu_sdh_arr[i].dev.user_data = (void *)&nu_sdh_arr[i];

        rt_sem_init(&nu_sdh_arr[i].lock, "sdhlock", 1, RT_IPC_FLAG_FIFO);

        SDH_Open(nu_sdh_arr[i].base, CardDetect_From_GPIO);

        nu_sdh_arr[i].pbuf = RT_NULL;
        ret = rt_device_register(&nu_sdh_arr[i].dev, nu_sdh_arr[i].name, flags);
        RT_ASSERT(ret == RT_EOK);
    }

    return (int)ret;
}
INIT_BOARD_EXPORT(rt_hw_sdh_init);

#if defined(NU_SDH_HOTPLUG)
static rt_bool_t nu_sdh_hotplug_is_mounted(const char *mounting_path)
{
    rt_bool_t ret = RT_FALSE;

#if defined(RT_USING_DFS)

    struct dfs_filesystem *psFS = dfs_filesystem_lookup(mounting_path);
    if (psFS == RT_NULL)
    {
        goto exit_nu_sdh_hotplug_is_mounted;
    }
    else if (!rt_memcmp(psFS->path, mounting_path, rt_strlen(mounting_path)))
    {
        ret = RT_TRUE;
    }
    else
    {
        ret = RT_FALSE;
    }

#endif

exit_nu_sdh_hotplug_is_mounted:

    return ret;
}
static rt_err_t nu_sdh_hotplug_mount(nu_sdh_t sdh)
{
    rt_err_t ret = RT_ERROR;
    DIR *t;

#if defined(RT_USING_DFS)

    if (nu_sdh_hotplug_is_mounted(sdh->mounted_point) == RT_TRUE)
    {
        ret = RT_EOK;
        goto exit_nu_sdh_hotplug_mount;
    }

    /* Check the SD folder path is valid. */
    if ((t =  opendir(sdh->mounted_point)) != RT_NULL)
    {
        closedir(t);
    }
    else
    {

        /* Check the ROOT path is valid. */
        if ((t =  opendir(NU_SDH_MOUNTPOINT_ROOT)) != RT_NULL)
        {
            closedir(t);
        }
        else if ((ret = mkdir(NU_SDH_MOUNTPOINT_ROOT, 0)) != RT_EOK)
        {
            rt_kprintf("Failed to mkdir %s\n", NU_SDH_MOUNTPOINT_ROOT);
            goto exit_nu_sdh_hotplug_mount;
        }

        if ((ret = mkdir(sdh->mounted_point, 0)) != RT_EOK)
        {
            rt_kprintf("Failed to mkdir %s\n", sdh->mounted_point);
            goto exit_nu_sdh_hotplug_mount;
        }

    } //else

    if ((ret = dfs_mount(sdh->name, sdh->mounted_point, "elm", 0, 0)) == 0)
    {
        rt_kprintf("Mounted %s on %s\n", sdh->name, sdh->mounted_point);
    }
    else
    {
        rt_kprintf("Failed to mount %s on %s\n", sdh->name, sdh->mounted_point);
        ret = RT_ERROR;
    }

exit_nu_sdh_hotplug_mount:

#endif
    return -(ret);
}

static rt_err_t nu_sdh_hotplug_unmount(nu_sdh_t sdh)
{
    rt_err_t ret = RT_ERROR;

#if defined(RT_USING_DFS)
    if (nu_sdh_hotplug_is_mounted(sdh->mounted_point) == RT_FALSE)
    {
        ret = RT_EOK;
        goto exit_nu_sdh_hotplug_unmount;
    }

    ret = dfs_unmount(sdh->mounted_point);
    if (ret != RT_EOK)
    {
        rt_kprintf("Failed to unmount %s.\n", sdh->mounted_point);
    }
    else
    {
        rt_kprintf("Succeed to unmount %s.\n", sdh->mounted_point);
        ret = RT_EOK;
    }
#endif

exit_nu_sdh_hotplug_unmount:

    return -(ret);
}
static void sdh_hotplugger(void *param)
{
    rt_uint32_t e;
    int i;

    for (i = (SDH_START + 1); i < SDH_CNT; i++)
    {
        if (SDH_IS_CARD_PRESENT(nu_sdh_arr[i].base))
        {
            nu_sdh_hotplug_mount(&nu_sdh_arr[i]);
        }
    }

    while (1)
    {
        if (rt_event_recv(&sdh_event, (NU_SDH_CARD_EVENT_ALL),
                          RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                          RT_WAITING_FOREVER, &e) == RT_EOK)
        {
            /* Debouce */
            rt_thread_delay(200);
            switch (e)
            {
#if defined(BSP_USING_SDH0)
            case NU_SDH_CARD_INSERTED_SD0:
                nu_sdh_hotplug_mount(&nu_sdh_arr[SDH0_IDX]);
                break;
            case NU_SDH_CARD_REMOVED_SD0:
                nu_sdh_hotplug_unmount(&nu_sdh_arr[SDH0_IDX]);
                break;
#endif
#if defined(BSP_USING_SDH1)
            case NU_SDH_CARD_INSERTED_SD1:
                nu_sdh_hotplug_mount(&nu_sdh_arr[SDH1_IDX]);
                break;
            case NU_SDH_CARD_REMOVED_SD1:
                nu_sdh_hotplug_unmount(&nu_sdh_arr[SDH1_IDX]);
                break;
#endif
            default:
                break;

            } //switch(e)

        } //if

    } /* while(1) */
}

int mnt_init_sdcard_hotplug(void)
{
    rt_thread_init(&sdh_tid, "hotplug", sdh_hotplugger, NULL, sdh_stack, sizeof(sdh_stack), RT_THREAD_PRIORITY_MAX - 2, 10);
    rt_thread_startup(&sdh_tid);

    return 0;
}
INIT_ENV_EXPORT(mnt_init_sdcard_hotplug);
#endif

#endif //#if defined(BSP_USING_SDH)
